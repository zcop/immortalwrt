#!/usr/bin/env python3
"""Execute the multicast kernel API helpers with controlled route/cache state.

Checks callback ownership, default-table namespace isolation, destination
bounds, statistics and balanced RCU/locks. This is not device acceptance.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
KERNEL = Path(os.environ['NSS_MC_KERNEL']) if 'NSS_MC_KERNEL' in os.environ else next(
    candidate for candidate in (ROOT / 'build_dir').glob(
        'target-*/linux-qualcommax_ipq50xx/linux-6.18.*') if candidate.is_dir())


def function(text, name):
    match = re.search(r'(?:static\s+)?(?:bool|int|void)\s+' + name +
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
#include <netinet/in.h>
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint32_t __be32;
#define MAXVIFS 32
#define MAXMIFS 32
#define RT_TABLE_DEFAULT 253
#define RT6_TABLE_DFLT 254
#define IPMR_MFC_EVENT_UPDATE 1
#define IPMR_MFC_EVENT_DELETE 2
#define IP6MR_MFC_EVENT_UPDATE 1
#define IP6MR_MFC_EVENT_DELETE 2
struct net { int id; } init_net, other_net;
struct net_device { int ifindex; } source_dev={10}, target_a={20}, target_b={30};
struct vif { struct net_device *dev; unsigned long pkt_in,bytes_in,pkt_out,bytes_out; };
struct mr_mfc;
struct list_head { struct mr_mfc *first; };
struct mr_table { struct net *net; int id; struct vif vif_table[32];struct list_head mfc_cache_list; } table;
struct mr_mfc {
 struct mr_mfc *next;
 unsigned int mfc_parent;
 struct { struct { int minvif,maxvif;unsigned char ttls[32];long pkt,bytes; } res; } mfc_un;
};
struct @CACHE@ { struct mr_mfc _c; @FIELDS@ } cache;
static int rcu_depth, mrt_lock, @PREFIX@_offload_lock;
static bool table_present=true, cache_present=true;
static int callbacks, event_count, event_kind;
@EXPECTED_DECL@
static u32 event_interfaces[32];
#define read_pnet(p) (*(p))
#define net_eq(a,b) ((a)==(b))
#define list_for_each_entry(p,h,field) for((p)=(h)->first;(p);(p)=(p)->next)
#define VIF_EXISTS(t,i) ((t)->vif_table[i].dev != NULL)
#define rcu_access_pointer(p) (p)
#define rcu_dereference(p) (assert(rcu_depth>0),(p))
#define rcu_assign_pointer(p,v) ((p)=(v))
static void rcu_read_lock(void) { rcu_depth++; }
static void rcu_read_unlock(void) { assert(rcu_depth>0);rcu_depth--; }
static void synchronize_rcu(void) { assert(!rcu_depth); }
static void spin_lock_bh(int *p) { assert(!*p);*p=1; }
static void spin_unlock_bh(int *p) { assert(*p);*p=0; }
static void atomic_long_add(long n,long *p) { *p+=n; }
typedef void (*@PREFIX@_mfc_event_offload_callback_t)(@CB_ARGS@,u32 count,u32 devices[],uint8_t op);
static @PREFIX@_mfc_event_offload_callback_t @PREFIX@_mfc_event_offload_callback;
static struct mr_table *@PREFIX@_get_table(struct net *n,int id) {
 assert(rcu_depth>0);return table_present?&table:NULL;
}
static struct @CACHE@ *@PREFIX@_cache_find(struct mr_table *t,@FIND_ARGS@) {
 assert(rcu_depth>0);@ADDRESS_CHECK@;return cache_present?&cache:NULL;
}
static void callback(@CB_ARGS@,u32 count,u32 devices[],uint8_t op) {
 assert(rcu_depth>0 && !mrt_lock);@ADDRESS_CHECK@;
 callbacks++;event_count=count;event_kind=op;
 for(u32 i=0;i<count;i++)event_interfaces[i]=devices[i];
}
'''
CHECKS = r'''
static void balanced(void) { assert(!rcu_depth && !mrt_lock && !@PREFIX@_offload_lock); }
int main(void) {
 table.net=&init_net;table.id=@DEFAULT@;
 table.mfc_cache_list.first=&cache._c;
 table.vif_table[0].dev=&source_dev;table.vif_table[1].dev=&target_a;table.vif_table[2].dev=&target_b;
 cache._c.mfc_parent=0;cache._c.mfc_un.res.maxvif=3;cache._c.mfc_un.res.minvif=1;
 cache._c.mfc_un.res.ttls[1]=1;cache._c.mfc_un.res.ttls[2]=255;
 @ADDR_DECL@
 @ADDRESS_INIT@
 u32 devices[3]={99,99,99};
 assert(!@PREFIX@_register_mfc_event_offload_callback(NULL));
 assert(@PREFIX@_register_mfc_event_offload_callback(callback));
 assert(!@PREFIX@_register_mfc_event_offload_callback(callback));balanced();
 @PREFIX@_sync_entry_update(&table,&cache);balanced();
 assert(callbacks==1 && event_count==1 && event_interfaces[0]==20);
 table.net=&other_net;@PREFIX@_sync_entry_update(&table,&cache);assert(callbacks==1);balanced();
 table.net=&init_net;table.id++;@PREFIX@_sync_entry_update(&table,&cache);assert(callbacks==1);balanced();
 table.id=@DEFAULT@;
 @PREFIX@_sync_vif_delete(&table,2);assert(callbacks==1);balanced();
 @PREFIX@_sync_vif_delete(&table,1);assert(callbacks==2&&event_kind==2);balanced();
 @PREFIX@_sync_vif_delete(&table,0);assert(callbacks==3&&event_kind==2);balanced();
 assert(@PREFIX@_find_mfc_entry(&init_net,@ADDR_ARGS@,3,devices)==1);assert(devices[0]==20);balanced();
 devices[0]=99;assert(@PREFIX@_find_mfc_entry(&init_net,@ADDR_ARGS@,0,devices)==-EINVAL);assert(devices[0]==99);balanced();
 cache._c.mfc_un.res.ttls[2]=1;
 assert(@PREFIX@_find_mfc_entry(&init_net,@ADDR_ARGS@,1,devices)==-EINVAL);balanced();
 assert(@PREFIX@_find_mfc_entry(&init_net,@ADDR_ARGS@,3,devices)==2);assert(devices[0]==20&&devices[1]==30);balanced();
 assert(!@PREFIX@_mfc_stats_update(&init_net,@ADDR_ARGS@,5,500,7,700));balanced();
 assert(table.vif_table[0].pkt_in==5&&table.vif_table[0].bytes_in==500);
 assert(table.vif_table[1].pkt_out==7&&table.vif_table[2].pkt_out==7);
 assert(cache._c.mfc_un.res.pkt==7&&cache._c.mfc_un.res.bytes==700);
 table.vif_table[2].dev=NULL;
 assert(@PREFIX@_find_mfc_entry(&init_net,@ADDR_ARGS@,3,devices)==-EINVAL);balanced();
 table.vif_table[2].dev=&target_b;
 cache_present=false;assert(@PREFIX@_find_mfc_entry(&init_net,@ADDR_ARGS@,3,devices)==-ENOENT);balanced();
 assert(@PREFIX@_mfc_stats_update(&init_net,@ADDR_ARGS@,1,1,1,1)==-ENOENT);balanced();cache_present=true;
 table_present=false;assert(@PREFIX@_find_mfc_entry(&init_net,@ADDR_ARGS@,3,devices)==-ENOENT);balanced();
 assert(@PREFIX@_mfc_stats_update(&init_net,@ADDR_ARGS@,1,1,1,1)==-ENOENT);balanced();table_present=true;
 @PREFIX@_sync_entry_delete(@ADDR_ARGS@);assert(event_kind==2&&event_count==0);balanced();
 int previous=callbacks;@PREFIX@_unregister_mfc_event_offload_callback();balanced();
 @PREFIX@_sync_entry_delete(@ADDR_ARGS@);assert(callbacks==previous);balanced();
 assert(@PREFIX@_register_mfc_event_offload_callback(callback));@PREFIX@_unregister_mfc_event_offload_callback();balanced();
 puts("PASS: @PREFIX@ callback, route/cache and lifetime checks");
}
'''

with tempfile.TemporaryDirectory(prefix='nss-multicast-kernel-') as directory:
    tmp = Path(directory)
    for version, prefix, cache, default in [(4, 'ipmr', 'mfc_cache', 'RT_TABLE_DEFAULT'),
                                             (6, 'ip6mr', 'mfc6_cache', 'RT6_TABLE_DFLT')]:
        text = (KERNEL / f'net/ipv{version}/{prefix}.c').read_text()
        helpers = '\n'.join(function(text, prefix + suffix) for suffix in (
            '_sync_entry_update', '_sync_entry_delete', '_sync_vif_delete',
            '_register_mfc_event_offload_callback', '_unregister_mfc_event_offload_callback',
            '_find_mfc_entry', '_mfc_stats_update'))
        replacements = {
            '@PREFIX@': prefix, '@CACHE@': cache, '@DEFAULT@': default,
            '@FIELDS@': '__be32 mfc_origin,mfc_mcastgrp;' if version == 4 else
                        'struct in6_addr mf6c_origin,mf6c_mcastgrp;',
            '@CB_ARGS@': '__be32 group,__be32 origin' if version == 4 else
                         'struct in6_addr *group,struct in6_addr *origin',
            '@FIND_ARGS@': '__be32 origin,__be32 group' if version == 4 else
                           'const struct in6_addr *origin,const struct in6_addr *group',
            '@ADDR_DECL@': '__be32 source=1,group=2;' if version == 4 else
                           'struct in6_addr source={},group={};',
            '@ADDR_ARGS@': 'source,group' if version == 4 else '&source,&group',
            '@EXPECTED_DECL@': 'static __be32 expected_origin,expected_group;' if version == 4 else
                               'static struct in6_addr expected_origin,expected_group;',
            '@ADDRESS_CHECK@': 'assert(origin==expected_origin && group==expected_group)' if version == 4 else
                               'assert(!memcmp(origin,&expected_origin,sizeof(*origin)) && !memcmp(group,&expected_group,sizeof(*group)))',
            '@ADDRESS_INIT@': 'expected_origin=cache.mfc_origin=source;expected_group=cache.mfc_mcastgrp=group;' if version == 4 else
                              'source.s6_addr[15]=1;group.s6_addr[15]=2;expected_origin=cache.mf6c_origin=source;expected_group=cache.mf6c_mcastgrp=group;',

        }
        mock, checks = MOCK, CHECKS
        for name, value in replacements.items():
            mock, checks = mock.replace(name, value), checks.replace(name, value)
        source = tmp / f'check{version}.c'
        source.write_text(mock + helpers + checks)
        exe = tmp / f'check{version}'
        subprocess.run(['cc', '-std=gnu11', '-O2', '-Wall', '-Werror',
                        str(source), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
