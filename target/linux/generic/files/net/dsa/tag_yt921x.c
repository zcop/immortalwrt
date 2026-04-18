// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Motorcomm YT921x Switch Extended CPU Port Tagging
 *
 * Copyright (c) 2025 David Yang <mmyangfl@gmail.com>
 *
 * +----+----+-------+-----+----+---------
 * | DA | SA | TagET | Tag | ET | Payload ...
 * +----+----+-------+-----+----+---------
 *   6    6      2      6    2       N
 *
 * Tag Ethertype: CPU_TAG_TPID_TPID (default: ETH_P_YT921X = 0x9988)
 *   * Hardcoded for the moment, but still configurable. Discuss it if there
 *     are conflicts somewhere and/or you want to change it for some reason.
 * Tag:
 *   2: VLAN Tag
 *   2: Rx Port
 *     15b: Rx Port Valid
 *     14b-11b: Rx Port
 *     10b-0b: Cmd?
 *   2: Tx Port(s)
 *     15b: Tx Port(s) Valid
 *     10b-0b: Tx Port(s) Mask
 */

#include <linux/etherdevice.h>

#include "tag.h"

#define YT921X_TAG_NAME	"yt921x"

#define YT921X_TAG_LEN	8

#define YT921X_TAG_PORT_EN		BIT(15)
#define YT921X_TAG_RX_PORT_M		GENMASK(14, 11)
#define YT921X_TAG_PRIO_M		GENMASK(10, 8)
#define  YT921X_TAG_PRIO(x)			FIELD_PREP(YT921X_TAG_PRIO_M, (x))
#define YT921X_TAG_CODE_EN		BIT(7)
#define YT921X_TAG_CODE_M		GENMASK(6, 1)
#define  YT921X_TAG_CODE(x)			FIELD_PREP(YT921X_TAG_CODE_M, (x))
#define YT921X_TAG_TX_PORTS_M		GENMASK(10, 0)
#define YT921X_TAG_TX_PORTn(port)	BIT(port)

enum yt921x_tag_code {
	YT921X_TAG_CODE_FORWARD = 0x00,
	YT921X_TAG_CODE_L2_CTRL = 0x18,
	YT921X_TAG_CODE_UNK_UCAST = 0x19,
	YT921X_TAG_CODE_UNK_MCAST = 0x1a,
	YT921X_TAG_CODE_PORT_COPY = 0x1b,
	YT921X_TAG_CODE_FDB_COPY = 0x1c,
};

static struct sk_buff *
yt921x_tag_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct dsa_port *dp = dsa_user_to_port(netdev);
	unsigned int port = dp->index;
	__be16 *tag;
	u16 tx;

	if (dp->cpu_dp && dp->cpu_dp->index != 8) {
		/* Secondary conduit sends raw frames (no YT tag). */
		return skb;
	}

	if (unlikely(skb_cow_head(skb, YT921X_TAG_LEN) < 0))
		return NULL;

	skb_push(skb, YT921X_TAG_LEN);
	dsa_alloc_etype_header(skb, YT921X_TAG_LEN);

	tag = dsa_etype_header_pos_tx(skb);

	tag[0] = htons(ETH_P_YT921X);
	/* VLAN tag unrelated when TX */
	tag[1] = 0;
	tag[2] = htons(YT921X_TAG_CODE(YT921X_TAG_CODE_FORWARD) |
		       YT921X_TAG_CODE_EN |
		       YT921X_TAG_PRIO(skb->priority));
	tx = YT921X_TAG_PORT_EN | YT921X_TAG_TX_PORTn(port);
	tag[3] = htons(tx);

	return skb;
}

static struct sk_buff *
yt921x_tag_rcv(struct sk_buff *skb, struct net_device *netdev)
{
	struct dsa_port *cpu_dp = netdev->dsa_ptr;
	unsigned int port;
	__be16 *tag;
	u16 code;
	u16 rx;

	if (unlikely(!pskb_may_pull(skb, YT921X_TAG_LEN)))
		return NULL;

	tag = dsa_etype_header_pos_rx(skb);

	if (unlikely(tag[0] != htons(ETH_P_YT921X))) {
		/* Secondary conduit receives plain Ethernet frames. */
		if (cpu_dp && cpu_dp->index != 8) {
			struct dsa_port *dp;
			struct net_device *user = NULL;

			dsa_switch_for_each_user_port(dp, cpu_dp->ds) {
				if (dsa_port_to_conduit(dp) != netdev)
					continue;

				if (unlikely(user)) {
					dev_warn_ratelimited(&netdev->dev,
							     "secondary conduit has multiple user ports, dropping raw frame\n");
					return NULL;
				}

				user = dp->user;
			}

			if (likely(user)) {
				skb->dev = user;
				return skb;
			}
		}

		return NULL;
	}

	/* Locate which port this is coming from */
	rx = ntohs(tag[2]);
	if (unlikely((rx & YT921X_TAG_PORT_EN) == 0)) {
		dev_warn_ratelimited(&netdev->dev,
				     "Unexpected rx tag 0x%04x\n", rx);
		return NULL;
	}

	port = FIELD_GET(YT921X_TAG_RX_PORT_M, rx);
	skb->dev = dsa_conduit_find_user(netdev, 0, port);
	if (unlikely(!skb->dev)) {
		/* During conduit retarget, tagged spillover can report a CPU
		 * source port (for example 4) on the primary conduit. That is
		 * not a user-port frame, so drop it quietly.
		 */
		if (cpu_dp && port < cpu_dp->ds->num_ports &&
		    dsa_is_cpu_port(cpu_dp->ds, port)) {
			return NULL;
		}

		dev_warn_ratelimited(&netdev->dev,
				     "Couldn't decode source port %u\n", port);
		return NULL;
	}

	skb->priority = FIELD_GET(YT921X_TAG_PRIO_M, rx);

	if (unlikely(!(rx & YT921X_TAG_CODE_EN))) {
		dev_warn_ratelimited(&netdev->dev,
				     "Tag code not enabled in rx packet\n");
		goto out_strip;
	}

	code = FIELD_GET(YT921X_TAG_CODE_M, rx);
	switch (code) {
	case YT921X_TAG_CODE_FORWARD:
	case YT921X_TAG_CODE_PORT_COPY:
	case YT921X_TAG_CODE_FDB_COPY:
		/* Already forwarded by hardware */
		dsa_default_offload_fwd_mark(skb);
		break;
	case YT921X_TAG_CODE_L2_CTRL:
	case YT921X_TAG_CODE_UNK_UCAST:
	case YT921X_TAG_CODE_UNK_MCAST:
		/* CPU-directed trap/copy classes. Do not set offload_fwd_mark. */
		break;
	default:
		dev_warn_ratelimited(&netdev->dev,
				     "Unknown tag code 0x%02x\n", code);
		break;
	}

out_strip:
	/* Remove YT921x tag and update checksum */
	skb_pull_rcsum(skb, YT921X_TAG_LEN);
	dsa_strip_etype_header(skb, YT921X_TAG_LEN);

	return skb;
}

static const struct dsa_device_ops yt921x_netdev_ops = {
	.name	= YT921X_TAG_NAME,
	.proto	= DSA_TAG_PROTO_YT921X,
	.xmit	= yt921x_tag_xmit,
	.rcv	= yt921x_tag_rcv,
	.needed_headroom = YT921X_TAG_LEN,
};

MODULE_DESCRIPTION("DSA tag driver for Motorcomm YT921x switches");
MODULE_LICENSE("GPL");
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_YT921X, YT921X_TAG_NAME);

module_dsa_tag_driver(yt921x_netdev_ops);
