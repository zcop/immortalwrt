/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Internal split unit for yt921x.c
 *
 * Copyright (c) 2025 David Yang
 * Copyright (c) 2026 zcop <hongson.hn@gmail.com>
 */

static int yt921x_port_setup(struct yt921x_priv *priv, int port)
{
	struct dsa_switch *ds = &priv->ds;
	u32 ctrl;
	int res;

	res = yt921x_userport_standalone(priv, port);
	if (res)
		return res;

	/* Clear prio order (even if DCB is not enabled) to avoid unsolicited
	 * priorities
	 */
	res = yt921x_reg_write(priv, YT921X_PORTn_PRIO_ORD(port), 0);
	if (res)
		return res;

	if (dsa_is_cpu_port(ds, port)) {
		if (yt921x_is_primary_cpu_port(priv, port)) {
			/* Primary conduit uses the YT921X tag format, so keep
			 * untagged egress from this CPU port blocked.
			 */
			/* Block all 11 modeled destinations, preserve unknown
			 * upper bits for forward compatibility.
			 */
			ctrl = GENMASK(YT921X_PORT_NUM - 1, 0);
			res = yt921x_reg_write(priv, YT921X_PORTn_ISOLATION(port),
					       ctrl);
			if (res)
				return res;
		} else {
			/* Secondary conduit carries plain Ethernet/802.1Q, so
			 * keep non-CPU destinations open and let VLAN tables
			 * steer traffic.
			 */
			res = yt921x_port_isolation_set(priv, port, 0);
			if (res)
				return res;

			/* Prevent primary-CPU sourced frames from spilling into the
			 * secondary conduit path.
			 */
			res = yt921x_reg_set_bits(priv,
						  YT921X_PORTn_ISOLATION(port),
						  BIT(priv->primary_cpu_port));
			if (res)
				return res;
		}

		/* To simplify FDB "isolation" simulation, we also disable
		 * learning on the CPU port, and let software identify packets
		 * towarding CPU (either trapped or a static FDB entry is
		 * matched, no matter which bridge that entry is for), which is
		 * already done by yt921x_userport_standalone(). As a result,
		 * VLAN-awareness becomes unrelated on the CPU port (set to
		 * VLAN-unaware by the way).
		 */
	}

	if (dsa_is_user_port(ds, port)) {
		int cpu_port = dsa_upstream_port(ds, port);

		res = yt921x_userport_cpu_isolation_set(priv, port, cpu_port);
		if (res)
			return res;

		res = yt921x_secondary_cpu_isolation_sync(priv, cpu_port);
		if (res)
			return res;
	}

	/* Apply full board override once during setup (before link events). */
	res = yt921x_port_ctrl_apply_dt(priv, port, true);
	if (res)
		return res;

	return 0;
}

static enum dsa_tag_protocol
yt921x_dsa_get_tag_protocol(struct dsa_switch *ds, int port,
			    enum dsa_tag_protocol m)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);

	if (dsa_is_cpu_port(ds, port) &&
	    yt921x_is_secondary_cpu_port(priv, port))
		return DSA_TAG_PROTO_NONE;

	return DSA_TAG_PROTO_YT921X;
}

static int yt921x_dsa_port_setup(struct dsa_switch *ds, int port)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_port_setup(priv, port);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int
yt921x_dsa_port_change_conduit(struct dsa_switch *ds, int port,
			       struct net_device *conduit,
			       struct netlink_ext_ack *extack)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	struct dsa_port *dp = dsa_to_port(ds, port);
	struct dsa_port *user_dp;
	int old_cpu_port;
	int new_cpu_port;
	u32 val;
	int res;

	if (!dsa_is_user_port(ds, port))
		return -EOPNOTSUPP;

	new_cpu_port = yt921x_dsa_conduit_to_cpu_port(ds, conduit, extack);
	if (new_cpu_port < 0)
		return new_cpu_port;

	if (!dp->cpu_dp) {
		NL_SET_ERR_MSG_MOD(extack, "User port has no previous CPU port");
		return -EINVAL;
	}

	mutex_lock(&priv->reg_lock);
	res = yt921x_userport_current_cpu_port_get(priv, port, &old_cpu_port);
	if (res) {
		/* Fallback when hardware state is not yet canonical. */
		old_cpu_port = dp->cpu_dp->index;
		res = 0;
	}

	/* Secondary conduit receives untagged Ethernet frames. The tagger can
	 * only steer those frames back to one user port, so reject mappings
	 * that would place multiple user ports on the secondary conduit.
	 */
	if (yt921x_is_secondary_cpu_port(priv, new_cpu_port) &&
	    new_cpu_port != old_cpu_port) {
		dsa_switch_for_each_user_port(user_dp, ds) {
			if (user_dp->index == port)
				continue;
			if (dsa_upstream_port(ds, user_dp->index) != new_cpu_port)
				continue;

			NL_SET_ERR_MSG_MOD(extack,
					   "secondary conduit supports only one user port");
			res = -EOPNOTSUPP;
			goto out_unlock;
		}
	}

	res = yt921x_userport_cpu_isolation_set(priv, port, new_cpu_port);
	if (!res && new_cpu_port != old_cpu_port)
		/* Ensure the moved user port always unblocks new CPU and blocks old
		 * CPU explicitly, even if prior state was non-canonical.
		 */
		res = yt921x_reg_update_bits(priv, YT921X_PORTn_ISOLATION(port),
					     BIT(old_cpu_port) | BIT(new_cpu_port),
					     BIT(old_cpu_port));

	if (!res && new_cpu_port != old_cpu_port)
		res = yt921x_reg_read(priv, YT921X_PORTn_ISOLATION(new_cpu_port),
				      &val);
	if (!res && new_cpu_port != old_cpu_port)
		res = yt921x_reg_write(priv, YT921X_PORTn_ISOLATION(new_cpu_port),
				       val & ~BIT(port));
	if (!res && new_cpu_port != old_cpu_port)
		res = yt921x_reg_read(priv, YT921X_PORTn_ISOLATION(old_cpu_port),
				      &val);
	if (!res && new_cpu_port != old_cpu_port)
		res = yt921x_reg_write(priv, YT921X_PORTn_ISOLATION(old_cpu_port),
				       val | BIT(port));
	if (!res && new_cpu_port != old_cpu_port)
		res = yt921x_secondary_cpu_isolation_sync(priv, new_cpu_port);
	if (!res && new_cpu_port != old_cpu_port)
		res = yt921x_secondary_cpu_isolation_sync(priv, old_cpu_port);
	if (!res)
		res = yt921x_conduit_fdb_retarget(priv, port, old_cpu_port,
						  new_cpu_port);
	if (!res) {
		res = yt921x_reg_update_bits(priv, YT921X_EXT_CPU_PORT,
					     YT921X_EXT_CPU_PORT_PORT_M,
					     YT921X_EXT_CPU_PORT_PORT(new_cpu_port));
		if (!res) {
			res = yt921x_reg_read(priv, YT921X_EXT_CPU_PORT, &val);
			if (!res)
				dev_info(ds->dev,
					 "port%d conduit cpu%d->cpu%d ext_cpu=%lu\n",
					 port, old_cpu_port, new_cpu_port,
					 FIELD_GET(YT921X_EXT_CPU_PORT_PORT_M, val));
		}
	}
