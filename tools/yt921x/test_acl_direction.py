#!/usr/bin/env python3
"""Behavioral tests for the YT921x ACL direction guard and trap action.

The flower add callback and the drop and trap action cases are extracted
from the driver source and compiled against minimal stubs.
"""

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
                  "target/linux/generic/files/drivers/net/dsa/yt921x_acl.c")
QINQ_SOURCE = DEFAULT_SOURCE.with_name("yt921x_qinq_acl.c")
EGRESS_MSG = ("Hardware egress ACL is unsupported; "
              "rule inversion is not an egress selector")


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
    """Return the definition text starting at the function-name line."""
    pattern = re.compile(r"\b" + re.escape(name) + r"\s*\(")
    for match in pattern.finditer(src):
        line_start = src.rfind("\n", 0, match.start()) + 1
        try:
            body = _body_start(src, match.end() - 1)
        except ValueError:
            continue
        if ";" in src[match.end():body]:
            continue  # prototype or call, not a definition
        return src[line_start:_matching_brace(src, body)]
    raise ValueError(f"definition of {name} not found")


C_HARNESS = r'''
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define YT921X_ACL_ENT_PER_BLK 8
#define YT921X_UDF_NUM 8
#define YT921X_ACL_FLOW_STATS_ID_INVALID UINT8_MAX
#define YT921X_ACL_METER_ID_INVALID UINT8_MAX
#define YT921X_ACL_METER_ID_BLACKHOLE 63
#define YT921X_ACL_ACTa_METER_EN (1u << 14)
#define YT921X_ACL_ACTa_METER_ID_M (0x3fu << 15)
#define YT921X_ACL_ACTc_REDIR_EN (1u << 9)
#define YT921X_ACL_ACTc_REDIR_DPORTS_M (0x7ffu << 10)
#define YT921X_ACL_ACTc_REDIR_DPORTn(port) (1u << (10 + (port)))
#define YT921X_ACL_ACTc_REDIR_M (3u << 21)
#define YT921X_ACL_ACTc_REDIR_STEER (2u << 21)
#define YT921X_ACL_ACTc_REDIR_TRAP (3u << 21)
#define YT921X_ACL_ACTa_FLOW_STATS_EN (1u << 12)
#define YT921X_ACL_ACTa_FLOW_STATS_PTR (0x3fu << 6)
#define FIELD_PREP(mask, val) \
	(((u32)(val) << __builtin_ctzll((u64)(mask))) & (u32)(mask))
#define BIT(n) (1UL << (n))

struct netlink_ext_ack {
	const char *msg;
};

struct dsa_switch {
	int unused;
};

struct yt921x_priv {
	int reg_lock;
	bool qinq;
	int primary_cpu_port;
	int secondary_cpu_port;
	u16 dt_secondary_conduit_user_mask;
	bool dt_secondary_conduit_user_mask_valid;
};

struct yt921x_acl_entry {
	u32 action[3];
	u8 meter_id;
	u8 flow_stats_id;
	u64 flow_stats_last;
	bool mirror_en;
	u8 mirror_to_port;
};

struct flow_action_entry {
	int id;
};

struct flow_action {
	unsigned int num_actions;
	struct flow_action_entry entries[4];
};

struct flow_rule {
	struct flow_action action;
};

struct flow_cls_offload {
	unsigned long cookie;
	struct {
		unsigned int chain_index;
		struct netlink_ext_ack *extack;
	} common;
	struct flow_rule rule;
};

enum {
	FLOW_ACTION_POLICE = 1,
	FLOW_ACTION_DROP,
	FLOW_ACTION_TRAP,
};

#define flow_action_for_each(i, act, action) \
	for ((i) = 0; (i) < (int)(action)->num_actions && \
	     ((act) = &(action)->entries[i]) != NULL; (i)++)

static void set_extack(struct netlink_ext_ack *extack, const char *msg)
{
	if (extack)
		extack->msg = msg;
}

#define NL_SET_ERR_MSG_MOD(extack, msg) set_extack((extack), (msg))
#define pr_info(fmt, ...) do { (void)(fmt); } while (0)

static struct yt921x_priv g_priv;
static unsigned int g_user_mask;
static int g_parse_calls;
static int g_find_calls;
static int g_lock_calls;
static int g_unlock_calls;
static int g_flow_alloc_calls;
static int g_meter_alloc_calls;
static int g_meter_apply_calls;
static int g_mirror_get_calls;
static int g_mirror_put_calls;
static int g_add_calls;
static int g_flow_clear_calls;
static int g_flow_free_calls;
static int g_meter_clear_calls;
static int g_meter_free_calls;
static int g_hw_writes;
static int g_flow_alloc_result;
static int g_meter_alloc_result;
static int g_add_result;

static void reset_state(void)
{
	g_user_mask = BIT(1);
	g_parse_calls = 0;
	g_find_calls = 0;
	g_lock_calls = 0;
	g_unlock_calls = 0;
	g_flow_alloc_calls = 0;
	g_meter_alloc_calls = 0;
	g_meter_apply_calls = 0;
	g_mirror_get_calls = 0;
	g_mirror_put_calls = 0;
	g_add_calls = 0;
	g_flow_clear_calls = 0;
	g_flow_free_calls = 0;
	g_meter_clear_calls = 0;
	g_meter_free_calls = 0;
	g_hw_writes = 0;
	g_flow_alloc_result = 0;
	g_meter_alloc_result = 0;
	g_add_result = 0;
	g_priv.reg_lock = 0;
}

static void mutex_lock(int *lock)
{
	(void)lock;
	g_lock_calls++;
}

static void mutex_unlock(int *lock)
{
	(void)lock;
	g_unlock_calls++;
}

static bool dsa_is_user_port(struct dsa_switch *ds, int port)
{
	(void)ds;
	return port >= 0 && port < 32 && (g_user_mask & (1u << port));
}

static struct yt921x_priv *yt921x_to_priv(struct dsa_switch *ds)
{
	(void)ds;
	return &g_priv;
}

static const struct flow_rule *
flow_cls_offload_flow_rule(struct flow_cls_offload *cls)
{
	return &cls->rule;
}

static unsigned int
yt921x_acl_parse(struct yt921x_acl_entry *group, u16 ports_mask,
		 u32 *udfs_ctrl, unsigned int *udfs_cntp,
		 struct yt921x_priv *priv, struct dsa_switch *ds,
		 struct flow_cls_offload *cls, bool ingress)
{
	(void)ports_mask;
	(void)udfs_ctrl;
	(void)udfs_cntp;
	(void)priv;
	(void)ds;
	(void)cls;
	(void)ingress;
	g_parse_calls++;
	memset(group, 0, sizeof(*group) * YT921X_ACL_ENT_PER_BLK);
	return 1;
}

static unsigned int
yt921x_acl_find(const struct yt921x_priv *priv, unsigned long cookie)
{
	(void)priv;
	(void)cookie;
	g_find_calls++;
	return UINT_MAX;
}

static int
yt921x_acl_flow_stats_alloc(struct yt921x_priv *priv, u32 *flow_stats_idp)
{
	(void)priv;
	g_flow_alloc_calls++;
	if (g_flow_alloc_result)
		return g_flow_alloc_result;
	*flow_stats_idp = 3;
	return 0;
}

static int
yt921x_acl_meter_alloc(struct yt921x_priv *priv, u32 *meter_idp)
{
	(void)priv;
	g_meter_alloc_calls++;
	if (g_meter_alloc_result)
		return g_meter_alloc_result;
	*meter_idp = 2;
	return 0;
}

static int
yt921x_acl_meter_apply(struct yt921x_priv *priv, int port, u32 meter_id,
		       const struct flow_action_entry *act,
		       struct netlink_ext_ack *extack)
{
	(void)priv;
	(void)port;
	(void)meter_id;
	(void)act;
	(void)extack;
	g_meter_apply_calls++;
	g_hw_writes++;
	return 0;
}

static int
yt921x_acl_mirror_get(struct yt921x_priv *priv, int to_local_port,
		      struct netlink_ext_ack *extack)
{
	(void)priv;
	(void)to_local_port;
	(void)extack;
	g_mirror_get_calls++;
	g_hw_writes++;
	return 0;
}

static void yt921x_acl_mirror_put(struct yt921x_priv *priv, int to_local_port)
{
	(void)priv;
	(void)to_local_port;
	g_mirror_put_calls++;
}

static int
yt921x_acl_add(struct yt921x_priv *priv,
	       const struct yt921x_acl_entry *group, unsigned int size,
	       const u32 *udfs_ctrl, unsigned int udfs_cnt,
	       struct netlink_ext_ack *extack)
{
	(void)priv;
	(void)group;
	(void)size;
	(void)udfs_ctrl;
	(void)udfs_cnt;
	(void)extack;
	g_add_calls++;
	g_hw_writes++;
	return g_add_result;
}

static int
yt921x_acl_meter_clear_hw(struct yt921x_priv *priv, u32 meter_id)
{
	(void)priv;
	(void)meter_id;
	g_meter_clear_calls++;
	g_hw_writes++;
	return 0;
}

static void yt921x_acl_meter_free(struct yt921x_priv *priv, u32 meter_id)
{
	(void)priv;
	(void)meter_id;
	g_meter_free_calls++;
}

static int
yt921x_acl_flow_stats_clear_hw(struct yt921x_priv *priv, u32 flow_stats_id)
{
	(void)priv;
	(void)flow_stats_id;
	g_flow_clear_calls++;
	g_hw_writes++;
	return 0;
}

static void
yt921x_acl_flow_stats_free(struct yt921x_priv *priv, u32 flow_stats_id)
{
	(void)priv;
	(void)flow_stats_id;
	g_flow_free_calls++;
}

@FUNCTION@
@QINQ_CPU@

static int encode_terminal_action(struct yt921x_priv *priv,
				  struct yt921x_acl_entry *group, int port, int id)
{
	struct netlink_ext_ack error = {0}, *extack = &error;
	bool police_seen = false;
	bool trap_seen = false;
	u32 *action = group[0].action;

	(void)priv;
	(void)port;
	switch (id) {
@TERMINAL_CASES@
	default:
		return -EINVAL;
	}
	return trap_seen;
}

#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		return 1; \
	} \
} while (0)

#define CHECK_NO_WORK() do { \
	CHECK(g_parse_calls == 0); \
	CHECK(g_find_calls == 0); \
	CHECK(g_lock_calls == 0); \
	CHECK(g_unlock_calls == 0); \
	CHECK(g_flow_alloc_calls == 0); \
	CHECK(g_meter_alloc_calls == 0); \
	CHECK(g_meter_apply_calls == 0); \
	CHECK(g_mirror_get_calls == 0); \
	CHECK(g_mirror_put_calls == 0); \
	CHECK(g_add_calls == 0); \
	CHECK(g_flow_clear_calls == 0); \
	CHECK(g_flow_free_calls == 0); \
	CHECK(g_meter_clear_calls == 0); \
	CHECK(g_meter_free_calls == 0); \
	CHECK(g_hw_writes == 0); \
} while (0)

static void
init_cls(struct flow_cls_offload *cls, struct netlink_ext_ack *extack,
	 unsigned long cookie)
{
	memset(cls, 0, sizeof(*cls));
	memset(extack, 0, sizeof(*extack));
	cls->cookie = cookie;
	cls->common.extack = extack;
}

static int test_egress_rejected_before_work(void)
{
	struct dsa_switch ds = {0};
	struct flow_cls_offload cls;
	struct netlink_ext_ack extack;
	int res;

	reset_state();
	g_user_mask = BIT(1);
	init_cls(&cls, &extack, 0x1111);
	res = yt921x_dsa_cls_flower_add(&ds, 1, &cls, false);
	CHECK(res == -EOPNOTSUPP);
	CHECK(extack.msg != NULL);
	CHECK(strcmp(extack.msg,
		     "Hardware egress ACL is unsupported; rule inversion is not an egress selector") == 0);
	CHECK_NO_WORK();
	return 0;
}

static int test_bad_cookie(void)
{
	struct dsa_switch ds = {0};
	struct flow_cls_offload cls;
	struct netlink_ext_ack extack;
	int res;

	reset_state();
	g_user_mask = BIT(1);
	init_cls(&cls, &extack, 0);
	res = yt921x_dsa_cls_flower_add(&ds, 1, &cls, true);
	CHECK(res == -EINVAL);
	CHECK(extack.msg == NULL);
	CHECK_NO_WORK();
	return 0;
}

static int test_nonuser_port(void)
{
	struct dsa_switch ds = {0};
	struct flow_cls_offload cls;
	struct netlink_ext_ack extack;
	int res;

	reset_state();
	g_user_mask = BIT(2);
	init_cls(&cls, &extack, 0x2222);
	res = yt921x_dsa_cls_flower_add(&ds, 1, &cls, true);
	CHECK(res == -EOPNOTSUPP);
	CHECK(extack.msg == NULL);
	CHECK_NO_WORK();

	/* User-port validation must precede the egress guard. */
	reset_state();
	g_user_mask = BIT(2);
	init_cls(&cls, &extack, 0x2223);
	res = yt921x_dsa_cls_flower_add(&ds, 1, &cls, false);
	CHECK(res == -EOPNOTSUPP);
	CHECK(extack.msg == NULL);
	CHECK_NO_WORK();
	return 0;
}

static int test_ingress_proceeds_to_parse_and_add(void)
{
	struct dsa_switch ds = {0};
	struct flow_cls_offload cls;
	struct netlink_ext_ack extack;
	int res;

	reset_state();
	g_user_mask = BIT(1);
	init_cls(&cls, &extack, 0x3333);
	res = yt921x_dsa_cls_flower_add(&ds, 1, &cls, true);
	CHECK(res == 0);
	CHECK(g_parse_calls == 1);
	CHECK(g_find_calls == 1);
	CHECK(g_lock_calls == 1);
	CHECK(g_unlock_calls == 1);
	CHECK(g_flow_alloc_calls == 1);
	CHECK(g_add_calls == 1);
	CHECK(g_meter_alloc_calls == 0);
	CHECK(g_mirror_get_calls == 0);
	CHECK(g_hw_writes == 1);
	CHECK(extack.msg == NULL);
	return 0;
}

static int test_flow_stats_alloc_failure(void)
{
	struct dsa_switch ds = {0};
	struct flow_cls_offload cls;
	struct netlink_ext_ack extack;
	int res;

	reset_state();
	g_user_mask = BIT(1);
	g_flow_alloc_result = -ENOSPC;
	init_cls(&cls, &extack, 0x4444);
	res = yt921x_dsa_cls_flower_add(&ds, 1, &cls, true);
	CHECK(res == -ENOSPC);
	CHECK(g_parse_calls == 1);
	CHECK(g_find_calls == 1);
	CHECK(g_lock_calls == 1);
	CHECK(g_unlock_calls == 1);
	CHECK(g_flow_alloc_calls == 1);
	CHECK(g_add_calls == 0);
	CHECK(g_flow_clear_calls == 0);
	CHECK(g_flow_free_calls == 0);
	CHECK(extack.msg != NULL);
	return 0;
}

static int test_meter_alloc_failure_cleanup(void)
{
	struct dsa_switch ds = {0};
	struct flow_cls_offload cls;
	struct netlink_ext_ack extack;
	int res;

	reset_state();
	g_user_mask = BIT(1);
	g_meter_alloc_result = -ENOSPC;
	init_cls(&cls, &extack, 0x5555);
	cls.rule.action.num_actions = 1;
	cls.rule.action.entries[0].id = FLOW_ACTION_POLICE;
	res = yt921x_dsa_cls_flower_add(&ds, 1, &cls, true);
	CHECK(res == -ENOSPC);
	CHECK(g_parse_calls == 1);
	CHECK(g_flow_alloc_calls == 1);
	CHECK(g_meter_alloc_calls == 1);
	CHECK(g_meter_apply_calls == 0);
	CHECK(g_add_calls == 0);
	CHECK(g_flow_clear_calls == 1);
	CHECK(g_flow_free_calls == 1);
	CHECK(g_meter_clear_calls == 0);
	CHECK(g_meter_free_calls == 0);
	CHECK(g_lock_calls == 1);
	CHECK(g_unlock_calls == 1);
	return 0;
}

static int test_add_failure_cleanup(void)
{
	struct dsa_switch ds = {0};
	struct flow_cls_offload cls;
	struct netlink_ext_ack extack;
	int res;

	reset_state();
	g_user_mask = BIT(1);
	g_add_result = -ENOSPC;
	init_cls(&cls, &extack, 0x6666);
	res = yt921x_dsa_cls_flower_add(&ds, 1, &cls, true);
	CHECK(res == -ENOSPC);
	CHECK(g_parse_calls == 1);
	CHECK(g_flow_alloc_calls == 1);
	CHECK(g_add_calls == 1);
	CHECK(g_flow_clear_calls == 1);
	CHECK(g_flow_free_calls == 1);
	CHECK(g_meter_clear_calls == 0);
	CHECK(g_meter_free_calls == 0);
	CHECK(g_lock_calls == 1);
	CHECK(g_unlock_calls == 1);
	return 0;
}

static int test_trap_and_drop_encoding(void)
{
	const u32 blackhole = YT921X_ACL_ACTa_METER_EN |
		FIELD_PREP(YT921X_ACL_ACTa_METER_ID_M,
			   YT921X_ACL_METER_ID_BLACKHOLE);
	const u32 trap = YT921X_ACL_ACTc_REDIR_EN | YT921X_ACL_ACTc_REDIR_TRAP;
	const u32 steer = YT921X_ACL_ACTc_REDIR_EN | YT921X_ACL_ACTc_REDIR_STEER;
	struct yt921x_priv priv = {
		.qinq = true,
		.primary_cpu_port = 8,
		.secondary_cpu_port = 4,
		.dt_secondary_conduit_user_mask = BIT(3),
		.dt_secondary_conduit_user_mask_valid = true,
	};
	struct yt921x_acl_entry entry;
	int port;

	for (port = 0; port < 4; port++) {
		int cpu = port == 3 ? 4 : 8;

		entry = (struct yt921x_acl_entry){
			.meter_id = YT921X_ACL_METER_ID_INVALID,
		};
		CHECK(encode_terminal_action(&priv, &entry, port, FLOW_ACTION_TRAP) == 1);
		CHECK(entry.action[0] == 0);
		CHECK(entry.action[2] == (steer | YT921X_ACL_ACTc_REDIR_DPORTn(cpu)));
		CHECK(entry.meter_id == YT921X_ACL_METER_ID_INVALID);
	}

	entry = (struct yt921x_acl_entry){ .meter_id = YT921X_ACL_METER_ID_INVALID };
	CHECK(encode_terminal_action(&priv, &entry, 0, FLOW_ACTION_DROP) == 0);
	CHECK(entry.action[0] == blackhole);
	CHECK(entry.action[2] == 0);
	CHECK(entry.meter_id == YT921X_ACL_METER_ID_BLACKHOLE);

	priv.qinq = false;
	entry = (struct yt921x_acl_entry){ .meter_id = YT921X_ACL_METER_ID_INVALID };
	CHECK(encode_terminal_action(&priv, &entry, 0, FLOW_ACTION_TRAP) == 1);
	CHECK(entry.action[0] == blackhole);
	CHECK(entry.action[2] == trap);
	CHECK(entry.meter_id == YT921X_ACL_METER_ID_BLACKHOLE);
	return 0;
}

int main(void)
{
	if (test_egress_rejected_before_work() ||
	    test_bad_cookie() ||
	    test_nonuser_port() ||
	    test_ingress_proceeds_to_parse_and_add() ||
	    test_flow_stats_alloc_failure() ||
	    test_meter_alloc_failure_cleanup() ||
	    test_add_failure_cleanup() ||
	    test_trap_and_drop_encoding())
		return 1;
	puts("PASS: egress rejected before parse/lock/alloc/hw; ingress still reaches parse/add");
	puts("PASS: QinQ trap uses its pinned CPU port without the blackhole meter; drop and legacy trap retain it");
	return 0;
}
'''


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE,
                        help="native yt921x_acl.c (default: repo-relative path)")
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    args = parser.parse_args()

    if not args.source.is_file():
        print(f"source not found: {args.source}", file=sys.stderr)
        return 2

    source = args.source.read_text()
    function = extract_function(source, "yt921x_dsa_cls_flower_add")
    parse_key = extract_function(source, "yt921x_acl_parse_key")
    parse_action = extract_function(source, "yt921x_acl_parse_action")
    qinq_cpu = extract_function(QINQ_SOURCE.read_text(), "yt921x_qinq_expected_cpu")
    terminal_cases = []
    for action in ("DROP", "TRAP"):
        match = re.search(rf"^\t\tcase FLOW_ACTION_{action}:.*?(?=^\t\t(?:case |default:))",
                          parse_action, re.MULTILINE | re.DOTALL)
        if match is None:
            raise ValueError(f"FLOW_ACTION_{action} case not found")
        terminal_cases.append(match.group())
    if "YT921X_ACL_KEYb_REVERSE" in parse_key:
        print("FAIL: KEYb_REVERSE is still used by yt921x_acl_parse_key",
              file=sys.stderr)
        return 1

    prefix = ("static int\n"
              if function.lstrip().startswith("yt921x_dsa_cls_flower_add")
              else "")
    harness = C_HARNESS.replace("@FUNCTION@", prefix + function)
    harness = harness.replace("@QINQ_CPU@", qinq_cpu)
    harness = harness.replace("@TERMINAL_CASES@", "".join(terminal_cases))
    with tempfile.TemporaryDirectory(prefix="yt921x-acl-direction-") as tmp:
        c_path = Path(tmp) / "harness.c"
        exe_path = Path(tmp) / "harness"
        c_path.write_text(harness)
        subprocess.run([args.cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                        "-O2", "-o", str(exe_path), str(c_path)], check=True)
        subprocess.run([str(exe_path)], check=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
