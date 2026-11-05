#!/usr/bin/env python3
"""Behavioral test for the yt921x ingress meter profile helper."""

import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORT_C = ROOT / "target/linux/generic/files/drivers/net/dsa/yt921x_port.c"
REG96 = ROOT / "target/linux/generic/files/drivers/net/dsa/yt921x_core.c"


def extract(src, name):
    match = re.search(r"(?:static\s+)?int\s+" + re.escape(name) + r"\s*\(", src)
    if not match:
        raise SystemExit(f"missing function: {name}")
    brace = src.index("{", match.end())
    depth = 0
    for pos in range(brace, len(src)):
        if src[pos] == "{":
            depth += 1
        elif src[pos] == "}":
            depth -= 1
            if not depth:
                return src[match.start():pos + 1]
    raise SystemExit(f"unterminated function: {name}")


def main():
    source = PORT_C.read_text()
    helpers = "\n\n".join([
        extract(REG96.read_text(), "yt921x_reg96_write"),
        extract(source, "yt921x_stock_ingress_rate_to_cir"),
        extract(source, "yt921x_ingress_meter_profile_apply"),
    ])

    harness = r"""
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#define U64_MAX UINT64_MAX
#include <stdio.h>
#include <string.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;

#define BIT(n) (1U << (n))
#define GENMASK_ULL(h, l) (((~0ULL) << (l)) & (~0ULL >> (63 - (h))))
#define FIELD_PREP(mask, val) ((mask) & ((typeof(mask))(val) << __builtin_ctzll(mask)))
#define FIELD_GET(mask, val) (((val) & (mask)) >> __builtin_ctzll(mask))
#define div_u64(a, b) ((a) / (b))

#define YT921X_RATE_SCALE_PER_SEC 1000000000ULL
#define YT921X_RATE_CIR_MAX GENMASK_ULL(17, 0)
#define YT921X_RATE_TOKEN_UNIT_DEFAULT 5U
#define YT921X_RATE_IGR_METER_BASE 64U
#define YT921X_RATE_METER_CONFIGn(n) (0x220800U + 0x10U * (n))
#define YT921X_RATE_METER_CONFIG_WORD1(n) (YT921X_RATE_METER_CONFIGn(n) + 0x4U)
#define YT921X_RATE_METER_CONFIG_WORD2(n) (YT921X_RATE_METER_CONFIGn(n) + 0x8U)
#define YT921X_RATE_METER_CFG_CIR_M GENMASK_ULL(19, 2)
#define YT921X_RATE_METER_CFG_CIR(x) FIELD_PREP(YT921X_RATE_METER_CFG_CIR_M, (x))
#define YT921X_RATE_METER_CFG_CBS_M GENMASK_ULL(31, 20)
#define YT921X_RATE_METER_CFG_F4_M GENMASK_ULL(9, 7)
#define YT921X_RATE_METER_CFG_F4(x) FIELD_PREP(YT921X_RATE_METER_CFG_F4_M, (x))
#define YT921X_RATE_METER_CFG_F6 BIT(5)
#define YT921X_METER_CTRLc_METER_EN BIT(14)
#define YT921X_METER_CTRLc_COLOR_BLIND BIT(10)
#define YT921X_METER_CTRLc_DROP_M GENMASK_ULL(12, 11)
#define YT921X_METER_CTRLc_DROP_YR FIELD_PREP(YT921X_METER_CTRLc_DROP_M, 1)
#define YT921X_METER_CTRLc_PKT_MODE BIT(5)
#define YT921X_METER_CTRLc_RFC2698 BIT(4)
#define YT921X_METER_CTRLbc_CBS_M GENMASK_ULL(35, 20)
#define YT921X_METER_CTRLbc_CBS(x) FIELD_PREP(YT921X_METER_CTRLbc_CBS_M, (u64)(x))
#define YT921X_METER_BURST_MAX ((1U << 16) - 1)

struct yt921x_priv { int unused; };

#define METER_ID 0U
#define METER_BASE YT921X_RATE_METER_CONFIGn(YT921X_RATE_IGR_METER_BASE + METER_ID)
#define W0_INIT 0xdeadbeefU
#define W1_INIT (0x3U | (0x15555U << 2) | (0xabcU << 20))
#define W2_COMMON (BIT(13) | BIT(15) | BIT(31) | BIT(6) | BIT(4) | BIT(5) | (2U << 11))
#define W2_F4_5 (W2_COMMON | (5U << 7) | 0xaU)
#define W2_F4_0 (W2_COMMON | 0xaU)

static u32 committed[3], pending[3];
static int pending_mask, commits, nwrites, nreads;
static int seen[3], all_read_before_write;
static int fail_read_off = -1, fail_write_off = -1;

static int reg_offset(u32 reg, int *off)
{
	if (reg < METER_BASE || reg > METER_BASE + 8U)
		return -EINVAL;
	*off = (int)(reg - METER_BASE);
	return (*off & 3) ? -EINVAL : 0;
}

static int yt921x_reg_read(struct yt921x_priv *priv, u32 reg, u32 *valp)
{
	int off, res;

	(void)priv;
	res = reg_offset(reg, &off);
	if (res)
		return res;
	if (off == fail_read_off)
		return -EIO;
	*valp = committed[off / 4];
	seen[off / 4] = 1;
	nreads++;
	return 0;
}

static int yt921x_reg_write(struct yt921x_priv *priv, u32 reg, u32 val)
{
	int off, res;

	(void)priv;
	res = reg_offset(reg, &off);
	if (res)
		return res;
	if (off == fail_write_off)
		return -EIO;
	if (!nwrites)
		all_read_before_write = seen[0] && seen[1] && seen[2];
	nwrites++;
	pending[off / 4] = val;
	pending_mask |= 1 << (off / 4);
	if (pending_mask == 7) {
		memcpy(committed, pending, sizeof(committed));
		pending_mask = 0;
		commits++;
	}
	return 0;
}

/* @@HELPERS@@ */

static int failures;
#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		failures++; \
	} \
} while (0)

static void check_eq(const char *what, u32 got, u32 want, int line)
{
	if (got != want) {
		fprintf(stderr, "FAIL %s:%d: %s got 0x%08x want 0x%08x\n",
			__FILE__, line, what, got, want);
		failures++;
	}
}
#define CHECK_EQ(got, want) check_eq(#got, (u32)(got), (u32)(want), __LINE__)

static void reset_io(void)
{
	memset(pending, 0, sizeof(pending));
	pending_mask = commits = nwrites = nreads = 0;
	memset(seen, 0, sizeof(seen));
	all_read_before_write = 0;
	fail_read_off = fail_write_off = -1;
}

static void reset_regs(u32 w0, u32 w1, u32 w2)
{
	reset_io();
	committed[0] = w0;
	committed[1] = w1;
	committed[2] = w2;
}

static int apply(u64 rate, u32 burst)
{
	struct yt921x_priv priv = {};

	return yt921x_ingress_meter_profile_apply(&priv, 64000U, METER_ID,
						   rate, burst);
}

static u32 expected_w1(u32 cir, u32 cbs)
{
	return (W1_INIT & 0x3U) | (u32)YT921X_RATE_METER_CFG_CIR(cir) |
	       (((cbs & 0xfffU) << 20) & (u32)YT921X_RATE_METER_CFG_CBS_M);
}

static u32 expected_w2(u32 unit, u32 cbs)
{
	return BIT(13) | BIT(15) | BIT(31) | BIT(6) |
	       YT921X_METER_CTRLc_COLOR_BLIND | YT921X_METER_CTRLc_DROP_YR |
	       YT921X_METER_CTRLc_METER_EN | ((unit & 7U) << 7) |
	       ((cbs >> 12) & 0xfU);
}

static void check_profile(u32 unit, u32 cir, u32 cbs)
{
	CHECK_EQ(committed[0], W0_INIT);
	CHECK_EQ(committed[1], expected_w1(cir, cbs));
	CHECK_EQ(committed[2], expected_w2(unit, cbs));
	CHECK_EQ(nreads, 3);
	CHECK_EQ(nwrites, 3);
	CHECK_EQ(commits, 1);
	CHECK(all_read_before_write);
}

static void test_rate_conversion(void)
{
	u32 cir = 0;

	CHECK_EQ(yt921x_stock_ingress_rate_to_cir(1000000ULL, 64000U, 5U,
						 false, &cir), 0);
	CHECK_EQ(cir, 1024);
	CHECK_EQ(yt921x_stock_ingress_rate_to_cir(12500000ULL, 64000U, 5U,
						 false, &cir), 0);
	CHECK_EQ(cir, 12800);
	CHECK_EQ(yt921x_stock_ingress_rate_to_cir(1ULL, 64000U, 5U,
						 false, &cir), 0);
	CHECK_EQ(cir, 1);
	CHECK_EQ(yt921x_stock_ingress_rate_to_cir(0, 64000U, 5U, false, &cir),
		 -EINVAL);
	CHECK_EQ(yt921x_stock_ingress_rate_to_cir(UINT64_MAX, 64000U, 5U,
						 false, &cir), -ERANGE);
}

static void test_cbs_profiles(void)
{
	reset_regs(W0_INIT, W1_INIT, W2_F4_5);
	CHECK_EQ(apply(1000000ULL, 1024U * 1024U), 0);
	check_profile(5, 1024, 512);

	reset_regs(W0_INIT, W1_INIT, W2_F4_0);
	CHECK_EQ(apply(1000000ULL, 64U * 1024U), 0);
	check_profile(5, 1024, 32);

	reset_regs(W0_INIT, W1_INIT, W2_F4_5);
	CHECK_EQ(apply(1000000ULL, 1U), 0);
	check_profile(5, 1024, 1);
}

static void test_cbs_split_boundary(void)
{
	static const u32 cbs_cases[] = { 4095, 4096, 65535 };
	size_t i;

	for (i = 0; i < sizeof(cbs_cases) / sizeof(cbs_cases[0]); i++) {
		u32 cbs = cbs_cases[i];

		reset_regs(W0_INIT, W1_INIT, W2_F4_5);
		CHECK_EQ(apply(1000000ULL, cbs << 11), 0);
		check_profile(5, 1024, cbs);
		CHECK_EQ((committed[1] >> 20) & 0xfffU, cbs & 0xfffU);
		CHECK_EQ(committed[2] & 0xfU, cbs >> 12);
	}
}

static void test_reject_without_writes(void)
{
	reset_regs(W0_INIT, W1_INIT, W2_F4_5);
	CHECK_EQ(apply(1000000ULL, 0), -EINVAL);
	CHECK_EQ(nwrites, 0);
	CHECK_EQ(commits, 0);
	CHECK_EQ(committed[0], W0_INIT);
	CHECK_EQ(committed[1], W1_INIT);
	CHECK_EQ(committed[2], W2_F4_5);

	reset_regs(W0_INIT, W1_INIT, W2_F4_5);
	CHECK_EQ(apply(1000000ULL, 65536U << 11), -ERANGE);
	CHECK_EQ(nwrites, 0);
	CHECK_EQ(commits, 0);
	CHECK_EQ(committed[2], W2_F4_5);
}

static void test_errors(void)
{
	static const int read_offs[] = { 0, 4, 8 };
	size_t i;

	for (i = 0; i < sizeof(read_offs) / sizeof(read_offs[0]); i++) {
		reset_regs(W0_INIT, W1_INIT, W2_F4_5);
		fail_read_off = read_offs[i];
		CHECK_EQ(apply(1000000ULL, 1024U * 1024U), -EIO);
		CHECK_EQ(nwrites, 0);
		CHECK_EQ(commits, 0);
	}

	for (i = 0; i < sizeof(read_offs) / sizeof(read_offs[0]); i++) {
		reset_regs(W0_INIT, W1_INIT, W2_F4_5);
		fail_write_off = read_offs[i];
		CHECK_EQ(apply(1000000ULL, 1024U * 1024U), -EIO);
		CHECK_EQ(nwrites, i);
		CHECK_EQ(commits, 0);
		CHECK_EQ(committed[0], W0_INIT);
		CHECK_EQ(committed[1], W1_INIT);
		CHECK_EQ(committed[2], W2_F4_5);
	}
}

int main(void)
{
	test_rate_conversion();
	test_cbs_profiles();
	test_cbs_split_boundary();
	test_reject_without_writes();
	test_errors();

	if (failures) {
		fprintf(stderr, "%d failure(s)\n", failures);
		return 1;
	}
	puts("ingress meter profile: PASS");
	return 0;
}
"""
    harness = harness.replace("/* @@HELPERS@@ */", helpers)
    with tempfile.TemporaryDirectory(prefix="yt921x-meter-") as tmp:
        c_file = Path(tmp) / "test.c"
        exe = Path(tmp) / "test"
        c_file.write_text(harness)
        subprocess.run(["gcc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                        "-O0", str(c_file), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
