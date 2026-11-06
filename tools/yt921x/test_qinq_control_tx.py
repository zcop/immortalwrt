#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Execute the actual yt921x QinQ control TX helpers against fake kernel APIs.

The C snippets are extracted from tag_yt921x_qinq.c, not copied here.  The
generated harness supplies only kernel-side fakes (skb storage, VLAN helpers,
DSA port lookups) and checks the resulting wire bytes and error ownership.
"""

import os
import re
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

SOURCE = (Path(__file__).resolve().parents[2] /
          "target/linux/generic/files/net/dsa/tag_yt921x_qinq.c")

FUNCTIONS = (
    "yt921x_qinq_is_vlan_tpid",
    "yt921x_qinq_is_control_frame",
    "yt921x_qinq_xmit_control",
    "yt921x_qinq_xmit_control_port",
    "yt921x_qinq_xmit",
)


def _matching_brace(text: str, start: int) -> int:
    depth = 0
    state = None
    i = start
    while i < len(text):
        char = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if state == "line":
            if char == "\n":
                state = None
        elif state == "block":
            if char == "*" and nxt == "/":
                state = None
                i += 2
                continue
        elif state in ("string", "char"):
            if char == "\\":
                i += 2
                continue
            if (state == "string" and char == '"') or \
               (state == "char" and char == "'"):
                state = None
        else:
            if char == "/" and nxt == "/":
                state = "line"
                i += 2
                continue
            if char == "/" and nxt == "*":
                state = "block"
                i += 2
                continue
            if char == '"':
                state = "string"
            elif char == "'":
                state = "char"
            elif char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
                if depth == 0:
                    return i
        i += 1
    raise ValueError("unterminated function body")


def extract_function(source: str, name: str) -> str:
    match = re.search(r"\b" + re.escape(name) + r"\s*\(", source)
    if not match:
        raise ValueError("function not found: " + name)

    start = source.rfind("static", 0, match.start())
    if start < 0:
        raise ValueError("static declaration not found: " + name)

    brace = source.find("{", match.end())
    if brace < 0 or ";" in source[match.end():brace]:
        raise ValueError("function body not found: " + name)

    return source[start:_matching_brace(source, brace) + 1]


def extract_macros(source: str, prefix: str) -> str:
    lines = source.splitlines()
    macros = []
    i = 0
    while i < len(lines):
        if re.match(r"#define\s+" + re.escape(prefix), lines[i]):
            macro = [lines[i]]
            while macro[-1].rstrip().endswith("\\"):
                i += 1
                if i >= len(lines):
                    raise ValueError("unterminated macro")
                macro.append(lines[i])
            macros.append("\n".join(macro))
        i += 1
    return "\n".join(macros)


HARNESS = r"""
#include <arpa/inet.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint16_t __be16;

#define NETIF_F_HW_VLAN_CTAG_FILTER 1U
#define ETH_ALEN 6
#define ETH_HLEN 14
#define VLAN_HLEN 4
#define VLAN_ETH_HLEN 18
#define VLAN_VID_MASK 0x0fff
#define DSA_TAG_8021Q_MAX_NUM_BRIDGES 7
#define VLAN_PRIO_SHIFT 13
#define CHECKSUM_NONE 0
#define CHECKSUM_PARTIAL 3
#define ETH_P_8021Q 0x8100
#define ETH_P_8021AD 0x88a8
#define ETH_P_QINQ1 0x9100
#define ETH_P_QINQ2 0x9200
#define ETH_P_QINQ3 0x9300
#define ETH_P_PAUSE 0x8808
#define ETH_P_YT921X 0x9988

#define BIT(n) (1U << (n))
#define GENMASK(h, l) (((~0U) << (l)) & (~0U >> (31 - (h))))
#define FIELD_PREP(mask, val) \
	(((val) << __builtin_ctz((unsigned int)(mask))) & (mask))
#define unlikely(x) (x)

#define GFP_ATOMIC 0

#define SKB_MAGIC 0x5a5a1234u

@@MACROS@@

struct ethhdr {
	unsigned char h_dest[ETH_ALEN];
	unsigned char h_source[ETH_ALEN];
	__be16 h_proto;
};

struct vlan_ethhdr {
	unsigned char h_dest[ETH_ALEN];
	unsigned char h_source[ETH_ALEN];
	__be16 h_vlan_proto;
	__be16 h_vlan_TCI;
	__be16 h_vlan_encapsulated_proto;
};

struct bridge_vlan_info {
	u16 vid;
};

struct dsa_switch {
	int num_ports;
};

struct net_device {
	int dummy;
	u32 features;
	bool bridge_port, lag_port;
	struct net_device *direct_upper, *direct_real;
	u16 direct_vid;
};

struct dsa_port {
	int index;
	struct dsa_switch *ds;
	struct net_device *user;
	struct net_device *conduit;
	struct dsa_port *cpu_dp;
};

