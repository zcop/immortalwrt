#!/usr/bin/env python3
"""Exercise the yt921x queue-table index mapping with production code.

The queue/group macros and capacity constants are extracted from
yt921x_enhanced.h; the queue-index helper and ETS apply function come from
yt921x_qos_tc.c. Independent literal oracles cover modeled queue indexes and
ETS register destinations. Named fake ETS dependencies do not model hardware
scheduling or validate Linux band-to-queue policy.

Usage:
    tools/nss/test_yt921x_queue_index.py
    tools/nss/test_yt921x_queue_index.py --source old.c
    tools/nss/test_yt921x_queue_index.py --sanitize address,undefined,leak

--source can test the old unicast index helper against the current header;
the old helper compiles and fails the independent port/queue oracles.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path


class HarnessError(RuntimeError):
    pass


def _skip_literal(src: str, pos: int) -> int:
    quote = src[pos]
    pos += 1
    while pos < len(src):
        if src[pos] == "\\":
            pos += 2
            continue
        if src[pos] == quote:
            return pos + 1
        pos += 1
    raise HarnessError("unterminated literal in source")


def _match_delim(src: str, pos: int, opening: str, closing: str) -> int:
    depth = 0
    while pos < len(src):
        ch = src[pos]
        if ch == "/" and src.startswith("/*", pos):
            end = src.find("*/", pos + 2)
            if end < 0:
                raise HarnessError("unterminated comment in source")
            pos = end + 2
            continue
        if ch == "/" and src.startswith("//", pos):
            end = src.find("\n", pos)
            pos = len(src) if end < 0 else end
            continue
        if ch in ('"', "'"):
            pos = _skip_literal(src, pos)
            continue
        if ch == opening:
            depth += 1
        elif ch == closing:
            depth -= 1
            if depth == 0:
                return pos
        pos += 1
    raise HarnessError(f"unbalanced {opening}{closing} in source")


def extract_define(src: str, name: str) -> str:
    match = re.search(
        rf"(?m)^[ \t]*#define[ \t]+{re.escape(name)}(?=[ \t(]|$)", src
    )
    if not match:
        raise HarnessError(f"define {name} not found in header")
    end = match.end()
    while True:
        newline = src.find("\n", end)
        if newline < 0:
            return src[match.start():]
        if src[newline - 1] == "\\":
            end = newline + 1
            continue
        return src[match.start():newline]


def extract_function(src: str, name: str) -> str:
    match = re.search(rf"(?m)^[ \t]*{re.escape(name)}\s*\(", src)
    if not match:
        match = re.search(
            rf"(?m)^[ \t]*(?:static\s+)?[A-Za-z_][\w \t\*]*?\b{re.escape(name)}\s*\(",
            src,
        )
    if not match:
        raise HarnessError(f"function {name} not found in source")
    start = match.start()
    name_pos = src.find(name, match.start(), match.end())
    if name_pos >= 0 and not src[start:name_pos].strip():
        # The return type sits on the previous line.
        while start > 0:
            prev_start = src.rfind("\n", 0, start - 1) + 1
            prev_line = src[prev_start:start - 1].strip()
            if not re.fullmatch(r"(?:static\s+)?[A-Za-z_][\w \t\*]*",
                                prev_line):
                break
            start = prev_start
    paren = src.find("(", match.start(), match.end())
    close_paren = _match_delim(src, paren, "(", ")")
    brace = close_paren + 1
    while brace < len(src) and src[brace].isspace():
        brace += 1
    if brace >= len(src) or src[brace] != "{":
        raise HarnessError(f"{name} is not a function definition")
    end = _match_delim(src, brace, "{", "}")
    return src[start:end + 1]


def sanitizer_flags(spec: str | None) -> tuple[list[str], dict[str, str]]:
    if not spec:
        return [], {}
    names = {name.strip().lower() for name in spec.split(",") if name.strip()}
    if not names:
        return [], {}
    known = {"address", "asan", "undefined", "ubsan", "leak", "lsan"}
    unknown = names - known
    if unknown:
        raise HarnessError(
            f"unknown sanitizer(s): {', '.join(sorted(unknown))}"
        )
    flags = ["-g", "-fno-omit-frame-pointer"]
    env: dict[str, str] = {}
    if names & {"address", "asan"}:
        flags.append("-fsanitize=address")
        env["ASAN_OPTIONS"] = "detect_leaks=1"
    elif names & {"leak", "lsan"}:
        flags.append("-fsanitize=leak")
    if names & {"undefined", "ubsan"}:
        flags.append("-fsanitize=undefined")
    return flags, env


DEFINES = (
    "YT921X_QOS_SCHED_PORTS",
    "YT921X_QOS_SCHED_UCAST_FLOWS",
    "YT921X_QOS_SCHED_MCAST_FLOWS",
    "YT921X_QOS_SCHED_FLOWS_PER_PORT",
    "YT921X_QOS_QUEUE_FLOW_INDEX",
    "YT921X_QOS_SCHED_FLOW_INDEX",
    "YT921X_PRIO_NUM",
    "YT921X_QOS_SCHED_SP",
    "YT921X_QOS_SCHED_SPn",
    "YT921X_QOS_SCHED_SP_MASK",
    "YT921X_QOS_SCHED_DWRR",
    "YT921X_QOS_SCHED_DWRRn",
    "YT921X_QOS_SCHED_DWRR_MODE0",
    "YT921X_QOS_SCHED_DWRR_MODE0n",
    "YT921X_QOS_SCHED_DWRR_MODE1",
    "YT921X_QOS_SCHED_DWRR_MODE1n",
    "YT921X_QOS_SCHED_FLOW_F0_M",
    "YT921X_QOS_SCHED_FLOW_F1_M",
    "YT921X_QOS_SCHED_FLOW_F0",
    "YT921X_QOS_SCHED_FLOW_F1",
    "YT921X_QOS_SCHED_DWRR_CFG_EN",
    "YT921X_QSCH_SHP_PORTS",
    "YT921X_QSCH_SHP_QUEUES",
    "YT921X_QSCH_SHP_FLOWS_PER_PORT",
    "YT921X_QSCH_SHP_ENTRIES",
)

PREAMBLE = r"""/* generated by test_yt921x_queue_index.py; do not edit */
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned int u32;

