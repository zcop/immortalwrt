// SPDX-License-Identifier: GPL-2.0
/* QinQ transport for the Motorcomm YT921x switch.
 *
 * Wire format:
 *   DA | SA | 88a8 | outer S-TCI | 8100 | inner C-TCI | ethertype | payload
 *
 * The outer S-tag is owned by the CPU transport. The switch inserts it on
 * ingress towards the CPU; Linux, or NSS/ECM in the accelerated path, inserts
 * it on egress. Its VID is always dsa_tag_8021q_standalone_vid(dp), which
 * identifies the exact switch and physical user port, never a bridge VID.
 *
 * The inner C-tag is the payload VLAN. In a VLAN-aware bridge it is the
 * customer tag, or the port PVID for untagged traffic. On standalone and
 * VLAN-unaware bridge ports it is the internal transport VLAN and is stripped
 * before the frame reaches Linux.
 *
 * Only zero or one customer VLAN is supported, and CPU-originated bridge
 * traffic is replicated by Linux per destination port. Switch bridge joins
 * therefore have to leave tx_fwd_offload false; frames arriving with
 * offload_fwd_mark set are dropped rather than forwarded undefined.
 */
#include <linux/dsa/8021q.h>
#include <linux/dsa/yt921x_qinq.h>
#include <linux/if_bridge.h>
#include <linux/if_vlan.h>
#include <net/dsa.h>

#include "tag.h"

#define YT921X_QINQ_NAME "yt921x-qinq"

static bool yt921x_qinq_is_vlan_tpid(__be16 proto)
{
	return eth_type_vlan(proto) ||
	       proto == htons(ETH_P_QINQ1) ||
	       proto == htons(ETH_P_QINQ2) ||
	       proto == htons(ETH_P_QINQ3);
}

/* Strip the outer S-tag, from either inline or hardware-accelerated metadata.
 * Returns with skb->data at the inner C-TCI and skb->protocol set to the
 * encapsulated EtherType.
 */
static int yt921x_qinq_pop_outer(struct sk_buff *skb, u16 *tci)
{
	if (skb_vlan_tag_present(skb)) {
		if (skb->vlan_proto != htons(ETH_P_8021AD))
			return -EINVAL;

		*tci = skb_vlan_tag_get(skb);
		__vlan_hwaccel_clear_tag(skb);
		return 0;
	}

	if (vlan_eth_hdr(skb)->h_vlan_proto != htons(ETH_P_8021AD))
		return -EINVAL;

	skb_push_rcsum(skb, ETH_HLEN);
	if (__skb_vlan_pop(skb, tci)) {
		skb_pull_rcsum(skb, ETH_HLEN);
		return -EINVAL;
	}
	skb_pull_rcsum(skb, ETH_HLEN);

	return 0;
}

static struct dsa_port *yt921x_qinq_find_dp(struct net_device *conduit,
					    u16 outer_vid)
{
	struct net_device *user;
	struct dsa_port *dp;
	int switch_id;
	int port;

	if (!vid_is_dsa_8021q(outer_vid))
		return NULL;

	switch_id = dsa_8021q_rx_switch_id(outer_vid);
	port = dsa_8021q_rx_source_port(outer_vid);

	user = dsa_conduit_find_user(conduit, switch_id, port);
	if (!user)
		return NULL;

	dp = dsa_user_to_port(user);

	if (dp->cpu_dp != conduit->dsa_ptr)
		return NULL;

	/* Exact match rejects VBID and reserved-bit aliases. */
	if (outer_vid != dsa_tag_8021q_standalone_vid(dp))
		return NULL;

	return dp;
}

static bool yt921x_qinq_internal_vid(struct dsa_port *dp,
				     struct net_device *br, u16 vid)
{
	if (br) {
		unsigned int bridge_num = dsa_port_bridge_num_get(dp);

		if (!bridge_num || bridge_num > DSA_TAG_8021Q_MAX_NUM_BRIDGES)
			return false;

		return vid == dsa_tag_8021q_bridge_vid(bridge_num);
	}

	return vid == dsa_tag_8021q_standalone_vid(dp);
}

static int yt921x_qinq_pop_inner(struct sk_buff *skb)
{
	u16 tci;

	skb_push_rcsum(skb, ETH_HLEN);
	if (__skb_vlan_pop(skb, &tci)) {
		skb_pull_rcsum(skb, ETH_HLEN);
		return -EINVAL;
	}
	skb_pull_rcsum(skb, ETH_HLEN);

	return 0;
}

static struct sk_buff *yt921x_qinq_rcv(struct sk_buff *skb,
				       struct net_device *netdev)
{
	struct bridge_vlan_info vinfo;
	struct vlan_ethhdr *veh;
	struct net_device *br;
	struct dsa_port *dp;
	bool vlan_aware;
	u16 outer_tci;
	u16 inner_tci;
	u16 inner_vid;

	/* The conduit is promiscuous, so MAC control frames reach us too. */
	if (unlikely(eth_hdr(skb)->h_proto == htons(ETH_P_PAUSE)))
		return NULL;

	if (yt921x_qinq_pop_outer(skb, &outer_tci))
		return NULL;

	if (!pskb_may_pull(skb, VLAN_HLEN))
		return NULL;

	veh = vlan_eth_hdr(skb);
	if (veh->h_vlan_proto != htons(ETH_P_8021Q))
		return NULL;

	if (yt921x_qinq_is_vlan_tpid(veh->h_vlan_encapsulated_proto))
		return NULL;

	inner_tci = ntohs(veh->h_vlan_TCI);
	inner_vid = inner_tci & VLAN_VID_MASK;

