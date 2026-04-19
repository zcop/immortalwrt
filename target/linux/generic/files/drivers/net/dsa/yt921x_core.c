/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Internal split unit for yt921x.c
 *
 * Copyright (c) 2025 David Yang
 * Copyright (c) 2026 zcop <hongson.hn@gmail.com>
 */

#include "yt921x_internal.h"

/* Must agree with yt921x_mib
 *
 * Unstructured fields (name != NULL) will appear in get_ethtool_stats(),
 * structured go to their *_stats() methods, but we need their sizes and offsets
 * to perform 32bit MIB overflow wraparound.
 */
const struct yt921x_mib_desc yt921x_mib_descs[] = {
	YT921X_MIB_DESC(1, 0x00, NULL),	/* RxBroadcast */
	YT921X_MIB_DESC(1, 0x04, NULL),	/* RxPause */
	YT921X_MIB_DESC(1, 0x08, NULL),	/* RxMulticast */
	YT921X_MIB_DESC(1, 0x0c, NULL),	/* RxCrcErr */

	YT921X_MIB_DESC(1, 0x10, NULL),	/* RxAlignErr */
	YT921X_MIB_DESC(1, 0x14, NULL),	/* RxUnderSizeErr */
	YT921X_MIB_DESC(1, 0x18, NULL),	/* RxFragErr */
	YT921X_MIB_DESC(1, 0x1c, NULL),	/* RxPktSz64 */

	YT921X_MIB_DESC(1, 0x20, NULL),	/* RxPktSz65To127 */
	YT921X_MIB_DESC(1, 0x24, NULL),	/* RxPktSz128To255 */
	YT921X_MIB_DESC(1, 0x28, NULL),	/* RxPktSz256To511 */
	YT921X_MIB_DESC(1, 0x2c, NULL),	/* RxPktSz512To1023 */

	YT921X_MIB_DESC(1, 0x30, NULL),	/* RxPktSz1024To1518 */
	YT921X_MIB_DESC(1, 0x34, NULL),	/* RxPktSz1519ToMax */
	/* 0x38: unused */
	YT921X_MIB_DESC(2, 0x3c, NULL),	/* RxGoodBytes */

	/* 0x40: 64 bytes */
	YT921X_MIB_DESC(2, 0x44, "RxBadBytes"),
	/* 0x48: 64 bytes */
	YT921X_MIB_DESC(1, 0x4c, NULL),	/* RxOverSzErr */

	YT921X_MIB_DESC(1, 0x50, NULL),	/* RxDropped */
	YT921X_MIB_DESC(1, 0x54, NULL),	/* TxBroadcast */
	YT921X_MIB_DESC(1, 0x58, NULL),	/* TxPause */
	YT921X_MIB_DESC(1, 0x5c, NULL),	/* TxMulticast */

	YT921X_MIB_DESC(1, 0x60, NULL),	/* TxUnderSizeErr */
	YT921X_MIB_DESC(1, 0x64, NULL),	/* TxPktSz64 */
	YT921X_MIB_DESC(1, 0x68, NULL),	/* TxPktSz65To127 */
	YT921X_MIB_DESC(1, 0x6c, NULL),	/* TxPktSz128To255 */

	YT921X_MIB_DESC(1, 0x70, NULL),	/* TxPktSz256To511 */
	YT921X_MIB_DESC(1, 0x74, NULL),	/* TxPktSz512To1023 */
	YT921X_MIB_DESC(1, 0x78, NULL),	/* TxPktSz1024To1518 */
	YT921X_MIB_DESC(1, 0x7c, NULL),	/* TxPktSz1519ToMax */

	/* 0x80: unused */
	YT921X_MIB_DESC(2, 0x84, NULL),	/* TxGoodBytes */
	/* 0x88: 64 bytes */
	YT921X_MIB_DESC(1, 0x8c, NULL),	/* TxCollision */

	YT921X_MIB_DESC(1, 0x90, NULL),	/* TxExcessiveCollistion */
	YT921X_MIB_DESC(1, 0x94, NULL),	/* TxMultipleCollision */
	YT921X_MIB_DESC(1, 0x98, NULL),	/* TxSingleCollision */
	YT921X_MIB_DESC(1, 0x9c, NULL),	/* TxPkt */

	YT921X_MIB_DESC(1, 0xa0, NULL),	/* TxDeferred */
	YT921X_MIB_DESC(1, 0xa4, NULL),	/* TxLateCollision */
	YT921X_MIB_DESC(1, 0xa8, "RxOAM"),
	YT921X_MIB_DESC(1, 0xac, "TxOAM"),
};
const size_t yt921x_mib_descs_count = ARRAY_SIZE(yt921x_mib_descs);

