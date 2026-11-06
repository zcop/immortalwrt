#!/usr/bin/env python3
"""Exercise the real yt921x per-VLAN FID/SVL C path against mocked hardware.

yt921x_vlan_add(), yt921x_vlan_del() and yt921x_proc_apply_vlan_fid_mode()
are extracted from the production sources and compiled unchanged.  A 64-bit
VLAN register model checks the emitted bytes, so production code that fails
to clear or set YT921X_VLAN_CTRL_SVL_EN fails this test.
"""

from __future__ import annotations

import os
import re
import shutil
import string
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DSA_DIR = ROOT / "target/linux/generic/files/drivers/net/dsa"
L2_C = DSA_DIR / "yt921x_l2.c"
DEBUGFS_C = DSA_DIR / "yt921x_debugfs.c"
SWITCH_H = DSA_DIR / "yt921x.h"

EXTRACTED = [
    (L2_C, "yt921x_vlan_fid_for_vid"),
    (L2_C, "yt921x_vlan_del"),
    (L2_C, "yt921x_vlan_add"),
    (DEBUGFS_C, "yt921x_proc_apply_vlan_fid_mode"),
]

HEADER_MACROS = [
    "YT921X_VID_UNAWARE",
    "YT921X_VLANn_CTRL",
    "YT921X_VLAN_CTRL_PORTS_SHIFT",
    "YT921X_VLAN_CTRL_UNTAG_PORTS_SHIFT",
    "YT921X_VLAN_CTRL_UNTAG_PORTn",
    "YT921X_VLAN_CTRL_STP_ID_M",
    "YT921X_VLAN_CTRL_STP_ID",
    "YT921X_VLAN_CTRL_SVL_EN",
    "YT921X_VLAN_CTRL_FID_M",
    "YT921X_VLAN_CTRL_FID",
    "YT921X_VLAN_CTRL_LEARN_DIS",
    "YT921X_VLAN_CTRL_PRIO_EN",
    "YT921X_VLAN_CTRL_PRIO_M",
    "YT921X_VLAN_CTRL_PRIO",
    "YT921X_VLAN_CTRL_PORTS_M",
    "YT921X_VLAN_CTRL_PORTn",
    "YT921X_VLAN_CTRL_BYPASS_1X_AC",
    "YT921X_VLAN_CTRL_METER_EN",
    "YT921X_VLAN_CTRL_METER_ID_M",
]


def _skip_line_comment(src: str, i: int) -> int:
    end = src.find("\n", i)
    return len(src) if end < 0 else end


def _skip_block_comment(src: str, i: int) -> int:
    end = src.find("*/", i + 2)
    if end < 0:
        raise RuntimeError("unterminated block comment")
    return end + 2


def _skip_literal(src: str, i: int) -> int:
    quote = src[i]
    i += 1
    while i < len(src):
        if src[i] == "\\":
            i += 2
            continue
        if src[i] == quote:
            return i + 1
        i += 1
    raise RuntimeError("unterminated literal")


def _skip_trivia(src: str, i: int) -> int:
    while i < len(src):
        if src[i].isspace():
            i += 1
        elif src.startswith("//", i):
            i = _skip_line_comment(src, i)
        elif src.startswith("/*", i):
            i = _skip_block_comment(src, i)
        else:
            break
    return i


def _match_pair(src: str, i: int, opening: str, closing: str) -> int:
    if src[i] != opening:
        raise RuntimeError(f"expected {opening!r} at {i}")
    depth = 0
    while i < len(src):
        if src.startswith("//", i):
            i = _skip_line_comment(src, i)
            continue
        if src.startswith("/*", i):
            i = _skip_block_comment(src, i)
            continue
        if src[i] in ("'", '"'):
            i = _skip_literal(src, i)
            continue
        if src[i] == opening:
            depth += 1
        elif src[i] == closing:
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise RuntimeError(f"unterminated {opening}{closing} pair")


