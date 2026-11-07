#!/usr/bin/env python3
"""Exercise the real yt921x per-VLAN fast-age C path against mocked hardware.

The driver functions are extracted from yt921x_l2.c and compiled unchanged.
The mocks implement the register command semantics needed by the FDB flush
path: IN1 carries the FID, IN2 the egress-port mask, and the FDB_OP write
starts a port+VID flush.  The FDB model then applies the command so the
callback's real effect can be checked.
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
MAIN_C = DSA_DIR / "yt921x_enhanced.c"
INTERNAL_H = DSA_DIR / "yt921x_internal.h"
SWITCH_H = DSA_DIR / "yt921x_enhanced.h"

CALLBACK = "yt921x_dsa_port_vlan_fast_age"
EXTRACTED = [
    "yt921x_vlan_fid_for_vid",
    "yt921x_fdb_recover_soft_locked",
    "yt921x_fdb_recover_locked",
    "yt921x_fdb_wait",
    "yt921x_fdb_flush_raw",
    CALLBACK,
]

HEADER_MACROS = [
    "YT921X_VID_UNAWARE",
    "YT921X_FDB_IN1",
    "YT921X_FDB_IN2",
    "YT921X_FDB_OP",
    "YT921X_FDB_OP_FLUSH_M",
    "YT921X_FDB_OP_FLUSH",
    "YT921X_FDB_OP_FLUSH_ALL",
    "YT921X_FDB_OP_FLUSH_PORT",
    "YT921X_FDB_OP_FLUSH_PORT_VID",
    "YT921X_FDB_OP_FLUSH_VID",
    "YT921X_FDB_OP_FLUSH_STATIC",
    "YT921X_FDB_OP_OP_M",
    "YT921X_FDB_OP_OP",
    "YT921X_FDB_OP_OP_FLUSH",
    "YT921X_FDB_OP_START",
    "YT921X_FDB_RESULT",
    "YT921X_FDB_RESULT_DONE",
    "YT921X_FDB_IO1_FID_M",
    "YT921X_FDB_IO1_FID",
    "YT921X_FDB_IO2_EGR_PORTS_M",
    "YT921X_FDB_IO2_EGR_PORTS",
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
        prev = src[prev_start:prev_end].strip()
        if not type_line.match(prev):
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


def extract_initializer(src: str, marker: str) -> str:
    at = src.find(marker)
    if at < 0:
        raise RuntimeError(f"initializer marker {marker!r} not found")
    brace = src.find("{", at)
    if brace < 0:
        raise RuntimeError(f"initializer for {marker!r} has no body")
    end = _match_pair(src, brace, "{", "}")
    return src[brace:end + 1]


def check_source_contract(internal: str, main_c: str, callback_body: str) -> None:
    proto = (
        f"int {CALLBACK}(struct dsa_switch *ds, int port, u16 vid);"
    )
    normalized_internal = re.sub(r"\s+", " ", internal)
    if proto not in normalized_internal:
        raise RuntimeError(f"missing prototype: {proto}")

    signature = re.search(
        r"\bint\s+" + re.escape(CALLBACK) +
        r"\s*\(\s*struct\s+dsa_switch\s*\*\s*ds\s*,\s*int\s+port\s*,\s*u16\s+vid\s*\)",
        callback_body,
    )
    if not signature:
        raise RuntimeError(f"unexpected {CALLBACK} signature")

    ops = extract_initializer(main_c, "yt921x_dsa_switch_ops")
    bindings = re.findall(
        r"\.port_vlan_fast_age\s*=\s*([A-Za-z_]\w*)\s*,", ops
    )
    if bindings != [CALLBACK]:
        raise RuntimeError(
            f"port_vlan_fast_age binding is {bindings!r}, expected {CALLBACK!r}"
        )
    if not re.search(
        r"\.port_fast_age\s*=\s*yt921x_dsa_port_fast_age\s*,", ops
    ):
        raise RuntimeError("port_fast_age binding changed or missing")


C_HARNESS = string.Template(
    r"""
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint16_t u16;
typedef uint32_t u32;
typedef uint8_t u8;

