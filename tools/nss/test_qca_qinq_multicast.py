#!/usr/bin/env python3
"""Check the real QCA multicast tag and ACL encoders against the wire fixture."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
kernel = Path(os.environ['NSS_MC_KERNEL']) if 'NSS_MC_KERNEL' in os.environ else next(
    candidate for candidate in (root / 'build_dir').glob(
        'target-*/linux-qualcommax_ipq50xx/linux-6.18.*') if candidate.is_dir())


def function(path, name):
    code = (kernel / path).read_text()
    match = re.search(r'(?:static (?:inline )?)?(?:bool|int|u16|u64) ' +
                      name + r'\([^;{}]*\)\n\{', code)
    assert match, name
    pos, braces = match.end(), 1
    while braces:
        braces += (code[pos] == '{') - (code[pos] == '}')
        pos += 1
    return code[match.start():pos]


mock = r'''
#include <arpa/inet.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32, __be32;
#define BIT(n) (1U << (n))
#define U32_MAX UINT32_MAX
struct qca8k_priv { unsigned int unused; };
struct qca8k_qinq_mc { __be32 group; u16 cvid; u8 cpu, slot, ports; };
static u32 values[5], masks[5], actions[3];
static int selected_slot;
static int qca8k_qinq_rule(struct qca8k_priv *p, int slot,
                          const u32 v[5], const u32 m[5], const u32 a[3])
{
    selected_slot = slot;
    memcpy(values, v, sizeof(values));
    memcpy(masks, m, sizeof(masks));
    memcpy(actions, a, sizeof(actions));
    return 0;
}
'''
checks = r'''
int main(void)
{
    struct qca8k_priv priv = {};
    struct qca8k_qinq_mc group = { .group = htonl(0xef157801), .cvid = 3088,
                                  .cpu = 0, .slot = 94 };
    unsigned int seen = 0;
    unsigned int i;

    /* All 32 IPv4 addresses with the same group MAC must have different SVIDs. */
    for (i = 0; i < 32; i++) {
        u16 vid = dsa_qca_qinq_ipv4_mc_svid(htonl(0xe0157801 | (i << 23)));
        assert(vid >= 4000 && vid <= 4031 && !(seen & BIT(vid - 4000)));
        seen |= BIT(vid - 4000);
    }
    assert(seen == U32_MAX);

    /* Match the measured lab rule with S4000, C3088 and two outputs. */
    assert(!qca8k_qinq_mc_write(&priv, &group, BIT(1) | BIT(4)));
    assert(selected_slot == 94);
    assert(values[0] == 0x5e157801 && values[1] == 0x100);
    assert(values[2] == 0x100fa000 && values[3] == 0xc300000c && values[4] == 1);
    assert(masks[0] == U32_MAX && masks[1] == 0xffff && masks[2] == 0xff0fff00);
    assert(masks[3] == 0xf && masks[4] == 0xff);
    assert(actions[1] == 0x40000000 && actions[2] == 0x12);

    assert(!qca8k_qinq_mc_write(&priv, &group, BIT(4)));
    assert(actions[1] == 0 && actions[2] == 0x12);
    assert(!qca8k_qinq_mc_write(&priv, &group, 0));
    assert(!masks[4] && !actions[1] && !actions[2]);
    puts("PASS: QCA multicast aliases, exact VLAN/CPU match and bitmap withdrawal");
}
'''
source = '\n'.join((mock,
    function('include/linux/dsa/qca_qinq.h', 'dsa_qca_qinq_ipv4_mc_svid'),
    function('drivers/net/dsa/qca/qca8k-qinq.c', 'qca8k_qinq_mc_write'), checks))
with tempfile.TemporaryDirectory(prefix='qca-qinq-mc-') as tmp:
    cfile = Path(tmp) / 'check.c'
    binary = Path(tmp) / 'check'
    cfile.write_text(source)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Werror', str(cfile), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

path_mock = r'''
#include <arpa/inet.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define BIT(n) (1U << (n))
#define QCA8K_NUM_PORTS 7
#define ETH_P_8021AD 0x88a8
#define ETH_P_8021Q 0x8100
#define IS_ERR_OR_NULL(p) (!(p))
#define READ_ONCE(v) (v)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32, __be32;
typedef uint64_t u64;
struct list_head {
    struct list_head *next;
    struct net_device *dev;
};
struct net_device {
    int ifindex;
    struct net_device *master;
    struct dsa_port *dp;
    struct { struct list_head lower; } adj_list;
};
struct dsa_port {
    int index;
    struct dsa_switch *ds;
    struct dsa_port *cpu_dp;
    struct net_device *conduit;
};
struct dsa_switch { bool qinq_ipv4_mc_enabled; struct qca8k_priv *priv; };
struct qca8k_priv {
    u16 qinq_native_vid[QCA8K_NUM_PORTS];
    struct { u64 ready; } qinq_mc[2];
};
struct dsa_qinq_ipv4_mc_path { struct net_device *conduit; u32 vlan[2]; };
struct qca8k_qinq_mc_target { struct dsa_port *cpu; u16 cvid; u8 ports; };
static int queued;

static void qca8k_qinq_mc_queue(struct qca8k_priv *priv, __be32 group) { queued++; }
static bool dsa_user_dev_check(struct net_device *dev) { return dev->dp != NULL; }
static struct dsa_port *dsa_port_from_netdev(struct net_device *dev) { return dev->dp; }
static struct net_device *netdev_master_upper_dev_get_rcu(struct net_device *dev) {
    return dev->master;
}
static struct net_device *netdev_next_lower_dev_rcu(struct net_device *br, struct list_head **it) {
    *it = (*it)->next;
    return *it ? (*it)->dev : NULL;
}
static unsigned int hweight8(u8 ports) { return __builtin_popcount(ports); }
'''
path_checks = r'''
int main(void)
{
    struct qca8k_priv priv = { .qinq_native_vid = { [1] = 3088, [2] = 3088 } };
    struct dsa_switch ds = { .qinq_ipv4_mc_enabled = true, .priv = &priv };
    struct net_device br = {}, conduit = {};
    struct dsa_port cpu = { .index = 0, .conduit = &conduit };
    struct dsa_port cpu2 = { .index = 5 };
    struct dsa_port a = { .index = 1, .ds = &ds, .cpu_dp = &cpu };
    struct dsa_port b = { .index = 2, .ds = &ds, .cpu_dp = &cpu };
    struct net_device lan1 = { .ifindex = 21, .master = &br, .dp = &a };
    struct net_device lan2 = { .ifindex = 22, .master = &br, .dp = &b };
    struct dsa_qinq_ipv4_mc_path path = {};
    const u32 listeners[] = { 21, 22 };
    const __be32 group = htonl(0xef157801);

    struct list_head second = { .dev = &lan2 };
    struct list_head first = { .next = &second, .dev = &lan1 };
    br.adj_list.lower.next = &first;
    assert(!qca8k_qinq_ipv4_mc_path(&ds, &br, group, listeners, 2, &path));
    assert(queued == 1);
    priv.qinq_mc[0].ready = qca8k_qinq_mc_cookie(group, 0, 3088, BIT(1) | BIT(2));
    assert(qca8k_qinq_ipv4_mc_path(&ds, &br, group, listeners, 2, &path));
    assert(path.conduit == &conduit && path.vlan[0] == 0x88a80fa0 &&
           path.vlan[1] == 0x81000c10);
    assert(!qca8k_qinq_ipv4_mc_path(&ds, &br, group, listeners, 1, &path));
    assert(queued == 2);
    b.cpu_dp = &cpu2;
    assert(!qca8k_qinq_ipv4_mc_path(&ds, &br, group, listeners, 2, &path));
    b.cpu_dp = &cpu;
    lan2.master = NULL;
    assert(!qca8k_qinq_ipv4_mc_path(&ds, &br, group, listeners, 2, &path));
    lan2.master = &br;
    ds.qinq_ipv4_mc_enabled = false;
    assert(!qca8k_qinq_ipv4_mc_path(&ds, &br, group, listeners, 2, &path));
    assert(queued == 2);
    puts("PASS: QCA publishes tags only for the exact installed receiver set");
}
'''
path_source = '\n'.join((
    path_mock, function('include/linux/dsa/qca_qinq.h', 'dsa_qca_qinq_ipv4_mc_svid'),
    *(function('drivers/net/dsa/qca/qca8k-qinq.c', name) for name in
      ('qca8k_qinq_mc_cookie', 'qca8k_qinq_mc_target', 'qca8k_qinq_ipv4_mc_path')),
    path_checks))
with tempfile.TemporaryDirectory(prefix='qca-qinq-mc-path-') as tmp:
    cfile = Path(tmp) / 'check.c'
    binary = Path(tmp) / 'check'
    cfile.write_text(path_source)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Werror', str(cfile), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

bridge_mock = r'''
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef uint32_t u32, __be32;
#define EXPORT_SYMBOL_GPL(name)
#define ETH_P_IP 0x0800
#define BROPT_MULTICAST_ENABLED 1
#define MCAST_EXCLUDE 0
#define MDB_PG_FLAGS_BLOCKED 1
#define BR_MULTICAST_TO_UNICAST 2
#define BR_ISOLATED 4
#define BR_PORT_LOCKED 8
#define BR_STATE_FORWARDING 3
struct net_device { int ifindex; bool bridge, running, vlan; void *priv; };
struct br_ip { union { __be32 ip4; } dst; unsigned short proto; };
struct net_bridge_mcast {
    int multicast_igmp_version, multicast_querier;
    int ip4_mc_router_list, ip4_other_query;
};
struct net_bridge { struct net_bridge_mcast multicast_ctx; int multicast_lock; bool enabled; };
struct net_bridge_port { struct net_device *dev; unsigned int flags; int state; };
struct net_bridge_port_group {
    struct { struct net_bridge_port *port; } key;
    struct net_bridge_port_group *next;
    int filter_mode, src_list, flags;
};
struct net_bridge_mdb_entry { struct net_bridge_port_group *ports; bool host_joined; };
static struct net_bridge_mdb_entry mdb;
static bool has_entry = true;
static bool ipv4_is_multicast(__be32 address) { return (ntohl(address) >> 28) == 14; }
static bool ipv4_is_local_multicast(__be32 address) { return (ntohl(address) >> 8) == 0xe00000; }
static void spin_lock_bh(int *held) { assert((*held)++ == 0); }
static void spin_unlock_bh(int *held) { assert((*held)-- == 1); }
#define netif_is_bridge_master(dev) ((dev)->bridge)
#define netif_running(dev) ((dev)->running)
#define netdev_priv(dev) ((dev)->priv)
#define br_vlan_enabled(dev) ((dev)->vlan)
#define br_opt_get(br, opt) ((br)->enabled)
#define hlist_empty(ptr) (!*(ptr))
#define mlock_dereference(pg, br) ((pg))
#define __br_multicast_querier_exists(ctx, other, v6) (!*(other))
static struct net_bridge_mdb_entry *br_mdb_ip_get(struct net_bridge *br, struct br_ip *key)
{
    assert(br->multicast_lock == 1 && key->proto == htons(ETH_P_IP));
    assert(key->dst.ip4 == htonl(0xef157801));
    return has_entry ? &mdb : NULL;
}
'''
bridge_checks = r'''
int main(void)
{
    struct net_bridge br = { .multicast_ctx = {2, 1, 0, 0}, .enabled = true };
    struct net_device bridge = { .bridge = true, .running = true, .priv = &br };
    struct net_device devices[] = {{ .ifindex = 4 }, { .ifindex = 7 }};
    struct net_bridge_port ports[] = {{ devices, 0, 3 }, { devices + 1, 0, 3 }};
    struct net_bridge_port_group first = { .key = { ports } };
    struct net_bridge_port_group second = { .key = { ports + 1 } };
    const __be32 address = htonl(0xef157801);
    u32 selected[2];

    mdb.ports = &first;
    assert(br_qinq_ipv4_mc_members(&bridge, address, selected, 2) == 1 && selected[0] == 4);
    first.next = &second;
    assert(br_qinq_ipv4_mc_members(&bridge, address, selected, 2) == 2 && selected[1] == 7);
    assert(br_qinq_ipv4_mc_members(&bridge, address, selected, 1) == -ENOSPC);
    ports[0].state = 0;
    assert(br_qinq_ipv4_mc_members(&bridge, address, selected, 2) == 1 && selected[0] == 7);
    ports[0].state = 3;
    second.src_list = 1;
    assert(br_qinq_ipv4_mc_members(&bridge, address, selected, 2) == -EOPNOTSUPP);
    second.src_list = 0;
    ports[1].flags = BR_MULTICAST_TO_UNICAST;
    assert(br_qinq_ipv4_mc_members(&bridge, address, selected, 2) == -EOPNOTSUPP);
    ports[1].flags = 0;
    mdb.host_joined = true;
    assert(br_qinq_ipv4_mc_members(&bridge, address, selected, 2) == -EOPNOTSUPP);
    mdb.host_joined = false;
    br.multicast_ctx.ip4_mc_router_list = 1;
    assert(br_qinq_ipv4_mc_members(&bridge, address, selected, 2) == -EOPNOTSUPP);
    br.multicast_ctx.ip4_mc_router_list = 0;
    bridge.vlan = true;
    assert(br_qinq_ipv4_mc_members(&bridge, address, selected, 2) == -EOPNOTSUPP);
    bridge.vlan = false;
    has_entry = false;
    assert(!br_qinq_ipv4_mc_members(&bridge, address, selected, 2));
    assert(!br.multicast_lock);
    puts("PASS: native MDB joins, blocked ports, unsupported receivers and last leave");
}
'''
source = '\n'.join((bridge_mock,
    function('net/bridge/br_multicast.c', 'br_qinq_ipv4_mc_members'), bridge_checks))
with tempfile.TemporaryDirectory(prefix='qca-qinq-mdb-') as tmp:
    cfile = Path(tmp) / 'check.c'
    binary = Path(tmp) / 'check'
    cfile.write_text(source)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Werror', str(cfile), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