out_unlock:
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int yt921x_dsa_port_enable(struct dsa_switch *ds, int port,
				  struct phy_device *phydev)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	u32 mask = YT921X_PORT_CTRL_ADMIN_M;
	int res;

	if (!dsa_is_user_port(ds, port))
		return 0;

	mutex_lock(&priv->reg_lock);
	res = yt921x_reg_set_bits(priv, YT921X_PORTn_CTRL(port), mask);
	if (!res)
		res = yt921x_port_ctrl_apply_dt(priv, port, false);
	mutex_unlock(&priv->reg_lock);

	if (!res && phydev)
		phy_support_asym_pause(phydev);

	if (!res)
		WRITE_ONCE(priv->ports[port].port_up, true);

	return res;
}

static void yt921x_dsa_port_disable(struct dsa_switch *ds, int port)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	u32 stp_ctrl;
	u32 stp_mask;
	int res;

	if (!dsa_is_user_port(ds, port))
		return;

	WRITE_ONCE(priv->ports[port].port_up, false);
	cancel_delayed_work_sync(&priv->ports[port].mib_read);

	mutex_lock(&priv->reg_lock);
	do {
		res = yt921x_stp_encode_state(port, BR_STATE_BLOCKING, &stp_ctrl);
		if (res)
			break;

		stp_mask = YT921X_STP_PORTn_M(port);
		res = yt921x_reg_update_bits(priv, YT921X_STPn(0), stp_mask,
					     stp_ctrl);
		if (res)
			break;

		res = yt921x_reg_set_bits(priv, YT921X_PORTn_LEARN(port),
					  YT921X_PORT_LEARN_DIS);
		if (res)
			break;

		res = yt921x_port_down(priv, port);
	} while (0);
	mutex_unlock(&priv->reg_lock);

	if (res)
		dev_err(ds->dev, "Failed to disable port %d: %d\n", port, res);
}

/* Not "port" - DSCP mapping is global */
static int __maybe_unused
yt921x_dsa_port_get_dscp_prio(struct dsa_switch *ds, int port, u8 dscp)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	u32 val;
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_reg_read(priv, YT921X_IPM_DSCPn(dscp), &val);
	mutex_unlock(&priv->reg_lock);

	if (res)
		return res;

	return FIELD_GET(YT921X_IPM_PRIO_M, val);
}

static int __maybe_unused
yt921x_dsa_port_del_dscp_prio(struct dsa_switch *ds, int port, u8 dscp, u8 prio)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	u32 val;
	int res;

	mutex_lock(&priv->reg_lock);
	/* During a "dcb app replace" command, the new app table entry will be
	 * added first, then the old one will be deleted. But the hardware only
	 * supports one QoS class per DSCP value (duh), so if we blindly delete
	 * the app table entry for this DSCP value, we end up deleting the
	 * entry with the new priority. Avoid that by checking whether user
	 * space wants to delete the priority which is currently configured, or
	 * something else which is no longer current.
	 */
	res = yt921x_reg_read(priv, YT921X_IPM_DSCPn(dscp), &val);
	if (!res && FIELD_GET(YT921X_IPM_PRIO_M, val) == prio)
		res = yt921x_reg_write(priv, YT921X_IPM_DSCPn(dscp),
				       YT921X_IPM_PRIO(IEEE8021Q_TT_BK));
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int __maybe_unused
yt921x_dsa_port_add_dscp_prio(struct dsa_switch *ds, int port, u8 dscp, u8 prio)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	int res;

	if (prio >= YT921X_PRIO_NUM)
		return -EINVAL;

	mutex_lock(&priv->reg_lock);
	res = yt921x_reg_write(priv, YT921X_IPM_DSCPn(dscp),
			       YT921X_IPM_PRIO(prio));
	if (!res) {
		for (u8 dp = 0; dp < YT921X_QOS_REMARK_DP_NUM; dp++) {
			res = yt921x_qos_remark_dscp_set(priv, prio, dp, dscp);
			if (res)
				break;
		}
	}
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int yt921x_edata_wait(struct yt921x_priv *priv, u32 *valp)
{
	u32 val = YT921X_EDATA_DATA_IDLE;
	int res;

	res = yt921x_reg_wait(priv, YT921X_EDATA_DATA,
			      YT921X_EDATA_DATA_STATUS_M, &val);
	if (res)
		return res;

	*valp = val;
	return 0;
}

