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
	WARN_ON(!mutex_is_locked(&priv->reg_lock));

	return priv->reg_ops->read(priv->reg_ctx, reg, valp);
}

int yt921x_reg_write(struct yt921x_priv *priv, u32 reg, u32 val)
{
	WARN_ON(!mutex_is_locked(&priv->reg_lock));

	return priv->reg_ops->write(priv->reg_ctx, reg, val);
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
	if (ret)
		return ret;
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

static enum yt921x_rma_action yt921x_rma_ctrl_action_get(u32 ctrl)
{
	bool f3 = !!(ctrl & YT921X_RMA_CTRL_F3);
	bool f4 = !!(ctrl & YT921X_RMA_CTRL_F4);

	if (f3 && f4)
		return YT921X_RMA_ACT_TRAP_TO_CPU;
	if (!f3 && f4)
		return YT921X_RMA_ACT_COPY_TO_CPU;
	if (f3)
		return YT921X_RMA_ACT_DROP;

	return YT921X_RMA_ACT_FORWARD;
}

static const char *yt921x_rma_action_name(enum yt921x_rma_action action)
{
	switch (action) {
	case YT921X_RMA_ACT_FORWARD:
		return "forward";
	case YT921X_RMA_ACT_TRAP_TO_CPU:
		return "trap";
	case YT921X_RMA_ACT_COPY_TO_CPU:
		return "copy";
	case YT921X_RMA_ACT_DROP:
		return "drop";
	default:
		return "unknown";
	}
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
	int i;
	int res;

	for (i = 0; i < ARRAY_SIZE(trap_to_cpu); i++) {
		res = yt921x_stock_rma_ctrl_set(
			priv, trap_to_cpu[i], YT921X_RMA_ACT_TRAP_TO_CPU,
			true, true);
		if (res)
			return res;
	}

	return 0;
}

int yt921x_apply_flood_filters_locked(struct yt921x_priv *priv)
{
	u32 mcast_mask = priv->flood_mcast_base_mask | priv->flood_storm_mask;
	u32 bcast_mask = priv->flood_bcast_base_mask | priv->flood_storm_mask;
	int res;

	mcast_mask &= YT921X_FILTER_PORTS_M;
	bcast_mask &= YT921X_FILTER_PORTS_M;

	res = yt921x_reg_write(priv, YT921X_FILTER_MCAST, mcast_mask);
	if (res)
		return res;

	return yt921x_reg_write(priv, YT921X_FILTER_BCAST, bcast_mask);
}

int yt921x_refresh_flood_masks_locked(struct yt921x_priv *priv)
{
	struct dsa_switch *ds = &priv->ds;
	struct dsa_port *dp;
	u16 mcast_mask = BIT(10);
	u16 bcast_mask = BIT(10);

	dsa_switch_for_each_user_port(dp, ds) {
		struct yt921x_port *pp = &priv->ports[dp->index];

		/* BR_MCAST_FLOOD/BR_BCAST_FLOOD are bridge-only controls. */
		if (!dsa_port_bridge_dev_get(dp))
			continue;

		if (!pp->mcast_flood)
			mcast_mask |= BIT(dp->index);
		if (!pp->bcast_flood)
			bcast_mask |= BIT(dp->index);
	}

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

static int yt921x_reg_mdio_read(void *context, u32 reg, u32 *valp)
{
	struct yt921x_reg_mdio *mdio = context;
	struct mii_bus *bus = mdio->bus;
	int addr = mdio->addr;
	u32 reg_addr;
	u32 reg_data;
	u32 val;
	int res;

	/* Hold the mdio bus lock to avoid (un)locking for 4 times */
	mutex_lock_nested(&bus->mdio_lock, MDIO_MUTEX_NESTED);

	reg_addr = YT921X_SMI_SWITCHID(mdio->switchid) | YT921X_SMI_ADDR |
		   YT921X_SMI_READ;
	res = __mdiobus_write(bus, addr, reg_addr, (u16)(reg >> 16));
	if (res)
		goto end;
	res = __mdiobus_write(bus, addr, reg_addr, (u16)reg);
	if (res)
		goto end;

	reg_data = YT921X_SMI_SWITCHID(mdio->switchid) | YT921X_SMI_DATA |
		   YT921X_SMI_READ;
	res = __mdiobus_read(bus, addr, reg_data);
	if (res < 0)
		goto end;
	val = (u16)res;
	res = __mdiobus_read(bus, addr, reg_data);
	if (res < 0)
		goto end;
	val = (val << 16) | (u16)res;

	*valp = val;
	res = 0;

end:
	mutex_unlock(&bus->mdio_lock);
	return res;
}

static int yt921x_reg_mdio_write(void *context, u32 reg, u32 val)
{
	struct yt921x_reg_mdio *mdio = context;
	struct mii_bus *bus = mdio->bus;
	int addr = mdio->addr;
	u32 reg_addr;
	u32 reg_data;
	int res;

	mutex_lock_nested(&bus->mdio_lock, MDIO_MUTEX_NESTED);

	reg_addr = YT921X_SMI_SWITCHID(mdio->switchid) | YT921X_SMI_ADDR |
		   YT921X_SMI_WRITE;
	res = __mdiobus_write(bus, addr, reg_addr, (u16)(reg >> 16));
	if (res)
		goto end;
	res = __mdiobus_write(bus, addr, reg_addr, (u16)reg);
	if (res)
		goto end;

	reg_data = YT921X_SMI_SWITCHID(mdio->switchid) | YT921X_SMI_DATA |
		   YT921X_SMI_WRITE;
	res = __mdiobus_write(bus, addr, reg_data, (u16)(val >> 16));
	if (res)
		goto end;
	res = __mdiobus_write(bus, addr, reg_data, (u16)val);
	if (res)
		goto end;

	res = 0;

end:
	mutex_unlock(&bus->mdio_lock);
	return res;
}

const struct yt921x_reg_ops yt921x_reg_ops_mdio = {
	.read = yt921x_reg_mdio_read,
	.write = yt921x_reg_mdio_write,
};

/* TODO: SPI/I2C */

static u32 yt921x_mbus_op_reg(bool extif)
{
	return extif ? YT921X_EXT_MBUS_OP : YT921X_INT_MBUS_OP;
}

static u32 yt921x_mbus_ctrl_reg(bool extif)
{
	return extif ? YT921X_EXT_MBUS_CTRL : YT921X_INT_MBUS_CTRL;
}

static u32 yt921x_mbus_dout_reg(bool extif)
{
	return extif ? YT921X_EXT_MBUS_DOUT : YT921X_INT_MBUS_DOUT;
}

static u32 yt921x_mbus_din_reg(bool extif)
{
	return extif ? YT921X_EXT_MBUS_DIN : YT921X_INT_MBUS_DIN;
}

static int yt921x_mbus_wait(struct yt921x_priv *priv, bool extif)
{
	u32 val = 0;

	return yt921x_reg_wait(priv, yt921x_mbus_op_reg(extif),
			       YT921X_MBUS_OP_START, &val);
}

static int yt921x_intif_wait(struct yt921x_priv *priv)
{
	return yt921x_mbus_wait(priv, false);
}

static int yt921x_extif_wait(struct yt921x_priv *priv)
{
	return yt921x_mbus_wait(priv, true);
}

static int
yt921x_mbus_read(struct yt921x_priv *priv, bool extif, int port, int reg,
		 u16 *valp)
{
	struct device *dev = yt921x_dev(priv);
	u32 op_reg = yt921x_mbus_op_reg(extif);
	u32 ctrl_reg = yt921x_mbus_ctrl_reg(extif);
	u32 din_reg = yt921x_mbus_din_reg(extif);
	u32 mask;
	u32 ctrl;
	u32 val;
	int res;

	res = extif ? yt921x_extif_wait(priv) : yt921x_intif_wait(priv);
	if (res)
		return res;

	mask = YT921X_MBUS_CTRL_PORT_M | YT921X_MBUS_CTRL_REG_M |
	       YT921X_MBUS_CTRL_OP_M;
	ctrl = YT921X_MBUS_CTRL_PORT(port) | YT921X_MBUS_CTRL_REG(reg) |
	       YT921X_MBUS_CTRL_READ;
	if (extif) {
		mask |= YT921X_MBUS_CTRL_TYPE_M;
		ctrl |= YT921X_MBUS_CTRL_TYPE_C22;
	}

	res = yt921x_reg_update_bits(priv, ctrl_reg, mask, ctrl);
	if (res)
		return res;

	res = yt921x_reg_write(priv, op_reg, YT921X_MBUS_OP_START);
	if (res)
		return res;

	res = extif ? yt921x_extif_wait(priv) : yt921x_intif_wait(priv);
	if (res)
		return res;

	res = yt921x_reg_read(priv, din_reg, &val);
	if (res)
		return res;

	if ((u16)val != val)
		dev_dbg(dev,
			"%s: port %d, reg 0x%x: Expected u16, got 0x%08x\n",
			extif ? "yt921x_extif_read" : "yt921x_intif_read",
			port, reg, val);
	*valp = (u16)val;

	return 0;
}

static int
yt921x_mbus_write(struct yt921x_priv *priv, bool extif, int port, int reg,
		  u16 val)
{
	u32 op_reg = yt921x_mbus_op_reg(extif);
	u32 ctrl_reg = yt921x_mbus_ctrl_reg(extif);
	u32 dout_reg = yt921x_mbus_dout_reg(extif);
	u32 mask;
	u32 ctrl;
	int res;

	res = extif ? yt921x_extif_wait(priv) : yt921x_intif_wait(priv);
	if (res)
		return res;

	mask = YT921X_MBUS_CTRL_PORT_M | YT921X_MBUS_CTRL_REG_M |
	       YT921X_MBUS_CTRL_OP_M;
	ctrl = YT921X_MBUS_CTRL_PORT(port) | YT921X_MBUS_CTRL_REG(reg) |
	       YT921X_MBUS_CTRL_WRITE;
	if (extif) {
		mask |= YT921X_MBUS_CTRL_TYPE_M;
		ctrl |= YT921X_MBUS_CTRL_TYPE_C22;
	}

	res = yt921x_reg_update_bits(priv, ctrl_reg, mask, ctrl);
	if (res)
		return res;

	res = yt921x_reg_write(priv, dout_reg, val);
	if (res)
		return res;

	res = yt921x_reg_write(priv, op_reg, YT921X_MBUS_OP_START);
	if (res)
		return res;

	return extif ? yt921x_extif_wait(priv) : yt921x_intif_wait(priv);
}

int
yt921x_intif_read(struct yt921x_priv *priv, int port, int reg, u16 *valp)
{
	return yt921x_mbus_read(priv, false, port, reg, valp);
}

int
yt921x_intif_write(struct yt921x_priv *priv, int port, int reg, u16 val)
{
	return yt921x_mbus_write(priv, false, port, reg, val);
}

static int yt921x_mbus_port_reg_validate(int port, int reg, int max_port)
{
	if (port < 0 || port > max_port || reg < 0 || reg > 0x1f)
		return -EINVAL;

	return 0;
}

static int
yt921x_mbus_bus_read(struct mii_bus *mbus, bool extif, int max_port, int port,
		     int reg)
{
	struct yt921x_priv *priv = mbus->priv;
	u16 val;
	int res;

	res = yt921x_mbus_port_reg_validate(port, reg, max_port);
	if (res)
		return res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_mbus_read(priv, extif, port, reg, &val);
	mutex_unlock(&priv->reg_lock);

	if (res)
		return res;
	return val;
}

static int
yt921x_mbus_bus_write(struct mii_bus *mbus, bool extif, int max_port, int port,
		      int reg, u16 data)
{
	struct yt921x_priv *priv = mbus->priv;
	int res;

	res = yt921x_mbus_port_reg_validate(port, reg, max_port);
	if (res)
		return res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_mbus_write(priv, extif, port, reg, data);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int yt921x_mbus_int_read(struct mii_bus *mbus, int port, int reg)
{
	return yt921x_mbus_bus_read(mbus, false, YT921X_PORT_NUM - 1, port, reg);
}

static int
yt921x_mbus_int_write(struct mii_bus *mbus, int port, int reg, u16 data)
{
	return yt921x_mbus_bus_write(mbus, false, YT921X_PORT_NUM - 1, port, reg,
				     data);
}

int
yt921x_mbus_int_init(struct yt921x_priv *priv, struct device_node *mnp)
{
	struct device *dev = yt921x_dev(priv);
	struct mii_bus *mbus;
	int res;

	mbus = devm_mdiobus_alloc(dev);
	if (!mbus)
		return -ENOMEM;

	mbus->name = "YT921x internal MDIO bus";
	snprintf(mbus->id, MII_BUS_ID_SIZE, "%s", dev_name(dev));
	mbus->priv = priv;
	mbus->read = yt921x_mbus_int_read;
	mbus->write = yt921x_mbus_int_write;
	mbus->parent = dev;
	mbus->phy_mask = (u32)~GENMASK(YT921X_PORT_NUM - 1, 0);

	res = devm_of_mdiobus_register(dev, mbus, mnp);
	if (res)
		return res;

	priv->mbus_int = mbus;

	return 0;
}

int
yt921x_extif_read(struct yt921x_priv *priv, int port, int reg, u16 *valp)
{
	return yt921x_mbus_read(priv, true, port, reg, valp);
}

int
yt921x_extif_write(struct yt921x_priv *priv, int port, int reg, u16 val)
{
	return yt921x_mbus_write(priv, true, port, reg, val);
}

static int yt921x_mbus_ext_read(struct mii_bus *mbus, int port, int reg)
{
	return yt921x_mbus_bus_read(mbus, true, 0x1f, port, reg);
}

static int
yt921x_mbus_ext_write(struct mii_bus *mbus, int port, int reg, u16 data)
{
	return yt921x_mbus_bus_write(mbus, true, 0x1f, port, reg, data);
}

static int
yt921x_mbus_ext_c45_prepare(struct yt921x_priv *priv, int port, int devnum,
			      int regnum)
{
	int res;

	if (port < 0 || port > 0x1f || devnum < 0 ||
	    devnum > MII_MMD_CTRL_DEVAD_MASK || regnum < 0 || regnum > 0xffff)
		return -EINVAL;

	res = yt921x_extif_write(priv, port, MII_MMD_CTRL, devnum);
	if (res)
		return res;

	res = yt921x_extif_write(priv, port, MII_MMD_DATA, regnum);
	if (res)
		return res;

	return yt921x_extif_write(priv, port, MII_MMD_CTRL,
				  devnum | MII_MMD_CTRL_NOINCR);
}

static int
yt921x_mbus_ext_read_c45(struct mii_bus *mbus, int port, int devnum, int regnum)
{
	struct yt921x_priv *priv = mbus->priv;
	u16 val;
	int res;

	mutex_lock(&priv->reg_lock);

	res = yt921x_mbus_ext_c45_prepare(priv, port, devnum, regnum);
	if (res)
		goto out_unlock;

	res = yt921x_extif_read(priv, port, MII_MMD_DATA, &val);

out_unlock:
	mutex_unlock(&priv->reg_lock);

	if (res)
		return res;
	return val;
}

static int
yt921x_mbus_ext_write_c45(struct mii_bus *mbus, int port, int devnum,
			   int regnum, u16 data)
{
	struct yt921x_priv *priv = mbus->priv;
	int res;

	mutex_lock(&priv->reg_lock);

	res = yt921x_mbus_ext_c45_prepare(priv, port, devnum, regnum);
	if (!res)
		res = yt921x_extif_write(priv, port, MII_MMD_DATA, data);

	mutex_unlock(&priv->reg_lock);

	return res;
}

int
yt921x_mbus_ext_init(struct yt921x_priv *priv, struct device_node *mnp)
{
	struct device *dev = yt921x_dev(priv);
	struct mii_bus *mbus;
	int res;

	mbus = devm_mdiobus_alloc(dev);
	if (!mbus)
		return -ENOMEM;

	mbus->name = "YT921x external MDIO bus";
	snprintf(mbus->id, MII_BUS_ID_SIZE, "%s@ext", dev_name(dev));
	mbus->priv = priv;
	mbus->read = yt921x_mbus_ext_read;
	mbus->write = yt921x_mbus_ext_write;
	mbus->read_c45 = yt921x_mbus_ext_read_c45;
	mbus->write_c45 = yt921x_mbus_ext_write_c45;
	mbus->parent = dev;

	res = devm_of_mdiobus_register(dev, mbus, mnp);
	if (res)
		return res;

	priv->mbus_ext = mbus;

	return 0;
}

#if IS_ENABLED(CONFIG_NET_DSA_YT921X_DEBUG)
static void yt921x_proc_reply_reset(struct yt921x_priv *priv)
{
	priv->proc_reply[0] = '\0';
	priv->proc_reply_len = 0;
}

static void yt921x_proc_reply_append(struct yt921x_priv *priv, const char *fmt,
				     ...)
{
	va_list ap;

	if (priv->proc_reply_len >= sizeof(priv->proc_reply) - 1)
		return;

	va_start(ap, fmt);
	priv->proc_reply_len += vscnprintf(priv->proc_reply +
					   priv->proc_reply_len,
					   sizeof(priv->proc_reply) -
					   priv->proc_reply_len,
					   fmt, ap);
	va_end(ap);
}

static void yt921x_proc_reply_help(struct yt921x_priv *priv)
{
	yt921x_proc_reply_reset(priv);
	yt921x_proc_reply_append(priv,
				 "commands:\n"
				 "  help\n"
				 "  reg read <addr>\n"
				 "  reg write <addr> <val>\n"
				 "  int read <port> <reg>\n"
				 "  int write <port> <reg> <val>\n"
				 "  ext read <port> <reg>\n"
				 "  ext write <port> <reg> <val>\n"
				 "  tbl info <id>\n"
				 "  tbl read <id> <index>\n"
				 "  tbl write <id> <index> <word> <val>\n"
				 "  field get <id> <index> <field|field_idx>\n"
				 "  field set <id> <index> <field|field_idx> <val>\n"
				 "  get_flood_filter\n"
				 "  set_flood_filter <mcast|bcast|both> <mask>\n"
				 "  port_status [port]\n"
				 "  ctrlpkt show\n"
				 "  ctrlpkt set <arp|nd|lldp_eee|lldp> <val>\n"
				 "  dot1x show [port]\n"
				 "  dot1x set <port> <port_based_val> <bypass_val>\n"
				 "  loop_detect show\n"
				 "  loop_detect set <0|1> [tpid] [gen_way]\n"
				 "  wol show\n"
				 "  wol set <0|1> [ethertype]\n"
				 "  unk show\n"
				 "  unk set filter <ucast|mcast|both> <mask>\n"
				 "  unk set action <ucast|mcast> <port> <flood|trap|drop|copy>\n"
				 "  unk set bypass <igmp|rma> <0|1>\n"
				 "  rma show [idx]\n"
				 "  rma set <idx> <forward|trap|copy|drop> [bypass_iso] [bypass_vlan]\n"
				 "  storm_guard show\n"
				 "  storm_guard set <0|1> <pps> <hold_ms> <interval_ms>\n"
				 "  mirror\n"
				 "  tbf [port]\n"
				 "  vlan dump <vid>\n"
				 "  vlan mode show\n"
				 "  vlan mode set <port|ctag|stag|tag|proto> <0|1>\n"
				 "  vlan fid_mode show\n"
				 "  vlan fid_mode set <ivl|svl> [svl_fid]\n"
				 "  vlan fid show <vid>\n"
				 "  vlan fid set <vid> <fid>\n"
				 "  vlan 1x_bypass <vid> [0|1]\n"
				 "  pvid dump [port]\n"
				 "  stock map <reg>\n"
				 "  acl_chain show\n"
				 "  acl_chain set <key1_mask>\n"
				 "  dump <start> <end> [stride]\n");
}

static int yt921x_proc_parse_u32(const char *s, u32 *val)
{
	return kstrtou32(s, 0, val);
}

static int yt921x_proc_parse_u16(const char *s, u16 *val)
{
	return kstrtou16(s, 0, val);
}

static int
yt921x_proc_parse_rma_action(const char *s, enum yt921x_rma_action *action)
{
	if (!strcmp(s, "forward") || !strcmp(s, "fwd")) {
		*action = YT921X_RMA_ACT_FORWARD;
		return 0;
	}
	if (!strcmp(s, "trap")) {
		*action = YT921X_RMA_ACT_TRAP_TO_CPU;
		return 0;
	}
	if (!strcmp(s, "copy")) {
		*action = YT921X_RMA_ACT_COPY_TO_CPU;
		return 0;
	}
	if (!strcmp(s, "drop")) {
		*action = YT921X_RMA_ACT_DROP;
		return 0;
	}

	return -EINVAL;
}

static const char *yt921x_proc_unk_action_name(u32 act)
{
	switch (act) {
	case 0:
		return "flood";
	case 1:
		return "trap";
	case 2:
		return "drop";
	case 3:
		return "copy";
	default:
		return "unknown";
	}
}

static int yt921x_proc_parse_unk_action(const char *s, u32 *act)
{
	if (!strcmp(s, "flood")) {
		*act = 0;
		return 0;
	}
	if (!strcmp(s, "trap")) {
		*act = 1;
		return 0;
	}
	if (!strcmp(s, "drop")) {
		*act = 2;
		return 0;
	}
	if (!strcmp(s, "copy")) {
		*act = 3;
		return 0;
	}

	return -EINVAL;
}

static int yt921x_proc_parse_vlan_mode(const char *s, u32 *mask)
{
	if (!strcmp(s, "port")) {
		*mask = YT921X_VLAN_IGR_TRANS_CTRL_PORT_MODE;
		return 0;
	}
	if (!strcmp(s, "ctag")) {
		*mask = YT921X_VLAN_IGR_TRANS_CTRL_CTAG_MODE;
		return 0;
	}
	if (!strcmp(s, "stag")) {
		*mask = YT921X_VLAN_IGR_TRANS_CTRL_STAG_MODE;
		return 0;
	}
	if (!strcmp(s, "tag")) {
		*mask = YT921X_VLAN_IGR_TRANS_CTRL_CTAG_MODE |
			YT921X_VLAN_IGR_TRANS_CTRL_STAG_MODE;
		return 0;
	}
	if (!strcmp(s, "proto")) {
		*mask = YT921X_VLAN_IGR_TRANS_CTRL_PROTO_MODE;
		return 0;
	}

	return -EINVAL;
}

static int yt921x_proc_parse_vlan_fid_mode(const char *s, u8 *mode)
{
	if (!strcmp(s, "ivl")) {
		*mode = YT921X_VLAN_FID_MODE_IVL;
		return 0;
	}
	if (!strcmp(s, "svl")) {
		*mode = YT921X_VLAN_FID_MODE_SVL;
		return 0;
	}

	return -EINVAL;
}

static const char *yt921x_proc_vlan_fid_mode_name(u8 mode)
{
	switch (mode) {
	case YT921X_VLAN_FID_MODE_IVL:
		return "ivl";
	case YT921X_VLAN_FID_MODE_SVL:
		return "svl";
	default:
		return "unknown";
	}
}

static int yt921x_proc_reply_rma_index(struct yt921x_priv *priv, u32 index)
{
	enum yt921x_rma_action action;
	u32 ctrl;
	int res;

	if (index >= 0x30)
		return -ERANGE;

	res = yt921x_reg_read(priv, YT921X_RMA_CTRLn(index), &ctrl);
	if (res)
		return res;

	action = yt921x_rma_ctrl_action_get(ctrl);
	yt921x_proc_reply_append(
		priv,
		"rma idx=0x%02x reg=0x%06x val=0x%08x action=%s fwd_mask=0x%02x bypass_iso=%u bypass_vlan=%u\n",
		index, YT921X_RMA_CTRLn(index), ctrl,
		yt921x_rma_action_name(action),
		(u32)FIELD_GET(YT921X_RMA_CTRL_FWD_MASK_M, ctrl),
		!!(ctrl & YT921X_RMA_CTRL_F6),
		!!(ctrl & YT921X_RMA_CTRL_F5));

	return 0;
}

static int yt921x_proc_reply_unknown_policy(struct yt921x_priv *priv)
{
	u32 filter_uc;
	u32 filter_mc;
	u32 act_uc;
	u32 act_mc;
	u32 port;
	int res;

	res = yt921x_reg_read(priv, YT921X_FILTER_UNK_UCAST, &filter_uc);
	if (res)
		return res;
	res = yt921x_reg_read(priv, YT921X_FILTER_UNK_MCAST, &filter_mc);
	if (res)
		return res;
	res = yt921x_reg_read(priv, YT921X_ACT_UNK_UCAST, &act_uc);
	if (res)
		return res;
	res = yt921x_reg_read(priv, YT921X_ACT_UNK_MCAST, &act_mc);
	if (res)
		return res;

	yt921x_proc_reply_append(
		priv,
		"unk filter_ucast=0x%03x filter_mcast=0x%03x act_ucast=0x%08x act_mcast=0x%08x bypass_igmp=%u bypass_rma=%u\n",
		filter_uc & YT921X_FILTER_PORTS_M,
		filter_mc & YT921X_FILTER_PORTS_M,
		act_uc, act_mc,
		!!(act_mc & YT921X_ACT_UNK_MCAST_BYPASS_DROP_IGMP),
		!!(act_mc & YT921X_ACT_UNK_MCAST_BYPASS_DROP_RMA));

	for (port = 0; port < YT921X_PORT_NUM; port++) {
		u32 shift = 2 * port;
		u32 uc = (act_uc >> shift) & 0x3;
		u32 mc = (act_mc >> shift) & 0x3;

		yt921x_proc_reply_append(priv,
					 "unk p%u ucast=%s(%u) mcast=%s(%u)\n",
					 port, yt921x_proc_unk_action_name(uc), uc,
					 yt921x_proc_unk_action_name(mc), mc);
	}

	return 0;
}

struct yt921x_proc_ctrlpkt_desc {
	const char *name;
	u32 reg;
	u8 tbl_id;
};

static const struct yt921x_proc_ctrlpkt_desc yt921x_proc_ctrlpkt_descs[] = {
	{ "arp", YT921X_CTRLPKT_ARP_ACT, 0x74 },
	{ "nd", YT921X_CTRLPKT_ND_ACT, 0x75 },
	{ "lldp_eee", YT921X_CTRLPKT_LLDP_EEE_ACT, 0x76 },
	{ "lldp", YT921X_CTRLPKT_LLDP_ACT, 0x77 },
};

static const struct yt921x_proc_ctrlpkt_desc *
yt921x_proc_ctrlpkt_find(const char *name)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(yt921x_proc_ctrlpkt_descs); i++) {
		if (!strcmp(name, yt921x_proc_ctrlpkt_descs[i].name))
			return &yt921x_proc_ctrlpkt_descs[i];
	}

	return NULL;
}

static int yt921x_proc_reply_ctrlpkt(struct yt921x_priv *priv)
{
	unsigned int i;
	int res;

	for (i = 0; i < ARRAY_SIZE(yt921x_proc_ctrlpkt_descs); i++) {
		const struct yt921x_proc_ctrlpkt_desc *d =
			&yt921x_proc_ctrlpkt_descs[i];
		u32 val;

		res = yt921x_reg_read(priv, d->reg, &val);
		if (res)
			return res;

		yt921x_proc_reply_append(priv,
					 "ctrlpkt %-8s tbl=0x%02x reg=0x%06x val=0x%08x\n",
					 d->name, d->tbl_id, d->reg, val);
	}

	return 0;
}

static int yt921x_proc_reply_dot1x(struct yt921x_priv *priv, u32 start, u32 end)
{
	u32 port_based_mask;
	u32 bypass_mask;
	u32 port;
	int res;

	res = yt921x_reg_read(priv, YT921X_DOT1X_PORT_BASED, &port_based_mask);
	if (res)
		return res;

	res = yt921x_reg_read(priv, YT921X_DOT1X_BYPASS_CTRL, &bypass_mask);
	if (res)
		return res;

	for (port = start; port <= end; port++) {
		yt921x_proc_reply_append(
			priv,
			"dot1x p%u port_based=%u bypass=%u raw_port_based=0x%08x raw_bypass=0x%08x\n",
			port,
			!!(port_based_mask & BIT(port)),
			!!(bypass_mask & BIT(port)),
			port_based_mask, bypass_mask);
	}

	return 0;
}

static const char *yt921x_proc_port_speed_name(u32 speed)
{
	switch (speed) {
	case 0:
		return "10";
	case 1:
		return "100";
	case 2:
		return "1000";
	case 3:
		return "10000";
	case 4:
		return "2500";
	default:
		return "unknown";
	}
}

static int yt921x_proc_reply_port_status(struct yt921x_priv *priv, u32 port)
{
	u32 ctrl;
	u32 st;
	u32 speed;
	const char *duplex;
	const char *speed_name;
	int res;

	res = yt921x_reg_read(priv, YT921X_PORTn_CTRL(port), &ctrl);
	if (res)
		return res;
	res = yt921x_reg_read(priv, YT921X_PORTn_STATUS(port), &st);
	if (res)
		return res;

	speed = FIELD_GET(YT921X_PORT_SPEED_M, st);
	duplex = (st & YT921X_PORT_DUPLEX_FULL) ? "full" : "half";
	speed_name = (st & YT921X_PORT_LINK) ? yt921x_proc_port_speed_name(speed) :
					     "down";

	yt921x_proc_reply_append(
		priv,
		"p%u ctrl=0x%08x status=0x%08x link=%u an=%u speed=%s duplex=%s rx_pause=%u tx_pause=%u rx_mac=%u tx_mac=%u\n",
		port, ctrl, st, !!(st & YT921X_PORT_LINK),
		!!(ctrl & YT921X_PORT_LINK), speed_name, duplex,
		!!(st & YT921X_PORT_RX_PAUSE), !!(st & YT921X_PORT_TX_PAUSE),
		!!(st & YT921X_PORT_RX_MAC_EN), !!(st & YT921X_PORT_TX_MAC_EN));

	return 0;
}

static void
yt921x_proc_reply_port_mask(struct yt921x_priv *priv, u32 mask)
{
	bool empty = true;
	int port;

	for (port = 0; port < YT921X_PORT_NUM; port++) {
		if (!(mask & BIT(port)))
			continue;
		yt921x_proc_reply_append(priv, "%s%d", empty ? "" : ",", port);
		empty = false;
	}

	if (empty)
		yt921x_proc_reply_append(priv, "-");
}

static void
yt921x_proc_stock_map(u32 reg, u16 *page, u16 *phy, u16 *word0, u16 *word1)
{
	*page = (reg >> 9) & 0x3ff;
	*phy = ((reg >> 6) & 0x7) | 0x10;
	*word0 = (reg >> 1) & 0x1e;
	*word1 = *word0 + 1;
}

static int yt921x_proc_reply_tbf_port(struct yt921x_priv *priv, u32 port)
{
	u64 burst_bytes;
	u32 rate_kbps;
	u32 ebs_eir;
	u32 ctrl;
	u32 eir;
	u32 ebs;
	int res;

	res = yt921x_reg_read(priv, YT921X_PSCH_SHPn_CTRL(port), &ctrl);
	if (res)
		return res;

	res = yt921x_reg_read(priv, YT921X_PSCH_SHPn_EBS_EIR(port), &ebs_eir);
	if (res)
		return res;

	eir = FIELD_GET(YT921X_PSCH_SHP_EIR_M, ebs_eir);
	ebs = FIELD_GET(YT921X_PSCH_SHP_EBS_M, ebs_eir);
	rate_kbps = yt921x_tbf_eir_to_rate_kbps(eir);
	burst_bytes = (u64)ebs * YT921X_PSCH_EBS_UNIT_BYTES;

	yt921x_proc_reply_append(priv,
				 "tbf p%u: en=%u dual_rate=%u meter=%u eir=%u (~%u kbps) ebs=%u (~%llu bytes)\n",
				 port, !!(ctrl & YT921X_PSCH_SHP_EN),
				 !!(ctrl & YT921X_PSCH_SHP_DUAL_RATE),
				 FIELD_GET(YT921X_PSCH_SHP_METER_ID_M, ctrl),
				 eir, rate_kbps, ebs, burst_bytes);

	return 0;
}

static int yt921x_proc_reply_vlan_dump(struct yt921x_priv *priv, u32 vid)
{
	u64 ctrl64;
	u32 members;
	u32 untag;
	u32 fid;
	u32 stp_id;
	int res;

	if (vid > YT921X_VID_UNAWARE)
		return -ERANGE;

	res = yt921x_reg64_read(priv, YT921X_VLANn_CTRL(vid), &ctrl64);
	if (res)
		return res;

	members = FIELD_GET(YT921X_VLAN_CTRL_PORTS_M, ctrl64);
	untag = FIELD_GET(YT921X_VLAN_CTRL_UNTAG_PORTS_M, ctrl64);
	fid = FIELD_GET(YT921X_VLAN_CTRL_FID_M, ctrl64);
	stp_id = FIELD_GET(YT921X_VLAN_CTRL_STP_ID_M, ctrl64);

	yt921x_proc_reply_append(priv,
				 "vlan %u reg=0x%06x val=0x%016llx members=0x%03x untag=0x%03x fid=%u stp=%u learn_dis=%u prio_en=%u bypass_1x=%u\n",
				 vid, YT921X_VLANn_CTRL(vid),
				 (unsigned long long)ctrl64, members, untag, fid,
				 stp_id, !!(ctrl64 & YT921X_VLAN_CTRL_LEARN_DIS),
				 !!(ctrl64 & YT921X_VLAN_CTRL_PRIO_EN),
				 !!(ctrl64 & YT921X_VLAN_CTRL_BYPASS_1X_AC));

	return 0;
}

static int yt921x_proc_reply_vlan_mode(struct yt921x_priv *priv)
{
	u32 ctrl;
	int res;

	res = yt921x_reg_read(priv, YT921X_VLAN_IGR_TRANS_CTRL, &ctrl);
	if (res)
		return res;

	yt921x_proc_reply_append(
		priv,
		"vlan_mode reg=0x%06x val=0x%08x port=%u ctag=%u stag=%u tag=%u proto=%u\n",
		YT921X_VLAN_IGR_TRANS_CTRL, ctrl,
		!!(ctrl & YT921X_VLAN_IGR_TRANS_CTRL_PORT_MODE),
		!!(ctrl & YT921X_VLAN_IGR_TRANS_CTRL_CTAG_MODE),
		!!(ctrl & YT921X_VLAN_IGR_TRANS_CTRL_STAG_MODE),
		!!(ctrl & (YT921X_VLAN_IGR_TRANS_CTRL_CTAG_MODE |
			   YT921X_VLAN_IGR_TRANS_CTRL_STAG_MODE)),
		!!(ctrl & YT921X_VLAN_IGR_TRANS_CTRL_PROTO_MODE));

	return 0;
}

static int yt921x_proc_apply_vlan_fid_mode(struct yt921x_priv *priv)
{
	u16 svl_fid = priv->vlan_svl_fid ? : 1;
	u16 vid;

	for (vid = 1; vid < YT921X_VID_UNAWARE; vid++) {
		u64 ctrl64;
		u16 fid;
		int res;

		res = yt921x_reg64_read(priv, YT921X_VLANn_CTRL(vid), &ctrl64);
		if (res)
			return res;

		if (!(ctrl64 & YT921X_VLAN_CTRL_PORTS_M))
			continue;

		fid = (priv->vlan_fid_mode == YT921X_VLAN_FID_MODE_SVL) ?
			svl_fid : vid;
		ctrl64 &= ~YT921X_VLAN_CTRL_FID_M;
		ctrl64 |= YT921X_VLAN_CTRL_FID(fid);

		res = yt921x_reg64_write(priv, YT921X_VLANn_CTRL(vid), ctrl64);
		if (res)
			return res;
	}

	return 0;
}

static int yt921x_proc_reply_vlan_fid_mode(struct yt921x_priv *priv)
{
	yt921x_proc_reply_append(priv, "vlan_fid_mode=%s svl_fid=%u\n",
				 yt921x_proc_vlan_fid_mode_name(priv->vlan_fid_mode),
				 priv->vlan_svl_fid ? : 1);

	return 0;
}

static int yt921x_proc_reply_pvid_port(struct yt921x_priv *priv, u32 port)
{
	u32 ctrl;
	u32 ctrl1;
	int res;

	if (port >= YT921X_PORT_NUM)
		return -ERANGE;

	res = yt921x_reg_read(priv, YT921X_PORTn_VLAN_CTRL(port), &ctrl);
	if (res)
		return res;
	res = yt921x_reg_read(priv, YT921X_PORTn_VLAN_CTRL1(port), &ctrl1);
	if (res)
		return res;

	yt921x_proc_reply_append(priv,
				 "pvid p%u ctrl=0x%08x ctrl1=0x%08x cvid=%u svid=%u cvlan_drop_tag=%u cvlan_drop_untag=%u\n",
				 port, ctrl, ctrl1,
				 FIELD_GET(YT921X_PORT_VLAN_CTRL_CVID_M, ctrl),
				 FIELD_GET(YT921X_PORT_VLAN_CTRL_SVID_M, ctrl),
				 !!(ctrl1 & YT921X_PORT_VLAN_CTRL1_CVLAN_DROP_TAGGED),
				 !!(ctrl1 & YT921X_PORT_VLAN_CTRL1_CVLAN_DROP_UNTAGGED));

	return 0;
}

static int yt921x_proc_reply_loop_detect(struct yt921x_priv *priv)
{
	u32 ctrl;
	u32 act_ctrl;
	u32 flag;
	u32 timer;
	int res;

	res = yt921x_reg_read(priv, YT921X_LOOP_DETECT_TOP_CTRL, &ctrl);
	if (res)
		return res;
	res = yt921x_reg_read(priv, YT921X_LOOP_DETECT_ACT_CTRL, &act_ctrl);
	if (res)
		return res;
	res = yt921x_reg_read(priv, YT921X_LOOP_DETECT_FLAG, &flag);
	if (res)
		return res;
	res = yt921x_reg_read(priv, YT921X_LOOP_DETECT_TIMER, &timer);
	if (res)
		return res;

	yt921x_proc_reply_append(
		priv,
		"loop_detect reg=0x%06x val=0x%08x en=%u tpid=0x%04x gen_way=%u f0=%u f1=%u unit={%u,%u,%u} f7=%u\n",
		YT921X_LOOP_DETECT_TOP_CTRL, ctrl,
		!!(ctrl & YT921X_LOOP_DETECT_EN),
		(u16)FIELD_GET(YT921X_LOOP_DETECT_TPID_M, ctrl),
		!!(ctrl & YT921X_LOOP_DETECT_GEN_WAY),
		(u32)FIELD_GET(YT921X_LOOP_DETECT_F0_M, ctrl),
		!!(ctrl & YT921X_LOOP_DETECT_F1),
		(u32)FIELD_GET(YT921X_LOOP_DETECT_UNIT_ID0_M, ctrl),
		(u32)FIELD_GET(YT921X_LOOP_DETECT_UNIT_ID1_M, ctrl),
		(u32)FIELD_GET(YT921X_LOOP_DETECT_UNIT_ID2_M, ctrl),
		!!(ctrl & YT921X_LOOP_DETECT_F7));
	yt921x_proc_reply_append(
		priv,
		"loop_detect aux act_ctrl[0x%06x]=0x%08x flag[0x%06x]=0x%08x timer[0x%06x]=0x%08x\n",
		YT921X_LOOP_DETECT_ACT_CTRL, act_ctrl,
		YT921X_LOOP_DETECT_FLAG, flag,
		YT921X_LOOP_DETECT_TIMER, timer);

	return 0;
}

static int yt921x_proc_reply_wol(struct yt921x_priv *priv)
{
	u32 ctrl;
	int res;

	res = yt921x_reg_read(priv, YT921X_WOL_CTRL, &ctrl);
	if (res)
		return res;

	yt921x_proc_reply_append(priv,
				 "wol reg=0x%06x val=0x%08x en=%u ethertype=0x%04x\n",
				 YT921X_WOL_CTRL, ctrl,
				 !!(ctrl & YT921X_WOL_CTRL_EN),
				 (u16)FIELD_GET(YT921X_WOL_CTRL_ETHERTYPE_M, ctrl));

	return 0;
}

void yt921x_storm_guard_workfn(struct work_struct *work)
{
	struct yt921x_priv *priv = container_of(to_delayed_work(work),
						struct yt921x_priv,
						storm_guard_work);
	u16 storm_mask = 0;
	unsigned long targets_mask;
	unsigned long now = jiffies;
	u32 interval_ms;
	u32 pps_limit;
	u32 hold_ms;
	bool enabled;
	int port;

	mutex_lock(&priv->reg_lock);
	enabled = priv->storm_guard_enabled;
	interval_ms = max_t(u32, priv->storm_guard_interval_ms, 100);
	pps_limit = max_t(u32, priv->storm_guard_pps, 1);
	hold_ms = max_t(u32, priv->storm_guard_hold_ms, 100);

	targets_mask = yt921x_non_cpu_port_mask(priv);
	for_each_set_bit(port, &targets_mask, YT921X_PORT_NUM) {
		struct yt921x_port *pp = &priv->ports[port];
		u64 rx_broadcast;
		u64 rx_multicast;
		u64 delta;
		u64 pps;

		if (yt921x_read_mib(priv, port))
			continue;

		rx_broadcast = pp->mib.rx_broadcast;
		rx_multicast = pp->mib.rx_multicast;

		if (!priv->storm_guard_primed) {
			priv->storm_guard_last_rx_broadcast[port] = rx_broadcast;
			priv->storm_guard_last_rx_multicast[port] = rx_multicast;
			continue;
		}

		delta = (rx_broadcast - priv->storm_guard_last_rx_broadcast[port]) +
			(rx_multicast - priv->storm_guard_last_rx_multicast[port]);
		priv->storm_guard_last_rx_broadcast[port] = rx_broadcast;
		priv->storm_guard_last_rx_multicast[port] = rx_multicast;

		if (!enabled)
			continue;

		pps = div_u64(delta * 1000ULL, interval_ms);
		if (pps >= pps_limit)
			priv->storm_guard_block_until[port] =
				now + msecs_to_jiffies(hold_ms);

		if (priv->storm_guard_block_until[port] &&
		    time_before(now, priv->storm_guard_block_until[port]))
			storm_mask |= BIT(port);
	}

	priv->storm_guard_primed = true;
	priv->flood_storm_mask = enabled ? (storm_mask & YT921X_FILTER_PORTS_M) :
					     0;
	yt921x_apply_flood_filters_locked(priv);
	mutex_unlock(&priv->reg_lock);

	if (enabled)
		schedule_delayed_work(&priv->storm_guard_work,
				      msecs_to_jiffies(interval_ms));
}

struct yt921x_proc_tbl_field_desc {
	u8 width;
	u8 word;
	u8 shift;
	const char *name;
};

struct yt921x_proc_tbl_desc {
	u8 id;
	const char *name;
	u32 base;
	u8 entry_words;
	u8 rw_words;
	u32 entries;
	const struct yt921x_proc_tbl_field_desc *fields;
	size_t nfields;
};

/* Stock storm/ingress-meter tables (reverse-mapped from yt_switch.ko):
 *  - tbl 0xc6: storm_rate_iom_field
 *  - tbl 0xc7: meter_timeslotm_field
 *  - tbl 0xc8: port_meter_ctrlnm_field
 *  - tbl 0xcc: storm_ctrlm_field
 *  - tbl 0xce: meter_config_tblm_field
 */
static const struct yt921x_proc_tbl_field_desc yt921x_tbl_fields_c6[] = {
	YT921X_PROC_FIELD(12, 0, 0, "timeslot"),
};

static const struct yt921x_proc_tbl_field_desc yt921x_tbl_fields_c7[] = {
	YT921X_PROC_FIELD(12, 0, 0, "meter_timeslot"),
};

static const struct yt921x_proc_tbl_field_desc yt921x_tbl_fields_c8[] = {
	YT921X_PROC_FIELD(1, 0, 4, "enable"),
	YT921X_PROC_FIELD(4, 0, 0, "meter_id"),
};

static const struct yt921x_proc_tbl_field_desc yt921x_tbl_fields_cc[] = {
	YT921X_PROC_FIELD(19, 0, 13, "rate_f0"),
	YT921X_PROC_FIELD(10, 0, 3, "rate_f1"),
	YT921X_PROC_FIELD(1, 0, 2, "include_gap"),
	YT921X_PROC_FIELD(1, 0, 1, "mode"),
	YT921X_PROC_FIELD(1, 0, 0, "enable"),
};

static const struct yt921x_proc_tbl_field_desc yt921x_tbl_fields_ce[] = {
	YT921X_PROC_FIELD(1, 2, 14, "meter_mode"),
	YT921X_PROC_FIELD(1, 2, 13, "color_mode"),
	YT921X_PROC_FIELD(2, 2, 11, "exceed_chg_pcp_cmd"),
	YT921X_PROC_FIELD(1, 2, 10, "exceed_pri"),
	YT921X_PROC_FIELD(3, 2, 7, "exceed_chg_pri_cmd"),
	YT921X_PROC_FIELD(1, 2, 6, "exceed_dp"),
	YT921X_PROC_FIELD(1, 2, 5, "violate_dp"),
	YT921X_PROC_FIELD(1, 2, 4, "exceed_dei"),
	YT921X_PROC_FIELD(4, 2, 0, "violate_pri"),
	YT921X_PROC_FIELD(12, 1, 20, "cbs"),
	YT921X_PROC_FIELD(18, 1, 2, "cir"),
	YT921X_PROC_FIELD(2, 1, 0, "token_unit"),
	YT921X_PROC_FIELD(14, 0, 18, "ebs"),
	YT921X_PROC_FIELD(18, 0, 0, "eir"),
};

static const struct yt921x_proc_tbl_field_desc yt921x_tbl_fields_d3[] = {
	YT921X_PROC_FIELD(3, 0, 28, "prio0_qid"),
	YT921X_PROC_FIELD(3, 0, 24, "prio1_qid"),
	YT921X_PROC_FIELD(3, 0, 20, "prio2_qid"),
	YT921X_PROC_FIELD(3, 0, 16, "prio3_qid"),
	YT921X_PROC_FIELD(3, 0, 12, "prio4_qid"),
	YT921X_PROC_FIELD(3, 0, 8, "prio5_qid"),
	YT921X_PROC_FIELD(3, 0, 4, "prio6_qid"),
	YT921X_PROC_FIELD(3, 0, 0, "prio7_qid"),
};

static const struct yt921x_proc_tbl_field_desc yt921x_tbl_fields_d4[] = {
	YT921X_PROC_FIELD(2, 0, 14, "prio0_qid"),
	YT921X_PROC_FIELD(2, 0, 12, "prio1_qid"),
	YT921X_PROC_FIELD(2, 0, 10, "prio2_qid"),
	YT921X_PROC_FIELD(2, 0, 8, "prio3_qid"),
	YT921X_PROC_FIELD(2, 0, 6, "prio4_qid"),
	YT921X_PROC_FIELD(2, 0, 4, "prio5_qid"),
	YT921X_PROC_FIELD(2, 0, 2, "prio6_qid"),
	YT921X_PROC_FIELD(2, 0, 0, "prio7_qid"),
};

static const struct yt921x_proc_tbl_field_desc yt921x_tbl_fields_d7[] = {
	YT921X_PROC_FIELD(8, 0, 0, "sp_mask"),
};

static const struct yt921x_proc_tbl_field_desc yt921x_tbl_fields_e4[] = {
	YT921X_PROC_FIELD(5, 0, 0, "slot_time"),
};

static const struct yt921x_proc_tbl_field_desc yt921x_tbl_fields_0e[] = {
	YT921X_PROC_FIELD(32, 0, 0, "raw"),
};

static const struct yt921x_proc_tbl_field_desc yt921x_tbl_fields_0f[] = {
	YT921X_PROC_FIELD(32, 0, 0, "raw"),
};

static const struct yt921x_proc_tbl_field_desc yt921x_tbl_fields_10[] = {
	YT921X_PROC_FIELD(32, 0, 0, "raw"),
};

static const struct yt921x_proc_tbl_field_desc yt921x_tbl_fields_2a[] = {
	YT921X_PROC_FIELD(16, 0, 0, "ethertype"),
	YT921X_PROC_FIELD(1, 0, 16, "enable"),
};

static const struct yt921x_proc_tbl_field_desc yt921x_tbl_fields_2e[] = {
	YT921X_PROC_FIELD(1, 0, 8, "vlan_range_en"),
	YT921X_PROC_FIELD(4, 0, 4, "range_profile_id"),
	YT921X_PROC_FIELD(1, 0, 3, "svlan_drop_tagged"),
	YT921X_PROC_FIELD(1, 0, 2, "svlan_drop_untagged"),
	YT921X_PROC_FIELD(1, 0, 1, "cvlan_drop_tagged"),
	YT921X_PROC_FIELD(1, 0, 0, "cvlan_drop_untagged"),
};

static const struct yt921x_proc_tbl_field_desc yt921x_tbl_fields_e5[] = {
	YT921X_PROC_FIELD(5, 0, 0, "slot_time"),
};

static const struct yt921x_proc_tbl_field_desc yt921x_tbl_fields_de[] = {
	YT921X_PROC_FIELD(1, 0, 28, "f0"),
	YT921X_PROC_FIELD(1, 0, 27, "f1"),
	YT921X_PROC_FIELD(12, 0, 15, "f2"),
	YT921X_PROC_FIELD(11, 0, 4, "f3"),
	YT921X_PROC_FIELD(1, 0, 3, "f4"),
	YT921X_PROC_FIELD(1, 0, 2, "f5"),
	YT921X_PROC_FIELD(1, 0, 1, "f6"),
	YT921X_PROC_FIELD(1, 0, 0, "f7"),
};

static const struct yt921x_proc_tbl_field_desc yt921x_tbl_fields_df[] = {
	YT921X_PROC_FIELD(12, 0, 13, "f0"),
	YT921X_PROC_FIELD(12, 0, 1, "f1"),
	YT921X_PROC_FIELD(1, 0, 0, "f2"),
};

static const struct yt921x_proc_tbl_field_desc yt921x_tbl_fields_e0[] = {
	YT921X_PROC_FIELD(12, 0, 14, "f0"),
	YT921X_PROC_FIELD(12, 0, 2, "f1"),
	YT921X_PROC_FIELD(1, 0, 1, "f2"),
	YT921X_PROC_FIELD(1, 0, 0, "f3"),
};

static const struct yt921x_proc_tbl_field_desc yt921x_tbl_fields_e1[] = {
	YT921X_PROC_FIELD(11, 0, 13, "f0"),
	YT921X_PROC_FIELD(11, 0, 2, "f1"),
	YT921X_PROC_FIELD(1, 0, 1, "f2"),
	YT921X_PROC_FIELD(1, 0, 0, "f3"),
};

static const struct yt921x_proc_tbl_field_desc yt921x_tbl_fields_e9[] = {
	YT921X_PROC_FIELD(1, 2, 6, "en"),
	YT921X_PROC_FIELD(1, 2, 5, "dual_rate"),
	YT921X_PROC_FIELD(1, 2, 4, "couple"),
	YT921X_PROC_FIELD(1, 2, 3, "mode"),
	YT921X_PROC_FIELD(3, 2, 0, "meter_id"),
	YT921X_PROC_FIELD(14, 1, 18, "ebs"),
	YT921X_PROC_FIELD(18, 1, 0, "eir"),
	YT921X_PROC_FIELD(14, 0, 18, "cbs"),
	YT921X_PROC_FIELD(18, 0, 0, "cir"),
};

static const struct yt921x_proc_tbl_field_desc yt921x_tbl_fields_ea[] = {
	YT921X_PROC_FIELD(1, 0, 0, "slot_time_inv"),
};

static const struct yt921x_proc_tbl_field_desc yt921x_tbl_fields_eb[] = {
	YT921X_PROC_FIELD(1, 1, 4, "en"),
	YT921X_PROC_FIELD(1, 1, 3, "dual_rate"),
	YT921X_PROC_FIELD(3, 1, 0, "meter_id"),
	YT921X_PROC_FIELD(14, 0, 18, "ebs"),
	YT921X_PROC_FIELD(18, 0, 0, "eir"),
};

static const struct yt921x_proc_tbl_field_desc yt921x_tbl_fields_ec[] = {
	YT921X_PROC_FIELD(2, 0, 0, "token_unit_inv"),
};

static const struct yt921x_proc_tbl_field_desc yt921x_tbl_fields_e6[] = {
	YT921X_PROC_FIELD(10, 0, 18, "flow_f0"),
	YT921X_PROC_FIELD(10, 0, 8, "flow_f1"),
	YT921X_PROC_FIELD(4, 0, 4, "flow_f2"),
	YT921X_PROC_FIELD(4, 0, 0, "flow_f3"),
};

static const struct yt921x_proc_tbl_field_desc yt921x_tbl_fields_e7[] = {
	YT921X_PROC_FIELD(1, 0, 0, "dwrr_en"),
};

static const struct yt921x_proc_tbl_field_desc yt921x_tbl_fields_e8[] = {
	YT921X_PROC_FIELD(1, 0, 0, "dwrr_en"),
};

static const struct yt921x_proc_tbl_desc yt921x_proc_tbl_descs[] = {
	{
		.id = 0x0e, .name = "loop-detect-act-ctrl",
		.base = YT921X_LOOP_DETECT_ACT_CTRL,
		.entry_words = 1, .rw_words = 1, .entries = 1,
		.fields = yt921x_tbl_fields_0e,
		.nfields = ARRAY_SIZE(yt921x_tbl_fields_0e),
	},
	{
		.id = 0x0f, .name = "loop-detect-flag",
		.base = YT921X_LOOP_DETECT_FLAG,
		.entry_words = 1, .rw_words = 1, .entries = 1,
		.fields = yt921x_tbl_fields_0f,
		.nfields = ARRAY_SIZE(yt921x_tbl_fields_0f),
	},
	{
		.id = 0x10, .name = "loop-detect-timer",
		.base = YT921X_LOOP_DETECT_TIMER,
		.entry_words = 1, .rw_words = 1, .entries = 1,
		.fields = yt921x_tbl_fields_10,
		.nfields = ARRAY_SIZE(yt921x_tbl_fields_10),
	},
	{
		.id = 0x20, .name = "vlan-igr-trans-ctrl",
		.base = YT921X_VLAN_IGR_TRANS_CTRL,
		.entry_words = 1, .rw_words = 1, .entries = 4,
	},
	{
		.id = 0x2a, .name = "wol-ctrl", .base = YT921X_WOL_CTRL,
		.entry_words = 1, .rw_words = 1, .entries = 1,
		.fields = yt921x_tbl_fields_2a,
		.nfields = ARRAY_SIZE(yt921x_tbl_fields_2a),
	},
	{
		.id = 0x2e, .name = "vlan-trans-port-ctrl",
		.base = YT921X_PORTn_VLAN_CTRL1(0),
		.entry_words = 1, .rw_words = 1, .entries = YT921X_PORT_NUM,
		.fields = yt921x_tbl_fields_2e,
		.nfields = ARRAY_SIZE(yt921x_tbl_fields_2e),
	},
	{
		.id = 0x2f, .name = "vlan-trans-untag-pvid-ignore",
		.base = YT921X_VLAN_TRANS_UNTAG_PVID_IGNORE,
		.entry_words = 1, .rw_words = 1, .entries = 1,
	},
	{
		.id = 0x30, .name = "vlan-trans-range-profile",
		.base = YT921X_VLAN_TRANS_RANGE_PROFILE,
		.entry_words = 4, .rw_words = 3, .entries = 10,
	},
	{
		.id = 0x32, .name = "vlan-igr-trans-table0",
		.base = YT921X_VLAN_IGR_TRANS_TABLE0,
		.entry_words = 2, .rw_words = 2, .entries = 64,
	},
	{
		.id = 0x33, .name = "vlan-igr-trans-table1",
		.base = YT921X_VLAN_IGR_TRANS_TABLE1,
		.entry_words = 2, .rw_words = 2, .entries = 64,
	},
	{
		.id = 0x34, .name = "acl-rule-ctrl", .base = YT921X_ACL_RULE_CTRL,
		.entry_words = 1, .rw_words = 1, .entries = 1,
	},
	{
		.id = 0x35, .name = "acl-port-ctrl", .base = YT921X_ACL_PORT_CTRL,
		.entry_words = 1, .rw_words = 1, .entries = 11,
	},
	{
		.id = 0x36, .name = "acl-block-ctrl", .base = YT921X_ACL_BLOCK_CTRL,
		.entry_words = 1, .rw_words = 1, .entries = 1,
	},
	{
		.id = 0x37, .name = "acl-rule-data", .base = YT921X_ACL_RULE_DATA,
		.entry_words = 1, .rw_words = 1, .entries = 1,
	},
	{
		.id = 0x74, .name = "ctrlpkt-arp-act", .base = YT921X_CTRLPKT_ARP_ACT,
		.entry_words = 1, .rw_words = 1, .entries = 1,
	},
	{
		.id = 0x75, .name = "ctrlpkt-nd-act", .base = YT921X_CTRLPKT_ND_ACT,
		.entry_words = 1, .rw_words = 1, .entries = 1,
	},
	{
		.id = 0x76, .name = "ctrlpkt-lldp-eee-act", .base = YT921X_CTRLPKT_LLDP_EEE_ACT,
		.entry_words = 1, .rw_words = 1, .entries = 1,
	},
	{
		.id = 0x77, .name = "ctrlpkt-lldp-act", .base = YT921X_CTRLPKT_LLDP_ACT,
		.entry_words = 1, .rw_words = 1, .entries = 1,
	},
	{
		.id = 0x9e, .name = "dot1x-port-based", .base = YT921X_DOT1X_PORT_BASED,
		.entry_words = 1, .rw_words = 1, .entries = 11,
	},
	{
		.id = 0x9f, .name = "dot1x-bypass-ctrl", .base = YT921X_DOT1X_BYPASS_CTRL,
		.entry_words = 1, .rw_words = 1, .entries = 11,
	},
	{
		.id = 0xa4, .name = "tbl-a4", .base = 0x180690,
		.entry_words = 1, .rw_words = 1, .entries = 1,
	},
	{
		.id = 0xa5, .name = "acl-unmatch-permit", .base = YT921X_ACL_UNMATCH_PERMIT,
		.entry_words = 1, .rw_words = 1, .entries = 1,
	},
	{
		.id = 0xa6, .name = "tbl-a6", .base = 0x1806ac,
		.entry_words = 1, .rw_words = 1, .entries = 1,
	},
	{
		.id = 0xa7, .name = "tbl-a7", .base = 0x1806b0,
		.entry_words = 1, .rw_words = 1, .entries = 1,
	},
	{
		.id = 0xb0, .name = "tbl-b0", .base = 0x180810,
		.entry_words = 1, .rw_words = 1, .entries = 1,
	},
	{
		.id = 0xb1, .name = "tbl-b1", .base = 0x180814,
		.entry_words = 1, .rw_words = 1, .entries = 1,
	},
	{
		.id = 0xb2, .name = "tbl-b2", .base = 0x180818,
		.entry_words = 1, .rw_words = 1, .entries = 1,
	},
	{
		.id = 0xb3, .name = "tbl-b3", .base = 0x180940,
		.entry_words = 1, .rw_words = 1, .entries = 1,
	},
	{
		.id = 0xb4, .name = "tbl-b4", .base = 0x180944,
		.entry_words = 1, .rw_words = 1, .entries = 1,
	},
	{
		.id = 0xb5, .name = "tbl-b5", .base = 0x180948,
		.entry_words = 1, .rw_words = 1, .entries = 1,
	},
	{
		.id = 0xb6, .name = "tbl-b6", .base = 0x18094c,
		.entry_words = 1, .rw_words = 1, .entries = 1,
	},
	{
		.id = 0xb7, .name = "tbl-b7", .base = 0x180950,
		.entry_words = 1, .rw_words = 1, .entries = 1,
	},
	{
		.id = 0xb8, .name = "tbl-b8", .base = 0x180954,
		.entry_words = 1, .rw_words = 1, .entries = 1,
	},
	{
		.id = 0xb9, .name = "tbl-b9", .base = 0x180958,
		.entry_words = 1, .rw_words = 1, .entries = 1,
	},
	{
		.id = 0xba, .name = "tbl-ba", .base = 0x18095c,
		.entry_words = 1, .rw_words = 1, .entries = 1,
	},
	{
		.id = 0xbb, .name = "tbl-bb", .base = 0x188000,
		.entry_words = 2, .rw_words = 2, .entries = 0x1000,
	},
	{
		.id = 0xbc, .name = "tbl-bc", .base = 0x198000,
		.entry_words = 4, .rw_words = 3, .entries = 0x200,
	},
	{
		.id = 0xbd, .name = "tbl-bd", .base = 0x19a000,
		.entry_words = 4, .rw_words = 3, .entries = 0x200,
	},
	{
		.id = 0xbe, .name = "tbl-be", .base = 0x19c000,
		.entry_words = 4, .rw_words = 3, .entries = 0x200,
	},
	{
		.id = 0xbf, .name = "tbl-bf", .base = 0x19e000,
		.entry_words = 4, .rw_words = 3, .entries = 0x200,
	},
	{
		.id = 0xc0, .name = "tbl-c0", .base = 0x1a0000,
		.entry_words = 4, .rw_words = 3, .entries = 0x200,
	},
	{
		.id = 0xc1, .name = "tbl-c1", .base = 0x1a2000,
		.entry_words = 4, .rw_words = 3, .entries = 0x200,
	},
	{
		.id = 0xc2, .name = "tbl-c2", .base = 0x1a4000,
		.entry_words = 4, .rw_words = 3, .entries = 0x200,
	},
	{
		.id = 0xc3, .name = "tbl-c3", .base = 0x1a6000,
		.entry_words = 4, .rw_words = 3, .entries = 0x200,
	},
	{
		.id = 0xc4, .name = "tbl-c4", .base = 0x1c0000,
		.entry_words = 4, .rw_words = 3, .entries = 0x180,
	},
	{
		.id = 0xc5, .name = "tbl-c5", .base = 0x220000,
		.entry_words = 1, .rw_words = 1, .entries = 11,
	},
	{
		.id = 0xc6, .name = "storm-rate-io", .base = YT921X_STORM_RATE_IO,
		.entry_words = 1, .rw_words = 1, .entries = 1,
		.fields = yt921x_tbl_fields_c6,
		.nfields = ARRAY_SIZE(yt921x_tbl_fields_c6),
	},
	{
		.id = 0xc7, .name = "meter-timeslot", .base = 0x220104,
		.entry_words = 1, .rw_words = 1, .entries = 1,
		.fields = yt921x_tbl_fields_c7,
		.nfields = ARRAY_SIZE(yt921x_tbl_fields_c7),
	},
	{
		.id = 0xc8, .name = "port-meter-ctrl", .base = 0x220108,
		.entry_words = 1, .rw_words = 1, .entries = 11,
		.fields = yt921x_tbl_fields_c8,
		.nfields = ARRAY_SIZE(yt921x_tbl_fields_c8),
	},
	{
		.id = 0xcc, .name = "storm-config", .base = YT921X_STORM_CONFIG,
		.entry_words = 1, .rw_words = 1, .entries = 1,
		.fields = yt921x_tbl_fields_cc,
		.nfields = ARRAY_SIZE(yt921x_tbl_fields_cc),
	},
	{
		.id = 0xce, .name = "meter-config", .base = 0x220800,
		.entry_words = 4, .rw_words = 3, .entries = 14,
		.fields = yt921x_tbl_fields_ce,
		.nfields = ARRAY_SIZE(yt921x_tbl_fields_ce),
	},
	{
		.id = 0xd3, .name = "qos-queue-map-ucast", .base = YT921X_QOS_QUEUE_MAP_UCAST,
		.entry_words = 1, .rw_words = 1, .entries = YT921X_PORT_NUM,
		.fields = yt921x_tbl_fields_d3,
		.nfields = ARRAY_SIZE(yt921x_tbl_fields_d3),
	},
	{
		.id = 0xd4, .name = "qos-queue-map-mcast", .base = YT921X_QOS_QUEUE_MAP_MCAST,
		.entry_words = 1, .rw_words = 1, .entries = YT921X_PORT_NUM,
		.fields = yt921x_tbl_fields_d4,
		.nfields = ARRAY_SIZE(yt921x_tbl_fields_d4),
	},
	{
		.id = 0xd7, .name = "qos-sched-sp", .base = YT921X_QOS_SCHED_SP,
		.entry_words = 1, .rw_words = 1, .entries = YT921X_PORT_NUM,
		.fields = yt921x_tbl_fields_d7,
		.nfields = ARRAY_SIZE(yt921x_tbl_fields_d7),
	},
	{
		.id = 0xde, .name = "vlan-egr-trans-table0",
		.base = YT921X_VLAN_EGR_TRANS_TABLE0,
		.entry_words = 1, .rw_words = 1, .entries = 32,
		.fields = yt921x_tbl_fields_de,
		.nfields = ARRAY_SIZE(yt921x_tbl_fields_de),
	},
	{
		.id = 0xdf, .name = "vlan-egr-trans-table1",
		.base = YT921X_VLAN_EGR_TRANS_TABLE1,
		.entry_words = 1, .rw_words = 1, .entries = 32,
		.fields = yt921x_tbl_fields_df,
		.nfields = ARRAY_SIZE(yt921x_tbl_fields_df),
	},
	{
		.id = 0xe0, .name = "vlan-egr-trans-table2",
		.base = YT921X_VLAN_EGR_TRANS_TABLE2,
		.entry_words = 1, .rw_words = 1, .entries = 32,
		.fields = yt921x_tbl_fields_e0,
		.nfields = ARRAY_SIZE(yt921x_tbl_fields_e0),
	},
	{
		.id = 0xe1, .name = "vlan-egr-transparent",
		.base = YT921X_VLAN_EGR_TRANSPARENT,
		.entry_words = 1, .rw_words = 1, .entries = 11,
		.fields = yt921x_tbl_fields_e1,
		.nfields = ARRAY_SIZE(yt921x_tbl_fields_e1),
	},
	{
		.id = 0xe4, .name = "qsch-slot-time", .base = 0x340008,
		.entry_words = 1, .rw_words = 1, .entries = 1,
		.fields = yt921x_tbl_fields_e4,
		.nfields = ARRAY_SIZE(yt921x_tbl_fields_e4),
	},
	{
		.id = 0xe5, .name = "psch-slot-time", .base = 0x34000c,
		.entry_words = 1, .rw_words = 1, .entries = 1,
		.fields = yt921x_tbl_fields_e5,
		.nfields = ARRAY_SIZE(yt921x_tbl_fields_e5),
	},
	{
		.id = 0xe6, .name = "qsch-flow-map", .base = YT921X_QOS_SCHED_DWRR,
		.entry_words = 1, .rw_words = 1,
		.entries = YT921X_QOS_SCHED_PORTS * YT921X_QOS_SCHED_FLOWS_PER_PORT,
		.fields = yt921x_tbl_fields_e6,
		.nfields = ARRAY_SIZE(yt921x_tbl_fields_e6),
	},
	{
		.id = 0xe7, .name = "qsch-dwrr-mode0", .base = YT921X_QOS_SCHED_DWRR_MODE0,
		.entry_words = 1, .rw_words = 1,
		.entries = YT921X_QOS_SCHED_PORTS * YT921X_QOS_SCHED_FLOWS_PER_PORT,
		.fields = yt921x_tbl_fields_e7,
		.nfields = ARRAY_SIZE(yt921x_tbl_fields_e7),
	},
	{
		.id = 0xe8, .name = "qsch-dwrr-mode1", .base = YT921X_QOS_SCHED_DWRR_MODE1,
		.entry_words = 1, .rw_words = 1,
		.entries = YT921X_QOS_SCHED_PORTS * YT921X_QOS_SCHED_FLOWS_PER_PORT,
		.fields = yt921x_tbl_fields_e8,
		.nfields = ARRAY_SIZE(yt921x_tbl_fields_e8),
	},
	{
		.id = 0xe9, .name = "qsch-shp-cfg", .base = 0x34c000,
		.entry_words = 4, .rw_words = 3, .entries = 9,
		.fields = yt921x_tbl_fields_e9,
		.nfields = ARRAY_SIZE(yt921x_tbl_fields_e9),
	},
	{
		.id = 0xea, .name = "qsch-meter-cfg", .base = 0x34f000,
		.entry_words = 1, .rw_words = 1, .entries = 1,
		.fields = yt921x_tbl_fields_ea,
		.nfields = ARRAY_SIZE(yt921x_tbl_fields_ea),
	},
	{
		.id = 0xeb, .name = "psch-shp-cfg", .base = 0x354000,
		.entry_words = 2, .rw_words = 2, .entries = 5,
		.fields = yt921x_tbl_fields_eb,
		.nfields = ARRAY_SIZE(yt921x_tbl_fields_eb),
	},
	{
		.id = 0xec, .name = "psch-meter-cfg", .base = 0x357000,
		.entry_words = 1, .rw_words = 1, .entries = 1,
		.fields = yt921x_tbl_fields_ec,
		.nfields = ARRAY_SIZE(yt921x_tbl_fields_ec),
	},
};

static const struct yt921x_proc_tbl_desc *
yt921x_proc_tbl_desc_find(u32 id)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(yt921x_proc_tbl_descs); i++) {
		if (yt921x_proc_tbl_descs[i].id == id)
			return &yt921x_proc_tbl_descs[i];
	}

	return NULL;
}

