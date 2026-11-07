#!/usr/bin/env python3
"""Compile and run actual NSS lifecycle C against a fake kernel surface.

Extracts nss_init/nss_cleanup/nss_common_cleanup, coredump, stats and
nss_hal_remove from --source-dir, then links them with the fakes below.
Run against logs/qinq-input/after to pass and logs/qinq-input/before to fail.
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

FUNCS = {
    "nss_init.c": ["nss_common_cleanup", "nss_init", "nss_cleanup"],
    "nss_coredump.c": ["nss_coredump_wait", "nss_coredump_init_delay_work",
                       "nss_coredump_notify_register",
                       "nss_coredump_notify_unregister",
                       "nss_coredump_destroy_delay_work"],
    "nss_stats.c": ["nss_stats_register_sysctl", "nss_stats_unregister_sysctl",
                    "nss_stats_clean"],
    "nss_hal/nss_hal.c": ["nss_hal_remove"],
}

HEADER = r"""
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#define KERNEL_VERSION(a, b, c) (((a) << 16) | ((b) << 8) | (c))
#define LINUX_VERSION_CODE KERNEL_VERSION(6, 6, 0)
#define NSS_DT_SUPPORT 1
#define NSS_HAL_IPQ50XX_SUPPORT 1
#define NSS_DATA_PLANE_GENERIC_SUPPORT 1
#define NSS_DRV_IPV4_ENABLE 1
#define NSS_DRV_IPV6_ENABLE 1
#define NSS_FREQ_SCALE_SUPPORT 1
#define NSS_SKB_REUSE_SUPPORT 1
#define NSS_FABRIC_SCALING_SUPPORT 0
#define NSS_HAL_SUPPORTED_INTERRUPTS 0xffffffffu
#define NSS_CORE_STATE_UNINITIALIZED 0
#define NSS_REGS_RESET_CTRL_OFFSET 0
#define ETH_DATA_LEN 1500
#define __init
#define __exit
#define nss_info(...) do { } while (0)
#define nss_info_always(...) do { } while (0)
#define nss_warning(...) do { } while (0)
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x, v) ((x) = (v))
#define wmb() do { } while (0)
#define rtnl_lock() do { } while (0)
#define rtnl_unlock() do { } while (0)
#define spin_lock_init(x) do { } while (0)
#define mutex_init(x) do { } while (0)
#define INIT_WORK(...) do { } while (0)
#define INIT_DELAYED_WORK(...) do { } while (0)
#define mdelay(x) do { } while (0)
#define likely(x) (x)
#define unlikely(x) (x)
#define nss_hal_disable_interrupt(a, b, c) do { } while (0)
#define panic(...) fake_panic()

struct ctl_table { int unused; };
struct ctl_table_header { int active; };
struct work_struct { int unused; };
struct delayed_work { struct work_struct work; int pending; };
struct workqueue_struct {
	const char *name;
	int pending;
	int destroyed;
	int destroyed_pending;
	int kind;
};
struct dentry { int unused; };
struct net_device { int unused; };
struct device_node { int unused; };
struct device { struct device_node *of_node; };
struct platform_device { uint32_t id; struct device dev; };
struct platform_driver { int unused; };
struct nss_hal_ops { int unused; };
struct nss_data_plane_ops { void (*data_plane_unregister)(void); };
struct notifier_block { int unused; };
struct fake_chain { struct notifier_block *nb; };
struct int_ctx_instance { uint32_t shift_factor; uint32_t irq; };
struct nss_ctx_instance {
	uint32_t id;
	int state;
	void *nmap;
	void *vmap;
	void *qgic_map;
	uint32_t num_irq;
	struct net_device *napi_ndev;
	struct int_ctx_instance int_ctx[8];
};
struct nss_scale_info { uint32_t frequency, minimum, maximum; };
struct nss_runtime_sampling {
	struct nss_scale_info freq_scale[3];
	int freq_scale_index;
	uint32_t freq_scale_ready;
	uint32_t freq_scale_rate_limit_down;
	uint32_t buffer_index;
	uint32_t sum;
	uint32_t sample_count;
	uint32_t average;
	uint32_t message_rate_limit;
	uint32_t initialized;
};
struct nss_cmd_buffer {
	uint32_t current_freq;
	int32_t auto_scale;
	int32_t max_freq;
	uint32_t average_inst;
	uint32_t coredump;
};
struct nss_top_instance {
	uint8_t num_nss;
	struct nss_ctx_instance nss[2];
	bool nss_hal_common_init_done;
	uint16_t prev_mtu_sz;
	struct nss_hal_ops *hal_ops;
	struct nss_data_plane_ops *data_plane_ops;
	struct dentry *top_dentry;
	int lock;
	int stats_lock;
	int wq_lock;
};