struct sk_buff {
	uint32_t magic;
	struct net_device *dev;
	unsigned char *head;
	unsigned char *data;
	unsigned int len;
	unsigned int priority;
	__be16 vlan_proto;
	u16 vlan_tci;
	__be16 protocol;
	unsigned int ip_summed;
	bool vlan_present;
	unsigned int offload_fwd_mark;
	unsigned int queue_mapping;
};

static struct dsa_switch test_ds;
static struct dsa_port test_dp, test_cpu, test_secondary;
static struct net_device test_netdev;

static bool fake_clone_fail;
static bool fake_cpu_valid;
static int fake_clone_calls, fake_frees;
static struct sk_buff *fake_queued;
static void skb_free(struct sk_buff *skb);
static int fake_checksum_help_result;
static int fake_checksum_help_calls;
static long fake_checksum_data_offset = -1;
static int fake_cow_head_fail_len;
static int fake_cow_head_calls;
static struct net_device *fake_bridge;
static bool fake_br_vlan_enabled;
static int fake_bridge_num;
static u16 fake_bridge_vid;
static u16 fake_standalone_vid;
static u16 fake_pvid;
static int fake_tc;

static void reset_fakes(void)
{
	fake_clone_fail = false;
 fake_cpu_valid = true;
 fake_clone_calls = fake_frees = 0;
 fake_queued = NULL;
 test_ds.num_ports = 11;
 test_cpu.index = 8;
 test_cpu.conduit = &test_netdev;
 test_secondary.index = 4;
	fake_checksum_help_result = 0;
	fake_checksum_help_calls = 0;
	fake_checksum_data_offset = -1;
	fake_cow_head_fail_len = -1;
	fake_cow_head_calls = 0;
	fake_bridge = NULL;
	fake_br_vlan_enabled = false;
	fake_bridge_num = 0;
	fake_bridge_vid = 0;
	fake_standalone_vid = 3004;
	fake_pvid = 0;
	fake_tc = 0;
	memset(&test_netdev, 0, sizeof(test_netdev));
	test_netdev.features = NETIF_F_HW_VLAN_CTAG_FILTER;

	memset(&test_dp, 0, sizeof(test_dp));
	test_dp.index = 0;
	test_dp.ds = &test_ds;
	test_dp.user = &test_netdev;
	test_dp.cpu_dp = &test_cpu;
}

static inline bool is_link_local_ether_addr(const unsigned char *addr)
{
	return addr[0] == 0x01 && addr[1] == 0x80 && addr[2] == 0xc2 &&
	       addr[3] == 0x00 && addr[4] == 0x00 && addr[5] <= 0x0f;
}

static inline bool eth_type_vlan(__be16 proto)
{
	return proto == htons(ETH_P_8021Q) || proto == htons(ETH_P_8021AD);
}

static inline bool skb_vlan_tag_present(const struct sk_buff *skb)
{
	return skb->vlan_present;
}

static inline u16 skb_vlan_tag_get(const struct sk_buff *skb)
{
	return skb->vlan_tci;
}

static inline void __vlan_hwaccel_clear_tag(struct sk_buff *skb)
{
	skb->vlan_present = false;
	skb->vlan_proto = 0;
	skb->vlan_tci = 0;
}

static inline struct ethhdr *eth_hdr(const struct sk_buff *skb)
{
	return (struct ethhdr *)skb->data;
}

static inline struct vlan_ethhdr *skb_vlan_eth_hdr(const struct sk_buff *skb)
{
	return (struct vlan_ethhdr *)skb->data;
}

static inline bool pskb_may_pull(struct sk_buff *skb, unsigned int len)
{
	return skb->len >= len;
}

static inline void *skb_push(struct sk_buff *skb, unsigned int len)
{
	if ((unsigned int)(skb->data - skb->head) < len) {
		fprintf(stderr, "fake skb_push underflow\n");
		abort();
	}
	skb->data -= len;
	skb->len += len;
	return skb->data;
}

static inline int skb_cow_head(struct sk_buff *skb, unsigned int len)
{
	fake_cow_head_calls++;
	if (fake_cow_head_fail_len == (int)len)
		return -ENOMEM;
	if ((unsigned int)(skb->data - skb->head) < len)
		return -ENOMEM;
	return 0;
}

static inline int skb_checksum_help(struct sk_buff *skb)
{
	fake_checksum_help_calls++;
	fake_checksum_data_offset = (long)(skb->data - skb->head);
	return fake_checksum_help_result;
}

static inline int __vlan_insert_tag(struct sk_buff *skb, __be16 vlan_proto,
				    u16 vlan_tci)
{
	unsigned char mac[2 * ETH_ALEN];
	struct vlan_ethhdr *veh;

	if (skb_cow_head(skb, VLAN_HLEN) < 0)
		return -ENOMEM;

	memcpy(mac, skb->data, sizeof(mac));
	skb_push(skb, VLAN_HLEN);
	memcpy(skb->data, mac, sizeof(mac));
	veh = (struct vlan_ethhdr *)skb->data;
	veh->h_vlan_proto = vlan_proto;
	veh->h_vlan_TCI = htons(vlan_tci);
	return 0;
}

