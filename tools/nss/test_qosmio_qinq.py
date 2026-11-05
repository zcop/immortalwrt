#!/usr/bin/env python3
"""Execute the actual adapter lookup/resync C functions with mocked kernel APIs.

This checks mapping and readiness logic, not firmware acceptance or hardware.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'package/kernel/qca-nss-qinq/src/qca_nss_qinq.c').read_text()


def function(name, text=source):
    match = re.search(r'(?:static\s+)?(?:bool|int)\s+' + name +
                      r'\s*\([^;{}]*\)\s*\{', text)
    assert match, name
    depth = 1
    end = match.end()
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[match.start():end]


mock = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
typedef uint16_t u16;
#define DSA_NSS_MAX_NODES 64
#define spin_lock_bh(x) ((void)0)
#define spin_unlock_bh(x) ((void)0)
#define mutex_lock(x) ((void)0)
#define mutex_unlock(x) ((void)0)
#define rtnl_lock() ((void)0)
#define rtnl_unlock() ((void)0)
struct net_device {
    int phys, index, refs;
    bool is_vlan, is_dsa, vlan_enabled;
    u16 vid;
    struct net_device *real;
    struct dsa_port *dp;
    bool vlans[4096];
};
struct dsa_nss_node {
    bool ready;
    u16 svid, vid;
    int parent_if_num, if_num, phys_if;
    struct net_device *ndev;
};
struct dsa_nss_want {
    u16 svid, vid;
    int phys_if;
    struct net_device *ndev;
};
static struct dsa_nss_node dsa_nss_nodes[DSA_NSS_MAX_NODES];
static bool dsa_nss_waiting_phys, dsa_nss_reconfiguring, mock_waiting;
static int dsa_nss_resyncs, dsa_nss_changed, mock_defunct_count;
static void atomic_inc(int *p) { ++*p; }
static int atomic_xchg(int *p, int n) { int old = *p; *p = n; return old; }
static int nss_cmn_get_interface_number_by_dev(struct net_device *dev) { return dev->phys; }
static void dsa_nss_defunct(void) { ++mock_defunct_count; }
static bool dsa_nss_is_root(const struct dsa_nss_node *n) { return !n->vid; }
static int dsa_nss_collect(struct dsa_nss_want *w, int max) {
    dsa_nss_waiting_phys = mock_waiting;
    return 0;
}
static bool dsa_nss_node_matches(const struct dsa_nss_node *n,
                               const struct dsa_nss_want *w) { return false; }
static bool dsa_nss_root_wanted(const struct dsa_nss_node *n,
                              const struct dsa_nss_want *w, int count) { return false; }
static struct dsa_nss_node *dsa_nss_find_root(u16 svid, int phys) { return NULL; }
static void dsa_nss_node_destroy(struct dsa_nss_node *n, const char *reason) { n->if_num = -1; }
static int dsa_nss_node_create(struct dsa_nss_node *n,
                              const struct dsa_nss_want *w, int parent) { return -EAGAIN; }
'''

checks = r'''
static void clear_nodes(void) {
    memset(dsa_nss_nodes, 0, sizeof(dsa_nss_nodes));
    for (int i = 0; i < DSA_NSS_MAX_NODES; i++) dsa_nss_nodes[i].if_num = -1;
    dsa_nss_reconfiguring = false;
}
static void node(int index, int phys, u16 svid, u16 vid, int ifnum, int parent) {
    dsa_nss_nodes[index] = (struct dsa_nss_node) {
        .ready = true, .phys_if = phys, .svid = svid, .vid = vid,
        .if_num = ifnum, .parent_if_num = parent
    };
}
int main(void) {
    struct net_device wan = {.phys = 0}, lan = {.phys = 1}, missing = {.phys = -1};
    u16 vid;
    clear_nodes();
    node(0, 1, 3072, 0, 100, -1);
    node(1, 1, 3072, 10, 101, 100);
    vid = 10; assert(qca_nss_qinq_resolve(&lan, 3072, &vid) == 101);
    vid = 0; assert(qca_nss_qinq_resolve(&lan, 3072, &vid) == 101 && vid == 10);
    vid = 10; assert(qca_nss_qinq_resolve(&wan, 3072, &vid) < 0);
    vid = 10; assert(qca_nss_qinq_resolve(&missing, 3072, &vid) < 0);
    node(2, 1, 3072, 20, 102, 100);
    vid = 0; assert(qca_nss_qinq_resolve(&lan, 3072, &vid) < 0 && vid == 0);
    vid = 20; assert(qca_nss_qinq_resolve(&lan, 3072, &vid) == 102);
    node(3, 0, 3072, 0, 110, -1);
    node(4, 0, 3072, 10, 111, 110);
    vid = 10; assert(qca_nss_qinq_resolve(&wan, 3072, &vid) == 111);
    vid = 10; assert(qca_nss_qinq_resolve(&lan, 3072, &vid) == 101);
    node(5, 1, 3073, 0, 120, -1);
    node(6, 1, 3073, 10, 121, 120);
    vid = 10; assert(qca_nss_qinq_resolve(&lan, 3073, &vid) == 121);
    assert(dsa_nss_lookup(1, 3073, 10) == 121);
    dsa_nss_nodes[6].ready = false;
    vid = 10; assert(qca_nss_qinq_resolve(&lan, 3073, &vid) < 0);
    assert(dsa_nss_lookup(1, 3073, 10) < 0);
    dsa_nss_nodes[6].ready = true;
    dsa_nss_nodes[5].ready = false;
    vid = 10; assert(qca_nss_qinq_resolve(&lan, 3073, &vid) < 0);
    dsa_nss_nodes[5].ready = true;
    dsa_nss_nodes[6].parent_if_num = 999;
    vid = 10; assert(qca_nss_qinq_resolve(&lan, 3073, &vid) < 0);
    dsa_nss_nodes[6].parent_if_num = 120;
    dsa_nss_reconfiguring = true;
    vid = 10; assert(qca_nss_qinq_resolve(&lan, 3073, &vid) < 0);
    assert(dsa_nss_lookup(1, 3073, 10) < 0);
    puts("PASS: exact conduit/port/VID, ambiguous VID, readiness and parent identity");

    clear_nodes(); mock_waiting = true; dsa_nss_changed = 1;
    assert(dsa_nss_resync());
    assert(dsa_nss_reconfiguring && mock_defunct_count == 1);
    mock_waiting = false;
    assert(!dsa_nss_resync());
    assert(!dsa_nss_reconfiguring && mock_defunct_count == 1);
    puts("PASS: empty desired set retries while NSS physical interfaces are unavailable");
    return 0;
}
'''

functions = '\n\n'.join(function(name) for name in (
    'dsa_nss_parent_ready', 'dsa_nss_lookup', 'qca_nss_qinq_resolve', 'dsa_nss_resync'))
with tempfile.TemporaryDirectory(prefix='qosmio-qinq-') as directory:
    path = Path(directory)
    (path / 'test.c').write_text(mock + functions + checks)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-Wno-unused-function',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)

# Exercise the actual patched ECM side resolver from the prepared package.
# This integration check runs after building the pinned qosmio profile.
prepared = root / ('build_dir/target-aarch64_cortex-a53_musl/'
                   'linux-qualcommax_ipq50xx/'
                   'qca-nss-ecm-12.5.2024.11.06~30fbfa4/'
                   'frontends/nss/ecm_nss_common.c')
ecm = prepared.read_text()
assert 'not merely one' in ecm, 'Build the latest qosmio ECM overlay first'
ecm_mock = r'''
typedef uint32_t u32;
#define IS_ERR_OR_NULL(p) (!(p))
#define DSA_TAG_PROTO_YT921X_QINQ 35
#define DSA_TAG_8021Q_MAX_NUM_BRIDGES 7
#define ECM_DB_IFACE_HEIRARCHY_MAX 8
#define NSS_MAX_PHYSICAL_INTERFACES 8
#define VLAN_VID_MASK 4095
#define ETH_P_8021AD 0x88a8U
#define ETH_P_8021Q 0x8100U
#define rcu_read_lock() ((void)0)
#define rcu_read_unlock() ((void)0)
#define symbol_get(name) (name)
#define symbol_put(name) ((void)0)
#define init_net 0
struct tag_ops { int proto; };
struct dsa_tree { struct tag_ops *tag_ops; };
struct dsa_switch { struct dsa_tree *dst; void *tag_8021q_ctx; };
struct dsa_cpu { struct net_device *conduit; };
struct dsa_port {
    struct dsa_switch *ds;
    struct dsa_cpu *cpu_dp;
    struct net_device *bridge;
    int port, bridge_num;
};
struct bridge_vlan_info { u16 vid; };
struct ecm_db_connection_instance { bool routed; };
typedef int ecm_db_iface_type_t;
enum { ECM_DB_IFACE_TYPE_ETHERNET, ECM_DB_IFACE_TYPE_BRIDGE,
       ECM_DB_IFACE_TYPE_VLAN, ECM_DB_IFACE_TYPE_PPPOE };
struct ecm_db_iface_instance { int index, type; };
struct ecm_nss_qinq_side { int physical, leaf; u32 outer, inner; };
static struct net_device *devices[8];
static struct net_device *dev_get_by_index(int *net, int index) {
    struct net_device *dev = devices[index];
    if (dev) ++dev->refs;
    return dev;
}
static void dev_put(struct net_device *dev) { assert(dev->refs > 0); --dev->refs; }
static bool dsa_user_dev_check(struct net_device *dev) { return dev->is_dsa; }
static bool is_vlan_dev(struct net_device *dev) { return dev->is_vlan; }
static struct net_device *vlan_dev_real_dev(struct net_device *dev) { return dev->real; }
static u16 vlan_dev_vlan_id(struct net_device *dev) { return dev->vid; }
static struct dsa_port *dsa_port_from_netdev(struct net_device *dev) { return dev->dp; }
static struct net_device *dsa_port_bridge_dev_get(struct dsa_port *dp) { return dp->bridge; }
static int dsa_port_bridge_num_get(struct dsa_port *dp) { return dp->bridge_num; }
static u16 dsa_tag_8021q_standalone_vid(struct dsa_port *dp) { return 3072 + dp->port; }
static u16 dsa_tag_8021q_bridge_vid(int id) { return 3088 + id; }
static bool br_vlan_enabled(struct net_device *dev) { return dev->vlan_enabled; }
static int br_vlan_get_info_rcu(struct net_device *dev, u16 vid,
                              struct bridge_vlan_info *info) {
    info->vid = vid;
    return dev->vlans[vid] ? 0 : -ENOENT;
}
static int ecm_db_iface_interface_identifier_get(struct ecm_db_iface_instance *ii) { return ii->index; }
static int ecm_db_iface_type_get(struct ecm_db_iface_instance *ii) { return ii->type; }
static bool ecm_db_connection_is_routed_get(struct ecm_db_connection_instance *ci) { return ci->routed; }
'''.replace('#define init_net 0', 'static int init_net;')

ecm_checks = r'''
static void clear_nodes(void) {
    memset(dsa_nss_nodes, 0, sizeof(dsa_nss_nodes));
    for (int i = 0; i < DSA_NSS_MAX_NODES; i++) dsa_nss_nodes[i].if_num = -1;
    dsa_nss_reconfiguring = false;
}
static void node(int index, int phys, u16 svid, u16 vid, int ifnum, int parent) {
    dsa_nss_nodes[index] = (struct dsa_nss_node) {
        .ready = true, .phys_if = phys, .svid = svid, .vid = vid,
        .if_num = ifnum, .parent_if_num = parent
    };
}
int main(void) {
    struct net_device conduit = {.phys = 1};
    struct net_device port = {.index = 1, .is_dsa = true};
    struct net_device bridge = {.index = 2};
    struct net_device upper = {.index = 3, .is_vlan = true, .real = &bridge, .vid = 10};
    struct tag_ops ops = {.proto = 35};
    struct dsa_tree tree = {.tag_ops = &ops};
    struct dsa_switch ds = {.dst = &tree, .tag_8021q_ctx = &tree};
    struct dsa_cpu cpu = {.conduit = &conduit};
    struct dsa_port dp = {.ds = &ds, .cpu_dp = &cpu, .bridge_num = 1};
    struct ecm_db_iface_instance port_ii = {.index = 1, .type = ECM_DB_IFACE_TYPE_ETHERNET};
    struct ecm_db_iface_instance upper_ii = {.index = 3, .type = ECM_DB_IFACE_TYPE_VLAN};
    struct ecm_db_iface_instance *ifaces[8] = {0};
    struct ecm_db_connection_instance ci = {.routed = true};
    struct ecm_nss_qinq_side side;
    port.dp = &dp; devices[1] = &port; devices[2] = &bridge; devices[3] = &upper;
    ifaces[7] = &port_ii;
    clear_nodes(); node(0, 1, 3072, 0, 100, -1); node(1, 1, 3072, 3072, 101, 100);
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 7, &side) == 1);
    assert(side.physical == 1 && side.leaf == 101 && side.outer == 0x88a80c00U && side.inner == 0x81000c00U);
    dp.bridge = &bridge; dsa_nss_nodes[1].vid = 3089;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 7, &side) == 1 && side.inner == 0x81000c11U);
    bridge.vlan_enabled = true; port.vlans[10] = true; dsa_nss_nodes[1].vid = 10;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 7, &side) == 1 && side.inner == 0x8100000aU);
    port.vlans[20] = true;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 7, &side) < 0); /* node cache still only VID10 */
    ifaces[6] = &port_ii; ifaces[7] = &upper_ii;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) == 1 && side.inner == 0x8100000aU);
    upper.vid = 20;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0);
    node(2, 1, 3072, 20, 102, 100);
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) == 1 && side.leaf == 102 && side.inner == 0x81000014U);
    port.vlans[20] = false;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0);
    upper.real = &port;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0); /* unsupported direct VLAN upper */
    upper.real = &bridge; upper.vid = 10; upper_ii.type = ECM_DB_IFACE_TYPE_PPPOE;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0);
    upper_ii.type = ECM_DB_IFACE_TYPE_VLAN; ci.routed = false;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0);
    ci.routed = true; ops.proto = 1;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 7, &side) == 0);
    assert(port.refs == 0 && bridge.refs == 0 && upper.refs == 0);
    puts("PASS: actual ECM resolver handles standalone/bridge/L3 VLAN and rejects stale, ambiguous or unsupported paths");
    return 0;
}
'''

ecm_functions = '\n\n'.join(function(name, ecm) for name in (
    'ecm_nss_qinq_is_port', 'ecm_nss_qinq_is_upper', 'ecm_nss_qinq_side_get'))
with tempfile.TemporaryDirectory(prefix='qosmio-ecm-') as directory:
    path = Path(directory)
    (path / 'test.c').write_text(mock + functions + ecm_mock + ecm_functions + ecm_checks)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-Wno-unused-function',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