#define ARRAY_SIZE(a) ((unsigned int)(sizeof(a) / sizeof((a)[0])))
#define BIT(n) (1U << (n))
#define GENMASK(h, l) ((~0U << (l)) & (~0U >> (31 - (h))))
#define FIELD_PREP(mask, value) (((u32)(value) << __builtin_ctz(mask)) & (mask))
#define min_t(type, a, b) ((type)(a) < (type)(b) ? (type)(a) : (type)(b))

"""

STATIC_ASSERTS = r"""
_Static_assert(YT921X_QSCH_SHP_ENTRIES == 132,
	       "queue-table capacity must stay 132");
_Static_assert(YT921X_QOS_SCHED_FLOWS_PER_PORT == 12,
	       "per-port DWRR group count stays 12");
_Static_assert(YT921X_QOS_SCHED_PORTS == 11, "expected 11 modeled ports");
_Static_assert(YT921X_QOS_SCHED_UCAST_FLOWS == 8, "expected 8 UC queues");
_Static_assert(YT921X_QOS_SCHED_MCAST_FLOWS == 4, "expected 4 MC queues");

"""

ETS_DEPENDENCIES = r"""
/* Named fakes: feed a precomputed band map/weights into the real apply body. */
struct update { u32 addr, mask, value; };
struct yt921x_priv {
    struct update writes[32];
    int calls, fail_at, map_error;
};
struct tc_ets_qopt_offload_replace_params {
    s8 band[8];
    u32 weight[8];
};
static int yt921x_qos_read_ucast_prio_qid_map(struct yt921x_priv *p,
                                            int port, u8 *map)
{
    for (int i = 0; i < 8; i++) map[i] = i;
    return p->map_error;
}
static int yt921x_ets_build_qid_band_map(
    const struct tc_ets_qopt_offload_replace_params *p,
    const u8 *map, s8 *out)
{
    memcpy(out, p->band, sizeof(p->band));
    return 0;
}
static int yt921x_ets_band_weight(
    const struct tc_ets_qopt_offload_replace_params *p, int band, u32 *out)
{
    *out = p->weight[band];
    return 0;
}
static int yt921x_reg_update_bits(struct yt921x_priv *p,
                                  u32 addr, u32 mask, u32 value)
{
    if (p->calls >= 32) abort();
    p->writes[p->calls++] = (struct update){ addr, mask, value };
    return p->calls == p->fail_at ? -EIO : 0;
}
"""

ETS_TESTS = r"""
static void check_ets_update(const struct yt921x_priv *p, int *n,
                             u32 addr, u32 mask, u32 value)
{
    const struct update *u = &p->writes[(*n)++];
    if (u->addr != addr || u->mask != mask || u->value != value)
        fail("ETS update %d: %x/%x/%x, expected %x/%x/%x",
             *n, u->addr, u->mask, u->value, addr, mask, value);
}

