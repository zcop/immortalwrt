#!/usr/bin/env python3
"""Check production FDB add/delete database mapping, including CPU callbacks.

Run with --source old_yt921x_l2.c for a negative control. No hardware touched.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
from test_yt921x_queue_index import extract_define, extract_function, sanitizer_flags

PREAMBLE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
typedef unsigned short u16;
typedef unsigned int u32;
#define BIT(n) (1U << (n))
struct net_device { bool filtering; };
struct dsa_bridge { struct net_device *dev; unsigned int num; };
struct dsa_port { int index; bool user, filtering; struct dsa_bridge *bridge; };
struct dsa_switch { struct dsa_port ports[12]; void *priv; };
struct yt921x_priv { struct dsa_switch ds; bool tag_8021q; int reg_lock; };
enum dsa_db_type { DSA_DB_PORT, DSA_DB_LAG, DSA_DB_BRIDGE };
struct dsa_db { enum dsa_db_type type; const struct dsa_port *dp; struct dsa_bridge bridge; };
#define dsa_switch_for_each_user_port(dp, ds) \
 for (int i=0; i<12; i++) if (((dp)=&(ds)->ports[i])->user)
static inline bool br_vlan_enabled(const struct net_device *d) { return d->filtering; }
static bool dsa_is_user_port(struct dsa_switch *ds, int port) { return ds->ports[port].user; }
static inline bool dsa_port_is_vlan_filtering(const struct dsa_port *dp) { return dp->filtering; }
static bool dsa_port_offloads_bridge_dev(const struct dsa_port *dp, const struct net_device *d) { return dp->bridge && dp->bridge->dev == d; }
static u16 dsa_tag_8021q_bridge_vid(unsigned int n) { return 3072 | (n << 4); }
static u16 dsa_tag_8021q_standalone_vid(const struct dsa_port *dp) { return 3072 | dp->index; }
static struct yt921x_priv *yt921x_to_priv(struct dsa_switch *ds) { return ds->priv; }
static int depth, error_code, calls;
static u16 actual_vid;
static u32 actual_ports;
static void mutex_lock(int *m) { (void)m; assert(!depth); depth++; }
static void mutex_unlock(int *m) { (void)m; assert(depth==1); depth--; }
static int record(struct yt921x_priv *p,const unsigned char *a,u16 v,u32 ports) {
 (void)p; (void)a; assert(depth==1); calls++; actual_vid=v; actual_ports=ports; return error_code;
}
#define yt921x_fdb_join record
#define yt921x_fdb_leave record
'''
TESTS = r'''
static const unsigned char mac[6] = {2,0,0,0,0,1};
static void check(struct yt921x_priv *p, struct dsa_db db, u16 input, u16 want) {
 /* Host operations target CPUs; db.dp/bridge, not that CPU, owns the VID. */
 const int ports[] = {0,4,8};
 for (unsigned i=0; i<3; i++) {
  calls=0; error_code=0;
  assert(yt921x_dsa_port_fdb_add(&p->ds,ports[i],mac,input,db)==0);
  assert(calls==1 && actual_vid==want && actual_ports==BIT(ports[i]) && !depth);
  assert(yt921x_dsa_port_fdb_del(&p->ds,ports[i],mac,input,db)==0);
  assert(calls==2 && actual_vid==want && actual_ports==BIT(ports[i]) && !depth);
  error_code=-EIO;
  assert(yt921x_dsa_port_fdb_add(&p->ds,ports[i],mac,input,db)==-EIO && !depth);
  assert(yt921x_dsa_port_fdb_del(&p->ds,ports[i],mac,input,db)==-EIO && !depth);
 }
}
int main(void) {
 _Static_assert(YT921X_VID_UNAWARE==4095,"unaware hardware domain");
 struct yt921x_priv p={0}; p.ds.priv=&p;
 struct net_device a={0}, b={.filtering=true}, other={0};
 struct dsa_bridge bridge={.dev=&a,.num=2};
 for(int i=0;i<12;i++)p.ds.ports[i].index=i;
 p.ds.ports[0].user=true; p.ds.ports[0].bridge=&bridge;
 p.ds.ports[1].user=true;
 struct dsa_db db={.type=DSA_DB_BRIDGE,.bridge={.dev=&a,.num=0}};
 check(&p,db,0,4095);             /* Linux VID0 in enhanced unaware bridge */
 db.bridge.dev=&b;
 check(&p,db,0,0);                /* aware bridge VID0 is not the unaware FID */
 for(int tag=0;tag<2;tag++) {
  p.tag_8021q=tag;
  for(int aware=0;aware<2;aware++) {
   a.filtering=aware; db.bridge.dev=&a;
   check(&p,db,35,35); check(&p,db,4094,4094);
  }
 }
 p.tag_8021q=false;
 db=(struct dsa_db){.type=DSA_DB_PORT,.dp=&p.ds.ports[1]};
 check(&p,db,0,4095);
 p.ds.ports[1].filtering=true; check(&p,db,0,0);
 db.dp=&p.ds.ports[8]; check(&p,db,0,0); /* do not reinterpret CPU databases */
 p.tag_8021q=true; db.dp=&p.ds.ports[1];
 check(&p,db,0,3073);             /* standalone private VID preserved */
 db=(struct dsa_db){.type=DSA_DB_BRIDGE,.bridge={.dev=&a,.num=0}};
 check(&p,db,0,3104);             /* member bridge num2, not zeroed db num */
 db.bridge.num=2; p.ds.ports[0].bridge=NULL; check(&p,db,0,3104);
 db.bridge.num=0; db.bridge.dev=&other; check(&p,db,0,0); /* absent bridge mapping unchanged */
 for(int tag=0;tag<2;tag++) {
  p.tag_8021q=tag; db=(struct dsa_db){.type=DSA_DB_LAG}; check(&p,db,0,0);
 }
 puts("PASS: yt921x FDB database domains and symmetric add/delete");
 return 0;
}
'''

def main():
 root=Path(__file__).resolve().parents[2]
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--source',type=Path,default=root/'target/linux/generic/files/drivers/net/dsa/yt921x_l2.c')
 p.add_argument('--sanitize',nargs='?',const='address,undefined,leak')
 a=p.parse_args();src=a.source.read_text()
 header=(root/'target/linux/generic/files/drivers/net/dsa/yt921x_enhanced.h').read_text()
 code=PREAMBLE+extract_define(header,'YT921X_VID_UNAWARE')+'\n'
 for name in ['yt921x_db_vid','yt921x_dsa_port_fdb_add','yt921x_dsa_port_fdb_del']:code+=extract_function(src,name)+'\n'
 flags,env=sanitizer_flags(a.sanitize)
 with tempfile.TemporaryDirectory(prefix='yt921x-db-') as td:
  c=Path(td)/'test.c';exe=Path(td)/'test';c.write_text(code+TESTS)
  subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-O0',*flags,str(c),'-o',str(exe)],check=True)
  return subprocess.run([str(exe)],env={**os.environ,**env}).returncode
if __name__=='__main__':raise SystemExit(main())