static int
yt921x_edata_read_cont(struct yt921x_priv *priv, u8 addr, u8 *valp)
{
	u32 ctrl;
	u32 val;
	int res;

	ctrl = YT921X_EDATA_CTRL_ADDR(addr) | YT921X_EDATA_CTRL_READ;
	res = yt921x_reg_write(priv, YT921X_EDATA_CTRL, ctrl);
	if (res)
		return res;
	res = yt921x_edata_wait(priv, &val);
	if (res)
		return res;

	*valp = FIELD_GET(YT921X_EDATA_DATA_DATA_M, val);
	return 0;
}

static int yt921x_edata_read(struct yt921x_priv *priv, u8 addr, u8 *valp)
{
	u32 val;
	int res;

	res = yt921x_edata_wait(priv, &val);
	if (res)
		return res;
	return yt921x_edata_read_cont(priv, addr, valp);
}

static int yt921x_chip_detect(struct yt921x_priv *priv)
{
	struct device *dev = yt921x_dev(priv);
	const struct yt921x_info *info;
	u8 extmode;
	u32 chipid;
	u32 major;
	u32 mode;
	u32 val;
	int res;

	res = yt921x_reg_read(priv, YT921X_CHIP_ID, &chipid);
	if (res)
		return res;

	major = FIELD_GET(YT921X_CHIP_ID_MAJOR, chipid);

	for (info = yt921x_infos; info->name; info++)
		if (info->major == major)
			break;
	if (!info->name) {
		dev_err(dev, "Unexpected chipid 0x%x\n", chipid);
		return -ENODEV;
	}

	res = yt921x_reg_read(priv, YT921X_CHIP_MODE, &mode);
	if (res)
		return res;
	res = yt921x_edata_read(priv, YT921X_EDATA_EXTMODE, &extmode);
	if (res)
		return res;

	for (; info->name; info++)
		if (info->major == major && info->mode == mode &&
		    info->extmode == extmode)
			break;
	if (!info->name) {
		dev_err(dev,
			"Unsupported chipid 0x%x with chipmode 0x%x 0x%x\n",
			chipid, mode, extmode);
		return -ENODEV;
	}

	res = yt921x_reg_read(priv, YT921X_SYS_CLK, &val);
	if (res)
		return res;
	switch (FIELD_GET(YT921X_SYS_CLK_SEL_M, val)) {
	case 0:
		priv->cycle_ns = info->major == YT9215_MAJOR ? 8 : 6;
		break;
	case YT921X_SYS_CLK_143M:
		priv->cycle_ns = 7;
		break;
	default:
		priv->cycle_ns = 8;
	}

	/* Print chipid here since we are interested in lower 16 bits */
	dev_info(dev,
		 "Motorcomm %s ethernet switch, chipid: 0x%x, chipmode: 0x%x 0x%x\n",
		 info->name, chipid, mode, extmode);

	priv->info = info;
	return 0;
}

static int yt921x_chip_reset(struct yt921x_priv *priv)
{
	struct device *dev = yt921x_dev(priv);
	u16 eth_p_tag;
	u32 val;
	int res;

	res = yt921x_chip_detect(priv);
	if (res)
		return res;

	/* Reset */
	res = yt921x_reg_write(priv, YT921X_RST, YT921X_RST_HW);
	if (res)
		return res;

	/* RST_HW is almost same as GPIO hard reset, so we need this delay. */
	fsleep(YT921X_RST_DELAY_US);

	val = 0;
	res = yt921x_reg_wait(priv, YT921X_RST, ~0, &val);
	if (res)
		return res;

	/* Check for tag EtherType; do it after reset in case you messed it up
	 * before.
	 */
	res = yt921x_reg_read(priv, YT921X_CPU_TAG_TPID, &val);
	if (res)
		return res;
	eth_p_tag = FIELD_GET(YT921X_CPU_TAG_TPID_TPID_M, val);
	if (eth_p_tag != ETH_P_YT921X) {
		dev_err(dev, "Tag type 0x%x != 0x%x\n", eth_p_tag,
			ETH_P_YT921X);
		/* Despite being possible, we choose not to set CPU_TAG_TPID,
		 * since there is no way it can be different unless you have the
		 * wrong chip.
		 */
		return -EINVAL;
	}

	return 0;
}

static bool yt921x_mdio_phyid_valid(u16 id1, u16 id2)
{
	/* 0x1140/0x1140 is a known pseudo responder pattern on non-PHY MBUS
	 * paths; exclude it from the valid responder mask.
	 */
	if (id1 == 0x1140 && id2 == 0x1140)
		return false;

	return id1 != 0x0000 && id1 != 0xffff &&
	       id2 != 0x0000 && id2 != 0xffff;
}