def _definition_start(src: str, name_at: int) -> int:
    line_start = src.rfind("\n", 0, name_at) + 1
    start = line_start
    type_line = re.compile(
        r"^(?:static\s+)?(?:inline\s+)?(?:const\s+)?(?:unsigned\s+)?"
        r"(?:void|int|u16|u32|bool|struct\s+\w+\s*\*?)\s*$"
    )
    while start > 0:
        prev_end = start - 1
        prev_start = src.rfind("\n", 0, prev_end) + 1
        if not type_line.match(src[prev_start:prev_end].strip()):
            break
        start = prev_start
    return start


def extract_function(src: str, name: str) -> str:
    for match in re.finditer(r"\b" + re.escape(name) + r"\b", src):
        i = _skip_trivia(src, match.end())
        if i >= len(src) or src[i] != "(":
            continue
        close = _match_pair(src, i, "(", ")")
        body = _skip_trivia(src, close + 1)
        if body >= len(src) or src[body] != "{":
            continue
        end = _match_pair(src, body, "{", "}")
        return src[_definition_start(src, match.start()):end + 1]
    raise RuntimeError(f"definition of {name} not found")


def extract_header_macros(src: str) -> str:
    seen = set()
    lines = []
    for line in src.splitlines():
        match = re.match(r"\s*#define\s+(\w+)\b", line)
        if match and match.group(1) in HEADER_MACROS:
            lines.append(line.strip())
            seen.add(match.group(1))
    missing = [name for name in HEADER_MACROS if name not in seen]
    if missing:
        raise RuntimeError(f"missing header macros: {', '.join(missing)}")
    return "\n".join(lines)


def extract_enum_value(src: str, name: str) -> int:
    match = re.search(
        r"\b" + re.escape(name) + r"\s*=\s*(0x[0-9a-fA-F]+|\d+)", src
    )
    if not match:
        raise RuntimeError(f"enum value {name} not found")
    return int(match.group(1), 0)