static void nss_common_cleanup(void);
static int nss_init(void);
static void nss_cleanup(void);
static void nss_coredump_wait(struct work_struct *work);
int nss_coredump_init_delay_work(void);
void nss_coredump_notify_register(void);
void nss_coredump_notify_unregister(void);
void nss_coredump_destroy_delay_work(void);
void nss_stats_register_sysctl(void);
void nss_stats_unregister_sysctl(void);
void nss_stats_clean(void);
int nss_hal_remove(struct platform_device *nss_dev);
static void fake_panic(void);
static int queue_work(struct workqueue_struct *wq, struct work_struct *work);

struct nss_top_instance nss_top_main;
struct nss_cmd_buffer nss_cmd_buf;
struct nss_runtime_sampling nss_runtime_samples;
struct workqueue_struct *nss_wq;
struct nss_hal_ops nss_hal_ipq50xx_ops;
struct nss_data_plane_ops nss_data_plane_ops;
struct platform_driver nss_driver;
static struct ctl_table_header *nss_dev_header;
static struct ctl_table_header *nss_clk_hdr;
static struct ctl_table_header *nss_gen_hdr;
static struct ctl_table_header *nss_skb_hdr;
static struct ctl_table_header *nss_stats_header;
static struct workqueue_struct *coredump_workqueue;
static struct delayed_work coredump_queuewait;
static struct notifier_block nss_panic_nb;
static bool nss_panic_registered;
static bool coredump_stopping;
static bool nss_ready;
static bool nss_platform_registered;
static struct ctl_table nss_freq_table[1];
static struct ctl_table nss_general_table[1];
static struct ctl_table nss_skb_reuse_table[1];
static struct ctl_table nss_stats_table[1];
static struct ctl_table nss_stats_root[1];

#define MAX_TRACE 256
static char trace[MAX_TRACE][40];
static int trace_n;
static int failures;
static struct work_struct freq_work;
static struct workqueue_struct freq_q_obj;
static struct workqueue_struct coredump_q_obj;
static struct workqueue_struct dp_q_obj;
static int model_irqs_left;
static int irq_stopped;
static int producer_running;
static int hw_live;
static int conn_free_while_live;
static int fail_create_wq;
static int fail_coredump_wq;
static int fail_platform_register;
static int symbol_only;
static int skip_probe;
static int fake_probe_done;
static int queue_coredump_during_unreg;
static int fire_coredump_during_unreg;
static int platform_register_calls;
static int platform_unregister_calls;
static int fake_panic_calls;
static int fake_panic_count;
static int panic_register_calls;
static int panic_unregister_calls;
static int panic_duplicate_calls;
static int sysctl_register_calls;
static int sysctl_unregister_calls;
static int misc_unregister_calls;
static int debugfs_remove_calls;
static struct notifier_block *fake_panic_nb;
static struct fake_chain panic_notifier_list;
static struct ctl_table_header fake_stats_hdr;
static struct ctl_table_header fake_hdr_pool[8];
static int fake_hdr_next;
static struct dentry fake_dentry;
static struct device_node fake_node;
static struct device_node fake_cmn_node;
static struct platform_device fake_pdev;
static int fake_io;
static struct net_device fake_netdev;

#define CHECK(c, name) do { \
	if (c) printf("PASS %s\n", name); \
	else { printf("FAIL %s\n", name); failures++; } \
} while (0)

static void ev(const char *s)
{
	if (trace_n < MAX_TRACE) {
		snprintf(trace[trace_n], sizeof(trace[0]), "%s", s);
		trace_n++;
	}
}

static int trace_index_from(int start, const char *s)
{
	int i;
	for (i = start; i < trace_n; i++)
		if (!strcmp(trace[i], s)) return i;
	return -1;
}

static int trace_last_from(int start, const char *s)
{
	int i, found = -1;
	for (i = start; i < trace_n; i++)
		if (!strcmp(trace[i], s)) found = i;
	return found;
}