static int yt921x_validate_setup_locked(struct yt921x_priv *priv)
{
	struct device *dev = yt921x_dev(priv);
	int cpu_port = priv->primary_cpu_port;
	u16 tpid;
	u32 val;
	int res;

	if (cpu_port < 0)
		return -EINVAL;

	res = yt921x_reg_read(priv, YT921X_EXT_CPU_PORT, &val);
	if (res)
		return res;

	if (!(val & YT921X_EXT_CPU_PORT_TAG_EN) ||
	    !(val & YT921X_EXT_CPU_PORT_PORT_EN) ||
	    FIELD_GET(YT921X_EXT_CPU_PORT_PORT_M, val) != cpu_port)
		dev_warn(dev,
			 "EXT_CPU_PORT unexpected: val=0x%08x expected tag+port_en+port=%d\n",
			 val, cpu_port);

	res = yt921x_reg_read(priv, YT921X_CPU_TAG_TPID, &val);
	if (res)
		return res;

	tpid = FIELD_GET(YT921X_CPU_TAG_TPID_TPID_M, val);
	if (tpid != ETH_P_YT921X)
		dev_warn(dev, "CPU_TAG_TPID unexpected: 0x%04x (expected 0x%04x)\n",
			 tpid, ETH_P_YT921X);

	if (cpu_port >= 8 && cpu_port <= 9) {
		res = yt921x_reg_read(priv, YT921X_XMIIn(cpu_port), &val);
		if (res)
			return res;

		if (val == 0xdeadbeef)
			dev_warn(dev, "XMIIn(cpu=%d) appears gated: 0x%08x\n",
				 cpu_port, val);
	}

	return 0;
}

static void yt921x_log_mdio_summary_locked(struct yt921x_priv *priv)
{
	struct device *dev = yt921x_dev(priv);
	unsigned long int_valid_mask = 0;
	unsigned long int_placeholder_mask = 0;
	u32 ext_valid_mask = 0;
	u16 id1;
	u16 id2;
	int port;
	int res;

	if (priv->mbus_int) {
		for (port = 0; port < YT921X_PORT_NUM; port++) {
			res = yt921x_intif_read(priv, port, MII_PHYSID1, &id1);
			if (res)
				continue;
			res = yt921x_intif_read(priv, port, MII_PHYSID2, &id2);
			if (res)
				continue;

			if (id1 == 0x1140 && id2 == 0x1140)
				__set_bit(port, &int_placeholder_mask);
			else if (yt921x_mdio_phyid_valid(id1, id2))
				__set_bit(port, &int_valid_mask);
		}

		dev_info(dev,
			 "mdio-int responders: phyid_mask=0x%03lx placeholder_mask=0x%03lx\n",
			 int_valid_mask, int_placeholder_mask);
	}

	if (priv->mbus_ext) {
		for (port = 0; port < 32; port++) {
			res = yt921x_extif_read(priv, port, MII_PHYSID1, &id1);
			if (res)
				continue;
			res = yt921x_extif_read(priv, port, MII_PHYSID2, &id2);
			if (res)
				continue;

			if (yt921x_mdio_phyid_valid(id1, id2))
				ext_valid_mask |= BIT(port);
		}

		if (ext_valid_mask)
			dev_info(dev, "mdio-ext responders: phyid_mask=0x%08x\n",
				 ext_valid_mask);
		else
			dev_info(dev,
				 "mdio-ext responders: none detected on ports 0..31\n");
	}
}

static int yt921x_qos_remark_index(u8 prio, u8 dp, bool spri, u8 *idxp)
{
	u8 idx;

	if (prio >= YT921X_PRIO_NUM || dp >= YT921X_QOS_REMARK_DP_NUM)
		return -ERANGE;

	idx = (prio << 2) | dp;
	if (spri)
		idx |= BIT(5);
	*idxp = idx;
	return 0;
}

static int yt921x_qos_remark_dscp_set(struct yt921x_priv *priv, u8 prio, u8 dp,
				      u8 dscp)
{
	u8 idx;
	int res;

	if (dscp >= DSCP_MAX)
		return -ERANGE;

	res = yt921x_qos_remark_index(prio, dp, false, &idx);
	if (res)
		return res;

	return yt921x_reg_update_bits(priv, YT921X_QOS_REMARK_DSCPn(idx),
				      YT921X_QOS_REMARK_DSCP_VAL_M,
				      YT921X_QOS_REMARK_DSCP_VAL(dscp));
}

static int yt921x_qos_remark_prio_set(struct yt921x_priv *priv, u8 prio, u8 dp,
				      bool spri, u8 pcp, bool enable)
{
	u8 idx;
	u32 val;
	int res;

	if (pcp >= YT921X_PRIO_NUM)
		return -ERANGE;

	res = yt921x_qos_remark_index(prio, dp, spri, &idx);
	if (res)
		return res;

	val = YT921X_QOS_REMARK_PRIO_VAL(pcp);
	if (enable)
		val |= YT921X_QOS_REMARK_PRIO_EN;

	return yt921x_reg_update_bits(priv, YT921X_QOS_REMARK_PRIOn(idx),
				      YT921X_QOS_REMARK_PRIO_VAL_M |
				      YT921X_QOS_REMARK_PRIO_EN, val);
}

static int yt921x_qos_remark_port_enable(struct yt921x_priv *priv, int port,
					 bool cpri_enable, bool spri_enable)
{
	u32 val = 0;

	if (port < 0 || port >= YT921X_PORT_NUM)
		return -ERANGE;

	if (cpri_enable)
		val |= YT921X_QOS_REMARK_PORT_CPRI_EN;
	if (spri_enable)
		val |= YT921X_QOS_REMARK_PORT_SPRI_EN;

	return yt921x_reg_update_bits(priv, YT921X_QOS_REMARK_PORT_CTRLn(port),
				      YT921X_QOS_REMARK_PORT_CPRI_EN |
				      YT921X_QOS_REMARK_PORT_SPRI_EN, val);
}