static inline void dsa_alloc_etype_header(struct sk_buff *skb, int len)
{
	memmove(skb->data, skb->data + len, 2 * ETH_ALEN);
}

static inline __be16 *dsa_etype_header_pos_tx(struct sk_buff *skb)
{
	return (__be16 *)(skb->data + 2 * ETH_ALEN);
}

static inline struct dsa_port *
dsa_user_to_port(struct net_device *netdev)
{
	(void)netdev;
	return &test_dp;
}

static inline struct net_device *dsa_port_bridge_dev_get(struct dsa_port *dp)
{
	(void)dp;
	return fake_bridge;
}

static inline bool br_vlan_enabled(struct net_device *br)
{
	(void)br;
	return fake_br_vlan_enabled;
}

static inline int dsa_port_bridge_num_get(struct dsa_port *dp)
{
	(void)dp;
	return fake_bridge_num;
}

static inline u16 dsa_tag_8021q_bridge_vid(unsigned int bridge_num)
{
	(void)bridge_num;
	return fake_bridge_vid;
}

static inline u16 dsa_tag_8021q_standalone_vid(struct dsa_port *dp)
{
	(void)dp;
	return fake_standalone_vid;
}

static inline bool
dsa_yt921x_qinq_customer_vid_valid(struct dsa_switch *ds, u16 vid)
{
	(void)ds;
	(void)vid;
	return true;
}

static inline bool netif_is_bridge_port(struct net_device *d) { return d->bridge_port; }
static inline bool netif_is_lag_port(struct net_device *d) { return d->lag_port; }
static inline struct net_device *__vlan_find_dev_deep_rcu(struct net_device *d, __be16 proto, u16 vid) {
	return proto == htons(ETH_P_8021Q) && d->direct_upper &&
	       d->direct_upper->direct_vid == vid ? d->direct_upper : NULL;
}
static inline struct net_device *vlan_dev_real_dev(struct net_device *d) { return d->direct_real; }

@@DIRECT_HELPER@@

static inline int br_vlan_get_info_rcu(struct net_device *dev, u16 vid,
				       struct bridge_vlan_info *vinfo)
{
	(void)dev;
	vinfo->vid = vid;
	return 0;
}

static inline int br_vlan_get_pvid_rcu(struct net_device *dev, u16 *pvid)
{
	(void)dev;
	*pvid = fake_pvid;
	return 0;
}

static inline int netdev_txq_to_tc(struct net_device *dev, unsigned int txq)
{
	(void)dev;
	(void)txq;
	return fake_tc;
}

static inline u16 skb_get_queue_mapping(const struct sk_buff *skb)
{
	return (u16)skb->queue_mapping;
}

static struct dsa_port *dsa_to_port(struct dsa_switch *ds, int port)
{
 (void)ds;
 if (port != 8) abort();
 return &test_cpu;
}
static bool dsa_is_cpu_port(struct dsa_switch *ds, int port)
{
 (void)ds;
 return fake_cpu_valid && port == 8;
}
static struct sk_buff *skb_clone(struct sk_buff *skb, int flags)
{
 (void)flags;
 fake_clone_calls++;
 if (fake_clone_fail) return NULL;
 struct sk_buff *clone = malloc(sizeof(*clone));
 if (!clone) abort();
 *clone = *skb;
 clone->head = malloc(1024);
 if (!clone->head) abort();
 memcpy(clone->head, skb->head, 1024);
 clone->data = clone->head + (skb->data - skb->head);
 return clone;
}
static void kfree_skb(struct sk_buff *skb)
{
 fake_frees++;
 skb_free(skb);
}
static int dev_queue_xmit(struct sk_buff *skb)
{
 if (fake_queued) abort();
 fake_queued = skb;
 return 0;
}

@@FUNCTIONS@@

static int failures;
static int checks;

#define CHECK(cond, name) do { \
	checks++; \
	if (cond) { \
		printf("PASS %s\n", (name)); \
	} else { \
		printf("FAIL %s (line %d)\n", (name), __LINE__); \
		failures++; \
	} \
} while (0)

static struct sk_buff *new_skb(const unsigned char *frame, size_t len,
			       unsigned int headroom)
{
	struct sk_buff *skb = calloc(1, sizeof(*skb));
	unsigned char *buf = calloc(1, 1024);

	if (!skb || !buf)
		abort();

	skb->magic = SKB_MAGIC;
	skb->head = buf;
	skb->data = buf + headroom;
	memcpy(skb->data, frame, len);
	skb->len = len;
	skb->ip_summed = CHECKSUM_NONE;
	skb->protocol = htons(0x0800);
	return skb;
}

static void skb_free(struct sk_buff *skb)
{
	if (!skb)
		return;
	if (skb->magic != SKB_MAGIC)
		printf("FAIL skb_magic_before_free\n");
	free(skb->head);
	free(skb);
}

static bool preserves_frame(const struct sk_buff *skb, const unsigned char *original,
                            size_t length)
{
 return !memcmp(skb->data, original, 12) &&
        !memcmp(skb->data + 20, original + 12, length - 12);
}

