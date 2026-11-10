#!/usr/bin/env python3
"""Compile real FDB reconciliation and bridge-policy bodies against controlled I/O.

Covers bounded walks, last-index safety, event/retry/lifecycle handling and
native/customer VID mapping. The bridge policy body comes from the actual
kernel patch and is compiled with VLAN filtering both enabled and disabled.
These are host control-path tests; hardware packet gates remain separate.
"""
from pathlib import Path
import argparse, os, re, subprocess, tempfile
from test_yt921x_queue_index import extract_function, extract_define, sanitizer_flags

BRIDGE = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef uint8_t u8; typedef uint16_t u16;
#define BIT(n) (1UL<<(n))
#define READ_ONCE(x) (x)
#define BR_STATE_DISABLED 0
#define BR_STATE_LISTENING 1
#define BR_STATE_LEARNING 2
#define BR_STATE_FORWARDING 3
#define BR_STATE_BLOCKING 4
#define BR_LEARNING BIT(0)
#define BR_PORT_LOCKED BIT(1)
#define BR_FDB_LOCAL 0
#define BR_FDB_STATIC 1
#define BR_FDB_STICKY 2
#define BR_FDB_LOCKED 3
struct net_device { struct net_bridge_port *port; bool filtering; };
struct net_bridge { struct net_device *dev; int fdb_hash_tbl; };
struct net_bridge_port { struct net_bridge *br; unsigned long flags; u8 state; };
struct net_bridge_fdb_entry { struct net_bridge_port *dst; unsigned long flags; };
struct net_bridge_vlan { u16 vid; bool usable; u8 state; };
static struct net_device bridge_dev, port_dev;
static struct net_bridge bridge;
static struct net_bridge_port old_port, new_port;
static struct net_bridge_fdb_entry entry;
#ifdef CONFIG_BRIDGE_VLAN_FILTERING
static struct net_bridge_vlan vlan;
static bool vlan_present, mst;
#endif
static bool present;
static unsigned updates, depth;
static u16 learned_vid;
#define ASSERT_RTNL() do {} while(0)
static void rcu_read_lock(void){depth++;}
static void rcu_read_unlock(void){assert(depth);depth--;}
static struct net_bridge_port *br_port_get_rcu(struct net_device *d){return d->port;}
static bool is_valid_ether_addr(const unsigned char *a){static const u8 zero[6];return !(a[0]&1)&&memcmp(a,zero,6);}
static bool test_bit(unsigned n,const unsigned long *v){return !!(*v&BIT(n));}
#ifdef CONFIG_BRIDGE_VLAN_FILTERING
static bool br_vlan_enabled(struct net_device *d){return d->filtering;}
static void *nbp_vlan_group_rcu(struct net_bridge_port *p){(void)p;return &vlan;}
static struct net_bridge_vlan *br_vlan_find(void *g,u16 v){(void)g;return vlan_present&&v==vlan.vid?&vlan:NULL;}
static bool br_vlan_should_use(struct net_bridge_vlan *v){return v->usable;}
static bool br_mst_is_enabled(struct net_bridge_port *p){(void)p;return mst;}
static u8 br_vlan_get_state(struct net_bridge_vlan *v){return v->state;}
#endif
static struct net_bridge_fdb_entry *fdb_find_rcu(void *h,const unsigned char *a,u16 v){(void)h;(void)a;(void)v;return present?&entry:NULL;}
static void br_fdb_update(struct net_bridge *b,struct net_bridge_port *p,const unsigned char *a,u16 v,unsigned long flags){(void)a;assert(b==&bridge&&p==&new_port&&entry.dst==&old_port&&flags==0);updates++;learned_vid=v;entry.dst=p;}
/* FUNCTION */
static void reset(void){bridge_dev.filtering=false;bridge.dev=&bridge_dev;new_port=(struct net_bridge_port){.br=&bridge,.flags=BR_LEARNING,.state=BR_STATE_FORWARDING};old_port.br=&bridge;port_dev.port=&new_port;entry=(struct net_bridge_fdb_entry){.dst=&old_port};present=true;updates=depth=0;learned_vid=65535;
#ifdef CONFIG_BRIDGE_VLAN_FILTERING
vlan=(struct net_bridge_vlan){.vid=35,.usable=true,.state=BR_STATE_FORWARDING};vlan_present=true;mst=false;
#endif
}
static const u8 mac[6]={2,0x30,0,0,0,1};
static void check(bool want,u16 vid){bool got=br_fdb_update_from_hw(&port_dev,mac,vid);assert(got==want&&updates==(unsigned)want&&depth==0);if(want)assert(entry.dst==&new_port&&learned_vid==vid&&!entry.flags);}
int main(void){
reset();check(true,0);
reset();entry.dst=&new_port;check(false,0);
reset();present=false;check(false,0);
reset();entry.dst=NULL;check(false,0);
reset();port_dev.port=NULL;check(false,0);
reset();new_port.flags=0;check(false,0);
reset();new_port.flags|=BR_PORT_LOCKED;check(false,0);
for(unsigned i=0;i<4;i++){reset();entry.flags=BIT(i);check(false,0);assert(entry.flags==BIT(i));}
for(u8 s=0;s<=4;s++){reset();new_port.state=s;check(s==2||s==3,0);}
reset();check(false,35);
reset();u8 group[6]={1,0,0,0,0,1};assert(!br_fdb_update_from_hw(&port_dev,group,0)&&!updates&&depth==0);
#ifdef CONFIG_BRIDGE_VLAN_FILTERING
reset();bridge_dev.filtering=true;check(true,35);
reset();bridge_dev.filtering=true;check(false,36);
reset();bridge_dev.filtering=true;check(false,0);
reset();bridge_dev.filtering=true;vlan_present=false;check(false,35);
reset();bridge_dev.filtering=true;vlan.usable=false;check(false,35);
reset();bridge_dev.filtering=true;vlan.state=BR_STATE_BLOCKING;check(false,35);
reset();bridge_dev.filtering=true;mst=true;new_port.state=BR_STATE_BLOCKING;check(true,35);
reset();bridge_dev.filtering=true;mst=true;vlan.state=BR_STATE_BLOCKING;check(false,35);
#endif
puts("PASS: hardware move bridge learning and protected-entry policy");return 0;}
"""

DRIVER = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef uint64_t u64;
#define ETH_ALEN 6
#define BIT(n) (1U<<(n))
#define GENMASK(h,l) ((~0U<<(l))&(~0U>>(31-(h))))
#define FIELD_PREP(m,v) (((u32)(v)<<__builtin_ctz(m))&(m))
#define FIELD_GET(m,v) (((u32)(v)&(m))>>__builtin_ctz(m))
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
#define to_delayed_work(w) ((struct delayed_work*)(w))
#define msecs_to_jiffies(v) (v)
#define INIT_DELAYED_WORK(w,f) ((w)->fn=(f))
/* DEFINES */
struct work_struct{int unused;};struct delayed_work{struct work_struct work;void(*fn)(struct work_struct*);};
struct net_device{bool filtering;};
struct dsa_port{bool userport;struct net_device *user,*bridge,*lag;unsigned bridge_num;};
struct dsa_switch{struct dsa_port ports[11];};
struct yt921x_priv{struct dsa_switch ds;int reg_lock;bool qinq;int vlan_fid_mode;u16 vlan_svl_fid;struct delayed_work fdb_sync_work;bool fdb_sync_active,fdb_sync_pending,fdb_sync_restart,fdb_sync_first;u16 fdb_sync_cursor;u32 fdb_sync_last_status;u64 fdb_sync_events,fdb_sync_rows,fdb_sync_moves,fdb_sync_errors;};
static bool busy,rt,locked,fail_status,fail_row;
static unsigned schedules,cancels,reads,commands,updates;
static u32 irq,last_op;
static u16 last_vid;static struct net_device *last_dev;
struct row{u16 index,fid,ports;u8 status;};
static struct row table[128];static unsigned nr;
static struct net_device br_dev,user_dev,bond_dev;
static int rtnl_trylock(void){if(busy)return 0;assert(!rt);rt=true;return 1;}
static void rtnl_unlock(void){assert(rt&&!locked);rt=false;}
static void mutex_lock(int *m){(void)m;assert(rt&&!locked);locked=true;}
static void mutex_unlock(int *m){(void)m;assert(locked);locked=false;}
static void schedule_delayed_work(struct delayed_work *w,unsigned ms){(void)w;assert(ms==100);schedules++;}
static void cancel_delayed_work_sync(struct delayed_work *w){(void)w;cancels++;}
static bool is_valid_ether_addr(const u8*a){static const u8 z[6];return !(a[0]&1)&&memcmp(a,z,6);}
static bool is_power_of_2(u16 x){return x&&!(x&(x-1));}
static unsigned __ffs(u16 x){assert(x);return __builtin_ctz(x);}
static bool dsa_is_user_port(struct dsa_switch*d,unsigned p){assert(p<11);return d->ports[p].userport;}
static struct dsa_port*dsa_to_port(struct dsa_switch*d,unsigned p){return &d->ports[p];}
static struct net_device*dsa_port_bridge_dev_get(struct dsa_port*p){return p->bridge;}
static struct net_device*dsa_port_to_bridge_port(struct dsa_port*p){return p->lag?p->lag:p->user;}
static unsigned dsa_port_bridge_num_get(struct dsa_port*p){return p->bridge_num;}
static u16 dsa_tag_8021q_bridge_vid(unsigned n){return 3072+n*16;}
static bool br_vlan_enabled(struct net_device*d){return d->filtering;}
static void ether_addr_copy(u8*d,const u8*s){memcpy(d,s,6);}
static bool br_fdb_update_from_hw(struct net_device*d,const u8*a,u16 vid){(void)a;assert(rt&&!locked);updates++;last_dev=d;last_vid=vid;return true;}
static int yt921x_reg_read(struct yt921x_priv*p,u32 reg,u32*v){(void)p;assert(rt&&locked&&reg==YT921X_INTR_STATUS);reads++;*v=irq;irq=0;if(fail_status){fail_status=false;return-EIO;}return 0;}
static int yt921x_reg_write(struct yt921x_priv*p,u32 reg,u32 v){(void)p;assert(rt&&locked&&reg==YT921X_FDB_OP);last_op=v;commands++;if((v&YT921X_FDB_OP_OP_M)==YT921X_FDB_OP_OP_GET_NEXT)assert(FIELD_GET(YT921X_FDB_OP_INDEX_M,v)!=4095);return 0;}
static int yt921x_fdb_read(struct yt921x_priv*p,u8*a,u16*fid,u16*ports,u16*index,u8*status){(void)p;assert(rt&&locked);if(fail_row){fail_row=false;return-EIO;}bool first=(last_op&YT921X_FDB_OP_OP_M)==YT921X_FDB_OP_OP_GET_ONE;unsigned from=FIELD_GET(YT921X_FDB_OP_INDEX_M,last_op);*ports=0;for(unsigned i=0;i<nr;i++)if(first?table[i].index==0:table[i].index>from){*index=table[i].index;*fid=table[i].fid;*ports=table[i].ports;*status=table[i].status;memset(a,0,6);a[0]=2;a[4]=table[i].index>>8;a[5]=table[i].index;break;}return 0;}
/* FUNCTIONS */
static void reset(struct yt921x_priv*p){memset(p,0,sizeof(*p));busy=rt=locked=fail_status=fail_row=false;schedules=cancels=reads=commands=updates=irq=0;nr=0;br_dev.filtering=false;p->qinq=true;p->fdb_sync_active=true;p->fdb_sync_first=true;p->ds.ports[0]=(struct dsa_port){.userport=true,.user=&user_dev,.bridge=&br_dev,.bridge_num=1};}
static void tick(struct yt921x_priv*p){yt921x_fdb_sync_workfn(&p->fdb_sync_work.work);assert(!rt&&!locked);}
int main(void){struct yt921x_priv p;const u8 mac[6]={2,0x30,0,0,0,1};
reset(&p);rt=true;assert(yt921x_fdb_sync_entry(&p,mac,3088,1,5)&&last_dev==&user_dev&&last_vid==0);
updates=0;assert(!yt921x_fdb_sync_entry(&p,mac,3072,1,5)&&!updates);
br_dev.filtering=true;assert(yt921x_fdb_sync_entry(&p,mac,35,1,5)&&last_vid==35);
p.vlan_fid_mode=YT921X_VLAN_FID_MODE_SVL;updates=0;assert(!yt921x_fdb_sync_entry(&p,mac,1,1,5)&&!updates);
br_dev.filtering=false;p.vlan_svl_fid=7;assert(yt921x_fdb_sync_entry(&p,mac,7,1,5)&&last_vid==0);
p.vlan_fid_mode=YT921X_VLAN_FID_MODE_IVL;updates=0;assert(!yt921x_fdb_sync_entry(&p,mac,3088,5,5));assert(!yt921x_fdb_sync_entry(&p,mac,3088,BIT(8),5));
for(u8 s=0;s<8;s++){bool want=s==1||s==3||s==5;unsigned before=updates;assert(yt921x_fdb_sync_entry(&p,mac,3088,1,s)==want);assert(updates==before+(unsigned)want);}
p.ds.ports[0].lag=&bond_dev;assert(yt921x_fdb_sync_entry(&p,mac,3088,1,5)&&last_dev==&bond_dev);p.ds.ports[0].bridge=NULL;assert(!yt921x_fdb_sync_entry(&p,mac,3088,1,5));rt=false;
reset(&p);tick(&p);assert(reads==1&&!commands&&!p.fdb_sync_pending);
reset(&p);busy=true;irq=YT921X_INTR_FDB_MOVE;tick(&p);assert(!reads&&!commands&&irq==YT921X_INTR_FDB_MOVE&&schedules==1);
reset(&p);p.fdb_sync_active=false;tick(&p);assert(!reads&&!commands&&!schedules);
reset(&p);nr=2;table[0]=(struct row){0,3088,1,5};table[1]=(struct row){4095,3088,1,5};irq=YT921X_INTR_FDB_MOVE;tick(&p);assert(updates==2&&!p.fdb_sync_pending&&commands==2);
reset(&p);nr=96;for(unsigned i=0;i<nr;i++)table[i]=(struct row){i,3088,1,5};irq=YT921X_INTR_FDB_MOVE;tick(&p);assert(commands==32&&p.fdb_sync_cursor==31&&p.fdb_sync_pending&&updates==32);
irq=YT921X_INTR_FDB_CREATE;tick(&p);assert(p.fdb_sync_restart&&p.fdb_sync_cursor==63);tick(&p);assert(p.fdb_sync_cursor==95);tick(&p);assert(p.fdb_sync_pending&&p.fdb_sync_first&&!p.fdb_sync_cursor&&!p.fdb_sync_restart);
for(unsigned i=0;i<4;i++){tick(&p);}assert(!p.fdb_sync_pending&&updates==192);
reset(&p);nr=1;table[0]=(struct row){1,3088,1,5};irq=YT921X_INTR_FDB_MOVE;fail_row=true;tick(&p);assert(p.fdb_sync_errors==1&&p.fdb_sync_pending&&p.fdb_sync_first&&!p.fdb_sync_cursor);tick(&p);assert(updates==1&&!p.fdb_sync_pending);
reset(&p);nr=1;table[0]=(struct row){1,3088,1,5};fail_status=true;tick(&p);assert(p.fdb_sync_errors==1&&p.fdb_sync_pending);tick(&p);assert(updates==2&&!p.fdb_sync_pending);
reset(&p);p.fdb_sync_active=false;p.qinq=false;yt921x_fdb_sync_init(&p);yt921x_fdb_sync_start(&p);assert(!schedules);p.qinq=true;p.fdb_sync_cursor=80;yt921x_fdb_sync_start(&p);assert(schedules==1&&p.fdb_sync_cursor==0&&p.fdb_sync_first);yt921x_fdb_sync_start(&p);assert(schedules==1);yt921x_fdb_sync_stop(&p);assert(!p.fdb_sync_active&&cancels==1);
puts("PASS: hardware-event mapping, bounded cursor, retry, lock and lifecycle gates");return 0;}
"""