#if IS_ENABLED(CONFIG_NET_DSA_YT921X_DEBUG)
enum {
	YT921X_INIT_DBG_2C_START = 0x2c0100,
	YT921X_INIT_DBG_2C_END = 0x2c013c,
	YT921X_INIT_DBG_2C_WORDS =
		((YT921X_INIT_DBG_2C_END - YT921X_INIT_DBG_2C_START) / 4) + 1,
	YT921X_INIT_DBG_31C_START = 0x31c000,
	YT921X_INIT_DBG_31C_END = 0x31c0fc,
	YT921X_INIT_DBG_31C_WORDS =
		((YT921X_INIT_DBG_31C_END - YT921X_INIT_DBG_31C_START) / 4) + 1,
};

static const u32 yt921x_init_dbg_watch_regs[] = {
	YT921X_FUNC,
	YT921X_EXT_CPU_PORT,
	YT921X_SERDES_CTRL,
	YT921X_SERDESn(8),
	YT921X_SERDESn(9),
	YT921X_XMII_CTRL,
	YT921X_XMIIn(8),
	YT921X_XMIIn(9),
	YT921X_MIB_CTRL,
	YT921X_CPU_COPY,
	YT921X_FILTER_UNK_UCAST,
	YT921X_FILTER_UNK_MCAST,
	YT921X_ACT_UNK_UCAST,
	YT921X_ACT_UNK_MCAST,
};

struct yt921x_init_dbg_snapshot {
	u32 win2c[YT921X_INIT_DBG_2C_WORDS];
	int win2c_err[YT921X_INIT_DBG_2C_WORDS];
	u32 win31c[YT921X_INIT_DBG_31C_WORDS];
	int win31c_err[YT921X_INIT_DBG_31C_WORDS];
	u32 watch[ARRAY_SIZE(yt921x_init_dbg_watch_regs)];
	int watch_err[ARRAY_SIZE(yt921x_init_dbg_watch_regs)];
};

static void
yt921x_debug_snapshot_window_locked(struct yt921x_priv *priv, u32 start, u32 end,
				       u32 *vals, int *errs)
{
	u32 reg;
	int i = 0;

	for (reg = start; reg <= end; reg += 4, i++) {
		errs[i] = yt921x_reg_read(priv, reg, &vals[i]);
		if (errs[i])
			vals[i] = 0;
	}
}

static void
yt921x_debug_snapshot_regs_locked(struct yt921x_priv *priv, const u32 *regs,
				  unsigned int n, u32 *vals, int *errs)
{
	unsigned int i;

	for (i = 0; i < n; i++) {
		errs[i] = yt921x_reg_read(priv, regs[i], &vals[i]);
		if (errs[i])
			vals[i] = 0;
	}
}

static int
yt921x_debug_log_reg_delta_locked(struct yt921x_priv *priv, const char *stage,
				  const u32 *regs, unsigned int n,
				  const u32 *before, const int *before_err,
				  const u32 *after, const int *after_err,
				  const char *label)
{
	struct device *dev = yt921x_dev(priv);
	unsigned int i;
	int changed = 0;

	for (i = 0; i < n; i++) {
		u32 reg = regs[i];

		if (before_err[i] || after_err[i]) {
			if (before_err[i] != after_err[i]) {
				dev_dbg(dev,
					 "init-delta %-12s %-6s reg 0x%06x err %d -> %d\n",
					 stage, label, reg, before_err[i],
					 after_err[i]);
				changed++;
			}
			continue;
		}

		if (before[i] == after[i])
			continue;

		dev_dbg(dev,
			 "init-delta %-12s %-6s reg 0x%06x 0x%08x -> 0x%08x\n",
			 stage, label, reg, before[i], after[i]);
		changed++;
	}

	return changed;
}

static int
yt921x_debug_log_window_delta_locked(struct yt921x_priv *priv, const char *stage,
				      const char *label, u32 start, u32 end,
				      const u32 *before, const int *before_err,
				      const u32 *after, const int *after_err)
{
	u32 reg;
	int changed = 0;
	unsigned int i = 0;

	for (reg = start; reg <= end; reg += 4, i++)
		changed += yt921x_debug_log_reg_delta_locked(priv, stage, &reg, 1,
							     &before[i], &before_err[i],
							     &after[i], &after_err[i],
							     label);

	return changed;
}

static void
yt921x_debug_init_snapshot_take_locked(struct yt921x_priv *priv,
				       struct yt921x_init_dbg_snapshot *snap)
{
	yt921x_debug_snapshot_window_locked(priv, YT921X_INIT_DBG_2C_START,
					    YT921X_INIT_DBG_2C_END,
					    snap->win2c, snap->win2c_err);
	yt921x_debug_snapshot_window_locked(priv, YT921X_INIT_DBG_31C_START,
					    YT921X_INIT_DBG_31C_END,
					    snap->win31c, snap->win31c_err);
	yt921x_debug_snapshot_regs_locked(priv, yt921x_init_dbg_watch_regs,
					  ARRAY_SIZE(yt921x_init_dbg_watch_regs),
					  snap->watch, snap->watch_err);
}

static void
yt921x_debug_init_checkpoint_locked(struct yt921x_priv *priv,
				    struct yt921x_init_dbg_snapshot *prev,
				    const char *stage)
{
	struct device *dev = yt921x_dev(priv);
	struct yt921x_init_dbg_snapshot now;
	int changed_watch;
	int changed_2c;
	int changed_31c;

	yt921x_debug_init_snapshot_take_locked(priv, &now);

	changed_watch = yt921x_debug_log_reg_delta_locked(
		priv, stage, yt921x_init_dbg_watch_regs,
		ARRAY_SIZE(yt921x_init_dbg_watch_regs), prev->watch,
		prev->watch_err, now.watch, now.watch_err, "watch");
	changed_2c = yt921x_debug_log_window_delta_locked(
		priv, stage, "0x2c01", YT921X_INIT_DBG_2C_START,
		YT921X_INIT_DBG_2C_END, prev->win2c, prev->win2c_err,
		now.win2c, now.win2c_err);
	changed_31c = yt921x_debug_log_window_delta_locked(
		priv, stage, "0x31c0", YT921X_INIT_DBG_31C_START,
		YT921X_INIT_DBG_31C_END, prev->win31c, prev->win31c_err,
		now.win31c, now.win31c_err);

	dev_dbg(dev,
		 "init-delta %-12s summary watch=%d 0x2c01=%d 0x31c0=%d\n",
		 stage, changed_watch, changed_2c, changed_31c);
	*prev = now;
}
#endif

