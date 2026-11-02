// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Experimental 802.1Q CPU transport for YT921x.
 * VLAN programming adapted from michioxd/openwrt-wf810df, commit
 * 63fc98585ad49fac9b43c9c2a4c30415a22b8edd. CR881x conduit policy and
 * boot-only selection are local. This is a single-tag transport, not QinQ.
 */
#include "yt921x_internal.h"

static u32 yt921x_8021q_cpu_block_mask(struct yt921x_priv *priv, int port)
{
	u32 allowed = yt921x_non_cpu_port_mask(priv);

	if (priv->dt_secondary_conduit_user_mask_valid) {
		if (yt921x_is_secondary_cpu_port(priv, port))
			allowed &= priv->dt_secondary_conduit_user_mask;
		else
			allowed &= ~priv->dt_secondary_conduit_user_mask;
	}

	return GENMASK(YT921X_PORT_NUM - 1, 0) & ~allowed;
}

static int yt921x_egress_ctag_mode_set(struct yt921x_priv *priv, int port,
				       u32 mode)
{
	return yt921x_reg_update_bits(priv,
				      YT921X_PORTn_EGR_VLAN_CTRL(port),
				      YT921X_PORT_EGR_VLAN_CTRL_CTAG_MODE_M,
				      YT921X_PORT_EGR_VLAN_CTRL_CTAG_MODE(mode));
}

static int yt921x_8021q_cpu_port_enter(struct yt921x_priv *priv, int port)
{
	u32 ctrl;
	int res;

	/* Leave the external CPU block disabled. While it is enabled, port 8
	 * is the CPU port and its egress path bypasses VLAN processing, so
	 * the conduit only ever receives untagged frames. Disabled, the port
	 * forwards through the VLAN table like any other external port.
	 */
	res = yt921x_reg_write(priv, YT921X_EXT_CPU_PORT, 0);
	if (res)
		return res;

	/* Egress TPID profile 0 is reset to 0, and every port selects it
	 * through PORTn_EGR. A zero TPID makes the switch strip the tag
	 * instead of writing one, so the CPU port would never see the
	 * tag_8021q VID.
	 */
	res = yt921x_reg_write(priv, YT921X_TPID_EGRn(0), 0x8100);
	if (res)
		return res;

	res = yt921x_egress_ctag_mode_set(priv, port,
					  YT921X_EGR_CTAG_MODE_ENTRY_BASED);
	if (res)
		return res;

	res = yt921x_vlan_aware_set(priv, port, true);
	if (res)
		return res;

	res = yt921x_port_set_pvid(priv, port, YT921X_VID_UNAWARE);
	if (res)
		return res;

	res = yt921x_reg_toggle_bits(priv, YT921X_VLAN_IGR_FILTER,
				     YT921X_VLAN_IGR_FILTER_PORTn(port), true);
	if (res)
		return res;

	/* VLAN membership and conduit isolation both constrain forwarding.
	 * Never forward between CPU ports or outside the board conduit mask.
	 */
	res = yt921x_reg_update_bits(priv, YT921X_PORTn_ISOLATION(port),
			GENMASK(YT921X_PORT_NUM - 1, 0),
			yt921x_8021q_cpu_block_mask(priv, port));
	if (res)
		return res;

	/* Frames from the host to a MAC the switch has not learned yet must
	 * flood: the bridge sends a single copy in the bridge VID.
	 */
	res = yt921x_reg_read(priv, YT921X_ACT_UNK_UCAST, &ctrl);
	if (res)
		return res;
	ctrl &= ~YT921X_ACT_UNK_ACTn_M(port);
	ctrl |= YT921X_ACT_UNK_ACTn_FORWARD(port);
	res = yt921x_reg_write(priv, YT921X_ACT_UNK_UCAST, ctrl);
	if (res)
		return res;

	res = yt921x_reg_read(priv, YT921X_ACT_UNK_MCAST, &ctrl);
	if (res)
		return res;
	ctrl &= ~YT921X_ACT_UNK_ACTn_M(port);
	ctrl |= YT921X_ACT_UNK_ACTn_FORWARD(port);

	return yt921x_reg_write(priv, YT921X_ACT_UNK_MCAST, ctrl);
}

/* A partial MDIO transaction cannot be rolled back reliably. Keep the
 * affected port closed until reboot, including across carrier changes.
 */
void yt921x_8021q_fault(struct yt921x_priv *priv, int port)
{
	int ingress, mac;

	priv->bridge_vlan_fault[port] = true;
	ingress = yt921x_reg_set_bits(priv, YT921X_PORTn_VLAN_CTRL1(port),
		YT921X_PORT_VLAN_CTRL1_CVLAN_DROP_TAGGED |
		YT921X_PORT_VLAN_CTRL1_CVLAN_DROP_UNTAGGED);
	mac = yt921x_reg_clear_bits(priv, YT921X_PORTn_CTRL(port),
		YT921X_PORT_RX_MAC_EN | YT921X_PORT_TX_MAC_EN);
	dev_err(yt921x_dev(priv),
		"port %d VLAN programming failed; reboot required (isolation ingress=%d MAC=%d)\n",
		port, ingress, mac);
}