static void check_control_tag(const struct sk_buff *skb, unsigned int port,
			      unsigned int prio, const char *name)
{
	unsigned char expected[8];
	u16 tpid = htons(ETH_P_YT921X);
	u16 vlan = htons(0);
	u16 code = htons((u16)(0x0080u | ((prio & 0x7u) << 8)));
	u16 tx = htons((u16)(0x8000u | (1u << port)));

	memcpy(expected, &tpid, 2);
	memcpy(expected + 2, &vlan, 2);
	memcpy(expected + 4, &code, 2);
	memcpy(expected + 6, &tx, 2);

	CHECK(memcmp(skb->data + 12, expected, sizeof(expected)) == 0, name);
}

static const unsigned char bpdu[] = {
	0x01, 0x80, 0xc2, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x01,
	0x00, 0x26,
	0x42, 0x42, 0x03,
};

static const unsigned char bpdu_ctag[] = {
	0x01, 0x80, 0xc2, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x01,
	0x81, 0x00, 0x20, 0x64,
	0x00, 0x26,
	0x42, 0x42, 0x03,
};

static const unsigned char bpdu_hw_ctag[] = {
	0x01, 0x80, 0xc2, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x01,
	0x81, 0x00, 0x30, 0x64,
	0x00, 0x26,
	0x42, 0x42, 0x03,
};

static const unsigned char unicast[] = {
	0x02, 0x00, 0x00, 0x00, 0x00, 0x02,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x01,
	0x08, 0x00,
	0x45, 0x00, 0x00, 0x14,
};

static const unsigned char link_local_0f[] = {
	0x01, 0x80, 0xc2, 0x00, 0x00, 0x0f,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x01,
	0x08, 0x00, 0x00,
};

static const unsigned char link_local_10[] = {
	0x01, 0x80, 0xc2, 0x00, 0x00, 0x10,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x01,
	0x08, 0x00, 0x00,
};

static const unsigned char other_mcast[] = {
	0x01, 0x00, 0x5e, 0x00, 0x00, 0x01,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x01,
	0x08, 0x00, 0x00,
};

static const unsigned char bpdu_stacked[] = {
	0x01, 0x80, 0xc2, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x01,
	0x81, 0x00, 0x20, 0x64,
	0x81, 0x00, 0x30, 0x64,
	0x08, 0x00,
};

static const unsigned char bpdu_outer_88a8[] = {
	0x01, 0x80, 0xc2, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x01,
	0x88, 0xa8, 0x20, 0x64,
	0x08, 0x00,
};

static void test_control_predicate(void)
{
	static const unsigned char short_frame[] = {
		0x01, 0x80, 0xc2, 0x00, 0x00, 0x00, 0x02, 0x00,
		0x00, 0x00, 0x00, 0x01, 0x08,
	};
	struct sk_buff *skb;

	reset_fakes();

	skb = new_skb(bpdu, sizeof(bpdu), 64);
	CHECK(yt921x_qinq_is_control_frame(skb),
	      "control_predicate_bpdu_00");
	skb_free(skb);

	skb = new_skb(link_local_0f, sizeof(link_local_0f), 64);
	CHECK(yt921x_qinq_is_control_frame(skb),
	      "control_predicate_link_local_0f");
	skb_free(skb);

	skb = new_skb(link_local_10, sizeof(link_local_10), 64);
	CHECK(!yt921x_qinq_is_control_frame(skb),
	      "control_predicate_rejects_link_local_10");
	skb_free(skb);

	skb = new_skb(other_mcast, sizeof(other_mcast), 64);
	CHECK(!yt921x_qinq_is_control_frame(skb),
	      "control_predicate_rejects_other_multicast");
	skb_free(skb);

	skb = new_skb(unicast, sizeof(unicast), 64);
	CHECK(!yt921x_qinq_is_control_frame(skb),
	      "control_predicate_rejects_unicast");
	skb_free(skb);

	skb = new_skb(short_frame, sizeof(short_frame), 64);
	CHECK(!yt921x_qinq_is_control_frame(skb),
	      "control_predicate_short_frame_false");
	CHECK(yt921x_qinq_xmit_control(skb, &test_dp) == NULL,
	      "control_helper_short_frame_null");
	CHECK(skb->magic == SKB_MAGIC,
	      "control_helper_short_frame_keeps_skb");
	skb_free(skb);
}

