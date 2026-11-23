#!/usr/bin/env python3
"""Execute the actual adapter lookup/resync C functions with mocked kernel APIs.

This checks mapping and readiness logic, not firmware acceptance or hardware.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'package/kernel/qca-nss-qinq/src/qca_nss_qinq.c').read_text()
header = (root / 'target/linux/generic/files/include/linux/dsa/yt921x_qinq.h').read_text()
qca_header = Path(os.environ.get('QCA_QINQ_HEADER', str(root /
    'build_dir/target-aarch64_cortex-a53_musl/linux-qualcommax_ipq50xx/linux-6.18.60/include/linux/dsa/qca_qinq.h'))).read_text()



def function(name, text=source):
    match = re.search(r'(?:static\s+)?(?:inline\s+)?(?:bool|int|void|struct\s+(?:dsa_port|net_device)\s*\*)\s*' + name +
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
#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
typedef uint16_t u16;
#define DSA_NSS_MAX_NODES 64
#define DSA_NSS_COALESCE_MS 100
#define DSA_NSS_RETRY_MS 2000
#define DSA_NSS_MAX_RETRIES 30
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x, v) ((x) = (v))
#define msecs_to_jiffies(x) ((unsigned long)(x))
#define system_wq ((void *)0)
#define pr_info(...) ((void)0)
#define pr_warn(...) ((void)0)
#define spin_lock_bh(x) ((void)0)
#define spin_unlock_bh(x) ((void)0)
#define mutex_lock(x) ((void)0)
#define mutex_unlock(x) ((void)0)
#define rtnl_lock() ((void)0)
#define rtnl_unlock() ((void)0)
struct net_device {
    int phys, refs, features, type, flags;
    union { int index; int ifindex; };
    bool is_vlan, is_dsa, vlan_enabled, is_bridge;
    u16 vid, proto;
    struct net_device *real, *master;
    struct dsa_port *dp;
    bool vlans[4096];
    struct net_device *vlan_uppers[8];
    int vlan_upper_count;
    bool running, carrier;
    int stp_state;
    unsigned long bridge_flags;
    void *ieee80211_ptr;
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
struct work_struct { int unused; };
struct delayed_work { int unused; };
static struct dsa_nss_node dsa_nss_nodes[DSA_NSS_MAX_NODES];
static bool dsa_nss_waiting_phys, dsa_nss_reconfiguring, mock_waiting;
static bool dsa_nss_stopping, dsa_nss_retry_exhausted;
static unsigned int dsa_nss_retries, port_id_tpid = 0x88a8, dsa_nss_generation;
static int dsa_nss_resyncs, dsa_nss_changed, mock_defunct_count;
static struct delayed_work dsa_nss_work;
static void atomic_inc(int *p) { ++*p; }
static int atomic_xchg(int *p, int n) { int old = *p; *p = n; return old; }
static void atomic_set(int *p, int n) { *p = n; }
static int mod_delayed_work(void *wq, void *work, unsigned long delay) { return 0; }
static int schedule_delayed_work(struct delayed_work *work, unsigned long delay) { return 0; }
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
    dsa_nss_retry_exhausted = false;
    dsa_nss_stopping = false;
    vid = 10; assert(qca_nss_qinq_resolve(&lan, 3073, &vid) == -EAGAIN);
    assert(dsa_nss_lookup(1, 3073, 10) == -1);
    assert(qca_nss_qinq_root_if_num(&lan, 3073) == -1);
    assert(qca_nss_qinq_lookup(&lan, 3073, 10) == -1);
    dsa_nss_retry_exhausted = true;
    vid = 10; assert(qca_nss_qinq_resolve(&lan, 3073, &vid) == -1);
    assert(qca_nss_qinq_root_if_num(&lan, 3073) == -1);
    assert(qca_nss_qinq_lookup(&lan, 3073, 10) == -1);
    dsa_nss_retry_exhausted = false;
    dsa_nss_stopping = true;
    vid = 10; assert(qca_nss_qinq_resolve(&lan, 3073, &vid) == -1);
    dsa_nss_stopping = false;
    dsa_nss_reconfiguring = false;
    puts("PASS: exact conduit/port/VID, ambiguous VID, readiness and parent identity");

    dsa_nss_retry_exhausted = true;
    dsa_nss_retries = 7;
    dsa_nss_changed = 0;
    dsa_nss_schedule();
    assert(dsa_nss_reconfiguring && !dsa_nss_retry_exhausted &&
           dsa_nss_retries == 0 && dsa_nss_generation == 1);
    dsa_nss_mark_exhausted(dsa_nss_generation - 1);
    assert(!dsa_nss_retry_exhausted);
    dsa_nss_mark_exhausted(dsa_nss_generation);
    assert(dsa_nss_retry_exhausted);
    dsa_nss_schedule();
    assert(!dsa_nss_retry_exhausted);
    puts("PASS: schedule opens a fresh retry window and stale give-up is ignored");

    clear_nodes();
    mock_waiting = true;
    dsa_nss_changed = 0;
    dsa_nss_retries = DSA_NSS_MAX_RETRIES;
    dsa_nss_work_fn(NULL);
    assert(dsa_nss_reconfiguring && dsa_nss_retry_exhausted);
    vid = 0; assert(qca_nss_qinq_resolve(&lan, 3073, &vid) == -1);
    dsa_nss_schedule();
    assert(!dsa_nss_retry_exhausted);
    dsa_nss_reconfiguring = false;
    mock_waiting = false;
    puts("PASS: terminal give-up is hard until a real resync is scheduled");

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
    'dsa_nss_parent_ready', 'dsa_nss_lookup',
    'qca_nss_qinq_root_if_num', 'qca_nss_qinq_lookup',
    'qca_nss_qinq_resolve', 'dsa_nss_resync',
    'dsa_nss_mark_exhausted', 'dsa_nss_work_fn', 'dsa_nss_schedule'))
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
prepared_dir = prepared.parent
ecm = Path(os.environ.get('ECM_QINQ_SOURCE', str(prepared))).read_text()
ecm_ipv4 = Path(os.environ.get('ECM_QINQ_IPV4_SOURCE',
                               str(prepared_dir / 'ecm_nss_ported_ipv4.c'))).read_text()
ecm_ipv6 = Path(os.environ.get('ECM_QINQ_IPV6_SOURCE',
                               str(prepared_dir / 'ecm_nss_ported_ipv6.c'))).read_text()
assert 'dsa_yt921x_qinq_direct_vid_valid(dp, cvid)' in ecm, 'Build the latest direct-VLAN ECM overlay first'
assert 'dsa_yt921x_qinq_bridge_upper_valid(dp, dev)' in ecm, 'Apply 0108-qinq-support-vlan-unaware-bridge-uppers.patch first'
assert 'side->leaf == -EAGAIN' in ecm, 'Apply 0107-retry-transient-qinq-resolution.patch first'
assert 'ecm_nss_qinq_ipv4_rule_retry' in ecm_ipv4, 'Apply 0107-retry-transient-qinq-resolution.patch first'
assert 'ecm_nss_qinq_ipv6_rule_retry' in ecm_ipv6, 'Apply 0107-retry-transient-qinq-resolution.patch first'
ecm_mock = r'''
typedef uint32_t u32;
#define IS_ERR_OR_NULL(p) (!(p))
#define DSA_TAG_PROTO_YT921X_QINQ 35
#define DSA_TAG_PROTO_QCA_QINQ 39
#define NETIF_F_HW_VLAN_CTAG_FILTER 1
#define DSA_TAG_8021Q_MAX_NUM_BRIDGES 7
#define ECM_DB_IFACE_HEIRARCHY_MAX 8
#define NSS_MAX_PHYSICAL_INTERFACES 8
#define ECM_INTERFACE_PPPOE_ENABLE
#define ARPHRD_PPP 512
#define ECM_DB_XREF_ENABLE
#define ECM_BAND_STEERING_ENABLE
#define ETH_ALEN 6
#define BR_STATE_FORWARDING 3
#define BR_ISOLATED 1
#define BR_PORT_LOCKED 2
#define BR_PORT_MAB 4
#define BR_PROXYARP 8
#define BR_PROXYARP_WIFI 16
#define BR_NEIGH_SUPPRESS 32
#define BR_NEIGH_VLAN_SUPPRESS 64
#define BR_VLAN_TUNNEL 128
#define ECM_DB_OBJ_DIR_FROM 0
#define ECM_DB_OBJ_DIR_TO_NAT 1
#define IFF_POINTOPOINT 0x10
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
struct dsa_switch {
    struct dsa_tree *dst;
    void *tag_8021q_ctx;
    unsigned int max_num_bridges;
    struct dsa_port *mock_user_port;
};
struct dsa_cpu { struct net_device *conduit; };
struct dsa_port {
    struct dsa_switch *ds;
    struct dsa_cpu *cpu_dp;
    struct net_device *bridge, *user;
    int port, bridge_num;
};
struct bridge_vlan_info { u16 vid; };
struct ecm_db_connection_instance { bool routed; unsigned char mac[2][6]; };
typedef int ecm_db_iface_type_t;
enum { ECM_DB_IFACE_TYPE_ETHERNET, ECM_DB_IFACE_TYPE_BRIDGE,
       ECM_DB_IFACE_TYPE_VLAN, ECM_DB_IFACE_TYPE_PPPOE };
struct ecm_db_iface_instance { int index, type, ae; };
struct ecm_nss_qinq_side { int physical, leaf; u32 outer, inner; };
static struct net_device *devices[32];
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
static u16 vlan_dev_vlan_proto(struct net_device *dev) { return htons(dev->proto); }
static struct net_device *__vlan_find_dev_deep_rcu(struct net_device *dev,
                                                   u16 proto, u16 vid) {
    for (int i = 0; i < dev->vlan_upper_count; i++) {
        struct net_device *upper = dev->vlan_uppers[i];
        if (vlan_dev_vlan_proto(upper) == proto &&
            vlan_dev_vlan_id(upper) == vid)
            return upper;
    }
    return NULL;
}
static void mock_publish_upper(struct net_device *bridge, struct net_device *upper) {
    bridge->vlan_uppers[bridge->vlan_upper_count++] = upper;
}
static void mock_unpublish_upper(struct net_device *bridge, struct net_device *upper) {
    for (int i = 0; i < bridge->vlan_upper_count; i++)
        if (bridge->vlan_uppers[i] == upper) {
            bridge->vlan_uppers[i] = bridge->vlan_uppers[--bridge->vlan_upper_count];
            return;
        }
}
static bool netif_is_bridge_master(struct net_device *dev) { return dev->is_bridge; }
static bool netif_is_bridge_port(struct net_device *dev) { return dev->master && dev->master->is_bridge; }
static bool netif_running(struct net_device *dev) { return dev->running; }
static bool netif_carrier_ok(struct net_device *dev) { return dev->carrier; }
static int br_port_get_stp_state_rcu(struct net_device *dev) { return dev->stp_state; }
static bool br_port_flag_is_set(struct net_device *dev, unsigned long mask) { return dev->bridge_flags & mask; }
static bool is_valid_ether_addr(const unsigned char *mac) {
    static const unsigned char zero[6] = {0};
    return !(mac[0] & 1) && memcmp(mac, zero, 6);
}
static void ecm_db_connection_node_address_get(struct ecm_db_connection_instance *ci, int dir, unsigned char *mac) {
    memcpy(mac, ci->mac[dir], 6);
}
static struct net_device *mock_fdb_from, *mock_fdb_to;
static struct net_device *br_port_dev_get(struct net_device *bridge, unsigned char *mac, void *skb, unsigned int cookie) {
    struct net_device *dev = mac[5] == 1 ? mock_fdb_from : mock_fdb_to;
    if (!dev || dev->master != bridge) return NULL;
    ++dev->refs;
    return dev;
}
static struct net_device *netdev_master_upper_dev_get_rcu(struct net_device *dev) { return dev->master; }
static struct net_device *netdev_master_upper_dev_get(struct net_device *dev) { return dev->master; }
static struct dsa_port *dsa_port_from_netdev(struct net_device *dev) { return dev->dp; }
static struct net_device *dsa_port_bridge_dev_get(const struct dsa_port *dp) { return dp->bridge; }
static int dsa_port_bridge_num_get(struct dsa_port *dp) { return dp->bridge_num; }
static u16 dsa_tag_8021q_standalone_vid(const struct dsa_port *dp) { return 3072 + dp->port; }
static u16 dsa_tag_8021q_bridge_vid(int id) { return 3088 + id; }
#define dsa_switch_for_each_user_port(dp, ds) \
    for ((dp) = (ds)->mock_user_port; (dp); (dp) = NULL)
static bool br_vlan_enabled(struct net_device *dev) { return dev->vlan_enabled; }
static int br_vlan_get_info_rcu(struct net_device *dev, u16 vid,
                              struct bridge_vlan_info *info) {
    info->vid = vid;
    return dev->vlans[vid] ? 0 : -ENOENT;
}
static bool dsa_yt921x_qinq_direct_vid_valid(struct dsa_port *dp, u16 vid) {
    return !dp->bridge && dp->user && dp->user->features && dp->user->vlans[vid];
}
static int ecm_db_iface_interface_identifier_get(struct ecm_db_iface_instance *ii) { return ii->index; }
static int ecm_db_iface_ae_interface_identifier_get(struct ecm_db_iface_instance *ii) { return ii->ae; }
static int ecm_db_iface_type_get(struct ecm_db_iface_instance *ii) { return ii->type; }
static bool ecm_db_connection_is_routed_get(struct ecm_db_connection_instance *ci) { return ci->routed; }
'''.replace('#define init_net 0', 'static int init_net;')

ecm_checks = r'''
static void clear_nodes(void) {
    memset(dsa_nss_nodes, 0, sizeof(dsa_nss_nodes));
    for (int i = 0; i < DSA_NSS_MAX_NODES; i++) dsa_nss_nodes[i].if_num = -1;
    dsa_nss_reconfiguring = false;
    dsa_nss_retry_exhausted = false;
    dsa_nss_stopping = false;
}
static void node(int index, int phys, u16 svid, u16 vid, int ifnum, int parent) {
    dsa_nss_nodes[index] = (struct dsa_nss_node) {
        .ready = true, .phys_if = phys, .svid = svid, .vid = vid,
        .if_num = ifnum, .parent_if_num = parent
    };
}
static void test_unaware_bridge_upper(void) {
    struct net_device conduit = {.phys = 1};
    struct net_device port = {.index = 1, .is_dsa = true, .features = 1};
    struct net_device bridge = {.index = 2};
    struct net_device upper35 = {.index = 3, .is_vlan = true, .real = &bridge, .vid = 35, .proto = 0x8100};
    struct net_device upper100 = {.index = 6, .is_vlan = true, .real = &bridge, .vid = 100, .proto = 0x8100};
    struct net_device upper4094 = {.index = 7, .is_vlan = true, .real = &bridge, .vid = 4094, .proto = 0x8100};
    struct net_device stacked = {.index = 4, .is_vlan = true, .real = &upper35, .vid = 36, .proto = 0x8100};
    struct net_device other = {.index = 5};
    struct tag_ops ops = {.proto = 35};
    struct dsa_tree tree = {.tag_ops = &ops};
    struct dsa_switch ds = {.dst = &tree, .tag_8021q_ctx = &tree, .max_num_bridges = 7};
    struct dsa_cpu cpu = {.conduit = &conduit};
    struct dsa_port dp = {.ds = &ds, .cpu_dp = &cpu, .bridge = &bridge, .bridge_num = 1};
    struct ecm_db_iface_instance port_ii = {.index = 1, .type = ECM_DB_IFACE_TYPE_ETHERNET};
    struct ecm_db_iface_instance upper35_ii = {.index = 3, .type = ECM_DB_IFACE_TYPE_VLAN};
    struct ecm_db_iface_instance upper100_ii = {.index = 6, .type = ECM_DB_IFACE_TYPE_VLAN};
    struct ecm_db_iface_instance upper4094_ii = {.index = 7, .type = ECM_DB_IFACE_TYPE_VLAN};
    struct ecm_db_iface_instance stacked_ii = {.index = 4, .type = ECM_DB_IFACE_TYPE_VLAN};
    struct ecm_db_iface_instance *ifaces[8] = {0};
    struct ecm_db_connection_instance ci = {.routed = true};
    struct ecm_nss_qinq_side side;

    for (int i = 0; i < 8; i++) devices[i] = NULL;
    port.dp = &dp; dp.user = &port; ds.mock_user_port = &dp;
    devices[1] = &port; devices[2] = &bridge; devices[3] = &upper35;
    devices[4] = &stacked; devices[5] = &other; devices[6] = &upper100; devices[7] = &upper4094;
    mock_publish_upper(&bridge, &upper35);
    mock_publish_upper(&bridge, &upper100);
    mock_publish_upper(&bridge, &upper4094);

    clear_nodes();
    node(0, 1, 3072, 0, 100, -1);
    node(1, 1, 3072, 3089, 101, 100);
    node(2, 1, 3072, 35, 102, 100);
    node(3, 1, 3072, 100, 103, 100);
    node(4, 1, 3072, 4094, 104, 100);
    node(5, 2, 3072, 0, 110, -1);
    node(6, 2, 3072, 35, 111, 110);
    ifaces[6] = &port_ii;

    ifaces[7] = &upper35_ii;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) == 1);
    assert(side.physical == 1 && side.leaf == 102 && side.outer == 0x88a80c00U && side.inner == 0x81000023U);
    assert(port.refs == 0 && upper35.refs == 0);

    dsa_nss_reconfiguring = true;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) == -EAGAIN);
    assert(port.refs == 0 && upper35.refs == 0);
    dsa_nss_reconfiguring = false;

    ifaces[7] = &upper100_ii;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) == 1 && side.leaf == 103 && side.inner == 0x81000064U);
    ifaces[7] = &upper4094_ii;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) == 1 && side.leaf == 104 && side.inner == 0x81000ffeU);

    /* Several customer leaves: native still resolves the internal bridge VID. */
    ifaces[7] = &port_ii;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 7, &side) == 1 && side.leaf == 101 && side.inner == 0x81000c11U);

    /* The leaf key includes the port's NSS physical interface. */
    conduit.phys = 2;
    ifaces[7] = &upper35_ii;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) == 1 && side.leaf == 111);
    conduit.phys = 1;

    mock_unpublish_upper(&bridge, &upper35);
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0);
    assert(port.refs == 0 && upper35.refs == 0);
    mock_publish_upper(&bridge, &upper35);

    upper35.real = &other;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0);
    upper35.real = &bridge;

    upper35.master = &other;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0);
    upper35.master = NULL;

    upper35.proto = 0x88a8;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0);
    upper35.proto = 0x8100;

    upper35.vid = 3091;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0);
    upper35.vid = 35;

    ifaces[5] = &port_ii; ifaces[6] = &upper35_ii; ifaces[7] = &stacked_ii;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 5, &side) < 0);
    assert(port.refs == 0 && upper35.refs == 0 && stacked.refs == 0);
    puts("PASS: actual ECM resolver selects exact unaware-bridge customer/native leaves and rejects stale/unrelated/mastered/S-tag/reserved/stacked uppers");
}
int main(void) {
    struct net_device conduit = {.phys = 1};
    struct net_device port = {.index = 1, .is_dsa = true, .features = 1};
    struct net_device bridge = {.index = 2};
    struct net_device upper = {.index = 3, .is_vlan = true, .real = &bridge, .vid = 10, .proto = 0x8100};
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
    port.dp = &dp; dp.user = &port; devices[1] = &port; devices[2] = &bridge; devices[3] = &upper;
    ifaces[7] = &port_ii;
    clear_nodes(); node(0, 1, 3072, 0, 100, -1); node(1, 1, 3072, 3072, 101, 100);
    dsa_nss_reconfiguring = true;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 7, &side) == -EAGAIN);
    assert(port.refs == 0);
    dsa_nss_retry_exhausted = true;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 7, &side) == -ENOENT);
    assert(port.refs == 0);
    dsa_nss_retry_exhausted = false;
    dsa_nss_stopping = true;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 7, &side) == -ENOENT);
    assert(port.refs == 0);
    dsa_nss_stopping = false;
    dsa_nss_reconfiguring = false;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 7, &side) == 1);
    assert(side.physical == 1 && side.leaf == 101 && side.outer == 0x88a80c00U && side.inner == 0x81000c00U);
    dp.bridge = &bridge; dsa_nss_nodes[1].vid = 3089;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 7, &side) == 1 && side.inner == 0x81000c11U);
    bridge.vlan_enabled = true; port.vlans[10] = true; dsa_nss_nodes[1].vid = 10;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 7, &side) == 1 && side.inner == 0x8100000aU);
    port.vlans[20] = true;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 7, &side) < 0); /* node cache still only VID10 */
    ifaces[6] = &port_ii; ifaces[7] = &upper_ii;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0);
    mock_publish_upper(&bridge, &upper);
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) == 1 && side.inner == 0x8100000aU);
    upper.vid = 20;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0);
    node(2, 1, 3072, 20, 102, 100);
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) == 1 && side.leaf == 102 && side.inner == 0x81000014U);
    port.vlans[20] = false;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0);
    mock_unpublish_upper(&bridge, &upper);
    upper.real = &port;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0); /* unsupported direct VLAN upper */
    upper.real = &bridge; upper.vid = 10; upper_ii.type = ECM_DB_IFACE_TYPE_PPPOE;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0);
    upper_ii.type = ECM_DB_IFACE_TYPE_VLAN; ci.routed = false;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0);
    ci.routed = true; ops.proto = 1;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 7, &side) == 0);
    ops.proto = 35; dp.bridge = NULL; ci.routed = true; upper.real = &port; upper.vid = 35;
    port.vlans[35] = true; ifaces[6] = &port_ii; ifaces[7] = &upper_ii;
    node(3, 1, 3072, 35, 103, 100); node(4, 1, 3072, 3072, 104, 100);
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) == 1 && side.leaf == 103 && side.inner == 0x81000023U);
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) == 1 && side.outer == 0x88a80c00U);
    ifaces[7] = &port_ii;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 7, &side) == 1 && side.leaf == 104 && side.inner == 0x81000c00U);
    ifaces[7] = &upper_ii; port.vlans[35] = false;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0); /* stale leaf cannot authorize removed upper */
    port.vlans[35] = true; upper.master = &bridge;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0);
    upper.master = NULL; upper.proto = 0x88a8;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0);
    upper.proto = 0x8100; upper.vid = 36; port.vlans[36] = true;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0); /* no ready VID36 leaf */
    upper.vid = 35;
    struct net_device stacked = {.index = 4, .is_vlan = true, .real = &upper, .vid = 36, .proto = 0x8100};
    struct ecm_db_iface_instance stacked_ii = {.index = 4, .type = ECM_DB_IFACE_TYPE_VLAN};
    devices[4] = &stacked; ifaces[5] = &port_ii; ifaces[6] = &upper_ii; ifaces[7] = &stacked_ii;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 5, &side) < 0 && stacked.refs == 0);
    assert(port.refs == 0 && bridge.refs == 0 && upper.refs == 0);
    test_unaware_bridge_upper();
    puts("PASS: actual ECM resolver handles standalone/bridge/L3 VLAN and rejects stale, ambiguous or unsupported paths");
    test_pppoe_hierarchy();
    /* QCA admits native, live direct and aware-bridge customer VLANs, plus plain PPP. */
    ops.proto = DSA_TAG_PROTO_QCA_QINQ;
    clear_nodes(); node(0, 1, 3072, 0, 100, -1);
    node(1, 1, 3072, 3072, 101, 100);
    port.dp = &dp; devices[1] = &port; devices[2] = &bridge; devices[3] = &upper;
    dp.bridge = NULL; ifaces[7] = &port_ii;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 7, &side) == 1 && side.leaf == 101);
    dp.bridge = &bridge; port.master = &bridge; bridge.vlan_enabled = false; dsa_nss_nodes[1].vid = 3089;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 7, &side) == 1 && side.inner == 0x81000c11U);
    bridge.vlan_enabled = true;
    memset(port.vlans, 0, sizeof(port.vlans)); port.vlans[10] = true; dsa_nss_nodes[1].vid = 10;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 7, &side) == 1 && side.inner == 0x8100000aU);
    port.vlans[20] = true;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 7, &side) < 0); /* ambiguous bare bridge */
    upper.real = &bridge; upper.vid = 10; upper.proto = 0x8100;
    ifaces[6] = &port_ii; ifaces[7] = &upper_ii; upper_ii.type = ECM_DB_IFACE_TYPE_VLAN;
    mock_publish_upper(&bridge, &upper);
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) == 1 && side.inner == 0x8100000aU);
    mock_unpublish_upper(&bridge, &upper);
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0); /* stale upper */
    mock_publish_upper(&bridge, &upper); upper.master = &port;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0);
    upper.master = NULL; port.vlans[10] = false;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0); /* stale leaf after membership removal */
    port.vlans[10] = true; ci.routed = false;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0);
    ci.routed = true; upper.proto = 0x88a8;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0);
    upper.proto = 0x8100; mock_unpublish_upper(&bridge, &upper); ifaces[7] = &port_ii;
    bridge.vlan_enabled = false; dp.bridge = NULL; port.master = NULL; dsa_nss_nodes[1].vid = 3072;
    port.master = &bridge; /* A non-DSA bridge master, e.g. bond, is outside scope. */
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 7, &side) == -EOPNOTSUPP);
    port.master = NULL;
    upper.real = &port; upper.proto = 0x8100; upper.vid = 35;
    ifaces[6] = &port_ii; ifaces[7] = &upper_ii; upper_ii.type = ECM_DB_IFACE_TYPE_VLAN;
    ds.mock_user_port = &dp; ds.max_num_bridges = 7;
    mock_publish_upper(&port, &upper);
    node(2, 1, 3072, 35, 102, 100);
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) == 1 && side.leaf == 102 && side.inner == 0x81000023U);
    mock_unpublish_upper(&port, &upper);
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0); /* stale FW leaf does not grant admission */
    mock_publish_upper(&port, &upper);
    port.features = 0;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0);
    port.features = 1; upper.vid = 4094; dsa_nss_nodes[2].vid = 4094;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) == 1 && side.inner == 0x81000ffeU);
    upper.vid = 3072;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0);
    upper.vid = 35; dsa_nss_nodes[2].vid = 35; dp.bridge = &bridge; port.master = &bridge;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0);
    dp.bridge = NULL; port.master = NULL; upper.master = &bridge;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0);
    upper.master = NULL; upper.proto = 0x88a8;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0);
    upper.proto = 0x8100;
    upper_ii.type = ECM_DB_IFACE_TYPE_PPPOE;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) == -EOPNOTSUPP);
    assert(port.refs == 0 && upper.refs == 0);
    puts("PASS: QCA direct and aware-bridge VLAN mapping, published uppers, exact membership and ambiguity checks; stale/mastered/S-tagged/PPP misuse rejected");
    return 0;
}
'''

pppoe_checks = r'''
static void test_pppoe_hierarchy(void) {
    struct net_device conduit = {.phys = 1};
    struct net_device port = {.index = 1, .is_dsa = true, .features = 1};
    struct net_device upper = {.index = 2, .is_vlan = true, .real = &port,
                               .vid = 35, .proto = 0x8100};
    struct net_device ppp = {.index = 3, .type = ARPHRD_PPP,
                             .flags = IFF_POINTOPOINT, .phys = 150};
    struct tag_ops ops = {.proto = DSA_TAG_PROTO_YT921X_QINQ};
    struct dsa_tree tree = {.tag_ops = &ops};
    struct dsa_switch ds = {.dst = &tree, .tag_8021q_ctx = &tree};
    struct dsa_cpu cpu = {.conduit = &conduit};
    struct dsa_port dp = {.ds = &ds, .cpu_dp = &cpu, .user = &port};
    struct ecm_db_iface_instance port_ii = {.index = 1, .type = ECM_DB_IFACE_TYPE_ETHERNET};
    struct ecm_db_iface_instance upper_ii = {.index = 2, .type = ECM_DB_IFACE_TYPE_VLAN};
    struct ecm_db_iface_instance ppp_ii = {.index = 3, .type = ECM_DB_IFACE_TYPE_PPPOE, .ae = 150};
    struct ecm_db_iface_instance *ifaces[8] = {0};
    struct ecm_db_connection_instance ci = {.routed = true};
    struct ecm_nss_qinq_side side;

    memset(devices, 0, sizeof(devices));
    port.dp = &dp; port.vlans[35] = true;
    devices[1] = &port; devices[2] = &upper; devices[3] = &ppp;
    clear_nodes();
    node(0, 1, 3072, 0, 100, -1);
    node(1, 1, 3072, 3072, 101, 100);
    node(2, 1, 3072, 35, 102, 100);
    ifaces[6] = &port_ii; ifaces[7] = &ppp_ii;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) == 1);
    assert(side.physical == 1 && side.leaf == 101 &&
           side.outer == 0x88a80c00U && side.inner == 0x81000c00U);
    ifaces[5] = &port_ii; ifaces[6] = &upper_ii;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 5, &side) == 1);
    assert(side.leaf == 102 && side.inner == 0x81000023U);

    dsa_nss_reconfiguring = true;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 5, &side) == -EAGAIN);
    dsa_nss_reconfiguring = false;
    port.vlans[35] = false;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 5, &side) < 0);
    port.vlans[35] = true;
    ppp_ii.ae = 149; /* old registration must not name a new session */
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 5, &side) < 0);
    ppp_ii.ae = 150; ppp.phys = -1;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 5, &side) < 0);
    ppp.phys = 150; ppp.flags = 0;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 5, &side) < 0);
    ppp.flags = IFF_POINTOPOINT; ppp.type = 1;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 5, &side) < 0);
    ppp.type = ARPHRD_PPP; ci.routed = false;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 5, &side) < 0);
    ci.routed = true; ifaces[6] = &ppp_ii;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 5, &side) < 0); /* nested PPP */
    ifaces[6] = &upper_ii; ppp_ii.type = 99;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 5, &side) < 0); /* tunnel */
    assert(port.refs == 0 && upper.refs == 0 && ppp.refs == 0);

    /* QCA admits a native or one-customer-VLAN standalone PPPoE underlay. */
    struct net_device bridge = {.index = 9};
    struct ecm_db_iface_instance bridge_ii = {.index = 9, .type = ECM_DB_IFACE_TYPE_BRIDGE};
    devices[9] = &bridge;
    ops.proto = DSA_TAG_PROTO_QCA_QINQ;
    ppp_ii.type = ECM_DB_IFACE_TYPE_PPPOE;
    ppp_ii.ae = 150; ppp.phys = 150;
    ppp.type = ARPHRD_PPP; ppp.flags = IFF_POINTOPOINT;
    dp.bridge = NULL; port.master = NULL;
    ifaces[5] = NULL; ifaces[6] = &port_ii; ifaces[7] = &ppp_ii;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) == 1);
    assert(side.physical == 1 && side.leaf == 101 &&
           side.outer == 0x88a80c00U && side.inner == 0x81000c00U);
    ppp_ii.ae = 149;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0);
    ppp_ii.ae = 150; ppp.phys = -1;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0);
    ppp.phys = 150; ppp.flags = 0;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0);
    ppp.flags = IFF_POINTOPOINT; ppp.type = 1;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0);
    ppp.type = ARPHRD_PPP; ci.routed = false;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0);
    ci.routed = true;
    ifaces[5] = &port_ii; ifaces[6] = &ppp_ii;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 5, &side) < 0); /* nested */
    ifaces[6] = &upper_ii;
    ds.mock_user_port = &dp; ds.max_num_bridges = 7;
    mock_publish_upper(&port, &upper);
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 5, &side) == 1);
    assert(side.physical == 1 && side.leaf == 102 &&
           side.outer == 0x88a80c00U && side.inner == 0x81000023U);
    assert(port.refs == 0 && upper.refs == 0 && ppp.refs == 0);
    upper.vid = 4094; port.vlans[4094] = true; dsa_nss_nodes[2].vid = 4094;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 5, &side) == 1 &&
           side.leaf == 102 && side.inner == 0x81000ffeU);
    upper.vid = 35; dsa_nss_nodes[2].vid = 35;
    mock_unpublish_upper(&port, &upper);
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 5, &side) < 0); /* stale firmware leaf */
    mock_publish_upper(&port, &upper);
    port.features = 0;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 5, &side) < 0); /* customer filter disabled */
    port.features = 1; dsa_nss_nodes[2].ready = false;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 5, &side) < 0); /* no ready leaf */
    dsa_nss_nodes[2].ready = true; dsa_nss_nodes[2].phys_if = 2;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 5, &side) < 0); /* other conduit */
    dsa_nss_nodes[2].phys_if = 1; upper.proto = 0x88a8;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 5, &side) < 0); /* customer S-tag */
    upper.proto = 0x8100; upper.vid = 3072; port.vlans[3072] = true;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 5, &side) < 0); /* private transport VID */
    upper.vid = 35; upper.master = &bridge;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 5, &side) < 0); /* mastered upper */
    upper.master = NULL; ppp_ii.ae = 149;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 5, &side) < 0); /* stale PPP session */
    ppp_ii.ae = 150; ppp.phys = -1;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 5, &side) < 0); /* unregistered PPP */
    ppp.phys = 150; dsa_nss_reconfiguring = true;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 5, &side) == -EAGAIN);
    dsa_nss_reconfiguring = false;
    struct net_device stacked = {.index = 4, .is_vlan = true,
                                 .real = &upper, .vid = 36, .proto = 0x8100};
    struct ecm_db_iface_instance stacked_ii = {.index = 4, .type = ECM_DB_IFACE_TYPE_VLAN};
    devices[4] = &stacked;
    ifaces[4] = &port_ii; ifaces[5] = &upper_ii; ifaces[6] = &stacked_ii;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 4, &side) < 0); /* stacked customer VLANs */
    assert(port.refs == 0 && upper.refs == 0 && stacked.refs == 0 && ppp.refs == 0);
    ifaces[4] = NULL; ifaces[5] = &port_ii;
    ifaces[6] = &bridge_ii;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 5, &side) < 0); /* stale/unrelated bridge */
    ifaces[5] = NULL; ifaces[6] = &port_ii;
    dp.bridge = &bridge; port.master = &bridge;
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0); /* bridged PPP */
    dp.bridge = NULL; /* A bond/non-DSA master cannot be a PPP underlay. */
    assert(ecm_nss_qinq_side_get(&ci, ifaces, 6, &side) < 0);
    port.master = NULL;
    assert(port.refs == 0 && upper.refs == 0 && ppp.refs == 0 && bridge.refs == 0);
    puts("PASS: QCA native/customer-VLAN PPPoE selects exact live leaves and rejects stale VLAN/session, wrong-conduit, S-tag, private-VID, bridge/master and nested paths");
    puts("PASS: one live PPPoE session over native/customer VLAN; stale, unregistered, malformed, nested and tunnel hierarchies rejected");
}
'''
ecm_checks = ecm_checks.replace('int main(void) {', pppoe_checks + '\nint main(void) {')

bridge_checks = r'''
static void test_bridge_pair(void) {
    struct net_device bridge = {.index=12, .is_bridge=true};
    struct net_device other_bridge = {.is_bridge=true};
    struct net_device conduit = {.phys=1};
    struct net_device wifi = {.index=10, .master=&bridge, .running=true, .carrier=true, .stp_state=3};
    struct net_device port = {.index=11, .is_dsa=true, .master=&bridge, .running=true, .carrier=true, .stp_state=3};
    struct tag_ops ops = {.proto=DSA_TAG_PROTO_QCA_QINQ};
    struct dsa_tree tree = {.tag_ops=&ops};
    struct dsa_switch ds = {.dst=&tree, .tag_8021q_ctx=&tree};
    struct dsa_cpu cpu = {.conduit=&conduit};
    struct dsa_port dp = {.ds=&ds, .cpu_dp=&cpu, .bridge=&bridge, .bridge_num=1, .port=1};
    struct ecm_db_iface_instance wi = {.index=10, .type=ECM_DB_IFACE_TYPE_ETHERNET};
    struct ecm_db_iface_instance pi = {.index=11, .type=ECM_DB_IFACE_TYPE_ETHERNET};
    struct ecm_db_iface_instance bi = {.index=12, .type=ECM_DB_IFACE_TYPE_BRIDGE};
    struct ecm_db_iface_instance *from[8] = {[6]=&wi, [7]=&bi};
    struct ecm_db_iface_instance *to[8] = {[6]=&pi, [7]=&bi};
    struct ecm_db_connection_instance ci = {.mac={{2,0,0,0,0,1},{2,0,0,0,0,2}}};
    struct ecm_nss_qinq_side side;
    wifi.ieee80211_ptr=&wifi; port.dp=&dp; dp.user=&port;
    devices[10]=&wifi; devices[11]=&port; devices[12]=&bridge;
    mock_fdb_from=&wifi; mock_fdb_to=&port;
    clear_nodes(); node(0,1,3073,0,100,-1); node(1,1,3073,3089,101,100);
    for (int proto=0; proto<2; proto++) {
        ops.proto=proto ? DSA_TAG_PROTO_YT921X_QINQ : DSA_TAG_PROTO_QCA_QINQ;
        assert(ecm_nss_qinq_side_get(&ci,to,6,&side)==1 && side.leaf==101);
        assert(ecm_nss_qinq_bridge_validate(&ci,from,6,to,6,false)==0);
    }
    assert(ecm_nss_qinq_bridge_validate(&ci,from,6,to,6,true)==-EOPNOTSUPP);
    mock_fdb_from=&port; mock_fdb_to=&wifi;
    assert(ecm_nss_qinq_bridge_validate(&ci,to,6,from,6,false)==0);
    mock_fdb_from=&wifi; mock_fdb_to=NULL;
    assert(ecm_nss_qinq_bridge_validate(&ci,from,6,to,6,false)==-EAGAIN);
    mock_fdb_to=&wifi;
    assert(ecm_nss_qinq_bridge_validate(&ci,from,6,to,6,false)==-EAGAIN);
    mock_fdb_to=&port; port.stp_state=2;
    assert(ecm_nss_qinq_bridge_validate(&ci,from,6,to,6,false)==-EAGAIN);
    port.stp_state=3; wifi.stp_state=2;
    assert(ecm_nss_qinq_bridge_validate(&ci,from,6,to,6,false)==-EAGAIN);
    wifi.stp_state=3; port.carrier=false;
    assert(ecm_nss_qinq_bridge_validate(&ci,from,6,to,6,false)==-EAGAIN);
    port.carrier=true; wifi.running=false;
    assert(ecm_nss_qinq_bridge_validate(&ci,from,6,to,6,false)==-EAGAIN);
    wifi.running=true;
    for (unsigned long flag=1; flag<=BR_VLAN_TUNNEL; flag<<=1) {
        port.bridge_flags=flag;
        assert(ecm_nss_qinq_bridge_validate(&ci,from,6,to,6,false)==-EOPNOTSUPP);
    }
    port.bridge_flags=0; wifi.bridge_flags=BR_ISOLATED;
    assert(ecm_nss_qinq_bridge_validate(&ci,from,6,to,6,false)==-EOPNOTSUPP);
    wifi.bridge_flags=0; bridge.vlan_enabled=true;
    assert(ecm_nss_qinq_side_get(&ci,to,6,&side)==-EOPNOTSUPP);
    assert(ecm_nss_qinq_bridge_validate(&ci,from,6,to,6,false)==-EOPNOTSUPP);
    bridge.vlan_enabled=false; wifi.master=NULL;
    assert(ecm_nss_qinq_bridge_validate(&ci,from,6,to,6,false)==-EOPNOTSUPP);
    wifi.master=&other_bridge;
    assert(ecm_nss_qinq_bridge_validate(&ci,from,6,to,6,false)==-EOPNOTSUPP);
    wifi.master=&bridge; wifi.ieee80211_ptr=NULL;
    assert(ecm_nss_qinq_bridge_validate(&ci,from,6,to,6,false)==-EOPNOTSUPP);
    wifi.ieee80211_ptr=&wifi; wi.type=ECM_DB_IFACE_TYPE_VLAN;
    assert(ecm_nss_qinq_bridge_validate(&ci,from,6,to,6,false)==-EOPNOTSUPP);
    wi.type=ECM_DB_IFACE_TYPE_ETHERNET;
    assert(ecm_nss_qinq_bridge_validate(&ci,to,6,to,6,false)==-EOPNOTSUPP);
    ci.mac[0][0]=1;
    assert(ecm_nss_qinq_bridge_validate(&ci,from,6,to,6,false)==-EAGAIN);
    ci.routed=true;
    assert(ecm_nss_qinq_bridge_validate(&ci,from,6,to,6,false)==0);
    assert(!wifi.refs && !port.refs && !bridge.refs && !other_bridge.refs);
    puts("PASS: known-unicast Wi-Fi/QinQ bridge pair, both directions; FDB moves/misses, blocked/down ports, restricted flags, VLAN/tunnel/other-bridge/hairpin paths decline without leaking references");
}
'''
ecm_checks = ecm_checks.replace('int main(void) {', bridge_checks + '\nint main(void) {\n    test_bridge_pair();')

header_functions = '\n\n'.join(function(name, header) for name in (
    'dsa_yt921x_qinq_customer_vid_valid', 'dsa_yt921x_qinq_bridge_upper_valid')) + '\n\n' + '\n\n'.join(function(name, qca_header) for name in (
    'dsa_qca_qinq_customer_vid_valid', 'dsa_qca_qinq_direct_vid_valid',
    'dsa_qca_qinq_bridge_vid_valid'))
ecm_functions = header_functions + '\n\n' + '\n\n'.join(function(name, ecm) for name in (
    'ecm_nss_qinq_is_port', 'ecm_nss_qinq_is_upper',
    'ecm_nss_qinq_bridge_port_get', 'ecm_nss_qinq_bridge_validate',
    'ecm_nss_qinq_side_get'))
with tempfile.TemporaryDirectory(prefix='qosmio-ecm-') as directory:
    path = Path(directory)
    (path / 'test.c').write_text(mock + functions + ecm_mock + ecm_functions + ecm_checks)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-Wno-unused-function',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)


# Actual desired-node collector: preserve root/base ownership alongside direct uppers.
collection_support = r'''
#define VLAN_N_VID 4096
#define pr_warn_once(...) ((void)0)
#define br_vlan_get_info br_vlan_get_info_rcu
#define for_each_netdev(net, dev) for (int mock_dev = 0; mock_dev < 32; mock_dev++) if (((dev) = devices[mock_dev]))
'''
collection_checks = r'''
static struct net_device *owner(struct dsa_nss_want *want, int n, int vid) {
    for (int i = 0; i < n; i++) if (want[i].vid == vid) return want[i].ndev;
    return NULL;
}
int main(void) {
    struct net_device conduit = {.phys = 0};
    struct net_device base = {.is_dsa = true, .features = 1};
    struct net_device upper35 = {.is_vlan = true, .real = &base, .vid = 35, .proto = 0x8100};
    struct net_device upper36 = {.is_vlan = true, .real = &base, .vid = 36, .proto = 0x8100};
    struct net_device bridge = {0};
    struct tag_ops ops = {.proto = DSA_TAG_PROTO_YT921X_QINQ};
    struct dsa_tree tree = {.tag_ops = &ops};
    struct dsa_switch ds = {.dst = &tree, .tag_8021q_ctx = &tree};
    struct dsa_cpu cpu = {.conduit = &conduit};
    struct dsa_port dp = {.ds = &ds, .cpu_dp = &cpu, .user = &base, .port = 0, .bridge_num = 1};
    struct dsa_nss_want want[64]; int n;
    ds.max_num_bridges = 7; ds.mock_user_port = &dp;
    base.dp = &dp; base.vlans[35] = base.vlans[36] = true;
    devices[0] = &upper35; devices[1] = &base; /* upper first must not own root */
    n = dsa_nss_collect_port_id(want, 64);
    assert(n == 3 && owner(want,n,0) == &base && owner(want,n,3072) == &base && owner(want,n,35) == &upper35);
    devices[2] = &upper36;
    n = dsa_nss_collect_port_id(want, 64);
    assert(n == 4 && owner(want,n,36) == &upper36 && owner(want,n,3072) == &base);
    base.features = 0;
    assert(dsa_nss_collect_port_id(want,64) == 2);
    base.features = 1; base.vlans[35] = false;
    n = dsa_nss_collect_port_id(want, 64);
    assert(n == 3 && !owner(want,n,35) && owner(want,n,36) == &upper36);
    base.vlans[35] = true; upper35.master = &bridge;
    n = dsa_nss_collect_port_id(want, 64);
    assert(n == 3 && !owner(want,n,35));
    upper35.master = NULL; upper35.proto = 0x88a8;
    n = dsa_nss_collect_port_id(want, 64);
    assert(n == 3 && !owner(want,n,35));
    upper35.proto = 0x8100;
    n = dsa_nss_collect_port_id(want, 2);
    assert(n == 2);
    dp.bridge = &bridge;
    n = dsa_nss_collect_port_id(want, 64);
    assert(n == 2 && owner(want,n,3089) == &base && !owner(want,n,35));
    bridge.vlan_enabled = true; base.vlans[35] = base.vlans[36] = false; base.vlans[100] = true;
    n = dsa_nss_collect_port_id(want, 64);
    assert(n == 2 && owner(want,n,100) == &base);
    ops.proto = DSA_TAG_PROTO_QCA_QINQ;
    dp.bridge = NULL;
    n = dsa_nss_collect_port_id(want, 64);
    assert(n == 2 && owner(want,n,3072) == &base && !owner(want,n,35));
    mock_publish_upper(&base, &upper35);
    n = dsa_nss_collect_port_id(want, 64);
    assert(n == 3 && owner(want,n,35) == &upper35 && !owner(want,n,36));
    mock_unpublish_upper(&base, &upper35);
    assert(dsa_nss_collect_port_id(want,64) == 2);
    mock_publish_upper(&base, &upper35);
    dp.bridge = &bridge; base.master = &bridge; bridge.vlan_enabled = false;
    n = dsa_nss_collect_port_id(want, 64);
    assert(n == 2 && owner(want,n,3089) == &base && !owner(want,n,36));
    bridge.vlan_enabled = true;
    n = dsa_nss_collect_port_id(want, 64);
    assert(n == 2 && owner(want,n,100) == &base && owner(want,n,0) == &base);
    assert(!owner(want,n,35) && !owner(want,n,3072));
    dp.bridge = NULL; base.master = &bridge;
    assert(dsa_nss_collect_port_id(want, 64) == 0);
    base.master = NULL; conduit.phys = -1; dsa_nss_waiting_phys = false;
    assert(!dsa_nss_collect_port_id(want, 64) && dsa_nss_waiting_phys);
    puts("PASS: actual node collector keeps base leaf, exact direct VID/owner, rejects stale/S-tagged/bridged uppers and bounds capacity");
}
'''
collection_functions = header_functions + '\n\n' + '\n\n'.join(function(name) for name in (
    'dsa_nss_port_is_qca', 'dsa_nss_port_is_8021q', 'dsa_nss_port_of', 'dsa_nss_want_add',
    'dsa_nss_collect_bridge_uppers', 'dsa_nss_collect_port_id'))
with tempfile.TemporaryDirectory(prefix='qosmio-collect-') as directory:
    path = Path(directory)
    (path / 'test.c').write_text(mock + ecm_mock + collection_support + collection_functions + collection_checks)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-Wno-unused-function', '-Wno-unused-variable',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)


# Actual bridge-upper collection: per-port SVID/phys leaves owned by the upper.
bridge_collection_checks = r'''
static struct net_device *owner_key(struct dsa_nss_want *want, int n, u16 svid, u16 vid) {
    for (int i = 0; i < n; i++)
        if (want[i].svid == svid && want[i].vid == vid)
            return want[i].ndev;
    return NULL;
}
int main(void) {
    struct net_device conduit0 = {.phys = 0};
    struct net_device conduit1 = {.phys = 3};
    struct net_device bridge_a = {.is_bridge = true};
    struct net_device bridge_b = {.is_bridge = true};
    struct net_device port0 = {.is_dsa = true, .features = 1};
    struct net_device port1 = {.is_dsa = true, .features = 1};
    struct net_device port2 = {.is_dsa = true, .features = 1};
    struct net_device a35 = {.is_vlan = true, .real = &bridge_a, .vid = 35, .proto = 0x8100};
    struct net_device a100 = {.is_vlan = true, .real = &bridge_a, .vid = 100, .proto = 0x8100};
    struct net_device a4094 = {.is_vlan = true, .real = &bridge_a, .vid = 4094, .proto = 0x8100};
    struct net_device b35 = {.is_vlan = true, .real = &bridge_b, .vid = 35, .proto = 0x8100};
    struct net_device stale37 = {.is_vlan = true, .real = &bridge_a, .vid = 37, .proto = 0x8100};
    struct net_device mastered38 = {.is_vlan = true, .real = &bridge_a, .vid = 38, .proto = 0x8100, .master = &bridge_b};
    struct net_device stag39 = {.is_vlan = true, .real = &bridge_a, .vid = 39, .proto = 0x88a8};
    struct net_device stacked40 = {.is_vlan = true, .real = &a35, .vid = 40, .proto = 0x8100};
    struct net_device reserved3091 = {.is_vlan = true, .real = &bridge_a, .vid = 3091, .proto = 0x8100};
    struct tag_ops ops = {.proto = DSA_TAG_PROTO_YT921X_QINQ};
    struct dsa_tree tree = {.tag_ops = &ops};
    struct dsa_switch ds = {.dst = &tree, .tag_8021q_ctx = &tree, .max_num_bridges = 7};
    struct dsa_cpu cpu0 = {.conduit = &conduit0};
    struct dsa_cpu cpu1 = {.conduit = &conduit1};
    struct dsa_port dp0 = {.ds = &ds, .cpu_dp = &cpu0, .user = &port0, .port = 0, .bridge = &bridge_a, .bridge_num = 1};
    struct dsa_port dp1 = {.ds = &ds, .cpu_dp = &cpu0, .user = &port1, .port = 1, .bridge = &bridge_a, .bridge_num = 1};
    struct dsa_port dp2 = {.ds = &ds, .cpu_dp = &cpu1, .user = &port2, .port = 2, .bridge = &bridge_b, .bridge_num = 2};
    struct dsa_nss_want want[64];
    int n;

    port0.dp = &dp0; port1.dp = &dp1; port2.dp = &dp2;
    ds.mock_user_port = &dp0;
    devices[0] = &port0; devices[1] = &port1; devices[2] = &port2;
    devices[3] = &a35; devices[4] = &a100; devices[5] = &a4094;
    devices[6] = &b35; devices[7] = &stale37; devices[8] = &mastered38;
    devices[9] = &stag39; devices[10] = &stacked40; devices[11] = &reserved3091;
    mock_publish_upper(&bridge_a, &a35);
    mock_publish_upper(&bridge_a, &a100);
    mock_publish_upper(&bridge_a, &a4094);
    mock_publish_upper(&bridge_a, &mastered38);
    mock_publish_upper(&bridge_a, &stag39);
    mock_publish_upper(&bridge_a, &reserved3091);
    mock_publish_upper(&bridge_b, &b35);

    n = dsa_nss_collect_port_id(want, 64);
    assert(n == 13);
    assert(owner_key(want, n, 3072, 0) == &port0);
    assert(owner_key(want, n, 3072, 3089) == &port0);
    assert(owner_key(want, n, 3072, 35) == &a35);
    assert(owner_key(want, n, 3072, 100) == &a100);
    assert(owner_key(want, n, 3072, 4094) == &a4094);
    assert(owner_key(want, n, 3073, 0) == &port1);
    assert(owner_key(want, n, 3073, 35) == &a35);
    assert(owner_key(want, n, 3074, 0) == &port2);
    assert(owner_key(want, n, 3074, 3090) == &port2);
    assert(owner_key(want, n, 3074, 35) == &b35);
    assert(!owner_key(want, n, 3072, 37));
    assert(!owner_key(want, n, 3072, 38));
    assert(!owner_key(want, n, 3072, 39));
    assert(!owner_key(want, n, 3072, 40));
    assert(!owner_key(want, n, 3072, 3091));
    assert(!owner_key(want, n, 3074, 100));

    mock_unpublish_upper(&bridge_a, &a35);
    n = dsa_nss_collect_port_id(want, 64);
    assert(n == 11);
    assert(!owner_key(want, n, 3072, 35) && !owner_key(want, n, 3073, 35));
    assert(owner_key(want, n, 3074, 35) == &b35);
    mock_publish_upper(&bridge_a, &a35);

    assert(dsa_nss_collect_port_id(want, 3) == 3);
    assert(dsa_nss_collect_port_id(want, 1) == 1);
    puts("PASS: actual collector publishes per-port customer leaves owned by the live upper, keeps bridges/ports distinct and bounds capacity");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='qosmio-bridge-collect-') as directory:
    path = Path(directory)
    (path / 'test.c').write_text(mock + ecm_mock + collection_support + collection_functions + bridge_collection_checks)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-Wno-unused-function', '-Wno-unused-variable',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)


# Actual notifier classification: unregister releases held refs synchronously.
notifier_support = r'''
struct notifier_block { int unused; };
#define NOTIFY_DONE 0
#define NETDEV_UNREGISTER 1
#define NETDEV_REGISTER 2
#define NETDEV_UP 3
#define NETDEV_DOWN 4
#define NETDEV_CHANGE 5
#define NETDEV_CHANGEUPPER 6
#define NETDEV_CHANGEADDR 7
#define NETDEV_CHANGEMTU 8
#define SWITCHDEV_PORT_OBJ_ADD 101
#define SWITCHDEV_PORT_OBJ_DEL 102
#define SWITCHDEV_PORT_ATTR_SET 103
#define SWITCHDEV_OBJ_ID_PORT_VLAN 1
#define SWITCHDEV_ATTR_ID_BRIDGE_VLAN_FILTERING 1
#define SWITCHDEV_ATTR_ID_PORT_STP_STATE 2
#define SWITCHDEV_ATTR_ID_PORT_PRE_BRIDGE_FLAGS 3
#define SWITCHDEV_ATTR_ID_PORT_BRIDGE_FLAGS 4
#define fallthrough __attribute__((fallthrough))
struct netdev_notifier_changeupper_info { struct net_device *dev; struct net_device *upper_dev; };
struct switchdev_attr { int id; };
struct switchdev_obj { int id; };
struct switchdev_notifier_port_attr_info { struct net_device *dev; struct switchdev_attr *attr; };
struct switchdev_notifier_port_obj_info { struct net_device *dev; struct switchdev_obj *obj; };
static bool is_vlan_dev(struct net_device *dev) { return dev->is_vlan; }
static struct net_device *vlan_dev_real_dev(struct net_device *dev) { return dev->real; }
static int mock_order, mock_destroy_count, mock_schedule_count;
static int mock_destroy_order = -1, mock_schedule_order = -1;
static struct net_device *mock_destroy_dev;
static struct dsa_port *dsa_nss_port_of(struct net_device *dev, bool *upper) {
    *upper = false;
    if (is_vlan_dev(dev)) {
        dev = vlan_dev_real_dev(dev);
        *upper = true;
    }
    if (!dev->is_dsa)
        return NULL;
    return (struct dsa_port *)&mock_order;
}
static void dsa_nss_schedule(void) {
    mock_schedule_order = ++mock_order;
    ++mock_schedule_count;
}
static void dsa_nss_destroy_nodes(struct net_device *dev, const char *why) {
    mock_destroy_order = ++mock_order;
    ++mock_destroy_count;
    mock_destroy_dev = dev;
}
static bool mock_is_bridge_master(struct net_device *dev) { return dev->is_bridge; }
#define netif_is_bridge_master(dev) mock_is_bridge_master(dev)
static bool netif_is_bridge_port(struct net_device *dev) { return dev->master && dev->master->is_bridge; }
static struct net_device *netdev_notifier_info_to_dev(void *ptr) {
    return ((struct netdev_notifier_changeupper_info *)ptr)->dev;
}
#define switchdev_notifier_info_to_dev(ptr) netdev_notifier_info_to_dev(ptr)
static int dsa_nss_netdev_event(struct notifier_block *, unsigned long, void *);
static int notify(struct notifier_block *nb, unsigned long event, struct net_device *dev) {
    struct netdev_notifier_changeupper_info info = {.dev=dev, .upper_dev=dev->master ? dev->master : dev};
    return dsa_nss_netdev_event(nb,event,&info);
}
static void reset_notifier(void) {
    mock_order = mock_destroy_count = mock_schedule_count = mock_defunct_count = 0;
    mock_destroy_order = mock_schedule_order = -1;
    mock_destroy_dev = NULL;
}
'''

notifier_checks = r'''
int main(void) {
    struct notifier_block nb = {0};
    struct net_device bridge = {.is_bridge = true};
    struct net_device upper = {.is_vlan = true, .real = &bridge};
    struct net_device port = {.is_dsa = true};
    struct net_device direct = {.is_vlan = true, .real = &port};
    struct net_device stacked = {.is_vlan = true, .real = &upper};
    struct net_device other = {0};

    /* The VLAN core has unpublished @upper; structural release must run now. */
    reset_notifier();
    notify(&nb, NETDEV_UNREGISTER, &upper);
    assert(mock_destroy_count == 1 && mock_destroy_dev == &upper);
    assert(mock_defunct_count == 1 && mock_schedule_count == 1);
    assert(mock_destroy_order == 1 && mock_schedule_order == 2);

    reset_notifier();
    notify(&nb, NETDEV_CHANGEADDR, &upper);
    assert(mock_schedule_count == 1 && mock_destroy_count == 0);

    reset_notifier();
    notify(&nb, NETDEV_CHANGEUPPER, &bridge);
    assert(mock_schedule_count == 1 && mock_destroy_count == 0);

    reset_notifier();
    notify(&nb, NETDEV_UNREGISTER, &direct);
    assert(mock_destroy_count == 1 && mock_destroy_dev == &direct);

    reset_notifier();
    notify(&nb, NETDEV_REGISTER, &stacked);
    assert(mock_schedule_count == 0 && mock_destroy_count == 0);

    reset_notifier();
    notify(&nb, NETDEV_REGISTER, &other);
    assert(mock_schedule_count == 0 && mock_destroy_count == 0);
    struct net_device wifi = {.master=&bridge};
    reset_notifier();
    notify(&nb, NETDEV_DOWN, &wifi);
    assert(mock_schedule_count == 1 && mock_defunct_count == 1);
    wifi.master=NULL;
    struct netdev_notifier_changeupper_info unlink = {.dev=&wifi, .upper_dev=&bridge};
    reset_notifier();
    dsa_nss_netdev_event(&nb, NETDEV_CHANGEUPPER, &unlink);
    assert(mock_schedule_count == 1 && mock_defunct_count == 1);
    wifi.master=&bridge;
    for (int id=1; id<=4; id++) {
        struct switchdev_attr attr = {.id=id};
        struct switchdev_notifier_port_attr_info info = {.dev=&wifi,.attr=&attr};
        reset_notifier();
        dsa_nss_switchdev_event(&nb,SWITCHDEV_PORT_ATTR_SET,&info);
        assert(mock_schedule_count == 1 && mock_defunct_count == 1);
    }
    puts("PASS: Wi-Fi bridge-port down/unlink and VLAN/state/policy attribute events block new lookups and retire existing rules");
    puts("PASS: actual notifier releases bridge-upper nodes synchronously on unregister and reacts to bridge/VLAN-upper events");
    return 0;
}
'''
notifier_functions = '\n\n'.join(function(name) for name in (
    'dsa_nss_is_bridge_upper', 'dsa_nss_netdev_event',
    'dsa_nss_switchdev_event'))
with tempfile.TemporaryDirectory(prefix='qosmio-notify-') as directory:
    path = Path(directory)
    (path / 'test.c').write_text(mock + notifier_support + notifier_functions + notifier_checks)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-Wno-unused-function', '-Wno-unused-variable',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)


# Actual IPv4/IPv6 early-lookup retry cleanup from the ported ECM sources.
retry_support = r'''
enum { ECM_FRONT_END_ACCELERATION_MODE_DECEL = 1 };
typedef int ecm_front_end_acceleration_mode_t;
struct ecm_front_end_connection_instance { int mock; };
struct nss_ipv4_msg { int mock; };
struct nss_ipv6_msg { int mock; };
static int mock_deref_calls, mock_kfree_calls, mock_clear_calls, mock_clear_mode;
static int mock_iface_refs[8];
static struct ecm_db_iface_instance **mock_deref_list[2];
static int mock_deref_first[2];
static void *mock_kfree_ptr;
static void ecm_db_connection_interfaces_deref(struct ecm_db_iface_instance **ifaces, int first) {
    assert(mock_deref_calls < 2);
    assert(first >= 0 && first < 8);
    assert(ifaces[first] && ifaces[first]->index >= 0 && ifaces[first]->index < 8);
    assert(mock_iface_refs[ifaces[first]->index] > 0);
    --mock_iface_refs[ifaces[first]->index];
    mock_deref_list[mock_deref_calls] = ifaces;
    mock_deref_first[mock_deref_calls] = first;
    ++mock_deref_calls;
}
static void mock_kfree(void *ptr) { ++mock_kfree_calls; mock_kfree_ptr = ptr; }
#define kfree(p) mock_kfree(p)
static void ecm_nss_ipv4_accel_pending_clear(struct ecm_front_end_connection_instance *feci,
                                             ecm_front_end_acceleration_mode_t mode) {
    ++mock_clear_calls;
    mock_clear_mode = mode;
}
static void ecm_nss_ipv6_accel_pending_clear(struct ecm_front_end_connection_instance *feci,
                                             ecm_front_end_acceleration_mode_t mode) {
    ++mock_clear_calls;
    mock_clear_mode = mode;
}
'''

retry_checks = r'''
static void reset_mocks(void) {
    mock_deref_calls = mock_kfree_calls = mock_clear_calls = 0;
    mock_clear_mode = 0;
    mock_iface_refs[0] = mock_iface_refs[1] = 1;
    mock_deref_list[0] = mock_deref_list[1] = NULL;
    mock_deref_first[0] = mock_deref_first[1] = -1;
    mock_kfree_ptr = NULL;
}
static void test_ipv4(int from_status, int to_status, int from_id, int to_id, bool want) {
    struct ecm_front_end_connection_instance feci = {0};
    struct nss_ipv4_msg nim = {0};
    struct ecm_db_iface_instance a = {.index = 0}, b = {.index = 1};
    struct ecm_db_iface_instance *from[8] = {0}, *to[8] = {0};
    bool got;

    from[3] = &a; to[5] = &b;
    reset_mocks();
    got = ecm_nss_qinq_ipv4_rule_retry(&feci, &nim, from, 3, to, 5,
                                       from_status, to_status, from_id, to_id);
    assert(got == want);
    if (want) {
        assert(mock_deref_calls == 2);
        assert(mock_deref_list[0] == from && mock_deref_first[0] == 3);
        assert(mock_deref_list[1] == to && mock_deref_first[1] == 5);
        assert(mock_iface_refs[0] == 0 && mock_iface_refs[1] == 0);
        assert(mock_kfree_calls == 1 && mock_kfree_ptr == &nim);
        assert(mock_clear_calls == 1 && mock_clear_mode == ECM_FRONT_END_ACCELERATION_MODE_DECEL);
    } else {
        assert(mock_deref_calls == 0 && mock_kfree_calls == 0 && mock_clear_calls == 0);
        assert(mock_iface_refs[0] == 1 && mock_iface_refs[1] == 1);
    }
}
static void test_ipv6(int from_status, int to_status, int from_id, int to_id, bool want) {
    struct ecm_front_end_connection_instance feci = {0};
    struct nss_ipv6_msg nim = {0};
    struct ecm_db_iface_instance a = {.index = 0}, b = {.index = 1};
    struct ecm_db_iface_instance *from[8] = {0}, *to[8] = {0};
    bool got;

    from[3] = &a; to[5] = &b;
    reset_mocks();
    got = ecm_nss_qinq_ipv6_rule_retry(&feci, &nim, from, 3, to, 5,
                                       from_status, to_status, from_id, to_id);
    assert(got == want);
    if (want) {
        assert(mock_deref_calls == 2);
        assert(mock_deref_list[0] == from && mock_deref_first[0] == 3);
        assert(mock_deref_list[1] == to && mock_deref_first[1] == 5);
        assert(mock_iface_refs[0] == 0 && mock_iface_refs[1] == 0);
        assert(mock_kfree_calls == 1 && mock_kfree_ptr == &nim);
        assert(mock_clear_calls == 1 && mock_clear_mode == ECM_FRONT_END_ACCELERATION_MODE_DECEL);
    } else {
        assert(mock_deref_calls == 0 && mock_kfree_calls == 0 && mock_clear_calls == 0);
        assert(mock_iface_refs[0] == 1 && mock_iface_refs[1] == 1);
    }
}
int main(void) {
    test_ipv4(-EAGAIN, 1, -1, 5, true);
    test_ipv4(1, -EAGAIN, 5, -1, true);
    test_ipv4(-EAGAIN, -EAGAIN, -1, -1, true);
    test_ipv4(-EAGAIN, 0, -1, 5, true);
    test_ipv4(0, -EAGAIN, 5, -1, true);
    test_ipv4(-EAGAIN, 0, -1, -1, false);
    test_ipv4(-EAGAIN, -ENOENT, -1, 5, false);
    test_ipv4(-ENOENT, -EAGAIN, 5, -1, false);
    test_ipv4(0, 0, -1, 5, false);
    test_ipv4(0, 0, 5, -1, false);
    test_ipv4(1, 1, 5, 5, false);
    test_ipv6(-EAGAIN, 1, -1, 5, true);
    test_ipv6(1, -EAGAIN, 5, -1, true);
    test_ipv6(-EAGAIN, -EAGAIN, -1, -1, true);
    test_ipv6(-EAGAIN, 0, -1, 5, true);
    test_ipv6(0, -EAGAIN, 5, -1, true);
    test_ipv6(-EAGAIN, 0, -1, -1, false);
    test_ipv6(-EAGAIN, -ENOENT, -1, 5, false);
    test_ipv6(-ENOENT, -EAGAIN, 5, -1, false);
    test_ipv6(0, 0, -1, 5, false);
    test_ipv6(0, 0, 5, -1, false);
    test_ipv6(1, 1, 5, 5, false);
    puts("PASS: IPv4/IPv6 retry cleanup releases both lists, frees nim, clears pending once and leaves hard/mixed errors permanent");
    return 0;
}
'''

retry_functions = '\n\n'.join((
    function('ecm_nss_qinq_ipv4_rule_retry', ecm_ipv4),
    function('ecm_nss_qinq_ipv6_rule_retry', ecm_ipv6)))
with tempfile.TemporaryDirectory(prefix='qosmio-qinq-retry-') as directory:
    path = Path(directory)
    (path / 'test.c').write_text(mock + ecm_mock + retry_support + retry_functions + retry_checks)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-Wno-unused-function', '-Wno-unused-variable',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