#define BIT(n) (1U << (n))
#define GENMASK(h, l) (((~0U) << (l)) & (~0U >> (31 - (h))))
#define FIELD_PREP(mask, val) \
	(((u32)(val) << __builtin_ctz((u32)(mask))) & (u32)(mask))
#define FIELD_GET(mask, val) \
	((((u32)(val) & (u32)(mask))) >> __builtin_ctz((u32)(mask)))
#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))

struct device {
	int dummy;
};

struct dsa_switch {
	struct device *dev;
	int num_ports;
};

struct mutex {
	int locked;
	unsigned int locks;
	unsigned int unlocks;
};

struct yt921x_priv {
	struct dsa_switch ds;
	struct mutex reg_lock;
	u8 vlan_fid_mode;
	u16 vlan_svl_fid;
};

#define yt921x_to_priv(ds) container_of((ds), struct yt921x_priv, ds)
#define yt921x_dev(priv) ((priv)->ds.dev)

/* Hardware ABI values extracted from yt921x_enhanced.h. */
$header_macros
#define YT921X_VLAN_FID_MODE_IVL 0
#define YT921X_VLAN_FID_MODE_SVL $svl_value

static int mock_dev_warn(struct device *dev, const char *fmt, ...)
{
	(void)dev;
	(void)fmt;
	return 0;
}

static int mock_dev_err(struct device *dev, const char *fmt, ...)
{
	(void)dev;
	(void)fmt;
	return 0;
}

#define dev_warn mock_dev_warn
#define dev_err mock_dev_err

int yt921x_reg_write(struct yt921x_priv *priv, u32 reg, u32 val);
int yt921x_reg_wait(struct yt921x_priv *priv, u32 reg, u32 mask, u32 *valp);
static void mutex_lock(struct mutex *m);
static void mutex_unlock(struct mutex *m);

/* Real driver functions extracted from yt921x_l2.c. */
$functions

#define FDB_MAX 32

struct fdb_model {
	bool used;
	u16 vid;
	u16 fid;
	u16 port;
	bool is_static;
};

static struct fdb_model fdb[FDB_MAX];
static u32 shadow_in1;
static u32 shadow_in2;
static u32 shadow_op;
static u32 result_reg;
static int write_count;
static int wait_count;
static int fail_reg = -1;
static int fail_err;
static int wait_fail_left;
static int wait_fail_err;
static int lock_violation;
static int unknown_flush_mode;
static u32 last_flush_mode;
static u16 last_flush_fid;
static u16 last_flush_ports;
static bool last_flush_static;
static int failures;

#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			fprintf(stderr, "FAIL:%d: %s\n", __LINE__, #cond); \
			failures++; \
		} \
	} while (0)

static void mutex_lock(struct mutex *m)
{
	if (m->locked) {
		fprintf(stderr, "double mutex_lock\n");
		abort();
	}
	m->locked = 1;
	m->locks++;
}

static void mutex_unlock(struct mutex *m)
{
	if (!m->locked) {
		fprintf(stderr, "mutex_unlock without lock\n");
		abort();
	}
	m->locked = 0;
	m->unlocks++;
}

static void model_flush(u32 op)
{
	u32 mode = FIELD_GET(YT921X_FDB_OP_FLUSH_M, op);
	u32 port_vid_mode = FIELD_GET(YT921X_FDB_OP_FLUSH_M,
				      YT921X_FDB_OP_FLUSH_PORT_VID);
	u32 port_mode = FIELD_GET(YT921X_FDB_OP_FLUSH_M,
				  YT921X_FDB_OP_FLUSH_PORT);
	u16 fid = FIELD_GET(YT921X_FDB_IO1_FID_M, shadow_in1);
	u16 ports = FIELD_GET(YT921X_FDB_IO2_EGR_PORTS_M, shadow_in2);
	int i;

	last_flush_mode = mode;
	last_flush_fid = fid;
	last_flush_ports = ports;
	last_flush_static = !!(op & YT921X_FDB_OP_FLUSH_STATIC);

	if (mode != port_vid_mode && mode != port_mode) {
		unknown_flush_mode = 1;
		return;
	}

	for (i = 0; i < FDB_MAX; i++) {
		if (!fdb[i].used)
			continue;
		if (fdb[i].is_static && !last_flush_static)
			continue;
		if (!(ports & BIT(fdb[i].port)))
			continue;
		if (mode == port_vid_mode && fdb[i].fid != fid)
			continue;
		fdb[i].used = false;
	}
}