static void test_control_tag_bytes(void)
{
	struct sk_buff *skb;

	reset_fakes();
	test_dp.index = 3;
	skb = new_skb(bpdu, sizeof(bpdu), 64);
	skb->priority = 0;
	CHECK(yt921x_qinq_xmit(skb, &test_netdev) == skb,
	      "control_xmit_returns_original_skb");
	CHECK(skb->len == sizeof(bpdu) + 8,
	      "control_xmit_adds_only_cpu_tag");
	check_control_tag(skb, 3, 0, "control_tag_port3_prio0");
	CHECK(preserves_frame(skb, bpdu, sizeof(bpdu)),
	      "control_xmit_preserves_frame_bytes");
	CHECK(fake_checksum_help_calls == 0,
	      "control_checksum_none_not_helped");
	CHECK(fake_cow_head_calls == 1,
	      "control_cpu_tag_one_cow_head");
	skb_free(skb);

	reset_fakes();
	test_dp.index = 0;
	skb = new_skb(bpdu, sizeof(bpdu), 64);
	skb->priority = 5;
	CHECK(yt921x_qinq_xmit(skb, &test_netdev) == skb,
	      "control_prio_xmit_returns_original_skb");
	check_control_tag(skb, 0, 5, "control_tag_port0_prio5");
	CHECK(skb->len == sizeof(bpdu) + 8,
	      "control_prio_adds_only_cpu_tag");
	skb_free(skb);
}

static void test_control_customer_tag(void)
{
	struct sk_buff *skb;

	reset_fakes();
	test_dp.index = 2;
	skb = new_skb(bpdu_ctag, sizeof(bpdu_ctag), 64);
	CHECK(yt921x_qinq_xmit(skb, &test_netdev) == skb,
	      "control_inline_ctag_returns_skb");
	CHECK(skb->len == sizeof(bpdu_ctag) + 8,
	      "control_inline_ctag_adds_only_cpu_tag");
	check_control_tag(skb, 2, 0, "control_inline_ctag_tag_bytes");
	CHECK(preserves_frame(skb, bpdu_ctag, sizeof(bpdu_ctag)),
	      "control_inline_ctag_preserved");
	CHECK(fake_checksum_help_calls == 0,
	      "control_inline_ctag_checksum_none");
	CHECK(fake_cow_head_calls == 1,
	      "control_inline_ctag_one_cow_head");
	skb_free(skb);

	reset_fakes();
	test_dp.index = 2;
	skb = new_skb(bpdu, sizeof(bpdu), 64);
	skb->vlan_present = true;
	skb->vlan_proto = htons(ETH_P_8021Q);
	skb->vlan_tci = 0x3064;
	CHECK(yt921x_qinq_xmit(skb, &test_netdev) == skb,
	      "control_hw_ctag_returns_skb");
	CHECK(skb->len == sizeof(bpdu) + 8 + VLAN_HLEN,
	      "control_hw_ctag_adds_cpu_and_c_tag");
	check_control_tag(skb, 2, 0, "control_hw_ctag_tag_bytes");
	CHECK(preserves_frame(skb, bpdu_hw_ctag, sizeof(bpdu_hw_ctag)),
	      "control_hw_ctag_materialized_inline");
	CHECK(!skb->vlan_present,
	      "control_hw_ctag_metadata_cleared");
	CHECK(fake_checksum_help_calls == 0,
	      "control_hw_ctag_checksum_none");
	CHECK(fake_cow_head_calls == 2,
	      "control_hw_ctag_two_cow_heads");
	skb_free(skb);
}

static void test_ordinary_data(void)
{
	struct sk_buff *skb;

	reset_fakes();
	test_dp.index = 4;
	fake_standalone_vid = 3004;
	skb = new_skb(unicast, sizeof(unicast), 64);
	CHECK(yt921x_qinq_xmit(skb, &test_netdev) == skb,
	      "ordinary_data_returns_skb");
	CHECK(skb->len == sizeof(unicast) + 2 * VLAN_HLEN,
	      "ordinary_data_two_vlan_tags");
	CHECK(skb->data[12] == 0x88 && skb->data[13] == 0xa8,
	      "ordinary_data_outer_s_tag");
	CHECK(skb->data[14] == 0x0b && skb->data[15] == 0xbc,
	      "ordinary_data_outer_vid");
	CHECK(skb->data[16] == 0x81 && skb->data[17] == 0x00,
	      "ordinary_data_inner_c_tag");
	CHECK(skb->data[18] == 0x0b && skb->data[19] == 0xbc,
	      "ordinary_data_inner_vid");
	CHECK(preserves_frame(skb, unicast, sizeof(unicast)),
	      "ordinary_data_payload_preserved");
	CHECK(!(skb->data[12] == 0x99 && skb->data[13] == 0x88),
	      "ordinary_data_has_no_cpu_tag");
	CHECK(fake_checksum_help_calls == 0,
	      "ordinary_data_no_checksum_help");
	skb_free(skb);

	reset_fakes();
	test_dp.index = 4;
	fake_standalone_vid = 3004;
	skb = new_skb(link_local_10, sizeof(link_local_10), 64);
	CHECK(yt921x_qinq_xmit(skb, &test_netdev) == skb,
	      "near_link_local_data_returns_skb");
	CHECK(skb->len == sizeof(link_local_10) + 2 * VLAN_HLEN,
	      "near_link_local_data_two_vlan_tags");
	CHECK(skb->data[12] == 0x88 && skb->data[13] == 0xa8,
	      "near_link_local_data_outer_s_tag");
	CHECK(!(skb->data[12] == 0x99 && skb->data[13] == 0x88),
	      "near_link_local_data_no_cpu_tag");
	skb_free(skb);
}