static int trace_count_from(int start, const char *s)
{
	int i, n = 0;
	for (i = start; i < trace_n; i++)
		if (!strcmp(trace[i], s)) n++;
	return n;
}

static int trace_has_from(int start, const char *s)
{
	return trace_index_from(start, s) >= 0;
}

static void fake_panic(void)
{
	fake_panic_calls++;
	ev("panic");
}

static void model_tick(void)
{
	if (producer_running && !irq_stopped)
		queue_work(nss_wq, &freq_work);
}

static int queue_work(struct workqueue_struct *wq, struct work_struct *work)
{
	(void)work;
	if (!wq) { ev("queue_null"); return 0; }
	if (wq->destroyed) { ev("queue_after_destroy"); return 0; }
	wq->pending++;
	ev("freq_queue");
	return 1;
}

static void flush_workqueue(struct workqueue_struct *wq)
{
	if (!wq) { ev("flush_null"); return; }
	ev("freq_flush");
	wq->pending = 0;
}

static void destroy_workqueue(struct workqueue_struct *wq)
{
	if (!wq) { ev("destroy_null"); return; }
	if (wq->destroyed) { ev("destroy_twice"); return; }
	if (wq->pending) {
		wq->destroyed_pending = 1;
		ev("destroy_pending");
	}
	wq->destroyed = 1;
	if (wq->kind == 1) ev("freq_destroy");
	else if (wq->kind == 2) ev("coredump_destroy");
	else ev("dp_destroy");
}

static struct workqueue_struct *create_workqueue(const char *name)
{
	if (!strcmp(name, "nss_freq_queue")) {
		if (fail_create_wq) { ev("freq_create_fail"); return NULL; }
		memset(&freq_q_obj, 0, sizeof(freq_q_obj));
		freq_q_obj.name = name;
		freq_q_obj.kind = 1;
		ev("freq_create");
		return &freq_q_obj;
	}
	return NULL;
}

static struct workqueue_struct *create_singlethread_workqueue(const char *name)
{
	if (!strcmp(name, "coredump_wait")) {
		if (fail_coredump_wq) { ev("coredump_create_fail"); return NULL; }
		memset(&coredump_q_obj, 0, sizeof(coredump_q_obj));
		coredump_q_obj.name = name;
		coredump_q_obj.kind = 2;
		ev("coredump_create");
		return &coredump_q_obj;
	}
	return NULL;
}

static int queue_delayed_work(struct workqueue_struct *wq,
			      struct delayed_work *dw, unsigned long delay)
{
	(void)delay;
	if (!wq || wq->destroyed) { ev("coredump_queue_after_destroy"); return 0; }
	wq->pending++;
	dw->pending = 1;
	ev("coredump_queue");
	return 1;
}

static int cancel_delayed_work_sync(struct delayed_work *dw)
{
	ev("coredump_cancel");
	dw->pending = 0;
	if (coredump_workqueue)
		coredump_workqueue->pending = 0;
	return 0;
}

static void nss_hal_clean_up_irq(struct int_ctx_instance *ctx)
{
	(void)ctx;
	ev("irq_cleanup");
	model_tick();
	if (--model_irqs_left <= 0) {
		irq_stopped = 1;
		ev("irq_stopped");
	}
}

static int atomic_notifier_chain_register(struct fake_chain *chain,
					  struct notifier_block *nb)
{
	(void)chain;
	panic_register_calls++;
	if (fake_panic_nb) {
		panic_duplicate_calls++;
		ev("panic_dup");
		return -EBUSY;
	}
	fake_panic_nb = nb;
	fake_panic_count = 1;
	ev("panic_reg");
	return 0;
}

static int atomic_notifier_chain_unregister(struct fake_chain *chain,
					    struct notifier_block *nb)
{
	(void)chain;
	panic_unregister_calls++;
	if (fake_panic_nb != nb) { ev("panic_unreg_miss"); return -ENOENT; }
	fake_panic_nb = NULL;
	fake_panic_count = 0;
	ev("panic_unreg");
	return 0;
}

static struct ctl_table_header *register_sysctl(const char *path,
						struct ctl_table *table)
{
	struct ctl_table_header *h;
	(void)table;
	sysctl_register_calls++;
	ev("sysctl_reg");
	if (!strcmp(path, "dev/nss/stats")) h = &fake_stats_hdr;
	else h = &fake_hdr_pool[fake_hdr_next++ & 7];
	h->active = 1;
	return h;
}

