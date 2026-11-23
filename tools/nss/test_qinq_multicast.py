#!/usr/bin/env python3
"""Run actual ECM multicast admission C with mocked topology/lifetime APIs.

Covers entire-group rejection, nested bridge/VLAN lower devices and membership
changes. This does not establish firmware or device multicast forwarding.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(os.environ['ECM_MC_SOURCE']) if 'ECM_MC_SOURCE' in os.environ else next(
    (ROOT / 'build_dir').glob('target-*/linux-qualcommax_ipq50xx/qca-nss-ecm-*'))
common = (SOURCE / 'frontends/nss/ecm_nss_common.c').read_text()


def function(text, name):
    match = re.search(r'(?:static\s+)?(?:inline\s+)?(?:bool|int|void)\s+' + name +
                      r'\s*\([^;{}]*\)\s*\{', text)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[match.start():end]


MOCK = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#define ECM_DB_IFACE_HEIRARCHY_MAX 2
#define ECM_DB_MULTICAST_IF_MAX 3
#define ECM_DB_OBJ_DIR_FROM 0
struct net { int unused; } init_net;
struct net_device { int id, qinq, refs; struct net_device *lower[3]; } devs[12];
struct netdev_nested_priv { void *data; };
struct ecm_db_iface_instance { int id; } iface[12];
struct ecm_db_connection_instance {
 struct ecm_db_iface_instance *from[2], *to[3][2];
 int from_first, to_first[3], targets;
};
static int rcu_depth, from_refs, to_refs;
static void rcu_read_lock(void) { rcu_depth++; }
static void rcu_read_unlock(void) { assert(rcu_depth > 0);rcu_depth--; }
static int ecm_db_iface_interface_identifier_get(struct ecm_db_iface_instance *i) { return i->id; }
static struct net_device *dev_get_by_index(struct net *n, int index) {
 if (index < 0 || index >= 12 || !devs[index].id) return NULL;
 devs[index].refs++;return &devs[index];
}
static void dev_put(struct net_device *d) { assert(d->refs > 0);d->refs--; }
static bool ecm_nss_qinq_is_port(struct net_device *d) { return d->qinq == 1 || d->qinq == 2; }
static bool ecm_nss_qinq_is_upper(struct net_device *d) { return d->qinq == 3; }
static bool dsa_qinq_ipv4_mc_switch(struct net_device *d) { return d->qinq == 4; }
static int netdev_walk_all_lower_dev_rcu(struct net_device *d,
 int (*callback)(struct net_device *, struct netdev_nested_priv *), struct netdev_nested_priv *priv) {
 assert(rcu_depth > 0);
 for (int i=0;i<3;i++) if (d->lower[i]) {
  int ret=callback(d->lower[i],priv);if(ret)return ret;
  ret=netdev_walk_all_lower_dev_rcu(d->lower[i],callback,priv);if(ret)return ret;
 }
 return 0;
}
static int ecm_db_connection_interfaces_get_and_ref(struct ecm_db_connection_instance *c,
 struct ecm_db_iface_instance **out, int direction) {
 if(c->from_first == 2)return 2;
 memcpy(out,c->from,sizeof(c->from));from_refs++;return c->from_first;
}
static void ecm_db_connection_interfaces_deref(struct ecm_db_iface_instance **i,int first) {
 assert(from_refs == 1);from_refs--;
}
static int ecm_db_multicast_connection_to_interfaces_get_and_ref_all(
 struct ecm_db_connection_instance *c, struct ecm_db_iface_instance **all, int32_t **first) {
 if(!c->targets)return 0;
 *all=(void *)c->to;*first=c->to_first;to_refs++;return c->targets;
}
static void ecm_db_multicast_connection_to_interfaces_deref_all(
 struct ecm_db_iface_instance *all,int32_t *first) { assert(to_refs == 1);to_refs--; }
static struct ecm_db_iface_instance *ecm_db_multicast_if_heirarchy_get(
 struct ecm_db_iface_instance *all,int index) { return (void *)((char *)all+index*2*sizeof(void *)); }
static void ecm_db_multicast_copy_if_heirarchy(struct ecm_db_iface_instance **out,
 struct ecm_db_iface_instance *in) { memcpy(out,in,2*sizeof(void *)); }
'''
CHECKS = r'''
static void result(struct ecm_db_connection_instance *c,bool expected) {
 assert(ecm_nss_qinq_multicast_admit(c) == expected);
 assert(!rcu_depth && !from_refs && !to_refs);
 for(int i=0;i<12;i++)assert(!devs[i].refs);
}
int main(void) {
 for(int i=1;i<12;i++){devs[i].id=i;iface[i].id=i;}
 devs[3].qinq=1;devs[4].qinq=2;devs[5].qinq=3;
 struct ecm_db_connection_instance c={.from={NULL,&iface[1]},.from_first=1,
  .to={{NULL,&iface[2]}},.to_first={1,2,2},.targets=1};
 result(&c,true); /* Ordinary NSS-capable interfaces are unchanged. */
 c.from[1]=&iface[3];result(&c,false); /* YT921x ingress. */
 c.from[1]=&iface[4];result(&c,false); /* QCA QinQ ingress. */
 c.from[1]=&iface[1];c.to[0][1]=&iface[3];result(&c,false);
 c.to[0][1]=&iface[2];c.to[1][1]=&iface[4];c.to_first[1]=1;c.targets=2;
 result(&c,false); /* One unsupported recipient rejects the entire set. */
 c.targets=1;c.to_first[1]=2;result(&c,true); /* Membership recovers. */
 c.to[0][1]=&iface[5];result(&c,false); /* VLAN upper of QinQ. */
 devs[8].qinq=4;c.to[0][1]=&iface[8];result(&c,false); /* Another QinQ backend. */
 c.to[0][1]=&iface[6];devs[6].lower[0]=&devs[3];result(&c,false);
 c.to[0][1]=&iface[7];devs[7].lower[0]=&devs[6];result(&c,false); /* Nested VLAN/bridge. */
 devs[6].lower[0]=&devs[2];result(&c,true);
 devs[7].id=0;result(&c,false);devs[7].id=7; /* Unregister race. */
 c.from_first=2;result(&c,false);c.from_first=1;
 c.targets=1;c.to_first[0]=2;result(&c,false); /* Stale empty snapshot. */
 c.targets=0;result(&c,false); /* No recipients. */
 puts("PASS: multicast whole-group admission and balanced topology references");
}
'''

for version in (4, 6):
    text = (SOURCE / f'frontends/nss/ecm_nss_multicast_ipv{version}.c').read_text()
    create = function(text, f'ecm_nss_multicast_ipv{version}_connection_accelerate')
    update = function(text, f'ecm_nss_multicast_ipv{version}_connection_update_accelerate')
    assert create.index('ecm_nss_qinq_multicast_admit(') < create.index('accel_pending_set(')
    assert update.index('ecm_nss_qinq_multicast_admit(') < update.index('kzalloc(')
    assert re.search(r'if \(!ecm_nss_qinq_multicast_admit\(feci->ci\)\)\s+return -EOPNOTSUPP;', update)

    # Exercise the actual callback's error branch with its declared ret type.
    # The vendor routed callbacks used unsigned ret, making ret < 0 impossible.
    names = [f'ecm_nss_multicast_ipv{version}_bridge_update_connections',
             'ecm_mfc_update_event_callback' if version == 4 else
             'ecm_nss_multicast_ipv6_mfc_update_event_callback']
    for name in names:
        callback = function(text, name)
        if version == 6 and name.endswith('mfc_update_event_callback'):
            update_case = callback.split('case IP6MR_MFC_EVENT_UPDATE:', 1)[1].split(
                'case IP6MR_MFC_EVENT_DELETE:', 1)[0]
            assert 'return;' not in update_case
            assert re.search(r'find_next_tuple:\s*'
                             r'tuple_instance_next = .*get_and_ref_next\(tuple_instance\);\s*'
                             r'ecm_db_multicast_connection_deref\(tuple_instance\);\s*'
                             r'tuple_instance = tuple_instance_next;',
                             re.sub(r'/\*.*?\*/', '', update_case, flags=re.DOTALL))
        declaration = re.search(r'^\s*(?:u?int(?:32_t)?)\s+[^;\n]*\bret\b[^;\n]*;',
                                callback, re.MULTILINE)
        assert declaration, name
        error_branch = re.search(
            r'ret = ecm_nss_multicast_ipv[46]_connection_update_accelerate\([^;]+;'
            r'\s*if \(ret < 0\) \{[^}]+\}', callback)
        assert error_branch, name
        branch = re.sub(r'ret = [^;]+;', 'ret = result;', error_branch.group())
        case = r'''
#include <stdint.h>
#include <errno.h>
#include <assert.h>
static int decelerated, refs;
struct frontend { void (*decelerate)(struct frontend *); };
static void decelerate(struct frontend *f) { decelerated++; }
static void ecm_front_end_connection_deref(struct frontend *f) { refs--; }
static void check(int result, int expected) {
    @DECL@
    struct frontend instance = { .decelerate = decelerate }, *feci = &instance;
    refs = 1; decelerated = 0;
    @UNUSED@
    @BRANCH@
    ecm_front_end_connection_deref(feci);
find_next_tuple:
    assert(decelerated == expected && refs == 0);
}
int main(void) {
    check(-EOPNOTSUPP, 1); check(-ENOMEM, 1); check(-1, 1); check(0, 0);
}
'''.replace('@DECL@', declaration.group()).replace('@BRANCH@', branch).replace(
            '@UNUSED@', '(void)i;' if re.search(r'\bi\b', declaration.group()) else '')
        with tempfile.TemporaryDirectory(prefix='nss-multicast-update-') as directory:
            tmp = Path(directory)
            (tmp / 'check.c').write_text(case)
            subprocess.run(['cc', '-std=gnu11', '-O2', '-Wall', '-Werror',
                            str(tmp / 'check.c'), '-o', str(tmp / 'check')], check=True)
            subprocess.run([str(tmp / 'check')], check=True)
        print(f'PASS: {name} decelerates the complete rule on update error')

    bridge = function(text, names[0])
    declaration = re.search(r'^\s*(?:u?int(?:32_t)?)\s+dst_if_cnt;', bridge, re.MULTILINE)
    assert declaration
    branch = re.search(r'if \(dst_if_cnt < 0\) \{[^}]+\}', bridge)
    assert branch
    case = r'''
#include <stdint.h>
#include <errno.h>
#include <assert.h>
#define DEBUG_WARN(...) do {} while (0)
static int decelerated, refs;
struct frontend { void (*decelerate)(struct frontend *); };
static void decelerate(struct frontend *f) { decelerated++; }
static void ecm_front_end_connection_deref(struct frontend *f) { refs--; }
static void check(int result, int expected) {
    @DECL@
    struct frontend instance = { .decelerate = decelerate }, *feci = &instance;
    refs = 1; decelerated = 0;
    dst_if_cnt = result;
    @BRANCH@
    ecm_front_end_connection_deref(feci);
find_next_tuple:
    assert(decelerated == expected && refs == 0);
}
int main(void) { check(-ENOENT, 1); check(-EINVAL, 1); check(1, 0); }
'''.replace('@DECL@', declaration.group()).replace('@BRANCH@', branch.group())
    with tempfile.TemporaryDirectory(prefix='nss-multicast-lookup-') as directory:
        tmp = Path(directory)
        (tmp / 'check.c').write_text(case)
        subprocess.run(['cc', '-std=gnu11', '-O2', '-Wall', '-Werror',
                        str(tmp / 'check.c'), '-o', str(tmp / 'check')], check=True)
        subprocess.run([str(tmp / 'check')], check=True)
    print(f'PASS: IPv{version} missing multicast route retires the firmware rule')

source = MOCK + '\n'.join(function(common, name) for name in (
    'ecm_nss_qinq_multicast_lower', 'ecm_nss_qinq_multicast_hierarchy',
    'ecm_nss_qinq_multicast_admit')) + CHECKS
with tempfile.TemporaryDirectory(prefix='nss-multicast-admission-') as directory:
    tmp = Path(directory)
    (tmp / 'check.c').write_text(source)
    subprocess.run(['cc', '-std=gnu11', '-O2', '-Wall', '-Werror',
                    str(tmp / 'check.c'), '-o', str(tmp / 'check')], check=True)
    subprocess.run([str(tmp / 'check')], check=True)

# Exercise the same rule ingress resolver with a separate Ethernet WAN and
# with WAN attached to a QinQ switch. Interface zero is a valid physical ID.
ingress = r'''
#include <arpa/inet.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>

#define IS_ENABLED(option) 1
#define IS_ERR_OR_NULL(ptr) (!(ptr))
#define READ_ONCE(value) (value)
#define ARPHRD_ETHER 1
#define ETH_P_8021AD 0x88a8
#define ETH_P_8021Q 0x8100
#define VLAN_VID_MASK 4095
#define NSS_PHYSICAL_IF_START 0
#define NSS_VIRTUAL_IF_START 8
#define ECM_FRONT_END_VLAN_ID_NOT_CONFIGURED 0xfff

typedef uint32_t u32;
typedef uint16_t u16;

struct dsa_port;
struct net_device {
    int type, nss_if;
    bool running, carrier, qinq, conduit;
    u16 vid, proto;
    struct dsa_port *port;
    struct net_device *master, *lower, *vlan;
};
struct dsa_port {
    struct dsa_port *cpu_dp;
    struct net_device *conduit;
    u16 svid;
};

static bool netif_running(struct net_device *dev) { return dev->running; }
static bool netif_carrier_ok(struct net_device *dev) { return dev->carrier; }
static struct net_device *netdev_master_upper_dev_get_rcu(struct net_device *dev) { return dev->master; }
static bool is_vlan_dev(struct net_device *dev) { return dev->lower != NULL; }
static struct net_device *vlan_dev_real_dev(struct net_device *dev) { return dev->lower; }
static u16 vlan_dev_vlan_id(struct net_device *dev) { return dev->vid; }
static u16 vlan_dev_vlan_proto(struct net_device *dev) { return dev->proto; }
static struct net_device *__vlan_find_dev_deep_rcu(struct net_device *dev, u16 proto, u16 vid) {
    return dev->vlan && dev->vlan->proto == proto && dev->vlan->vid == vid ? dev->vlan : NULL;
}
static bool dsa_user_dev_check(struct net_device *dev) { return dev->port != NULL; }
static bool ecm_nss_qinq_is_port(struct net_device *dev) { return dev->qinq; }
static struct dsa_port *dsa_port_from_netdev(struct net_device *dev) { return dev->port; }
static u16 dsa_tag_8021q_standalone_vid(struct dsa_port *port) { return port->svid; }
static bool netdev_uses_dsa(struct net_device *dev) { return dev->conduit; }
static int nss_cmn_get_interface_number_by_dev(struct net_device *dev) { return dev->nss_if; }
'''
ingress_checks = r'''
int main(void) {
    struct net_device raw = {
        .type = ARPHRD_ETHER, .running = true, .carrier = true, .nss_if = 0,
    };
    struct net_device tagged = raw;
    struct dsa_port cpu = { .conduit = &raw };
    struct dsa_port user = { .cpu_dp = &cpu, .svid = 3076 };
    struct net_device wan = raw;
    u32 tags[2];

    assert(ecm_nss_qinq_mc_ingress(&raw, tags) == 0);
    assert(tags[0] == 0xfff && tags[1] == 0xfff);

    tagged.vid = 35;
    tagged.proto = htons(ETH_P_8021Q);
    tagged.lower = &raw;
    raw.vlan = &tagged;
    assert(ecm_nss_qinq_mc_ingress(&tagged, tags) == 0);
    assert(tags[0] == 0x81000023 && tags[1] == 0xfff);
    raw.vlan = NULL;
    assert(ecm_nss_qinq_mc_ingress(&tagged, tags) < 0);
    raw.vlan = &tagged;
    tagged.proto = htons(ETH_P_8021AD);
    assert(ecm_nss_qinq_mc_ingress(&tagged, tags) < 0);
    tagged.proto = htons(ETH_P_8021Q);
    raw.lower = &wan;
    assert(ecm_nss_qinq_mc_ingress(&tagged, tags) < 0);
    raw.lower = NULL;

    raw.conduit = true;
    assert(ecm_nss_qinq_mc_ingress(&raw, tags) < 0);
    assert(ecm_nss_qinq_mc_ingress(&tagged, tags) < 0);
    wan.port = &user;
    assert(ecm_nss_qinq_mc_ingress(&wan, tags) < 0);
    wan.qinq = true;
    assert(ecm_nss_qinq_mc_ingress(&wan, tags) == 0);
    assert(tags[0] == 0x88a80c04 && tags[1] == 0x81000c04);
    wan.master = &tagged;
    assert(ecm_nss_qinq_mc_ingress(&wan, tags) < 0);
    wan.master = NULL;
    raw.nss_if = NSS_VIRTUAL_IF_START;
    assert(ecm_nss_qinq_mc_ingress(&wan, tags) < 0);
    raw.nss_if = 0;
    raw.carrier = false;
    assert(ecm_nss_qinq_mc_ingress(&wan, tags) < 0);
    puts("PASS: standalone, single-tag and switch WAN ingress; invalid paths rejected");
}
'''
ipv4 = (SOURCE / 'frontends/nss/ecm_nss_multicast_ipv4.c').read_text()
source = ingress + '\n'.join(function(ipv4, name) for name in (
    'ecm_nss_qinq_mc_physical_if', 'ecm_nss_qinq_mc_ingress')) + ingress_checks
with tempfile.TemporaryDirectory(prefix='nss-multicast-ingress-') as directory:
    tmp = Path(directory)
    (tmp / 'check.c').write_text(source)
    subprocess.run(['cc', '-std=gnu11', '-O2', '-Wall', '-Werror',
                    str(tmp / 'check.c'), '-o', str(tmp / 'check')], check=True)
    subprocess.run([str(tmp / 'check')], check=True)

classifier = (SOURCE / 'ecm_classifier.h').read_text()
frontend = (SOURCE / 'frontends/cmn/ecm_multicast_ipv4.c').read_text()
assert re.search(r'if \(is_routed && sender == ECM_TRACKER_SENDER_TYPE_SRC &&\s+'
                 r'feci->accel_engine == ECM_FRONT_END_ENGINE_NSS &&\s+'
                 r'READ_ONCE\(ecm_classifier_accel_delay_pkts\) == 1\)\s+'
                 r'iph->one_way_multicast = ecm_nss_qinq_ipv4_mc_eligible', frontend)
for name in ('default', 'dscp'):
    source = (SOURCE / f'ecm_classifier_{name}.c').read_text()
    assert 'ecm_classifier_packet_accel_delay(ip_hdr->one_way_multicast)' in source

policy_mock = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#define READ_ONCE(value) (value)
static int ecm_classifier_accel_delay_pkts;
'''
policy_checks = r'''
int main(void) {
    for (int configured = 0; configured < 5; configured++) {
        ecm_classifier_accel_delay_pkts = configured;
        assert(ecm_classifier_packet_accel_delay(false) == configured);
        assert(ecm_classifier_packet_accel_delay(true) == (configured == 1 ? 2 : configured));
        assert(ecm_classifier_accel_delay_pkts == configured);
    }
    puts("PASS: qualified one-way multicast gets a finite default delay; other policies are preserved");
}
'''
with tempfile.TemporaryDirectory(prefix='nss-multicast-classifier-') as directory:
    tmp = Path(directory)
    (tmp / 'check.c').write_text(policy_mock +
        function(classifier, 'ecm_classifier_packet_accel_delay') + policy_checks)
    subprocess.run(['cc', '-std=gnu11', '-O2', '-Wall', '-Werror',
                    str(tmp / 'check.c'), '-o', str(tmp / 'check')], check=True)
    subprocess.run([str(tmp / 'check')], check=True)