static void test_control_conduit(void)
{
 struct sk_buff *skb;
 reset_fakes();
 test_dp.cpu_dp = &test_secondary;
 test_dp.index = 3;
 skb = new_skb(bpdu, sizeof(bpdu), 64);
 CHECK(!yt921x_qinq_xmit(skb, &test_netdev), "secondary_control_consumes_clone_only");
 CHECK(fake_queued && fake_queued->dev == &test_netdev, "secondary_control_uses_primary_conduit");
 CHECK(fake_clone_calls == 1 && !fake_frees, "secondary_control_queue_owns_one_clone");
 CHECK(skb->len == sizeof(bpdu) && !memcmp(skb->data, bpdu, sizeof(bpdu)), "secondary_control_original_unchanged");
 if (fake_queued) {
  check_control_tag(fake_queued, 3, 0, "secondary_control_exact_wan_mask");
  CHECK(preserves_frame(fake_queued, bpdu, sizeof(bpdu)), "secondary_control_clone_payload");
  skb_free(fake_queued);
 }
 skb_free(skb);

 reset_fakes();
 test_dp.cpu_dp = &test_secondary;
 skb = new_skb(unicast, sizeof(unicast), 64);
 CHECK(yt921x_qinq_xmit(skb, &test_netdev) == skb, "secondary_data_keeps_normal_conduit");
 CHECK(!fake_clone_calls && !fake_queued, "secondary_data_never_redirected");
 skb_free(skb);

 for (int error=0;error<5;error++) {
  reset_fakes();
  test_dp.cpu_dp = &test_secondary;
  if (error==0) fake_clone_fail=true;
  if (error==1) fake_cow_head_fail_len=8;
  if (error==2) test_cpu.conduit=NULL;
  if (error==3) fake_cpu_valid=false;
  if (error==4) test_ds.num_ports=8;
  skb = new_skb(bpdu, sizeof(bpdu), 64);
  CHECK(!yt921x_qinq_xmit(skb, &test_netdev), "secondary_control_error_returns_null");
  CHECK(!fake_queued && fake_frees==(error==1), "secondary_control_error_clone_cleanup");
  CHECK(skb->len==sizeof(bpdu) && !memcmp(skb->data,bpdu,sizeof(bpdu)), "secondary_control_error_original_preserved");
  skb_free(skb);
 }
}