static int yt921x_chip_setup_dsa(struct yt921x_priv *priv)
{
	struct dsa_switch *ds = &priv->ds;
	struct device *dev = yt921x_dev(priv);
	unsigned long cpu_ports_mask;
	u64 ctrl64;
	u32 ctrl;
	int port;
	int res;
#if IS_ENABLED(CONFIG_NET_DSA_YT921X_DEBUG)
	struct yt921x_init_dbg_snapshot dbg_stage;
#endif

	/* Enable DSA */
	priv->cpu_ports_mask = dsa_cpu_ports(ds);
	if (!priv->cpu_ports_mask)
		return -EINVAL;

	if (priv->cpu_ports_mask & BIT(8))
		priv->primary_cpu_port = 8;
	else
		priv->primary_cpu_port = __ffs(priv->cpu_ports_mask);
	cpu_ports_mask = priv->cpu_ports_mask & ~BIT(priv->primary_cpu_port);
	if (cpu_ports_mask)
		priv->secondary_cpu_port = __ffs(cpu_ports_mask);
	else
		priv->secondary_cpu_port = -1;

	if (hweight16(priv->cpu_ports_mask) > 2)
		dev_warn(dev,
			 "Only two CPU ports are supported, mask=0x%03x (primary=%d secondary=%d)\n",
			 priv->cpu_ports_mask, priv->primary_cpu_port,
			 priv->secondary_cpu_port);

#if IS_ENABLED(CONFIG_NET_DSA_YT921X_DEBUG)
	yt921x_debug_init_snapshot_take_locked(priv, &dbg_stage);
#endif

	ctrl = YT921X_EXT_CPU_PORT_TAG_EN | YT921X_EXT_CPU_PORT_PORT_EN |
	       YT921X_EXT_CPU_PORT_PORT(priv->primary_cpu_port);
	res = yt921x_reg_write(priv, YT921X_EXT_CPU_PORT, ctrl);
	if (res)
		return res;
#if IS_ENABLED(CONFIG_NET_DSA_YT921X_DEBUG)
	yt921x_debug_init_checkpoint_locked(priv, &dbg_stage, "ext-cpu-port");
#endif

	/* Setup software switch */
	ctrl = YT921X_CPU_COPY_TO_EXT_CPU;
	res = yt921x_reg_write(priv, YT921X_CPU_COPY, ctrl);
	if (res)
		return res;
#if IS_ENABLED(CONFIG_NET_DSA_YT921X_DEBUG)
	yt921x_debug_init_checkpoint_locked(priv, &dbg_stage, "cpu-copy");
#endif

	/* Keep stock loop-detect block enabled with safe defaults. */
	res = yt921x_loop_detect_setup_locked(priv);
	if (res)
		return res;
#if IS_ENABLED(CONFIG_NET_DSA_YT921X_DEBUG)
	yt921x_debug_init_checkpoint_locked(priv, &dbg_stage, "loop-detect");
#endif

	/* Program explicit handling for key 802.1 link-local RMAs. */
	res = yt921x_rma_setup_locked(priv);
	if (res)
		return res;
#if IS_ENABLED(CONFIG_NET_DSA_YT921X_DEBUG)
	yt921x_debug_init_checkpoint_locked(priv, &dbg_stage, "rma");
#endif

	/* Multicast control plane defaults from stock path:
	 * - router ports pinned to CPU conduits
	 * - report/query/leave processing allowed
	 * - IGMP bypasses port isolation
	 * - fast leave disabled until requested by bridge flags
	 */
	ctrl = priv->cpu_ports_mask & YT921X_MCAST_STATIC_ROUTER_PORT_M;
	res = yt921x_reg_update_bits(priv, YT921X_MCAST_STATIC_ROUTER_PORT,
				     YT921X_MCAST_STATIC_ROUTER_PORT_M,
				     ctrl);
	if (res)
		return res;

	res = yt921x_reg_update_bits(priv, YT921X_MCAST_DYNAMIC_ROUTER_PORT,
				     YT921X_MCAST_DYNAMIC_ROUTER_PORT_ALLOW_M,
				     YT921X_MCAST_DYNAMIC_ROUTER_PORT_ALLOW(ctrl));
	if (res)
		return res;

	ctrl = YT921X_MCAST_PORT_POLICY_LEAVE_ALLOW |
	       YT921X_MCAST_PORT_POLICY_REPORT_ALLOW |
	       YT921X_MCAST_PORT_POLICY_QUERY_ALLOW |
	       YT921X_MCAST_PORT_POLICY_IGMP_BYPASS_ISO;
	res = yt921x_reg_update_bits(priv, YT921X_MCAST_PORT_POLICY, ctrl,
				     ctrl);
	if (res)
		return res;

	res = yt921x_reg_update_bits(priv, YT921X_MCAST_FWD_POLICY,
				     YT921X_MCAST_FWD_POLICY_FAST_LEAVE,
				     0);
	if (res)
		return res;
#if IS_ENABLED(CONFIG_NET_DSA_YT921X_DEBUG)
	yt921x_debug_init_checkpoint_locked(priv, &dbg_stage, "mcast-policy");
#endif

	/* Unknown UC/MC egress drop mask:
	 * keep only internal MCU (port 10) blocked, allow CPU/LAN/WAN delivery.
	 * 0x7ff blackholes unknown unicast destined for router MAC on this board.
	 */
	ctrl = BIT(10);
	res = yt921x_reg_write(priv, YT921X_FILTER_UNK_UCAST, ctrl);
	if (res)
		return res;
	res = yt921x_reg_write(priv, YT921X_FILTER_UNK_MCAST, ctrl);
	if (res)
		return res;
	/* Keep stock-safe defaults for flood filters until semantics are fully
	 * mapped on YT9215.
	 */
	priv->flood_mcast_base_mask = BIT(10);
	priv->flood_bcast_base_mask = BIT(10);
	priv->flood_storm_mask = 0;
	res = yt921x_apply_flood_filters_locked(priv);
	if (res)
		return res;
#if IS_ENABLED(CONFIG_NET_DSA_YT921X_DEBUG)
	yt921x_debug_init_checkpoint_locked(priv, &dbg_stage, "unk-filter");
#endif

	/* Let unknown traffic follow hardware forwarding/isolation. Untagged
	 * ingress from CPU ports is still dropped.
	 */
	ctrl = 0;
	cpu_ports_mask = priv->cpu_ports_mask;
	for_each_set_bit(port, &cpu_ports_mask, YT921X_PORT_NUM) {
		ctrl &= ~YT921X_ACT_UNK_ACTn_M(port);
		ctrl |= yt921x_is_primary_cpu_port(priv, port) ?
			YT921X_ACT_UNK_ACTn_DROP(port) :
			YT921X_ACT_UNK_ACTn_FORWARD(port);
	}
	res = yt921x_reg_write(priv, YT921X_ACT_UNK_UCAST, ctrl);
	if (res)
		return res;

	/* Keep IGMP control traffic visible to software snooping even when
	 * unknown multicast filtering/flood controls are active.
	 */
	ctrl |= YT921X_ACT_UNK_MCAST_BYPASS_DROP_IGMP;
	res = yt921x_reg_write(priv, YT921X_ACT_UNK_MCAST, ctrl);
	if (res)
		return res;
#if IS_ENABLED(CONFIG_NET_DSA_YT921X_DEBUG)
	yt921x_debug_init_checkpoint_locked(priv, &dbg_stage, "unk-action");
#endif

	/* Reset mirror engine to known state; firmware can leave stale bits. */
	res = yt921x_reg_write(priv, YT921X_MIRROR, 0);
	if (res)
		return res;
#if IS_ENABLED(CONFIG_NET_DSA_YT921X_DEBUG)
	yt921x_debug_init_checkpoint_locked(priv, &dbg_stage, "mirror-reset");
#endif

	/* Reset mirror-priority map. Keep it disabled until mirror rules are
	 * installed, then enable with a safe low-priority class.
	 */
	res = yt921x_reg_write(priv, YT921X_MIRROR_PRIO_MAP, 0);
	if (res)
		return res;
	priv->acl_mirror_count = 0;
	priv->acl_mirror_to_port = -1;
#if IS_ENABLED(CONFIG_NET_DSA_YT921X_DEBUG)
	yt921x_debug_init_checkpoint_locked(priv, &dbg_stage, "mirror-prio-reset");
#endif

	/* Tagged VID 0 should be treated as untagged, which confuses the
	 * hardware a lot
	 */
	ctrl64 = YT921X_VLAN_CTRL_LEARN_DIS | YT921X_VLAN_CTRL_PORTS_M;
	res = yt921x_reg64_write(priv, YT921X_VLANn_CTRL(0), ctrl64);
	if (res)
		return res;
#if IS_ENABLED(CONFIG_NET_DSA_YT921X_DEBUG)
	yt921x_debug_init_checkpoint_locked(priv, &dbg_stage, "vlan0");
#endif

	return 0;
}

