#!/usr/bin/env python3
"""Behavioral test for the YT921x cross-conduit bridge-join guard."""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SOURCE = (REPO_ROOT /
                  "target/linux/generic/files/drivers/net/dsa/yt921x_l2.c")


def _skip_comment_or_literal(src: str, pos: int) -> int:
    if src.startswith("//", pos):
        end = src.find("\n", pos + 2)
        return len(src) if end < 0 else end + 1
    if src.startswith("/*", pos):
        end = src.find("*/", pos + 2)
        if end < 0:
            raise ValueError("unterminated block comment")
        return end + 2
    quote = src[pos]
    pos += 1
    while pos < len(src):
        if src[pos] == "\\":
            pos += 2
        elif src[pos] == quote:
            return pos + 1
        else:
            pos += 1
    raise ValueError("unterminated literal")


def _body_start(src: str, open_paren: int) -> int:
    depth = 0
    pos = open_paren
    while pos < len(src):
        ch = src[pos]
        if ch == "/" and src.startswith(("//", "/*"), pos):
            pos = _skip_comment_or_literal(src, pos)
            continue
        if ch in "\"'":
            pos = _skip_comment_or_literal(src, pos)
            continue
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        elif ch == "{" and depth == 0:
            return pos
        pos += 1
    raise ValueError("function body not found")


def _matching_brace(src: str, body_start: int) -> int:
    depth = 1
    pos = body_start + 1
    while pos < len(src):
        ch = src[pos]
        if ch == "/" and src.startswith(("//", "/*"), pos):
            pos = _skip_comment_or_literal(src, pos)
            continue
        if ch in "\"'":
            pos = _skip_comment_or_literal(src, pos)
            continue
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return pos + 1
        pos += 1
    raise ValueError("unbalanced braces")


def extract_function(src: str, name: str) -> str:
    pattern = re.compile(r"\b" + re.escape(name) + r"\s*\(")
    for match in pattern.finditer(src):
        line_start = src.rfind("\n", 0, match.start()) + 1
        try:
            body = _body_start(src, match.end() - 1)
        except ValueError:
            continue
        if ";" in src[match.end():body]:
            continue
        return src[line_start:_matching_brace(src, body)]
    raise ValueError(f"definition of {name} not found")