static int
yt921x_proc_tbl_field_lookup(const struct yt921x_proc_tbl_desc *tbl,
			     const char *token, u32 *fieldp)
{
	unsigned int i;
	u32 field;
	int res;

	res = yt921x_proc_parse_u32(token, &field);
	if (!res) {
		if (field >= tbl->nfields)
			return -ERANGE;
		*fieldp = field;
		return 0;
	}

	for (i = 0; i < tbl->nfields; i++) {
		if (!strcmp(token, tbl->fields[i].name)) {
			*fieldp = i;
			return 0;
		}
	}

	return -EINVAL;
}

static int
yt921x_proc_tbl_reg(const struct yt921x_proc_tbl_desc *tbl, u32 index, u32 word,
		    u32 *regp)
{
	u32 entry_stride;

	if (index >= tbl->entries || word >= tbl->rw_words)
		return -ERANGE;

	entry_stride = tbl->entry_words * 4;
	*regp = tbl->base + entry_stride * index + 4 * word;

	return 0;
}

static int
yt921x_proc_tbl_read_entry(struct yt921x_priv *priv,
			   const struct yt921x_proc_tbl_desc *tbl, u32 index,
			   u32 *words)
{
	u32 word;
	int res;

	for (word = 0; word < tbl->rw_words; word++) {
		u32 reg;

		res = yt921x_proc_tbl_reg(tbl, index, word, &reg);
		if (res)
			return res;

		res = yt921x_reg_read(priv, reg, &words[word]);
		if (res)
			return res;
	}

	return 0;
}