static void test_ets_table_destinations(void)
{
    for (int port = 0; port < 11; port++) {
        for (int mixed = 0; mixed < 2; mixed++) {
            struct yt921x_priv priv = { 0 };
            struct tc_ets_qopt_offload_replace_params p = { 0 };
            int n = 0;

            for (int q = 0; q < 8; q++) {
                p.band[q] = mixed && q == 7 ? -1 : q;
                p.weight[q] = mixed && !(q & 1) ? 0 : q + 1;
            }
            if (yt921x_ets_apply(&priv, port, &p))
                fail("ETS apply failed port%d mixed%d", port, mixed);
            for (int q = 0; q < 8; q++) {
                u32 en = 0, w = p.weight[q];
                if (p.band[q] >= 0 && w) {
                    /* Independent literals: packed flow stride 8. */
                    check_ets_update(&priv, &n, 0x341000 + 4*(port*8+q),
                                     0x0fffff00, (w << 18) | (w << 8));
                    en = 1;
                }
                /* Preserve baseline mode traversal, stride12 plus qid.
                 * Priority-group resolution is separate hardware coverage. */
                check_ets_update(&priv, &n, 0x342000 + 4*(port*12+q), 1, en);
                check_ets_update(&priv, &n, 0x343000 + 4*(port*12+q), 1, en);
            }
            /* The existing trailing SP write is opaque here. Vendor source
             * identifies 0x300400 as flush_cfg, so do not turn that legacy
             * assumption into a hardware oracle. Its redesign is separate. */
            if (priv.calls != n + 1) fail("ETS unexpected update count");
            n = priv.calls;

            /* Each I/O error stops at that attempted operation, with no
             * rollback claim for this existing ETS apply implementation. */
            for (int fail_at = 1; fail_at <= n; fail_at++) {
                struct yt921x_priv fault = { .fail_at = fail_at };
                if (yt921x_ets_apply(&fault, port, &p) != -EIO ||
                    fault.calls != fail_at ||
                    memcmp(fault.writes, priv.writes,
                           fail_at * sizeof(struct update)))
                    fail("ETS I/O prefix port%d operation%d", port, fail_at);
            }
        }
    }
    struct yt921x_priv fault = { .map_error = -EUCLEAN };
    struct tc_ets_qopt_offload_replace_params p = { 0 };
    if (yt921x_ets_apply(&fault, 2, &p) != -EUCLEAN || fault.calls)
        fail("ETS map-read failure must not write registers");
}
"""

TESTS = r"""
#define SENTINEL 0xdeadbeefU

static int failures;

static void fail(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "FAIL: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	failures++;
}

