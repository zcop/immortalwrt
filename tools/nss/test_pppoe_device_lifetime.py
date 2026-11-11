#!/usr/bin/env python3
"""Exercise production PPPoE setup/unwind with a held underlying device."""
import argparse
from pathlib import Path
import subprocess
import tempfile

from test_qinq_node_lifetime import extract_function

MOCK = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int nss_tx_status_t;
#define ARPHRD_PPP 512
#define IFF_POINTOPOINT 16
#define IFF_EBRIDGE 32
#define PX_PROTO_OE 1
#define NOTIFY_DONE 0
#define NSS_TX_SUCCESS 0
#define NSS_DYNAMIC_INTERFACE_TYPE_PPPOE 1
#define NSS_LAG0_INTERFACE_NUM 50
#define NSS_PPPOE_MSG_SESSION_CREATE 1
#define nss_connmgr_pppoe_warn(...) ((void)0)
#define nss_connmgr_pppoe_info(...) ((void)0)
struct net_device { int type, flags, priv_flags, refs, mtu; bool bond; const char *name; };
struct ppp_channel { int ignored; };
struct pppoe_opt { struct net_device *dev; struct { uint16_t sid; unsigned char remote[6]; } pa; };
struct nss_connmgr_pppoe_session_info { unsigned session_id; unsigned char server_mac[6], local_mac[6]; };
struct nss_connmgr_pppoe_session_entry { struct nss_connmgr_pppoe_session_info info; int hash_list; };
struct nss_ctx_instance { int ignored; };
struct nss_pppoe_create_msg { int session_id, base_if_num, mtu; unsigned char server_mac[6], local_mac[6]; };
struct nss_pppoe_msg { union { struct nss_pppoe_create_msg create; } msg; };
static struct net_device underlay, ppp;
static struct ppp_channel channel;
static struct nss_connmgr_pppoe_session_entry entry;
static struct nss_ctx_instance context;
static int fault, releases, freed, deleted, deallocations, unregisters;
static void held(void) { assert(underlay.refs == 1); }
static void dev_put(struct net_device *dev) { assert(dev == &underlay); held(); --dev->refs; }
static bool ppp_is_multilink(struct net_device *dev) { assert(dev == &ppp); return fault == 1; }
static int ppp_hold_channels(struct net_device *dev, struct ppp_channel **out, int n) {
    assert(dev == &ppp && n == 1); *out = &channel; return fault == 2 ? 0 : 1;
}
static int ppp_channel_get_protocol(struct ppp_channel *p) { assert(p == &channel); return fault == 3 ? 9 : PX_PROTO_OE; }
static int pppoe_channel_addressing_get(struct ppp_channel *p, struct pppoe_opt *opt) {
    assert(p == &channel); if (fault == 4) return -1;
    opt->dev = &underlay; ++underlay.refs; return 0;
}
static void ppp_release_channels(struct ppp_channel **p, int n) { assert(*p == &channel && n == 1); ++releases; }
static int nss_cmn_get_interface_number_by_dev(struct net_device *dev) {
    if (dev == &ppp) return -1;
    assert(dev == &underlay); held(); return -1; /* QinQ has no single netdev-wide ID */
}
static int nss_dynamic_interface_alloc_node(int type) { assert(type == 1); held(); return fault == 5 ? -1 : 64; }
static bool nss_is_dynamic_interface(int id) { held(); assert(id == 64); return fault != 6; }
static struct nss_connmgr_pppoe_session_entry *nss_connmgr_add_pppoe_session(struct net_device *dev, struct pppoe_opt *opt) {
    assert(dev == &ppp && opt->dev == &underlay); held(); return fault == 7 ? NULL : &entry;
}
static struct nss_ctx_instance *nss_register_pppoe_session_if(int id, void *cb, struct net_device *dev, uint32_t features, void *data) {
    assert(id == 64 && !cb && dev == &ppp && !features && data == &entry); held(); return fault == 8 ? NULL : &context;
}
static bool netif_is_bond_master(struct net_device *dev) { assert(dev == &underlay); held(); return dev->bond; }
static struct net_device *br_port_dev_get(struct net_device *dev, unsigned char *mac, void *p, int n) {
    (void)mac; assert(dev == &underlay && !p && !n); held(); return NULL;
}
#define ether_addr_copy(dst, src) memcpy(dst, src, 6)
static void nss_pppoe_msg_init(struct nss_pppoe_msg *msg, int id, int type, unsigned size, void *cb, void *data) {
    assert(msg && id == 64 && type == 1 && size == sizeof(struct nss_pppoe_create_msg) && !cb && !data); held();
}
static int nss_pppoe_tx_msg_sync(struct nss_ctx_instance *ctx, struct nss_pppoe_msg *msg) {
    assert(ctx == &context && msg->msg.create.base_if_num == -1 && msg->msg.create.mtu == 1492); held(); return fault == 11 ? -1 : NSS_TX_SUCCESS;
}
static void nss_unregister_pppoe_session_if(int id) { assert(id == 64); held(); ++unregisters; }
static void fake_hash_del(int *hash) { assert(hash == &entry.hash_list && !deleted); held(); ++deleted; }
#define hash_del_rcu(ptr) fake_hash_del(ptr)
#define synchronize_rcu() ((void)0)
static void kfree(void *p) { assert(p == &entry && !freed); held(); ++freed; }
static int nss_dynamic_interface_dealloc_node(int id, int type) { assert(id == 64 && type == 1); held(); ++deallocations; return NSS_TX_SUCCESS; }
'''

CHECKS = r'''
int main(void) {
    for (fault = 0; fault <= 11; fault++) {
        underlay = (struct net_device){ .name = "wan", .bond = fault == 9,
                                       .priv_flags = fault == 10 ? IFF_EBRIDGE : 0 };
        ppp = (struct net_device){ .name = "pppoe-wan", .type = ARPHRD_PPP, .flags = IFF_POINTOPOINT, .mtu = 1492 };
        releases = freed = deleted = deallocations = unregisters = 0;
        assert(nss_connmgr_pppoe_connect(&ppp) == NOTIFY_DONE);
        assert(underlay.refs == 0);
        assert(releases == (fault == 1 || fault == 2 ? 0 : 1));
        assert(deallocations == (fault >= 6 ? 1 : 0));
        assert(freed == (fault >= 8 ? 1 : 0));
        assert(deleted == freed);
        assert(unregisters == (fault >= 9 ? 1 : 0));
    }
    puts("PASS: underlying device held through session setup; all eleven failure paths release it exactly once");
    return 0;
}
'''


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=root / 'build_dir/target-aarch64_cortex-a53_musl/linux-qualcommax_ipq50xx/qca-nss-clients-12.5.2024.09.11~51be82d/pppoe/nss_connmgr_pppoe.c')
    args = parser.parse_args()
    source = args.source.read_text()
    body = '\n'.join(extract_function(source, n) for n in
                     ['nss_connmgr_pppoe_get_session', 'nss_connmgr_pppoe_connect'])
    with tempfile.TemporaryDirectory(prefix='nss-pppoe-lifetime-') as d:
        path = Path(d); (path / 'test.c').write_text(MOCK + body + CHECKS)
        subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', str(path / 'test.c'),
                        '-o', str(path / 'test')], check=True)
        return subprocess.run([str(path / 'test')]).returncode


if __name__ == '__main__':
    raise SystemExit(main())