static void unregister_sysctl_table(struct ctl_table_header *h)
{
	if (!h) { ev("sysctl_unreg_null"); return; }
	if (!h->active) { ev("sysctl_double_unreg"); return; }
	h->active = 0;
	sysctl_unregister_calls++;
	ev(h == &fake_stats_hdr ? "stats_unreg" : "sysctl_unreg");
}

static void nss_n2h_single_core_register_sysctl(void) {}
static void nss_n2h_multi_core_register_sysctl(void) {}
static void nss_rps_register_sysctl(void) {}
static void nss_project_register_sysctl(void) {}
static void nss_ipv4_register_sysctl(void) {}
static void nss_ipv6_register_sysctl(void) {}
static void nss_n2h_unregister_sysctl(void) { misc_unregister_calls++; ev("n2h_unreg"); }
static void nss_rps_unregister_sysctl(void) { misc_unregister_calls++; ev("rps_unreg"); }
static void nss_project_unregister_sysctl(void) { misc_unregister_calls++; ev("project_unreg"); }
static void nss_ipv4_unregister_sysctl(void) { ev("ipv4_unreg"); }
static void nss_ipv6_unregister_sysctl(void) { ev("ipv6_unreg"); }
static void nss_ipv4_free_conn_tables(void)
{
	if (hw_live) conn_free_while_live++;
	ev("ipv4_free");
}
static void nss_ipv6_free_conn_tables(void)
{
	if (hw_live) conn_free_while_live++;
	ev("ipv6_free");
}

static void nss_stats_init(void)
{
	nss_top_main.top_dentry = &fake_dentry;
	ev("stats_init");
}
static void nss_strings_init(void) {}
static void debugfs_remove_recursive(struct dentry *d)
{
	(void)d;
	debugfs_remove_calls++;
	ev("debugfs_remove");
}

static int nss_data_plane_init_delay_work(void)
{
	memset(&dp_q_obj, 0, sizeof(dp_q_obj));
	dp_q_obj.name = "nss_data_plane_workqueue";
	dp_q_obj.kind = 3;
	ev("dp_create");
	return 0;
}
static void nss_data_plane_destroy_delay_work(void)
{
	destroy_workqueue(&dp_q_obj);
}

static struct device_node *of_find_node_by_name(struct device_node *from,
						const char *name)
{
	(void)from; (void)name;
	return symbol_only ? NULL : &fake_cmn_node;
}
static void of_node_put(struct device_node *np) { (void)np; }
static int of_machine_is_compatible(const char *compat)
{
	return !strcmp(compat, "qcom,ipq5018");
}
static void iounmap(void *addr) { (void)addr; ev("iounmap"); }
static void free_netdev(struct net_device *ndev) { (void)ndev; ev("free_netdev"); }
static void nss_write_32(void *addr, uint32_t off, uint32_t val)
{
	(void)addr; (void)off; (void)val;
}

static void fake_data_plane_unregister(void)
{
	hw_live = 0;
	ev("hw_unmap");
}

static void fake_probe(void)
{
	struct nss_ctx_instance *ctx = &nss_top_main.nss[0];
	ctx->id = 0;
	ctx->state = 1;
	ctx->nmap = &fake_io;
	ctx->vmap = &fake_io;
	ctx->qgic_map = &fake_io;
	ctx->napi_ndev = &fake_netdev;
	ctx->num_irq = 2;
	ctx->int_ctx[0].shift_factor = 0;
	ctx->int_ctx[1].shift_factor = 1;
	nss_top_main.data_plane_ops = &nss_data_plane_ops;
	model_irqs_left = 2;
	irq_stopped = 0;
	producer_running = 1;
	hw_live = 1;
	fake_probe_done = 1;
	ev("probe");
}

static int platform_driver_register(struct platform_driver *drv)
{
	(void)drv;
	platform_register_calls++;
	ev("platform_reg");
	if (fail_platform_register) {
		ev("platform_reg_fail");
		return -EBUSY;
	}
	if (!skip_probe)
		fake_probe();
	return 0;
}

static void platform_driver_unregister(struct platform_driver *drv)
{
	(void)drv;
	platform_unregister_calls++;
	ev("platform_unreg");
	if (fake_probe_done) {
		fake_probe_done = 0;
		nss_hal_remove(&fake_pdev);
	}
	if (queue_coredump_during_unreg) {
		queue_coredump_during_unreg = 0;
		if (coredump_workqueue)
			queue_delayed_work(coredump_workqueue,
					   &coredump_queuewait, 1);
		ev("coredump_race_queue");
	}
	if (fire_coredump_during_unreg) {
		fire_coredump_during_unreg = 0;
		nss_coredump_wait(&coredump_queuewait.work);
	}
}

