#!/usr/bin/env python3
"""Exercise the actual NSS host-restore function with two DSA conduits.

Models DSA's destructive NETDEV_GOING_DOWN notification and checks port
administration state, failure handling and notifier suppression. No hardware.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'package/kernel/qca-dwmac-nss/src/qca_dwmac_nss.c').read_text()
function = re.search(r'^static int dwmac_nss_host_restore\(.*?^\}', source, re.M | re.S)
assert function, 'Actual host-restore function not found'
preamble = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define IS_ENABLED(x) (x)
#define GFP_KERNEL 0
#define ASSERT_RTNL() assert(rtnl_held)
#define netdev_warn(...) ((void)0)
#define netdev_info(...) ((void)0)
struct net_device;
struct dsa_port;
struct port_list { struct dsa_port *first; };
struct dsa_switch_tree { struct port_list ports; };
struct dsa_switch { struct dsa_switch_tree *dst; };
struct dsa_port {
 struct dsa_switch *ds;
 struct dsa_port *cpu_dp, *next;
 struct net_device *user;
 bool is_user;
};
struct net_device {
 int id;
 bool running;
 struct dsa_port *dsa_ptr, *user_dp;
};
#define list_for_each_entry(p,h,m) for ((p)=(h)->first; (p); (p)=(p)->next)
#if CONFIG_NET_DSA
static bool netdev_uses_dsa(struct net_device *d) { return d->dsa_ptr != NULL; }
static bool dsa_port_is_user(struct dsa_port *p) { return p->is_user; }
static bool netif_running(struct net_device *d) { return d->running; }
#endif
static bool dwmac_nss_in_restore, rtnl_held = true;
static bool alloc_fail;
static int alloc_calls, allocations, fail_open = -1;
static int opens[32], closes[32];
static struct dsa_switch_tree tree;
static struct dsa_switch sw = {&tree};
static struct dsa_port cpu[2], ports[4];
static struct net_device conduits[2], users[4];
static void *kcalloc(size_t n, size_t size, int flags) {
 (void)flags; alloc_calls++;
 if (alloc_fail) return NULL;
 void *p=calloc(n,size);assert(p);allocations++;return p;
}
static void kfree(void *p) { if (p) {allocations--;free(p);} }
static void dev_close(struct net_device *d) {
 assert(dwmac_nss_in_restore);closes[d->id]++;d->running=false;
 if (d->dsa_ptr) {
  struct dsa_port *p;
  list_for_each_entry(p,&tree.ports,list)
   if (p->is_user && p->cpu_dp==d->dsa_ptr) p->user->running=false;
 }
}
static int dev_open(struct net_device *d, void *extack) {
 (void)extack;
 /* Each NETDEV_UP would deadlock the glue notifier if this were false. */
 assert(dwmac_nss_in_restore);opens[d->id]++;
 if (d->id==fail_open) return d->user_dp ? -ENOSPC : -EIO;
 if (d->user_dp) assert(conduits[d->user_dp->cpu_dp==&cpu[1]].running);
 d->running=true;return 0;
}
static void reset(void) {
 assert(!allocations);memset(opens,0,sizeof(opens));memset(closes,0,sizeof(closes));
 memset(cpu,0,sizeof(cpu));memset(ports,0,sizeof(ports));
 memset(conduits,0,sizeof(conduits));memset(users,0,sizeof(users));
 alloc_calls=0;alloc_fail=false;fail_open=-1;dwmac_nss_in_restore=false;
 for (int i=0;i<2;i++) {
  cpu[i].ds=&sw;conduits[i].id=10+i;conduits[i].running=true;
  conduits[i].dsa_ptr=&cpu[i];
 }
 tree.ports.first=&ports[0];
 for (int i=0;i<4;i++) {
  ports[i].ds=&sw;ports[i].is_user=true;ports[i].user=&users[i];
  ports[i].cpu_dp=&cpu[i==3];ports[i].next=i==3?NULL:&ports[i+1];
  users[i].id=i;users[i].running=true;users[i].user_dp=&ports[i];
 }
}
'''
tests = r'''
int main(void) {
 int tests=0;
 reset();conduits[0].dsa_ptr=NULL;
 assert(!dwmac_nss_host_restore(&conduits[0]));
 assert(conduits[0].running && opens[10]==1 && closes[10]==1);
 assert(users[0].running && !alloc_calls && !dwmac_nss_in_restore);tests++;
#if CONFIG_NET_DSA
 reset();assert(!dwmac_nss_host_restore(&conduits[0]));
 for (int i=0;i<4;i++) assert(users[i].running);
 assert(opens[0]==1 && opens[1]==1 && opens[2]==1 && opens[3]==0);
 assert(conduits[1].running && !closes[11] && !allocations);tests++;
 reset();users[1].running=false;
 assert(!dwmac_nss_host_restore(&conduits[0]));
 assert(users[0].running && !users[1].running && users[2].running);
 assert(!opens[1] && !opens[3] && users[3].running);tests++;
 reset();assert(!dwmac_nss_host_restore(&conduits[1]));
 assert(users[3].running && opens[3]==1);
 assert(users[0].running && !opens[0] && !closes[10]);tests++;
 reset();for (int i=0;i<3;i++) users[i].running=false;
 assert(!dwmac_nss_host_restore(&conduits[0]));
 assert(!alloc_calls && !opens[0] && !opens[1] && !opens[2]);tests++;
 reset();fail_open=10;
 assert(dwmac_nss_host_restore(&conduits[0])==-EIO);
 assert(!conduits[0].running && !opens[0] && !opens[1] && !opens[2]);
 assert(users[3].running && !dwmac_nss_in_restore && !allocations);tests++;
 reset();fail_open=0;
 assert(dwmac_nss_host_restore(&conduits[0])==-ENOSPC);
 assert(!users[0].running && users[1].running && users[2].running);
 assert(users[3].running && !dwmac_nss_in_restore && !allocations);tests++;
 reset();alloc_fail=true;
 assert(dwmac_nss_host_restore(&conduits[0])==-ENOMEM);
 assert(conduits[0].running && opens[10]==1 && closes[10]==1);
 assert(!users[0].running && !users[1].running && !users[2].running);
 assert(users[3].running && !dwmac_nss_in_restore && !allocations);tests++;
 reset();users[1].running=false;
 for (int i=0;i<3;i++) assert(!dwmac_nss_host_restore(&conduits[0]));
 assert(users[0].running && !users[1].running && users[2].running);
 assert(opens[0]==3 && opens[2]==3 && !opens[1] && !allocations);tests++;
#endif
 assert(!dwmac_nss_in_restore && !allocations);
 printf("CONFIG_NET_DSA=%d: %d state/failure cases passed\n",CONFIG_NET_DSA,tests);
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='nss-host-restore-') as directory:
    c = Path(directory) / 'restore.c'
    c.write_text(preamble + function[0] + tests)
    for dsa in (1, 0):
        binary = Path(directory) / f'restore-{dsa}'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-function', '-fsanitize=address,undefined',
                        '-fno-pie', '-no-pie', f'-DCONFIG_NET_DSA={dsa}',
                        str(c), '-o', str(binary)], check=True)
        env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
        subprocess.run([str(binary)], env=env, check=True)