int yt921x_reg_write(struct yt921x_priv *priv, u32 reg, u32 val)
{
	if (!priv->reg_lock.locked)
		lock_violation = 1;

	write_count++;
	if (fail_reg >= 0 && reg == (u32)fail_reg)
		return fail_err;

	switch (reg) {
	case YT921X_FDB_IN1:
		shadow_in1 = val;
		break;
	case YT921X_FDB_IN2:
		shadow_in2 = val;
		break;
	case YT921X_FDB_OP:
		shadow_op = val;
		if ((val & YT921X_FDB_OP_START) &&
		    FIELD_GET(YT921X_FDB_OP_OP_M, val) ==
		    FIELD_GET(YT921X_FDB_OP_OP_M, YT921X_FDB_OP_OP_FLUSH))
			model_flush(val);
		break;
	default:
		break;
	}

	return 0;
}

int yt921x_reg_wait(struct yt921x_priv *priv, u32 reg, u32 mask, u32 *valp)
{
	if (!priv->reg_lock.locked)
		lock_violation = 1;

	wait_count++;
	if (wait_fail_left > 0) {
		wait_fail_left--;
		return wait_fail_err;
	}

	if (reg != YT921X_FDB_RESULT)
		return -EINVAL;

	*valp = result_reg & mask;
	return 0;
}

static void reset_model(struct yt921x_priv *priv)
{
	memset(fdb, 0, sizeof(fdb));
	shadow_in1 = 0;
	shadow_in2 = 0;
	shadow_op = 0;
	result_reg = YT921X_FDB_RESULT_DONE;
	write_count = 0;
	wait_count = 0;
	fail_reg = -1;
	fail_err = 0;
	wait_fail_left = 0;
	wait_fail_err = 0;
	lock_violation = 0;
	unknown_flush_mode = 0;
	last_flush_mode = 0;
	last_flush_fid = 0;
	last_flush_ports = 0;
	last_flush_static = false;
	priv->vlan_fid_mode = YT921X_VLAN_FID_MODE_IVL;
	priv->vlan_svl_fid = 0;
	memset(&priv->reg_lock, 0, sizeof(priv->reg_lock));
}

static int add_entry(u16 vid, u16 fid, u16 port, bool is_static)
{
	int i;

	for (i = 0; i < FDB_MAX; i++) {
		if (fdb[i].used)
			continue;
		fdb[i].used = true;
		fdb[i].vid = vid;
		fdb[i].fid = fid;
		fdb[i].port = port;
		fdb[i].is_static = is_static;
		return i;
	}

	fprintf(stderr, "FDB model full\n");
	abort();
}

static void check_locked_balanced(struct yt921x_priv *priv)
{
	CHECK(!lock_violation);
	CHECK(!priv->reg_lock.locked);
	CHECK(priv->reg_lock.locks == priv->reg_lock.unlocks);
}

static void test_invalid_inputs(struct yt921x_priv *priv)
{
	struct dsa_switch *ds = &priv->ds;
	int entry;

	reset_model(priv);
	entry = add_entry(100, 100, 3, false);
	CHECK(yt921x_dsa_port_vlan_fast_age(ds, -1, 100) == -EINVAL);
	CHECK(yt921x_dsa_port_vlan_fast_age(ds, ds->num_ports, 100) == -EINVAL);
	CHECK(yt921x_dsa_port_vlan_fast_age(ds, 4096, 100) == -EINVAL);
	CHECK(yt921x_dsa_port_vlan_fast_age(ds, 3, 0) == -EINVAL);
	CHECK(yt921x_dsa_port_vlan_fast_age(ds, 3, 4095) == -EINVAL);
	CHECK(yt921x_dsa_port_vlan_fast_age(ds, 3, 4096) == -EINVAL);
	CHECK(yt921x_dsa_port_vlan_fast_age(ds, 3, 0xffff) == -EINVAL);
	CHECK(write_count == 0);
	CHECK(wait_count == 0);
	CHECK(fdb[entry].used);
	check_locked_balanced(priv);
}

