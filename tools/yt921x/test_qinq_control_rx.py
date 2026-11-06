#!/usr/bin/env python3
"""Compile and run the real yt921x QinQ RX policy functions against fakes."""
import argparse
import os
import shlex
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SOURCE = ROOT / "target/linux/generic/files/net/dsa/tag_yt921x_qinq.c"
FUNCS = (
    "yt921x_qinq_control_cpu",
    "yt921x_qinq_is_rx_control",
    "yt921x_qinq_find_dp",
    "yt921x_qinq_internal_vid",
    "yt921x_qinq_rx_vlan",
)


def extract(source, name):
    match = re.search(r"\bstatic\s+[^{;]*?\b" + re.escape(name) + r"\s*\([^;{]*\)\s*\{", source)
    if not match:
        raise SystemExit(f"missing static function {name}")
    depth = 0
    for pos in range(match.end() - 1, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[match.start():pos + 1]
    raise SystemExit(f"unterminated function {name}")


HARNESS = r"""
#include <stdbool.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef u16 __be16;
#define ETH_ALEN 6
#define ETH_HLEN 14
#define ETH_DATA_LEN 1500
#define VLAN_HLEN 4
#define VLAN_VID_MASK 0x0fff
#define ETH_P_8021Q 0x8100
#define ETH_P_SLOW 0x8809
#define ETH_P_PAE 0x888e
#define ETH_P_LLDP 0x88cc
#define DSA_TAG_8021Q_MAX_NUM_BRIDGES 7
#define BR_STATE_DISABLED 0
#define BR_STATE_LISTENING 1
#define BR_STATE_LEARNING 2
#define BR_STATE_FORWARDING 3
#define BR_STATE_BLOCKING 4
#define EINVAL 22

struct net_device;
struct dsa_switch { int index; unsigned int cpu_mask; };
struct dsa_port {
    struct dsa_switch *ds;
    int index;
    struct net_device *conduit;
    struct dsa_port *cpu_dp;
    struct net_device *user;
    int stp_state;
    unsigned int bridge_num;
};
struct vlan_cfg { int enabled, pvid_fail, member_count; u16 pvid; u16 members[4]; };
struct net_device { struct dsa_port *dsa_ptr; struct vlan_cfg cfg; };
struct ethhdr { u8 h_dest[6], h_source[6]; __be16 h_proto; } __attribute__((packed));
struct vlan_ethhdr { u8 h_dest[6], h_source[6]; __be16 h_vlan_proto, h_vlan_TCI, h_vlan_encapsulated_proto; } __attribute__((packed));
struct sk_buff { u8 *head; u8 *data; unsigned int len; };
struct bridge_vlan_info { u16 flags, vid; };

static struct ethhdr *eth_hdr(struct sk_buff *skb) { return (struct ethhdr *)skb->head; }
static struct vlan_ethhdr *vlan_eth_hdr(struct sk_buff *skb) { return (struct vlan_ethhdr *)skb->head; }
static bool is_link_local_ether_addr(const u8 *a) {
    return a[0] == 1 && a[1] == 0x80 && a[2] == 0xc2 && a[3] == 0 && a[4] == 0 && !(a[5] & 0xf0);
}
static bool pskb_may_pull(struct sk_buff *skb, unsigned int len) { return skb->len >= len; }
static bool vid_is_dsa_8021q(u16 vid) { return (vid & 0xc00) == 0xc00; }
static int dsa_8021q_rx_switch_id(u16 vid) { return (vid >> 6) & 7; }
static int dsa_8021q_rx_source_port(u16 vid) { return vid & 0xf; }
static u16 dsa_tag_8021q_standalone_vid(const struct dsa_port *dp) {
    return 0xc00 | ((dp->ds->index & 7) << 6) | (dp->index & 0xf);
}
static u16 dsa_tag_8021q_bridge_vid(unsigned int num) { return 0xc00 | ((num & 3) << 4) | ((num & 4) << 7); }
static bool dsa_is_cpu_port(struct dsa_switch *ds, int port) {
    return port >= 0 && port < 32 && (ds->cpu_mask & (1u << port));
}
static bool br_vlan_enabled(const struct net_device *br) { return br->cfg.enabled; }
static int br_vlan_get_pvid_rcu(struct net_device *dev, u16 *pvid) {
    if (dev->cfg.pvid_fail) return -1;
    *pvid = dev->cfg.pvid;
    return 0;
}
static int br_vlan_get_info_rcu(struct net_device *dev, u16 vid, struct bridge_vlan_info *vinfo) {
    int i;
    for (i = 0; i < dev->cfg.member_count; i++)
        if (dev->cfg.members[i] == vid) { vinfo->vid = vid; return 0; }
    return -1;
}
static unsigned int dsa_port_bridge_num_get(const struct dsa_port *dp) { return dp->bridge_num; }
static bool dsa_yt921x_qinq_customer_vid_valid(struct dsa_switch *ds, u16 vid) {
    (void)ds;
    return vid == 100 || vid == 200;
}

static struct dsa_switch ds0, ds1;
static struct net_device conduit4, conduit8, conduit8_alt, conduit8_bad, conduit_other, conduit_null;
static struct dsa_port cpu4, cpu8, cpu8_alt, cpu8_bad;
static struct net_device wan_user, lan_user;
static struct dsa_port wan_dp, lan_dp;
static struct net_device br, br_off;
struct fake_map { int port; struct dsa_port *dp; };
static const struct fake_map fake_maps[] = { {3, &wan_dp}, {1, &lan_dp} };

static struct net_device *dsa_conduit_find_user(struct net_device *conduit, int sid, int port) {
    unsigned int i;
    (void)conduit;
    if (sid != 0) return NULL;
    for (i = 0; i < sizeof(fake_maps) / sizeof(fake_maps[0]); i++)
        if (fake_maps[i].port == port) return fake_maps[i].dp->user;
    return NULL;
}
static struct dsa_port *dsa_user_to_port(const struct net_device *user) { return user->dsa_ptr; }

static void setup(void) {
    ds0.index = 0; ds0.cpu_mask = (1u << 4) | (1u << 8);
    ds1.index = 1; ds1.cpu_mask = (1u << 8);
    conduit4.dsa_ptr = &cpu4; conduit8.dsa_ptr = &cpu8;
    conduit8_alt.dsa_ptr = &cpu8_alt; conduit8_bad.dsa_ptr = &cpu8_bad;
    cpu4.ds = &ds0; cpu4.index = 4; cpu4.conduit = &conduit4;
    cpu8.ds = &ds0; cpu8.index = 8; cpu8.conduit = &conduit8;
    cpu8_alt.ds = &ds1; cpu8_alt.index = 8; cpu8_alt.conduit = &conduit8_alt;
    cpu8_bad.ds = &ds0; cpu8_bad.index = 8; cpu8_bad.conduit = &conduit_other;
    wan_dp.ds = &ds0; wan_dp.index = 3; wan_dp.cpu_dp = &cpu4; wan_dp.user = &wan_user; wan_dp.conduit = &conduit4;
    lan_dp.ds = &ds0; lan_dp.index = 1; lan_dp.cpu_dp = &cpu4; lan_dp.user = &lan_user; lan_dp.conduit = &conduit4;
    wan_user.dsa_ptr = &wan_dp; lan_user.dsa_ptr = &lan_dp;
    wan_user.cfg.pvid = 100; wan_user.cfg.member_count = 2; wan_user.cfg.members[0] = 100; wan_user.cfg.members[1] = 200;
    lan_user.cfg.pvid = 100; lan_user.cfg.member_count = 1; lan_user.cfg.members[0] = 100;
    br.cfg.enabled = 1;
}

/*__FUNCS__*/

static int passed, failed;
static void check(const char *name, int got, int want) {
    if (got == want) { passed++; return; }
    failed++; printf("FAIL %s: got=%d want=%d\n", name, got, want);
}
static void check_rx(const char *name, int got, int want) {
    if ((want < 0 && got < 0) || (want >= 0 && got == want)) { passed++; return; }
    failed++; printf("FAIL %s: got=%d want=%d\n", name, got, want);
}

static void test_control_cpu(void) {
    check("control cpu8 primary", yt921x_qinq_control_cpu(&conduit8, &wan_dp), 1);
    check("control cpu4 rejected", yt921x_qinq_control_cpu(&conduit4, &wan_dp), 0);
    check("control null cpu", yt921x_qinq_control_cpu(&conduit_null, &wan_dp), 0);
    check("control cpu8 other switch", yt921x_qinq_control_cpu(&conduit8_alt, &wan_dp), 0);
    check("control cpu8 wrong conduit", yt921x_qinq_control_cpu(&conduit8_bad, &wan_dp), 0);
    ds0.cpu_mask &= ~(1u << 8);
    check("control port must be CPU", yt921x_qinq_control_cpu(&conduit8, &wan_dp), 0);
    ds0.cpu_mask |= 1u << 8;
}

struct cls_case { const char *name; u8 da[6], llc[3]; u16 proto; unsigned int len; bool want; };
static void test_classifier(void) {
    static const struct cls_case cases[] = {
        {"bpdu len3", {0x01,0x80,0xc2,0,0,0x00}, {0x42,0x42,0x03}, 3, 7, true},
        {"bpdu len1500", {0x01,0x80,0xc2,0,0,0x00}, {0x42,0x42,0x03}, 1500, 7, true},
        {"bpdu len2", {0x01,0x80,0xc2,0,0,0x00}, {0x42,0x42,0x03}, 2, 7, false},
        {"bpdu len1501", {0x01,0x80,0xc2,0,0,0x00}, {0x42,0x42,0x03}, 1501, 7, false},
        {"slow", {0x01,0x80,0xc2,0,0,0x02}, {0,0,0}, 0x8809, 6, true},
        {"eapol", {0x01,0x80,0xc2,0,0,0x03}, {0,0,0}, 0x888e, 8, true},
        {"lldp", {0x01,0x80,0xc2,0,0,0x0e}, {0,0,0}, 0x88cc, 6, true},
        {"unicast", {0x02,0,0,0,0,0}, {0x42,0x42,0x03}, 3, 7, false},
        {"wrong da", {0x01,0x80,0xc2,0,0,0x01}, {0x42,0x42,0x03}, 3, 7, false},
        {"group ipv4", {0x01,0x00,0x5e,0,0,0x01}, {0,0,0}, 0x0800, 24, false},
        {"bpdu DA ipv4", {0x01,0x80,0xc2,0,0,0x00}, {0,0,0}, 0x0800, 24, false},
        {"bpdu DA eapol", {0x01,0x80,0xc2,0,0,0x00}, {0,0,0}, 0x888e, 8, false},
        {"pause actual", {0x01,0x80,0xc2,0,0,0x01}, {0,0,0}, 0x8808, 6, false},
        {"unsupported bpdu DA08", {0x01,0x80,0xc2,0,0,0x08}, {0x42,0x42,3}, 38, 7, false},
        {"lldp short", {0x01,0x80,0xc2,0,0,0x0e}, {0,0,0}, 0x88cc, 5, false},
        {"wrong llc", {0x01,0x80,0xc2,0,0,0x00}, {0x42,0x42,0x04}, 3, 7, false},
        {"pause da08", {0x01,0x80,0xc2,0,0,0x08}, {0,0,0}, 0x8808, 6, false},
        {"bpdu short", {0x01,0x80,0xc2,0,0,0x00}, {0x42,0x42,0x03}, 3, 6, false},
        {"slow short", {0x01,0x80,0xc2,0,0,0x02}, {0,0,0}, 0x8809, 5, false},
        {"eapol short", {0x01,0x80,0xc2,0,0,0x03}, {0,0,0}, 0x888e, 7, false},
    };
    unsigned int i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const struct cls_case *c = &cases[i];
        u8 buf[64];
        u16 tpid = htons(ETH_P_8021Q), tci = htons(100), proto = htons(c->proto);
        struct sk_buff skb;
        memset(buf, 0, sizeof(buf));
        memcpy(buf, c->da, 6); buf[6] = 2; buf[11] = 1;
        memcpy(buf + 12, &tpid, 2); memcpy(buf + 14, &tci, 2); memcpy(buf + 16, &proto, 2);
        memcpy(buf + 18, c->llc, 3);
        skb.head = buf; skb.data = buf + ETH_HLEN; skb.len = c->len;
        check(c->name, yt921x_qinq_is_rx_control(&skb), c->want);
    }
}

struct dp_case { const char *name; struct net_device *conduit; u16 vid; bool control; struct dsa_port *want; };
static void test_find_dp(void) {
    static const struct dp_case cases[] = {
        {"wan data cpu4", &conduit4, 0xc03, false, &wan_dp},
        {"wan control own cpu4", &conduit4, 0xc03, true, &wan_dp},
        {"wan data cpu8", &conduit8, 0xc03, false, NULL},
        {"wan control cpu8", &conduit8, 0xc03, true, &wan_dp},
        {"bridge alias control", &conduit8, 0xc13, true, NULL},
        {"high bridge alias control", &conduit8, 0xe03, true, NULL},
        {"non-DSA vid", &conduit4, 100, false, NULL},
        {"reserved alias data", &conduit4, 0xc13, false, NULL},
        {"missing port", &conduit4, 0xc07, false, NULL},
        {"switch 1 vid", &conduit4, 0xc43, false, NULL},
        {"cpu8 other switch", &conduit8_alt, 0xc03, true, NULL},
        {"cpu8 wrong conduit", &conduit8_bad, 0xc03, true, NULL},
        {"lan data cpu4", &conduit4, 0xc01, false, &lan_dp},
    };
    unsigned int i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const struct dp_case *c = &cases[i];
        check(c->name, yt921x_qinq_find_dp(c->conduit, c->vid, c->control) == c->want, 1);
    }
}

static void test_internal_vid(void) {
    wan_dp.bridge_num = 1;
    check("standalone vid ok", yt921x_qinq_internal_vid(&wan_dp, NULL, 0xc03), 1);
    check("standalone vid bad", yt921x_qinq_internal_vid(&wan_dp, NULL, 0xc04), 0);
    check("bridge vid ok", yt921x_qinq_internal_vid(&wan_dp, &br, 0xc10), 1);
    check("bridge vid bad", yt921x_qinq_internal_vid(&wan_dp, &br, 0xc20), 0);
    wan_dp.bridge_num = 0;
    check("bridge num zero", yt921x_qinq_internal_vid(&wan_dp, &br, 0xc10), 0);
    wan_dp.bridge_num = 8;
    check("bridge num high", yt921x_qinq_internal_vid(&wan_dp, &br, 0xc10), 0);
    wan_dp.bridge_num = 7;
    check("max bridge", yt921x_qinq_internal_vid(&wan_dp, &br, 3632), 1);
    wan_dp.bridge_num = 1;
}

struct vlan_case { const char *name; struct dsa_port *dp; struct net_device *br; u16 vid, pvid; int control, control_cpu, stp, pvid_fail, bridge_num, want; };
static void test_rx_vlan(void) {
    static const struct vlan_case cases[] = {
        {"ctl pvid listening", &wan_dp, &br, 100, 100, 1, 0, BR_STATE_LISTENING, 0, 1, 1},
        {"ctl pvid blocking", &wan_dp, &br, 100, 100, 1, 0, BR_STATE_BLOCKING, 0, 1, 1},
        {"ctl member nonpvid", &wan_dp, &br, 200, 100, 1, 0, BR_STATE_FORWARDING, 0, 1, 0},
        {"ctl absent member", &lan_dp, &br, 200, 100, 1, 0, BR_STATE_FORWARDING, 0, 0, -1},
        {"data fwd member", &wan_dp, &br, 100, 100, 0, 0, BR_STATE_FORWARDING, 0, 1, 0},
        {"data learn member", &wan_dp, &br, 100, 100, 0, 0, BR_STATE_LEARNING, 0, 1, 0},
        {"data listening", &wan_dp, &br, 100, 100, 0, 0, BR_STATE_LISTENING, 0, 1, -1},
        {"data blocking", &wan_dp, &br, 100, 100, 0, 0, BR_STATE_BLOCKING, 0, 1, -1},
        {"data disabled", &wan_dp, &br, 100, 100, 0, 0, BR_STATE_DISABLED, 0, 1, -1},
        {"data nonmember", &lan_dp, &br, 200, 100, 0, 0, BR_STATE_FORWARDING, 0, 0, -1},
        {"data private4095", &wan_dp, &br, 4095, 100, 0, 0, BR_STATE_FORWARDING, 0, 1, -1},
        {"data private4095 cpu8", &wan_dp, &br, 4095, 100, 0, 1, BR_STATE_FORWARDING, 0, 1, -1},
        {"data vid0", &wan_dp, &br, 0, 100, 0, 0, BR_STATE_FORWARDING, 0, 1, -1},
        {"ctl4095 primary blocking", &wan_dp, &br, 4095, 100, 1, 1, BR_STATE_BLOCKING, 0, 1, 1},
        {"ctl4095 primary no pvid", &wan_dp, &br, 4095, 0, 1, 1, BR_STATE_DISABLED, 0, 1, 1},
        {"ctl4095 without VLAN group", &wan_dp, &br, 4095, 0, 1, 1, BR_STATE_LISTENING, 1, 1, 1},
        {"ctl4095 other cpu", &wan_dp, &br, 4095, 100, 1, 0, BR_STATE_FORWARDING, 0, 1, -1},
        {"ctl vid0 primary", &wan_dp, &br, 0, 0, 1, 1, BR_STATE_FORWARDING, 0, 1, 1},
        {"ctl vid0 other cpu", &wan_dp, &br, 0, 0, 1, 0, BR_STATE_FORWARDING, 0, 1, -1},
        {"data vid0 no pvid", &wan_dp, &br, 0, 0, 0, 0, BR_STATE_FORWARDING, 0, 1, -1},
        {"ctl pvid read fail", &wan_dp, &br, 100, 100, 1, 0, BR_STATE_FORWARDING, 1, 1, -1},
        {"ctl vid0 pvid fail", &wan_dp, &br, 0, 0, 1, 1, BR_STATE_FORWARDING, 1, 1, -1},
        {"standalone vid ok", &wan_dp, NULL, 0xc03, 0, 0, 0, BR_STATE_FORWARDING, 0, 0, 1},
        {"standalone vid bad", &wan_dp, NULL, 0xc04, 0, 0, 0, BR_STATE_FORWARDING, 0, 0, -1},
        {"unaware bridge ok", &wan_dp, &br_off, 0xc10, 0, 0, 0, BR_STATE_FORWARDING, 0, 1, 1},
        {"unaware bridge bad", &wan_dp, &br_off, 0xc20, 0, 0, 0, BR_STATE_FORWARDING, 0, 1, -1},
        {"standalone ctl4095", &wan_dp, NULL, 4095, 0, 1, 1, BR_STATE_FORWARDING, 0, 0, 1},
        {"standalone ctl4095 other", &wan_dp, NULL, 4095, 0, 1, 0, BR_STATE_FORWARDING, 0, 0, -1},
    };
    unsigned int i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const struct vlan_case *c = &cases[i];
        c->dp->stp_state = c->stp;
        c->dp->bridge_num = (unsigned int)c->bridge_num;
        c->dp->user->cfg.pvid = c->pvid;
        c->dp->user->cfg.pvid_fail = c->pvid_fail;
        check_rx(c->name, yt921x_qinq_rx_vlan(c->dp, c->br, c->vid, c->control, c->control_cpu), c->want);
    }
}

int main(void) {
    setup();
    test_control_cpu();
    test_classifier();
    test_find_dp();
    test_internal_vid();
    test_rx_vlan();
    printf("PASS %d FAIL %d\n", passed, failed);
    return failed != 0;
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    args = parser.parse_args()
    if not args.source.is_file():
        raise SystemExit(f"source not found: {args.source}")
    source = args.source.read_text()
    funcs = "\n\n".join(extract(source, name) for name in FUNCS)
    macro = re.search(r"^#define YT921X_QINQ_CPU_TAG_CONTROL_PORT\s+.*$", source, re.M)
    if not macro: raise SystemExit("missing actual control-port constant")
    program = HARNESS.replace("/*__FUNCS__*/", macro.group() + "\n" + funcs)
    with tempfile.TemporaryDirectory(prefix="yt921x-qinq-rx-") as tmp:
        c_file = Path(tmp) / "harness.c"
        exe = Path(tmp) / "harness"
        c_file.write_text(program)
        build = subprocess.run(
            shlex.split(os.environ.get("CC", "cc")) + ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O0", "-o", str(exe), str(c_file)],
            text=True, capture_output=True,
        )
        if build.returncode:
            print(build.stderr, file=sys.stderr)
            return build.returncode
        return subprocess.run([str(exe)], text=True).returncode


if __name__ == "__main__":
    sys.exit(main())