static int
yt921x_proc_tbl_write_entry(struct yt921x_priv *priv,
			    const struct yt921x_proc_tbl_desc *tbl, u32 index,
			    const u32 *words)
{
	u32 word;
	int res;

	for (word = 0; word < tbl->rw_words; word++) {
		u32 reg;

		res = yt921x_proc_tbl_reg(tbl, index, word, &reg);
		if (res)
			return res;

		res = yt921x_reg_write(priv, reg, words[word]);
		if (res)
			return res;
	}

	return 0;
}

static u32 yt921x_proc_field_mask(u8 width)
{
	if (width >= 32)
		return U32_MAX;
	if (!width)
		return 0;

	return (1U << width) - 1;
}

static int
yt921x_proc_tbl_field_get(struct yt921x_priv *priv,
			  const struct yt921x_proc_tbl_desc *tbl, u32 index,
			  u32 field, u32 *valp)
{
	u32 words[YT921X_PROC_TBL_MAX_WORDS] = { 0 };
	const struct yt921x_proc_tbl_field_desc *f;
	u32 mask;
	u32 lo_bits;
	int res;

	if (field >= tbl->nfields)
		return -ERANGE;
	if (tbl->rw_words > YT921X_PROC_TBL_MAX_WORDS)
		return -E2BIG;

	res = yt921x_proc_tbl_read_entry(priv, tbl, index, words);
	if (res)
		return res;

	f = &tbl->fields[field];
	if (f->word >= tbl->rw_words)
		return -ERANGE;

	mask = yt921x_proc_field_mask(f->width);
	if (f->shift + f->width <= 32) {
		*valp = (words[f->word] >> f->shift) & mask;
		return 0;
	}

	if (f->word + 1 >= tbl->rw_words)
		return -ERANGE;

	lo_bits = 32 - f->shift;
	*valp = ((words[f->word] >> f->shift) |
		 (words[f->word + 1] << lo_bits)) & mask;
	return 0;
}