static void fake_queue_coredump(void)
{
	if (coredump_workqueue)
		queue_delayed_work(coredump_workqueue, &coredump_queuewait, 1);
}

static void reset_all(void)
{
	memset(&nss_top_main, 0, sizeof(nss_top_main));
	memset(&nss_cmd_buf, 0, sizeof(nss_cmd_buf));
	memset(&nss_runtime_samples, 0, sizeof(nss_runtime_samples));
	nss_wq = NULL;
	nss_dev_header = nss_clk_hdr = nss_gen_hdr = nss_skb_hdr = NULL;
	nss_stats_header = NULL;
	coredump_workqueue = NULL;
	memset(&coredump_queuewait, 0, sizeof(coredump_queuewait));
	nss_panic_registered = false;
	coredump_stopping = false;
	nss_ready = false;
	nss_platform_registered = false;
	memset(&freq_q_obj, 0, sizeof(freq_q_obj));
	memset(&coredump_q_obj, 0, sizeof(coredump_q_obj));
	memset(&dp_q_obj, 0, sizeof(dp_q_obj));
	memset(&fake_hdr_pool, 0, sizeof(fake_hdr_pool));
	memset(&fake_stats_hdr, 0, sizeof(fake_stats_hdr));
	fake_hdr_next = 0;
	fake_panic_nb = NULL;
	fake_panic_count = 0;
	fake_panic_calls = 0;
	panic_register_calls = panic_unregister_calls = panic_duplicate_calls = 0;
	sysctl_register_calls = sysctl_unregister_calls = 0;
	misc_unregister_calls = 0;
	debugfs_remove_calls = 0;
	platform_register_calls = platform_unregister_calls = 0;
	fail_create_wq = fail_coredump_wq = fail_platform_register = 0;
	symbol_only = skip_probe = 0;
	queue_coredump_during_unreg = fire_coredump_during_unreg = 0;
	fake_probe_done = 0;
	producer_running = 0;
	irq_stopped = 0;
	model_irqs_left = 0;
	hw_live = 0;
	conn_free_while_live = 0;
	memset(&fake_pdev, 0, sizeof(fake_pdev));
	fake_pdev.id = 0;
	fake_pdev.dev.of_node = &fake_node;
	nss_data_plane_ops.data_plane_unregister = fake_data_plane_unregister;
	trace_n = 0;
}
"""

MAIN = r"""
static void scenario_repeat(void)
{
	int iter;
	reset_all();
	for (iter = 0; iter < 2; iter++) {
		int mark;
		int r = nss_init();
		CHECK(r == 0, "normal init succeeds");
		CHECK(nss_wq == &freq_q_obj && !freq_q_obj.destroyed,
		      "freq queue live after init");
		CHECK(coredump_workqueue == &coredump_q_obj &&
		      !coredump_q_obj.destroyed, "coredump queue live after init");
		CHECK(fake_panic_count == 1 && panic_register_calls == iter + 1,
		      "panic handler registered once");
		CHECK(panic_duplicate_calls == 0, "no duplicate panic registration");
		CHECK(nss_stats_header == &fake_stats_hdr,
		      "stats sysctl registered");
		CHECK(nss_top_main.top_dentry != NULL, "debugfs registered");
		CHECK(hw_live == 1, "hardware live after probe");

		fake_queue_coredump();
		queue_coredump_during_unreg = 1;
		fire_coredump_during_unreg = 1;
		producer_running = 1;
		irq_stopped = 0;
		model_tick();

		mark = trace_n;
		nss_cleanup();

		CHECK(nss_wq == NULL && freq_q_obj.destroyed &&
		      !freq_q_obj.destroyed_pending,
		      "freq queue drained then destroyed");
		CHECK(coredump_workqueue == NULL && coredump_q_obj.destroyed &&
		      !coredump_q_obj.destroyed_pending,
		      "coredump queue cancelled then destroyed");
		CHECK(coredump_queuewait.pending == 0,
		      "coredump timer not pending after exit");
		CHECK(fake_panic_count == 0 &&
		      panic_unregister_calls == iter + 1,
		      "panic handler removed");
		CHECK(nss_stats_header == NULL, "stats sysctl removed");
		CHECK(!nss_ready && !nss_platform_registered,
		      "ownership flags cleared");
		CHECK(fake_panic_calls == 0,
		      "stopping timer did not panic during platform removal");
		CHECK(conn_free_while_live == 0,
		      "conn tables not freed while hardware live");
		CHECK(trace_index_from(mark, "ipv4_free") >
		      trace_index_from(mark, "hw_unmap"),
		      "conn free after hardware unmap");
		CHECK(trace_index_from(mark, "ipv4_free") >
		      trace_index_from(mark, "platform_unreg"),
		      "conn free after platform unregister");
		CHECK(trace_index_from(mark, "freq_flush") >
		      trace_index_from(mark, "irq_stopped"),
		      "freq flush after irq stop");
		CHECK(trace_index_from(mark, "hw_unmap") >
		      trace_index_from(mark, "freq_flush"),
		      "freq flush before hardware unmap");
		CHECK(trace_last_from(mark, "coredump_cancel") >
		      trace_index_from(mark, "coredump_race_queue"),
		      "final cancel covers racing coredump queue");
		CHECK(!trace_has_from(mark, "destroy_pending") &&
		      !trace_has_from(mark, "sysctl_double_unreg") &&
		      !trace_has_from(mark, "queue_after_destroy") &&
		      !trace_has_from(mark, "panic_unreg_miss"),
		      "no teardown ordering errors");
	}
	CHECK(platform_register_calls == 2 && platform_unregister_calls == 2,
	      "platform registered and unregistered twice");
}