const struct yt921x_info yt921x_infos[] = {
	{
		"YT9215SC", YT9215_MAJOR, 1, 0,
		GENMASK(4, 0),
		YT921X_PORT_MASK_EXT0 | YT921X_PORT_MASK_EXT1,
	},
	{
		"YT9215S", YT9215_MAJOR, 2, 0,
		GENMASK(4, 0),
		YT921X_PORT_MASK_EXT0 | YT921X_PORT_MASK_EXT1,
	},
	{
		"YT9215RB", YT9215_MAJOR, 3, 0,
		GENMASK(4, 0),
		YT921X_PORT_MASK_EXT0 | YT921X_PORT_MASK_EXT1,
	},
	{
		"YT9214NB", YT9215_MAJOR, 3, 2,
		YT921X_PORT_MASK_INTn(1) | YT921X_PORT_MASK_INTn(3),
		YT921X_PORT_MASK_EXT0 | YT921X_PORT_MASK_EXT1,
	},
	{
		"YT9213NB", YT9215_MAJOR, 3, 3,
		YT921X_PORT_MASK_INTn(1) | YT921X_PORT_MASK_INTn(3),
		YT921X_PORT_MASK_EXT1,
	},
	{
		"YT9218N", YT9218_MAJOR, 0, 0,
		GENMASK(7, 0),
		0,
	},
	{
		"YT9218MB", YT9218_MAJOR, 1, 0,
		GENMASK(7, 0),
		YT921X_PORT_MASK_EXT0 | YT921X_PORT_MASK_EXT1,
	},
	{}
};

/* TODO: SPI/I2C */

bool yt921x_is_external_port(const struct yt921x_priv *priv, int port)
{
	return !!(priv->info->external_mask & BIT(port));
}

bool yt921x_is_primary_cpu_port(const struct yt921x_priv *priv, int port)
{
	return priv->primary_cpu_port >= 0 && port == priv->primary_cpu_port;
}

bool yt921x_is_secondary_cpu_port(const struct yt921x_priv *priv, int port)
{
	return priv->secondary_cpu_port >= 0 && port == priv->secondary_cpu_port;
}

int yt921x_reg_read(struct yt921x_priv *priv, u32 reg, u32 *valp)
{
	int res;

	WARN_ON(!mutex_is_locked(&priv->reg_lock));

	res = priv->reg_ops->read(priv->reg_ctx, reg, valp);
	if (res)
		YT921X_RECORD_ERR(priv, reg_io_errors, YT921X_TELEM_STAGE_REG_READ,
				  res, -1, reg, 0, 0);

	return res;
}

int yt921x_reg_write(struct yt921x_priv *priv, u32 reg, u32 val)
{
	int res;

	WARN_ON(!mutex_is_locked(&priv->reg_lock));

	res = priv->reg_ops->write(priv->reg_ctx, reg, val);
	if (res)
		YT921X_RECORD_ERR(priv, reg_io_errors, YT921X_TELEM_STAGE_REG_WRITE,
				  res, -1, reg, val, 0);

	return res;
}

int
yt921x_reg_wait(struct yt921x_priv *priv, u32 reg, u32 mask, u32 *valp)
{
	u32 val;
	int res;
	int ret;

	ret = read_poll_timeout(yt921x_reg_read, res,
				res || (val & mask) == *valp,
				YT921X_POLL_SLEEP_US, YT921X_POLL_TIMEOUT_US,
				false, priv, reg, &val);
	if (ret) {
		YT921X_RECORD_ERR(priv, reg_poll_timeouts, YT921X_TELEM_STAGE_REG_WAIT,
				  ret, -1, reg, mask, *valp);
		return ret;
	}
	if (res)
		return res;

	*valp = val;
	return 0;
}

int
yt921x_reg_update_bits(struct yt921x_priv *priv, u32 reg, u32 mask, u32 val)
{
	int res;
	u32 v;
	u32 u;

	res = yt921x_reg_read(priv, reg, &v);
	if (res)
		return res;

	u = v;
	u &= ~mask;
	u |= val;
	if (u == v)
		return 0;

	return yt921x_reg_write(priv, reg, u);
}

int yt921x_reg_set_bits(struct yt921x_priv *priv, u32 reg, u32 mask)
{
	return yt921x_reg_update_bits(priv, reg, 0, mask);
}

int yt921x_reg_clear_bits(struct yt921x_priv *priv, u32 reg, u32 mask)
{
	return yt921x_reg_update_bits(priv, reg, mask, 0);
}

int
yt921x_reg_toggle_bits(struct yt921x_priv *priv, u32 reg, u32 mask, bool set)
{
	return yt921x_reg_update_bits(priv, reg, mask, !set ? 0 : mask);
}

enum yt921x_rma_action {
	YT921X_RMA_ACT_FORWARD = 0,
	YT921X_RMA_ACT_TRAP_TO_CPU = 1,
	YT921X_RMA_ACT_COPY_TO_CPU = 2,
	YT921X_RMA_ACT_DROP = 3,
};

/* Release-facing optional policy overrides.
 * -1 keeps the stock default behavior.
 */
static int yt921x_rma_slow_action = -1;
module_param_named(rma_slow_action, yt921x_rma_slow_action, int, 0644);
MODULE_PARM_DESC(rma_slow_action,
		 "RMA action for 01:80:c2:00:00:02 (slow protocols): "
		 "-1=default trap, 0=forward, 1=trap, 2=copy, 3=drop");

static int yt921x_ctrlpkt_lldp_eee_act = -1;
module_param_named(ctrlpkt_lldp_eee_act, yt921x_ctrlpkt_lldp_eee_act, int, 0644);
MODULE_PARM_DESC(ctrlpkt_lldp_eee_act,
		 "Port mask for LLDP EEE control packets (tbl 0x76), -1 keeps stock");