static int
yt921x_proc_tbl_field_set(struct yt921x_priv *priv,
			  const struct yt921x_proc_tbl_desc *tbl, u32 index,
			  u32 field, u32 value)
{
	u32 words[YT921X_PROC_TBL_MAX_WORDS] = { 0 };
	const struct yt921x_proc_tbl_field_desc *f;
	u32 mask;
	u32 lo_bits;
	u32 lo_mask;
	u32 hi_mask;
	int res;

	if (field >= tbl->nfields)
		return -ERANGE;
	if (tbl->rw_words > YT921X_PROC_TBL_MAX_WORDS)
		return -E2BIG;

	f = &tbl->fields[field];
	if (f->word >= tbl->rw_words)
		return -ERANGE;

	mask = yt921x_proc_field_mask(f->width);
	if (value & ~mask)
		return -ERANGE;

	res = yt921x_proc_tbl_read_entry(priv, tbl, index, words);
	if (res)
		return res;

	if (f->shift + f->width <= 32) {
		words[f->word] &= ~(mask << f->shift);
		words[f->word] |= value << f->shift;
		return yt921x_proc_tbl_write_entry(priv, tbl, index, words);
	}

	if (f->word + 1 >= tbl->rw_words)
		return -ERANGE;

	lo_bits = 32 - f->shift;
	lo_mask = yt921x_proc_field_mask(lo_bits);
	hi_mask = yt921x_proc_field_mask(f->width - lo_bits);

	words[f->word] &= ~(lo_mask << f->shift);
	words[f->word] |= (value & lo_mask) << f->shift;

	words[f->word + 1] &= ~hi_mask;
	words[f->word + 1] |= (value >> lo_bits) & hi_mask;

	return yt921x_proc_tbl_write_entry(priv, tbl, index, words);
}

