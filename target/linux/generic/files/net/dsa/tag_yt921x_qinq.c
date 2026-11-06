// SPDX-License-Identifier: GPL-2.0
/* QinQ transport for the Motorcomm YT921x switch.
 *
 * Data wire format:
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
 * Link-local control exception (DA 01:80:c2:00:00:00..0f):
 *   DA | SA | 9988 | 0000 | code/prio | tx port | [8100 | C-TCI] | ethertype
 *
 * These frames bypass the QinQ transport tags and carry the legacy 8-byte
 * YT921x CPU forwarding tag instead. Zero or one existing customer C-tag is
 * preserved verbatim; no S-tag or synthetic C-tag is added. This is a TX host
 * slowpath exception: data traffic retains the accelerated QinQ path.
 * Control TX uses CPU8, including WAN. Supported RMA RX copies may also
 * arrive there with the CPU port's private unaware CVID (4095). Only a
 * recognized control protocol may discard that private C-tag or cross
 * from CPU8 to a user port assigned to the other conduit.
 *
 * Only zero or one customer VLAN is supported, and CPU-originated bridge
 * traffic is replicated by Linux per destination port. Switch bridge joins
 * therefore have to leave tx_fwd_offload false; frames arriving with
 * offload_fwd_mark set are dropped rather than forwarded undefined.
 */
#include <linux/dsa/8021q.h>
#include <linux/dsa/yt921x_qinq.h>
#include <linux/etherdevice.h>
#include <linux/if_bridge.h>
#include <linux/if_vlan.h>
#include <net/dsa.h>

#include "tag.h"

#define YT921X_QINQ_NAME "yt921x-qinq"

#define YT921X_QINQ_CPU_TAG_LEN		8
#define YT921X_QINQ_CPU_TAG_CONTROL_PORT	8

#define YT921X_QINQ_CPU_TAG_PORT_EN		BIT(15)
#define YT921X_QINQ_CPU_TAG_PRIO_M		GENMASK(10, 8)
#define  YT921X_QINQ_CPU_TAG_PRIO(x)		FIELD_PREP(YT921X_QINQ_CPU_TAG_PRIO_M, (x))
#define YT921X_QINQ_CPU_TAG_CODE_EN		BIT(7)
#define YT921X_QINQ_CPU_TAG_CODE_M		GENMASK(6, 1)
#define  YT921X_QINQ_CPU_TAG_CODE(x)		FIELD_PREP(YT921X_QINQ_CPU_TAG_CODE_M, (x))
#define YT921X_QINQ_CPU_TAG_TX_PORTn(port)	BIT(port)

#define YT921X_QINQ_CPU_TAG_CODE_FORWARD	0x00

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

static bool yt921x_qinq_control_cpu(struct net_device *conduit,
				  struct dsa_port *dp)
{
	struct dsa_port *cpu = conduit->dsa_ptr;

	return cpu && cpu->ds == dp->ds &&
	       cpu->index == YT921X_QINQ_CPU_TAG_CONTROL_PORT &&
	       dsa_is_cpu_port(dp->ds, cpu->index) && cpu->conduit == conduit;
}

/* Called after validating the inner C-tag; skb->data starts at its TCI.
 * A link-local DA alone is insufficient: it can also carry ordinary data.
 */
static bool yt921x_qinq_is_rx_control(struct sk_buff *skb)
{
	const u8 *dest = eth_hdr(skb)->h_dest;
	__be16 proto = vlan_eth_hdr(skb)->h_vlan_encapsulated_proto;

	if (!is_link_local_ether_addr(dest))
		return false;

	switch (dest[ETH_ALEN - 1]) {
	case 0x00:
		return ntohs(proto) >= 3 && ntohs(proto) <= ETH_DATA_LEN &&
		       pskb_may_pull(skb, VLAN_HLEN + 3) &&
		       !memcmp(skb->data + VLAN_HLEN, "\x42\x42\x03", 3);
	case 0x02:
		return proto == htons(ETH_P_SLOW) &&
		       pskb_may_pull(skb, VLAN_HLEN + 2);
	case 0x03:
		return proto == htons(ETH_P_PAE) &&
		       pskb_may_pull(skb, VLAN_HLEN + 4);
	case 0x0e:
		return proto == htons(ETH_P_LLDP) &&
		       pskb_may_pull(skb, VLAN_HLEN + 2);
	default:
		return false;
	}
}

static struct dsa_port *yt921x_qinq_find_dp(struct net_device *conduit,
					    u16 outer_vid, bool control)
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

	if (dp->cpu_dp != conduit->dsa_ptr &&
	    !(control && yt921x_qinq_control_cpu(conduit, dp)))
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

/* Return 1 to strip the C-tag, 0 to retain it, or a negative error to drop.
 * Hardware RMA copies on CPU8 can use its private unaware VID even when
 * the source port has a real PVID. That value never admits ordinary data.
 */
