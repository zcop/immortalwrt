// SPDX-License-Identifier: GPL-2.0
/* 802.1Q VLAN-based tag driver for the Motorcomm YT921x switch
 *
 * The YT921x can either prepend its own 8-byte tag (ETH_P_YT921X) to the
 * frames it delivers to the CPU port, or speak plain 802.1Q there: the
 * source port then travels in a VLAN tag the switch inserts from its VLAN
 * table (3072 + port for a standalone port, 3088 + n for bridge n), the
 * same wire format as tag_qca_8021q, tag_rtl8365mb_8021q and
 * tag_an8855_8021q.
 *
 * The switch side is in the driver (yt921x_change_tag_protocol and the
 * tag_8021q_vlan_add/del hooks).
 */
#include <linux/dsa/8021q.h>
#include <linux/if_bridge.h>
#include <linux/if_vlan.h>
#include <net/dsa.h>

#include "tag.h"
#include "tag_8021q.h"

#define YT921X_8021Q_NAME "yt921x-8021q"

static struct sk_buff *yt921x_8021q_xmit(struct sk_buff *skb,
					 struct net_device *netdev)
{
	struct dsa_port *dp = dsa_user_to_port(netdev);
	u16 queue_mapping = skb_get_queue_mapping(skb);
	u16 tx_vid = dsa_tag_8021q_standalone_vid(dp);
	u8 pcp;

	if (skb->offload_fwd_mark) {
		unsigned int bridge_num = dsa_port_bridge_num_get(dp);
		struct net_device *br = dsa_port_bridge_dev_get(dp);

		if (unlikely(!br))
			return NULL;

		/* A VLAN-aware bridge carries its own VID; the switch
		 * forwards on that one.
		 */
		if (br_vlan_enabled(br))
			return skb;

		tx_vid = dsa_tag_8021q_bridge_vid(bridge_num);
	}

	pcp = netdev_txq_to_tc(netdev, queue_mapping);

	return dsa_8021q_xmit(skb, netdev, ETH_P_8021Q,
			      ((pcp << VLAN_PRIO_SHIFT) | tx_vid));
}

/* Inactive VLAN configuration of an unaware bridge must not select RX. */
static struct net_device *yt921x_8021q_find_bridge_vlan(struct net_device *conduit,
							u16 vid)
{
	struct dsa_port *cpu_dp = conduit->dsa_ptr;
	struct bridge_vlan_info vinfo;
	struct dsa_port *dp;

	dsa_tree_for_each_user_port(dp, cpu_dp->dst) {
		if (dp->cpu_dp != cpu_dp || !dp->user || !dp->bridge ||
		    !dsa_port_is_vlan_filtering(dp))
			continue;
		if (dp->stp_state != BR_STATE_LEARNING &&
		    dp->stp_state != BR_STATE_FORWARDING)
			continue;
		if (!br_vlan_get_info_rcu(dp->user, vid, &vinfo))
			return dp->user;
	}
	return NULL;
}

static struct sk_buff *yt921x_8021q_rcv(struct sk_buff *skb,
					struct net_device *netdev)
{
	int src_port = -1, switch_id = -1, vbid = -1, vid = -1;

	/* The conduit is promiscuous for the sake of the tag_8021q VIDs,
	 * and a promiscuous MAC passes MAC control frames up as well.
	 */
	if (unlikely(eth_hdr(skb)->h_proto == htons(ETH_P_PAUSE)))
		return NULL;

	/* The generic decoder assumes a complete VLAN header. Reject plain
	 * Ethernet, old proprietary tags and truncated input before calling it.
	 */
	if (skb_vlan_tag_present(skb)) {
		if (skb->vlan_proto != htons(ETH_P_8021Q))
			return NULL;
	} else if (eth_hdr(skb)->h_proto != htons(ETH_P_8021Q) ||
		   !pskb_may_pull(skb, VLAN_HLEN)) {
		return NULL;
	}

	dsa_8021q_rcv(skb, &src_port, &switch_id, &vbid, &vid);

	/* Bridge VLAN metadata must reach a live member of that bridge,
	 * never an unrelated direct port VLAN upper with the same VID.
	 */
	if (vid >= 0 && !vid_is_dsa_8021q(vid) && skb_vlan_tag_present(skb))
		skb->dev = yt921x_8021q_find_bridge_vlan(netdev, vid);
	else
		skb->dev = dsa_tag_8021q_find_user(netdev, src_port, switch_id,
						   vid, vbid);
	if (!skb->dev ||
	    dsa_user_to_port(skb->dev)->cpu_dp != netdev->dsa_ptr) {
		net_warn_ratelimited("%s: dropped frame from %pM proto 0x%04x: src_port=%d switch_id=%d vid=%d vbid=%d\n",
				     netdev->name, eth_hdr(skb)->h_source,
				     ntohs(eth_hdr(skb)->h_proto), src_port,
				     switch_id, vid, vbid);
		return NULL;
	}

	dsa_default_offload_fwd_mark(skb);

	return skb;
}

static const struct dsa_device_ops yt921x_8021q_netdev_ops = {
	.name			= YT921X_8021Q_NAME,
	.proto			= DSA_TAG_PROTO_YT921X_8021Q,
	.xmit			= yt921x_8021q_xmit,
	.rcv			= yt921x_8021q_rcv,
	.needed_headroom	= VLAN_HLEN,
	.promisc_on_conduit	= true,
};

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DSA tag driver for Motorcomm YT921x switches, using VLAN");
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_YT921X_8021Q, YT921X_8021Q_NAME);

module_dsa_tag_driver(yt921x_8021q_netdev_ops);