static void test_error_paths(void)
{
	struct sk_buff *skb;
	struct sk_buff *out;

	reset_fakes();
	test_dp.index = 3;
	skb = new_skb(bpdu, sizeof(bpdu), 64);
	skb->offload_fwd_mark = 1;
	out = yt921x_qinq_xmit(skb, &test_netdev);
	CHECK(out == NULL, "offload_rejected_null");
	CHECK(skb->len == sizeof(bpdu),
	      "offload_rejected_len_unchanged");
	CHECK(memcmp(skb->data, bpdu, sizeof(bpdu)) == 0,
	      "offload_rejected_untouched");
	CHECK(fake_checksum_help_calls == 0,
	      "offload_rejected_no_checksum");
	CHECK(fake_cow_head_calls == 0, "offload_rejected_no_cow");
	CHECK(skb->magic == SKB_MAGIC, "offload_rejected_keeps_skb");
	skb_free(skb);

	reset_fakes();
	test_dp.index = 3;
	skb = new_skb(bpdu, sizeof(bpdu), 64);
	skb->ip_summed = CHECKSUM_PARTIAL;
	out = yt921x_qinq_xmit(skb, &test_netdev);
	CHECK(out == skb, "checksum_help_success_returns_skb");
	CHECK(fake_checksum_help_calls == 1,
	      "checksum_help_called_once");
	CHECK(fake_checksum_data_offset == 64,
	      "checksum_help_before_cpu_tag");
	CHECK(skb->len == sizeof(bpdu) + 8,
	      "checksum_help_then_cpu_tag");
	check_control_tag(skb, 3, 0, "checksum_help_tag_bytes");
	skb_free(skb);

	reset_fakes();
	test_dp.index = 3;
	skb = new_skb(bpdu, sizeof(bpdu), 64);
	skb->ip_summed = CHECKSUM_PARTIAL;
	fake_checksum_help_result = -EINVAL;
	out = yt921x_qinq_xmit(skb, &test_netdev);
	CHECK(out == NULL, "checksum_help_failure_null");
	CHECK(fake_checksum_help_calls == 1,
	      "checksum_help_failure_called");
	CHECK(skb->len == sizeof(bpdu),
	      "checksum_help_failure_len_unchanged");
	CHECK(memcmp(skb->data, bpdu, sizeof(bpdu)) == 0,
	      "checksum_help_failure_untouched");
	CHECK(skb->magic == SKB_MAGIC,
	      "checksum_help_failure_keeps_skb");
	CHECK(fake_cow_head_calls == 0,
	      "checksum_help_failure_no_cow");
	skb_free(skb);

	reset_fakes();
	test_dp.index = 3;
	skb = new_skb(bpdu, sizeof(bpdu), 4);
	out = yt921x_qinq_xmit(skb, &test_netdev);
	CHECK(out == NULL, "cpu_tag_headroom_failure_null");
	CHECK(skb->len == sizeof(bpdu),
	      "cpu_tag_headroom_failure_len_unchanged");
	CHECK(memcmp(skb->data, bpdu, sizeof(bpdu)) == 0,
	      "cpu_tag_headroom_failure_untouched");
	CHECK(fake_cow_head_calls == 1,
	      "cpu_tag_headroom_failure_cow_called");
	CHECK(skb->magic == SKB_MAGIC,
	      "cpu_tag_headroom_failure_keeps_skb");
	skb_free(skb);

	reset_fakes();
	test_dp.index = 3;
	fake_cow_head_fail_len = 8;
	skb = new_skb(bpdu, sizeof(bpdu), 64);
	out = yt921x_qinq_xmit(skb, &test_netdev);
	CHECK(out == NULL, "cpu_tag_cow_failure_null");
	CHECK(skb->len == sizeof(bpdu),
	      "cpu_tag_cow_failure_len_unchanged");
	CHECK(memcmp(skb->data, bpdu, sizeof(bpdu)) == 0,
	      "cpu_tag_cow_failure_untouched");
	CHECK(skb->magic == SKB_MAGIC, "cpu_tag_cow_failure_keeps_skb");
	skb_free(skb);

	reset_fakes();
	test_dp.index = 3;
	skb = new_skb(bpdu, sizeof(bpdu), 64);
	skb->vlan_present = true;
	skb->vlan_proto = htons(ETH_P_8021AD);
	skb->vlan_tci = 0x3064;
	out = yt921x_qinq_xmit(skb, &test_netdev);
	CHECK(out == NULL, "hw_unsupported_tpid_null");
	CHECK(skb->len == sizeof(bpdu),
	      "hw_unsupported_tpid_len_unchanged");
	CHECK(fake_cow_head_calls == 0, "hw_unsupported_tpid_no_cow");
	CHECK(skb->magic == SKB_MAGIC,
	      "hw_unsupported_tpid_keeps_skb");
	skb_free(skb);

	reset_fakes();
	test_dp.index = 3;
	skb = new_skb(bpdu_stacked, sizeof(bpdu_stacked), 64);
	out = yt921x_qinq_xmit(skb, &test_netdev);
	CHECK(out == NULL, "stacked_ctag_null");
	CHECK(skb->len == sizeof(bpdu_stacked),
	      "stacked_ctag_len_unchanged");
	CHECK(memcmp(skb->data, bpdu_stacked, sizeof(bpdu_stacked)) == 0,
	      "stacked_ctag_untouched");
	CHECK(fake_cow_head_calls == 0, "stacked_ctag_no_cow");
	CHECK(skb->magic == SKB_MAGIC, "stacked_ctag_keeps_skb");
	skb_free(skb);

	reset_fakes();
	test_dp.index = 3;
	skb = new_skb(bpdu_outer_88a8, sizeof(bpdu_outer_88a8), 64);
	out = yt921x_qinq_xmit(skb, &test_netdev);
	CHECK(out == NULL, "inline_88a8_null");
	CHECK(skb->len == sizeof(bpdu_outer_88a8),
	      "inline_88a8_len_unchanged");
	CHECK(fake_cow_head_calls == 0, "inline_88a8_no_cow");
	CHECK(skb->magic == SKB_MAGIC, "inline_88a8_keeps_skb");
	skb_free(skb);

	reset_fakes();
	test_dp.index = 3;
	skb = new_skb(bpdu, sizeof(bpdu), 2);
	skb->vlan_present = true;
	skb->vlan_proto = htons(ETH_P_8021Q);
	skb->vlan_tci = 0x3064;
	out = yt921x_qinq_xmit(skb, &test_netdev);
	CHECK(out == NULL, "hw_ctag_headroom_failure_null");
	CHECK(skb->len == sizeof(bpdu),
	      "hw_ctag_headroom_failure_len_unchanged");
	CHECK(skb->magic == SKB_MAGIC,
	      "hw_ctag_headroom_failure_keeps_skb");
	CHECK(fake_cow_head_calls == 1,
	      "hw_ctag_headroom_failure_cow_called");
	skb_free(skb);

	reset_fakes();
	test_dp.index = 3;
	skb = new_skb(bpdu, sizeof(bpdu), 6);
	skb->vlan_present = true;
	skb->vlan_proto = htons(ETH_P_8021Q);
	skb->vlan_tci = 0x3064;
	out = yt921x_qinq_xmit(skb, &test_netdev);
	CHECK(out == NULL,
	      "hw_ctag_then_cpu_tag_headroom_failure_null");
	CHECK(skb->magic == SKB_MAGIC,
	      "hw_ctag_then_cpu_tag_failure_keeps_skb");
	CHECK(fake_cow_head_calls == 2,
	      "hw_ctag_then_cpu_tag_failure_cow_calls");
	skb_free(skb);
}