C_HARNESS = r'''
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

struct netlink_ext_ack { const char *msg; };
struct net_device { int id; };
struct dsa_bridge { struct net_device *dev; unsigned int num; };
struct dsa_switch { int unused; };
struct dsa_port { int index; struct dsa_port *cpu_dp; };
struct yt921x_priv {
	bool tag_8021q;
	bool qinq;
	u16 cpu_ports_mask;
	int reg_lock;
};

#define BIT(n) (1U << (n))
#define NL_SET_ERR_MSG_MOD(extack, text) \
	do { if (extack) (extack)->msg = (text); } while (0)

static struct yt921x_priv priv;
static struct dsa_switch ds;
static struct dsa_port ports[9];
static struct net_device brdev, otherdev;
static struct net_device *port_bridge[9];
static unsigned int user_mask, cpu_mask;
static int tag_join_calls, bridge_ports_calls, restore_calls;
static int bridge_join_calls, refresh_calls, fault_calls, leave_calls;
static int lock_calls, unlock_calls;
static int tag_join_result, restore_result, bridge_join_result, refresh_result;
static u16 bridge_ports_mask, bridge_join_mask;

static struct dsa_port *dsa_to_port(struct dsa_switch *d, int p)
{
	(void)d;
	return (p >= 0 && p < 9 && (user_mask & BIT(p))) ? &ports[p] : NULL;
}

#define dsa_switch_for_each_user_port(dp, d) \
	for (int __i = 0; __i < 9; __i++) \
		if (((dp) = dsa_to_port((d), __i)) != NULL)

static bool dsa_is_cpu_port(struct dsa_switch *d, int p)
{
	(void)d;
	return !!(cpu_mask & BIT(p));
}

static struct yt921x_priv *yt921x_to_priv(struct dsa_switch *d)
{
	(void)d;
	return &priv;
}

static bool dsa_port_offloads_bridge_dev(struct dsa_port *dp,
					 struct net_device *dev)
{
	return port_bridge[dp->index] == dev;
}

static int dsa_tag_8021q_bridge_join(struct dsa_switch *d, int p,
				     struct dsa_bridge b, bool *tx,
				     struct netlink_ext_ack *e)
{
	(void)d; (void)p; (void)b; (void)e;
	tag_join_calls++;
	*tx = true;
	return tag_join_result;
}

static u16 yt921x_dsa_bridge_ports(struct dsa_switch *d,
				   struct net_device *dev)
{
	(void)d; (void)dev;
	bridge_ports_calls++;
	return bridge_ports_mask;
}

static int yt921x_qinq_restore_standalone(struct yt921x_priv *p, int port)
{
	(void)p; (void)port;
	restore_calls++;
	return restore_result;
}

static int yt921x_bridge_join(struct yt921x_priv *p, int port, u16 mask)
{
	(void)p; (void)port;
	bridge_join_calls++;
	bridge_join_mask = mask;
	return bridge_join_result;
}

static int yt921x_refresh_flood_masks_locked(struct yt921x_priv *p)
{
	(void)p;
	refresh_calls++;
	return refresh_result;
}

static void yt921x_8021q_fault(struct yt921x_priv *p, int port)
{
	(void)p; (void)port;
	fault_calls++;
}

static void dsa_tag_8021q_bridge_leave(struct dsa_switch *d, int p,
				       struct dsa_bridge b)
{
	(void)d; (void)p; (void)b;
	leave_calls++;
}

static void mutex_lock(int *lock)
{
	(void)lock;
	lock_calls++;
}

static void mutex_unlock(int *lock)
{
	(void)lock;
	unlock_calls++;
}

@FUNCTION@

#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		return 1; \
	} \
} while (0)

static void reset(void)
{
	memset(&priv, 0, sizeof(priv));
	memset(&ds, 0, sizeof(ds));
	memset(ports, 0, sizeof(ports));
	memset(port_bridge, 0, sizeof(port_bridge));
	priv.tag_8021q = true;
	priv.qinq = false;
	priv.cpu_ports_mask = BIT(8) | BIT(4);
	user_mask = BIT(1) | BIT(2);
	cpu_mask = BIT(4) | BIT(8);
	ports[1].index = 1;
	ports[2].index = 2;
	ports[4].index = 4;
	ports[8].index = 8;
	ports[1].cpu_dp = &ports[8];
	ports[2].cpu_dp = &ports[4];
	port_bridge[1] = port_bridge[2] = &brdev;
	tag_join_calls = bridge_ports_calls = restore_calls = 0;
	bridge_join_calls = refresh_calls = fault_calls = leave_calls = 0;
	lock_calls = unlock_calls = 0;
	tag_join_result = restore_result = bridge_join_result = refresh_result = 0;
	bridge_ports_mask = BIT(1) | BIT(2);
	bridge_join_mask = 0;
}

static int join(int port, bool *tx, struct netlink_ext_ack *e)
{
	struct dsa_bridge bridge = { .dev = &brdev, .num = 1 };

	return yt921x_dsa_port_bridge_join(&ds, port, bridge, tx, e);
}

static int test_qinq_cross(void)
{
	bool tx = false;
	struct netlink_ext_ack e = {0};

	reset();
	priv.qinq = true;
	CHECK(join(1, &tx, &e) == 0);
	CHECK(tx == false);
	CHECK(tag_join_calls == 1 && bridge_ports_calls == 1);
	CHECK(restore_calls == 1 && bridge_join_calls == 1 && refresh_calls == 1);
	CHECK(lock_calls == 1 && unlock_calls == 1);
	CHECK(fault_calls == 0 && leave_calls == 0 && e.msg == NULL);
	CHECK(bridge_join_mask == (BIT(1) | BIT(2) | priv.cpu_ports_mask));
	return 0;
}

static int test_single_cross(void)
{
	bool tx = false;
	struct netlink_ext_ack e = {0};

	reset();
	CHECK(join(1, &tx, &e) == -EOPNOTSUPP);
	CHECK(e.msg != NULL);
	CHECK(strcmp(e.msg, "Experimental transport cannot bridge across CPU conduits") == 0);
	CHECK(tag_join_calls == 0 && bridge_ports_calls == 0 && restore_calls == 0);
	CHECK(bridge_join_calls == 0 && refresh_calls == 0);
	CHECK(lock_calls == 0 && unlock_calls == 0);
	CHECK(fault_calls == 0 && leave_calls == 0);
	return 0;
}

static int test_same_conduit(void)
{
	bool tx = false;
	struct netlink_ext_ack e = {0};

	reset();
	ports[2].cpu_dp = &ports[8];
	CHECK(join(1, &tx, &e) == 0);
	CHECK(tx == true);
	CHECK(tag_join_calls == 1 && bridge_ports_calls == 1);
	CHECK(restore_calls == 0 && bridge_join_calls == 1 && refresh_calls == 1);
	CHECK(lock_calls == 1 && unlock_calls == 1);
	CHECK(fault_calls == 0 && leave_calls == 0);
	return 0;
}

static int test_different_bridge(void)
{
	bool tx = false;
	struct netlink_ext_ack e = {0};

	reset();
	port_bridge[2] = &otherdev;
	CHECK(join(1, &tx, &e) == 0);
	CHECK(tx == true);
	CHECK(tag_join_calls == 1 && bridge_ports_calls == 1);
	CHECK(restore_calls == 0 && bridge_join_calls == 1 && refresh_calls == 1);
	CHECK(lock_calls == 1 && unlock_calls == 1);
	CHECK(fault_calls == 0 && leave_calls == 0);
	return 0;
}

static int test_error_cleanup(void)
{
	bool tx = false;
	struct netlink_ext_ack e = {0};

	reset();
	ports[2].cpu_dp = &ports[8];
	bridge_join_result = -EIO;
	CHECK(join(1, &tx, &e) == -EIO);
	CHECK(tag_join_calls == 1 && bridge_join_calls == 1 && refresh_calls == 0);
	CHECK(restore_calls == 0 && lock_calls == 1 && unlock_calls == 1);
	CHECK(fault_calls == 1 && leave_calls == 1);

	reset();
	priv.qinq = true;
	ports[2].cpu_dp = &ports[8];
	bridge_join_result = -EIO;
	CHECK(join(1, &tx, &e) == -EIO);
	CHECK(tag_join_calls == 1 && restore_calls == 1 && bridge_join_calls == 1);
	CHECK(refresh_calls == 0 && lock_calls == 1 && unlock_calls == 1);
	CHECK(fault_calls == 1 && leave_calls == 1);
	return 0;
}

int main(void)
{
	if (test_qinq_cross() || test_single_cross() || test_same_conduit() ||
	    test_different_bridge() || test_error_cleanup())
		return 1;
	puts("PASS: QinQ cross-conduit proceeds; single-tag cross rejected; "
	     "same/different bridge proceed; cleanup works");
	return 0;
}
'''


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    args = parser.parse_args()

    if not args.source.is_file():
        print(f"source not found: {args.source}", file=sys.stderr)
        return 2

    function = extract_function(args.source.read_text(),
                                "yt921x_dsa_port_bridge_join")
    prefix = ("static int\n"
              if function.lstrip().startswith("yt921x_dsa_port_bridge_join")
              else "")
    harness = C_HARNESS.replace("@FUNCTION@", prefix + function)
    with tempfile.TemporaryDirectory(prefix="yt921x-bridge-conduits-") as tmp:
        c_path = Path(tmp) / "harness.c"
        exe_path = Path(tmp) / "harness"
        c_path.write_text(harness)
        subprocess.run([args.cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                        "-O2", "-o", str(exe_path), str(c_path)], check=True)
        subprocess.run([str(exe_path)], check=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