static int yt921x_ctrlpkt_lldp_act = -1;
module_param_named(ctrlpkt_lldp_act, yt921x_ctrlpkt_lldp_act, int, 0644);
MODULE_PARM_DESC(ctrlpkt_lldp_act,
		 "Port mask for LLDP control packets (tbl 0x77), -1 keeps stock");

/* Optional ingress VLAN translation mode overrides.
 * -1 keeps stock behavior for each mode bit.
 */
static int yt921x_vlan_mode_port = -1;
module_param_named(vlan_mode_port, yt921x_vlan_mode_port, int, 0644);
MODULE_PARM_DESC(vlan_mode_port,
		 "Override VLAN ingress port-based mode bit: -1=stock, 0=off, 1=on");

static int yt921x_vlan_mode_ctag = -1;
module_param_named(vlan_mode_ctag, yt921x_vlan_mode_ctag, int, 0644);
MODULE_PARM_DESC(vlan_mode_ctag,
		 "Override VLAN ingress C-tag mode bit: -1=stock, 0=off, 1=on");

static int yt921x_vlan_mode_stag = -1;
module_param_named(vlan_mode_stag, yt921x_vlan_mode_stag, int, 0644);
MODULE_PARM_DESC(vlan_mode_stag,
		 "Override VLAN ingress S-tag mode bit: -1=stock, 0=off, 1=on");

static int yt921x_vlan_mode_proto = -1;
module_param_named(vlan_mode_proto, yt921x_vlan_mode_proto, int, 0644);
MODULE_PARM_DESC(vlan_mode_proto,
		 "Override VLAN ingress protocol-based mode bit: -1=stock, 0=off, 1=on");

static int yt921x_vlan_mode_param_get_mask_val(u32 *mask, u32 *val)
{
	struct {
		int param;
		u32 bit;
	} params[] = {
		{ yt921x_vlan_mode_port, YT921X_VLAN_IGR_TRANS_CTRL_PORT_MODE },
		{ yt921x_vlan_mode_ctag, YT921X_VLAN_IGR_TRANS_CTRL_CTAG_MODE },
		{ yt921x_vlan_mode_stag, YT921X_VLAN_IGR_TRANS_CTRL_STAG_MODE },
		{ yt921x_vlan_mode_proto, YT921X_VLAN_IGR_TRANS_CTRL_PROTO_MODE },
	};
	unsigned int i;
	u32 m = 0;
	u32 v = 0;

	for (i = 0; i < ARRAY_SIZE(params); i++) {
		if (params[i].param == -1)
			continue;
		if (params[i].param != 0 && params[i].param != 1)
			return -EINVAL;

		m |= params[i].bit;
		if (params[i].param)
			v |= params[i].bit;
	}

	*mask = m;
	*val = v;

	return 0;
}

int yt921x_vlan_mode_setup_locked(struct yt921x_priv *priv)
{
	struct device *dev = yt921x_dev(priv);
	u32 mask;
	u32 val;
	int res;

	res = yt921x_vlan_mode_param_get_mask_val(&mask, &val);
	if (res) {
		dev_err(dev,
			"Invalid vlan_mode_* parameter value(s): port=%d ctag=%d stag=%d proto=%d\n",
			yt921x_vlan_mode_port, yt921x_vlan_mode_ctag,
			yt921x_vlan_mode_stag, yt921x_vlan_mode_proto);
		return res;
	}
	if (!mask)
		return 0;

	dev_info(dev,
		 "Applying VLAN ingress mode override: mask=0x%x val=0x%x (port=%d ctag=%d stag=%d proto=%d)\n",
		 mask, val, yt921x_vlan_mode_port, yt921x_vlan_mode_ctag,
		 yt921x_vlan_mode_stag, yt921x_vlan_mode_proto);

	return yt921x_reg_update_bits(priv, YT921X_VLAN_IGR_TRANS_CTRL, mask, val);
}

enum yt921x_devlink_param_id {
	YT921X_DEVLINK_PARAM_ID_BASE = DEVLINK_PARAM_GENERIC_ID_MAX,
	YT921X_DEVLINK_PARAM_ID_VLAN_MODE_PORT,
	YT921X_DEVLINK_PARAM_ID_VLAN_MODE_CTAG,
	YT921X_DEVLINK_PARAM_ID_VLAN_MODE_STAG,
	YT921X_DEVLINK_PARAM_ID_VLAN_MODE_PROTO,
	YT921X_DEVLINK_PARAM_ID_VLAN_UNTAG_PVID_IGNORE,
	YT921X_DEVLINK_PARAM_ID_VLAN_RANGE_EN_MASK,
	YT921X_DEVLINK_PARAM_ID_VLAN_CVLAN_DROP_TAGGED_MASK,
	YT921X_DEVLINK_PARAM_ID_VLAN_CVLAN_DROP_UNTAGGED_MASK,
	YT921X_DEVLINK_PARAM_ID_VLAN_SVLAN_DROP_TAGGED_MASK,
	YT921X_DEVLINK_PARAM_ID_VLAN_SVLAN_DROP_UNTAGGED_MASK,
	YT921X_DEVLINK_PARAM_ID_DOT1X_MAC_BASED_MASK,
};