C_HARNESS = string.Template(
    r"""
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define BIT_ULL(n) (1ULL << (n))
#define GENMASK_ULL(h, l) (((~0ULL) << (l)) & (~0ULL >> (63 - (h))))
#define FIELD_PREP(mask, val) \
	((((u64)(val)) << __builtin_ctzll((u64)(mask))) & (u64)(mask))
#define FIELD_GET(mask, val) \
	((((u64)(val)) & (u64)(mask)) >> __builtin_ctzll((u64)(mask)))

$header_macros
#define YT921X_VLAN_FID_MODE_IVL 0
#define YT921X_VLAN_FID_MODE_SVL $svl_value

struct yt921x_priv {
	u8 vlan_fid_mode;
	u16 vlan_svl_fid;
};

#define VLAN_NUM 4096U
static u64 vlan_regs[VLAN_NUM];
static int write_count;
static int read_fail_vid = -1;
static int write_fail_vid = -1;
static int read_fail_err;
static int write_fail_err;
static int failures;

#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			fprintf(stderr, "FAIL:%d: %s\n", __LINE__, #cond); \
			failures++; \
		} \
	} while (0)

static int vid_of(u32 reg)
{
	u32 off;

	if (reg < YT921X_VLANn_CTRL(0))
		return -1;
	off = reg - YT921X_VLANn_CTRL(0);
	if (off % 8 || off / 8 >= VLAN_NUM)
		return -1;
	return (int)(off / 8);
}

int yt921x_reg64_read(struct yt921x_priv *priv, u32 reg, u64 *valp)
{
	int vid = vid_of(reg);

	(void)priv;
	if (vid < 0)
		return -EINVAL;
	if (vid == read_fail_vid)
		return read_fail_err < 0 ? read_fail_err : -EIO;
	*valp = vlan_regs[vid];
	return 0;
}

int yt921x_reg64_write(struct yt921x_priv *priv, u32 reg, u64 val)
{
	int vid = vid_of(reg);

	(void)priv;
	write_count++;
	if (vid < 0)
		return -EINVAL;
	if (vid == write_fail_vid)
		return write_fail_err < 0 ? write_fail_err : -EIO;
	vlan_regs[vid] = val;
	return 0;
}

int yt921x_reg64_update_bits(struct yt921x_priv *priv, u32 reg, u64 mask,
			     u64 val)
{
	u64 old;
	int res;

	res = yt921x_reg64_read(priv, reg, &old);
	if (res)
		return res;
	return yt921x_reg64_write(priv, reg, (old & ~mask) | (val & mask));
}

/* Real driver functions extracted from the production sources. */
$functions

#define UNRELATED \
	(YT921X_VLAN_CTRL_LEARN_DIS | YT921X_VLAN_CTRL_PRIO_EN | \
	 YT921X_VLAN_CTRL_PRIO(5) | YT921X_VLAN_CTRL_STP_ID(9) | \
	 YT921X_VLAN_CTRL_BYPASS_1X_AC | YT921X_VLAN_CTRL_METER_EN | \
	 (7ULL & YT921X_VLAN_CTRL_METER_ID_M))

static void reset_state(struct yt921x_priv *priv)
{
	memset(vlan_regs, 0, sizeof(vlan_regs));
	write_count = 0;
	read_fail_vid = -1;
	write_fail_vid = -1;
	priv->vlan_fid_mode = YT921X_VLAN_FID_MODE_IVL;
	priv->vlan_svl_fid = 0;
}

static void check_unrelated(u16 vid)
{
	CHECK((vlan_regs[vid] & UNRELATED) == UNRELATED);
}

static void test_add(struct yt921x_priv *priv)
{
	reset_state(priv);
	vlan_regs[100] = YT921X_VLAN_CTRL_SVL_EN |
			 YT921X_VLAN_CTRL_FID(300) | UNRELATED;
	CHECK(yt921x_vlan_add(priv, 3, 100, false) == 0);
	CHECK(!(vlan_regs[100] & YT921X_VLAN_CTRL_SVL_EN));
	CHECK(FIELD_GET(YT921X_VLAN_CTRL_FID_M, vlan_regs[100]) == 100);
	CHECK(vlan_regs[100] & YT921X_VLAN_CTRL_PORTn(3));
	CHECK(!(vlan_regs[100] & YT921X_VLAN_CTRL_UNTAG_PORTn(3)));
	check_unrelated(100);

	priv->vlan_fid_mode = YT921X_VLAN_FID_MODE_SVL;
	priv->vlan_svl_fid = 3088;
	vlan_regs[35] = UNRELATED;
	CHECK(yt921x_vlan_add(priv, 2, 35, true) == 0);
	CHECK(vlan_regs[35] & YT921X_VLAN_CTRL_SVL_EN);
	CHECK(FIELD_GET(YT921X_VLAN_CTRL_FID_M, vlan_regs[35]) == 3088);
	CHECK(vlan_regs[35] & YT921X_VLAN_CTRL_PORTn(2));
	CHECK(vlan_regs[35] & YT921X_VLAN_CTRL_UNTAG_PORTn(2));
	check_unrelated(35);

	priv->vlan_svl_fid = 0;
	vlan_regs[36] = 0;
	CHECK(yt921x_vlan_add(priv, 4, 36, false) == 0);
	CHECK(vlan_regs[36] & YT921X_VLAN_CTRL_SVL_EN);
	CHECK(FIELD_GET(YT921X_VLAN_CTRL_FID_M, vlan_regs[36]) == 1);
}

static void test_del(struct yt921x_priv *priv)
{
	reset_state(priv);
	vlan_regs[100] = YT921X_VLAN_CTRL_PORTn(3) |
			 YT921X_VLAN_CTRL_UNTAG_PORTn(3) |
			 YT921X_VLAN_CTRL_FID(100) |
			 YT921X_VLAN_CTRL_SVL_EN | UNRELATED;
	CHECK(yt921x_vlan_del(priv, 3, 100) == 0);
	CHECK(!(vlan_regs[100] & YT921X_VLAN_CTRL_PORTn(3)));
	CHECK(!(vlan_regs[100] & YT921X_VLAN_CTRL_UNTAG_PORTn(3)));
	CHECK(!(vlan_regs[100] & YT921X_VLAN_CTRL_FID_M));
	CHECK(!(vlan_regs[100] & YT921X_VLAN_CTRL_SVL_EN));
	check_unrelated(100);

	vlan_regs[101] = YT921X_VLAN_CTRL_PORTn(2) |
			 YT921X_VLAN_CTRL_PORTn(3) |
			 YT921X_VLAN_CTRL_FID(101) |
			 YT921X_VLAN_CTRL_SVL_EN;
	CHECK(yt921x_vlan_del(priv, 3, 101) == 0);
	CHECK(!(vlan_regs[101] & YT921X_VLAN_CTRL_PORTn(3)));
	CHECK(vlan_regs[101] & YT921X_VLAN_CTRL_PORTn(2));
	CHECK(FIELD_GET(YT921X_VLAN_CTRL_FID_M, vlan_regs[101]) == 101);
	CHECK(vlan_regs[101] & YT921X_VLAN_CTRL_SVL_EN);
}

static void test_vid0(struct yt921x_priv *priv)
{
	u64 base = YT921X_VLAN_CTRL_FID(77) | YT921X_VLAN_CTRL_SVL_EN |
		   UNRELATED;

	reset_state(priv);
	vlan_regs[0] = base;
	CHECK(yt921x_vlan_add(priv, 5, 0, true) == 0);
	CHECK(vlan_regs[0] & YT921X_VLAN_CTRL_PORTn(5));
	CHECK(vlan_regs[0] & YT921X_VLAN_CTRL_UNTAG_PORTn(5));
	CHECK((vlan_regs[0] & ~(YT921X_VLAN_CTRL_PORTn(5) |
				YT921X_VLAN_CTRL_UNTAG_PORTn(5))) == base);
	CHECK(yt921x_vlan_del(priv, 5, 0) == 0);
	CHECK(vlan_regs[0] == base);
}

static void test_apply(struct yt921x_priv *priv)
{
	static const u16 vids[] = { 10, 100, 4094 };
	u64 vid0 = YT921X_VLAN_CTRL_PORTn(7) | YT921X_VLAN_CTRL_FID(123) |
		   YT921X_VLAN_CTRL_SVL_EN | UNRELATED;
	u64 empty = YT921X_VLAN_CTRL_FID(555) | YT921X_VLAN_CTRL_SVL_EN;
	unsigned int i;

	reset_state(priv);
	for (i = 0; i < 3; i++)
		vlan_regs[vids[i]] = YT921X_VLAN_CTRL_PORTn(1) |
				     YT921X_VLAN_CTRL_FID(999) |
				     YT921X_VLAN_CTRL_SVL_EN | UNRELATED;
	vlan_regs[0] = vid0;
	vlan_regs[500] = empty;

	write_count = 0;
	CHECK(yt921x_proc_apply_vlan_fid_mode(priv) == 0);
	CHECK(write_count == 3);
	for (i = 0; i < 3; i++) {
		CHECK(FIELD_GET(YT921X_VLAN_CTRL_FID_M, vlan_regs[vids[i]]) ==
		      vids[i]);
		CHECK(!(vlan_regs[vids[i]] & YT921X_VLAN_CTRL_SVL_EN));
		CHECK(vlan_regs[vids[i]] & YT921X_VLAN_CTRL_PORTn(1));
		check_unrelated(vids[i]);
	}
	CHECK(vlan_regs[0] == vid0);
	CHECK(vlan_regs[500] == empty);

	priv->vlan_fid_mode = YT921X_VLAN_FID_MODE_SVL;
	priv->vlan_svl_fid = 3088;
	write_count = 0;
	CHECK(yt921x_proc_apply_vlan_fid_mode(priv) == 0);
	CHECK(write_count == 3);
	for (i = 0; i < 3; i++) {
		CHECK(FIELD_GET(YT921X_VLAN_CTRL_FID_M, vlan_regs[vids[i]]) ==
		      3088);
		CHECK(vlan_regs[vids[i]] & YT921X_VLAN_CTRL_SVL_EN);
	}
	CHECK(vlan_regs[0] == vid0);
	CHECK(vlan_regs[500] == empty);

	priv->vlan_fid_mode = YT921X_VLAN_FID_MODE_IVL;
	CHECK(yt921x_proc_apply_vlan_fid_mode(priv) == 0);
	for (i = 0; i < 3; i++) {
		CHECK(FIELD_GET(YT921X_VLAN_CTRL_FID_M, vlan_regs[vids[i]]) ==
		      vids[i]);
		CHECK(!(vlan_regs[vids[i]] & YT921X_VLAN_CTRL_SVL_EN));
	}

	priv->vlan_fid_mode = YT921X_VLAN_FID_MODE_SVL;
	priv->vlan_svl_fid = 0;
	CHECK(yt921x_proc_apply_vlan_fid_mode(priv) == 0);
	for (i = 0; i < 3; i++) {
		CHECK(FIELD_GET(YT921X_VLAN_CTRL_FID_M, vlan_regs[vids[i]]) == 1);
		CHECK(vlan_regs[vids[i]] & YT921X_VLAN_CTRL_SVL_EN);
	}
}

static void test_errors(struct yt921x_priv *priv)
{
	reset_state(priv);
	priv->vlan_fid_mode = YT921X_VLAN_FID_MODE_SVL;
	priv->vlan_svl_fid = 7;
	vlan_regs[100] = UNRELATED;

	read_fail_vid = 100;
	read_fail_err = -EIO;
	CHECK(yt921x_vlan_add(priv, 3, 100, false) == -EIO);
	CHECK(write_count == 0);
	read_fail_vid = -1;

	write_fail_vid = 100;
	write_fail_err = -ENOSPC;
	CHECK(yt921x_vlan_add(priv, 3, 100, false) == -ENOSPC);
	CHECK(vlan_regs[100] == UNRELATED);
	write_fail_vid = -1;

	read_fail_vid = 100;
	CHECK(yt921x_vlan_del(priv, 3, 100) == -EIO);
	read_fail_vid = -1;

	write_fail_vid = 100;
	CHECK(yt921x_vlan_del(priv, 3, 100) == -ENOSPC);
	write_fail_vid = -1;

	vlan_regs[50] = YT921X_VLAN_CTRL_PORTn(1);
	vlan_regs[60] = YT921X_VLAN_CTRL_PORTn(2);
	read_fail_vid = 55;
	read_fail_err = -ETIMEDOUT;
	CHECK(yt921x_proc_apply_vlan_fid_mode(priv) == -ETIMEDOUT);
	CHECK(vlan_regs[60] == YT921X_VLAN_CTRL_PORTn(2));
	read_fail_vid = -1;

	write_fail_vid = 50;
	write_fail_err = -EACCES;
	CHECK(yt921x_proc_apply_vlan_fid_mode(priv) == -EACCES);
}

int main(void)
{
	struct yt921x_priv priv;

	test_add(&priv);
	test_del(&priv);
	test_vid0(&priv);
	test_apply(&priv);
	test_errors(&priv);

	if (failures) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}

	printf("VLAN FID mode harness: all checks passed\n");
	return 0;
}
"""
)