static void scenario_guards(void)
{
	int mark;
	int r;
	reset_all();
	mark = trace_n;
	nss_stats_unregister_sysctl();
	nss_coredump_notify_unregister();
	CHECK(trace_n == mark, "guarded unregister before init is no-op");
	CHECK(panic_unregister_calls == 0 && sysctl_unregister_calls == 0,
	      "no unregister calls before init");

	r = nss_init();
	CHECK(r == 0, "guard scenario init succeeds");
	nss_stats_unregister_sysctl();
	nss_stats_unregister_sysctl();
	CHECK(trace_count_from(mark, "stats_unreg") == 1,
	      "stats header unregister is idempotent");
	CHECK(nss_stats_header == NULL, "stats header pointer cleared");
	nss_coredump_notify_unregister();
	nss_coredump_notify_unregister();
	CHECK(panic_unregister_calls == 1 && fake_panic_count == 0,
	      "panic unregister is idempotent");
	nss_cleanup();
	nss_coredump_destroy_delay_work();
	CHECK(!trace_has_from(mark, "sysctl_double_unreg") &&
	      !trace_has_from(mark, "panic_unreg_miss") &&
	      !trace_has_from(mark, "destroy_null"),
	      "guarded paths do not double unregister or destroy");
}

static void scenario_empty(void)
{
	int mark;
	reset_all();
	mark = trace_n;
	nss_cleanup();
	CHECK(trace_n == mark && !nss_ready, "exit before init is a no-op");
}

static void scenario_symbol_only(void)
{
	int mark;
	int r;
	reset_all();
	symbol_only = 1;
	mark = trace_n;
	r = nss_init();
	CHECK(r == 0, "symbol-only init succeeds");
	CHECK(nss_wq == NULL && coredump_workqueue == NULL,
	      "symbol-only init creates no queues");
	CHECK(trace_n == mark, "symbol-only init has no resource side effects");
	nss_cleanup();
	CHECK(trace_n == mark, "symbol-only exit is a no-op");
	CHECK(sysctl_unregister_calls == 0 && platform_unregister_calls == 0 &&
	      debugfs_remove_calls == 0,
	      "symbol-only exit touched no resources");
}

static void scenario_fail_wq(void)
{
	int mark;
	int r;
	reset_all();
	fail_create_wq = 1;
	r = nss_init();
	CHECK(r != 0, "freq queue allocation failure fails init");
	CHECK(nss_wq == NULL && !nss_ready,
	      "freq queue failure unwinds ownership");
	CHECK(panic_register_calls == 0 && fake_panic_count == 0,
	      "panic handler not published on queue failure");
	CHECK(nss_stats_header == NULL && sysctl_unregister_calls > 0,
	      "sysctls unwound on queue failure");
	CHECK(dp_q_obj.destroyed, "dp workqueue unwound on queue failure");
	CHECK(debugfs_remove_calls >= 1, "debugfs unwound on queue failure");
	CHECK(platform_register_calls == 0,
	      "platform not registered on queue failure");
	mark = trace_n;
	nss_cleanup();
	CHECK(trace_n == mark, "cleanup after failed init is a no-op");
}

