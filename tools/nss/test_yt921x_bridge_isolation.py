#!/usr/bin/env python3
"""Compile real bridge-leave/isolation bodies and check peer destination masks."""
from pathlib import Path
import argparse,os,subprocess,tempfile
from test_yt921x_queue_index import extract_function,extract_define,sanitizer_flags
C=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
typedef uint32_t u32;
#define BIT(n) (1U<<(n))
struct device { int dummy; };
struct dsa_switch { int dummy; };
struct dsa_bridge { int dummy; };
struct yt921x_priv { struct dsa_switch ds;struct device dev;u32 cpu_ports_mask;bool tag_8021q;int reg_lock;struct {bool mrouter;} ports[11]; };
static u32 regs[11];static int calls,fail_call,standalone_error,flood,mrouter,fault,tag_leave;
static struct yt921x_priv *yt921x_to_priv(struct dsa_switch *d){return (struct yt921x_priv *)d;}
static struct device *yt921x_dev(struct yt921x_priv*p){return &p->dev;}
static bool dsa_is_cpu_port(struct dsa_switch*d,int p){return !!(yt921x_to_priv(d)->cpu_ports_mask&BIT(p));}
static void mutex_lock(int*p){assert(!*p);++*p;}static void mutex_unlock(int*p){assert(*p==1);--*p;}
static void dev_err(struct device*d,const char*f,...){(void)d;(void)f;}
static int yt921x_reg_set_bits(struct yt921x_priv*p,u32 r,u32 mask){assert(p->reg_lock==1);int i=(r-0x180294)/4;assert(i>=0&&i<11);calls++;if(calls==fail_call)return -EIO;regs[i]|=mask;return 0;}
static int yt921x_isolate(struct yt921x_priv *priv,int port);
static int yt921x_8021q_standalone(struct yt921x_priv*p,int port){assert(p->reg_lock==1);regs[port]|=0x6ef;return standalone_error;}
static int yt921x_bridge_leave(struct yt921x_priv*p,int port){int r=yt921x_8021q_standalone(p,port);return r?r:yt921x_isolate(p,port);}
static void dsa_tag_8021q_bridge_leave(struct dsa_switch*d,int port,struct dsa_bridge b){(void)d;(void)port;(void)b;tag_leave++;}
static int yt921x_refresh_flood_masks_locked(struct yt921x_priv*p){assert(p->reg_lock==1);flood++;return 0;}
static int yt921x_sync_mrouter_masks_locked(struct yt921x_priv*p){assert(p->reg_lock==1);mrouter++;return 0;}
static void yt921x_8021q_fault(struct yt921x_priv*p,int port){(void)port;assert(p->reg_lock==1);fault++;}
'''
T=r'''
static void reset(struct yt921x_priv*p){memset(p,0,sizeof(*p));memset(regs,0,sizeof(regs));p->cpu_ports_mask=BIT(4)|BIT(8);p->tag_8021q=true;p->ports[1].mrouter=true;calls=fail_call=standalone_error=flood=mrouter=fault=tag_leave=0;regs[0]=0x6f9;regs[1]=0x6ff;regs[2]=0x6fc;regs[4]=0x7f7;regs[8]=0x118;}
int main(void){struct yt921x_priv p;struct dsa_bridge b={0};
 reset(&p);yt921x_dsa_port_bridge_leave(&p.ds,1,b);assert(regs[0]==0x6fb&&regs[2]==0x6fe&&regs[1]==0x6ff);assert(regs[4]==0x7f7&&regs[8]==0x118);assert(calls==8&&flood==1&&mrouter==1&&!p.ports[1].mrouter&&!p.reg_lock&&!fault&&tag_leave==1);
 reset(&p);p.tag_8021q=false;yt921x_dsa_port_bridge_leave(&p.ds,1,b);assert(regs[0]==0x6fb&&regs[2]==0x6fe&&calls==8&&!tag_leave&&!fault);
 reset(&p);standalone_error=-EIO;yt921x_dsa_port_bridge_leave(&p.ds,1,b);assert(!calls&&!flood&&!mrouter&&fault==1&&!p.reg_lock);
 reset(&p);fail_call=2;yt921x_dsa_port_bridge_leave(&p.ds,1,b);assert(calls==2&&!flood&&!mrouter&&fault==1&&!p.reg_lock);
 reset(&p);yt921x_dsa_port_bridge_leave(&p.ds,8,b);assert(!calls&&!flood&&!tag_leave&&!p.reg_lock);
 puts("PASS bridge leave isolates all former peers, preserves CPU bits and stops on error");return 0;}
'''
def main():
 p=argparse.ArgumentParser();p.add_argument('--source',type=Path);p.add_argument('--sanitize',default='');a=p.parse_args();root=Path(__file__).resolve().parents[2];d=root/'target/linux/generic/files/drivers/net/dsa';source=(a.source or d/'yt921x_l2.c').read_text();header=(d/'yt921x_enhanced.h').read_text()
 text=C+'\n'+extract_define(header,'YT921X_PORT_NUM')+'\n'+extract_define(header,'YT921X_PORTn_ISOLATION')+'\n'+'\n'.join(extract_function(source,n) for n in ['yt921x_isolate','yt921x_dsa_port_bridge_leave'])+T
 flags,environment=sanitizer_flags(a.sanitize)
 with tempfile.TemporaryDirectory(prefix='yt921x-bridge-isolation-') as td:
  c=Path(td)/'test.c';c.write_text(text);exe=Path(td)/'test';subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror',*flags,str(c),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True,env=environment)
if __name__=='__main__':main()
