#!/usr/bin/env python3
"""Run the patched real DSA VLAN-add callback through its cleanup branches."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
KERNEL = ROOT / 'build_dir/target-aarch64_cortex-a53_musl/linux-qualcommax_ipq50xx/linux-6.18.55'
PATCH = ROOT / 'target/linux/generic/pending-6.18/764-net-dsa-undo-user-VLAN-on-host-setup-failure.patch'


def function(text):
    start = text.index('static int dsa_user_vlan_rx_add_vid(')
    end = text.index('static int dsa_user_vlan_rx_kill_vid(', start)
    return text[start:end]


MOCK = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
typedef uint16_t u16;
typedef uint16_t __be16;
#define SWITCHDEV_OBJ_ID_PORT_VLAN 1
#define GFP_KERNEL 0
#define DSA_MC_ADD 1
#define DSA_UC_ADD 2
struct list_head { int unused; };
struct dsa_switch { int unused; };
struct dsa_port { struct dsa_switch *ds; struct dsa_port *cpu_dp; int index; struct list_head user_vlans; };
struct net_device { int unused; };
struct switchdev_obj_port_vlan { struct { int id; void *orig_dev; } obj; u16 vid, flags; };
struct netlink_ext_ack { const char *_msg; };
struct netdev_hw_addr { unsigned char addr[6]; };
struct dsa_vlan { u16 vid; struct list_head list; };
static struct dsa_switch ds;
static struct dsa_port cpu = {.index = 8}, dp = {.ds = &ds, .cpu_dp = &cpu};
static struct dsa_vlan entry;
static int user_err, host_err, delete_err, alloc_err, alloc_calls, flushes, lists;
static bool filtering;
static char events[16];
static void event(char c) { size_t n = strlen(events); assert(n + 1 < sizeof(events)); events[n] = c; events[n + 1] = 0; }
static struct dsa_port *dsa_user_to_port(struct net_device *d) { return &dp; }
static int dsa_port_vlan_add(struct dsa_port *p, struct switchdev_obj_port_vlan *v, struct netlink_ext_ack *e) { assert(p == &dp && v->vid == 35 && v->flags == 0 && !v->obj.orig_dev); event('U'); e->_msg = user_err ? "user failure" : NULL; return user_err; }
static int dsa_port_host_vlan_add(struct dsa_port *p, struct switchdev_obj_port_vlan *v, struct netlink_ext_ack *e) { event('H'); e->_msg = host_err ? "host failure" : NULL; return host_err; }
static int dsa_port_vlan_del(struct dsa_port *p, struct switchdev_obj_port_vlan *v) { event('D'); return delete_err; }
static int dsa_port_host_vlan_del(struct dsa_port *p, struct switchdev_obj_port_vlan *v) { event('R'); return 0; }
static bool dsa_switch_supports_uc_filtering(struct dsa_switch *s) { return filtering; }
static bool dsa_switch_supports_mc_filtering(struct dsa_switch *s) { return filtering; }
static void *kzalloc(size_t n, int flags) { alloc_calls++; assert(n == sizeof(entry)); return alloc_err ? NULL : &entry; }
static void dsa_flush_workqueue(void) { flushes++; }
static void list_add_tail(struct list_head *a, struct list_head *b) { lists++; }
#define netdev_err(...) ((void)0)
#define netif_addr_lock_bh(d) ((void)0)
#define netif_addr_unlock_bh(d) ((void)0)
#define netdev_for_each_synced_mc_addr(h, d) for ((h) = NULL; (h); (h) = NULL)
#define netdev_for_each_synced_uc_addr(h, d) for ((h) = NULL; (h); (h) = NULL)
#define dsa_user_schedule_standalone_work(...) ((void)0)
"""

TEST = r"""
static void reset(void) { user_err = host_err = delete_err = alloc_err = alloc_calls = flushes = lists = 0; filtering = false; events[0] = 0; }
int main(void) {
 struct net_device dev;
 reset(); user_err = -EIO;
 assert(dsa_user_vlan_rx_add_vid(&dev, 0x8100, 35) == -EIO && !strcmp(events, "U") && !alloc_calls);
 reset(); host_err = -ENOMEM;
 assert(dsa_user_vlan_rx_add_vid(&dev, 0x8100, 35) == -ENOMEM && !strcmp(events, "UHD") && !alloc_calls);
 reset(); host_err = -EIO; delete_err = -EINVAL;
 assert(dsa_user_vlan_rx_add_vid(&dev, 0x8100, 35) == -EIO && !strcmp(events, "UHD"));
 reset(); host_err = -EOPNOTSUPP;
 assert(dsa_user_vlan_rx_add_vid(&dev, 0x8100, 35) == -EOPNOTSUPP && !strcmp(events, "UHD"));
 reset();
 assert(dsa_user_vlan_rx_add_vid(&dev, 0x8100, 35) == 0 && !strcmp(events, "UH") && !alloc_calls);
 reset(); filtering = true; alloc_err = 1;
 assert(dsa_user_vlan_rx_add_vid(&dev, 0x8100, 35) == -ENOMEM && !strcmp(events, "UHRD") && alloc_calls == 1 && !lists && !flushes);
 reset(); filtering = true;
 assert(dsa_user_vlan_rx_add_vid(&dev, 0x8100, 35) == 0 && !strcmp(events, "UH") && alloc_calls == 1 && lists == 1 && flushes == 1 && entry.vid == 35);
 puts("PASS actual DSA callback: user/host/allocation failures unwind correctly; success preserved");
}
"""

with tempfile.TemporaryDirectory(prefix='dsa-vlan-rollback-') as d:
    tmp = Path(d)
    target = tmp / 'net/dsa/user.c'
    target.parent.mkdir(parents=True)
    source = (KERNEL / 'net/dsa/user.c').read_text()
    target.write_text(source)
    if 'goto rollback_user;' not in function(source):
        subprocess.run(['patch', '-s', '-p1', '-i', str(PATCH)], cwd=tmp, check=True)
    source = target.read_text()
    (tmp / 'test.c').write_text(MOCK + function(source) + TEST)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', str(tmp / 'test.c'), '-o', str(tmp / 'test')], check=True)
    subprocess.run([str(tmp / 'test')], check=True)