static void scenario_fail_coredump(void)
{
	int r;
	reset_all();
	fail_coredump_wq = 1;
	r = nss_init();
	CHECK(r != 0, "coredump queue allocation failure fails init");
	CHECK(nss_wq == NULL && freq_q_obj.destroyed &&
	      !freq_q_obj.destroyed_pending,
	      "freq queue unwound on coredump failure");
	CHECK(coredump_workqueue == NULL && panic_register_calls == 0 &&
	      fake_panic_count == 0,
	      "panic not published without coredump queue");
	CHECK(nss_stats_header == NULL && dp_q_obj.destroyed &&
	      debugfs_remove_calls >= 1,
	      "common resources unwound on coredump failure");
	CHECK(platform_register_calls == 0,
	      "platform not registered on coredump failure");
}

static void scenario_fail_platform(void)
{
	int r;
	reset_all();
	fail_platform_register = 1;
	r = nss_init();
	CHECK(r != 0, "platform register failure fails init");
	CHECK(platform_unregister_calls == 0,
	      "failed platform register not unregistered");
	CHECK(fake_panic_count == 0 && panic_unregister_calls == 1,
	      "panic handler removed on platform failure");
	CHECK(coredump_workqueue == NULL && coredump_q_obj.destroyed,
	      "coredump queue destroyed on platform failure");
	CHECK(nss_wq == NULL && freq_q_obj.destroyed,
	      "freq queue destroyed on platform failure");
	CHECK(conn_free_while_live == 0,
	      "conn tables freed only with hardware stopped");
	CHECK(nss_stats_header == NULL && debugfs_remove_calls >= 1,
	      "stats and debugfs unwound on platform failure");

	fail_platform_register = 0;
	r = nss_init();
	CHECK(r == 0 && panic_duplicate_calls == 0 && fake_panic_count == 1,
	      "retry registers without a dangling panic entry");
	nss_cleanup();
	CHECK(fake_panic_count == 0, "retry cleanup removes panic entry");
}

static void scenario_no_probe(void)
{
	int r;
	reset_all();
	skip_probe = 1;
	r = nss_init();
	CHECK(r == 0 && nss_top_main.top_dentry != NULL,
	      "no-probe init registers debugfs");
	nss_cleanup();
	CHECK(nss_top_main.top_dentry == NULL && debugfs_remove_calls >= 1,
	      "debugfs cleaned when no core probed");
	CHECK(freq_q_obj.destroyed && !freq_q_obj.destroyed_pending,
	      "freq queue destroyed when no core probed");
	CHECK(coredump_q_obj.destroyed,
	      "coredump queue destroyed when no core probed");
}