static int yt921x_8021q_pvid_apply(struct yt921x_priv *priv, int port)
{
	u16 pvid = priv->bridge_vlan_filtering[port] ?
		priv->bridge_pvid[port] : priv->tag_8021q_pvid[port];
	u32 mask = YT921X_PORT_VLAN_CTRL1_CVLAN_DROP_UNTAGGED |
		   YT921X_PORT_VLAN_CTRL1_CVLAN_DROP_TAGGED;
	int res;

	if (priv->bridge_vlan_fault[port])
		return -EIO;
	res = yt921x_port_set_pvid(priv, port, pvid);
	if (res)
		return res;

	/* No PVID means untagged ingress must not enter any VLAN. */
	return yt921x_reg_update_bits(priv, YT921X_PORTn_VLAN_CTRL1(port),
				      mask, pvid ? 0 :
				      YT921X_PORT_VLAN_CTRL1_CVLAN_DROP_UNTAGGED);
}

static int yt921x_8021q_user_port_enter(struct yt921x_priv *priv, int port)
{
	int res;

	res = yt921x_egress_ctag_mode_set(priv, port,
					  YT921X_EGR_CTAG_MODE_ENTRY_BASED);
	if (res)
		return res;

	res = yt921x_vlan_aware_set(priv, port, true);
	if (res)
		return res;

	res = yt921x_8021q_pvid_apply(priv, port);
	if (res)
		return res;

	return yt921x_reg_toggle_bits(priv, YT921X_VLAN_IGR_FILTER,
				      YT921X_VLAN_IGR_FILTER_PORTn(port), true);
}

int yt921x_8021q_port_enter(struct yt921x_priv *priv, int port)
{
	if (dsa_is_cpu_port(&priv->ds, port))
		return yt921x_8021q_cpu_port_enter(priv, port);

	return yt921x_8021q_user_port_enter(priv, port);
}

int yt921x_tag_8021q_vlan_add(struct dsa_switch *ds, int port, u16 vid,
			      u16 flags)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	bool untagged = flags & BRIDGE_VLAN_INFO_UNTAGGED;
	bool pvid = flags & BRIDGE_VLAN_INFO_PVID;
	int res;

	mutex_lock(&priv->reg_lock);
	if (dsa_is_user_port(ds, port) && priv->bridge_vlan_filtering[port])
		res = 0;
	else
		res = yt921x_vlan_add(priv, port, vid, untagged);
	if (!res && pvid && dsa_is_user_port(ds, port)) {
		priv->tag_8021q_pvid[port] = vid;
		res = yt921x_8021q_pvid_apply(priv, port);
	}
	if (res)
		yt921x_8021q_fault(priv, port);
	mutex_unlock(&priv->reg_lock);

	return res;
}

int yt921x_tag_8021q_vlan_del(struct dsa_switch *ds, int port, u16 vid)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_vlan_del(priv, port, vid);
	if (!res && priv->tag_8021q_pvid[port] == vid) {
		priv->tag_8021q_pvid[port] = 0;
		res = yt921x_8021q_pvid_apply(priv, port);
	}
	if (res)
		yt921x_8021q_fault(priv, port);
	mutex_unlock(&priv->reg_lock);

	return res;
}

int yt921x_8021q_standalone(struct yt921x_priv *priv, int port)
{
	u32 mask;
	int res;

	res = yt921x_port_isolation_set(priv, port,
					yt921x_non_cpu_port_mask(priv));
	if (res)
		return res;

	mask = YT921X_PORT_LEARN_DIS;

	return yt921x_reg_set_bits(priv, YT921X_PORTn_LEARN(port), mask);
}