static void test_direct_customer_vlan(void)
{
	unsigned char frame[80];
	struct net_device upper = { .direct_vid = 35, .direct_real = &test_netdev };
	struct sk_buff *skb;
	reset_fakes();
	test_netdev.direct_upper = &upper;
	memset(frame, 0, sizeof(frame));
	memcpy(frame, unicast, 12);
	frame[12] = 0x81; frame[13] = 0;
	frame[14] = 0; frame[15] = 35;
	frame[16] = 0x08; frame[17] = 0;
	skb = new_skb(frame, sizeof(frame), 32);
	CHECK(yt921x_qinq_xmit(skb, &test_netdev) == skb, "direct_registered_upper_TX");
	CHECK(((u16)skb->data[12] << 8 | skb->data[13]) == ETH_P_8021AD &&
	      ((u16)skb->data[14] << 8 | skb->data[15]) == fake_standalone_vid &&
	      ((u16)skb->data[16] << 8 | skb->data[17]) == ETH_P_8021Q &&
	      ((u16)skb->data[18] << 8 | skb->data[19]) == 35, "direct_exact_port_and_customer_tags");
	skb_free(skb);
	reset_fakes();
	skb = new_skb(frame, sizeof(frame), 32);
	CHECK(!yt921x_qinq_xmit(skb, &test_netdev), "direct_unregistered_upper_drops");
	skb_free(skb);
	reset_fakes();test_netdev.direct_upper = &upper;upper.direct_vid = 36;
	skb = new_skb(frame, sizeof(frame), 32);
	CHECK(!yt921x_qinq_xmit(skb, &test_netdev), "direct_wrong_vid_drops");
	skb_free(skb);upper.direct_vid = 35;
	reset_fakes();test_netdev.direct_upper = &upper;test_netdev.bridge_port = true;
	skb = new_skb(frame, sizeof(frame), 32);
	CHECK(!yt921x_qinq_xmit(skb, &test_netdev), "direct_software_bridge_rejected");
	skb_free(skb);
	reset_fakes();test_netdev.direct_upper = &upper;test_netdev.lag_port = true;
	skb = new_skb(frame, sizeof(frame), 32);
	CHECK(!yt921x_qinq_xmit(skb, &test_netdev), "direct_lag_rejected");
	skb_free(skb);
	reset_fakes();test_netdev.direct_upper = &upper;upper.direct_real = &upper;
	skb = new_skb(frame, sizeof(frame), 32);
	CHECK(!yt921x_qinq_xmit(skb, &test_netdev), "direct_wrong_real_device_rejected");
	skb_free(skb);upper.direct_real = &test_netdev;
	reset_fakes();test_netdev.direct_upper = &upper;test_netdev.features = 0;
	skb = new_skb(frame, sizeof(frame), 32);
	CHECK(!yt921x_qinq_xmit(skb, &test_netdev), "direct_without_hardware_VLAN_hook_rejected");
	skb_free(skb);
}

int main(void)
{
	test_control_predicate();
	test_control_tag_bytes();
	test_control_customer_tag();
	test_ordinary_data();
	test_error_paths();
	test_control_conduit();
	test_direct_customer_vlan();

	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
"""


def main() -> int:
    if not SOURCE.is_file():
        print("missing source: %s" % SOURCE, file=sys.stderr)
        return 1

    source = SOURCE.read_text(encoding="utf-8")
    macros = extract_macros(source, "YT921X_QINQ_CPU_TAG_")
    if not macros:
        print("control tag macros not found", file=sys.stderr)
        return 1

    bodies = []
    for name in FUNCTIONS:
        try:
            bodies.append(extract_function(source, name))
        except ValueError as err:
            print(str(err), file=sys.stderr)
            return 1

    harness = HARNESS.replace("@@MACROS@@", macros)
    harness = harness.replace("@@FUNCTIONS@@", "\n\n".join(bodies))
    shared = (SOURCE.parents[2] / "include/linux/dsa/yt921x_qinq.h").read_text()
    harness = harness.replace("@@DIRECT_HELPER@@", extract_function(shared, "dsa_yt921x_qinq_direct_vid_valid"))

    cc = shlex.split(os.environ.get("CC", "cc"))
    with tempfile.TemporaryDirectory(prefix="yt921x-qinq-test-") as tmp:
        c_path = Path(tmp) / "harness.c"
        exe_path = Path(tmp) / "harness"
        c_path.write_text(harness, encoding="utf-8")

        compile_cmd = cc + [
            "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror",
            "-o", str(exe_path), str(c_path),
        ]
        try:
            compiled = subprocess.run(compile_cmd, text=True,
                                      capture_output=True)
        except FileNotFoundError:
            print("compiler not found: %s" % cc[0], file=sys.stderr)
            return 1

        if compiled.returncode:
            print(compiled.stdout, end="")
            print(compiled.stderr, end="", file=sys.stderr)
            return 1

        result = subprocess.run([str(exe_path)], text=True,
                                capture_output=True)
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        return result.returncode


if __name__ == "__main__":
    sys.exit(main())
