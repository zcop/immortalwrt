#!/usr/bin/env python3
"""Check actual QCA QinQ TX/policy functions against independent wire bytes."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def extract(source, name):
    m = re.search(r'static\s+(?:inline\s+)?[^;{]*?\b' + name + r'\([^;{]*\)\s*\{', source)
    assert m, name
    depth, end = 1, m.end()
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[m.start():end]


MOCK = r'''
#include <assert.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
typedef uint8_t u8; typedef uint16_t u16; typedef u16 __be16;
#define ETH_HLEN 14
#define VLAN_HLEN 4
#define VLAN_VID_MASK 4095
#define VLAN_PRIO_SHIFT 13
#define ETH_P_8021Q 0x8100
#define ETH_P_8021AD 0x88a8
#define ETH_P_QINQ1 0x9100
#define ETH_P_QINQ2 0x9200
#define ETH_P_QINQ3 0x9300
#define NETIF_F_HW_VLAN_CTAG_FILTER 1
#define DSA_TAG_PROTO_QCA_QINQ 39
#define DSA_TAG_8021Q_MAX_NUM_BRIDGES 7
#define unlikely(x) (x)
#define rcu_read_lock() ((void)0)
#define rcu_read_unlock() ((void)0)
struct net_device;
struct tag_ops { int proto; };
struct dsa_tree { struct tag_ops *tag_ops; };
struct dsa_port { int index; struct dsa_switch *ds; struct net_device *user, *bridge; };
struct dsa_switch { struct dsa_tree *dst; unsigned max_num_bridges; struct dsa_port *port; };
struct bridge_vlan_info { u16 flags; };
struct net_device { int features; bool aware, published, member; u16 vid, pvid; struct net_device *real, *master, *upper; struct dsa_port *dp; };
struct sk_buff { u8 buffer[512], *data; unsigned len; bool offload_fwd_mark, tagged; __be16 vlan_proto, protocol; u16 tci; };
struct ethhdr { u8 da[6], sa[6]; __be16 h_proto; } __attribute__((packed));
struct vlan_ethhdr { u8 da[6], sa[6]; __be16 h_vlan_proto, h_vlan_TCI, h_vlan_encapsulated_proto; } __attribute__((packed));
#define dsa_switch_for_each_user_port(dp, ds) for ((dp) = (ds)->port; (dp); (dp) = NULL)
static bool eth_type_vlan(__be16 p) { return p == htons(0x8100) || p == htons(0x88a8); }
static struct net_device *dsa_port_bridge_dev_get(const struct dsa_port *dp) { return dp->bridge; }
static bool br_vlan_enabled(struct net_device *d) { return d->aware; }
static int br_vlan_get_info_rcu(struct net_device *d, u16 vid, struct bridge_vlan_info *i) { return d->member && d->vid == vid ? 0 : -1; }
static int br_vlan_get_pvid_rcu(struct net_device *d, u16 *vid) { *vid=d->pvid; return 0; }
static int dsa_port_bridge_num_get(struct dsa_port *dp) { return 1; }
static u16 dsa_tag_8021q_standalone_vid(const struct dsa_port *dp) { return 3072 + dp->index; }
static u16 dsa_tag_8021q_bridge_vid(unsigned n) { return 0xc00 | ((n & 3) << 4) | ((n & 4) << 7); }
static struct net_device *netdev_master_upper_dev_get_rcu(struct net_device *d) { return d->master; }
static struct net_device *__vlan_find_dev_deep_rcu(struct net_device *d, __be16 proto, u16 vid) {
 return proto == htons(0x8100) && d->published && d->upper && d->upper->vid == vid ? d->upper : NULL;
}
static bool is_vlan_dev(struct net_device *d) { return d->real != NULL; }
static struct net_device *vlan_dev_real_dev(struct net_device *d) { return d->real; }
static u16 vlan_dev_vlan_id(struct net_device *d) { return d->vid; }
static __be16 vlan_dev_vlan_proto(struct net_device *d) { return htons(0x8100); }
static struct dsa_port *dsa_user_to_port(struct net_device *d) { return d->dp; }
static bool skb_vlan_tag_present(struct sk_buff *s) { return s->tagged; }
static u16 skb_vlan_tag_get(struct sk_buff *s) { return s->tci; }
static void __vlan_hwaccel_clear_tag(struct sk_buff *s) { s->tagged = false; }
static struct ethhdr *eth_hdr(struct sk_buff *s) { return (void *)s->data; }
static struct vlan_ethhdr *vlan_eth_hdr(struct sk_buff *s) { return (void *)s->data; }
static bool pskb_may_pull(struct sk_buff *s, unsigned n) { return s->len >= n; }
static int skb_get_queue_mapping(struct sk_buff *s) { return 2; }
static int netdev_txq_to_tc(struct net_device *d, int q) { return q; }
static int __vlan_insert_tag(struct sk_buff *s, __be16 proto, u16 tci) {
 u8 *old = s->data; s->data -= 4; s->len += 4;
 memmove(s->data, old, 12); memcpy(s->data+12, &proto, 2);
 tci = htons(tci); memcpy(s->data+14, &tci, 2); return 0;
}
'''

CHECKS = r'''
static struct tag_ops ops = {39};
static struct dsa_tree tree = {&ops};
static struct dsa_switch ds;
static struct dsa_port dp;
static struct net_device port, upper, bridge;
static struct sk_buff skb;
static void setup(u16 vid) {
 memset(&skb,0,sizeof(skb)); memset(&port,0,sizeof(port)); memset(&upper,0,sizeof(upper)); memset(&bridge,0,sizeof(bridge));
 ds=(struct dsa_switch){.dst=&tree,.max_num_bridges=7,.port=&dp};
 dp=(struct dsa_port){.index=4,.ds=&ds,.user=&port};
 port=(struct net_device){.dp=&dp,.features=1,.upper=&upper,.published=true};
 upper=(struct net_device){.real=&port,.vid=vid};
 skb.data=skb.buffer+128; skb.len=34;
 for(int i=0;i<12;i++)skb.data[i]=i;
 skb.data[12]=8; skb.data[13]=0;
 for(int i=14;i<34;i++)skb.data[i]=i;
}
static void emit(const char *name) {
 assert(qca_qinq_xmit(&skb,&port)==&skb);
 assert(skb.protocol==htons(0x88a8) && !skb.tagged);
 printf("%s ",name);for(unsigned i=0;i<skb.len;i++)printf("%02x",skb.data[i]);puts("");
}
int main(void) {
 setup(35); emit("native");
 setup(35); dp.bridge=&bridge; emit("bridge-native");
 for(int mode=0;mode<2;mode++) {
  setup(35); if(mode) {skb.tagged=true;skb.vlan_proto=htons(0x8100);skb.tci=0xb023;}
  else assert(!__vlan_insert_tag(&skb,htons(0x8100),0xb023));
  emit(mode?"metadata35":"inline35");
 }
 setup(4094);skb.tagged=true;skb.vlan_proto=htons(0x8100);skb.tci=0xfffe;emit("metadata4094");
 const unsigned tag_types[]={0x8100,0x88a8,0x9100,0x9200,0x9300};
 for(unsigned i=0;i<sizeof(tag_types)/sizeof(tag_types[0]);i++) {
  unsigned proto=tag_types[i];
  setup(35);assert(!__vlan_insert_tag(&skb,htons(proto),35));skb.tagged=true;skb.vlan_proto=htons(0x8100);skb.tci=35;
  assert(!qca_qinq_xmit(&skb,&port) && skb.tagged);
 }
 for(int n=0;n<6;n++) {
  setup(35);skb.tagged=true;skb.vlan_proto=htons(0x8100);skb.tci=35;
  if(n==0)port.published=false;
  if(n==1)port.features=0;
  if(n==2)dp.bridge=&bridge;
  if(n==3)upper.master=&bridge;
  if(n==4)skb.vlan_proto=htons(0x88a8);
  if(n==5)skb.offload_fwd_mark=true;
  assert(!qca_qinq_xmit(&skb,&port) && skb.tagged && skb.len==34);
 }
 for(int mode=0;mode<3;mode++) {
  setup(35);dp.bridge=&bridge;bridge.aware=true;port.master=&bridge;port.member=true;port.vid=35;port.pvid=35;
  if(mode==1){skb.tagged=true;skb.vlan_proto=htons(0x8100);skb.tci=0xb023;}
  if(mode==2)assert(!__vlan_insert_tag(&skb,htons(0x8100),0xb023));
  emit(mode==0?"aware-access35":mode==1?"aware-metadata35":"aware-inline35");
 }
 for(int n=0;n<4;n++) {
  setup(35);dp.bridge=&bridge;bridge.aware=true;port.master=&bridge;port.member=true;port.vid=35;port.pvid=35;
  if(n==0)port.pvid=0;
  if(n==1)port.member=false;
  if(n==2)port.master=NULL;
  if(n==3){port.pvid=0;skb.tagged=true;skb.vlan_proto=htons(0x8100);skb.tci=36;}
  assert(!qca_qinq_xmit(&skb,&port));
 }
 setup(35);dp.bridge=&bridge;bridge.aware=true;port.master=&bridge;port.member=true;port.vid=35;
 assert(qca_qinq_rx_vlan(&dp,&bridge,35)==0 && qca_qinq_rx_vlan(&dp,&bridge,36)<0);
 port.member=false;assert(qca_qinq_rx_vlan(&dp,&bridge,35)<0);
 setup(35); assert(qca_qinq_rx_vlan(&dp,NULL,3076)==1 && qca_qinq_rx_vlan(&dp,NULL,35)==0);
 assert(qca_qinq_rx_vlan(&dp,NULL,36)<0 && qca_qinq_rx_vlan(&dp,NULL,4095)<0);
 port.published=false;assert(qca_qinq_rx_vlan(&dp,NULL,35)<0);
 setup(35);dp.bridge=&bridge;assert(qca_qinq_rx_vlan(&dp,&bridge,3088)==1 && qca_qinq_rx_vlan(&dp,&bridge,35)<0);
 bridge.aware=true;assert(qca_qinq_rx_vlan(&dp,&bridge,3088)<0);
 setup(35);assert(!dsa_qca_qinq_customer_vid_valid(&ds,0) && !dsa_qca_qinq_customer_vid_valid(&ds,3076));
 assert(dsa_qca_qinq_customer_vid_valid(&ds,4094) && !dsa_qca_qinq_customer_vid_valid(&ds,4095));
 puts("POLICY_OK");return 0;
}
'''

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('source', type=Path, help='kernel tree with the QCA tagger/header')
args = parser.parse_args()
tagger = (args.source / 'net/dsa/tag_qca_qinq.c').read_text()
header = (args.source / 'include/linux/dsa/qca_qinq.h').read_text()
functions = '\n'.join(extract(header, n) for n in (
    'dsa_qca_qinq_customer_vid_valid', 'dsa_qca_qinq_direct_vid_valid',
    'dsa_qca_qinq_bridge_vid_valid', 'dsa_qca_qinq_bridge_pvid'))
functions += '\n' + '\n'.join(extract(tagger, n) for n in (
    'qca_qinq_is_vlan_tpid', 'qca_qinq_internal_vid', 'qca_qinq_rx_vlan',
    'qca_qinq_customer_tag', 'qca_qinq_xmit'))
with tempfile.TemporaryDirectory(prefix='qca-qinq-tagger-') as directory:
    p = Path(directory)
    (p/'check.c').write_text(MOCK + functions + CHECKS)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-fsanitize=undefined',
                    '-fno-sanitize-recover=all', str(p/'check.c'), '-o', str(p/'check')], check=True)
    rows = subprocess.check_output([str(p/'check')], text=True).splitlines()
body = bytes(range(12)) + bytes.fromhex('0800') + bytes(range(14, 34))
for row in rows:
    if row == 'POLICY_OK':
        continue
    name, data = row.split()
    inner = {'native': 3076, 'bridge-native': 3088,
             'inline35': 0xb023, 'metadata35': 0xb023, 'metadata4094': 0xfffe,
             'aware-access35':35, 'aware-metadata35':0xb023, 'aware-inline35':0xb023}[name]
    expected = body[:12] + bytes.fromhex('88a84c048100') + inner.to_bytes(2, 'big') + body[12:]
    assert bytes.fromhex(data) == expected, name
assert 'POLICY_OK' in rows
print('PASS: native/bridge/customer wire oracle, inline/metadata PCP+DEI, stale/unknown/nested/alternate-tag rejection and RX policy')