int yt921x_8021q_filtering(struct yt921x_priv *priv, int port, bool enable)
{
	bool old = priv->bridge_vlan_filtering[port];
	u16 internal_vid = priv->tag_8021q_pvid[port];
	u32 drop = YT921X_PORT_VLAN_CTRL1_CVLAN_DROP_TAGGED |
		   YT921X_PORT_VLAN_CTRL1_CVLAN_DROP_UNTAGGED;
	int vid, res;

	if (priv->bridge_vlan_fault[port])
		return -EIO;
	if (enable) {
		struct dsa_switch *ds = &priv->ds;
		struct dsa_port *dp = dsa_to_port(ds, port), *other;

		dsa_switch_for_each_user_port(other, ds) {
			if (other == dp || !priv->bridge_vlan_filtering[other->index] ||
			    (dp->bridge && other->bridge &&
			     dp->bridge->dev == other->bridge->dev))
				continue;
			for (vid = 1; vid < ARRAY_SIZE(priv->bridge_vlan_members); vid++)
				if ((priv->bridge_vlan_members[vid] & BIT(port)) &&
				    (priv->bridge_vlan_members[vid] & BIT(other->index)))
					return -EBUSY;
		}
	}

	res = yt921x_reg_set_bits(priv, YT921X_PORTn_VLAN_CTRL1(port), drop);
	if (res)
		return res;

	if (internal_vid) {
		res = yt921x_vlan_del(priv, port, internal_vid);
		if (res)
			return res;
	}
	for (vid = 1; vid < ARRAY_SIZE(priv->bridge_vlan_members); vid++) {
		if (!(priv->bridge_vlan_members[vid] & BIT(port)))
			continue;
		if (enable)
			res = yt921x_vlan_add(priv, port, vid,
				priv->bridge_vlan_untagged[vid] & BIT(port));
		else
			res = yt921x_vlan_del(priv, port, vid);
		if (res)
			return res;
	}
	if (!enable && internal_vid) {
		res = yt921x_vlan_add(priv, port, internal_vid, true);
		if (res)
			return res;
	}
	res = yt921x_fdb_flush_port(priv, port, false);
	if (res)
		return res;

	priv->bridge_vlan_filtering[port] = enable;
	res = yt921x_8021q_pvid_apply(priv, port);
	if (res)
		priv->bridge_vlan_filtering[port] = old;
	/* On an I/O failure ingress stays blocked, rather than leaking VLANs. */
	return res;
}

int yt921x_8021q_bridge_vlan(struct yt921x_priv *priv, int port,
				    const struct switchdev_obj_port_vlan *vlan,
				    bool add, struct netlink_ext_ack *extack)
{
	struct dsa_switch *ds = &priv->ds;
	struct dsa_port *dp = dsa_to_port(ds, port), *other;
	u16 vid = vlan->vid;
	u16 old_members, old_untagged, old_pvid;
	int res = 0;

	/* Direct port VLAN uppers have no bridge domain/source-port identity. */
	if (!vlan->obj.orig_dev) {
		NL_SET_ERR_MSG_MOD(extack, "Use bridge VLANs, not direct port VLAN uppers, with this tagger");
		return -EOPNOTSUPP;
	}
	if (!vid || vid >= ARRAY_SIZE(priv->bridge_vlan_members)) {
		NL_SET_ERR_MSG_MOD(extack, "VLAN IDs 3072-4095 are reserved for DSA transport");
		return -EBUSY;
	}

	mutex_lock(&priv->reg_lock);
	if (priv->bridge_vlan_fault[port]) {
		res = -EIO;
		goto out;
	}
	if (add && dsa_is_user_port(ds, port) && priv->bridge_vlan_filtering[port]) {
		/* A CPU frame carries only VID, not a bridge identifier. */
		dsa_switch_for_each_user_port(other, ds) {
			if (other == dp || !priv->bridge_vlan_filtering[other->index] ||
			    !(priv->bridge_vlan_members[vid] & BIT(other->index)))
				continue;
			if (!dp->bridge || !other->bridge ||
			    dp->bridge->dev != other->bridge->dev) {
				NL_SET_ERR_MSG_MOD(extack, "A VLAN ID cannot be shared by different bridge domains");
				res = -EBUSY;
				goto out;
			}
		}
	}

	old_members = priv->bridge_vlan_members[vid];
	old_untagged = priv->bridge_vlan_untagged[vid];
	old_pvid = priv->bridge_pvid[port];
	if (add) {
		priv->bridge_vlan_members[vid] |= BIT(port);
		if (!dsa_is_cpu_port(ds, port) &&
		    (vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED))
			priv->bridge_vlan_untagged[vid] |= BIT(port);
		else
			priv->bridge_vlan_untagged[vid] &= ~BIT(port);
	} else {
		priv->bridge_vlan_members[vid] &= ~BIT(port);
		priv->bridge_vlan_untagged[vid] &= ~BIT(port);
	}

	if (dsa_is_user_port(ds, port)) {
		if (add && (vlan->flags & BRIDGE_VLAN_INFO_PVID))
			priv->bridge_pvid[port] = vid;
		else if (old_pvid == vid)
			priv->bridge_pvid[port] = 0;
	}

	if (dsa_is_cpu_port(ds, port) || priv->bridge_vlan_filtering[port]) {
		if (add)
			res = yt921x_vlan_add(priv, port, vid,
				priv->bridge_vlan_untagged[vid] & BIT(port));
		else
			res = yt921x_vlan_del(priv, port, vid);
		if (!res && dsa_is_user_port(ds, port))
			res = yt921x_8021q_pvid_apply(priv, port);
		if (!res)
			res = yt921x_fdb_flush_port(priv, port, false);
	}
	if (res) {
		priv->bridge_vlan_members[vid] = old_members;
		priv->bridge_vlan_untagged[vid] = old_untagged;
		priv->bridge_pvid[port] = old_pvid;
		yt921x_8021q_fault(priv, port);
	}
out:
	mutex_unlock(&priv->reg_lock);
	return res;
}
