#!/usr/bin/env python3
"""Exercise actual transport functions against a fake MDIO/DSA environment.

This validates software state and register intent, not silicon or NSS firmware.
"""
from pathlib import Path
import re
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
DRIVER = ROOT / 'target/linux/generic/files/drivers/net/dsa'
header = (DRIVER / 'yt921x.h').read_text().splitlines(True)
defines = []
continued = False
for line in header:
    if line.startswith('#define ') or continued:
        defines.append(line)
        continued = line.rstrip().endswith('\\')
common = (DRIVER / 'yt921x_l2.c').read_text()
functions = []
for name in ['yt921x_vlan_fid_for_vid', 'yt921x_vlan_aware_set',
             'yt921x_port_set_pvid', 'yt921x_vlan_add', 'yt921x_vlan_del']:
    match = re.search(r'^(?:static )?(?:int|u16)\s+' + name + r'\(.*?^\}', common, re.M | re.S)
    assert match, name
    functions.append(match.group(0))
transport = (DRIVER / 'yt921x_8021q.c').read_text().replace('#include "yt921x_internal.h"', '')
acl = (DRIVER / 'yt921x_qinq_acl.c').read_text().replace('#include "yt921x_internal.h"', '')
preamble = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#define BIT(x) (1U << (x))
#define BIT_ULL(x) (1ULL << (x))
#define GENMASK(h,l) ((~0U << (l)) & (~0U >> (31-(h))))
#define GENMASK_ULL(h,l) ((~0ULL << (l)) & (~0ULL >> (63-(h))))
#define FIELD_PREP(mask,x) (((uint64_t)(x) << __builtin_ctzll(mask)) & (mask))
#define FIELD_GET(mask,x) (((x) & (mask)) >> __builtin_ctzll(mask))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define container_of_const(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define dev_err(...) ((void)0)
#define NL_SET_ERR_MSG_MOD(...) ((void)0)
#define BRIDGE_VLAN_INFO_PVID 2
#define BRIDGE_VLAN_INFO_UNTAGGED 4
struct net_device { int id; };
struct netlink_ext_ack { int unused; };
struct bridge { struct net_device *dev; };
struct dsa_switch;
struct dsa_port { int index; struct dsa_switch *ds; struct bridge *bridge; struct dsa_port *cpu_dp; };
struct dsa_switch { struct dsa_port ports[11]; int num_ports; };
struct switchdev_obj_port_vlan {
 struct { struct net_device *orig_dev; } obj;
 u16 vid, flags;
};
struct yt921x_priv {
 struct dsa_switch ds;
 int reg_lock, primary_cpu_port, secondary_cpu_port;
 u16 cpu_ports_mask, dt_secondary_conduit_user_mask;
 bool dt_secondary_conduit_user_mask_valid, qinq;
 bool bridge_vlan_fault[11], bridge_vlan_filtering[11];
 u16 tag_8021q_pvid[11], bridge_pvid[11];
 u16 bridge_vlan_members[3072], bridge_vlan_untagged[3072];
 u8 vlan_fid_mode;
 u16 vlan_svl_fid;
};
#define YT921X_VLAN_FID_MODE_SVL 1
#define YT921X_ACL_TYPE_VLAN 4
#define dsa_to_port(ds,p) (&(ds)->ports[p])
#define dsa_is_user_port(ds,p) ((p) >= 0 && (p) < 4)
#define dsa_is_cpu_port(ds,p) ((p) == 4 || (p) == 8)
#define dsa_switch_for_each_user_port(dp,s) \
 for ((dp)=&(s)->ports[0]; (dp)<&(s)->ports[4]; (dp)++)