DEFINES = ["YT921X_INTR_STATUS", "YT921X_INTR_FDB_MOVE", "YT921X_INTR_FDB_CREATE",
    "YT921X_FDB_NUM", "YT921X_FDB_OP", "YT921X_FDB_OP_INDEX_M",
    "YT921X_FDB_OP_INDEX", "YT921X_FDB_OP_MODE_INDEX", "YT921X_FDB_OP_START",
    "YT921X_FDB_OP_OP_M", "YT921X_FDB_OP_OP", "YT921X_FDB_OP_OP_GET_ONE",
    "YT921X_FDB_OP_OP_GET_NEXT", "YT921X_FDB_OP_NEXT_TYPE_M",
    "YT921X_FDB_OP_NEXT_TYPE", "YT921X_FDB_OP_NEXT_TYPE_UCAST"]
FUNCTIONS = ["yt921x_vlan_fid_for_vid", "yt921x_fdb_sync_entry",
    "yt921x_fdb_sync_step", "yt921x_fdb_sync_workfn", "yt921x_fdb_sync_init",
    "yt921x_fdb_sync_start", "yt921x_fdb_sync_stop"]

def added_bridge_body(patch):
    section = patch.split("+++ b/net/bridge/br_fdb.c\n", 1)[1].split("--- a/", 1)[0]
    added = "\n".join(line[1:] for line in section.splitlines() if line.startswith("+"))
    return extract_function(added, "br_fdb_update_from_hw")