int main(void)
{
	int i;
	for (i = 0; i < missing_count; i++) {
		printf("FAIL missing function: %s\n", missing_names[i]);
		failures++;
	}
	scenario_repeat();
	scenario_guards();
	scenario_empty();
	scenario_symbol_only();
	scenario_fail_wq();
	scenario_fail_coredump();
	scenario_fail_platform();
	scenario_no_probe();
	if (failures)
		printf("RESULT FAIL (%d)\n", failures);
	else
		printf("RESULT PASS\n");
	return failures ? 1 : 0;
}
"""

STUBS = {
    "nss_common_cleanup": "static void nss_common_cleanup(void) { missing(\"nss_common_cleanup\"); }",
    "nss_init": "static int nss_init(void) { missing(\"nss_init\"); return -EINVAL; }",
    "nss_cleanup": "static void nss_cleanup(void) { missing(\"nss_cleanup\"); }",
    "nss_coredump_wait": "static void nss_coredump_wait(struct work_struct *w) { (void)w; missing(\"nss_coredump_wait\"); }",
    "nss_coredump_init_delay_work": "int nss_coredump_init_delay_work(void) { missing(\"nss_coredump_init_delay_work\"); return -EINVAL; }",
    "nss_coredump_notify_register": "void nss_coredump_notify_register(void) { missing(\"nss_coredump_notify_register\"); }",
    "nss_coredump_notify_unregister": "void nss_coredump_notify_unregister(void) { missing(\"nss_coredump_notify_unregister\"); }",
    "nss_coredump_destroy_delay_work": "void nss_coredump_destroy_delay_work(void) { missing(\"nss_coredump_destroy_delay_work\"); }",
    "nss_stats_register_sysctl": "void nss_stats_register_sysctl(void) { missing(\"nss_stats_register_sysctl\"); }",
    "nss_stats_unregister_sysctl": "void nss_stats_unregister_sysctl(void) { missing(\"nss_stats_unregister_sysctl\"); }",
    "nss_stats_clean": "void nss_stats_clean(void) { missing(\"nss_stats_clean\"); }",
    "nss_hal_remove": "int nss_hal_remove(struct platform_device *d) { (void)d; missing(\"nss_hal_remove\"); return 0; }",
}


def extract(text, name):
    pattern = re.compile(
        r"^(?:static\s+)?(?:int|void|bool)\s+(?:__init\s+|__exit\s+)?"
        + re.escape(name) + r"\s*\(")
    lines = text.splitlines()
    for start, line in enumerate(lines):
        if not pattern.match(line):
            continue
        body = []
        for idx in range(start, len(lines)):
            body.append(lines[idx])
            if idx > start and lines[idx].rstrip() == "}":
                return "\n".join(body)
        return None
    return None


def build_c(source_dir):
    found, missing = {}, []
    for rel, names in FUNCS.items():
        path = source_dir / rel
        if not path.exists():
            missing.extend(names)
            continue
        text = path.read_text()
        for name in names:
            body = extract(text, name)
            if body is None:
                missing.append(name)
            else:
                found[name] = body

    parts = [HEADER]
    names = ", ".join('"%s"' % n for n in missing) or "0"
    parts.append("static const char *missing_names[] = { %s };\n" % names)
    parts.append("static int missing_count = %d;\n" % len(missing))
    parts.append('static void missing(const char *n) { ev(n); }\n')
    for name in missing:
        parts.append(STUBS.get(name, 'static void stub_%s(void) {}\n' % name))
    for name in [n for group in FUNCS.values() for n in group
                 if n in found]:
        parts.append(found[name])
    parts.append(MAIN)
    return "\n".join(parts), missing


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", type=Path,
                        default=Path(__file__).resolve().parents[2]
                        / "build_dir/target-aarch64_cortex-a53_musl/linux-qualcommax_ipq50xx"
                        / "qca-nss-drv-12.5.2024.11.13~d5ee67b")
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--no-sanitize", action="store_true")
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()

    code, missing = build_c(args.source_dir)
    if missing:
        print("MISSING: " + ", ".join(missing))
    with tempfile.TemporaryDirectory() as td:
        cpath = Path(td) / "lifecycle.c"
        bpath = Path(td) / "lifecycle"
        cpath.write_text(code)
        cmd = [args.cc, "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra",
               "-Wno-unused-parameter", "-Wno-unused-variable",
               "-Wno-unused-function", "-Wno-unused-but-set-variable",
               "-Wno-missing-field-initializers",
               "-Werror=implicit-function-declaration",
               "-Werror=incompatible-pointer-types"]
        if not args.no_sanitize:
            cmd += ["-fsanitize=address,undefined",
                    "-fno-omit-frame-pointer", "-fno-sanitize-recover=all"]
        cmd += [str(cpath), "-o", str(bpath)]
        built = subprocess.run(cmd, text=True, capture_output=True)
        if built.returncode:
            sys.stderr.write(built.stdout)
            sys.stderr.write(built.stderr)
            print("RESULT FAIL (compile)")
            return 1
        environment = dict(os.environ)
        # LeakSanitizer cannot run under the restricted runner's ptrace.
        # These fakes use static objects; ASan and UBSan remain enabled.
        environment["ASAN_OPTIONS"] = environment.get("ASAN_OPTIONS", "") + ":detect_leaks=0"
        run = subprocess.run([str(bpath)], text=True, capture_output=True,
                             env=environment)
        sys.stdout.write(run.stdout)
        if run.stderr:
            sys.stderr.write(run.stderr)
        if args.keep:
            Path("lifecycle.c").write_text(code)
            print("kept lifecycle.c")
        return run.returncode or (1 if missing else 0)


if __name__ == "__main__":
    sys.exit(main())