	dp = yt921x_qinq_find_dp(netdev, outer_tci & VLAN_VID_MASK);
	if (!dp)
		return NULL;

	br = dsa_port_bridge_dev_get(dp);
	vlan_aware = br && br_vlan_enabled(br);

	if (vlan_aware) {
		if (!dsa_yt921x_qinq_customer_vid_valid(dp->ds, inner_vid))
			return NULL;

		if (dp->stp_state != BR_STATE_LEARNING &&
		    dp->stp_state != BR_STATE_FORWARDING)
			return NULL;

		if (br_vlan_get_info_rcu(dp->user, inner_vid, &vinfo))
			return NULL;
	} else {
		if (!yt921x_qinq_internal_vid(dp, br, inner_vid))
			return NULL;

		if (yt921x_qinq_pop_inner(skb))
			return NULL;
	}

	skb->dev = dp->user;
	skb->priority = (outer_tci & VLAN_PRIO_MASK) >> VLAN_PRIO_SHIFT;
	dsa_default_offload_fwd_mark(skb);

	return skb;
}

static struct sk_buff *yt921x_qinq_xmit(struct sk_buff *skb,
					struct net_device *netdev)
{
	struct bridge_vlan_info vinfo;
	struct dsa_port *dp = dsa_user_to_port(netdev);
	struct vlan_ethhdr *veh;
	struct net_device *br;
	bool vlan_aware;
	u16 inner_vid;
	u8 pcp;

	/* On every error the caller (dsa_user_xmit) frees the original skb. */

	/* Integration prerequisite: the switch driver must not advertise
	 * tx_fwd_offload, so Linux replicates bridge traffic per port.
	 */
	if (unlikely(skb->offload_fwd_mark))
		return NULL;

	br = dsa_port_bridge_dev_get(dp);
	vlan_aware = br && br_vlan_enabled(br);

	/* Put hardware-accelerated customer tags inline before adding the
	 * outer tag, so the wire order is S-tag then C-tag.
	 */
	if (skb_vlan_tag_present(skb)) {
		if (skb->vlan_proto != htons(ETH_P_8021Q))
			return NULL;

		if (__vlan_insert_tag(skb, skb->vlan_proto,
				      skb_vlan_tag_get(skb)))
			return NULL;

		__vlan_hwaccel_clear_tag(skb);
		skb->protocol = htons(ETH_P_8021Q);
	}

	if (!pskb_may_pull(skb, ETH_HLEN))
		return NULL;

	veh = skb_vlan_eth_hdr(skb);
	if (veh->h_vlan_proto == htons(ETH_P_8021Q)) {
		if (!pskb_may_pull(skb, VLAN_ETH_HLEN))
			return NULL;

		veh = skb_vlan_eth_hdr(skb);
		if (yt921x_qinq_is_vlan_tpid(veh->h_vlan_encapsulated_proto))
			return NULL;

		inner_vid = ntohs(veh->h_vlan_TCI) & VLAN_VID_MASK;

		/* Tagged traffic on standalone or unaware bridge ports is
		 * deliberately unsupported at this stage.
		 */
		if (!vlan_aware)
			return NULL;

		if (!dsa_yt921x_qinq_customer_vid_valid(dp->ds, inner_vid))
			return NULL;

		if (br_vlan_get_info_rcu(dp->user, inner_vid, &vinfo))
			return NULL;
	} else {
		if (yt921x_qinq_is_vlan_tpid(veh->h_vlan_proto))
			return NULL;

		if (vlan_aware) {
			u16 pvid;

			if (br_vlan_get_pvid_rcu(dp->user, &pvid) ||
			    !dsa_yt921x_qinq_customer_vid_valid(dp->ds, pvid))
				return NULL;

			if (br_vlan_get_info_rcu(dp->user, pvid, &vinfo))
				return NULL;

			inner_vid = pvid;
		} else if (br) {
			unsigned int bridge_num;

			bridge_num = dsa_port_bridge_num_get(dp);
			if (!bridge_num ||
			    bridge_num > DSA_TAG_8021Q_MAX_NUM_BRIDGES)
				return NULL;

			inner_vid = dsa_tag_8021q_bridge_vid(bridge_num);
		} else {
			inner_vid = dsa_tag_8021q_standalone_vid(dp);
		}

		if (__vlan_insert_tag(skb, htons(ETH_P_8021Q), inner_vid))
			return NULL;

		skb->protocol = htons(ETH_P_8021Q);
	}

	pcp = netdev_txq_to_tc(netdev, skb_get_queue_mapping(skb));

	/* Leave skb->protocol at the inner tag, matching the existing DSA
	 * EtherType-tagger convention; the inline header carries 0x88a8.
	 */
	if (__vlan_insert_tag(skb, htons(ETH_P_8021AD),
			      ((pcp & 0x7) << VLAN_PRIO_SHIFT) |
			      dsa_tag_8021q_standalone_vid(dp)))
		return NULL;

	return skb;
}

static const struct dsa_device_ops yt921x_qinq_netdev_ops = {
	.name			= YT921X_QINQ_NAME,
	.proto			= DSA_TAG_PROTO_YT921X_QINQ,
	.xmit			= yt921x_qinq_xmit,
	.rcv			= yt921x_qinq_rcv,
	.needed_headroom	= 2 * VLAN_HLEN,
	.promisc_on_conduit	= true,
};

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DSA QinQ tag driver for Motorcomm YT921x switches");
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_YT921X_QINQ, YT921X_QINQ_NAME);

module_dsa_tag_driver(yt921x_qinq_netdev_ops);