def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=root/"target/linux/generic/files/drivers/net/dsa/yt921x_l2.c")
    parser.add_argument("--patch", type=Path, default=root/"target/linux/generic/pending-6.18/967-56-net-bridge-reconcile-hardware-fdb-moves.patch")
    parser.add_argument("--sanitize", nargs="?", const="address,undefined,leak")
    args = parser.parse_args()
    source = args.source.read_text()
    header = (root/"target/linux/generic/files/drivers/net/dsa/yt921x_enhanced.h").read_text()
    definitions = "\n".join(extract_define(header, name) for name in DEFINES)
    for name in ["yt921x_fdb_entry_status", "yt921x_vlan_fid_mode"]:
        definitions += "\n" + re.search(r"enum " + name + r"\s*\{[^}]+\};", header).group()
    row = re.search(r"(?ms)^struct yt921x_fdb_sync_row \{.*?^\};", source).group()
    functions = row + "\n" + "\n".join(extract_function(source, name) for name in FUNCTIONS)
    driver = DRIVER.replace("/* DEFINES */", definitions).replace("/* FUNCTIONS */", functions)
    bridge = BRIDGE.replace("/* FUNCTION */", added_bridge_body(args.patch.read_text()))
    programs = [("bridge-no-vlan", bridge, []),
        ("bridge-vlan", bridge, ["-DCONFIG_BRIDGE_VLAN_FILTERING"]),
        ("driver", driver, [])]
    flags, env = sanitizer_flags(args.sanitize)
    with tempfile.TemporaryDirectory(prefix="yt921x-fdb-sync-") as tmp:
        for name, body, extra in programs:
            c = Path(tmp)/(name+".c"); binary = Path(tmp)/name
            c.write_text(body)
            subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O0",
                *flags, *extra, str(c), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], env={**os.environ, **env}, check=True)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