static void test_valid_boundaries(struct yt921x_priv *priv)
{
	struct dsa_switch *ds = &priv->ds;

	reset_model(priv);
	CHECK(yt921x_dsa_port_vlan_fast_age(ds, 3, 1) == 0);
	CHECK(write_count == 3);
	CHECK(wait_count == 1);
	CHECK(last_flush_fid == 1);
	CHECK(!unknown_flush_mode);
	check_locked_balanced(priv);

	reset_model(priv);
	CHECK(yt921x_dsa_port_vlan_fast_age(ds, 3, 4094) == 0);
	CHECK(write_count == 3);
	CHECK(wait_count == 1);
	CHECK(last_flush_fid == 4094);
	CHECK(!unknown_flush_mode);
	check_locked_balanced(priv);
}

static void test_ivl(struct yt921x_priv *priv)
{
	struct dsa_switch *ds = &priv->ds;
	int target;
	int other_port;
	int other_vid;
	int static_entry;
	int other_both;
	int cpu_entry;

	reset_model(priv);
	target = add_entry(100, 100, 3, false);
	other_port = add_entry(100, 100, 2, false);
	other_vid = add_entry(200, 200, 3, false);
	static_entry = add_entry(100, 100, 3, true);
	other_both = add_entry(200, 200, 2, false);
	cpu_entry = add_entry(100, 100, 8, false);

	CHECK(yt921x_dsa_port_vlan_fast_age(ds, 3, 100) == 0);
	CHECK(!fdb[target].used);
	CHECK(fdb[other_port].used);
	CHECK(fdb[other_vid].used);
	CHECK(fdb[static_entry].used);
	CHECK(fdb[other_both].used);
	CHECK(fdb[cpu_entry].used);

	CHECK(write_count == 3);
	CHECK(wait_count == 1);
	CHECK(last_flush_mode ==
	      FIELD_GET(YT921X_FDB_OP_FLUSH_M, YT921X_FDB_OP_FLUSH_PORT_VID));
	CHECK(!last_flush_static);
	CHECK(last_flush_fid == 100);
	CHECK(last_flush_ports == (u16)BIT(3));
	CHECK(shadow_in1 == YT921X_FDB_IO1_FID(100));
	CHECK(shadow_in2 == YT921X_FDB_IO2_EGR_PORTS(BIT(3)));
	CHECK(!unknown_flush_mode);
	check_locked_balanced(priv);
}

static void test_svl_shared_fid(struct yt921x_priv *priv)
{
	struct dsa_switch *ds = &priv->ds;
	int target;
	int shared_fid;
	int other_port;
	int static_entry;
	int other_fid;

	reset_model(priv);
	priv->vlan_fid_mode = YT921X_VLAN_FID_MODE_SVL;
	priv->vlan_svl_fid = 7;

	target = add_entry(100, 7, 3, false);
	shared_fid = add_entry(200, 7, 3, false);
	other_port = add_entry(100, 7, 2, false);
	static_entry = add_entry(100, 7, 3, true);
	other_fid = add_entry(300, 9, 3, false);

	CHECK(yt921x_dsa_port_vlan_fast_age(ds, 3, 100) == 0);
	CHECK(!fdb[target].used);
	CHECK(!fdb[shared_fid].used);
	CHECK(fdb[other_port].used);
	CHECK(fdb[static_entry].used);
	CHECK(fdb[other_fid].used);

	CHECK(write_count == 3);
	CHECK(wait_count == 1);
	CHECK(last_flush_fid == 7);
	CHECK(shadow_in1 == YT921X_FDB_IO1_FID(7));
	CHECK(!unknown_flush_mode);
	check_locked_balanced(priv);
}