static int yt921x_devlink_param_to_vlan_mask(u32 id, u32 *mask)
{
	switch (id) {
	case YT921X_DEVLINK_PARAM_ID_VLAN_MODE_PORT:
		*mask = YT921X_VLAN_IGR_TRANS_CTRL_PORT_MODE;
		return 0;
	case YT921X_DEVLINK_PARAM_ID_VLAN_MODE_CTAG:
		*mask = YT921X_VLAN_IGR_TRANS_CTRL_CTAG_MODE;
		return 0;
	case YT921X_DEVLINK_PARAM_ID_VLAN_MODE_STAG:
		*mask = YT921X_VLAN_IGR_TRANS_CTRL_STAG_MODE;
		return 0;
	case YT921X_DEVLINK_PARAM_ID_VLAN_MODE_PROTO:
		*mask = YT921X_VLAN_IGR_TRANS_CTRL_PROTO_MODE;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int yt921x_devlink_param_to_vlan_ctrl1_mask(u32 id, u32 *mask)
{
	switch (id) {
	case YT921X_DEVLINK_PARAM_ID_VLAN_RANGE_EN_MASK:
		*mask = YT921X_PORT_VLAN_CTRL1_VLAN_RANGE_EN;
		return 0;
	case YT921X_DEVLINK_PARAM_ID_VLAN_CVLAN_DROP_TAGGED_MASK:
		*mask = YT921X_PORT_VLAN_CTRL1_CVLAN_DROP_TAGGED;
		return 0;
	case YT921X_DEVLINK_PARAM_ID_VLAN_CVLAN_DROP_UNTAGGED_MASK:
		*mask = YT921X_PORT_VLAN_CTRL1_CVLAN_DROP_UNTAGGED;
		return 0;
	case YT921X_DEVLINK_PARAM_ID_VLAN_SVLAN_DROP_TAGGED_MASK:
		*mask = YT921X_PORT_VLAN_CTRL1_SVLAN_DROP_TAGGED;
		return 0;
	case YT921X_DEVLINK_PARAM_ID_VLAN_SVLAN_DROP_UNTAGGED_MASK:
		*mask = YT921X_PORT_VLAN_CTRL1_SVLAN_DROP_UNTAGGED;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static u32 yt921x_user_ports_mask(struct dsa_switch *ds)
{
	struct dsa_port *dp;
	u32 mask = 0;

	dsa_switch_for_each_user_port(dp, ds)
		mask |= BIT(dp->index);

	return mask;
}

static int yt921x_vlan_ctrl1_port_mask_get_locked(struct yt921x_priv *priv,
						  u32 ctrl1_mask, u32 *mask)
{
	struct dsa_switch *ds = &priv->ds;
	struct dsa_port *dp;
	u32 ctrl1;
	int res;

	*mask = 0;

	dsa_switch_for_each_user_port(dp, ds) {
		res = yt921x_reg_read(priv, YT921X_PORTn_VLAN_CTRL1(dp->index),
				      &ctrl1);
		if (res)
			return res;
		if (ctrl1 & ctrl1_mask)
			*mask |= BIT(dp->index);
	}

	return 0;
}

static int yt921x_vlan_ctrl1_port_mask_set_locked(struct yt921x_priv *priv,
						  u32 ctrl1_mask, u32 req_mask)
{
	struct dsa_switch *ds = &priv->ds;
	struct dsa_port *dp;
	u32 allowed_mask;
	int res;

	allowed_mask = yt921x_user_ports_mask(ds);
	if (req_mask & ~allowed_mask)
		return -EINVAL;

	dsa_switch_for_each_user_port(dp, ds) {
		res = yt921x_reg_toggle_bits(priv,
					     YT921X_PORTn_VLAN_CTRL1(dp->index),
					     ctrl1_mask,
					     !!(req_mask & BIT(dp->index)));
		if (res)
			return res;
	}

	return 0;
}

static int yt921x_dot1x_mac_based_get_locked(struct yt921x_priv *priv, u32 *mask)
{
	struct dsa_switch *ds = &priv->ds;
	struct dsa_port *dp;
	u32 dot1x_en;
	u32 dot1x_ctrl2;
	u32 rx_permit;
	int res;

	*mask = 0;

	res = yt921x_reg_read(priv, YT921X_DOT1X_PORT_BASED, &dot1x_en);
	if (res)
		return res;

	res = yt921x_reg_read(priv, YT921X_DOT1X_BYPASS_CTRL, &dot1x_ctrl2);
	if (res)
		return res;

	rx_permit = FIELD_GET(YT921X_DOT1X_CTRL2_RX_PERMIT_MASK_M, dot1x_ctrl2);

	dsa_switch_for_each_user_port(dp, ds) {
		if (!(dot1x_en & BIT(dp->index)))
			continue;

		/* In unauthorized state we use "RX blocked, TX permitted". */
		if (rx_permit & BIT(dp->index))
			continue;

		*mask |= BIT(dp->index);
	}

	return 0;
}

static int yt921x_dot1x_mac_based_set_locked(struct yt921x_priv *priv, u32 req_mask)
{
	struct dsa_switch *ds = &priv->ds;
	struct dsa_port *dp;
	u32 allowed_mask;
	u32 dot1x_en;
	u32 dot1x_ctrl2;
	u32 rx_permit;
	u32 tx_permit;
	int res;

	allowed_mask = yt921x_user_ports_mask(ds);
	if (req_mask & ~allowed_mask)
		return -EINVAL;

	res = yt921x_reg_read(priv, YT921X_DOT1X_PORT_BASED, &dot1x_en);
	if (res)
		return res;

	res = yt921x_reg_read(priv, YT921X_DOT1X_BYPASS_CTRL, &dot1x_ctrl2);
	if (res)
		return res;

	rx_permit = FIELD_GET(YT921X_DOT1X_CTRL2_RX_PERMIT_MASK_M, dot1x_ctrl2);
	tx_permit = FIELD_GET(YT921X_DOT1X_CTRL2_TX_PERMIT_MASK_M, dot1x_ctrl2);

	dsa_switch_for_each_user_port(dp, ds) {
		if (req_mask & BIT(dp->index)) {
			/* Unauthorized: enable 802.1X gate on this port and
			 * block ingress from the edge while keeping egress
			 * towards the edge open.
			 */
			dot1x_en |= BIT(dp->index);
			rx_permit &= ~BIT(dp->index);
			tx_permit |= BIT(dp->index);
			continue;
		}

		/* Authorized/disabled: pass both directions. */
		dot1x_en &= ~BIT(dp->index);
		rx_permit |= BIT(dp->index);
		tx_permit |= BIT(dp->index);
	}

	res = yt921x_reg_write(priv, YT921X_DOT1X_PORT_BASED, dot1x_en);
	if (res)
		return res;

	dot1x_ctrl2 &= ~(YT921X_DOT1X_CTRL2_RX_PERMIT_MASK_M |
			 YT921X_DOT1X_CTRL2_TX_PERMIT_MASK_M);
	dot1x_ctrl2 |= YT921X_DOT1X_CTRL2_RX_PERMIT_MASK(rx_permit) |
		       YT921X_DOT1X_CTRL2_TX_PERMIT_MASK(tx_permit);

	return yt921x_reg_write(priv, YT921X_DOT1X_BYPASS_CTRL, dot1x_ctrl2);
}

int yt921x_devlink_param_get(struct dsa_switch *ds, u32 id,
			     struct devlink_param_gset_ctx *ctx)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	u32 ctrl1_mask;
	u32 mask;
	u32 ctrl;
	u32 dot1x_mask;
	u32 port_mask;
	int res;

	if (id == YT921X_DEVLINK_PARAM_ID_DOT1X_MAC_BASED_MASK) {
		mutex_lock(&priv->reg_lock);
		res = yt921x_dot1x_mac_based_get_locked(priv, &dot1x_mask);
		mutex_unlock(&priv->reg_lock);
		if (res)
			return res;

		ctx->val.vu32 = dot1x_mask;
		return 0;
	}

	if (id == YT921X_DEVLINK_PARAM_ID_VLAN_UNTAG_PVID_IGNORE) {
		mutex_lock(&priv->reg_lock);
		res = yt921x_reg_read(priv, YT921X_VLAN_TRANS_UNTAG_PVID_IGNORE,
				      &ctrl);
		mutex_unlock(&priv->reg_lock);
		if (res)
			return res;

		ctx->val.vbool = !!(ctrl & YT921X_VLAN_TRANS_UNTAG_PVID_IGNORE_EN);
		return 0;
	}

	res = yt921x_devlink_param_to_vlan_ctrl1_mask(id, &ctrl1_mask);
	if (!res) {
		mutex_lock(&priv->reg_lock);
		res = yt921x_vlan_ctrl1_port_mask_get_locked(priv, ctrl1_mask,
							      &port_mask);
		mutex_unlock(&priv->reg_lock);
		if (res)
			return res;

		ctx->val.vu32 = port_mask;
		return 0;
	}

	res = yt921x_devlink_param_to_vlan_mask(id, &mask);
	if (res)
		return res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_reg_read(priv, YT921X_VLAN_IGR_TRANS_CTRL, &ctrl);
	mutex_unlock(&priv->reg_lock);
	if (res)
		return res;

	ctx->val.vbool = !!(ctrl & mask);

	return 0;
}

int yt921x_devlink_param_set(struct dsa_switch *ds, u32 id,
			     struct devlink_param_gset_ctx *ctx)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	u32 ctrl1_mask;
	u32 mask;
	int res;

	if (id == YT921X_DEVLINK_PARAM_ID_DOT1X_MAC_BASED_MASK) {
		mutex_lock(&priv->reg_lock);
		res = yt921x_dot1x_mac_based_set_locked(priv, ctx->val.vu32);
		mutex_unlock(&priv->reg_lock);
		return res;
	}

	if (id == YT921X_DEVLINK_PARAM_ID_VLAN_UNTAG_PVID_IGNORE) {
		mutex_lock(&priv->reg_lock);
		res = yt921x_reg_toggle_bits(priv,
					     YT921X_VLAN_TRANS_UNTAG_PVID_IGNORE,
					     YT921X_VLAN_TRANS_UNTAG_PVID_IGNORE_EN,
					     ctx->val.vbool);
		mutex_unlock(&priv->reg_lock);
		return res;
	}

	res = yt921x_devlink_param_to_vlan_ctrl1_mask(id, &ctrl1_mask);
	if (!res) {
		mutex_lock(&priv->reg_lock);
		res = yt921x_vlan_ctrl1_port_mask_set_locked(priv, ctrl1_mask,
							      ctx->val.vu32);
		mutex_unlock(&priv->reg_lock);
		return res;
	}

	res = yt921x_devlink_param_to_vlan_mask(id, &mask);
	if (res)
		return res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_reg_toggle_bits(priv, YT921X_VLAN_IGR_TRANS_CTRL, mask,
				     ctx->val.vbool);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static const struct devlink_param yt921x_devlink_params[] = {
	DSA_DEVLINK_PARAM_DRIVER(YT921X_DEVLINK_PARAM_ID_VLAN_MODE_PORT,
				 "vlan_mode_port", DEVLINK_PARAM_TYPE_BOOL,
				 BIT(DEVLINK_PARAM_CMODE_RUNTIME)),
	DSA_DEVLINK_PARAM_DRIVER(YT921X_DEVLINK_PARAM_ID_VLAN_MODE_CTAG,
				 "vlan_mode_ctag", DEVLINK_PARAM_TYPE_BOOL,
				 BIT(DEVLINK_PARAM_CMODE_RUNTIME)),
	DSA_DEVLINK_PARAM_DRIVER(YT921X_DEVLINK_PARAM_ID_VLAN_MODE_STAG,
				 "vlan_mode_stag", DEVLINK_PARAM_TYPE_BOOL,
				 BIT(DEVLINK_PARAM_CMODE_RUNTIME)),
	DSA_DEVLINK_PARAM_DRIVER(YT921X_DEVLINK_PARAM_ID_VLAN_MODE_PROTO,
				 "vlan_mode_proto", DEVLINK_PARAM_TYPE_BOOL,
				 BIT(DEVLINK_PARAM_CMODE_RUNTIME)),
	DSA_DEVLINK_PARAM_DRIVER(YT921X_DEVLINK_PARAM_ID_VLAN_UNTAG_PVID_IGNORE,
				 "vlan_trans_untag_pvid_ignore",
				 DEVLINK_PARAM_TYPE_BOOL,
				 BIT(DEVLINK_PARAM_CMODE_RUNTIME)),
	DSA_DEVLINK_PARAM_DRIVER(YT921X_DEVLINK_PARAM_ID_VLAN_RANGE_EN_MASK,
				 "vlan_trans_range_en_mask",
				 DEVLINK_PARAM_TYPE_U32,
				 BIT(DEVLINK_PARAM_CMODE_RUNTIME)),
	DSA_DEVLINK_PARAM_DRIVER(YT921X_DEVLINK_PARAM_ID_VLAN_CVLAN_DROP_TAGGED_MASK,
				 "vlan_trans_cvlan_drop_tagged_mask",
				 DEVLINK_PARAM_TYPE_U32,
				 BIT(DEVLINK_PARAM_CMODE_RUNTIME)),
	DSA_DEVLINK_PARAM_DRIVER(YT921X_DEVLINK_PARAM_ID_VLAN_CVLAN_DROP_UNTAGGED_MASK,
				 "vlan_trans_cvlan_drop_untagged_mask",
				 DEVLINK_PARAM_TYPE_U32,
				 BIT(DEVLINK_PARAM_CMODE_RUNTIME)),
	DSA_DEVLINK_PARAM_DRIVER(YT921X_DEVLINK_PARAM_ID_VLAN_SVLAN_DROP_TAGGED_MASK,
				 "vlan_trans_svlan_drop_tagged_mask",
				 DEVLINK_PARAM_TYPE_U32,
				 BIT(DEVLINK_PARAM_CMODE_RUNTIME)),
	DSA_DEVLINK_PARAM_DRIVER(YT921X_DEVLINK_PARAM_ID_VLAN_SVLAN_DROP_UNTAGGED_MASK,
				 "vlan_trans_svlan_drop_untagged_mask",
				 DEVLINK_PARAM_TYPE_U32,
				 BIT(DEVLINK_PARAM_CMODE_RUNTIME)),
	DSA_DEVLINK_PARAM_DRIVER(YT921X_DEVLINK_PARAM_ID_DOT1X_MAC_BASED_MASK,
				 "dot1x_mac_based_mask", DEVLINK_PARAM_TYPE_U32,
				 BIT(DEVLINK_PARAM_CMODE_RUNTIME)),
};

int yt921x_devlink_params_register(struct dsa_switch *ds)
{
	return dsa_devlink_params_register(ds, yt921x_devlink_params,
					   ARRAY_SIZE(yt921x_devlink_params));
}

void yt921x_devlink_params_unregister(struct dsa_switch *ds)
{
	dsa_devlink_params_unregister(ds, yt921x_devlink_params,
				      ARRAY_SIZE(yt921x_devlink_params));
}

int yt921x_loop_detect_setup_locked(struct yt921x_priv *priv)
{
	u32 ctrl;
	int res;

	res = yt921x_reg_read(priv, YT921X_LOOP_DETECT_TOP_CTRL, &ctrl);
	if (res)
		return res;

	/* Keep stock-programmed unit-id/slice fields as-is.
	 * Force loop-detect enable, keep generate-way off by default, and seed a
	 * sane TPID only when firmware left it zero.
	 */
	ctrl |= YT921X_LOOP_DETECT_EN;
	ctrl &= ~YT921X_LOOP_DETECT_GEN_WAY;

	if (!FIELD_GET(YT921X_LOOP_DETECT_TPID_M, ctrl)) {
		ctrl &= ~YT921X_LOOP_DETECT_TPID_M;
		ctrl |= FIELD_PREP(YT921X_LOOP_DETECT_TPID_M,
				   YT921X_LOOP_DETECT_DEFAULT_TPID);
	}

	return yt921x_reg_write(priv, YT921X_LOOP_DETECT_TOP_CTRL, ctrl);
}

static int yt921x_rma_ctrl_action_set(u32 *ctrl,
					    enum yt921x_rma_action action)
{
	*ctrl &= ~(YT921X_RMA_CTRL_F3 |
		   YT921X_RMA_CTRL_F4 |
		   YT921X_RMA_CTRL_FWD_MASK_M);

	switch (action) {
	case YT921X_RMA_ACT_FORWARD:
		*ctrl |= YT921X_RMA_CTRL_FWD_MASK_ALL;
		break;
	case YT921X_RMA_ACT_TRAP_TO_CPU:
		*ctrl |= YT921X_RMA_CTRL_F3 |
			 YT921X_RMA_CTRL_F4;
		break;
	case YT921X_RMA_ACT_COPY_TO_CPU:
		*ctrl |= YT921X_RMA_CTRL_F4 |
			 YT921X_RMA_CTRL_FWD_MASK_ALL;
		break;
	case YT921X_RMA_ACT_DROP:
		*ctrl |= YT921X_RMA_CTRL_F3;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int
yt921x_stock_rma_ctrl_set(struct yt921x_priv *priv, u8 index,
			  enum yt921x_rma_action action,
			  bool bypass_port_isolation, bool bypass_vlan_filter)
{
	u32 reg = YT921X_RMA_CTRLn(index);
	u32 ctrl;
	int res;

	if (index >= 0x30)
		return -ERANGE;

	res = yt921x_reg_read(priv, reg, &ctrl);
	if (res)
		return res;

	res = yt921x_rma_ctrl_action_set(&ctrl, action);
	if (res)
		return res;

	ctrl = bypass_port_isolation ?
		(ctrl | YT921X_RMA_CTRL_F6) :
		(ctrl & ~YT921X_RMA_CTRL_F6);
	ctrl = bypass_vlan_filter ?
		(ctrl | YT921X_RMA_CTRL_F5) :
		(ctrl & ~YT921X_RMA_CTRL_F5);

	return yt921x_reg_write(priv, reg, ctrl);
}

int yt921x_rma_setup_locked(struct yt921x_priv *priv)
{
	/* 01:80:c2:00:00:xx low-byte indexes:
	 *   0x00 BPDU, 0x02 Slow Protocols (LACP), 0x03 EAPOL, 0x0e LLDP.
	 * Trap to CPU for software control-plane handling.
	 */
	static const u8 trap_to_cpu[] = { 0x00, 0x02, 0x03, 0x0e };
	enum yt921x_rma_action slow_action = YT921X_RMA_ACT_TRAP_TO_CPU;
	int slow_action_opt = yt921x_rma_slow_action;
	int i;
	int res;

	if (priv->dt_rma_slow_action != -1)
		slow_action_opt = priv->dt_rma_slow_action;

	if (priv->dt_rma_slow_action != -1)
		dev_info(yt921x_dev(priv), "DT override: rma-slow-action=%d\n",
			 priv->dt_rma_slow_action);

	if (slow_action_opt >= YT921X_RMA_ACT_FORWARD &&
	    slow_action_opt <= YT921X_RMA_ACT_DROP)
		slow_action = slow_action_opt;
	else if (slow_action_opt != -1)
		dev_warn(yt921x_dev(priv),
			 "Invalid rma_slow_action=%d, using default trap\n",
			 slow_action_opt);

	for (i = 0; i < ARRAY_SIZE(trap_to_cpu); i++) {
		enum yt921x_rma_action action = YT921X_RMA_ACT_TRAP_TO_CPU;

		if (trap_to_cpu[i] == 0x02)
			action = slow_action;

		res = yt921x_stock_rma_ctrl_set(
			priv, trap_to_cpu[i], action,
			true, true);
		if (res)
			return res;
	}

	return 0;
}

int yt921x_ctrlpkt_setup_locked(struct yt921x_priv *priv)
{
	struct device *dev = yt921x_dev(priv);
	int lldp_eee_act = yt921x_ctrlpkt_lldp_eee_act;
	int lldp_act = yt921x_ctrlpkt_lldp_act;
	int res;

	if (priv->dt_ctrlpkt_lldp_eee_act != -1)
		lldp_eee_act = priv->dt_ctrlpkt_lldp_eee_act;

	if (priv->dt_ctrlpkt_lldp_act != -1)
		lldp_act = priv->dt_ctrlpkt_lldp_act;

	if (priv->dt_ctrlpkt_lldp_eee_act != -1)
		dev_info(dev, "DT override: ctrlpkt-lldp-eee-act=0x%x\n",
			 priv->dt_ctrlpkt_lldp_eee_act);

	if (priv->dt_ctrlpkt_lldp_act != -1)
		dev_info(dev, "DT override: ctrlpkt-lldp-act=0x%x\n",
			 priv->dt_ctrlpkt_lldp_act);

	if (lldp_eee_act != -1) {
		if (lldp_eee_act & ~YT921X_FILTER_PORTS_M) {
			dev_err(dev, "Invalid ctrlpkt_lldp_eee_act=0x%x\n",
				lldp_eee_act);
			return -EINVAL;
		}

		res = yt921x_reg_write(priv, YT921X_CTRLPKT_LLDP_EEE_ACT,
				       lldp_eee_act);
		if (res)
			return res;
	}

	if (lldp_act != -1) {
		if (lldp_act & ~YT921X_FILTER_PORTS_M) {
			dev_err(dev, "Invalid ctrlpkt_lldp_act=0x%x\n",
				lldp_act);
			return -EINVAL;
		}

		res = yt921x_reg_write(priv, YT921X_CTRLPKT_LLDP_ACT,
				       lldp_act);
		if (res)
			return res;
	}

	return 0;
}

int yt921x_apply_flood_filters_locked(struct yt921x_priv *priv)
{
	u32 unk_ucast_mask = priv->flood_unk_ucast_base_mask;
	u32 mcast_mask = priv->flood_mcast_base_mask | priv->flood_storm_mask;
	u32 bcast_mask = priv->flood_bcast_base_mask | priv->flood_storm_mask;
	int res;

	unk_ucast_mask &= YT921X_FILTER_PORTS_M;
	mcast_mask &= YT921X_FILTER_PORTS_M;
	bcast_mask &= YT921X_FILTER_PORTS_M;

	res = yt921x_reg_write(priv, YT921X_FILTER_UNK_UCAST, unk_ucast_mask);
	if (res)
		return res;

	res = yt921x_reg_write(priv, YT921X_FILTER_MCAST, mcast_mask);
	if (res)
		return res;

	return yt921x_reg_write(priv, YT921X_FILTER_BCAST, bcast_mask);
}

int yt921x_refresh_flood_masks_locked(struct yt921x_priv *priv)
{
	struct dsa_switch *ds = &priv->ds;
	struct dsa_port *dp;
	u16 unk_ucast_mask = BIT(10);
	u16 mcast_mask = BIT(10);
	u16 bcast_mask = BIT(10);

	dsa_switch_for_each_user_port(dp, ds) {
		struct yt921x_port *pp = &priv->ports[dp->index];

		/* BR_MCAST_FLOOD/BR_BCAST_FLOOD are bridge-only controls. */
		if (!dsa_port_bridge_dev_get(dp))
			continue;

		if (!pp->ucast_flood)
			unk_ucast_mask |= BIT(dp->index);
		if (!pp->mcast_flood)
			mcast_mask |= BIT(dp->index);
		if (!pp->bcast_flood)
			bcast_mask |= BIT(dp->index);
	}

	priv->flood_unk_ucast_base_mask = unk_ucast_mask;
	priv->flood_mcast_base_mask = mcast_mask;
	priv->flood_bcast_base_mask = bcast_mask;

	return yt921x_apply_flood_filters_locked(priv);
}

/* Some registers, like VLANn_CTRL, should always be written in 64-bit, even if
 * you are to write only the lower / upper 32 bits.
 *
 * There is no such restriction for reading, but we still provide 64-bit read
 * wrappers so that we always handle u64 values.
 */

int yt921x_reg64_read(struct yt921x_priv *priv, u32 reg, u64 *valp)
{
	u32 lo;
	u32 hi;
	int res;

	res = yt921x_reg_read(priv, reg, &lo);
	if (res)
		return res;
	res = yt921x_reg_read(priv, reg + 4, &hi);
	if (res)
		return res;

	*valp = ((u64)hi << 32) | lo;
	return 0;
}

int yt921x_reg64_write(struct yt921x_priv *priv, u32 reg, u64 val)
{
	int res;

	res = yt921x_reg_write(priv, reg, (u32)val);
	if (res)
		return res;
	return yt921x_reg_write(priv, reg + 4, (u32)(val >> 32));
}

int
yt921x_reg64_update_bits(struct yt921x_priv *priv, u32 reg, u64 mask, u64 val)
{
	int res;
	u64 v;
	u64 u;

	res = yt921x_reg64_read(priv, reg, &v);
	if (res)
		return res;

	u = v;
	u &= ~mask;
	u |= val;
	if (u == v)
		return 0;

	return yt921x_reg64_write(priv, reg, u);
}

int yt921x_reg64_clear_bits(struct yt921x_priv *priv, u32 reg, u64 mask)
{
	return yt921x_reg64_update_bits(priv, reg, mask, 0);
}

int yt921x_reg96_write(struct yt921x_priv *priv, u32 reg,
			      const u32 vals[3])
{
	int res;

	res = yt921x_reg_write(priv, reg, vals[0]);
	if (res)
		return res;
	res = yt921x_reg_write(priv, reg + 4, vals[1]);
	if (res)
		return res;

	return yt921x_reg_write(priv, reg + 8, vals[2]);
}