static int yt921x_proc_run(struct yt921x_priv *priv, char *cmd)
{
	char *argv[6] = { 0 };
	unsigned int argc = 0;
	char *tok;
	int res = 0;

	cmd = strim(cmd);
	while (argc < ARRAY_SIZE(argv) && (tok = strsep(&cmd, " \t")) != NULL) {
		if (!*tok)
			continue;
		argv[argc++] = tok;
	}

	if (!argc || !strcmp(argv[0], "help")) {
		yt921x_proc_reply_help(priv);
		return 0;
	}

	yt921x_proc_reply_reset(priv);

	if (!strcmp(argv[0], "get_flood_filter") && argc == 1) {
		u32 mcast_mask;
		u32 bcast_mask;

		mutex_lock(&priv->reg_lock);
		res = yt921x_reg_read(priv, YT921X_FILTER_MCAST, &mcast_mask);
		if (!res)
			res = yt921x_reg_read(priv, YT921X_FILTER_BCAST,
					      &bcast_mask);
		if (!res)
			yt921x_proc_reply_append(
				priv,
				"flood mcast=0x%03x bcast=0x%03x base_mcast=0x%03x base_bcast=0x%03x storm=0x%03x\n",
				mcast_mask & YT921X_FILTER_PORTS_M,
				bcast_mask & YT921X_FILTER_PORTS_M,
				priv->flood_mcast_base_mask &
					YT921X_FILTER_PORTS_M,
				priv->flood_bcast_base_mask &
					YT921X_FILTER_PORTS_M,
				priv->flood_storm_mask &
					YT921X_FILTER_PORTS_M);
		mutex_unlock(&priv->reg_lock);
		goto out;
	}

	if (!strcmp(argv[0], "set_flood_filter") && argc >= 3) {
		u32 mask;

		res = yt921x_proc_parse_u32(argv[2], &mask);
		if (res)
			goto out;

		mask &= YT921X_FILTER_PORTS_M;

		mutex_lock(&priv->reg_lock);
		if (!strcmp(argv[1], "mcast")) {
			priv->flood_mcast_base_mask = mask;
		} else if (!strcmp(argv[1], "bcast")) {
			priv->flood_bcast_base_mask = mask;
		} else if (!strcmp(argv[1], "both")) {
			priv->flood_mcast_base_mask = mask;
			priv->flood_bcast_base_mask = mask;
		} else {
			res = -EINVAL;
		}

		if (!res)
			res = yt921x_apply_flood_filters_locked(priv);

		if (!res)
			yt921x_proc_reply_append(
				priv,
				"flood set %s=0x%03x -> mcast=0x%03x bcast=0x%03x (storm=0x%03x)\n",
				argv[1], mask,
				priv->flood_mcast_base_mask &
					YT921X_FILTER_PORTS_M,
				priv->flood_bcast_base_mask &
					YT921X_FILTER_PORTS_M,
				priv->flood_storm_mask &
					YT921X_FILTER_PORTS_M);
		mutex_unlock(&priv->reg_lock);
		goto out;
	}

	if (!strcmp(argv[0], "port_status")) {
		u32 start = 0;
		u32 end = YT921X_PORT_NUM - 1;
		u32 port;

		if (argc >= 2) {
			res = yt921x_proc_parse_u32(argv[1], &port);
			if (res)
				goto out;
			if (port >= YT921X_PORT_NUM) {
				res = -ERANGE;
				goto out;
			}
			start = end = port;
		}

		mutex_lock(&priv->reg_lock);
		for (port = start; port <= end; port++) {
			res = yt921x_proc_reply_port_status(priv, port);
			if (res)
				break;
		}
		mutex_unlock(&priv->reg_lock);
		goto out;
	}

	if (!strcmp(argv[0], "ctrlpkt")) {
		if (argc == 1 || !strcmp(argv[1], "show")) {
			mutex_lock(&priv->reg_lock);
			res = yt921x_proc_reply_ctrlpkt(priv);
			mutex_unlock(&priv->reg_lock);
			goto out;
		}

		if (!strcmp(argv[1], "set") && argc >= 4) {
			const struct yt921x_proc_ctrlpkt_desc *d;
			u32 val;

			d = yt921x_proc_ctrlpkt_find(argv[2]);
			if (!d) {
				res = -EINVAL;
				goto out;
			}

			res = yt921x_proc_parse_u32(argv[3], &val);
			if (res)
				goto out;

			mutex_lock(&priv->reg_lock);
			res = yt921x_reg_write(priv, d->reg, val);
			if (!res)
				res = yt921x_proc_reply_ctrlpkt(priv);
			mutex_unlock(&priv->reg_lock);
			goto out;
		}

		res = -EINVAL;
		goto out;
	}

	if (!strcmp(argv[0], "dot1x")) {
		u32 start = 0;
		u32 end = YT921X_PORT_NUM - 1;

		if (argc == 1 || !strcmp(argv[1], "show")) {
			if (argc >= 3) {
				u32 port;

				res = yt921x_proc_parse_u32(argv[2], &port);
				if (res)
					goto out;
				if (port >= YT921X_PORT_NUM) {
					res = -ERANGE;
					goto out;
				}
				start = end = port;
			}

			mutex_lock(&priv->reg_lock);
			res = yt921x_proc_reply_dot1x(priv, start, end);
			mutex_unlock(&priv->reg_lock);
			goto out;
		}

		if (!strcmp(argv[1], "set") && argc >= 5) {
			u32 port_based_mask;
			u32 bypass_mask;
			u32 port;
			u32 port_based;
			u32 bypass;

			res = yt921x_proc_parse_u32(argv[2], &port);
			if (res)
				goto out;
			if (port >= YT921X_PORT_NUM) {
				res = -ERANGE;
				goto out;
			}
			res = yt921x_proc_parse_u32(argv[3], &port_based);
			if (res)
				goto out;
			res = yt921x_proc_parse_u32(argv[4], &bypass);
			if (res)
				goto out;
			if (port_based > 1 || bypass > 1) {
				res = -ERANGE;
				goto out;
			}

			mutex_lock(&priv->reg_lock);
			res = yt921x_reg_read(priv, YT921X_DOT1X_PORT_BASED,
					      &port_based_mask);
			if (!res) {
				if (port_based)
					port_based_mask |= BIT(port);
				else
					port_based_mask &= ~BIT(port);

				res = yt921x_reg_write(priv,
						       YT921X_DOT1X_PORT_BASED,
						       port_based_mask);
			}
			if (!res)
				res = yt921x_reg_read(priv, YT921X_DOT1X_BYPASS_CTRL,
						      &bypass_mask);
			if (!res) {
				if (bypass)
					bypass_mask |= BIT(port);
				else
					bypass_mask &= ~BIT(port);

				res = yt921x_reg_write(priv,
						       YT921X_DOT1X_BYPASS_CTRL,
						       bypass_mask);
			}
			if (!res)
				res = yt921x_proc_reply_dot1x(priv, port, port);
			mutex_unlock(&priv->reg_lock);
			goto out;
		}

		res = -EINVAL;
		goto out;
	}

	if (!strcmp(argv[0], "loop_detect")) {
		if (argc == 1 || !strcmp(argv[1], "show")) {
			mutex_lock(&priv->reg_lock);
			res = yt921x_proc_reply_loop_detect(priv);
			mutex_unlock(&priv->reg_lock);
			goto out;
		}

		if (!strcmp(argv[1], "set") && argc >= 3) {
			u32 enable;
			u32 tpid = 0;
			u32 gen_way = 0;
			u32 ctrl;

			res = yt921x_proc_parse_u32(argv[2], &enable);
			if (res)
				goto out;
			if (argc >= 4) {
				res = yt921x_proc_parse_u32(argv[3], &tpid);
				if (res)
					goto out;
				if (tpid > 0xffff) {
					res = -ERANGE;
					goto out;
				}
			}
			if (argc >= 5) {
				res = yt921x_proc_parse_u32(argv[4], &gen_way);
				if (res)
					goto out;
				if (gen_way > 1) {
					res = -ERANGE;
					goto out;
				}
			}

			mutex_lock(&priv->reg_lock);
			res = yt921x_reg_read(priv, YT921X_LOOP_DETECT_TOP_CTRL,
					      &ctrl);
			if (!res) {
				ctrl = enable ? (ctrl | YT921X_LOOP_DETECT_EN) :
						(ctrl & ~YT921X_LOOP_DETECT_EN);
				if (argc >= 4) {
					ctrl &= ~YT921X_LOOP_DETECT_TPID_M;
					ctrl |= FIELD_PREP(YT921X_LOOP_DETECT_TPID_M,
							   tpid);
				}
				if (argc >= 5) {
					ctrl = gen_way ?
						(ctrl | YT921X_LOOP_DETECT_GEN_WAY) :
						(ctrl & ~YT921X_LOOP_DETECT_GEN_WAY);
				}
				res = yt921x_reg_write(priv,
						       YT921X_LOOP_DETECT_TOP_CTRL,
						       ctrl);
			}
			if (!res)
				res = yt921x_proc_reply_loop_detect(priv);
			mutex_unlock(&priv->reg_lock);
			goto out;
		}

		res = -EINVAL;
		goto out;
	}

	if (!strcmp(argv[0], "wol")) {
		if (argc == 1 || !strcmp(argv[1], "show")) {
			mutex_lock(&priv->reg_lock);
			res = yt921x_proc_reply_wol(priv);
			mutex_unlock(&priv->reg_lock);
			goto out;
		}

		if (!strcmp(argv[1], "set") && argc >= 3) {
			u32 enable;
			u32 ethertype = 0;
			u32 ctrl;

			res = yt921x_proc_parse_u32(argv[2], &enable);
			if (res)
				goto out;
			if (enable > 1) {
				res = -ERANGE;
				goto out;
			}
			if (argc >= 4) {
				res = yt921x_proc_parse_u32(argv[3], &ethertype);
				if (res)
					goto out;
				if (ethertype > 0xffff) {
					res = -ERANGE;
					goto out;
				}
			}

			mutex_lock(&priv->reg_lock);
			res = yt921x_reg_read(priv, YT921X_WOL_CTRL, &ctrl);
			if (!res) {
				ctrl = enable ? (ctrl | YT921X_WOL_CTRL_EN) :
						(ctrl & ~YT921X_WOL_CTRL_EN);
				if (argc >= 4) {
					ctrl &= ~YT921X_WOL_CTRL_ETHERTYPE_M;
					ctrl |= YT921X_WOL_CTRL_ETHERTYPE(ethertype);
				}
				res = yt921x_reg_write(priv, YT921X_WOL_CTRL, ctrl);
			}
			if (!res)
				res = yt921x_proc_reply_wol(priv);
			mutex_unlock(&priv->reg_lock);
			goto out;
		}

		res = -EINVAL;
		goto out;
	}

	if (!strcmp(argv[0], "unk")) {
		if (argc == 1 || !strcmp(argv[1], "show")) {
			mutex_lock(&priv->reg_lock);
			res = yt921x_proc_reply_unknown_policy(priv);
			mutex_unlock(&priv->reg_lock);
			goto out;
		}

		if (!strcmp(argv[1], "set") && argc >= 3) {
			if (!strcmp(argv[2], "filter") && argc >= 5) {
				u32 mask;

				res = yt921x_proc_parse_u32(argv[4], &mask);
				if (res)
					goto out;
				mask &= YT921X_FILTER_PORTS_M;

				mutex_lock(&priv->reg_lock);
				if (!strcmp(argv[3], "ucast") ||
				    !strcmp(argv[3], "both")) {
					res = yt921x_reg_write(priv,
							       YT921X_FILTER_UNK_UCAST,
							       mask);
					if (res)
						goto out_unlock_unk;
				}
				if (!strcmp(argv[3], "mcast") ||
				    !strcmp(argv[3], "both")) {
					res = yt921x_reg_write(priv,
							       YT921X_FILTER_UNK_MCAST,
							       mask);
					if (res)
						goto out_unlock_unk;
				}
				if (strcmp(argv[3], "ucast") &&
				    strcmp(argv[3], "mcast") &&
				    strcmp(argv[3], "both")) {
					res = -EINVAL;
					goto out_unlock_unk;
				}

				res = yt921x_proc_reply_unknown_policy(priv);
out_unlock_unk:
				mutex_unlock(&priv->reg_lock);
				goto out;
			}

			if (!strcmp(argv[2], "action") && argc >= 6) {
				u32 reg;
				u32 port;
				u32 act;
				u32 val;
				u32 mask;
				u32 shift;

				if (!strcmp(argv[3], "ucast"))
					reg = YT921X_ACT_UNK_UCAST;
				else if (!strcmp(argv[3], "mcast"))
					reg = YT921X_ACT_UNK_MCAST;
				else {
					res = -EINVAL;
					goto out;
				}

				res = yt921x_proc_parse_u32(argv[4], &port);
				if (res)
					goto out;
				if (port >= YT921X_PORT_NUM) {
					res = -ERANGE;
					goto out;
				}

				res = yt921x_proc_parse_unk_action(argv[5], &act);
				if (res)
					goto out;

				mutex_lock(&priv->reg_lock);
				res = yt921x_reg_read(priv, reg, &val);
				if (res)
					goto out_unlock_unk_act;

				shift = 2 * port;
				mask = 0x3u << shift;
				val &= ~mask;
				val |= (act & 0x3) << shift;

				res = yt921x_reg_write(priv, reg, val);
				if (!res)
					res = yt921x_proc_reply_unknown_policy(priv);
out_unlock_unk_act:
				mutex_unlock(&priv->reg_lock);
				goto out;
			}

			if (!strcmp(argv[2], "bypass") && argc >= 5) {
				u32 enable;
				u32 val;
				u32 bit;

				if (!strcmp(argv[3], "igmp"))
					bit = YT921X_ACT_UNK_MCAST_BYPASS_DROP_IGMP;
				else if (!strcmp(argv[3], "rma"))
					bit = YT921X_ACT_UNK_MCAST_BYPASS_DROP_RMA;
				else {
					res = -EINVAL;
					goto out;
				}

				res = yt921x_proc_parse_u32(argv[4], &enable);
				if (res)
					goto out;
				if (enable > 1) {
					res = -ERANGE;
					goto out;
				}

				mutex_lock(&priv->reg_lock);
				res = yt921x_reg_read(priv, YT921X_ACT_UNK_MCAST, &val);
				if (res)
					goto out_unlock_unk_bypass;

				if (enable)
					val |= bit;
				else
					val &= ~bit;

				res = yt921x_reg_write(priv, YT921X_ACT_UNK_MCAST, val);
				if (!res)
					res = yt921x_proc_reply_unknown_policy(priv);
out_unlock_unk_bypass:
				mutex_unlock(&priv->reg_lock);
				goto out;
			}
		}

		res = -EINVAL;
		goto out;
	}

	if (!strcmp(argv[0], "rma")) {
		if (argc == 1 || !strcmp(argv[1], "show")) {
			static const u8 defaults[] = { 0x00, 0x02, 0x03, 0x0e };
			unsigned int i;

			mutex_lock(&priv->reg_lock);
			if (argc >= 3) {
				u32 index;

				res = yt921x_proc_parse_u32(argv[2], &index);
				if (!res)
					res = yt921x_proc_reply_rma_index(priv, index);
				mutex_unlock(&priv->reg_lock);
				goto out;
			}

			for (i = 0; i < ARRAY_SIZE(defaults); i++) {
				res = yt921x_proc_reply_rma_index(priv, defaults[i]);
				if (res)
					break;
			}
			if (!res)
				yt921x_proc_reply_append(
					priv,
					"hint: use `rma show <idx>` for any index in [0x00..0x2f]\n");
			mutex_unlock(&priv->reg_lock);
			goto out;
		}

		if (!strcmp(argv[1], "set") && argc >= 4) {
			enum yt921x_rma_action action;
			bool bypass_iso;
			bool bypass_vlan;
			u32 index;
			u32 ctrl;
			u32 val;

			res = yt921x_proc_parse_u32(argv[2], &index);
			if (res)
				goto out;
			if (index >= 0x30) {
				res = -ERANGE;
				goto out;
			}

			res = yt921x_proc_parse_rma_action(argv[3], &action);
			if (res)
				goto out;

			mutex_lock(&priv->reg_lock);
			res = yt921x_reg_read(priv, YT921X_RMA_CTRLn(index), &ctrl);
			if (res)
				goto out_unlock_rma;

			bypass_iso = !!(ctrl & YT921X_RMA_CTRL_F6);
			bypass_vlan = !!(ctrl & YT921X_RMA_CTRL_F5);

			if (argc >= 5) {
				res = yt921x_proc_parse_u32(argv[4], &val);
				if (res)
					goto out_unlock_rma;
				bypass_iso = !!val;
			}
			if (argc >= 6) {
				res = yt921x_proc_parse_u32(argv[5], &val);
				if (res)
					goto out_unlock_rma;
				bypass_vlan = !!val;
			}

			res = yt921x_stock_rma_ctrl_set(priv, index, action,
							bypass_iso, bypass_vlan);
			if (!res)
				res = yt921x_proc_reply_rma_index(priv, index);

out_unlock_rma:
			mutex_unlock(&priv->reg_lock);
			goto out;
		}

		res = -EINVAL;
		goto out;
	}

	if (!strcmp(argv[0], "storm_guard")) {
		if (argc == 1 || !strcmp(argv[1], "show")) {
			mutex_lock(&priv->reg_lock);
			yt921x_proc_reply_append(
				priv,
				"storm_guard en=%u pps=%u hold_ms=%u interval_ms=%u mask=0x%03x\n",
				priv->storm_guard_enabled, priv->storm_guard_pps,
				priv->storm_guard_hold_ms,
				priv->storm_guard_interval_ms,
				priv->flood_storm_mask &
					YT921X_FILTER_PORTS_M);
			mutex_unlock(&priv->reg_lock);
			goto out;
		}

		if (!strcmp(argv[1], "set") && argc >= 6) {
			u32 enable;
			u32 pps;
			u32 hold_ms;
			u32 interval_ms;

			res = yt921x_proc_parse_u32(argv[2], &enable);
			if (res)
				goto out;
			res = yt921x_proc_parse_u32(argv[3], &pps);
			if (res)
				goto out;
			res = yt921x_proc_parse_u32(argv[4], &hold_ms);
			if (res)
				goto out;
			res = yt921x_proc_parse_u32(argv[5], &interval_ms);
			if (res)
				goto out;

			if (!enable)
				cancel_delayed_work_sync(&priv->storm_guard_work);

			mutex_lock(&priv->reg_lock);
			priv->storm_guard_enabled = !!enable;
			priv->storm_guard_pps = max_t(u32, pps, 1);
			priv->storm_guard_hold_ms = max_t(u32, hold_ms, 100);
			priv->storm_guard_interval_ms = max_t(u32, interval_ms,
							      100);
			memset(priv->storm_guard_block_until, 0,
			       sizeof(priv->storm_guard_block_until));
			priv->storm_guard_primed = false;
			priv->flood_storm_mask = 0;
			res = yt921x_apply_flood_filters_locked(priv);
			mutex_unlock(&priv->reg_lock);
			if (res)
				goto out;

			if (enable)
				mod_delayed_work(system_wq,
						 &priv->storm_guard_work, 0);

			yt921x_proc_reply_append(
				priv,
				"storm_guard set en=%u pps=%u hold_ms=%u interval_ms=%u\n",
				!!enable, max_t(u32, pps, 1),
				max_t(u32, hold_ms, 100),
				max_t(u32, interval_ms, 100));
			goto out;
		}

		res = -EINVAL;
		goto out;
	}

	if (!strcmp(argv[0], "reg") && argc >= 3) {
		u32 reg;
		u32 val;

		res = yt921x_proc_parse_u32(argv[2], &reg);
		if (res)
			goto out;

		mutex_lock(&priv->reg_lock);
		if (!strcmp(argv[1], "read")) {
			res = yt921x_reg_read(priv, reg, &val);
			if (!res)
				yt921x_proc_reply_append(priv,
							 "reg 0x%06x = 0x%08x\n",
							 reg, val);
		} else if (!strcmp(argv[1], "write") && argc >= 4) {
			res = yt921x_proc_parse_u32(argv[3], &val);
			if (!res)
				res = yt921x_reg_write(priv, reg, val);
			if (!res)
				yt921x_proc_reply_append(priv,
							 "reg 0x%06x <= 0x%08x\n",
							 reg, val);
		} else {
			res = -EINVAL;
		}
		mutex_unlock(&priv->reg_lock);
		goto out;
	}

	if ((!strcmp(argv[0], "int") || !strcmp(argv[0], "ext")) && argc >= 4) {
		bool ext = !strcmp(argv[0], "ext");
		u32 port;
		u32 reg32;
		u16 reg;
		u16 val;

		res = yt921x_proc_parse_u32(argv[2], &port);
		if (res)
			goto out;
		res = yt921x_proc_parse_u32(argv[3], &reg32);
		if (res || reg32 > 0x1f) {
			res = -EINVAL;
			goto out;
		}
		if ((!ext && port >= YT921X_PORT_NUM) || (ext && port > 0x1f)) {
			res = -EINVAL;
			goto out;
		}
		reg = reg32;

		mutex_lock(&priv->reg_lock);
		if (!strcmp(argv[1], "read")) {
			if (ext)
				res = yt921x_extif_read(priv, port, reg, &val);
			else
				res = yt921x_intif_read(priv, port, reg, &val);
			if (!res)
				yt921x_proc_reply_append(priv,
							 "%s[%u].0x%04x = 0x%04x\n",
							 argv[0], port, reg, val);
		} else if (!strcmp(argv[1], "write") && argc >= 5) {
			res = yt921x_proc_parse_u16(argv[4], &val);
			if (!res) {
				if (ext)
					res = yt921x_extif_write(priv, port, reg,
								 val);
				else
					res = yt921x_intif_write(priv, port, reg,
								 val);
			}
			if (!res)
				yt921x_proc_reply_append(priv,
							 "%s[%u].0x%04x <= 0x%04x\n",
							 argv[0], port, reg, val);
		} else {
			res = -EINVAL;
		}
		mutex_unlock(&priv->reg_lock);
		goto out;
	}

	if (!strcmp(argv[0], "tbl") && argc >= 3) {
		const struct yt921x_proc_tbl_desc *tbl;
		u32 id;

		res = yt921x_proc_parse_u32(argv[2], &id);
		if (res)
			goto out;

		tbl = yt921x_proc_tbl_desc_find(id);
		if (!tbl) {
			res = -ENOENT;
			goto out;
		}

		if (!strcmp(argv[1], "info")) {
			u32 field;

			yt921x_proc_reply_append(priv,
						 "tbl 0x%02x %s base=0x%06x entry_words=%u rw_words=%u entries=%u\n",
						 tbl->id, tbl->name, tbl->base,
						 tbl->entry_words, tbl->rw_words,
						 tbl->entries);
			for (field = 0; field < tbl->nfields; field++) {
				const struct yt921x_proc_tbl_field_desc *f;

				f = &tbl->fields[field];
				yt921x_proc_reply_append(priv,
							 "  field[%u] %-16s width=%u word=%u shift=%u\n",
							 field, f->name, f->width,
							 f->word, f->shift);
			}
			goto out;
		}

		if (argc < 4) {
			res = -EINVAL;
			goto out;
		}

		if (!strcmp(argv[1], "read")) {
			u32 index;
			u32 words[YT921X_PROC_TBL_MAX_WORDS] = { 0 };
			u32 word;

			res = yt921x_proc_parse_u32(argv[3], &index);
			if (res)
				goto out;

			mutex_lock(&priv->reg_lock);
			res = yt921x_proc_tbl_read_entry(priv, tbl, index, words);
			if (!res) {
				yt921x_proc_reply_append(priv,
							 "tbl 0x%02x[%u] %s\n",
							 tbl->id, index, tbl->name);
				for (word = 0; word < tbl->rw_words; word++) {
					u32 reg;

					res = yt921x_proc_tbl_reg(tbl, index, word,
								  &reg);
					if (res)
						break;
					yt921x_proc_reply_append(priv,
								 "  reg 0x%06x w%u = 0x%08x\n",
								 reg, word, words[word]);
				}
			}
			mutex_unlock(&priv->reg_lock);
			goto out;
		}

		if (!strcmp(argv[1], "write") && argc >= 6) {
			u32 index;
			u32 word;
			u32 val;
			u32 words[YT921X_PROC_TBL_MAX_WORDS] = { 0 };
			u32 reg;

			res = yt921x_proc_parse_u32(argv[3], &index);
			if (res)
				goto out;
			res = yt921x_proc_parse_u32(argv[4], &word);
			if (res)
				goto out;
			res = yt921x_proc_parse_u32(argv[5], &val);
			if (res)
				goto out;

			if (word >= tbl->rw_words ||
			    tbl->rw_words > YT921X_PROC_TBL_MAX_WORDS) {
				res = -ERANGE;
				goto out;
			}

			mutex_lock(&priv->reg_lock);
			res = yt921x_proc_tbl_read_entry(priv, tbl, index, words);
			if (!res) {
				words[word] = val;
				res = yt921x_proc_tbl_write_entry(priv, tbl, index,
								  words);
			}
			if (!res) {
				res = yt921x_proc_tbl_reg(tbl, index, word, &reg);
				if (!res)
					yt921x_proc_reply_append(priv,
								 "tbl 0x%02x[%u].w%u reg 0x%06x <= 0x%08x\n",
								 tbl->id, index, word,
								 reg, val);
			}
			mutex_unlock(&priv->reg_lock);
			goto out;
		}

		res = -EINVAL;
		goto out;
	}

	if (!strcmp(argv[0], "field") && argc >= 5) {
		const struct yt921x_proc_tbl_desc *tbl;
		const struct yt921x_proc_tbl_field_desc *f;
		u32 id;
		u32 index;
		u32 field;
		u32 val;

		res = yt921x_proc_parse_u32(argv[2], &id);
		if (res)
			goto out;
		res = yt921x_proc_parse_u32(argv[3], &index);
		if (res)
			goto out;
		tbl = yt921x_proc_tbl_desc_find(id);
		if (!tbl) {
			res = -ENOENT;
			goto out;
		}
		res = yt921x_proc_tbl_field_lookup(tbl, argv[4], &field);
		if (res)
			goto out;
		f = &tbl->fields[field];

		mutex_lock(&priv->reg_lock);
		if (!strcmp(argv[1], "get")) {
			res = yt921x_proc_tbl_field_get(priv, tbl, index, field,
							&val);
			if (!res)
				yt921x_proc_reply_append(priv,
							 "field 0x%02x[%u].%u(%s) = 0x%x (%u)\n",
							 tbl->id, index, field,
							 f->name, val, val);
		} else if (!strcmp(argv[1], "set") && argc >= 6) {
			res = yt921x_proc_parse_u32(argv[5], &val);
			if (!res)
				res = yt921x_proc_tbl_field_set(priv, tbl, index,
								field, val);
			if (!res)
				yt921x_proc_reply_append(priv,
							 "field 0x%02x[%u].%u(%s) <= 0x%x (%u)\n",
							 tbl->id, index, field,
							 f->name, val, val);
		} else {
			res = -EINVAL;
		}
		mutex_unlock(&priv->reg_lock);
		goto out;
	}

	if (!strcmp(argv[0], "mirror") && argc == 1) {
		u32 igr_ports;
		u32 egr_ports;
		u32 dst_port;
		u32 val;

		mutex_lock(&priv->reg_lock);
		res = yt921x_reg_read(priv, YT921X_MIRROR, &val);
		mutex_unlock(&priv->reg_lock);
		if (res)
			goto out;

		igr_ports = FIELD_GET(YT921X_MIRROR_IGR_PORTS_M, val);
		egr_ports = FIELD_GET(YT921X_MIRROR_EGR_PORTS_M, val);
		dst_port = FIELD_GET(YT921X_MIRROR_PORT_M, val);

		yt921x_proc_reply_append(priv,
					 "mirror reg 0x%06x = 0x%08x\n",
					 YT921X_MIRROR, val);
		yt921x_proc_reply_append(priv, "  ingress src: ");
		yt921x_proc_reply_port_mask(priv, igr_ports);
		yt921x_proc_reply_append(priv, "\n");
		yt921x_proc_reply_append(priv, "  egress src: ");
		yt921x_proc_reply_port_mask(priv, egr_ports);
		yt921x_proc_reply_append(priv, "\n");
		yt921x_proc_reply_append(priv, "  dst port: %u\n", dst_port);
		goto out;
	}

	if (!strcmp(argv[0], "tbf")) {
		u32 start = 0;
		u32 end = YT921X_PSCH_SHP_PORTS - 1;
		u32 port;

		if (argc >= 2) {
			res = yt921x_proc_parse_u32(argv[1], &port);
			if (res)
				goto out;
			if (port >= YT921X_PSCH_SHP_PORTS) {
				res = -ERANGE;
				goto out;
			}
			start = end = port;
		}

		mutex_lock(&priv->reg_lock);
		for (port = start; port <= end; port++) {
			res = yt921x_proc_reply_tbf_port(priv, port);
			if (res)
				break;
		}
		mutex_unlock(&priv->reg_lock);
		goto out;
	}

	if (!strcmp(argv[0], "vlan") && argc >= 3 && !strcmp(argv[1], "dump")) {
		u32 vid;

		res = yt921x_proc_parse_u32(argv[2], &vid);
		if (res)
			goto out;

		mutex_lock(&priv->reg_lock);
		res = yt921x_proc_reply_vlan_dump(priv, vid);
		mutex_unlock(&priv->reg_lock);
		goto out;
	}

	if (!strcmp(argv[0], "vlan") && argc >= 3 && !strcmp(argv[1], "mode")) {
		mutex_lock(&priv->reg_lock);
		if (!strcmp(argv[2], "show") && argc == 3) {
			res = yt921x_proc_reply_vlan_mode(priv);
		} else if (!strcmp(argv[2], "set") && argc >= 5) {
			u32 mask;
			u32 enable;

			res = yt921x_proc_parse_vlan_mode(argv[3], &mask);
			if (res)
				goto vlan_mode_out;

			res = yt921x_proc_parse_u32(argv[4], &enable);
			if (res)
				goto vlan_mode_out;
			if (enable > 1) {
				res = -ERANGE;
				goto vlan_mode_out;
			}

			res = yt921x_reg_toggle_bits(priv,
						     YT921X_VLAN_IGR_TRANS_CTRL,
						     mask, !!enable);
			if (res)
				goto vlan_mode_out;

			res = yt921x_proc_reply_vlan_mode(priv);
		} else {
			res = -EINVAL;
		}
vlan_mode_out:
		mutex_unlock(&priv->reg_lock);
		goto out;
	}

	if (!strcmp(argv[0], "vlan") && argc >= 3 && !strcmp(argv[1], "fid_mode")) {
		mutex_lock(&priv->reg_lock);
		if (!strcmp(argv[2], "show") && argc == 3) {
			res = yt921x_proc_reply_vlan_fid_mode(priv);
		} else if (!strcmp(argv[2], "set") && argc >= 4) {
			u32 svl_fid = 1;
			u8 mode;

			res = yt921x_proc_parse_vlan_fid_mode(argv[3], &mode);
			if (res)
				goto vlan_fid_mode_out;

			if (mode == YT921X_VLAN_FID_MODE_SVL && argc >= 5) {
				res = yt921x_proc_parse_u32(argv[4], &svl_fid);
				if (res)
					goto vlan_fid_mode_out;
				if (!svl_fid || svl_fid >= YT921X_VID_UNAWARE) {
					res = -ERANGE;
					goto vlan_fid_mode_out;
				}
			}

			priv->vlan_fid_mode = mode;
			if (mode == YT921X_VLAN_FID_MODE_SVL)
				priv->vlan_svl_fid = svl_fid;

			res = yt921x_proc_apply_vlan_fid_mode(priv);
			if (res)
				goto vlan_fid_mode_out;

			res = yt921x_proc_reply_vlan_fid_mode(priv);
		} else {
			res = -EINVAL;
		}
vlan_fid_mode_out:
		mutex_unlock(&priv->reg_lock);
		goto out;
	}

	if (!strcmp(argv[0], "vlan") && argc >= 4 && !strcmp(argv[1], "fid")) {
		u32 vid;

		res = yt921x_proc_parse_u32(argv[3], &vid);
		if (res)
			goto out;
		if (vid > YT921X_VID_UNAWARE) {
			res = -ERANGE;
			goto out;
		}

		mutex_lock(&priv->reg_lock);
		if (!strcmp(argv[2], "show") && argc == 4) {
			res = yt921x_proc_reply_vlan_dump(priv, vid);
		} else if (!strcmp(argv[2], "set") && argc >= 5) {
			u64 ctrl64;
			u32 fid;

			if (!vid || vid == YT921X_VID_UNAWARE) {
				res = -EINVAL;
				goto vlan_fid_out;
			}

			res = yt921x_proc_parse_u32(argv[4], &fid);
			if (res)
				goto vlan_fid_out;
			if (!fid || fid >= YT921X_VID_UNAWARE) {
				res = -ERANGE;
				goto vlan_fid_out;
			}

			res = yt921x_reg64_read(priv, YT921X_VLANn_CTRL(vid), &ctrl64);
			if (res)
				goto vlan_fid_out;

			ctrl64 &= ~YT921X_VLAN_CTRL_FID_M;
			ctrl64 |= YT921X_VLAN_CTRL_FID(fid);
			res = yt921x_reg64_write(priv, YT921X_VLANn_CTRL(vid), ctrl64);
			if (res)
				goto vlan_fid_out;

			res = yt921x_proc_reply_vlan_dump(priv, vid);
		} else {
			res = -EINVAL;
		}
vlan_fid_out:
		mutex_unlock(&priv->reg_lock);
		goto out;
	}

	if (!strcmp(argv[0], "vlan") && argc >= 3 && !strcmp(argv[1], "1x_bypass")) {
		u64 ctrl64;
		u32 vid;
		u32 enable = 0;

		res = yt921x_proc_parse_u32(argv[2], &vid);
		if (res)
			goto out;
		if (vid > YT921X_VID_UNAWARE) {
			res = -ERANGE;
			goto out;
		}

		if (argc >= 4) {
			res = yt921x_proc_parse_u32(argv[3], &enable);
			if (res)
				goto out;
			if (enable > 1) {
				res = -ERANGE;
				goto out;
			}
		}

		mutex_lock(&priv->reg_lock);
		res = yt921x_reg64_read(priv, YT921X_VLANn_CTRL(vid), &ctrl64);
		if (!res && argc >= 4) {
			if (enable)
				ctrl64 |= YT921X_VLAN_CTRL_BYPASS_1X_AC;
			else
				ctrl64 &= ~YT921X_VLAN_CTRL_BYPASS_1X_AC;

			res = yt921x_reg64_write(priv, YT921X_VLANn_CTRL(vid), ctrl64);
		}
		if (!res)
			res = yt921x_proc_reply_vlan_dump(priv, vid);
		mutex_unlock(&priv->reg_lock);
		goto out;
	}

	if (!strcmp(argv[0], "pvid") && argc >= 2 && !strcmp(argv[1], "dump")) {
		u32 start = 0;
		u32 end = YT921X_PORT_NUM - 1;
		u32 port;

		if (argc >= 3) {
			res = yt921x_proc_parse_u32(argv[2], &port);
			if (res)
				goto out;
			if (port >= YT921X_PORT_NUM) {
				res = -ERANGE;
				goto out;
			}
			start = end = port;
		}

		mutex_lock(&priv->reg_lock);
		for (port = start; port <= end; port++) {
			res = yt921x_proc_reply_pvid_port(priv, port);
			if (res)
				break;
		}
		mutex_unlock(&priv->reg_lock);
		goto out;
	}

	if (!strcmp(argv[0], "stock") && argc >= 3) {
		u16 page;
		u16 phy;
		u16 w0;
		u16 w1;
		u32 reg;

		res = yt921x_proc_parse_u32(argv[2], &reg);
		if (res)
			goto out;

		yt921x_proc_stock_map(reg, &page, &phy, &w0, &w1);

		if (!strcmp(argv[1], "map")) {
			yt921x_proc_reply_append(priv,
						 "stock map reg=0x%06x page=0x%03x phy=0x%02x w0=0x%02x w1=0x%02x\n",
						 reg, page, phy, w0, w1);
		} else {
			res = -EINVAL;
		}
		goto out;
	}

	if (!strcmp(argv[0], "acl_chain")) {
		if (argc == 1 || !strcmp(argv[1], "show")) {
			yt921x_proc_reply_append(
				priv,
				"acl_chain key1_mask=0x%08x (applies to non-final entries in multi-entry ACL rule)\n",
				READ_ONCE(priv->acl_chain_key_mask));
			goto out;
		}

		if (!strcmp(argv[1], "set") && argc >= 3) {
			u32 mask;

			res = yt921x_proc_parse_u32(argv[2], &mask);
			if (res)
				goto out;

			WRITE_ONCE(priv->acl_chain_key_mask, mask);
			yt921x_proc_reply_append(
				priv,
				"acl_chain key1_mask <= 0x%08x (takes effect for newly installed ACL rules)\n",
				mask);
			goto out;
		}

		res = -EINVAL;
		goto out;
	}

	if (!strcmp(argv[0], "dump") && argc >= 3) {
		u32 start;
		u32 end;
		u32 stride = 4;
		u32 val;
		u32 reg;

		res = yt921x_proc_parse_u32(argv[1], &start);
		if (res)
			goto out;
		res = yt921x_proc_parse_u32(argv[2], &end);
		if (res)
			goto out;
		if (start > end) {
			res = -EINVAL;
			goto out;
		}
		if (argc >= 4) {
			res = yt921x_proc_parse_u32(argv[3], &stride);
			if (res || !stride) {
				res = -EINVAL;
				goto out;
			}
		}

		mutex_lock(&priv->reg_lock);
		for (reg = start; reg <= end; reg += stride) {
			res = yt921x_reg_read(priv, reg, &val);
			if (res) {
				yt921x_proc_reply_append(priv,
							 "reg 0x%06x err=%d\n",
							 reg, res);
				break;
			}
			yt921x_proc_reply_append(priv,
						 "reg 0x%06x = 0x%08x\n",
						 reg, val);
			if (priv->proc_reply_len >= sizeof(priv->proc_reply) -
						   32) {
				yt921x_proc_reply_append(priv,
							 "... truncated ...\n");
				break;
			}
			if (reg > U32_MAX - stride)
				break;
		}
		mutex_unlock(&priv->reg_lock);
		goto out;
	}

	res = -EINVAL;
out:
	if (res)
		yt921x_proc_reply_append(priv, "err=%d\n", res);
	return res;
}