struct boundary {
	int port;
	u32 uc[YT921X_QOS_SCHED_UCAST_FLOWS];
	u32 mc[YT921X_QOS_SCHED_MCAST_FLOWS];
};

static const struct boundary boundaries[] = {
	{ 0, { 0, 1, 2, 3, 4, 5, 6, 7 }, { 88, 89, 90, 91 } },
	{ 2, { 16, 17, 18, 19, 20, 21, 22, 23 }, { 96, 97, 98, 99 } },
	{ 8, { 64, 65, 66, 67, 68, 69, 70, 71 }, { 120, 121, 122, 123 } },
	{ 10, { 80, 81, 82, 83, 84, 85, 86, 87 }, { 128, 129, 130, 131 } },
};

static void test_macro_layout(void)
{
	unsigned char seen[YT921X_QSCH_SHP_ENTRIES] = { 0 };
	u32 port;
	u32 qid;
	u32 i;

	for (port = 0; port < YT921X_QOS_SCHED_PORTS; port++) {
		for (qid = 0; qid < YT921X_QOS_SCHED_UCAST_FLOWS; qid++) {
			u32 idx = YT921X_QOS_QUEUE_FLOW_INDEX(port, qid, false);
			u32 want = port * 8U + qid;

			if (idx != want) {
				fail("UC macro p%u q%u -> %u, expected %u",
				     port, qid, idx, want);
			} else if (idx >= 88U) {
				fail("UC macro p%u q%u -> %u outside 0..87",
				     port, qid, idx);
			} else {
				seen[idx]++;
			}
		}

		for (qid = 0; qid < YT921X_QOS_SCHED_MCAST_FLOWS; qid++) {
			u32 idx = YT921X_QOS_QUEUE_FLOW_INDEX(port, qid, true);
			u32 want = 88U + port * 4U + qid;

			if (idx != want) {
				fail("MC macro p%u q%u -> %u, expected %u",
				     port, qid, idx, want);
			} else if (idx < 88U) {
				fail("MC macro p%u q%u -> %u entered 0..87",
				     port, qid, idx);
			} else if (idx >= YT921X_QSCH_SHP_ENTRIES) {
				fail("MC macro p%u q%u -> %u beyond capacity",
				     port, qid, idx);
			} else {
				seen[idx]++;
			}
		}
	}

	for (i = 0; i < YT921X_QSCH_SHP_ENTRIES; i++) {
		if (seen[i] != 1)
			fail("index %u covered %u times, expected once",
			     i, (unsigned int)seen[i]);
	}
}

static void test_literal_boundaries(void)
{
	unsigned int i;
	u32 qid;

	for (i = 0; i < ARRAY_SIZE(boundaries); i++) {
		int port = boundaries[i].port;

		for (qid = 0; qid < YT921X_QOS_SCHED_UCAST_FLOWS; qid++) {
			u32 idx = YT921X_QOS_QUEUE_FLOW_INDEX(port, qid, false);

			if (idx != boundaries[i].uc[qid])
				fail("UC boundary p%d q%u -> %u, expected %u",
				     port, qid, idx, boundaries[i].uc[qid]);
		}

		for (qid = 0; qid < YT921X_QOS_SCHED_MCAST_FLOWS; qid++) {
			u32 idx = YT921X_QOS_QUEUE_FLOW_INDEX(port, qid, true);

			if (idx != boundaries[i].mc[qid])
				fail("MC boundary p%d q%u -> %u, expected %u",
				     port, qid, idx, boundaries[i].mc[qid]);
		}
	}
}

static void test_function_mapping(void)
{
	u32 port;
	u32 qid;

	for (port = 0; port < YT921X_QSCH_SHP_PORTS; port++) {
		for (qid = 0; qid < YT921X_QSCH_SHP_QUEUES; qid++) {
			u32 idx = SENTINEL;
			u32 want = port * 8U + qid;
			int res = yt921x_qsch_flow_index((int)port, (u8)qid,
							 &idx);

			if (res)
				fail("flow_index p%u q%u returned %d",
				     port, qid, res);
			else if (idx != want)
				fail("flow_index p%u q%u -> %u, expected %u",
				     port, qid, idx, want);
		}
	}
}