static void test_svl_default_fid(struct yt921x_priv *priv)
{
	struct dsa_switch *ds = &priv->ds;
	int target;
	int other;

	reset_model(priv);
	priv->vlan_fid_mode = YT921X_VLAN_FID_MODE_SVL;
	priv->vlan_svl_fid = 0;

	target = add_entry(100, 1, 3, false);
	other = add_entry(100, 2, 3, false);

	CHECK(yt921x_dsa_port_vlan_fast_age(ds, 3, 100) == 0);
	CHECK(!fdb[target].used);
	CHECK(fdb[other].used);
	CHECK(last_flush_fid == 1);
	CHECK(shadow_in1 == YT921X_FDB_IO1_FID(1));
	CHECK(!unknown_flush_mode);
	check_locked_balanced(priv);
}

static void test_write_failure(struct yt921x_priv *priv, u32 reg, int err,
			       int expected_writes)
{
	int entry;

	reset_model(priv);
	entry = add_entry(100, 100, 3, false);
	fail_reg = reg;
	fail_err = err;

	CHECK(yt921x_dsa_port_vlan_fast_age(&priv->ds, 3, 100) == err);
	CHECK(fdb[entry].used);
	CHECK(write_count == expected_writes);
	CHECK(wait_count == 0);
	CHECK(!unknown_flush_mode);
	check_locked_balanced(priv);
}

static void test_wait_failure(struct yt921x_priv *priv, int failures_left)
{
	int entry;

	reset_model(priv);
	entry = add_entry(100, 100, 3, false);
	wait_fail_left = failures_left;
	wait_fail_err = -ETIMEDOUT;

	CHECK(yt921x_dsa_port_vlan_fast_age(&priv->ds, 3, 100) == -ETIMEDOUT);
	CHECK(!fdb[entry].used);
	CHECK(write_count == 4);
	CHECK(wait_count == 2);
	CHECK(shadow_op == 0U);
	CHECK(!unknown_flush_mode);
	check_locked_balanced(priv);
}

int main(void)
{
	struct yt921x_priv priv;
	struct device dev = { .dummy = 0 };

	memset(&priv, 0, sizeof(priv));
	priv.ds.dev = &dev;
	priv.ds.num_ports = 11;

	test_invalid_inputs(&priv);
	test_valid_boundaries(&priv);
	test_ivl(&priv);
	test_svl_shared_fid(&priv);
	test_svl_default_fid(&priv);
	test_write_failure(&priv, YT921X_FDB_IN1, -EIO, 1);
	test_write_failure(&priv, YT921X_FDB_IN2, -ENOMEM, 2);
	test_write_failure(&priv, YT921X_FDB_OP, -EACCES, 3);
	test_wait_failure(&priv, 1);
	test_wait_failure(&priv, 2);

	if (failures) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}

	printf("MST fast-age harness: all checks passed\n");
	return 0;
}
"""
)


def main() -> int:
    l2_source = L2_C.read_text()
    main_source = MAIN_C.read_text()
    internal_source = INTERNAL_H.read_text()
    switch_header = SWITCH_H.read_text()

    functions = "\n\n".join(extract_function(l2_source, name) for name in EXTRACTED)
    callback_body = extract_function(l2_source, CALLBACK)
    check_source_contract(internal_source, main_source, callback_body)

    header_macros = extract_header_macros(switch_header)
    svl_value = extract_enum_value(switch_header, "YT921X_VLAN_FID_MODE_SVL")

    harness = C_HARNESS.substitute(
        header_macros=header_macros,
        svl_value=svl_value,
        functions=functions,
    )

    cc = os.environ.get("CC", "cc")
    if shutil.which(cc) is None:
        print(f"compiler not found: {cc}", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="yt921x-mst-") as tmp:
        c_path = Path(tmp) / "test_mst_fast_age.c"
        exe_path = Path(tmp) / "test_mst_fast_age"
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
        compiled = subprocess.run(
            compile_cmd, text=True, capture_output=True
        )
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