def main() -> int:
    try:
        functions = []
        for path, name in EXTRACTED:
            functions.append(extract_function(path.read_text(), name))

        switch_header = SWITCH_H.read_text()
        header_macros = extract_header_macros(switch_header)
        svl_value = extract_enum_value(switch_header, "YT921X_VLAN_FID_MODE_SVL")
    except RuntimeError as exc:
        print(f"source contract error: {exc}", file=sys.stderr)
        return 1

    harness = C_HARNESS.substitute(
        header_macros=header_macros,
        svl_value=svl_value,
        functions="\n\n".join(functions),
    )

    cc = os.environ.get("CC", "cc")
    if shutil.which(cc) is None:
        print(f"compiler not found: {cc}", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="yt921x-vlan-fid-") as tmp:
        c_path = Path(tmp) / "test_vlan_fid_mode.c"
        exe_path = Path(tmp) / "test_vlan_fid_mode"
        c_path.write_text(harness)

        compile_cmd = [
            cc,
            "-std=gnu11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-O2",
            "-o",
            str(exe_path),
            str(c_path),
        ]
        compiled = subprocess.run(compile_cmd, text=True, capture_output=True)
        if compiled.returncode:
            sys.stderr.write(compiled.stdout)
            sys.stderr.write(compiled.stderr)
            return compiled.returncode

        tested = subprocess.run([str(exe_path)], text=True, capture_output=True)
        sys.stdout.write(tested.stdout)
        sys.stderr.write(tested.stderr)
        return tested.returncode


if __name__ == "__main__":
    sys.exit(main())