static int yt921x_qinq_rx_vlan(struct dsa_port *dp, struct net_device *br,
			       u16 vid, bool control, bool control_cpu)
{
	struct bridge_vlan_info vinfo;
	u16 pvid = 0;

	if (control && control_cpu && vid == VLAN_VID_MASK)
		return 1;

	if (!br || !br_vlan_enabled(br))
		return yt921x_qinq_internal_vid(dp, br, vid) ? 1 : -EINVAL;

	if (control && br_vlan_get_pvid_rcu(dp->user, &pvid))
		return -EINVAL;

	if (control && control_cpu && !vid && !pvid)
		return 1;

	if (!dsa_yt921x_qinq_customer_vid_valid(dp->ds, vid))
		return -EINVAL;

	if (!control && dp->stp_state != BR_STATE_LEARNING &&
	    dp->stp_state != BR_STATE_FORWARDING)
		return -EINVAL;

	if (br_vlan_get_info_rcu(dp->user, vid, &vinfo))
		return -EINVAL;

	/* Native PVID tags are equivalent to untagged ingress. In particular,
	 * LLC/STP consumers need this artificial tag removed. Preserve an
	 * admitted non-PVID customer tag, and leave all data handling alone.
	 */
	return control && vid == pvid ? 1 : 0;
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
	struct vlan_ethhdr *veh;
	struct net_device *br;
	struct dsa_port *dp;
	bool control;
	u16 outer_tci;
	u16 inner_tci;
	u16 inner_vid;
	int strip;

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
	control = yt921x_qinq_is_rx_control(skb);

	dp = yt921x_qinq_find_dp(netdev, outer_tci & VLAN_VID_MASK, control);
	if (!dp)
		return NULL;

	br = dsa_port_bridge_dev_get(dp);
	strip = yt921x_qinq_rx_vlan(dp, br, inner_vid, control,
				  yt921x_qinq_control_cpu(netdev, dp));
	if (strip < 0 || (strip && yt921x_qinq_pop_inner(skb)))
		return NULL;

	skb->dev = dp->user;
	skb->priority = (outer_tci & VLAN_PRIO_MASK) >> VLAN_PRIO_SHIFT;
	if (control)
		skb->offload_fwd_mark = 0;
	else
		dsa_default_offload_fwd_mark(skb);

	return skb;
}

static bool yt921x_qinq_is_control_frame(struct sk_buff *skb)
{
	if (!pskb_may_pull(skb, ETH_HLEN))
		return false;

	return is_link_local_ether_addr(eth_hdr(skb)->h_dest);
}

/* Frame a link-local control packet with the legacy YT921x CPU tag. The
 * caller owns the skb on both success and error.
 */
static struct sk_buff *
yt921x_qinq_xmit_control(struct sk_buff *skb, struct dsa_port *dp)
{
	struct vlan_ethhdr *veh;
	__be16 *tag;
	u16 tx;

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
	} else if (yt921x_qinq_is_vlan_tpid(veh->h_vlan_proto)) {
		return NULL;
	}

	if (skb->ip_summed == CHECKSUM_PARTIAL && skb_checksum_help(skb))
		return NULL;

	if (unlikely(skb_cow_head(skb, YT921X_QINQ_CPU_TAG_LEN) < 0))
		return NULL;

	skb_push(skb, YT921X_QINQ_CPU_TAG_LEN);
	dsa_alloc_etype_header(skb, YT921X_QINQ_CPU_TAG_LEN);

	tag = dsa_etype_header_pos_tx(skb);

	tag[0] = htons(ETH_P_YT921X);
	tag[1] = 0;
	tag[2] = htons(YT921X_QINQ_CPU_TAG_CODE(YT921X_QINQ_CPU_TAG_CODE_FORWARD) |
		       YT921X_QINQ_CPU_TAG_CODE_EN |
		       YT921X_QINQ_CPU_TAG_PRIO(skb->priority));
	tx = YT921X_QINQ_CPU_TAG_PORT_EN |
	     YT921X_QINQ_CPU_TAG_TX_PORTn(dp->index);
	tag[3] = htons(tx);

	return skb;
}

/* The driver restricts QinQ to the CR881x CPU8/CPU4 topology. The
 * proprietary command parser is on CPU8, even for a WAN destination.
 * Clone secondary-conduit control TX so dsa_user_xmit still owns its skb;
 * dev_queue_xmit consumes only our clone. Data keeps its normal conduit.
 */
static struct sk_buff *
yt921x_qinq_xmit_control_port(struct sk_buff *skb, struct dsa_port *dp)
{
	struct dsa_switch *ds = dp->ds;
	struct dsa_port *cpu;
	struct sk_buff *control;

	if (ds->num_ports <= YT921X_QINQ_CPU_TAG_CONTROL_PORT ||
	    !dsa_is_cpu_port(ds, YT921X_QINQ_CPU_TAG_CONTROL_PORT))
		return NULL;
	cpu = dsa_to_port(ds, YT921X_QINQ_CPU_TAG_CONTROL_PORT);
	if (!cpu->conduit)
		return NULL;
	if (dp->cpu_dp == cpu)
		return yt921x_qinq_xmit_control(skb, dp);

	control = skb_clone(skb, GFP_ATOMIC);
	if (!control)
		return NULL;
	if (!yt921x_qinq_xmit_control(control, dp)) {
		kfree_skb(control);
		return NULL;
	}
	control->dev = cpu->conduit;
	dev_queue_xmit(control);
	return NULL;
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

	if (yt921x_qinq_is_control_frame(skb))
		return yt921x_qinq_xmit_control_port(skb, dp);

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