#define mutex_lock(x) ((void)0)
#define mutex_unlock(x) ((void)0)
static uint32_t registers[0x400000/4];
static int writes, fail_write;
static int yt921x_reg_write(struct yt921x_priv *p, u32 reg, u32 val) {
 assert(reg % 4 == 0 && reg < sizeof(registers));
 if (++writes == fail_write) return -EIO;
 registers[reg/4] = val;
 return 0;
}
static int yt921x_reg_read(struct yt921x_priv *p, u32 reg, u32 *val) {
 assert(reg % 4 == 0 && reg < sizeof(registers));
 *val=registers[reg/4]; return 0;
}
static int yt921x_reg_update_bits(struct yt921x_priv *p,u32 r,u32 m,u32 v) {
 u32 old=registers[r/4]; return yt921x_reg_write(p,r,(old & ~m)|(v & m));
}
static int yt921x_reg_set_bits(struct yt921x_priv *p,u32 r,u32 m) {
 return yt921x_reg_update_bits(p,r,m,m);
}
static int yt921x_reg_clear_bits(struct yt921x_priv *p,u32 r,u32 m) {
 return yt921x_reg_update_bits(p,r,m,0);
}
static int yt921x_reg_toggle_bits(struct yt921x_priv *p,u32 r,u32 m,bool v) {
 return yt921x_reg_update_bits(p,r,m,v ? m : 0);
}
static int yt921x_reg64_read(struct yt921x_priv *p,u32 r,u64 *v) {
 *v=registers[r/4] | ((u64)registers[r/4+1]<<32); return 0;
}
static int yt921x_reg64_write(struct yt921x_priv *p,u32 r,u64 v) {
 int e=yt921x_reg_write(p,r,v); return e ? e : yt921x_reg_write(p,r+4,v>>32);
}
static int yt921x_reg64_update_bits(struct yt921x_priv *p,u32 r,u64 m,u64 v) {
 u64 old; yt921x_reg64_read(p,r,&old); return yt921x_reg64_write(p,r,(old & ~m)|(v & m));
}
static u16 dsa_tag_8021q_standalone_vid(struct dsa_port *p) {return 3072+p->index;}
static int yt921x_reg96_write(struct yt921x_priv *p,u32 r,const u32 *v) {
 for(int i=0;i<3;i++) {int e=yt921x_reg_write(p,r+4*i,v[i]);if(e)return e;}return 0;
}
static u32 yt921x_non_cpu_port_mask(struct yt921x_priv *p) { return 0x7ff & ~p->cpu_ports_mask; }
static bool yt921x_is_secondary_cpu_port(struct yt921x_priv *p,int port) { return port==p->secondary_cpu_port; }
static int yt921x_port_isolation_set(struct yt921x_priv *p,int port,u32 mask) { return 0; }
static int yt921x_fdb_flush_port(struct yt921x_priv *p,int port,bool all) { return 0; }
'''
tests = r'''
static struct yt921x_priv priv;
static struct net_device brdev[2];
static struct bridge bridges[2] = {{&brdev[0]}, {&brdev[1]}};
static struct switchdev_obj_port_vlan v = {{&brdev[0]}, 10, BRIDGE_VLAN_INFO_PVID|BRIDGE_VLAN_INFO_UNTAGGED};
static void reset(void) {
 memset(&priv,0,sizeof(priv)); memset(registers,0,sizeof(registers)); writes=0; fail_write=-1;
 priv.cpu_ports_mask=BIT(4)|BIT(8); priv.primary_cpu_port=8; priv.secondary_cpu_port=4;
 priv.dt_secondary_conduit_user_mask=BIT(3); priv.dt_secondary_conduit_user_mask_valid=true;
 for (int i=0;i<11;i++) {priv.ds.ports[i].index=i;priv.ds.ports[i].ds=&priv.ds;priv.ds.ports[i].cpu_dp=&priv.ds.ports[i==3?4:8];}
 priv.ds.num_ports=11;
 priv.ds.ports[0].bridge=&bridges[0];priv.ds.ports[1].bridge=&bridges[0];priv.ds.ports[3].bridge=&bridges[1];
 v.vid=10;v.obj.orig_dev=&brdev[0];
}
static u64 vlan(int vid) {u64 value;yt921x_reg64_read(&priv,YT921X_VLANn_CTRL(vid),&value);return value;}
static u16 pvid(int port) {return FIELD_GET(YT921X_PORT_VLAN_CTRL_CVID_M,registers[YT921X_PORTn_VLAN_CTRL(port)/4]);}
static int tag(int port,int vid) {return yt921x_tag_8021q_vlan_add(&priv.ds,port,vid,BRIDGE_VLAN_INFO_UNTAGGED|BRIDGE_VLAN_INFO_PVID);}
int main(void) {
 reset();
 assert(!yt921x_8021q_port_enter(&priv,8)); assert(!yt921x_8021q_port_enter(&priv,4));
 assert(registers[YT921X_EXT_CPU_PORT/4]==0);assert(registers[YT921X_TPID_EGRn(0)/4]==0x8100);
 u32 a=registers[YT921X_PORTn_ISOLATION(8)/4],b=registers[YT921X_PORTn_ISOLATION(4)/4];
 assert((a & (BIT(4)|BIT(8)|BIT(3)))==(BIT(4)|BIT(8)|BIT(3))); assert(!(a & 7));
 assert((b & (BIT(4)|BIT(8)|7))==(BIT(4)|BIT(8)|7));assert(!(b & BIT(3)));
 puts("PASS: both CPU links retain opposite LAN/WAN isolation");
 reset();assert(!tag(0,3072));assert(!yt921x_tag_8021q_vlan_add(&priv.ds,8,3072,0));
 assert(pvid(0)==3072);assert(vlan(3072)&YT921X_VLAN_CTRL_UNTAG_PORTn(0));assert(!(vlan(3072)&YT921X_VLAN_CTRL_UNTAG_PORTn(8)));
 assert(!tag(0,3089));assert(!yt921x_tag_8021q_vlan_del(&priv.ds,0,3072));assert(pvid(0)==3089);
 puts("PASS: replacing standalone VID preserves bridge PVID and CPU tagging");
 assert(!yt921x_8021q_bridge_vlan(&priv,0,&v,true,NULL));assert(!(vlan(10)&YT921X_VLAN_CTRL_PORTn(0)));
 assert(!yt921x_8021q_filtering(&priv,0,true));assert(pvid(0)==10);
 assert(vlan(10)&YT921X_VLAN_CTRL_PORTn(0));assert(!(vlan(3089)&YT921X_VLAN_CTRL_PORTn(0)));
 assert(!yt921x_8021q_filtering(&priv,0,false));assert(pvid(0)==3089);
 assert(!(vlan(10)&YT921X_VLAN_CTRL_PORTn(0)));assert(vlan(3089)&YT921X_VLAN_CTRL_PORTn(0));
 puts("PASS: inactive bridge VLANs cache safely and filtering round-trip restores transport");
 int before=writes;v.vid=3072;assert(yt921x_8021q_bridge_vlan(&priv,0,&v,true,NULL)==-EBUSY);assert(writes==before);
 v.vid=10;v.obj.orig_dev=NULL;assert(yt921x_8021q_bridge_vlan(&priv,0,&v,true,NULL)==-EOPNOTSUPP);assert(writes==before);v.obj.orig_dev=&brdev[0];
 puts("PASS: reserved VIDs and direct VLAN uppers fail without hardware writes");
 assert(!yt921x_8021q_filtering(&priv,0,true));priv.bridge_vlan_filtering[3]=true;
 before=writes;assert(yt921x_8021q_bridge_vlan(&priv,3,&v,true,NULL)==-EBUSY);assert(writes==before);
 puts("PASS: a customer VID cannot cross independent bridge domains");
 /* Inject an error into each write of a successful VLAN add. */
 reset();priv.bridge_vlan_filtering[0]=true;assert(!yt921x_8021q_bridge_vlan(&priv,0,&v,true,NULL));int count=writes;
 for(int fail=1;fail<=count;fail++) {
  reset();priv.bridge_vlan_filtering[0]=true;registers[YT921X_PORTn_CTRL(0)/4]=YT921X_PORT_RX_MAC_EN|YT921X_PORT_TX_MAC_EN;fail_write=fail;
  assert(yt921x_8021q_bridge_vlan(&priv,0,&v,true,NULL)==-EIO);
  assert(priv.bridge_vlan_fault[0]);assert(priv.bridge_vlan_members[10]==0);
  assert(!(registers[YT921X_PORTn_CTRL(0)/4] & (YT921X_PORT_RX_MAC_EN|YT921X_PORT_TX_MAC_EN)));
  assert(yt921x_8021q_filtering(&priv,0,false)==-EIO);
 }
 printf("PASS: all %d VLAN-add write failure points latch a closed port\n",count);
 reset();priv.bridge_vlan_filtering[0]=true;assert(!yt921x_8021q_bridge_vlan(&priv,0,&v,true,NULL));writes=0;
 assert(!yt921x_8021q_bridge_vlan(&priv,0,&v,false,NULL));count=writes;
 for(int fail=1;fail<=count;fail++) {
  reset();priv.bridge_vlan_filtering[0]=true;assert(!yt921x_8021q_bridge_vlan(&priv,0,&v,true,NULL));
  registers[YT921X_PORTn_CTRL(0)/4]=YT921X_PORT_RX_MAC_EN|YT921X_PORT_TX_MAC_EN;writes=0;fail_write=fail;
  assert(yt921x_8021q_bridge_vlan(&priv,0,&v,false,NULL)==-EIO);assert(priv.bridge_vlan_fault[0]);
  assert(priv.bridge_vlan_members[10] & BIT(0));assert(priv.bridge_pvid[0]==10);
  assert(!(registers[YT921X_PORTn_CTRL(0)/4] & (YT921X_PORT_RX_MAC_EN|YT921X_PORT_TX_MAC_EN)));
 }
 printf("PASS: all %d VLAN-delete write failure points restore cached state and latch closed\n",count);
 reset();assert(!tag(0,3089));assert(!yt921x_8021q_bridge_vlan(&priv,0,&v,true,NULL));
 priv.bridge_vlan_members[10] |= BIT(3);priv.bridge_vlan_filtering[3]=true;int previous=writes;
 assert(yt921x_8021q_filtering(&priv,0,true)==-EBUSY);assert(writes==previous);assert(pvid(0)==3089);
 puts("PASS: enabling a cached conflicting VLAN fails before altering hardware");
 reset();priv.qinq=true;registers[YT921X_ACL_PERMIT_UNMATCH/4]=0x7ff;
 assert(!yt921x_qinq_acl_setup(&priv));int aclwrites=writes;
 assert((registers[YT921X_ACL_PERMIT_UNMATCH/4]&(BIT(4)|BIT(8)))==0);
 for(int i=0;i<4;i++) {
  u32 keyhi=registers[(YT921X_ACLn_KEYm(YT921X_QINQ_ACL_BLOCK,i)+4)/4];
  assert(FIELD_GET(YT921X_ACL_KEYb_SPORTS_M,keyhi)==BIT(i==3?4:8));
  assert(registers[YT921X_ACLn_ACT(8*YT921X_QINQ_ACL_BLOCK+i)/4]==0);
  assert(FIELD_GET(YT921X_ACL_ACTc_REDIR_DPORTS_M,registers[(YT921X_ACLn_ACT(8*YT921X_QINQ_ACL_BLOCK+i)+8)/4])==BIT(i));
 }
 assert(!yt921x_qinq_acl_teardown(&priv));assert(registers[YT921X_ACLn_ENTRY(YT921X_QINQ_ACL_BLOCK)/4]==0);
 assert((registers[YT921X_ACL_PERMIT_UNMATCH/4]&(BIT(4)|BIT(8)))==(BIT(4)|BIT(8)));
 for(int fail=2;fail<=aclwrites;fail++) {
  reset();priv.qinq=true;registers[YT921X_ACL_PERMIT_UNMATCH/4]=0x7ff;fail_write=fail;
  assert(yt921x_qinq_acl_setup(&priv)==-EIO);
  assert((registers[YT921X_ACL_PERMIT_UNMATCH/4]&(BIT(4)|BIT(8)))==0);
  assert(registers[YT921X_ACLn_ENTRY(YT921X_QINQ_ACL_BLOCK)/4]==0);
 }
 reset();priv.qinq=true;
 for(int i=0;i<4;i++) priv.ds.ports[i].cpu_dp=&priv.ds.ports[4];
 assert(!yt921x_qinq_acl_setup(&priv));
 assert(FIELD_GET(YT921X_ACL_KEYb_SPORTS_M,registers[(YT921X_ACLn_KEYm(YT921X_QINQ_ACL_BLOCK,0)+4)/4])==BIT(8));
 reset();priv.qinq=true;priv.dt_secondary_conduit_user_mask=BIT(2);
 assert(yt921x_qinq_acl_setup(&priv)==-EINVAL);assert(writes==0);
 printf("PASS: QinQ CPU/port mapping, teardown and %d commit failure points fail closed\n",aclwrites-1);
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='yt921x-transport-') as temporary:
    directory=Path(temporary)
    source=directory/'transport.c'
    source.write_text(preamble+'\n'+''.join(defines)+'\n'+'\n'.join(functions)+'\n'+acl+'\n'+transport+'\n'+tests)
    executable=directory/'transport-test'
    subprocess.run(['cc','-std=gnu11','-O1','-g','-fsanitize=address,undefined',str(source),'-o',str(executable)],check=True)
    # LeakSanitizer cannot run under the traced execution sandbox. This
    # harness uses no heap allocation; retain ASan and UBSan checking.
    environment = dict(os.environ)
    environment['ASAN_OPTIONS'] = environment.get('ASAN_OPTIONS', '') + ':detect_leaks=0'
    subprocess.run([str(executable)], check=True, env=environment)