static int yt921x_chip_setup_tc(struct yt921x_priv *priv)
{
	unsigned int op_ns;
	u32 ctrl;
	int res;

	op_ns = 8 * priv->cycle_ns;

	ctrl = max(priv->meter_slot_ns / op_ns, YT921X_METER_SLOT_MIN);
	res = yt921x_reg_write(priv, YT921X_METER_SLOT, ctrl);
	if (res)
		return res;
	priv->meter_slot_ns = ctrl * op_ns;

	ctrl = max(priv->port_shape_slot_ns / op_ns,
		   YT921X_PORT_SHAPE_SLOT_MIN);
	res = yt921x_reg_write(priv, YT921X_PORT_SHAPE_SLOT, ctrl);
	if (res)
		return res;
	priv->port_shape_slot_ns = ctrl * op_ns;

	ctrl = max(priv->queue_shape_slot_ns / op_ns,
		   YT921X_QUEUE_SHAPE_SLOT_MIN);
	res = yt921x_reg_write(priv, YT921X_QUEUE_SHAPE_SLOT, ctrl);
	if (res)
		return res;
	priv->queue_shape_slot_ns = ctrl * op_ns;

	/* Keep legacy storm limiter disabled unless explicitly used later. */
	res = yt921x_reg_update_bits(priv, YT921X_STORM_MC_TYPE_CTRL,
				     YT921X_STORM_MC_TYPE_CTRL_PORTS_M, 0);
	if (res)
		return res;

	res = yt921x_reg_update_bits(priv, YT921X_STORM_CONFIG,
				     YT921X_STORM_CONFIG_EN, 0);
	if (res)
		return res;

	return 0;
}