static void expect_invalid(const char *label, int port, u8 qid)
{
	u32 idx = SENTINEL;
	int res = yt921x_qsch_flow_index(port, qid, &idx);

	if (res != -ERANGE)
		fail("%s: returned %d, expected %d", label, res, -ERANGE);
	if (idx != SENTINEL)
		fail("%s: changed output to 0x%08x", label, idx);
}

static void test_invalid_inputs(void)
{
	expect_invalid("port -1", -1, 0);
	expect_invalid("port 11", 11, 0);
	expect_invalid("qid 8 p0", 0, 8);
	expect_invalid("qid 8 p10", 10, 8);
	expect_invalid("port -1 qid 8", -1, 8);
	expect_invalid("port 11 qid 8", 11, 8);
}

int main(void)
{
	test_macro_layout();
	test_literal_boundaries();
	test_function_mapping();
	test_invalid_inputs();

	if (failures) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}

	printf("PASS: yt921x queue-index layout\n");
	return 0;
}
"""


def compose(header: Path, source: Path) -> str:
    header_src = header.read_text()
    source_src = source.read_text()
    defines = "\n".join(extract_define(header_src, name) for name in DEFINES)
    function = extract_function(source_src, "yt921x_qsch_flow_index")
    ets = extract_function(source_src, "yt921x_ets_apply")
    tests = TESTS.replace("int main(void)", ETS_TESTS + "\nint main(void)")
    tests = tests.replace("\ttest_invalid_inputs();",
                          "\ttest_invalid_inputs();\n\ttest_ets_table_destinations();")
    return (
        PREAMBLE
        + defines
        + "\n"
        + STATIC_ASSERTS
        + function
        + "\n"
        + ETS_DEPENDENCIES
        + ets
        + "\n"
        + tests
    )


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--header",
        type=Path,
        default=root / "target/linux/generic/files/drivers/net/dsa/yt921x_enhanced.h",
        help="yt921x_enhanced.h to extract the macro/constants from",
    )
    parser.add_argument(
        "--source",
        type=Path,
        default=root / "target/linux/generic/files/drivers/net/dsa/yt921x_qos_tc.c",
        help="yt921x_qos_tc.c to extract yt921x_qsch_flow_index() from",
    )
    parser.add_argument("--cc", default="cc", help="C compiler to use")
    parser.add_argument(
        "--sanitize",
        nargs="?",
        const="address,undefined,leak",
        metavar="LIST",
        help="comma-separated sanitizers: address/asan, undefined/ubsan, "
             "leak/lsan (bare flag enables all; ASan includes LSan)",
    )
    args = parser.parse_args()

    try:
        program = compose(args.header, args.source)
        extra_flags, extra_env = sanitizer_flags(args.sanitize)
    except (HarnessError, OSError) as err:
        print(f"harness: {err}", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="yt921x-queue-index-") as tmp:
        c_file = Path(tmp) / "test.c"
        binary = Path(tmp) / "test"
        c_file.write_text(program)
        build = subprocess.run(
            [args.cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror",
             "-Wno-sign-compare", "-Wno-unused-parameter", "-Wno-unused-function", "-O0",
             *extra_flags, "-o", str(binary), str(c_file)],
            capture_output=True,
            text=True,
        )
        if build.returncode:
            print(build.stdout, end="", file=sys.stderr)
            print(build.stderr, end="", file=sys.stderr)
            return 2
        run = subprocess.run(
            [str(binary)], capture_output=True, text=True,
            env={**os.environ, **extra_env},
        )
        print(run.stdout, end="")
        print(run.stderr, end="", file=sys.stderr)
        return run.returncode


if __name__ == "__main__":
    sys.exit(main())