static ssize_t yt921x_debugfs_read(struct file *file, char __user *buf,
				   size_t len, loff_t *ppos)
{
	struct yt921x_priv *priv = file->private_data;
	ssize_t res;

	mutex_lock(&priv->proc_lock);
	if (!priv->proc_reply_len)
		yt921x_proc_reply_help(priv);
	res = simple_read_from_buffer(buf, len, ppos, priv->proc_reply,
				      priv->proc_reply_len);
	mutex_unlock(&priv->proc_lock);

	return res;
}

static ssize_t yt921x_debugfs_write(struct file *file, const char __user *buf,
				    size_t len, loff_t *ppos)
{
	struct yt921x_priv *priv = file->private_data;
	char cmd[128];

	if (!len)
		return 0;
	if (len >= sizeof(cmd))
		return -EINVAL;
	if (copy_from_user(cmd, buf, len))
		return -EFAULT;
	cmd[len] = '\0';

	mutex_lock(&priv->proc_lock);
	yt921x_proc_run(priv, cmd);
	mutex_unlock(&priv->proc_lock);

	return len;
}

static const struct file_operations yt921x_debugfs_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = yt921x_debugfs_read,
	.write = yt921x_debugfs_write,
	.llseek = default_llseek,
};

int yt921x_proc_init(struct yt921x_priv *priv)
{
	struct device *dev = yt921x_dev(priv);

	mutex_init(&priv->proc_lock);
	yt921x_proc_reply_help(priv);

	priv->proc_cmd = debugfs_create_file("yt921x_cmd", 0600, NULL, priv,
					     &yt921x_debugfs_fops);
	if (IS_ERR_OR_NULL(priv->proc_cmd)) {
		dev_warn(dev, "failed to create debugfs yt921x_cmd\n");
		priv->proc_cmd = NULL;
	}

	return 0;
}

void yt921x_proc_exit(struct yt921x_priv *priv)
{
	if (priv->proc_cmd)
		debugfs_remove(priv->proc_cmd);
	priv->proc_cmd = NULL;
	mutex_destroy(&priv->proc_lock);
}
#else
int yt921x_proc_init(struct yt921x_priv *priv)
{
	return 0;
}

void yt921x_proc_exit(struct yt921x_priv *priv)
{
}
#endif