static int yt921x_chip_setup_acl(struct yt921x_priv *priv)
{
	u32 ctrls[3] = {0};
	u32 ctrl;
	int res;

	memset(&priv->acl, 0, sizeof(priv->acl));
	bitmap_zero(priv->acl_meter_map, YT921X_METER_NUM);
	memset(priv->udfs_ctrl, 0, sizeof(priv->udfs_ctrl));
	memset(priv->udfs_refcnt, 0, sizeof(priv->udfs_refcnt));

	/* Reserve one meter as a permanent blackhole used by DROP actions. */
	__set_bit(YT921X_ACL_METER_ID_BLACKHOLE, priv->acl_meter_map);
	ctrls[2] = YT921X_METER_CTRLc_METER_EN | YT921X_METER_CTRLc_DROP_GYR;
	res = yt921x_reg96_write(priv,
				 YT921X_METERn_CTRL(YT921X_ACL_METER_ID_BLACKHOLE),
				 ctrls);
	if (res)
		return res;

	ctrl = YT921X_ACL_PERMIT_UNMATCH_PORTS_M;
	res = yt921x_reg_write(priv, YT921X_ACL_PERMIT_UNMATCH, ctrl);
	if (res)
		return res;

	ctrl = YT921X_ACL_PORT_PORTS_M;
	res = yt921x_reg_write(priv, YT921X_ACL_PORT, ctrl);
	if (res)
		return res;

	return 0;
}


static int __maybe_unused yt921x_chip_setup_qos(struct yt921x_priv *priv)
{
	u32 ctrl;
	int res;

	/* DSCP to internal priorities */
	for (u8 dscp = 0; dscp < DSCP_MAX; dscp++) {
		int prio = ietf_dscp_to_ieee8021q_tt(dscp);

		if (prio < 0)
			prio = SIMPLE_IETF_DSCP_TO_IEEE8021Q_TT(dscp);

		res = yt921x_reg_write(priv, YT921X_IPM_DSCPn(dscp),
				       YT921X_IPM_PRIO(prio));
		if (res)
			return res;
	}

	/* 802.1Q QoS to internal priorities */
	for (u8 pcp = 0; pcp < 8; pcp++)
		for (u8 dei = 0; dei < 2; dei++) {
			ctrl = YT921X_IPM_PRIO(pcp);
			if (dei)
				/* "Red" almost means drop, so it's not that
				 * useful. Note that tc police does not support
				 * Three-Color very well
				 */
				ctrl |= YT921X_IPM_COLOR_YELLOW;

			for (u8 svlan = 0; svlan < 2; svlan++) {
				u32 reg = YT921X_IPM_PCPn(svlan, dei, pcp);

				res = yt921x_reg_write(priv, reg, ctrl);
				if (res)
					return res;
			}
		}

	/* Egress remark defaults:
	 *  - DSCP remark map uses class-selector baseline (prio -> prio*8)
	 *  - CPRI/SPRI remark maps use identity prio rewrite, enabled.
	 */
	for (u8 prio = 0; prio < YT921X_PRIO_NUM; prio++) {
		u8 dscp = min_t(u8, (u8)(prio << 3), (u8)(DSCP_MAX - 1));

		for (u8 dp = 0; dp < YT921X_QOS_REMARK_DP_NUM; dp++) {
			res = yt921x_qos_remark_dscp_set(priv, prio, dp, dscp);
			if (res)
				return res;

			res = yt921x_qos_remark_prio_set(priv, prio, dp, false,
							 prio, true);
			if (res)
				return res;

			res = yt921x_qos_remark_prio_set(priv, prio, dp, true,
							 prio, true);
			if (res)
				return res;
		}
	}

	for (int port = 0; port < YT921X_PORT_NUM; port++) {
		if (!dsa_is_user_port(&priv->ds, port))
			continue;

		res = yt921x_qos_remark_port_enable(priv, port, true, true);
		if (res)
			return res;
	}

	return 0;
}

static int yt921x_chip_setup(struct yt921x_priv *priv)
{
	u32 ctrl;
	int res;

	ctrl = YT921X_FUNC_MIB | YT921X_FUNC_ACL | YT921X_FUNC_METER;
	res = yt921x_reg_set_bits(priv, YT921X_FUNC, ctrl);
	if (res)
		return res;

	res = yt921x_chip_setup_dsa(priv);
	if (res)
		return res;

	res = yt921x_chip_setup_tc(priv);
	if (res)
		return res;

	res = yt921x_chip_setup_acl(priv);
	if (res)
		return res;

	res = yt921x_chip_setup_qos(priv);
	if (res)
		return res;

	res = yt921x_trap_copp_default_apply(priv);
	if (res)
		return res;

	/* Clear MIB */
	ctrl = YT921X_MIB_CTRL_CLEAN | YT921X_MIB_CTRL_ALL_PORT;
	res = yt921x_reg_write(priv, YT921X_MIB_CTRL, ctrl);
	if (res)
		return res;

	/* Miscellaneous */
	res = yt921x_reg_set_bits(priv, YT921X_SENSOR, YT921X_SENSOR_TEMP);
	if (res)
		return res;

	/* Flush dynamic FDB entries on hardware link-down events. */
	res = yt921x_reg_set_bits(priv, YT921X_FDB_HW_FLUSH,
				  YT921X_FDB_HW_FLUSH_ON_LINKDOWN);
	if (res)
		return res;

	res = yt921x_validate_setup_locked(priv);
	if (res)
		return res;

	yt921x_log_mdio_summary_locked(priv);

	return 0;
}
