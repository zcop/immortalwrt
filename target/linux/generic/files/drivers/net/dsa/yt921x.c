// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Motorcomm YT921x Switch
 *
 * Should work on YT9213/YT9214/YT9215/YT9218, but only tested on YT9215+SGMII,
 * be sure to do your own checks before porting to another chip.
 *
 * Copyright (c) 2025 David Yang
 */

#include <linux/dcbnl.h>
#include <linux/etherdevice.h>
#include <linux/if_bridge.h>
#include <linux/if_hsr.h>
#include <linux/if_vlan.h>
#include <linux/iopoll.h>
#include <linux/jiffies.h>
#include <linux/mdio.h>
#include <linux/mii.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include <linux/workqueue.h>
#if IS_ENABLED(CONFIG_NET_DSA_YT921X_DEBUG)
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#endif

#include <net/dsa.h>
#include <net/dscp.h>
#include <net/flow_offload.h>
#include <net/ieee8021q.h>
#include <net/pkt_cls.h>
#include <net/pkt_sched.h>

#include "yt921x.h"

struct yt921x_mib_desc {
	unsigned int size;
	unsigned int offset;
	const char *name;
};

/* Must agree with yt921x_mib
 *
 * Unstructured fields (name != NULL) will appear in get_ethtool_stats(),
 * structured go to their *_stats() methods, but we need their sizes and offsets
 * to perform 32bit MIB overflow wraparound.
 */
static const struct yt921x_mib_desc yt921x_mib_descs[] = {
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

struct yt921x_info {
	const char *name;
	u16 major;
	/* Unknown, seems to be plain enumeration */
	u8 mode;
	u8 extmode;
	/* Ports with integral GbE PHYs, not including MCU Port 10 */
	u16 internal_mask;
	/* External ports */
	u16 external_mask;
};

static const struct yt921x_info yt921x_infos[] = {
	{
		"YT9215SC", YT9215_MAJOR, 1, 0,
		YT921X_PORT_MASK_INT0_n(5),
		YT921X_PORT_MASK_EXT0 | YT921X_PORT_MASK_EXT1,
	},
	{
		"YT9215S", YT9215_MAJOR, 2, 0,
		YT921X_PORT_MASK_INT0_n(5),
		YT921X_PORT_MASK_EXT0 | YT921X_PORT_MASK_EXT1,
	},
	{
		"YT9215RB", YT9215_MAJOR, 3, 0,
		YT921X_PORT_MASK_INT0_n(5),
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
		YT921X_PORT_MASK_INT0_n(8),
		0,
	},
	{
		"YT9218MB", YT9218_MAJOR, 1, 0,
		YT921X_PORT_MASK_INT0_n(8),
		YT921X_PORT_MASK_EXT0 | YT921X_PORT_MASK_EXT1,
	},
	{}
};

struct yt921x_reg_mdio {
	struct mii_bus *bus;
	int addr;
	/* SWITCH_ID_1 / SWITCH_ID_0 of the device
	 *
	 * This is a way to multiplex multiple devices on the same MII phyaddr.
	 * One MDIO device node still maps to one core mii_bus address, but DT
	 * can override switchid used for SMI register addressing.
	 */
	unsigned char switchid;
};

/* TODO: SPI/I2C */

static int yt921x_read_mib(struct yt921x_priv *priv, int port);
static u32 yt921x_non_cpu_port_mask(const struct yt921x_priv *priv);
static int yt921x_qos_remark_dscp_set(struct yt921x_priv *priv, u8 prio, u8 dp,
				      u8 dscp);

static bool yt921x_is_external_port(const struct yt921x_priv *priv, int port)
{
	return !!(priv->info->external_mask & BIT(port));
}

static bool yt921x_is_primary_cpu_port(const struct yt921x_priv *priv, int port)
{
	return priv->primary_cpu_port >= 0 && port == priv->primary_cpu_port;
}

static bool yt921x_is_secondary_cpu_port(const struct yt921x_priv *priv, int port)
{
	return priv->secondary_cpu_port >= 0 && port == priv->secondary_cpu_port;
}

static int yt921x_reg_read(struct yt921x_priv *priv, u32 reg, u32 *valp)
{
	WARN_ON(!mutex_is_locked(&priv->reg_lock));

	return priv->reg_ops->read(priv->reg_ctx, reg, valp);
}

static int yt921x_reg_write(struct yt921x_priv *priv, u32 reg, u32 val)
{
	WARN_ON(!mutex_is_locked(&priv->reg_lock));

	return priv->reg_ops->write(priv->reg_ctx, reg, val);
}

static int
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

static int
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

static int yt921x_reg_set_bits(struct yt921x_priv *priv, u32 reg, u32 mask)
{
	return yt921x_reg_update_bits(priv, reg, 0, mask);
}

static int yt921x_reg_clear_bits(struct yt921x_priv *priv, u32 reg, u32 mask)
{
	return yt921x_reg_update_bits(priv, reg, mask, 0);
}

static int
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

static int yt921x_loop_detect_setup_locked(struct yt921x_priv *priv)
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

static int yt921x_rma_setup_locked(struct yt921x_priv *priv)
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

static int yt921x_apply_flood_filters_locked(struct yt921x_priv *priv)
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

static int yt921x_refresh_flood_masks_locked(struct yt921x_priv *priv)
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

static int yt921x_reg64_read(struct yt921x_priv *priv, u32 reg, u64 *valp)
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

static int yt921x_reg64_write(struct yt921x_priv *priv, u32 reg, u64 val)
{
	int res;

	res = yt921x_reg_write(priv, reg, (u32)val);
	if (res)
		return res;
	return yt921x_reg_write(priv, reg + 4, (u32)(val >> 32));
}

static int
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

static int yt921x_reg64_clear_bits(struct yt921x_priv *priv, u32 reg, u64 mask)
{
	return yt921x_reg64_update_bits(priv, reg, mask, 0);
}

static void u32p_replace_bits_unaligned(u32 *lo, u32 *hi, u64 val, u64 mask)
{
	*lo &= ~lower_32_bits(mask);
	*hi &= ~upper_32_bits(mask);
	*lo |= lower_32_bits(val);
	*hi |= upper_32_bits(val);
}

static int yt921x_reg96_write(struct yt921x_priv *priv, u32 reg,
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

static u32 mac_hi4_to_cpu(const unsigned char *addr)
{
	return ((u32)addr[0] << 24) | ((u32)addr[1] << 16) |
	       ((u32)addr[2] << 8) | addr[3];
}

static u16 mac_lo2_to_cpu(const unsigned char *addr)
{
	return ((u16)addr[4] << 8) | addr[5];
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

static const struct yt921x_reg_ops yt921x_reg_ops_mdio = {
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
		dev_info(dev,
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

static int
yt921x_intif_read(struct yt921x_priv *priv, int port, int reg, u16 *valp)
{
	return yt921x_mbus_read(priv, false, port, reg, valp);
}

static int
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

static int
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

static int
yt921x_extif_read(struct yt921x_priv *priv, int port, int reg, u16 *valp)
{
	return yt921x_mbus_read(priv, true, port, reg, valp);
}

static int
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

static int
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
				 "  storm_guard show\n"
				 "  storm_guard set <0|1> <pps> <hold_ms> <interval_ms>\n"
				 "  mirror\n"
				 "  tbf [port]\n"
				 "  vlan dump <vid>\n"
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
	u32 port;
	int res;

	for (port = start; port <= end; port++) {
		u32 port_based;
		u32 bypass;

		res = yt921x_reg_read(priv, YT921X_DOT1X_PORT_BASEDn(port),
				      &port_based);
		if (res)
			return res;

		res = yt921x_reg_read(priv, YT921X_DOT1X_BYPASS_CTRLn(port),
				      &bypass);
		if (res)
			return res;

		yt921x_proc_reply_append(
			priv,
			"dot1x p%u port_based=0x%08x bypass=0x%08x\n",
			port, port_based, bypass);
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

static u32 yt921x_tbf_eir_to_rate_kbps(u32 eir)
{
	if (eir <= YT921X_PSCH_EIR_RATE_OFFSET)
		return 0;

	return DIV_ROUND_CLOSEST_ULL((u64)(eir - YT921X_PSCH_EIR_RATE_OFFSET) *
				     YT921X_PSCH_EIR_RATE_SCALE_DEN,
				     YT921X_PSCH_EIR_RATE_SCALE_NUM);
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
				 "vlan %u reg=0x%06x val=0x%016llx members=0x%03x untag=0x%03x fid=%u stp=%u learn_dis=%u prio_en=%u\n",
				 vid, YT921X_VLANn_CTRL(vid),
				 (unsigned long long)ctrl64, members, untag, fid,
				 stp_id, !!(ctrl64 & YT921X_VLAN_CTRL_LEARN_DIS),
				 !!(ctrl64 & YT921X_VLAN_CTRL_PRIO_EN));

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
	int res;

	res = yt921x_reg_read(priv, YT921X_LOOP_DETECT_TOP_CTRL, &ctrl);
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

	return 0;
}

static void yt921x_storm_guard_workfn(struct work_struct *work)
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

static const struct yt921x_proc_tbl_field_desc yt921x_tbl_fields_e4[] = {
	YT921X_PROC_FIELD(5, 0, 0, "slot_time"),
};

static const struct yt921x_proc_tbl_field_desc yt921x_tbl_fields_e5[] = {
	YT921X_PROC_FIELD(5, 0, 0, "slot_time"),
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

static const struct yt921x_proc_tbl_desc yt921x_proc_tbl_descs[] = {
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

			mutex_lock(&priv->reg_lock);
			res = yt921x_reg_write(priv,
					       YT921X_DOT1X_PORT_BASEDn(port),
					       port_based);
			if (!res)
				res = yt921x_reg_write(priv,
						       YT921X_DOT1X_BYPASS_CTRLn(port),
						       bypass);
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
	len = min(len, sizeof(cmd) - 1);
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

static int yt921x_proc_init(struct yt921x_priv *priv)
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

static void yt921x_proc_exit(struct yt921x_priv *priv)
{
	if (priv->proc_cmd)
		debugfs_remove(priv->proc_cmd);
	priv->proc_cmd = NULL;
	mutex_destroy(&priv->proc_lock);
}
#else
static int yt921x_proc_init(struct yt921x_priv *priv)
{
	return 0;
}

static void yt921x_proc_exit(struct yt921x_priv *priv)
{
}
#endif

#if !IS_ENABLED(CONFIG_NET_DSA_YT921X_DEBUG)
static u32 yt921x_tbf_eir_to_rate_kbps(u32 eir)
{
	if (eir <= YT921X_PSCH_EIR_RATE_OFFSET)
		return 0;

	return DIV_ROUND_CLOSEST_ULL((u64)(eir - YT921X_PSCH_EIR_RATE_OFFSET) *
				     YT921X_PSCH_EIR_RATE_SCALE_DEN,
				     YT921X_PSCH_EIR_RATE_SCALE_NUM);
}
#endif

/* Read and handle overflow of 32bit MIBs. MIB buffer must be zeroed before. */
static int yt921x_read_mib(struct yt921x_priv *priv, int port)
{
	struct yt921x_port *pp = &priv->ports[port];
	struct device *dev = yt921x_dev(priv);
	struct yt921x_mib *mib = &pp->mib;
	int res = 0;

	/* Reading of yt921x_port::mib is not protected by a lock and it's vain
	 * to keep its consistency, since we have to read registers one by one
	 * and there is no way to make a snapshot of MIB stats.
	 *
	 * Writing (by this function only) is and should be protected by
	 * reg_lock.
	 */

	for (size_t i = 0; i < ARRAY_SIZE(yt921x_mib_descs); i++) {
		const struct yt921x_mib_desc *desc = &yt921x_mib_descs[i];
		u32 reg = YT921X_MIBn_DATA0(port) + desc->offset;
		u64 *valp = &((u64 *)mib)[i];
		u32 val0;
		u64 val;

		res = yt921x_reg_read(priv, reg, &val0);
		if (res)
			break;

		if (desc->size <= 1) {
			u64 old_val = *valp;

			val = (old_val & ~(u64)U32_MAX) | val0;
			if (val < old_val)
				val += 1ull << 32;
		} else {
			u32 val1;

			res = yt921x_reg_read(priv, reg + 4, &val1);
			if (res)
				break;
			val = ((u64)val1 << 32) | val0;
		}

		WRITE_ONCE(*valp, val);
	}

	pp->rx_frames = mib->rx_64byte + mib->rx_65_127byte +
			mib->rx_128_255byte + mib->rx_256_511byte +
			mib->rx_512_1023byte + mib->rx_1024_1518byte +
			mib->rx_jumbo;
	pp->tx_frames = mib->tx_64byte + mib->tx_65_127byte +
			mib->tx_128_255byte + mib->tx_256_511byte +
			mib->tx_512_1023byte + mib->tx_1024_1518byte +
			mib->tx_jumbo;

	if (res)
		dev_err(dev, "Failed to %s port %d: %i\n", "read stats for",
			port, res);
	return res;
}

static void yt921x_poll_mib(struct work_struct *work)
{
	struct yt921x_port *pp = container_of_const(work, struct yt921x_port,
						    mib_read.work);
	struct yt921x_priv *priv = pp->priv;
	unsigned long delay = YT921X_STATS_INTERVAL_JIFFIES;
	int port = pp->index;
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_read_mib(priv, port);
	mutex_unlock(&priv->reg_lock);
	if (res)
		delay *= 4;

	if (READ_ONCE(pp->port_up))
		schedule_delayed_work(&pp->mib_read, delay);
}

static const char *const yt921x_qos_stat_names[] = {
	"qos_uc_qid_p0", "qos_uc_qid_p1", "qos_uc_qid_p2", "qos_uc_qid_p3",
	"qos_uc_qid_p4", "qos_uc_qid_p5", "qos_uc_qid_p6", "qos_uc_qid_p7",
	"qos_mc_qid_p0", "qos_mc_qid_p1", "qos_mc_qid_p2", "qos_mc_qid_p3",
	"qos_mc_qid_p4", "qos_mc_qid_p5", "qos_mc_qid_p6", "qos_mc_qid_p7",
	"qos_sp_mask",
	"qos_dwrr_uc_en",
	"qos_dwrr_mc_en",
	"qos_ptbf_en",
	"qos_ptbf_rate_kbps",
	"qos_ptbf_burst_b",
	"qos_qtbf_en_mask",
	"qos_qtbf_rate_q0", "qos_qtbf_rate_q1", "qos_qtbf_rate_q2", "qos_qtbf_rate_q3",
	"qos_qtbf_rate_q4", "qos_qtbf_rate_q5", "qos_qtbf_rate_q6", "qos_qtbf_rate_q7",
	"qos_qtbf_burst_q0", "qos_qtbf_burst_q1", "qos_qtbf_burst_q2", "qos_qtbf_burst_q3",
	"qos_qtbf_burst_q4", "qos_qtbf_burst_q5", "qos_qtbf_burst_q6", "qos_qtbf_burst_q7",
	"qos_policer_en",
	"qos_policer_rate_kbps",
	"qos_policer_burst_b",
};

static void yt921x_qos_telemetry_fill(struct yt921x_priv *priv, int port, u64 *data)
{
	u32 ucast = 0;
	u32 mcast = 0;
	u32 sp = 0;
	u32 port_ctrl = 0;
	u32 port_ebs_eir = 0;
	u32 queue_en_mask = 0;
	size_t j = 0;
	u64 rate_kbps;
	int qid;
	int res;

	res = yt921x_reg_read(priv, YT921X_QOS_QUEUE_MAP_UCASTn(port), &ucast);
	if (res)
		ucast = 0;
	res = yt921x_reg_read(priv, YT921X_QOS_QUEUE_MAP_MCASTn(port), &mcast);
	if (res)
		mcast = 0;

	for (qid = 0; qid < YT921X_PRIO_NUM; qid++)
		data[j++] = (ucast & YT921X_QOS_UCAST_QMAP_PRIO_M(qid)) >>
			    YT921X_QOS_UCAST_QMAP_PRIO_SHIFT(qid);

	for (qid = 0; qid < YT921X_PRIO_NUM; qid++)
		data[j++] = (mcast & YT921X_QOS_MCAST_QMAP_PRIO_M(qid)) >>
			    YT921X_QOS_MCAST_QMAP_PRIO_SHIFT(qid);

	res = yt921x_reg_read(priv, YT921X_QOS_SCHED_SPn(port), &sp);
	if (res)
		sp = 0;
	data[j++] = FIELD_GET(YT921X_QOS_SCHED_SP_MASK, sp);

	data[j] = 0;
	if (port >= 0 && port < YT921X_QOS_SCHED_PORTS) {
		for (qid = 0; qid < YT921X_QOS_SCHED_UCAST_FLOWS; qid++) {
			u32 idx = (u32)port * YT921X_QOS_SCHED_FLOWS_PER_PORT + qid;
			u32 v = 0;

			res = yt921x_reg_read(priv, YT921X_QOS_SCHED_DWRR_MODE0n(idx), &v);
			if (!res && (v & YT921X_QOS_SCHED_DWRR_CFG_EN))
				data[j]++;
		}
	}
	j++;

	data[j] = 0;
	if (port >= 0 && port < YT921X_QOS_SCHED_PORTS) {
		for (qid = 0; qid < YT921X_QOS_SCHED_MCAST_FLOWS; qid++) {
			u32 idx = (u32)port * YT921X_QOS_SCHED_FLOWS_PER_PORT +
				  YT921X_QOS_SCHED_UCAST_FLOWS + qid;
			u32 v = 0;

			res = yt921x_reg_read(priv, YT921X_QOS_SCHED_DWRR_MODE0n(idx), &v);
			if (!res && (v & YT921X_QOS_SCHED_DWRR_CFG_EN))
				data[j]++;
		}
	}
	j++;

	if (port >= 0 && port < YT921X_PSCH_SHP_PORTS) {
		res = yt921x_reg_read(priv, YT921X_PSCH_SHPn_CTRL(port), &port_ctrl);
		if (res)
			port_ctrl = 0;
		res = yt921x_reg_read(priv, YT921X_PSCH_SHPn_EBS_EIR(port), &port_ebs_eir);
		if (res)
			port_ebs_eir = 0;
	}

	data[j++] = !!(port_ctrl & YT921X_PSCH_SHP_EN);
	data[j++] = yt921x_tbf_eir_to_rate_kbps(FIELD_GET(YT921X_PSCH_SHP_EIR_M, port_ebs_eir));
	data[j++] = (u64)FIELD_GET(YT921X_PSCH_SHP_EBS_M, port_ebs_eir) * YT921X_PSCH_EBS_UNIT_BYTES;

	for (qid = 0; qid < YT921X_QSCH_SHP_QUEUES; qid++) {
		u32 idx = 0;
		u32 w0 = 0;
		u32 w2 = 0;

		data[j + qid] = 0;
		data[j + YT921X_QSCH_SHP_QUEUES + qid] = 0;

		if (port < 0 || port >= YT921X_QSCH_SHP_PORTS)
			continue;

		idx = (u32)port * YT921X_QSCH_SHP_FLOWS_PER_PORT + qid;

		res = yt921x_reg_read(priv, YT921X_QSCH_SHP_WORD2(idx), &w2);
		if (res || !(w2 & YT921X_QSCH_SHP_EN))
			continue;

		queue_en_mask |= BIT(qid);

		res = yt921x_reg_read(priv, YT921X_QSCH_SHP_WORD0(idx), &w0);
		if (res)
			continue;

		data[j + qid] =
			yt921x_tbf_eir_to_rate_kbps(FIELD_GET(YT921X_QSCH_SHP_CIR_M, w0));
		data[j + YT921X_QSCH_SHP_QUEUES + qid] =
			(u64)FIELD_GET(YT921X_QSCH_SHP_CBS_M, w0) * YT921X_PSCH_EBS_UNIT_BYTES;
	}

	data[j++] = queue_en_mask;
	j += YT921X_QSCH_SHP_QUEUES;
	j += YT921X_QSCH_SHP_QUEUES;

	data[j++] = !!(priv->storm_policer_ports & BIT(port));
	rate_kbps = DIV_ROUND_UP_ULL(priv->storm_policer_rate_bytes_per_sec * 8, 1000);
	data[j++] = rate_kbps;
	data[j++] = priv->storm_policer_burst;
}

static void
yt921x_dsa_get_strings(struct dsa_switch *ds, int port, u32 stringset,
		       uint8_t *data)
{
	if (stringset != ETH_SS_STATS)
		return;

	for (size_t i = 0; i < ARRAY_SIZE(yt921x_mib_descs); i++) {
		const struct yt921x_mib_desc *desc = &yt921x_mib_descs[i];

		if (desc->name)
			ethtool_puts(&data, desc->name);
	}

	for (size_t i = 0; i < ARRAY_SIZE(yt921x_qos_stat_names); i++)
		ethtool_puts(&data, yt921x_qos_stat_names[i]);
}

static void
yt921x_dsa_get_ethtool_stats(struct dsa_switch *ds, int port, uint64_t *data)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	struct yt921x_port *pp = &priv->ports[port];
	struct yt921x_mib *mib = &pp->mib;
	size_t j;

	mutex_lock(&priv->reg_lock);
	yt921x_read_mib(priv, port);

	j = 0;
	for (size_t i = 0; i < ARRAY_SIZE(yt921x_mib_descs); i++) {
		const struct yt921x_mib_desc *desc = &yt921x_mib_descs[i];

		if (!desc->name)
			continue;

		data[j] = ((u64 *)mib)[i];
		j++;
	}

	yt921x_qos_telemetry_fill(priv, port, &data[j]);
	j += ARRAY_SIZE(yt921x_qos_stat_names);
	mutex_unlock(&priv->reg_lock);
}

static int yt921x_dsa_get_sset_count(struct dsa_switch *ds, int port, int sset)
{
	int cnt = 0;

	if (sset != ETH_SS_STATS)
		return 0;

	for (size_t i = 0; i < ARRAY_SIZE(yt921x_mib_descs); i++) {
		const struct yt921x_mib_desc *desc = &yt921x_mib_descs[i];

		if (desc->name)
			cnt++;
	}

	cnt += ARRAY_SIZE(yt921x_qos_stat_names);

	return cnt;
}

static void
yt921x_dsa_get_eth_mac_stats(struct dsa_switch *ds, int port,
			     struct ethtool_eth_mac_stats *mac_stats)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	struct yt921x_port *pp = &priv->ports[port];
	struct yt921x_mib *mib = &pp->mib;

	mutex_lock(&priv->reg_lock);
	yt921x_read_mib(priv, port);

	mac_stats->FramesTransmittedOK = pp->tx_frames;
	mac_stats->SingleCollisionFrames = mib->tx_single_collisions;
	mac_stats->MultipleCollisionFrames = mib->tx_multiple_collisions;
	mac_stats->FramesReceivedOK = pp->rx_frames;
	mac_stats->FrameCheckSequenceErrors = mib->rx_crc_errors;
	mac_stats->AlignmentErrors = mib->rx_alignment_errors;
	mac_stats->OctetsTransmittedOK = mib->tx_good_bytes;
	mac_stats->FramesWithDeferredXmissions = mib->tx_deferred;
	mac_stats->LateCollisions = mib->tx_late_collisions;
	mac_stats->FramesAbortedDueToXSColls = mib->tx_aborted_errors;
	/* mac_stats->FramesLostDueToIntMACXmitError */
	/* mac_stats->CarrierSenseErrors */
	mac_stats->OctetsReceivedOK = mib->rx_good_bytes;
	/* mac_stats->FramesLostDueToIntMACRcvError */
	mac_stats->MulticastFramesXmittedOK = mib->tx_multicast;
	mac_stats->BroadcastFramesXmittedOK = mib->tx_broadcast;
	/* mac_stats->FramesWithExcessiveDeferral */
	mac_stats->MulticastFramesReceivedOK = mib->rx_multicast;
	mac_stats->BroadcastFramesReceivedOK = mib->rx_broadcast;
	/* mac_stats->InRangeLengthErrors */
	/* mac_stats->OutOfRangeLengthField */
	mac_stats->FrameTooLongErrors = mib->rx_oversize_errors;
	mutex_unlock(&priv->reg_lock);
}

static void
yt921x_dsa_get_eth_ctrl_stats(struct dsa_switch *ds, int port,
			      struct ethtool_eth_ctrl_stats *ctrl_stats)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	struct yt921x_port *pp = &priv->ports[port];
	struct yt921x_mib *mib = &pp->mib;

	mutex_lock(&priv->reg_lock);
	yt921x_read_mib(priv, port);

	ctrl_stats->MACControlFramesTransmitted = mib->tx_pause;
	ctrl_stats->MACControlFramesReceived = mib->rx_pause;
	/* ctrl_stats->UnsupportedOpcodesReceived */
	mutex_unlock(&priv->reg_lock);
}

static const struct ethtool_rmon_hist_range yt921x_rmon_ranges[] = {
	{ 0, 64 },
	{ 65, 127 },
	{ 128, 255 },
	{ 256, 511 },
	{ 512, 1023 },
	{ 1024, 1518 },
	{ 1519, YT921X_FRAME_SIZE_MAX },
	{}
};

static void
yt921x_dsa_get_rmon_stats(struct dsa_switch *ds, int port,
			  struct ethtool_rmon_stats *rmon_stats,
			  const struct ethtool_rmon_hist_range **ranges)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	struct yt921x_port *pp = &priv->ports[port];
	struct yt921x_mib *mib = &pp->mib;

	mutex_lock(&priv->reg_lock);
	yt921x_read_mib(priv, port);

	*ranges = yt921x_rmon_ranges;

	rmon_stats->undersize_pkts = mib->rx_undersize_errors;
	rmon_stats->oversize_pkts = mib->rx_oversize_errors;
	rmon_stats->fragments = mib->rx_alignment_errors;
	/* rmon_stats->jabbers */

	rmon_stats->hist[0] = mib->rx_64byte;
	rmon_stats->hist[1] = mib->rx_65_127byte;
	rmon_stats->hist[2] = mib->rx_128_255byte;
	rmon_stats->hist[3] = mib->rx_256_511byte;
	rmon_stats->hist[4] = mib->rx_512_1023byte;
	rmon_stats->hist[5] = mib->rx_1024_1518byte;
	rmon_stats->hist[6] = mib->rx_jumbo;

	rmon_stats->hist_tx[0] = mib->tx_64byte;
	rmon_stats->hist_tx[1] = mib->tx_65_127byte;
	rmon_stats->hist_tx[2] = mib->tx_128_255byte;
	rmon_stats->hist_tx[3] = mib->tx_256_511byte;
	rmon_stats->hist_tx[4] = mib->tx_512_1023byte;
	rmon_stats->hist_tx[5] = mib->tx_1024_1518byte;
	rmon_stats->hist_tx[6] = mib->tx_jumbo;
	mutex_unlock(&priv->reg_lock);
}

static void
yt921x_dsa_get_stats64(struct dsa_switch *ds, int port,
		       struct rtnl_link_stats64 *stats)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	struct yt921x_port *pp = &priv->ports[port];
	struct yt921x_mib *mib = &pp->mib;
	u64 rx_fcs_bytes;
	u64 tx_fcs_bytes;

	mutex_lock(&priv->reg_lock);

	/* Keep per-port traffic counters fresh for userspace readers such as
	 * LuCI port status. Relying only on delayed polling can expose stale
	 * values after topology/conduit changes.
	 */
	yt921x_read_mib(priv, port);

	stats->rx_length_errors = mib->rx_undersize_errors +
				  mib->rx_fragment_errors;
	stats->rx_over_errors = mib->rx_oversize_errors;
	stats->rx_crc_errors = mib->rx_crc_errors;
	stats->rx_frame_errors = mib->rx_alignment_errors;
	/* stats->rx_fifo_errors */
	/* stats->rx_missed_errors */

	stats->tx_aborted_errors = mib->tx_aborted_errors;
	/* stats->tx_carrier_errors */
	stats->tx_fifo_errors = mib->tx_undersize_errors;
	/* stats->tx_heartbeat_errors */
	stats->tx_window_errors = mib->tx_late_collisions;

	stats->rx_packets = pp->rx_frames;
	stats->tx_packets = pp->tx_frames;
	rx_fcs_bytes = ETH_FCS_LEN * stats->rx_packets;
	tx_fcs_bytes = ETH_FCS_LEN * stats->tx_packets;
	stats->rx_bytes = mib->rx_good_bytes;
	if (stats->rx_bytes >= rx_fcs_bytes)
		stats->rx_bytes -= rx_fcs_bytes;
	stats->tx_bytes = mib->tx_good_bytes;
	if (stats->tx_bytes >= tx_fcs_bytes)
		stats->tx_bytes -= tx_fcs_bytes;
	stats->rx_errors = stats->rx_length_errors + stats->rx_over_errors +
			   stats->rx_crc_errors + stats->rx_frame_errors;
	stats->tx_errors = stats->tx_aborted_errors + stats->tx_fifo_errors +
			   stats->tx_window_errors;
	stats->rx_dropped = mib->rx_dropped;
	/* stats->tx_dropped */
	stats->multicast = mib->rx_multicast;
	stats->collisions = mib->tx_collisions;

	mutex_unlock(&priv->reg_lock);
}

static void
yt921x_dsa_get_pause_stats(struct dsa_switch *ds, int port,
			   struct ethtool_pause_stats *pause_stats)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	struct yt921x_port *pp = &priv->ports[port];
	struct yt921x_mib *mib = &pp->mib;

	mutex_lock(&priv->reg_lock);
	yt921x_read_mib(priv, port);

	pause_stats->tx_pause_frames = mib->tx_pause;
	pause_stats->rx_pause_frames = mib->rx_pause;
	mutex_unlock(&priv->reg_lock);
}

static int
yt921x_set_eee(struct yt921x_priv *priv, int port, struct ethtool_keee *e)
{
	/* Poor datasheet for EEE operations; don't ask if you are confused */

	bool enable = e->eee_enabled;
	u16 new_mask;
	int res;

	/* Enable / disable global EEE */
	new_mask = priv->eee_ports_mask;
	new_mask &= ~BIT(port);
	new_mask |= !enable ? 0 : BIT(port);

	if (!!new_mask != !!priv->eee_ports_mask) {
		res = yt921x_reg_toggle_bits(priv, YT921X_PON_STRAP_FUNC,
					     YT921X_PON_STRAP_EEE, !!new_mask);
		if (res)
			return res;
		res = yt921x_reg_toggle_bits(priv, YT921X_PON_STRAP_VAL,
					     YT921X_PON_STRAP_EEE, !!new_mask);
		if (res)
			return res;
	}

	priv->eee_ports_mask = new_mask;

	/* Enable / disable port EEE */
	res = yt921x_reg_toggle_bits(priv, YT921X_EEE_CTRL,
				     YT921X_EEE_CTRL_ENn(port), enable);
	if (res)
		return res;
	res = yt921x_reg_toggle_bits(priv, YT921X_EEEn_VAL(port),
				     YT921X_EEE_VAL_DATA, enable);
	if (res)
		return res;

	return 0;
}

static int
yt921x_dsa_set_mac_eee(struct dsa_switch *ds, int port, struct ethtool_keee *e)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_set_eee(priv, port, e);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int
yt921x_dsa_port_change_mtu(struct dsa_switch *ds, int port, int new_mtu)
{
	/* Only serves as packet filter, since the frame size is always set to
	 * maximum after reset
	 */

	struct yt921x_priv *priv = yt921x_to_priv(ds);
	struct dsa_port *dp = dsa_to_port(ds, port);
	int frame_size;
	int res;

	frame_size = new_mtu + ETH_HLEN + ETH_FCS_LEN;
	if (dsa_port_is_cpu(dp))
		frame_size += YT921X_TAG_LEN;

	mutex_lock(&priv->reg_lock);
	res = yt921x_reg_update_bits(priv, YT921X_MACn_FRAME(port),
				     YT921X_MAC_FRAME_SIZE_M,
				     YT921X_MAC_FRAME_SIZE(frame_size));
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int yt921x_dsa_port_max_mtu(struct dsa_switch *ds, int port)
{
	/* Only called for user ports, exclude tag len here */
	return YT921X_FRAME_SIZE_MAX - ETH_HLEN - ETH_FCS_LEN - YT921X_TAG_LEN;
}

static int yt921x_mtu_fetch(struct yt921x_priv *priv, int port)
{
	struct dsa_port *dp = dsa_to_port(&priv->ds, port);

	return dp->user ? READ_ONCE(dp->user->mtu) : ETH_DATA_LEN;
}

/* v * 2^e */
static u64 ldexpu64(u64 v, int e)
{
	return e >= 0 ? v << e : v >> -e;
}

/* slot (ns) * rate (/s) / 10^9 (ns/s) = 2^C * token * 4^unit */
static u32 rate2token(u64 rate, unsigned int slot_ns, int unit, int C)
{
	int e = 2 * unit + C + YT921X_TOKEN_RATE_C;

	return div_u64(ldexpu64(slot_ns * rate, -e), 1000000000);
}

static u64 token2rate(u32 token, unsigned int slot_ns, int unit, int C)
{
	int e = 2 * unit + C + YT921X_TOKEN_RATE_C;

	return div_u64(ldexpu64(mul_u32_u32(1000000000, token), e), slot_ns);
}

/* burst = 2^C * token * 4^unit */
static u32 burst2token(u64 burst, int unit, int C)
{
	return ldexpu64(burst, -(2 * unit + C));
}

static u64 token2burst(u32 token, int unit, int C)
{
	return ldexpu64(token, 2 * unit + C);
}

struct yt921x_meter {
	u32 cir;
	u32 cbs;
	u32 ebs;
	int unit;
};

#define YT921X_METER_PKT_MODE		BIT(0)
#define YT921X_METER_SINGLE_BUCKET	BIT(1)

static int
yt921x_meter_tfm(struct yt921x_priv *priv, int port, unsigned int slot_ns,
		 u64 rate, u64 burst, unsigned int flags,
		 u32 cir_max, u32 cbs_max, int unit_max,
		 struct yt921x_meter *meterp)
{
	const int C = flags & YT921X_METER_PKT_MODE ? YT921X_TOKEN_PKT_C :
		      YT921X_TOKEN_BYTE_C;
	struct device *dev = yt921x_dev(priv);
	struct yt921x_meter meter;
	u64 burst_est;
	u64 burst_sug;
	u64 burst_max;
	u64 rate_max;

	meter.unit = unit_max;
	rate_max = token2rate(cir_max, slot_ns, meter.unit, C);
	burst_max = token2burst(cbs_max, meter.unit, C);
	if (rate > rate_max || burst > burst_max)
		return -ERANGE;

	burst_est = div_u64(slot_ns * rate, 1000000000);
	burst_sug = burst_est;
	if (flags & YT921X_METER_PKT_MODE)
		burst_sug++;
	else
		burst_sug += ETH_HLEN + yt921x_mtu_fetch(priv, port) + ETH_FCS_LEN;
	if (burst_sug > burst)
		dev_warn(dev, "Consider burst at least %llu to match rate %llu\n",
			 burst_sug, rate);

	for (; meter.unit > 0; meter.unit--) {
		if (rate > (rate_max >> 2) || burst > (burst_max >> 2))
			break;
		rate_max >>= 2;
		burst_max >>= 2;
	}

	meter.cir = rate2token(rate, slot_ns, meter.unit, C);
	if (!meter.cir)
		meter.cir = 1;
	else if (WARN_ON(meter.cir > cir_max))
		meter.cir = cir_max;
	meter.cbs = burst2token(burst, meter.unit, C);
	if (!meter.cbs)
		meter.cbs = 1;
	else if (WARN_ON(meter.cbs > cbs_max))
		meter.cbs = cbs_max;

	meter.ebs = 0;
	if (!(flags & YT921X_METER_SINGLE_BUCKET)) {
		if (flags & YT921X_METER_PKT_MODE)
			burst_est++;
		else
			burst_est += YT921X_FRAME_SIZE_MAX;

		if (burst_est < burst) {
			u32 pbs = meter.cbs;

			meter.cbs = burst2token(burst_est, meter.unit, C);
			if (!meter.cbs)
				meter.cbs = 1;
			else if (meter.cbs > cbs_max)
				meter.cbs = cbs_max;

			if (pbs > meter.cbs)
				meter.ebs = pbs - meter.cbs;
		}
	}

	dev_dbg(dev,
		"slot %u ns, rate %llu, burst %llu -> unit %d, cir %u, cbs %u, ebs %u\n",
		slot_ns, rate, burst, meter.unit, meter.cir, meter.cbs, meter.ebs);

	*meterp = meter;
	return 0;
}

static bool yt921x_tbf_supported_port(struct dsa_switch *ds, int port)
{
	/* PSCH table has 5 entries and maps to MAC0..MAC4 on YT9215. */
	return dsa_is_user_port(ds, port) && port >= 0 &&
	       port < YT921X_PSCH_SHP_PORTS;
}

static bool yt921x_mqprio_supported_port(struct dsa_switch *ds, int port)
{
	return dsa_is_user_port(ds, port);
}

static int yt921x_tbf_rate_to_eir(u64 rate_bytes_ps, u32 *eirp)
{
	u64 rate_kbps;
	u64 eir;

	if (!rate_bytes_ps)
		return -EINVAL;

	/* bytes/s -> kbits/s */
	rate_kbps = div_u64(rate_bytes_ps, 125);
	eir = DIV_ROUND_CLOSEST_ULL(rate_kbps * YT921X_PSCH_EIR_RATE_SCALE_NUM,
				    YT921X_PSCH_EIR_RATE_SCALE_DEN);
	eir += YT921X_PSCH_EIR_RATE_OFFSET;
	eir = max(eir, YT921X_PSCH_EIR_MIN_SAFE);

	if (eir > YT921X_PSCH_EIR_MAX)
		return -EOPNOTSUPP;

	*eirp = (u32)eir;
	return 0;
}

static int yt921x_tbf_burst_to_ebs(u32 max_size, u32 *ebsp)
{
	u64 ebs;

	if (!max_size)
		return -EINVAL;

	/* TBF burst (bytes) -> PSCH EBS cells (64B each). */
	ebs = DIV_ROUND_UP_ULL((u64)max_size, YT921X_PSCH_EBS_UNIT_BYTES);
	if (ebs > YT921X_PSCH_EBS_MAX)
		return -EOPNOTSUPP;

	*ebsp = (u32)ebs;
	return 0;
}

static int yt921x_tbf_del(struct yt921x_priv *priv, int port)
{
	int res;

	/* Hard-clear both words to avoid stale EIR/EBS retention across
	 * qdisc teardown/recreate cycles.
	 */
	res = yt921x_reg_write(priv, YT921X_PSCH_SHPn_CTRL(port), 0);
	if (res)
		return res;

	return yt921x_reg_write(priv, YT921X_PSCH_SHPn_EBS_EIR(port), 0);
}

static int
yt921x_tbf_add(struct yt921x_priv *priv, int port, struct tc_tbf_qopt_offload *qopt)
{
	const struct tc_tbf_qopt_offload_replace_params *params = &qopt->replace_params;
	u32 eir;
	u32 ebs;
	int res;

	res = yt921x_tbf_rate_to_eir(params->rate.rate_bytes_ps, &eir);
	if (res)
		return res;

	res = yt921x_tbf_burst_to_ebs(params->max_size, &ebs);
	if (res)
		return res;

	/* Some chips only latch EBS/EIR while shaper is disabled.
	 * Use hard writes to avoid carrying stale state between qdisc sessions.
	 */
	res = yt921x_reg_write(priv, YT921X_PSCH_SHPn_CTRL(port), 0);
	if (res)
		return res;

	res = yt921x_reg_write(priv, YT921X_PSCH_SHPn_EBS_EIR(port),
			       YT921X_PSCH_SHP_EIR(eir) |
			       YT921X_PSCH_SHP_EBS(ebs));
	if (res)
		return res;

	return yt921x_reg_write(priv, YT921X_PSCH_SHPn_CTRL(port),
				YT921X_PSCH_SHP_EN |
				YT921X_PSCH_SHP_METER_ID(0));
}

static int yt921x_qsch_queue_from_parent(u32 parent, u8 *qidp)
{
	u32 classid = parent;
	u32 qid;

	if (classid == TC_H_ROOT)
		return -EINVAL;

	qid = TC_H_MIN(classid);
	if (!qid || qid > YT921X_QSCH_SHP_QUEUES)
		return -EOPNOTSUPP;

	*qidp = qid - 1;
	return 0;
}

static int yt921x_qsch_flow_index(int port, u8 qid, u32 *idxp)
{
	if (port < 0 || port >= YT921X_QSCH_SHP_PORTS)
		return -ERANGE;
	if (qid >= YT921X_QSCH_SHP_QUEUES)
		return -ERANGE;

	*idxp = (u32)port * YT921X_QSCH_SHP_FLOWS_PER_PORT + qid;
	if (*idxp >= YT921X_QSCH_SHP_ENTRIES)
		return -ERANGE;

	return 0;
}

static int yt921x_qsch_slot_time_ensure(struct yt921x_priv *priv)
{
	u32 slot;
	int res;

	res = yt921x_reg_read(priv, YT921X_QSCH_SHP_SLOT_TIME, &slot);
	if (res)
		return res;

	if (FIELD_GET(YT921X_QSCH_SHP_SLOT_TIME_M, slot))
		return 0;

	return yt921x_reg_update_bits(priv, YT921X_QSCH_SHP_SLOT_TIME,
				      YT921X_QSCH_SHP_SLOT_TIME_M,
				      YT921X_QSCH_SHP_SLOT_TIME_VAL(
					      YT921X_QSCH_SLOT_TIME_DEFAULT));
}

static int yt921x_qsch_tbf_del(struct yt921x_priv *priv, int port, u8 qid)
{
	u32 idx;
	int res;

	res = yt921x_qsch_flow_index(port, qid, &idx);
	if (res)
		return res;

	res = yt921x_reg_update_bits(priv, YT921X_QSCH_METER_WORD0(idx),
				     YT921X_QSCH_METER_TOKEN_M,
				     YT921X_QSCH_METER_TOKEN(
					     YT921X_QSCH_METER_TOKEN_DEFAULT));
	if (res)
		return res;

	res = yt921x_reg_write(priv, YT921X_QSCH_SHP_WORD2(idx), 0);
	if (res)
		return res;
	res = yt921x_reg_write(priv, YT921X_QSCH_SHP_WORD1(idx), 0);
	if (res)
		return res;

	return yt921x_reg_write(priv, YT921X_QSCH_SHP_WORD0(idx), 0);
}

static int
yt921x_qsch_tbf_add(struct yt921x_priv *priv, int port, u8 qid,
		       struct tc_tbf_qopt_offload *qopt)
{
	const struct tc_tbf_qopt_offload_replace_params *params = &qopt->replace_params;
	u32 cir;
	u32 cbs;
	u32 idx;
	int res;

	res = yt921x_qsch_flow_index(port, qid, &idx);
	if (res)
		return res;

	res = yt921x_qsch_slot_time_ensure(priv);
	if (res)
		return res;

	res = yt921x_tbf_rate_to_eir(params->rate.rate_bytes_ps, &cir);
	if (res)
		return res;

	res = yt921x_tbf_burst_to_ebs(params->max_size, &cbs);
	if (res)
		return res;

	/* Program single-rate queue shaper profile. Keep meter-id at 0 and
	 * mirror CIR/CBS into EIR/EBS for hardware variants that consult both.
	 */
	res = yt921x_reg_write(priv, YT921X_QSCH_SHP_WORD2(idx), 0);
	if (res)
		return res;
	res = yt921x_reg_write(priv, YT921X_QSCH_SHP_WORD1(idx),
			       YT921X_QSCH_SHP_EIR(cir) |
			       YT921X_QSCH_SHP_EBS(cbs));
	if (res)
		return res;
	res = yt921x_reg_write(priv, YT921X_QSCH_SHP_WORD0(idx),
			       YT921X_QSCH_SHP_CIR(cir) |
			       YT921X_QSCH_SHP_CBS(cbs));
	if (res)
		return res;

	res = yt921x_reg_update_bits(priv, YT921X_QSCH_METER_WORD0(idx),
				     YT921X_QSCH_METER_TOKEN_M,
				     YT921X_QSCH_METER_TOKEN(
					     YT921X_QSCH_METER_TOKEN_DEFAULT));
	if (res)
		return res;

	return yt921x_reg_write(priv, YT921X_QSCH_SHP_WORD2(idx),
				YT921X_QSCH_SHP_EN |
				YT921X_QSCH_SHP_METER_ID(0));
}

static int
yt921x_mqprio_build_prio_qid_map(struct tc_mqprio_qopt_offload *mqprio,
				 u8 map[YT921X_PRIO_NUM])
{
	u8 num_tc = mqprio->qopt.num_tc;
	int i;

	if (!num_tc) {
		for (i = 0; i < YT921X_PRIO_NUM; i++)
			map[i] = i;
		return 0;
	}

	for (i = 0; i < YT921X_PRIO_NUM; i++) {
		u8 tc = mqprio->qopt.prio_tc_map[i];
		u16 count;
		u16 offset;

		if (tc >= num_tc)
			return -EINVAL;

		count = mqprio->qopt.count[tc];
		offset = mqprio->qopt.offset[tc];

		/* Queue groups are not reverse-mapped yet; support 1:1 queue IDs
		 * only for now.
		 */
		if (count != 1 || offset >= YT921X_PRIO_NUM)
			return -EOPNOTSUPP;

		map[i] = offset;
	}

	return 0;
}

static int
yt921x_mqprio_apply(struct yt921x_priv *priv, int port, const u8 map[YT921X_PRIO_NUM])
{
	u32 ucast;
	u32 mcast;
	int res;
	int i;

	res = yt921x_reg_read(priv, YT921X_QOS_QUEUE_MAP_UCASTn(port), &ucast);
	if (res)
		return res;

	res = yt921x_reg_read(priv, YT921X_QOS_QUEUE_MAP_MCASTn(port), &mcast);
	if (res)
		return res;

	for (i = 0; i < YT921X_PRIO_NUM; i++) {
		u32 qid = map[i] & GENMASK(2, 0);

		ucast &= ~YT921X_QOS_UCAST_QMAP_PRIO_M(i);
		ucast |= YT921X_QOS_UCAST_QMAP_PRIO(i, qid);

		/* Stock mcast map field is 2-bit wide; keep best-effort map. */
		mcast &= ~YT921X_QOS_MCAST_QMAP_PRIO_M(i);
		mcast |= YT921X_QOS_MCAST_QMAP_PRIO(i, min_t(u32, qid, 3));
	}

	res = yt921x_reg_write(priv, YT921X_QOS_QUEUE_MAP_UCASTn(port), ucast);
	if (res)
		return res;

	return yt921x_reg_write(priv, YT921X_QOS_QUEUE_MAP_MCASTn(port), mcast);
}

static int
yt921x_mqprio_sched_apply(struct yt921x_priv *priv, int port,
			  const u8 map[YT921X_PRIO_NUM])
{
	u16 used_ucast_qid = 0;
	u16 used_mcast_qid = 0;
	int qid;
	int i;
	int res;

	if (port < 0 || port >= YT921X_QOS_SCHED_PORTS)
		return -ERANGE;

	/* Keep plain mqprio in fully fair mode unless explicitly overridden
	 * by ETS offload.
	 */
	res = yt921x_reg_update_bits(priv, YT921X_QOS_SCHED_SPn(port),
				     YT921X_QOS_SCHED_SP_MASK, 0);
	if (res)
		return res;

	/* Scheduler tables are indexed by physical queue id (not by skb prio).
	 * Build "queue used" masks from the prio->qid map programmed above.
	 */
	for (i = 0; i < YT921X_PRIO_NUM; i++) {
		u32 uqid = map[i] & GENMASK(2, 0);
		u32 mqid = min_t(u32, uqid, YT921X_QOS_SCHED_MCAST_FLOWS - 1);

		used_ucast_qid |= BIT(uqid);
		used_mcast_qid |= BIT(mqid);
	}

	/* Stock decodes qsch_e/qsch_c dwrr cfg as bit0 in tables 0xe7/0xe8.
	 * Apply a conservative baseline: enable DWRR on queues referenced by
	 * current mqprio map, disable it on unused queues.
	 */
	for (qid = 0;
	     qid < YT921X_QOS_SCHED_UCAST_FLOWS +
		   YT921X_QOS_SCHED_MCAST_FLOWS;
	     qid++) {
		bool is_mcast = qid >= YT921X_QOS_SCHED_UCAST_FLOWS;
		u32 local_qid = is_mcast ? qid - YT921X_QOS_SCHED_UCAST_FLOWS :
					   qid;
		u32 idx = YT921X_QOS_SCHED_FLOW_INDEX(port, local_qid, is_mcast);
		u32 en = 0;

		if (is_mcast)
			en = (used_mcast_qid & BIT(local_qid)) ?
				      YT921X_QOS_SCHED_DWRR_CFG_EN :
				      0;
		else
			en = (used_ucast_qid & BIT(local_qid)) ?
				      YT921X_QOS_SCHED_DWRR_CFG_EN :
				      0;

		res = yt921x_reg_update_bits(priv,
					     YT921X_QOS_SCHED_DWRR_MODE0n(idx),
					     YT921X_QOS_SCHED_DWRR_CFG_EN,
					     en);
		if (res)
			return res;

		res = yt921x_reg_update_bits(priv,
					     YT921X_QOS_SCHED_DWRR_MODE1n(idx),
					     YT921X_QOS_SCHED_DWRR_CFG_EN,
					     en);
		if (res)
			return res;
	}

	return 0;
}

static int yt921x_ets_supported_port(struct dsa_switch *ds, int port)
{
	return dsa_is_user_port(ds, port) &&
	       port < YT921X_QOS_SCHED_PORTS;
}

static int
yt921x_ets_band_weight(const struct tc_ets_qopt_offload_replace_params *p,
		       int band, u32 *weight)
{
	u32 quanta = p->quanta[band];
	u32 wb = p->weights[band];

	if (!quanta && !wb) {
		*weight = 0;
		return 0;
	}

	/* Prefer quanta when present, fall back to weights. */
	*weight = quanta ? quanta : wb;
	if (!*weight)
		return -EINVAL;

	return 0;
}

static int
yt921x_ets_validate(struct tc_ets_qopt_offload *qopt)
{
	struct tc_ets_qopt_offload_replace_params *p = &qopt->replace_params;
	int band;
	int i;

	if (qopt->parent != TC_H_ROOT)
		return -EOPNOTSUPP;

	if (qopt->command == TC_ETS_DESTROY)
		return 0;

	if (qopt->command != TC_ETS_REPLACE)
		return -EOPNOTSUPP;

	if (!p->bands || p->bands > YT921X_PRIO_NUM)
		return -EOPNOTSUPP;

	for (i = 0; i <= TC_PRIO_MAX; i++) {
		if (p->priomap[i] >= p->bands)
			return -EINVAL;
	}

	for (band = 0; band < p->bands; band++) {
		u32 weight;
		int res;

		res = yt921x_ets_band_weight(p, band, &weight);
		if (res)
			return res;
	}

	return 0;
}

static int
yt921x_qos_read_ucast_prio_qid_map(struct yt921x_priv *priv, int port,
				   u8 map[YT921X_PRIO_NUM])
{
	u32 ucast;
	int i;
	int res;

	res = yt921x_reg_read(priv, YT921X_QOS_QUEUE_MAP_UCASTn(port), &ucast);
	if (res)
		return res;

	for (i = 0; i < YT921X_PRIO_NUM; i++)
		map[i] = (ucast >> YT921X_QOS_UCAST_QMAP_PRIO_SHIFT(i)) &
			 GENMASK(2, 0);

	return 0;
}

static int
yt921x_ets_build_qid_band_map(const struct tc_ets_qopt_offload_replace_params *p,
			      const u8 prio_qid_map[YT921X_PRIO_NUM],
			      s8 qid_band[YT921X_PRIO_NUM])
{
	int prio;

	memset(qid_band, -1, sizeof(s8) * YT921X_PRIO_NUM);

	/* ETS priomap has 16 entries, while queue-map is defined on priorities
	 * 0..7. Build queue ownership from priorities 0..7 only.
	 */
	for (prio = 0; prio < YT921X_PRIO_NUM; prio++) {
		u8 qid = prio_qid_map[prio] & GENMASK(2, 0);
		u8 band = p->priomap[prio];

		if (band >= p->bands)
			continue;

		if (qid_band[qid] >= 0 && qid_band[qid] != band)
			return -EOPNOTSUPP;

		qid_band[qid] = band;
	}

	return 0;
}

static int
yt921x_ets_apply(struct yt921x_priv *priv, int port,
		 const struct tc_ets_qopt_offload_replace_params *p)
{
	u32 sp = 0;
	s8 qid_band[YT921X_PRIO_NUM];
	u8 prio_qid_map[YT921X_PRIO_NUM];
	int qid;
	int res;

	res = yt921x_qos_read_ucast_prio_qid_map(priv, port, prio_qid_map);
	if (res)
		return res;

	res = yt921x_ets_build_qid_band_map(p, prio_qid_map, qid_band);
	if (res)
		return res;

	for (qid = 0; qid < YT921X_QOS_SCHED_UCAST_FLOWS; qid++) {
		u32 idx = YT921X_QOS_SCHED_FLOW_INDEX(port, qid, false);
		u32 en = 0;
		u32 weight = 0;
		s8 band = qid_band[qid];

		if (band >= 0) {
			res = yt921x_ets_band_weight(p, band, &weight);
			if (res)
				return res;
		}

		if (band >= 0 && !weight) {
			sp |= BIT(qid);
		} else if (band >= 0) {
			u32 w = min_t(u32, weight, (u32)GENMASK(9, 0));

			res = yt921x_reg_update_bits(priv,
						     YT921X_QOS_SCHED_DWRRn(idx),
						     YT921X_QOS_SCHED_FLOW_F0_M |
						     YT921X_QOS_SCHED_FLOW_F1_M,
						     YT921X_QOS_SCHED_FLOW_F0(w) |
						     YT921X_QOS_SCHED_FLOW_F1(w));
			if (res)
				return res;
			en = YT921X_QOS_SCHED_DWRR_CFG_EN;
		}

		res = yt921x_reg_update_bits(priv,
					     YT921X_QOS_SCHED_DWRR_MODE0n(idx),
					     YT921X_QOS_SCHED_DWRR_CFG_EN,
					     en);
		if (res)
			return res;

		res = yt921x_reg_update_bits(priv,
					     YT921X_QOS_SCHED_DWRR_MODE1n(idx),
					     YT921X_QOS_SCHED_DWRR_CFG_EN,
					     en);
		if (res)
			return res;
	}

	return yt921x_reg_update_bits(priv, YT921X_QOS_SCHED_SPn(port),
				      YT921X_QOS_SCHED_SP_MASK, sp);
}

static int
yt921x_ets_destroy(struct yt921x_priv *priv, int port)
{
	u8 map[YT921X_PRIO_NUM];
	int res;

	res = yt921x_qos_read_ucast_prio_qid_map(priv, port, map);
	if (res)
		return res;

	return yt921x_mqprio_sched_apply(priv, port, map);
}


static int
yt921x_dsa_port_setup_tc(struct dsa_switch *ds, int port,
			 enum tc_setup_type type, void *type_data)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	struct tc_ets_qopt_offload *ets;
	struct tc_mqprio_qopt_offload *mqprio;
	struct tc_tbf_qopt_offload *qopt;
	u8 prio_qid_map[YT921X_PRIO_NUM];
	int res = -EOPNOTSUPP;

	switch (type) {
	case TC_SETUP_QDISC_MQPRIO:
		if (!yt921x_mqprio_supported_port(ds, port))
			return -EOPNOTSUPP;

		mqprio = type_data;
		if ((mqprio->flags & TC_MQPRIO_F_MODE) &&
		    mqprio->mode != TC_MQPRIO_MODE_DCB)
			return -EOPNOTSUPP;
		if (mqprio->flags & (TC_MQPRIO_F_SHAPER |
				     TC_MQPRIO_F_MIN_RATE |
				     TC_MQPRIO_F_MAX_RATE))
			return -EOPNOTSUPP;

		res = yt921x_mqprio_build_prio_qid_map(mqprio, prio_qid_map);
		if (res)
			return res;

		mutex_lock(&priv->reg_lock);
		res = yt921x_mqprio_apply(priv, port, prio_qid_map);
		if (!res)
			res = yt921x_mqprio_sched_apply(priv, port, prio_qid_map);
		mutex_unlock(&priv->reg_lock);
		if (!res)
			mqprio->qopt.hw = TC_MQPRIO_HW_OFFLOAD_TCS;
		break;
	case TC_SETUP_QDISC_ETS:
		if (!yt921x_ets_supported_port(ds, port))
			return -EOPNOTSUPP;

		ets = type_data;
		res = yt921x_ets_validate(ets);
		if (res)
			return res;

		mutex_lock(&priv->reg_lock);
		switch (ets->command) {
		case TC_ETS_REPLACE:
			res = yt921x_ets_apply(priv, port, &ets->replace_params);
			break;
		case TC_ETS_DESTROY:
			res = yt921x_ets_destroy(priv, port);
			break;
		case TC_ETS_STATS:
		case TC_ETS_GRAFT:
		default:
			res = -EOPNOTSUPP;
			break;
		}
		mutex_unlock(&priv->reg_lock);
		break;
	case TC_SETUP_QDISC_TBF:
		if (!yt921x_tbf_supported_port(ds, port))
			return -EOPNOTSUPP;

		qopt = type_data;
		mutex_lock(&priv->reg_lock);
		switch (qopt->command) {
		case TC_TBF_REPLACE:
			if (qopt->parent == TC_H_ROOT) {
				res = yt921x_tbf_add(priv, port, qopt);
			} else {
				u8 qid;

				res = yt921x_qsch_queue_from_parent(qopt->parent, &qid);
				if (!res)
					res = yt921x_qsch_tbf_add(priv, port, qid, qopt);
			}
			break;
		case TC_TBF_DESTROY:
			if (qopt->parent == TC_H_ROOT) {
				res = yt921x_tbf_del(priv, port);
			} else {
				u8 qid;

				res = yt921x_qsch_queue_from_parent(qopt->parent, &qid);
				if (!res)
					res = yt921x_qsch_tbf_del(priv, port, qid);
			}
			break;
		case TC_TBF_GRAFT:
			if (qopt->parent == TC_H_ROOT) {
				/* Root tbf detach tears down port shaper. */
				if (!qopt->child_handle ||
				    qopt->child_handle == TC_H_UNSPEC)
					res = yt921x_tbf_del(priv, port);
				else
					res = 0;
			} else {
				u8 qid;

				res = yt921x_qsch_queue_from_parent(qopt->parent, &qid);
				if (res)
					break;
				if (!qopt->child_handle ||
				    qopt->child_handle == TC_H_UNSPEC)
					res = yt921x_qsch_tbf_del(priv, port, qid);
				else
					res = 0;
			}
			break;
		case TC_TBF_STATS:
			res = 0;
			break;
		default:
			res = -EOPNOTSUPP;
			break;
		}
		mutex_unlock(&priv->reg_lock);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return res;
}

#define YT921X_ACL_METER_ID_INVALID	U8_MAX
#define YT921X_ACL_METER_ID_BLACKHOLE	(YT921X_METER_NUM - 1)

static int yt921x_acl_meter_alloc(struct yt921x_priv *priv, u32 *meter_idp)
{
	unsigned long meter_id;

	meter_id = find_first_zero_bit(priv->acl_meter_map, YT921X_METER_NUM);
	if (meter_id >= YT921X_METER_NUM)
		return -ENOSPC;

	__set_bit(meter_id, priv->acl_meter_map);
	*meter_idp = meter_id;

	return 0;
}

static void yt921x_acl_meter_free(struct yt921x_priv *priv, u32 meter_id)
{
	if (meter_id >= YT921X_METER_NUM)
		return;

	__clear_bit(meter_id, priv->acl_meter_map);
}

static int yt921x_acl_meter_clear_hw(struct yt921x_priv *priv, u32 meter_id)
{
	u32 ctrls[3] = {};

	return yt921x_reg96_write(priv, YT921X_METERn_CTRL(meter_id), ctrls);
}

static int
yt921x_acl_meter_apply(struct yt921x_priv *priv, int port, u32 meter_id,
		       const struct flow_action_entry *act,
		       struct netlink_ext_ack *extack)
{
	struct yt921x_meter meter;
	bool pkt_mode;
	u32 ctrls[3];
	u64 burst;
	u64 rate;
	int res;

	if (meter_id >= YT921X_METER_NUM) {
		NL_SET_ERR_MSG_MOD(extack, "ACL meter id out of range");
		return -EINVAL;
	}

	if (act->police.peakrate_bytes_ps || act->police.avrate ||
	    act->police.overhead) {
		NL_SET_ERR_MSG_MOD(extack,
				   "peakrate / avrate / overhead not supported");
		return -EOPNOTSUPP;
	}

	if (act->police.exceed.act_id != FLOW_ACTION_DROP ||
	    act->police.notexceed.act_id != FLOW_ACTION_ACCEPT) {
		NL_SET_ERR_MSG_MOD(extack,
				   "conform-exceed other than drop-ok not supported");
		return -EOPNOTSUPP;
	}

	pkt_mode = !!act->police.rate_pkt_ps;
	rate = pkt_mode ? act->police.rate_pkt_ps : act->police.rate_bytes_ps;
	burst = pkt_mode ? act->police.burst_pkt : act->police.burst;
	if (!rate || !burst) {
		NL_SET_ERR_MSG_MOD(extack, "police rate/burst cannot be zero");
		return -EOPNOTSUPP;
	}

	res = yt921x_meter_tfm(priv, port, priv->meter_slot_ns, rate, burst,
			       pkt_mode ? YT921X_METER_PKT_MODE : 0,
			       YT921X_METER_RATE_MAX, YT921X_METER_BURST_MAX,
			       YT921X_METER_UNIT_MAX, &meter);
	if (res) {
		NL_SET_ERR_MSG_MOD(extack, "Unexpected tremendous rate");
		return res;
	}

	ctrls[0] = 0;
	ctrls[1] = YT921X_METER_CTRLb_CIR(meter.cir);
	ctrls[2] = YT921X_METER_CTRLc_UNIT(meter.unit) |
		   YT921X_METER_CTRLc_DROP_R |
		   YT921X_METER_CTRLc_TOKEN_OVERFLOW_EN |
		   YT921X_METER_CTRLc_METER_EN;
	if (pkt_mode)
		ctrls[2] |= YT921X_METER_CTRLc_PKT_MODE;

	u32p_replace_bits_unaligned(&ctrls[0], &ctrls[1],
				    YT921X_METER_CTRLab_EBS(meter.ebs),
				    YT921X_METER_CTRLab_EBS_M);
	u32p_replace_bits_unaligned(&ctrls[1], &ctrls[2],
				    YT921X_METER_CTRLbc_CBS(meter.cbs),
				    YT921X_METER_CTRLbc_CBS_M);

	return yt921x_reg96_write(priv, YT921X_METERn_CTRL(meter_id), ctrls);
}

/* ACL: 48 blocks * 8 entries
 *
 * One rule can span multiple entries, but within a block.
 */
static void
yt921x_acl_entry_set(struct yt921x_acl_entry *entry, unsigned int offset,
		     u32 flags)
{
	entry->key[offset] |= flags;
	entry->mask[offset] |= flags;
}

static unsigned int
yt921x_acl_append_first_frag(struct yt921x_acl_entry *group, unsigned int size)
{
	unsigned int i;

	for (i = 0; i < size; i++)
		switch (FIELD_GET(YT921X_ACL_KEYb_TYPE_M, group[i].key[1])) {
		case YT921X_ACL_TYPE_IPV6_DA2:
		case YT921X_ACL_TYPE_IPV6_SA2:
			yt921x_acl_entry_set(&group[i], 1,
					     YT921X_ACL_BINb_IPV6_xA2_FIRST_FRAG);
			return size;
		case YT921X_ACL_TYPE_MISC:
			yt921x_acl_entry_set(&group[i], 0,
					     YT921X_ACL_BINa_MISC_FIRST_FRAG);
			return size;
		default:
			break;
		}

	if (size >= YT921X_ACL_ENT_PER_BLK)
		return 0;

	group[i] = (typeof(*group)){};
	group[i].meter_id = YT921X_ACL_METER_ID_INVALID;
	group[i].key[1] = YT921X_ACL_KEYb_TYPE(YT921X_ACL_TYPE_MISC);
	yt921x_acl_entry_set(&group[i], 0, YT921X_ACL_BINa_MISC_FIRST_FRAG);

	return size + 1;
}

static unsigned int
yt921x_acl_append_frag(struct yt921x_acl_entry *group, unsigned int size)
{
	unsigned int i;

	for (i = 0; i < size; i++)
		switch (FIELD_GET(YT921X_ACL_KEYb_TYPE_M, group[i].key[1])) {
		case YT921X_ACL_TYPE_IPV4_DA:
		case YT921X_ACL_TYPE_IPV4_SA:
			yt921x_acl_entry_set(&group[i], 1,
					     YT921X_ACL_BINb_IPV4_FRAG);
			return size;
		case YT921X_ACL_TYPE_IPV6_DA3:
		case YT921X_ACL_TYPE_IPV6_SA3:
			yt921x_acl_entry_set(&group[i], 1,
					     YT921X_ACL_BINb_IPV6_xA3_FRAG);
			return size;
		case YT921X_ACL_TYPE_MISC:
			yt921x_acl_entry_set(&group[i], 1,
					     YT921X_ACL_BINb_MISC_FRAG);
			return size;
		case YT921X_ACL_TYPE_L4:
			yt921x_acl_entry_set(&group[i], 1,
					     YT921X_ACL_BINb_L4_FRAG);
			return size;
		default:
			break;
		}

	if (size >= YT921X_ACL_ENT_PER_BLK)
		return 0;

	group[i] = (typeof(*group)){};
	group[i].meter_id = YT921X_ACL_METER_ID_INVALID;
	group[i].key[1] = YT921X_ACL_KEYb_TYPE(YT921X_ACL_TYPE_MISC);
	yt921x_acl_entry_set(&group[i], 1, YT921X_ACL_BINb_MISC_FRAG);

	return size + 1;
}

static struct yt921x_acl_entry *
yt921x_acl_find_misc(struct yt921x_acl_entry *group, unsigned int size)
{
	for (unsigned int i = 0; i < size; i++)
		if (FIELD_GET(YT921X_ACL_KEYb_TYPE_M, group[i].key[1]) ==
		    YT921X_ACL_TYPE_MISC)
			return &group[i];

	return NULL;
}

static unsigned int
yt921x_acl_parse_key(struct yt921x_priv *priv,
		     struct yt921x_acl_entry *group, u16 ports_mask,
		     struct flow_cls_offload *cls, bool ingress)
{
	const struct flow_rule *rule = flow_cls_offload_flow_rule(cls);
	struct netlink_ext_ack *extack = cls->common.extack;
	const struct flow_dissector *dissector;
	bool n_proto_is_ipv4 = false;
	bool n_proto_is_ipv6 = false;
	struct yt921x_acl_entry *entry;
#if IS_ENABLED(CONFIG_NET_DSA_YT921X_DEBUG)
	u32 chain_mask = READ_ONCE(priv->acl_chain_key_mask);
#else
	u32 chain_mask = 0;
#endif
	unsigned int size = 0;

	dissector = rule->match.dissector;
	if (dissector->used_keys &
	    ~(BIT_ULL(FLOW_DISSECTOR_KEY_CONTROL) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_BASIC) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_IPV4_ADDRS) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_IPV6_ADDRS) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_PORTS) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_PORTS_RANGE) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_ETH_ADDRS) |
	      BIT_ULL(FLOW_DISSECTOR_KEY_TCP))) {
		NL_SET_ERR_MSG_MOD(extack, "Unsupported keys used");
		return 0;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_match_basic match;

		flow_rule_match_basic(rule, &match);
		if (match.mask->n_proto == htons(0xffff)) {
			n_proto_is_ipv4 = match.key->n_proto == htons(ETH_P_IP);
			n_proto_is_ipv6 = match.key->n_proto == htons(ETH_P_IPV6);
		}
	}

#define entry_prepare() \
	if (size >= YT921X_ACL_ENT_PER_BLK) \
		goto too_complex; \
	entry = &group[size]; \
	*entry = (typeof(*entry)){}; \
	entry->meter_id = YT921X_ACL_METER_ID_INVALID; \
	size++;

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IPV4_ADDRS) &&
	    !n_proto_is_ipv6) {
		struct flow_match_ipv4_addrs match;
		bool want_dst;
		bool want_src;

		flow_rule_match_ipv4_addrs(rule, &match);
		want_dst = !!match.mask->dst;
		want_src = !!match.mask->src;
		if (!ingress && want_src) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Egress source IPv4 match is not supported");
			return 0;
		}

		if (want_dst) {
			entry_prepare();
			entry->key[0] = ntohl(match.key->dst);
			entry->key[1] = YT921X_ACL_KEYb_TYPE(YT921X_ACL_TYPE_IPV4_DA);
			entry->mask[0] = ntohl(match.mask->dst);
		}
		if (want_src) {
			entry_prepare();
			entry->key[0] = ntohl(match.key->src);
			entry->key[1] = YT921X_ACL_KEYb_TYPE(YT921X_ACL_TYPE_IPV4_SA);
			entry->mask[0] = ntohl(match.mask->src);
		}
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IPV6_ADDRS) &&
	    !n_proto_is_ipv4) {
		struct flow_match_ipv6_addrs match;
		bool want_dst;
		bool want_src;

		flow_rule_match_ipv6_addrs(rule, &match);
		want_dst = !!match.mask->dst.s6_addr32[0] ||
			   !!match.mask->dst.s6_addr32[1] ||
			   !!match.mask->dst.s6_addr32[2] ||
			   !!match.mask->dst.s6_addr32[3];
		want_src = !!match.mask->src.s6_addr32[0] ||
			   !!match.mask->src.s6_addr32[1] ||
			   !!match.mask->src.s6_addr32[2] ||
			   !!match.mask->src.s6_addr32[3];
		if (!ingress && want_src) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Egress source IPv6 match is not supported");
			return 0;
		}

		if (want_dst)
			for (unsigned int i = 0; i < 4; i++) {
				entry_prepare();
				entry->key[0] = ntohl(match.key->dst.s6_addr32[i]);
				entry->key[1] = YT921X_ACL_KEYb_TYPE(YT921X_ACL_TYPE_IPV6_DA0 + i);
				entry->mask[0] = ntohl(match.mask->dst.s6_addr32[i]);
			}
		if (want_src)
			for (unsigned int i = 0; i < 4; i++) {
				entry_prepare();
				entry->key[0] = ntohl(match.key->src.s6_addr32[i]);
				entry->key[1] = YT921X_ACL_KEYb_TYPE(YT921X_ACL_TYPE_IPV6_SA0 + i);
				entry->mask[0] = ntohl(match.mask->src.s6_addr32[i]);
			}
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_PORTS)) {
		struct flow_match_ports match;

		entry_prepare();
		flow_rule_match_ports(rule, &match);
		entry->key[0] = (ntohs(match.key->dst) << 16) | ntohs(match.key->src);
		entry->key[1] = YT921X_ACL_KEYb_TYPE(YT921X_ACL_TYPE_L4);
		entry->mask[0] = (ntohs(match.mask->dst) << 16) |
				 ntohs(match.mask->src);
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_PORTS_RANGE)) {
		struct flow_match_ports_range match;

		entry_prepare();
		flow_rule_match_ports_range(rule, &match);
		entry->key[0] = (ntohs(match.key->tp_min.dst) << 16) |
				ntohs(match.key->tp_min.src);
		entry->key[1] = YT921X_ACL_KEYb_TYPE(YT921X_ACL_TYPE_L4) |
				YT921X_ACL_KEYb_L4_SPORT_RANGE_EN |
				YT921X_ACL_KEYb_L4_DPORT_RANGE_EN;
		entry->mask[0] = (ntohs(match.mask->tp_max.dst) << 16) |
				 ntohs(match.mask->tp_max.src);
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ETH_ADDRS)) {
		struct flow_match_eth_addrs match;
		bool want_dst;
		bool want_src;

		flow_rule_match_eth_addrs(rule, &match);
		want_dst = !is_zero_ether_addr(match.mask->dst);
		want_src = !is_zero_ether_addr(match.mask->src);
		if (!ingress && want_src) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Egress source MAC match is not supported");
			return 0;
		}

		if (want_dst) {
			entry_prepare();
			entry->key[0] = mac_hi4_to_cpu(match.key->dst);
			entry->key[1] = YT921X_ACL_KEYb_TYPE(YT921X_ACL_TYPE_MAC_DA0);
			entry->mask[0] = mac_hi4_to_cpu(match.mask->dst);
		}
		if (want_src) {
			entry_prepare();
			entry->key[0] = mac_hi4_to_cpu(match.key->src);
			entry->key[1] = YT921X_ACL_KEYb_TYPE(YT921X_ACL_TYPE_MAC_SA0);
			entry->mask[0] = mac_hi4_to_cpu(match.mask->src);
		}
		if (want_src || want_dst) {
			entry_prepare();
			entry->key[0] = (mac_lo2_to_cpu(match.key->dst) << 16) |
					mac_lo2_to_cpu(match.key->src);
			entry->key[1] = YT921X_ACL_KEYb_TYPE(YT921X_ACL_TYPE_MAC_DA1_SA1);
			entry->mask[0] = (mac_lo2_to_cpu(match.mask->dst) << 16) |
					 mac_lo2_to_cpu(match.mask->src);
		}
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_CONTROL)) {
		u32 supp_flags = FLOW_DIS_IS_FRAGMENT | FLOW_DIS_FIRST_FRAG;
		struct flow_match_control match;

		flow_rule_match_control(rule, &match);
		if (!flow_rule_is_supp_control_flags(supp_flags,
						     match.mask->flags, extack))
			return 0;

		if (match.mask->flags & FLOW_DIS_FIRST_FRAG)
			size = yt921x_acl_append_first_frag(group, size);
		else if (match.mask->flags & FLOW_DIS_IS_FRAGMENT)
			size = yt921x_acl_append_frag(group, size);

		if (!size)
			goto too_complex;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_TCP)) {
		struct flow_match_tcp match;

		entry = yt921x_acl_find_misc(group, size);
		if (!entry) {
			entry_prepare();
			entry->key[1] = YT921X_ACL_KEYb_TYPE(YT921X_ACL_TYPE_MISC);
		}

		flow_rule_match_tcp(rule, &match);
		entry->key[0] |= YT921X_ACL_BINa_MISC_TCP_FLAGS(ntohs(match.key->flags));
		entry->mask[0] |= YT921X_ACL_BINa_MISC_TCP_FLAGS(ntohs(match.mask->flags));
	}

	for (unsigned int i = 0; i < size; i++) {
		u32 ctrl_key_bits;

		/* Experimental: for multi-entry ACL groups, set user-selected
		 * bits on non-final entries to probe potential chain/AND-next
		 * semantics in undocumented key-control fields.
		 */
		if (chain_mask && i + 1 < size)
			group[i].key[1] |= chain_mask;

		group[i].key[1] |= YT921X_ACL_KEYb_SPORTS(ports_mask);
		if (!ingress)
			group[i].key[1] |= YT921X_ACL_KEYb_REVERSE;
		group[i].mask[1] |= YT921X_ACL_KEYb_SPORTS_M;
		if (FIELD_GET(YT921X_ACL_KEYb_TYPE_M, group[i].key[1]) !=
		    YT921X_ACL_TYPE_NA)
			group[i].mask[1] |= YT921X_ACL_KEYb_TYPE_M;

		ctrl_key_bits = group[i].key[1] &
				~(YT921X_ACL_KEYb_SPORTS_M |
				  YT921X_ACL_KEYb_TYPE_M);
		group[i].mask[1] |= ctrl_key_bits;
	}

	for (unsigned int i = 1; i < size; i++)
		group[i].type = U32_MAX;

	return size;

too_complex:
	NL_SET_ERR_MSG_MOD(extack, "Rule too complex");
	return 0;
}

static int
yt921x_acl_parse_action(struct yt921x_acl_entry *group,
			struct dsa_switch *ds,
			struct flow_cls_offload *cls)
{
	const struct flow_rule *rule = flow_cls_offload_flow_rule(cls);
	struct netlink_ext_ack *extack = cls->common.extack;
	const struct flow_action_entry *act;
	struct dsa_port *to_dp;
	bool mirror_seen = false;
	u32 *action = group[0].action;
	int i;

	memset(action, 0, sizeof(group[0].action));
	group[0].mirror_en = false;
	group[0].mirror_to_port = 0;
	flow_action_for_each(i, act, &rule->action) {
		switch (act->id) {
		case FLOW_ACTION_ACCEPT:
			/* Match + no punitive action = permit/pass. */
			break;
		case FLOW_ACTION_DROP:
			if ((action[2] & YT921X_ACL_ACTc_REDIR_EN) &&
			    (action[2] & YT921X_ACL_ACTc_REDIR_M) ==
				    YT921X_ACL_ACTc_REDIR_TRAP) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Trap cannot be combined with drop");
				return -EOPNOTSUPP;
			}
			action[0] |= YT921X_ACL_ACTa_METER_EN;
			action[0] |= FIELD_PREP(YT921X_ACL_ACTa_METER_ID_M,
						YT921X_ACL_METER_ID_BLACKHOLE);
			group[0].meter_id = YT921X_ACL_METER_ID_BLACKHOLE;
			break;
		case FLOW_ACTION_TRAP:
			if (group[0].mirror_en) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Trap cannot be combined with mirror");
				return -EOPNOTSUPP;
			}
			if ((action[2] & YT921X_ACL_ACTc_REDIR_EN) &&
			    (action[2] & YT921X_ACL_ACTc_REDIR_M) !=
				    YT921X_ACL_ACTc_REDIR_TRAP) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Trap cannot be combined with redirect");
				return -EOPNOTSUPP;
			}
			action[2] &= ~(YT921X_ACL_ACTc_REDIR_M |
				       YT921X_ACL_ACTc_REDIR_DPORTS_M);
			action[2] |= YT921X_ACL_ACTc_REDIR_EN;
			action[2] |= YT921X_ACL_ACTc_REDIR_TRAP;
			break;
		case FLOW_ACTION_REDIRECT:
#ifdef FLOW_ACTION_REDIRECT_INGRESS
		case FLOW_ACTION_REDIRECT_INGRESS:
#endif
			if (group[0].mirror_en) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Redirect cannot be combined with mirror");
				return -EOPNOTSUPP;
			}
			if ((action[2] & YT921X_ACL_ACTc_REDIR_EN) &&
			    (action[2] & YT921X_ACL_ACTc_REDIR_M) !=
				    YT921X_ACL_ACTc_REDIR_STEER) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Redirect cannot be combined with trap");
				return -EOPNOTSUPP;
			}
			to_dp = dsa_port_from_netdev(act->dev);
			if (IS_ERR(to_dp)) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Redirect destination is not a switch port");
				return -EOPNOTSUPP;
			}
			if (to_dp->ds != ds) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Redirect destination on different switch is not supported");
				return -EOPNOTSUPP;
			}
			action[2] &= ~(YT921X_ACL_ACTc_REDIR_M |
				       YT921X_ACL_ACTc_REDIR_DPORTS_M);
			action[2] |= YT921X_ACL_ACTc_REDIR_EN;
			action[2] |= YT921X_ACL_ACTc_REDIR_STEER;
			action[2] |= YT921X_ACL_ACTc_REDIR_DPORTn(to_dp->index);
			break;
		case FLOW_ACTION_MIRRED:
#ifdef FLOW_ACTION_MIRRED_INGRESS
		case FLOW_ACTION_MIRRED_INGRESS:
#endif
			to_dp = dsa_port_from_netdev(act->dev);
			if (IS_ERR(to_dp)) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Mirror destination is not a switch port");
				return -EOPNOTSUPP;
			}
			if (to_dp->ds != ds) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Mirror destination on different switch is not supported");
				return -EOPNOTSUPP;
			}
			if (!dsa_is_user_port(ds, to_dp->index)) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Only user ports can be mirror destination");
				return -EOPNOTSUPP;
			}
			if (action[2] & YT921X_ACL_ACTc_REDIR_EN) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Mirror cannot be combined with redirect/trap");
				return -EOPNOTSUPP;
			}
			if (mirror_seen && group[0].mirror_to_port != to_dp->index) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Multiple mirror destinations are not supported");
				return -EOPNOTSUPP;
			}

			action[0] |= YT921X_ACL_ACTa_MIRROR_EN;
			group[0].mirror_en = true;
			group[0].mirror_to_port = to_dp->index;
			mirror_seen = true;
			break;
		case FLOW_ACTION_PRIORITY:
			if (act->priority >= YT921X_PRIO_NUM) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Priority value is too high");
				return -EOPNOTSUPP;
			}
			action[0] |= YT921X_ACL_ACTa_PRIO_EN;
			action[1] |= YT921X_ACL_ACTb_PRIO(act->priority);
			break;
		case FLOW_ACTION_POLICE:
			if ((action[2] & YT921X_ACL_ACTc_REDIR_EN) &&
			    (action[2] & YT921X_ACL_ACTc_REDIR_M) ==
				    YT921X_ACL_ACTc_REDIR_TRAP) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Trap cannot be combined with police");
				return -EOPNOTSUPP;
			}
			if (act->police.peakrate_bytes_ps || act->police.avrate ||
			    act->police.overhead) {
				NL_SET_ERR_MSG_MOD(extack,
						   "peakrate / avrate / overhead not supported");
				return -EOPNOTSUPP;
			}
			if (act->police.exceed.act_id != FLOW_ACTION_DROP ||
			    act->police.notexceed.act_id != FLOW_ACTION_ACCEPT) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Hardware police requires conform-exceed drop/ok");
				return -EOPNOTSUPP;
			}
			action[0] |= YT921X_ACL_ACTa_METER_EN;
			break;
		default:
			NL_SET_ERR_MSG_MOD(extack, "Action not supported");
			return -EOPNOTSUPP;
		}
	}

	return 0;
}

static unsigned int
yt921x_acl_parse(struct yt921x_acl_entry *group, u16 ports_mask,
		 struct yt921x_priv *priv, struct dsa_switch *ds,
		 struct flow_cls_offload *cls, bool ingress)
{
	unsigned int size;
	int res;

	size = yt921x_acl_parse_key(priv, group, ports_mask, cls, ingress);
	if (!size)
		return 0;

	res = yt921x_acl_parse_action(group, ds, cls);
	if (res)
		return 0;

	for (unsigned int i = 0; i < size; i++)
		group[i].cookie = cls->cookie;

	return size;
}

static unsigned int
yt921x_acl_find(const struct yt921x_priv *priv, unsigned long cookie)
{
	for (unsigned int i = 0; i < YT921X_ACL_NUM; i++)
		if (priv->acl.entries[i].cookie == cookie)
			return i;

	return UINT_MAX;
}

static int
yt921x_acl_commit(struct yt921x_priv *priv, unsigned int blkid, u8 ents_mask,
		  u8 acts_mask)
{
	const struct yt921x_acl_entry *entries;
	unsigned long mask;
	unsigned long i;
	u32 ctrl;
	int res;

	entries = &priv->acl.entries[YT921X_ACL_ENT_PER_BLK * blkid];

	ctrl = YT921X_ACL_BLK_CMD_MODIFY | YT921X_ACL_BLK_CMD_BLKID(blkid);
	res = yt921x_reg_write(priv, YT921X_ACL_BLK_CMD, ctrl);
	if (res)
		return res;

	mask = ents_mask;
	for_each_set_bit(i, &mask, YT921X_ACL_ENT_PER_BLK) {
		u64 key = ((u64)entries[i].key[1] << 32) | entries[i].key[0];
		u64 acl_mask = ((u64)entries[i].mask[1] << 32) | entries[i].mask[0];

		res = yt921x_reg64_write(priv, YT921X_ACLn_KEYm(blkid, i), key);
		if (res)
			return res;
		res = yt921x_reg64_write(priv, YT921X_ACLn_MASKm(blkid, i),
					 acl_mask);
		if (res)
			return res;
	}

	ctrl = 0;
	for (unsigned int j = 0; j < YT921X_ACL_ENT_PER_BLK; j++)
		ctrl |= YT921X_ACL_BLK_KEEP_KEEPn(j);
	mask = ents_mask;
	for_each_set_bit(i, &mask, YT921X_ACL_ENT_PER_BLK)
		ctrl &= ~YT921X_ACL_BLK_KEEP_KEEPn(i);
	res = yt921x_reg_write(priv, YT921X_ACL_BLK_KEEP, ctrl);
	if (res)
		return res;

	/* Write actions first, then enable entries. This avoids a window where
	 * an entry is active while still carrying stale action state.
	 */
	mask = acts_mask;
	for_each_set_bit(i, &mask, YT921X_ACL_ENT_PER_BLK) {
		unsigned int e = i + YT921X_ACL_ENT_PER_BLK * blkid;

		res = yt921x_reg96_write(priv, YT921X_ACLn_ACT(e),
					 entries[i].action);
		if (res)
			return res;
	}

	ctrl = 0;
	for (unsigned int j = 0; j < YT921X_ACL_ENT_PER_BLK; j++) {
		unsigned int start;

		if (!entries[j].cookie)
			continue;
		start = entries[j].type != U32_MAX ? j : entries[j].start;
		ctrl |= YT921X_ACL_ENTRY_ENm(j) | YT921X_ACL_ENTRY_GRPIDm(j, start);
	}
	res = yt921x_reg_write(priv, YT921X_ACLn_ENTRY(blkid), ctrl);
	if (res)
		return res;

	ctrl = YT921X_ACL_BLK_CMD_BLKID(blkid);
	res = yt921x_reg_write(priv, YT921X_ACL_BLK_CMD, ctrl);
	if (res)
		return res;

	return 0;
}

static int
yt921x_acl_del(struct yt921x_priv *priv, unsigned long cookie,
	       bool *mirror_enp, u8 *mirror_to_portp)
{
	struct yt921x_acl_entry *entries;
	unsigned int offset;
	unsigned int blkid;
	unsigned int entid;
	u32 meter_id;
	bool meter_en;
	u8 ents_mask;
	int res;

	entid = yt921x_acl_find(priv, cookie);
	if (entid == UINT_MAX)
		return -ENOENT;

	blkid = entid / YT921X_ACL_ENT_PER_BLK;
	offset = entid % YT921X_ACL_ENT_PER_BLK;
	entries = &priv->acl.entries[YT921X_ACL_ENT_PER_BLK * blkid];
	if (entries[offset].type == U32_MAX)
		offset = entries[offset].start;
	meter_id = entries[offset].meter_id;
	meter_en = !!(entries[offset].action[0] & YT921X_ACL_ACTa_METER_EN);
	if (mirror_enp)
		*mirror_enp = entries[offset].mirror_en;
	if (mirror_to_portp)
		*mirror_to_portp = entries[offset].mirror_to_port;

	ents_mask = 0;
	for (unsigned int i = offset; i < YT921X_ACL_ENT_PER_BLK; i++) {
		if (entries[i].cookie != cookie)
			continue;
		entries[i] = (typeof(*entries)){};
		ents_mask |= BIT(i);
	}

	priv->acl.useds[blkid] -= hweight8(ents_mask);

	res = yt921x_acl_commit(priv, blkid, ents_mask, BIT(offset));
	if (res)
		return res;

	if (meter_en && meter_id != YT921X_ACL_METER_ID_INVALID &&
	    meter_id != YT921X_ACL_METER_ID_BLACKHOLE) {
		res = yt921x_acl_meter_clear_hw(priv, meter_id);
		if (res)
			return res;

		yt921x_acl_meter_free(priv, meter_id);
	}

	return 0;
}

static int
yt921x_acl_add(struct yt921x_priv *priv, const struct yt921x_acl_entry *group,
	       unsigned int size, struct netlink_ext_ack *extack)
{
	struct yt921x_acl_entry *entries;
	unsigned int used_total = 0;
	unsigned int best_free;
	unsigned int offset = 0;
	unsigned int blkid;
	unsigned int free;
	unsigned int used;
	u8 ents_mask;

	best_free = UINT_MAX;
	blkid = UINT_MAX;
	for (unsigned int i = 0; i < YT921X_ACL_BLK_NUM; i++) {
		used = priv->acl.useds[i];
		used_total += used;
		if (used > YT921X_ACL_ENT_PER_BLK) {
			WARN_ON_ONCE(1);
			continue;
		}

		free = YT921X_ACL_ENT_PER_BLK - used;
		if (free < size)
			continue;
		if (free >= best_free)
			continue;

		best_free = free;
		blkid = i;
		if (free == size)
			break;
	}

	if (blkid == UINT_MAX) {
		if (used_total >= YT921X_ACL_NUM)
			NL_SET_ERR_MSG_MOD(extack, "Hardware ACL table full (max 384 entries)");
		else if (size > 1)
			NL_SET_ERR_MSG_MOD(extack, "No ACL block has enough free slots for this rule");
		else
			NL_SET_ERR_MSG_MOD(extack, "No ACL slot available");
		return -ENOSPC;
	}

	entries = &priv->acl.entries[YT921X_ACL_ENT_PER_BLK * blkid];
	ents_mask = 0;
	for (unsigned int i = 0, j = 0; i < YT921X_ACL_ENT_PER_BLK; i++) {
		if (entries[i].cookie)
			continue;

		entries[i] = group[j];
		if (!j)
			offset = i;
		else
			entries[i].start = offset;

		ents_mask |= BIT(i);
		j++;
		if (j >= size)
			break;
	}

	priv->acl.useds[blkid] += size;
	WARN_ON(priv->acl.useds[blkid] > YT921X_ACL_ENT_PER_BLK);

	return yt921x_acl_commit(priv, blkid, ents_mask, BIT(offset));
}

static int yt921x_acl_mirror_get(struct yt921x_priv *priv, int to_local_port,
				 struct netlink_ext_ack *extack);
static void yt921x_acl_mirror_put(struct yt921x_priv *priv, int to_local_port);

static int
yt921x_dsa_cls_flower_stats(struct dsa_switch *ds, int port,
			    struct flow_cls_offload *cls, bool ingress)
{
	if (!cls->cookie)
		return -EINVAL;

	return -EOPNOTSUPP;
}

static int
yt921x_dsa_cls_flower_del(struct dsa_switch *ds, int port,
			  struct flow_cls_offload *cls, bool ingress)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	u8 mirror_to_port = 0;
	bool mirror_en = false;
	int res;

	if (!cls->cookie)
		return -EINVAL;
	if (!dsa_is_user_port(ds, port))
		return -EOPNOTSUPP;

	mutex_lock(&priv->reg_lock);
	res = yt921x_acl_del(priv, cls->cookie, &mirror_en, &mirror_to_port);
	if (!res && mirror_en)
		yt921x_acl_mirror_put(priv, mirror_to_port);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int
yt921x_dsa_cls_flower_add(struct dsa_switch *ds, int port,
			  struct flow_cls_offload *cls, bool ingress)
{
	struct yt921x_acl_entry group[YT921X_ACL_ENT_PER_BLK];
	const struct flow_rule *rule = flow_cls_offload_flow_rule(cls);
	const struct flow_action_entry *act;
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	u32 meter_id = YT921X_ACL_METER_ID_INVALID;
	bool mirror_prepared = false;
	bool police_seen = false;
	unsigned int size;
	int i;
	int res;

	if (!cls->cookie)
		return -EINVAL;
	if (!dsa_is_user_port(ds, port))
		return -EOPNOTSUPP;
	if (cls->common.chain_index) {
		NL_SET_ERR_MSG_MOD(cls->common.extack, "chain is not supported");
		return -EOPNOTSUPP;
	}

	size = yt921x_acl_parse(group, BIT(port), priv, ds, cls, ingress);
	if (!size)
		return -EOPNOTSUPP;

	mutex_lock(&priv->reg_lock);
	flow_action_for_each(i, act, &rule->action) {
		if (act->id != FLOW_ACTION_POLICE)
			continue;

		if (police_seen) {
			NL_SET_ERR_MSG_MOD(cls->common.extack,
					   "Multiple police actions are not supported");
			res = -EOPNOTSUPP;
			goto out_unlock;
		}

		res = yt921x_acl_meter_alloc(priv, &meter_id);
		if (res) {
			NL_SET_ERR_MSG_MOD(cls->common.extack,
					   "No ACL meter profile available");
			res = -ENOSPC;
			goto out_unlock;
		}

		res = yt921x_acl_meter_apply(priv, port, meter_id,
					     act, cls->common.extack);
		if (res) {
			yt921x_acl_meter_free(priv, meter_id);
			meter_id = YT921X_ACL_METER_ID_INVALID;
			goto out_unlock;
		}

		group[0].action[0] &= ~YT921X_ACL_ACTa_METER_ID_M;
		group[0].action[0] |= FIELD_PREP(YT921X_ACL_ACTa_METER_ID_M,
						 meter_id);
		group[0].meter_id = meter_id;
		police_seen = true;
	}

	if (group[0].mirror_en) {
		res = yt921x_acl_mirror_get(priv, group[0].mirror_to_port,
					    cls->common.extack);
		if (res) {
			if (meter_id != YT921X_ACL_METER_ID_INVALID) {
				yt921x_acl_meter_clear_hw(priv, meter_id);
				yt921x_acl_meter_free(priv, meter_id);
			}
			goto out_unlock;
		}

		mirror_prepared = true;
	}

	res = yt921x_acl_add(priv, group, size, cls->common.extack);
	if (res && meter_id != YT921X_ACL_METER_ID_INVALID) {
		yt921x_acl_meter_clear_hw(priv, meter_id);
		yt921x_acl_meter_free(priv, meter_id);
	}
	if (res && mirror_prepared)
		yt921x_acl_mirror_put(priv, group[0].mirror_to_port);
out_unlock:
	mutex_unlock(&priv->reg_lock);

	return res;
}


static int
yt921x_mirror_prio_map_apply(struct yt921x_priv *priv, bool enable)
{
	u32 val;
	u32 ctrl = 0;
	int res;

	if (enable) {
		ctrl |= YT921X_MIRROR_PRIO_MAP_IGR_EN;
		ctrl |= YT921X_MIRROR_PRIO_MAP_IGR_PRIO
			(YT921X_MIRROR_PRIO_MAP_DEFAULT_PRIO);
		ctrl |= YT921X_MIRROR_PRIO_MAP_EGR_EN;
		ctrl |= YT921X_MIRROR_PRIO_MAP_EGR_PRIO
			(YT921X_MIRROR_PRIO_MAP_DEFAULT_PRIO);
	}

	res = yt921x_reg_read(priv, YT921X_MIRROR_PRIO_MAP, &val);
	if (res)
		return res;
	if (val == ctrl)
		return 0;

	return yt921x_reg_write(priv, YT921X_MIRROR_PRIO_MAP, ctrl);
}

static int
yt921x_mirror_del(struct yt921x_priv *priv, int port, bool ingress)
{
	u32 src_mask;
	u32 val;
	u32 ctrl;
	bool mirror_active;
	int res;

	if (ingress)
		src_mask = YT921X_MIRROR_IGR_PORTn(port);
	else
		src_mask = YT921X_MIRROR_EGR_PORTn(port);

	res = yt921x_reg_read(priv, YT921X_MIRROR, &val);
	if (res)
		return res;

	ctrl = val & ~src_mask;
	mirror_active = ctrl & (YT921X_MIRROR_EGR_PORTS_M |
				YT921X_MIRROR_IGR_PORTS_M);
	if (!mirror_active)
		ctrl &= ~YT921X_MIRROR_PORT_M;
	if (ctrl != val) {
		res = yt921x_reg_write(priv, YT921X_MIRROR, ctrl);
		if (res)
			return res;
	}

	if (!mirror_active)
		return yt921x_mirror_prio_map_apply(priv, false);

	return 0;
}

static int
yt921x_mirror_add(struct yt921x_priv *priv, int port, bool ingress,
		  int to_local_port, struct netlink_ext_ack *extack)
{
	u32 srcs;
	u32 ctrl;
	u32 val;
	u32 dst;
	bool changed;
	int res;

	if (ingress)
		srcs = YT921X_MIRROR_IGR_PORTn(port);
	else
		srcs = YT921X_MIRROR_EGR_PORTn(port);
	dst = YT921X_MIRROR_PORT(to_local_port);

	res = yt921x_reg_read(priv, YT921X_MIRROR, &val);
	if (res)
		return res;

	/* other mirror tasks & different dst port -> conflict */
	if ((val & ~srcs & (YT921X_MIRROR_EGR_PORTS_M |
			    YT921X_MIRROR_IGR_PORTS_M)) &&
	    (val & YT921X_MIRROR_PORT_M) != dst) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Sniffer port is already configured, delete existing rules & retry");
		return -EBUSY;
	}

	ctrl = val & ~YT921X_MIRROR_PORT_M;
	ctrl |= srcs;
	ctrl |= dst;
	changed = ctrl != val;

	if (changed) {
		res = yt921x_reg_write(priv, YT921X_MIRROR, ctrl);
		if (res)
			return res;
	}

	res = yt921x_mirror_prio_map_apply(priv, true);
	if (res && changed)
		yt921x_reg_write(priv, YT921X_MIRROR, val);

	return res;
}

static int yt921x_acl_mirror_get(struct yt921x_priv *priv, int to_local_port,
				 struct netlink_ext_ack *extack)
{
	u32 val;
	u32 dst;
	u32 ctrl;
	int res;

	if (priv->acl_mirror_count) {
		if (priv->acl_mirror_to_port != to_local_port) {
			NL_SET_ERR_MSG_MOD(extack,
					   "ACL mirror uses one global destination port");
			return -EBUSY;
		}

		priv->acl_mirror_count++;
		return 0;
	}

	dst = YT921X_MIRROR_PORT(to_local_port);
	res = yt921x_reg_read(priv, YT921X_MIRROR, &val);
	if (res)
		return res;

	if ((val & (YT921X_MIRROR_EGR_PORTS_M | YT921X_MIRROR_IGR_PORTS_M)) &&
	    (val & YT921X_MIRROR_PORT_M) != dst) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Sniffer port already configured with a different destination");
		return -EBUSY;
	}

	ctrl = (val & ~YT921X_MIRROR_PORT_M) | dst;
	if (ctrl != val) {
		res = yt921x_reg_write(priv, YT921X_MIRROR, ctrl);
		if (res)
			return res;
	}

	res = yt921x_mirror_prio_map_apply(priv, true);
	if (res) {
		if (ctrl != val)
			yt921x_reg_write(priv, YT921X_MIRROR, val);
		return res;
	}

	priv->acl_mirror_to_port = to_local_port;
	priv->acl_mirror_count = 1;

	return 0;
}

static void yt921x_acl_mirror_put(struct yt921x_priv *priv, int to_local_port)
{
	u32 val;
	u32 ctrl;
	bool mirror_active;

	if (!priv->acl_mirror_count)
		return;
	if (priv->acl_mirror_to_port != to_local_port)
		return;

	priv->acl_mirror_count--;
	if (priv->acl_mirror_count)
		return;

	priv->acl_mirror_to_port = -1;

	if (yt921x_reg_read(priv, YT921X_MIRROR, &val))
		return;

	ctrl = val;
	mirror_active = !!(ctrl & (YT921X_MIRROR_EGR_PORTS_M |
				   YT921X_MIRROR_IGR_PORTS_M));
	if (!mirror_active) {
		ctrl &= ~YT921X_MIRROR_PORT_M;
		if (ctrl != val)
			yt921x_reg_write(priv, YT921X_MIRROR, ctrl);
		yt921x_mirror_prio_map_apply(priv, false);
	}
}

static void
yt921x_dsa_port_mirror_del(struct dsa_switch *ds, int port,
			   struct dsa_mall_mirror_tc_entry *mirror)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	struct device *dev = yt921x_dev(priv);
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_mirror_del(priv, port, mirror->ingress);
	mutex_unlock(&priv->reg_lock);

	if (res)
		dev_err(dev, "Failed to %s port %d: %i\n", "unmirror",
			port, res);
}

static int
yt921x_dsa_port_mirror_add(struct dsa_switch *ds, int port,
			   struct dsa_mall_mirror_tc_entry *mirror,
			   bool ingress, struct netlink_ext_ack *extack)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	int to_port = mirror->to_local_port;
	int res;

	if (!dsa_is_user_port(ds, port)) {
		NL_SET_ERR_MSG_MOD(extack, "Only user ports can be mirrored");
		return -EOPNOTSUPP;
	}
	if (to_port >= ds->num_ports || !dsa_is_user_port(ds, to_port)) {
		NL_SET_ERR_MSG_MOD(extack, "Mirror destination must be a user port");
		return -EOPNOTSUPP;
	}
	if (to_port == port) {
		NL_SET_ERR_MSG_MOD(extack, "Mirror destination must differ from source port");
		return -EINVAL;
	}

	mutex_lock(&priv->reg_lock);
	res = yt921x_mirror_add(priv, port, ingress,
				to_port, extack);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int yt921x_lag_hash(struct yt921x_priv *priv, u32 ctrl, bool unique_lag,
			   struct netlink_ext_ack *extack)
{
	u32 val;
	int res;

	/* Hash Mode is global. Make sure the same Hash Mode is set to all the
	 * 2 possible lags.
	 * If we are the unique LAG we can set whatever hash mode we want.
	 * To change hash mode it's needed to remove all LAG and change the mode
	 * with the latest.
	 */
	if (unique_lag) {
		res = yt921x_reg_write(priv, YT921X_LAG_HASH, ctrl);
		if (res)
			return res;
	} else {
		res = yt921x_reg_read(priv, YT921X_LAG_HASH, &val);
		if (res)
			return res;

		if (val != ctrl) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Mismatched Hash Mode across different lags is not supported");
			return -EOPNOTSUPP;
		}
	}

	return 0;
}

static int yt921x_lag_set(struct yt921x_priv *priv, u8 index, u16 ports_mask)
{
	unsigned long targets_mask = ports_mask;
	unsigned int cnt;
	u32 ctrl;
	int port;
	int res;

	cnt = 0;
	for_each_set_bit(port, &targets_mask, YT921X_PORT_NUM) {
		ctrl = YT921X_LAG_MEMBER_PORT(port);
		res = yt921x_reg_write(priv, YT921X_LAG_MEMBERnm(index, cnt),
				       ctrl);
		if (res)
			return res;

		cnt++;
	}

	ctrl = YT921X_LAG_GROUP_PORTS(ports_mask) |
	       YT921X_LAG_GROUP_MEMBER_NUM(cnt);
	return yt921x_reg_write(priv, YT921X_LAG_GROUPn(index), ctrl);
}

static int
yt921x_dsa_port_lag_leave(struct dsa_switch *ds, int port, struct dsa_lag lag)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	struct dsa_port *dp;
	u32 ctrl;
	int res;

	if (!lag.id || lag.id > ds->num_lag_ids)
		return 0;

	ctrl = 0;
	dsa_lag_foreach_port(dp, ds->dst, &lag)
		ctrl |= BIT(dp->index);

	mutex_lock(&priv->reg_lock);
	res = yt921x_lag_set(priv, lag.id - 1, ctrl);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int
yt921x_dsa_port_lag_check(struct dsa_switch *ds, int port, struct dsa_lag lag,
			  struct netdev_lag_upper_info *info,
			  struct netlink_ext_ack *extack)
{
	unsigned int members;
	struct dsa_port *dp;

	if (!dsa_is_user_port(ds, port)) {
		NL_SET_ERR_MSG_MOD(extack, "Only user ports can join a LAG");
		return -EOPNOTSUPP;
	}

	if (!lag.id) {
		NL_SET_ERR_MSG_MOD(extack, "No free hardware LAG ID available");
		return -EOPNOTSUPP;
	}

	if (lag.id > ds->num_lag_ids) {
		NL_SET_ERR_MSG_MOD(extack, "LAG ID exceeds hardware capacity");
		return -EOPNOTSUPP;
	}

	members = 0;
	dsa_lag_foreach_port(dp, ds->dst, &lag)
		/* Includes the port joining the LAG */
		members++;

	if (members > YT921X_LAG_PORT_NUM) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Cannot offload more than 4 LAG ports");
		return -EOPNOTSUPP;
	}

	if (info->tx_type != NETDEV_LAG_TX_TYPE_HASH) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Can only offload LAG using hash TX type");
		return -EOPNOTSUPP;
	}

	if (info->hash_type != NETDEV_LAG_HASH_L2 &&
	    info->hash_type != NETDEV_LAG_HASH_L23 &&
	    info->hash_type != NETDEV_LAG_HASH_L34) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Can only offload L2 or L2+L3 or L3+L4 TX hash");
		return -EOPNOTSUPP;
	}

	return 0;
}

static int
yt921x_dsa_port_lag_join(struct dsa_switch *ds, int port, struct dsa_lag lag,
			 struct netdev_lag_upper_info *info,
			 struct netlink_ext_ack *extack)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	struct dsa_port *dp;
	bool unique_lag;
	unsigned int i;
	u32 ctrl;
	int res;

	res = yt921x_dsa_port_lag_check(ds, port, lag, info, extack);
	if (res)
		return res;

	ctrl = 0;
	switch (info->hash_type) {
	case NETDEV_LAG_HASH_L34:
		ctrl |= YT921X_LAG_HASH_IP_DST;
		ctrl |= YT921X_LAG_HASH_IP_SRC;
		ctrl |= YT921X_LAG_HASH_IP_PROTO;

		ctrl |= YT921X_LAG_HASH_L4_DPORT;
		ctrl |= YT921X_LAG_HASH_L4_SPORT;
		break;
	case NETDEV_LAG_HASH_L23:
		ctrl |= YT921X_LAG_HASH_MAC_DA;
		ctrl |= YT921X_LAG_HASH_MAC_SA;

		ctrl |= YT921X_LAG_HASH_IP_DST;
		ctrl |= YT921X_LAG_HASH_IP_SRC;
		ctrl |= YT921X_LAG_HASH_IP_PROTO;
		break;
	case NETDEV_LAG_HASH_L2:
		ctrl |= YT921X_LAG_HASH_MAC_DA;
		ctrl |= YT921X_LAG_HASH_MAC_SA;
		break;
	default:
		return -EOPNOTSUPP;
	}

	/* Check if we are the unique configured LAG */
	unique_lag = true;
	dsa_lags_foreach_id(i, ds->dst)
		if (i != lag.id && dsa_lag_by_id(ds->dst, i)) {
			unique_lag = false;
			break;
		}

	mutex_lock(&priv->reg_lock);
	do {
		res = yt921x_lag_hash(priv, ctrl, unique_lag, extack);
		if (res)
			break;

		ctrl = 0;
		dsa_lag_foreach_port(dp, ds->dst, &lag)
			ctrl |= BIT(dp->index);
		res = yt921x_lag_set(priv, lag.id - 1, ctrl);
	} while (0);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int yt921x_fdb_wait(struct yt921x_priv *priv, u32 *valp)
{
	struct device *dev = yt921x_dev(priv);
	u32 val = YT921X_FDB_RESULT_DONE;
	int res;

	res = yt921x_reg_wait(priv, YT921X_FDB_RESULT, YT921X_FDB_RESULT_DONE,
			      &val);
	if (res) {
		dev_err(dev, "FDB probably stuck\n");
		return res;
	}

	*valp = val;
	return 0;
}

static int
yt921x_fdb_in01(struct yt921x_priv *priv, const unsigned char *addr,
		u16 vid, u32 ctrl1)
{
	u32 ctrl;
	int res;

	ctrl = (addr[0] << 24) | (addr[1] << 16) | (addr[2] << 8) | addr[3];
	res = yt921x_reg_write(priv, YT921X_FDB_IN0, ctrl);
	if (res)
		return res;

	ctrl = ctrl1 | YT921X_FDB_IO1_FID(vid) | (addr[4] << 8) | addr[5];
	return yt921x_reg_write(priv, YT921X_FDB_IN1, ctrl);
}

static int
yt921x_fdb_has(struct yt921x_priv *priv, const unsigned char *addr, u16 vid,
	       u16 *indexp)
{
	u32 ctrl;
	u32 val;
	int res;

	res = yt921x_fdb_in01(priv, addr, vid, 0);
	if (res)
		return res;

	ctrl = 0;
	res = yt921x_reg_write(priv, YT921X_FDB_IN2, ctrl);
	if (res)
		return res;

	ctrl = YT921X_FDB_OP_OP_GET_ONE | YT921X_FDB_OP_START;
	res = yt921x_reg_write(priv, YT921X_FDB_OP, ctrl);
	if (res)
		return res;

	res = yt921x_fdb_wait(priv, &val);
	if (res)
		return res;
	if (val & YT921X_FDB_RESULT_NOTFOUND) {
		*indexp = YT921X_FDB_NUM;
		return 0;
	}

	*indexp = FIELD_GET(YT921X_FDB_RESULT_INDEX_M, val);
	return 0;
}

static int
yt921x_fdb_read(struct yt921x_priv *priv, unsigned char *addr, u16 *vidp,
		u16 *ports_maskp, u16 *indexp, u8 *statusp)
{
	struct device *dev = yt921x_dev(priv);
	u16 index;
	u32 data0;
	u32 data1;
	u32 data2;
	u32 val;
	int res;

	res = yt921x_fdb_wait(priv, &val);
	if (res)
		return res;
	if (val & YT921X_FDB_RESULT_NOTFOUND) {
		*ports_maskp = 0;
		return 0;
	}
	index = FIELD_GET(YT921X_FDB_RESULT_INDEX_M, val);

	res = yt921x_reg_read(priv, YT921X_FDB_OUT1, &data1);
	if (res)
		return res;
	if ((data1 & YT921X_FDB_IO1_STATUS_M) ==
	    YT921X_FDB_IO1_STATUS_INVALID) {
		*ports_maskp = 0;
		return 0;
	}

	res = yt921x_reg_read(priv, YT921X_FDB_OUT0, &data0);
	if (res)
		return res;
	res = yt921x_reg_read(priv, YT921X_FDB_OUT2, &data2);
	if (res)
		return res;

	addr[0] = data0 >> 24;
	addr[1] = data0 >> 16;
	addr[2] = data0 >> 8;
	addr[3] = data0;
	addr[4] = data1 >> 8;
	addr[5] = data1;
	*vidp = FIELD_GET(YT921X_FDB_IO1_FID_M, data1);
	*indexp = index;
	*ports_maskp = FIELD_GET(YT921X_FDB_IO2_EGR_PORTS_M, data2);
	*statusp = FIELD_GET(YT921X_FDB_IO1_STATUS_M, data1);

	dev_dbg(dev,
		"%s: index 0x%x, mac %02x:%02x:%02x:%02x:%02x:%02x, vid %d, ports 0x%x, status %d\n",
		__func__, *indexp, addr[0], addr[1], addr[2], addr[3],
		addr[4], addr[5], *vidp, *ports_maskp, *statusp);
	return 0;
}

static int
yt921x_fdb_dump(struct yt921x_priv *priv, u16 ports_mask,
		dsa_fdb_dump_cb_t *cb, void *data)
{
	struct device *dev = yt921x_dev(priv);
	unsigned char addr[ETH_ALEN];
	u8 status;
	u16 pmask;
	u16 index;
	u32 ctrl;
	u16 vid;
	unsigned int iter = 0;
	int res;

	ctrl = YT921X_FDB_OP_INDEX(0) | YT921X_FDB_OP_MODE_INDEX |
	       YT921X_FDB_OP_OP_GET_ONE | YT921X_FDB_OP_START;
	res = yt921x_reg_write(priv, YT921X_FDB_OP, ctrl);
	if (res)
		return res;
	res = yt921x_fdb_read(priv, addr, &vid, &pmask, &index, &status);
	if (res)
		return res;
	if ((pmask & ports_mask) && !is_multicast_ether_addr(addr)) {
		res = cb(addr, vid,
			 status == YT921X_FDB_ENTRY_STATUS_STATIC, data);
		if (res)
			return res;
	}

	ctrl = YT921X_FDB_IO2_EGR_PORTS(ports_mask);
	res = yt921x_reg_write(priv, YT921X_FDB_IN2, ctrl);
	if (res)
		return res;

	index = 0;
	do {
		u16 prev = index;

		ctrl = YT921X_FDB_OP_INDEX(index) | YT921X_FDB_OP_MODE_INDEX |
		       YT921X_FDB_OP_NEXT_TYPE_UCAST_PORT |
		       YT921X_FDB_OP_OP_GET_NEXT | YT921X_FDB_OP_START;
		res = yt921x_reg_write(priv, YT921X_FDB_OP, ctrl);
		if (res)
			return res;

		res = yt921x_fdb_read(priv, addr, &vid, &pmask, &index,
				      &status);
		if (res)
			return res;
		if (!pmask)
			break;

		if ((pmask & ports_mask) && !is_multicast_ether_addr(addr)) {
			res = cb(addr, vid,
				 status == YT921X_FDB_ENTRY_STATUS_STATIC,
				 data);
			if (res)
				return res;
		}
		if (index <= prev) {
			dev_err(dev, "FDB get-next stalled at index %u\n", index);
			return -EIO;
		}
		if (++iter >= YT921X_FDB_NUM) {
			dev_err(dev, "FDB get-next overflow\n");
			return -EIO;
		}

		/* Never call GET_NEXT with 4095, otherwise it will hang
		 * forever until a reset!
		 */
	} while (index < YT921X_FDB_NUM - 1);

	return 0;
}

static int
yt921x_fdb_flush_raw(struct yt921x_priv *priv, u16 ports_mask, u16 vid,
		     bool flush_static)
{
	u32 ctrl;
	u32 val;
	int res;

	if (vid < 4096) {
		ctrl = YT921X_FDB_IO1_FID(vid);
		res = yt921x_reg_write(priv, YT921X_FDB_IN1, ctrl);
		if (res)
			return res;
	}

	ctrl = YT921X_FDB_IO2_EGR_PORTS(ports_mask);
	res = yt921x_reg_write(priv, YT921X_FDB_IN2, ctrl);
	if (res)
		return res;

	ctrl = YT921X_FDB_OP_OP_FLUSH | YT921X_FDB_OP_START;
	if (vid >= 4096)
		ctrl |= YT921X_FDB_OP_FLUSH_PORT;
	else
		ctrl |= YT921X_FDB_OP_FLUSH_PORT_VID;
	if (flush_static)
		ctrl |= YT921X_FDB_OP_FLUSH_STATIC;
	res = yt921x_reg_write(priv, YT921X_FDB_OP, ctrl);
	if (res)
		return res;

	res = yt921x_fdb_wait(priv, &val);
	if (res)
		return res;

	return 0;
}

static int
yt921x_fdb_flush_port(struct yt921x_priv *priv, int port, bool flush_static)
{
	return yt921x_fdb_flush_raw(priv, BIT(port), 4096, flush_static);
}

static int
yt921x_fdb_add_index_in12(struct yt921x_priv *priv, u16 index, u16 ctrl1,
			  u16 ctrl2)
{
	u32 ctrl;
	u32 val;
	int res;

	res = yt921x_reg_write(priv, YT921X_FDB_IN1, ctrl1);
	if (res)
		return res;
	res = yt921x_reg_write(priv, YT921X_FDB_IN2, ctrl2);
	if (res)
		return res;

	ctrl = YT921X_FDB_OP_INDEX(index) | YT921X_FDB_OP_MODE_INDEX |
	       YT921X_FDB_OP_OP_ADD | YT921X_FDB_OP_START;
	res = yt921x_reg_write(priv, YT921X_FDB_OP, ctrl);
	if (res)
		return res;

	return yt921x_fdb_wait(priv, &val);
}

static int
yt921x_fdb_add(struct yt921x_priv *priv, const unsigned char *addr, u16 vid,
	       u16 ports_mask)
{
	u32 ctrl;
	u32 val;
	int res;

	ctrl = YT921X_FDB_IO1_STATUS_STATIC;
	res = yt921x_fdb_in01(priv, addr, vid, ctrl);
	if (res)
		return res;

	ctrl = YT921X_FDB_IO2_EGR_PORTS(ports_mask);
	res = yt921x_reg_write(priv, YT921X_FDB_IN2, ctrl);
	if (res)
		return res;

	ctrl = YT921X_FDB_OP_OP_ADD | YT921X_FDB_OP_START;
	res = yt921x_reg_write(priv, YT921X_FDB_OP, ctrl);
	if (res)
		return res;

	return yt921x_fdb_wait(priv, &val);
}

static int
yt921x_fdb_leave(struct yt921x_priv *priv, const unsigned char *addr,
		 u16 vid, u16 ports_mask)
{
	u16 index;
	u32 ctrl1;
	u32 ctrl2;
	u32 ctrl;
	u32 val2;
	u32 val;
	int res;

	/* Check for presence */
	res = yt921x_fdb_has(priv, addr, vid, &index);
	if (res)
		return res;
	if (index >= YT921X_FDB_NUM)
		return 0;

	/* Check if action required */
	res = yt921x_reg_read(priv, YT921X_FDB_OUT2, &val2);
	if (res)
		return res;

	ctrl2 = val2 & ~YT921X_FDB_IO2_EGR_PORTS(ports_mask);
	if (ctrl2 == val2)
		return 0;
	if (!(ctrl2 & YT921X_FDB_IO2_EGR_PORTS_M)) {
		ctrl = YT921X_FDB_OP_OP_DEL | YT921X_FDB_OP_START;
		res = yt921x_reg_write(priv, YT921X_FDB_OP, ctrl);
		if (res)
			return res;

		return yt921x_fdb_wait(priv, &val);
	}

	res = yt921x_reg_read(priv, YT921X_FDB_OUT1, &ctrl1);
	if (res)
		return res;

	return yt921x_fdb_add_index_in12(priv, index, ctrl1, ctrl2);
}

static int
yt921x_fdb_join(struct yt921x_priv *priv, const unsigned char *addr, u16 vid,
		u16 ports_mask)
{
	u16 index;
	u32 ctrl1;
	u32 ctrl2;
	u32 val1;
	u32 val2;
	int res;

	/* Check for presence */
	res = yt921x_fdb_has(priv, addr, vid, &index);
	if (res)
		return res;
	if (index >= YT921X_FDB_NUM)
		return yt921x_fdb_add(priv, addr, vid, ports_mask);

	/* Check if action required */
	res = yt921x_reg_read(priv, YT921X_FDB_OUT1, &val1);
	if (res)
		return res;
	res = yt921x_reg_read(priv, YT921X_FDB_OUT2, &val2);
	if (res)
		return res;

	ctrl1 = val1 & ~YT921X_FDB_IO1_STATUS_M;
	ctrl1 |= YT921X_FDB_IO1_STATUS_STATIC;
	ctrl2 = val2 | YT921X_FDB_IO2_EGR_PORTS(ports_mask);
	if (ctrl1 == val1 && ctrl2 == val2)
		return 0;

	return yt921x_fdb_add_index_in12(priv, index, ctrl1, ctrl2);
}

static int
yt921x_dsa_port_fdb_dump(struct dsa_switch *ds, int port,
			 dsa_fdb_dump_cb_t *cb, void *data)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	int res;

	mutex_lock(&priv->reg_lock);
	/* Hardware FDB is shared for fdb and mdb, "bridge fdb show"
	 * only wants to see unicast
	 */
	res = yt921x_fdb_dump(priv, BIT(port), cb, data);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static void yt921x_dsa_port_fast_age(struct dsa_switch *ds, int port)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	struct device *dev = yt921x_dev(priv);
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_fdb_flush_port(priv, port, false);
	mutex_unlock(&priv->reg_lock);

	if (res)
		dev_err(dev, "Failed to %s port %d: %i\n", "clear FDB for",
			port, res);
}

static int
yt921x_dsa_set_ageing_time(struct dsa_switch *ds, unsigned int msecs)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	u32 ctrl;
	int res;

	/* AGEING reg is set in 5s step */
	ctrl = clamp(msecs / 5000, 1, U16_MAX);

	mutex_lock(&priv->reg_lock);
	res = yt921x_reg_write(priv, YT921X_AGEING, ctrl);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int
yt921x_dsa_port_fdb_del(struct dsa_switch *ds, int port,
			const unsigned char *addr, u16 vid, struct dsa_db db)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_fdb_leave(priv, addr, vid, BIT(port));
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int
yt921x_dsa_port_fdb_add(struct dsa_switch *ds, int port,
			const unsigned char *addr, u16 vid, struct dsa_db db)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_fdb_join(priv, addr, vid, BIT(port));
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int
yt921x_dsa_port_mdb_del(struct dsa_switch *ds, int port,
			const struct switchdev_obj_port_mdb *mdb,
			struct dsa_db db)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	struct device *dev = yt921x_dev(priv);
	const unsigned char *addr = mdb->addr;
	u16 vid = mdb->vid;
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_fdb_leave(priv, addr, vid, BIT(port));
	mutex_unlock(&priv->reg_lock);

	dev_dbg(dev, "mdb del: grp=%pM vid=%u port=%d res=%d\n",
		addr, vid, port, res);

	return res;
}

static int
yt921x_dsa_port_mdb_add(struct dsa_switch *ds, int port,
			const struct switchdev_obj_port_mdb *mdb,
			struct dsa_db db)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	struct device *dev = yt921x_dev(priv);
	const unsigned char *addr = mdb->addr;
	u16 vid = mdb->vid;
	int res;

	/* Bridge core rejects static MDB programming when multicast snooping
	 * is disabled; userspace must enable bridge multicast_snooping first.
	 */
	mutex_lock(&priv->reg_lock);
	res = yt921x_fdb_join(priv, addr, vid, BIT(port));
	mutex_unlock(&priv->reg_lock);

	dev_dbg(dev, "mdb add: grp=%pM vid=%u port=%d res=%d\n",
		addr, vid, port, res);

	return res;
}

static int
yt921x_vlan_aware_set(struct yt921x_priv *priv, int port, bool vlan_aware)
{
	u32 ctrl;

	/* Abuse SVLAN for PCP parsing without polluting the FDB - it just works
	 * despite YT921X_VLAN_CTRL_SVLAN_EN never being set
	 */
	if (!vlan_aware)
		ctrl = YT921X_PORT_IGR_TPIDn_STAG(0);
	else
		ctrl = YT921X_PORT_IGR_TPIDn_CTAG(0);
	return yt921x_reg_write(priv, YT921X_PORTn_IGR_TPID(port), ctrl);
}

static int
yt921x_port_set_pvid(struct yt921x_priv *priv, int port, u16 vid)
{
	u32 mask;
	u32 ctrl;

	mask = YT921X_PORT_VLAN_CTRL_CVID_M;
	ctrl = YT921X_PORT_VLAN_CTRL_CVID(vid);
	return yt921x_reg_update_bits(priv, YT921X_PORTn_VLAN_CTRL(port),
				      mask, ctrl);
}

static int
yt921x_vlan_filtering(struct yt921x_priv *priv, int port, bool vlan_filtering)
{
	struct dsa_port *dp = dsa_to_port(&priv->ds, port);
	struct net_device *bdev;
	u16 pvid = 0;
	u32 mask;
	u32 ctrl;
	int res;

	bdev = dsa_port_bridge_dev_get(dp);

	if (!bdev || !vlan_filtering)
		pvid = YT921X_VID_UNAWARE;
	else if (br_vlan_get_pvid(bdev, &pvid))
		pvid = 0;
	res = yt921x_port_set_pvid(priv, port, pvid);
	if (res)
		return res;

	mask = YT921X_PORT_VLAN_CTRL1_CVLAN_DROP_TAGGED |
	       YT921X_PORT_VLAN_CTRL1_CVLAN_DROP_UNTAGGED;
	ctrl = 0;
	/* Do not drop tagged frames here; let VLAN_IGR_FILTER do it */
	if (vlan_filtering && !pvid)
		ctrl |= YT921X_PORT_VLAN_CTRL1_CVLAN_DROP_UNTAGGED;
	res = yt921x_reg_update_bits(priv, YT921X_PORTn_VLAN_CTRL1(port),
				     mask, ctrl);
	if (res)
		return res;

	res = yt921x_reg_toggle_bits(priv, YT921X_VLAN_IGR_FILTER,
				     YT921X_VLAN_IGR_FILTER_PORTn(port),
				     vlan_filtering);
	if (res)
		return res;

	res = yt921x_vlan_aware_set(priv, port, vlan_filtering);
	if (res)
		return res;

	return 0;
}

static int
yt921x_vlan_del(struct yt921x_priv *priv, int port, u16 vid)
{
	u64 ctrl64;
	int res;

	res = yt921x_reg64_read(priv, YT921X_VLANn_CTRL(vid), &ctrl64);
	if (res)
		return res;

	ctrl64 &= ~YT921X_VLAN_CTRL_PORTn(port);
	ctrl64 &= ~YT921X_VLAN_CTRL_UNTAG_PORTn(port);

	/* Keep VID=FID mapping tidy when the last member leaves VLAN X.
	 * Keep VID 0 untouched for priority-tagged traffic semantics.
	 */
	if (vid && !(ctrl64 & YT921X_VLAN_CTRL_PORTS_M))
		ctrl64 &= ~YT921X_VLAN_CTRL_FID_M;

	return yt921x_reg64_write(priv, YT921X_VLANn_CTRL(vid), ctrl64);
}

static int
yt921x_vlan_add(struct yt921x_priv *priv, int port, u16 vid, bool untagged)
{
	u64 mask64;
	u64 ctrl64;

	mask64 = YT921X_VLAN_CTRL_PORTn(port);
	ctrl64 = mask64;

	mask64 |= YT921X_VLAN_CTRL_UNTAG_PORTn(port);
	if (untagged)
		ctrl64 |= YT921X_VLAN_CTRL_UNTAG_PORTn(port);

	/* Program VID=FID for IVL behavior expected by vlan_filtering
	 * bridges. Leave VID 0 unchanged.
	 */
	if (vid) {
		mask64 |= YT921X_VLAN_CTRL_FID_M;
		ctrl64 |= YT921X_VLAN_CTRL_FID(vid);
	}

	return yt921x_reg64_update_bits(priv, YT921X_VLANn_CTRL(vid),
					mask64, ctrl64);
}

static int
yt921x_pvid_clear(struct yt921x_priv *priv, int port)
{
	struct dsa_port *dp = dsa_to_port(&priv->ds, port);
	bool vlan_filtering;
	u32 mask;
	int res;

	vlan_filtering = dsa_port_is_vlan_filtering(dp);

	res = yt921x_port_set_pvid(priv, port,
				   vlan_filtering ? 0 : YT921X_VID_UNAWARE);
	if (res)
		return res;

	if (vlan_filtering) {
		mask = YT921X_PORT_VLAN_CTRL1_CVLAN_DROP_UNTAGGED;
		res = yt921x_reg_set_bits(priv, YT921X_PORTn_VLAN_CTRL1(port),
					  mask);
		if (res)
			return res;
	}

	return 0;
}

static int
yt921x_pvid_set(struct yt921x_priv *priv, int port, u16 vid)
{
	struct dsa_port *dp = dsa_to_port(&priv->ds, port);
	bool vlan_filtering;
	u32 mask;
	int res;

	vlan_filtering = dsa_port_is_vlan_filtering(dp);

	if (vlan_filtering) {
		res = yt921x_port_set_pvid(priv, port, vid);
		if (res)
			return res;
	}

	mask = YT921X_PORT_VLAN_CTRL1_CVLAN_DROP_UNTAGGED;
	res = yt921x_reg_clear_bits(priv, YT921X_PORTn_VLAN_CTRL1(port), mask);
	if (res)
		return res;

	return 0;
}

static int
yt921x_dsa_port_vlan_filtering(struct dsa_switch *ds, int port,
			       bool vlan_filtering,
			       struct netlink_ext_ack *extack)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	int res;

	if (dsa_is_cpu_port(ds, port))
		return 0;

	mutex_lock(&priv->reg_lock);
	do {
		res = yt921x_vlan_filtering(priv, port, vlan_filtering);
		if (res)
			break;

		/* Keep VLAN/FID transition clean: drop stale dynamic entries
		 * learned under the previous filtering mode.
		 */
		res = yt921x_fdb_flush_port(priv, port, false);
	} while (0);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int
yt921x_dsa_port_vlan_del(struct dsa_switch *ds, int port,
			 const struct switchdev_obj_port_vlan *vlan)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	u16 vid = vlan->vid;
	u16 pvid = 0;
	int res;

	mutex_lock(&priv->reg_lock);
	do {
		res = yt921x_vlan_del(priv, port, vid);
		if (res)
			break;

		if (dsa_is_user_port(ds, port)) {
			struct dsa_port *dp = dsa_to_port(ds, port);
			struct net_device *bdev;

			bdev = dsa_port_bridge_dev_get(dp);
			if (bdev) {
				if (!br_vlan_get_pvid(bdev, &pvid) &&
				    pvid == vid)
					res = yt921x_pvid_clear(priv, port);
			}
		}
	} while (0);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int
yt921x_dsa_port_vlan_add(struct dsa_switch *ds, int port,
			 const struct switchdev_obj_port_vlan *vlan,
			 struct netlink_ext_ack *extack)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	u16 vid = vlan->vid;
	u16 pvid = 0;
	int res;

	mutex_lock(&priv->reg_lock);
	do {
		res = yt921x_vlan_add(priv, port, vid,
				      vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED);
		if (res)
			break;

		if (dsa_is_user_port(ds, port)) {
			struct dsa_port *dp = dsa_to_port(ds, port);
			struct net_device *bdev;

			bdev = dsa_port_bridge_dev_get(dp);
			if (bdev) {
				if (vlan->flags & BRIDGE_VLAN_INFO_PVID) {
					res = yt921x_pvid_set(priv, port, vid);
				} else {
					if (!br_vlan_get_pvid(bdev, &pvid) &&
					    pvid == vid)
						res = yt921x_pvid_clear(priv, port);
				}
			}
		}
	} while (0);
	mutex_unlock(&priv->reg_lock);

	return res;
}


static u32 yt921x_non_cpu_port_mask(const struct yt921x_priv *priv)
{
	return GENMASK(YT921X_PORT_NUM - 1, 0) & ~priv->cpu_ports_mask;
}

static int
yt921x_port_isolation_set(struct yt921x_priv *priv, int port, u32 blocked_mask)
{
	u32 mask;

	mask = yt921x_non_cpu_port_mask(priv);

	return yt921x_reg_update_bits(priv, YT921X_PORTn_ISOLATION(port),
				      mask, blocked_mask & mask);
}

static int
yt921x_userport_cpu_isolation_set(struct yt921x_priv *priv, int port, int cpu_port)
{
	u32 mask = priv->cpu_ports_mask;
	u32 blocked_mask;

	blocked_mask = mask & ~BIT(cpu_port);

	return yt921x_reg_update_bits(priv, YT921X_PORTn_ISOLATION(port),
				      mask, blocked_mask);
}

static int
yt921x_userport_current_cpu_port_get(struct yt921x_priv *priv, int port, int *cpu_port)
{
	u32 allowed_mask;
	u32 val;
	int res;

	res = yt921x_reg_read(priv, YT921X_PORTn_ISOLATION(port), &val);
	if (res)
		return res;

	allowed_mask = priv->cpu_ports_mask & ~val;
	if (hweight16(allowed_mask) != 1)
		return -ENOENT;

	*cpu_port = __ffs(allowed_mask);

	return 0;
}

static int yt921x_secondary_cpu_isolation_sync(struct yt921x_priv *priv,
					       int cpu_port)
{
	struct dsa_switch *ds = &priv->ds;
	struct dsa_port *dp;
	u32 blocked_mask;
	int upstream_port;

	if (!dsa_is_cpu_port(ds, cpu_port) ||
	    yt921x_is_primary_cpu_port(priv, cpu_port))
		return 0;

	blocked_mask = yt921x_non_cpu_port_mask(priv);
	dsa_switch_for_each_user_port(dp, ds) {
		if (!yt921x_userport_current_cpu_port_get(priv, dp->index,
							  &upstream_port)) {
			if (upstream_port == cpu_port)
				blocked_mask &= ~BIT(dp->index);
			continue;
		}

		if (dsa_upstream_port(ds, dp->index) == cpu_port)
			blocked_mask &= ~BIT(dp->index);
	}

	return yt921x_port_isolation_set(priv, cpu_port, blocked_mask);
}

static int
yt921x_dsa_conduit_to_cpu_port(struct dsa_switch *ds, struct net_device *conduit,
			       struct netlink_ext_ack *extack)
{
	struct dsa_port *cpu_dp;

	if (!conduit) {
		NL_SET_ERR_MSG_MOD(extack, "Missing conduit device");
		return -EINVAL;
	}

	if (netif_is_lag_master(conduit)) {
		NL_SET_ERR_MSG_MOD(extack, "CPU LAG conduit is not supported");
		return -EOPNOTSUPP;
	}

	cpu_dp = conduit->dsa_ptr;
	if (!cpu_dp || !dsa_port_is_cpu(cpu_dp) || cpu_dp->ds != ds) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Conduit does not map to a local CPU port");
		return -EINVAL;
	}

	return cpu_dp->index;
}

static int
yt921x_conduit_fdb_retarget(struct yt921x_priv *priv, int user_port,
			    int old_cpu_port, int new_cpu_port)
{
	int res;

	if (old_cpu_port == new_cpu_port)
		return 0;

	/* Conduit switch can leave stale dynamic CPU-directed entries.
	 * Keep static entries intact to avoid disrupting unrelated user ports.
	 */
	res = yt921x_fdb_flush_port(priv, user_port, false);
	if (res)
		return res;

	res = yt921x_fdb_flush_port(priv, old_cpu_port, false);
	if (res)
		return res;

	return yt921x_fdb_flush_port(priv, new_cpu_port, false);
}

static u32
yt921x_bridge_block_mask(struct yt921x_priv *priv, int port, u16 ports_mask,
			 u32 isolated_mask)
{
	struct yt921x_port *pp = &priv->ports[port];
	u32 blocked_mask;

	/* PORTn_ISOLATION keeps unrelated upper bits. Only the non-CPU
	 * destination field was proven by live register tests.
	 */
	blocked_mask = yt921x_non_cpu_port_mask(priv) & ~ports_mask;

	if (pp->isolated)
		blocked_mask |= isolated_mask;

	if (!pp->hairpin)
		blocked_mask |= BIT(port);
	else
		blocked_mask &= ~BIT(port);

	return blocked_mask;
}

static int yt921x_userport_standalone(struct yt921x_priv *priv, int port)
{
	u32 mask;
	u32 blocked_mask;
	int res;

	blocked_mask = yt921x_non_cpu_port_mask(priv);
	res = yt921x_port_isolation_set(priv, port, blocked_mask);
	if (res)
		return res;

	/* Turn off FDB learning to prevent FDB pollution */
	mask = YT921X_PORT_LEARN_DIS;
	res = yt921x_reg_set_bits(priv, YT921X_PORTn_LEARN(port), mask);
	if (res)
		return res;

	/* Turn off VLAN awareness */
	res = yt921x_vlan_aware_set(priv, port, false);
	if (res)
		return res;

	/* Unrelated since learning is off and all packets are trapped;
	 * set it anyway
	 */
	res = yt921x_port_set_pvid(priv, port, YT921X_VID_UNAWARE);
	if (res)
		return res;

	return 0;
}

static int yt921x_userport_bridge(struct yt921x_priv *priv, int port)
{
	u32 mask;
	int res;

	mask = YT921X_PORT_LEARN_DIS;
	res = yt921x_reg_clear_bits(priv, YT921X_PORTn_LEARN(port), mask);
	if (res)
		return res;

	return 0;
}

static int yt921x_isolate(struct yt921x_priv *priv, int port)
{
	u32 mask;
	int res;

	mask = BIT(port);
	for (int i = 0; i < YT921X_PORT_NUM; i++) {
		if ((BIT(i) & priv->cpu_ports_mask) || i == port)
			continue;

		res = yt921x_reg_set_bits(priv, YT921X_PORTn_ISOLATION(i),
					  mask);
		if (res)
			return res;
	}

	return 0;
}

/* Make sure to include the CPU port in ports_mask, or your bridge will
 * not have it.
 */
static int yt921x_bridge(struct yt921x_priv *priv, u16 ports_mask)
{
	unsigned long targets_mask = ports_mask & ~priv->cpu_ports_mask;
	u32 isolated_mask;
	u32 blocked_mask;
	int port;
	int res;

	isolated_mask = 0;
	for_each_set_bit(port, &targets_mask, YT921X_PORT_NUM) {
		struct yt921x_port *pp = &priv->ports[port];

		if (pp->isolated)
			isolated_mask |= BIT(port);
	}

	/* Block from non-cpu bridge ports ... */
	for_each_set_bit(port, &targets_mask, YT921X_PORT_NUM) {
		blocked_mask = yt921x_bridge_block_mask(priv, port, ports_mask,
							isolated_mask);

		res = yt921x_port_isolation_set(priv, port, blocked_mask);
		if (res)
			return res;
	}

	return 0;
}

static int yt921x_bridge_leave(struct yt921x_priv *priv, int port)
{
	int res;

	res = yt921x_userport_standalone(priv, port);
	if (res)
		return res;

	res = yt921x_isolate(priv, port);
	if (res)
		return res;

	return 0;
}

static int
yt921x_bridge_join(struct yt921x_priv *priv, int port, u16 ports_mask)
{
	int res;

	res = yt921x_userport_bridge(priv, port);
	if (res)
		return res;

	res = yt921x_bridge(priv, ports_mask);
	if (res)
		return res;

	return 0;
}

static u32
yt921x_dsa_bridge_ports(struct dsa_switch *ds, const struct net_device *bdev)
{
	struct dsa_port *dp;
	u32 mask = 0;

	dsa_switch_for_each_user_port(dp, ds)
		if (dsa_port_offloads_bridge_dev(dp, bdev))
			mask |= BIT(dp->index);

	return mask;
}

static int
yt921x_bridge_flags(struct yt921x_priv *priv, int port,
		    struct switchdev_brport_flags flags)
{
	struct yt921x_port *pp = &priv->ports[port];
	bool refresh_flood;
	bool refresh_mcast_policy;
	bool do_flush;
	u32 mask;
	int res;

	if (flags.mask & BR_LEARNING) {
		bool learning = flags.val & BR_LEARNING;

		mask = YT921X_PORT_LEARN_DIS;
		res = yt921x_reg_toggle_bits(priv, YT921X_PORTn_LEARN(port),
					     mask, !learning);
		if (res)
			return res;
	}

	/* BR_FLOOD (unknown UC) remains global via ACT_UNK_* policy. */
	refresh_flood = false;
	refresh_mcast_policy = false;
	if (flags.mask & BR_MCAST_FLOOD) {
		pp->mcast_flood = flags.val & BR_MCAST_FLOOD;
		refresh_flood = true;
	}
	if (flags.mask & BR_BCAST_FLOOD) {
		pp->bcast_flood = flags.val & BR_BCAST_FLOOD;
		refresh_flood = true;
	}
	if (flags.mask & BR_MULTICAST_FAST_LEAVE) {
		pp->mcast_fast_leave = flags.val & BR_MULTICAST_FAST_LEAVE;
		refresh_mcast_policy = true;
	}

	do_flush = false;
	if (flags.mask & BR_HAIRPIN_MODE) {
		pp->hairpin = flags.val & BR_HAIRPIN_MODE;
		do_flush = true;
	}
	if (flags.mask & BR_ISOLATED) {
		pp->isolated = flags.val & BR_ISOLATED;
		do_flush = true;
	}
	if (do_flush) {
		struct dsa_switch *ds = &priv->ds;
		struct dsa_port *dp = dsa_to_port(ds, port);
		struct net_device *bdev;

		bdev = dsa_port_bridge_dev_get(dp);
		if (bdev) {
			u32 ports_mask;

			ports_mask = yt921x_dsa_bridge_ports(ds, bdev);
			ports_mask |= priv->cpu_ports_mask;
			res = yt921x_bridge(priv, ports_mask);
			if (res)
				return res;
		}
	}

	if (refresh_flood) {
		res = yt921x_refresh_flood_masks_locked(priv);
		if (res)
			return res;
	}

	if (refresh_mcast_policy) {
		struct dsa_switch *ds = &priv->ds;
		struct dsa_port *dp;
		u32 fast_leave = 0;

		dsa_switch_for_each_user_port(dp, ds) {
			if (priv->ports[dp->index].mcast_fast_leave) {
				fast_leave = YT921X_MCAST_FWD_POLICY_FAST_LEAVE;
				break;
			}
		}

		res = yt921x_reg_update_bits(priv, YT921X_MCAST_FWD_POLICY,
					     YT921X_MCAST_FWD_POLICY_FAST_LEAVE,
					     fast_leave);
		if (res)
			return res;
	}

	return 0;
}

static int
yt921x_dsa_port_pre_bridge_flags(struct dsa_switch *ds, int port,
				 struct switchdev_brport_flags flags,
				 struct netlink_ext_ack *extack)
{
	if (flags.mask & ~(BR_HAIRPIN_MODE | BR_LEARNING | BR_FLOOD |
			   BR_MCAST_FLOOD | BR_BCAST_FLOOD |
			   BR_MULTICAST_FAST_LEAVE | BR_ISOLATED))
		return -EINVAL;
	return 0;
}

static int
yt921x_dsa_port_bridge_flags(struct dsa_switch *ds, int port,
			     struct switchdev_brport_flags flags,
			     struct netlink_ext_ack *extack)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	int res;

	if (dsa_is_cpu_port(ds, port))
		return 0;

	mutex_lock(&priv->reg_lock);
	res = yt921x_bridge_flags(priv, port, flags);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static void
yt921x_dsa_port_bridge_leave(struct dsa_switch *ds, int port,
			     struct dsa_bridge bridge)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	struct device *dev = yt921x_dev(priv);
	int res;

	if (dsa_is_cpu_port(ds, port))
		return;

	mutex_lock(&priv->reg_lock);
	res = yt921x_bridge_leave(priv, port);
	if (!res)
		res = yt921x_refresh_flood_masks_locked(priv);
	mutex_unlock(&priv->reg_lock);

	if (res)
		dev_err(dev, "Failed to %s port %d: %i\n", "unbridge",
			port, res);
}

static int
yt921x_dsa_port_bridge_join(struct dsa_switch *ds, int port,
			    struct dsa_bridge bridge, bool *tx_fwd_offload,
			    struct netlink_ext_ack *extack)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	u16 ports_mask;
	int res;

	if (dsa_is_cpu_port(ds, port))
		return 0;

	ports_mask = yt921x_dsa_bridge_ports(ds, bridge.dev);
	ports_mask |= priv->cpu_ports_mask;

	mutex_lock(&priv->reg_lock);
	res = yt921x_bridge_join(priv, port, ports_mask);
	if (!res)
		res = yt921x_refresh_flood_masks_locked(priv);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int
yt921x_mdio_polling_set(struct yt921x_priv *priv, int port, bool link,
			int speed, int duplex)
{
	u32 ctrl = 0;

	if (!yt921x_is_external_port(priv, port))
		return 0;

	if (!link)
		return yt921x_reg_write(priv, YT921X_MDIO_POLLINGn(port), 0);

	switch (speed) {
	case SPEED_10:
		ctrl = YT921X_MDIO_POLLING_SPEED_10;
		break;
	case SPEED_100:
		ctrl = YT921X_MDIO_POLLING_SPEED_100;
		break;
	case SPEED_1000:
		ctrl = YT921X_MDIO_POLLING_SPEED_1000;
		break;
	case SPEED_2500:
		ctrl = YT921X_MDIO_POLLING_SPEED_2500;
		break;
	case SPEED_10000:
		ctrl = YT921X_MDIO_POLLING_SPEED_10000;
		break;
	default:
		return -EINVAL;
	}

	if (duplex == DUPLEX_FULL)
		ctrl |= YT921X_MDIO_POLLING_DUPLEX_FULL;
	ctrl |= YT921X_MDIO_POLLING_LINK;

	return yt921x_reg_write(priv, YT921X_MDIO_POLLINGn(port), ctrl);
}

/* Optional per-port board override for PORTn_CTRL.
 * - motorcomm,port-ctrl-mask: bits to update
 * - motorcomm,port-ctrl-value: replacement value for masked bits
 */
static int
yt921x_port_ctrl_apply_dt(struct yt921x_priv *priv, int port, bool allow_managed)
{
	struct device *dev = yt921x_dev(priv);
	struct dsa_port *dp = dsa_to_port(&priv->ds, port);
	u32 mask = 0;
	u32 val = 0;
	int res;

	if (!dp || !dp->dn)
		return 0;

	if (of_property_read_u32(dp->dn, "motorcomm,port-ctrl-mask", &mask))
		return 0;

	of_property_read_u32(dp->dn, "motorcomm,port-ctrl-value", &val);

	if (!allow_managed)
		mask &= ~YT921X_PORT_CTRL_DYNAMIC_M;

	if (!mask)
		return 0;

	res = yt921x_reg_update_bits(priv, YT921X_PORTn_CTRL(port), mask, val);
	if (res) {
		dev_err(dev, "Failed to apply port-ctrl override on port %d: %d\n",
			port, res);
		return res;
	}

	return 0;
}

static int
yt921x_stp_encode_state(int port, u8 state, u32 *ctrl)
{
	switch (state) {
	case BR_STATE_DISABLED:
		*ctrl = YT921X_STP_PORTn_DISABLED(port);
		break;
	case BR_STATE_BLOCKING:
	case BR_STATE_LISTENING:
		*ctrl = YT921X_STP_PORTn_BLOCKING(port);
		break;
	case BR_STATE_LEARNING:
		*ctrl = YT921X_STP_PORTn_LEARNING(port);
		break;
	case BR_STATE_FORWARDING:
		*ctrl = YT921X_STP_PORTn_FORWARD(port);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int
yt921x_dsa_port_mst_state_set(struct dsa_switch *ds, int port,
			      const struct switchdev_mst_state *st)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	u32 mask;
	u32 ctrl;
	int res;

	if (st->msti >= YT921X_MSTI_NUM)
		return -EINVAL;

	res = yt921x_stp_encode_state(port, st->state, &ctrl);
	if (res)
		return res;

	mask = YT921X_STP_PORTn_M(port);
	mutex_lock(&priv->reg_lock);
	res = yt921x_reg_update_bits(priv, YT921X_STPn(st->msti), mask, ctrl);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int
yt921x_dsa_vlan_msti_set(struct dsa_switch *ds, struct dsa_bridge bridge,
			 const struct switchdev_vlan_msti *msti)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	u64 mask64;
	u64 ctrl64;
	int res;

	if (!msti->vid)
		return -EINVAL;
	if (!msti->msti || msti->msti >= YT921X_MSTI_NUM)
		return -EINVAL;

	mask64 = YT921X_VLAN_CTRL_STP_ID_M;
	ctrl64 = YT921X_VLAN_CTRL_STP_ID(msti->msti);

	mutex_lock(&priv->reg_lock);
	res = yt921x_reg64_update_bits(priv, YT921X_VLANn_CTRL(msti->vid),
				       mask64, ctrl64);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static void
yt921x_dsa_port_stp_state_set(struct dsa_switch *ds, int port, u8 state)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	struct dsa_port *dp = dsa_to_port(ds, port);
	struct device *dev = yt921x_dev(priv);
	bool learning;
	u32 mask;
	u32 ctrl;
	int res;

	mask = YT921X_STP_PORTn_M(port);
	res = yt921x_stp_encode_state(port, state, &ctrl);
	if (res) {
		dev_warn(dev, "Ignore unsupported STP state %u on port %d\n",
			 state, port);
		return;
	}

	learning = (state == BR_STATE_LEARNING || state == BR_STATE_FORWARDING) &&
		   dp && dp->learning;

	mutex_lock(&priv->reg_lock);
	do {
		res = yt921x_reg_update_bits(priv, YT921X_STPn(0), mask, ctrl);
		if (res)
			break;

		mask = YT921X_PORT_LEARN_DIS;
		ctrl = !learning ? YT921X_PORT_LEARN_DIS : 0;
		res = yt921x_reg_update_bits(priv, YT921X_PORTn_LEARN(port),
					     mask, ctrl);
	} while (0);
	mutex_unlock(&priv->reg_lock);

	if (res)
		dev_err(dev, "Failed to %s port %d: %i\n", "set STP state for",
			port, res);
}

static int __maybe_unused
yt921x_dsa_port_get_default_prio(struct dsa_switch *ds, int port)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	u32 val;
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_reg_read(priv, YT921X_PORTn_QOS(port), &val);
	mutex_unlock(&priv->reg_lock);

	if (res)
		return res;

	return FIELD_GET(YT921X_PORT_QOS_PRIO_M, val);
}

static int __maybe_unused
yt921x_dsa_port_set_default_prio(struct dsa_switch *ds, int port, u8 prio)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	u32 mask;
	u32 ctrl;
	int res;

	if (prio >= YT921X_PRIO_NUM)
		return -EINVAL;

	mutex_lock(&priv->reg_lock);
	mask = YT921X_PORT_QOS_PRIO_M | YT921X_PORT_QOS_PRIO_EN;
	ctrl = YT921X_PORT_QOS_PRIO(prio) | YT921X_PORT_QOS_PRIO_EN;
	res = yt921x_reg_update_bits(priv, YT921X_PORTn_QOS(port), mask, ctrl);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static int __maybe_unused appprios_cmp(const void *a, const void *b)
{
	return ((const u8 *)b)[1] - ((const u8 *)a)[1];
}

static int __maybe_unused
yt921x_dsa_port_get_apptrust(struct dsa_switch *ds, int port, u8 *sel,
			     int *nselp)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	u8 appprios[2][2] = {};
	int nsel;
	u32 val;
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_reg_read(priv, YT921X_PORTn_PRIO_ORD(port), &val);
	mutex_unlock(&priv->reg_lock);

	if (res)
		return res;

	appprios[0][0] = IEEE_8021QAZ_APP_SEL_DSCP;
	appprios[0][1] = (val >> (3 * YT921X_APP_SEL_DSCP)) & 7;
	appprios[1][0] = DCB_APP_SEL_PCP;
	appprios[1][1] = (val >> (3 * YT921X_APP_SEL_CVLAN_PCP)) & 7;
	sort(appprios, ARRAY_SIZE(appprios), sizeof(appprios[0]), appprios_cmp,
	     NULL);

	nsel = 0;
	for (int i = 0; i < ARRAY_SIZE(appprios) && appprios[i][1]; i++) {
		sel[nsel] = appprios[i][0];
		nsel++;
	}
	*nselp = nsel;

	return 0;
}

static int __maybe_unused
yt921x_dsa_port_set_apptrust(struct dsa_switch *ds, int port, const u8 *sel,
			     int nsel)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	struct device *dev = yt921x_dev(priv);
	u32 ctrl;
	int res;

	if (nsel > YT921X_APP_SEL_NUM)
		return -EINVAL;

	ctrl = 0;
	for (int i = 0; i < nsel; i++) {
		switch (sel[i]) {
		case IEEE_8021QAZ_APP_SEL_DSCP:
			ctrl |= YT921X_PORT_PRIO_ORD_APPm(YT921X_APP_SEL_DSCP,
							  7 - i);
			break;
		case DCB_APP_SEL_PCP:
			ctrl |= YT921X_PORT_PRIO_ORD_APPm(YT921X_APP_SEL_CVLAN_PCP,
							  7 - i);
			ctrl |= YT921X_PORT_PRIO_ORD_APPm(YT921X_APP_SEL_SVLAN_PCP,
							  7 - i);
			break;
		default:
			dev_err(dev,
				"Invalid apptrust selector (at %d-th). Supported: dscp, pcp\n",
				i + 1);
			return -EOPNOTSUPP;
		}
	}

	mutex_lock(&priv->reg_lock);
	res = yt921x_reg_write(priv, YT921X_PORTn_PRIO_ORD(port), ctrl);
	mutex_unlock(&priv->reg_lock);

	return res;
}

static bool yt921x_storm_policer_supported_port(struct dsa_switch *ds, int port)
{
	return dsa_is_user_port(ds, port);
}

static int
yt921x_storm_rate_to_fields(struct yt921x_priv *priv, u64 rate_bytes_per_sec,
			    u32 *rate_f0, u32 *rate_f1)
{
	u64 units_per_sec;
	u32 io;
	u32 slot;
	int res;

	if (!rate_bytes_per_sec)
		return -EINVAL;

	/* Stock code derives storm config fields from this timeslot register.
	 * Keep using the same source and encode as a 29-bit split value
	 * ([31:13] + [12:3]) expected by 0x220200.
	 */
	res = yt921x_reg_read(priv, YT921X_STORM_RATE_IO, &io);
	if (res)
		return res;

	slot = FIELD_GET(YT921X_STORM_RATE_IO_TIMESLOT_M, io);
	if (!slot)
		slot = 1;

	units_per_sec = DIV_ROUND_UP_ULL(rate_bytes_per_sec, slot);
	units_per_sec = clamp_t(u64, units_per_sec, 1, YT921X_STORM_RATE_UNITS_MAX);

	*rate_f0 = (u32)(units_per_sec >> 10);
	*rate_f1 = (u32)(units_per_sec & GENMASK(9, 0));

	return 0;
}

static int yt921x_storm_policer_apply(struct yt921x_priv *priv)
{
	u32 storm_ports = priv->storm_policer_ports & yt921x_non_cpu_port_mask(priv);
	u32 cfg;
	u32 f0;
	u32 f1;
	int res;

	res = yt921x_reg_update_bits(priv, YT921X_STORM_MC_TYPE_CTRL,
				     YT921X_STORM_MC_TYPE_CTRL_PORTS_M,
				     YT921X_STORM_MC_TYPE_CTRL_PORTS(storm_ports));
	if (res)
		return res;

	res = yt921x_reg_read(priv, YT921X_STORM_CONFIG, &cfg);
	if (res)
		return res;

	if (!storm_ports) {
		cfg &= ~YT921X_STORM_CONFIG_EN;
		return yt921x_reg_write(priv, YT921X_STORM_CONFIG, cfg);
	}

	res = yt921x_storm_rate_to_fields(priv, priv->storm_policer_rate_bytes_per_sec,
					  &f0, &f1);
	if (res)
		return res;

	cfg &= ~(YT921X_STORM_CONFIG_RATE_F0_M |
		 YT921X_STORM_CONFIG_RATE_F1_M);
	cfg |= YT921X_STORM_CONFIG_RATE_F0(f0) |
	       YT921X_STORM_CONFIG_RATE_F1(f1) |
	       YT921X_STORM_CONFIG_EN;

	return yt921x_reg_write(priv, YT921X_STORM_CONFIG, cfg);
}

static int
yt921x_stock_ingress_meter_token_level(struct yt921x_priv *priv, u32 *token_level)
{
	u32 chip_id;
	u32 io;
	u32 timeslot;
	u32 token_bytes;
	int res;

	res = yt921x_reg_read(priv, YT921X_RATE_IGR_BW_CTRL, &io);
	if (res)
		return res;

	timeslot = FIELD_GET(YT921X_RATE_IGR_BW_CTRL_TIMESLOT_M, io);
	if (!timeslot)
		timeslot = 1;

	res = yt921x_reg_read(priv, YT921X_CHIP_ID, &chip_id);
	if (res)
		return res;

	token_bytes = (FIELD_GET(YT921X_CHIP_ID_MINOR, chip_id) == 0x9001) ? 7 : 8;
	*token_level = token_bytes * (timeslot << 3);
	if (!*token_level)
		return -ERANGE;

	return 0;
}

static int
yt921x_stock_ingress_rate_to_cir(u64 rate_bytes_per_sec, u32 token_level,
				 u32 meter_f4, bool meter_f6, u32 *cirp)
{
	u64 rate_bits_per_sec;
	u64 scaled;
	u64 cir;
	int shift;

	if (!rate_bytes_per_sec || !token_level)
		return -EINVAL;
	if (rate_bytes_per_sec > U64_MAX / 8)
		return -ERANGE;

	/* Stock helper uses 1e9 scaling in bit/s domain. */
	rate_bits_per_sec = rate_bytes_per_sec * 8;
	if (rate_bits_per_sec > U64_MAX / token_level)
		return -ERANGE;
	scaled = rate_bits_per_sec * token_level;

	shift = (meter_f4 ? 21 : 11) - (meter_f6 ? 2 : 0);
	if (shift > 0) {
		if (scaled > (U64_MAX >> shift))
			return -ERANGE;
		scaled <<= shift;
	} else if (shift < 0) {
		scaled >>= -shift;
	}

	cir = div_u64(scaled, YT921X_RATE_SCALE_PER_SEC);
	if (!cir)
		cir = 1;
	if (cir > YT921X_RATE_CIR_MAX)
		return -ERANGE;

	*cirp = (u32)cir;
	return 0;
}

static int yt921x_ingress_meter_policer_apply(struct yt921x_priv *priv)
{
	u32 policer_ports = priv->storm_policer_ports & yt921x_non_cpu_port_mask(priv);
	unsigned long targets_mask = yt921x_non_cpu_port_mask(priv);
	u32 meter_word1;
	u32 meter_word2;
	u32 token_level;
	u32 cir;
	u32 c8;
	u32 f4;
	bool f6;
	int port;
	int res;

	if (YT921X_RATE_METER_DEFAULT_ID >= YT921X_RATE_METER_NUM)
		return -EINVAL;

	if (!policer_ports)
		goto disable_port_ctrl;

	res = yt921x_stock_ingress_meter_token_level(priv, &token_level);
	if (res)
		return res;

	res = yt921x_reg_read(priv,
			      YT921X_RATE_METER_CONFIG_WORD2(YT921X_RATE_METER_DEFAULT_ID),
			      &meter_word2);
	if (res)
		return res;

	f4 = FIELD_GET(YT921X_RATE_METER_CFG_F4_M, meter_word2);
	f6 = !!(meter_word2 & YT921X_RATE_METER_CFG_F6);

	res = yt921x_stock_ingress_rate_to_cir(priv->storm_policer_rate_bytes_per_sec,
					       token_level, f4, f6, &cir);
	if (res)
		return res;

	res = yt921x_reg_read(priv,
			      YT921X_RATE_METER_CONFIG_WORD1(YT921X_RATE_METER_DEFAULT_ID),
			      &meter_word1);
	if (res)
		return res;

	meter_word1 &= ~YT921X_RATE_METER_CFG_CIR_M;
	meter_word1 |= YT921X_RATE_METER_CFG_CIR(cir);
	res = yt921x_reg_write(priv,
			       YT921X_RATE_METER_CONFIG_WORD1(YT921X_RATE_METER_DEFAULT_ID),
			       meter_word1);
	if (res)
		return res;

disable_port_ctrl:
	for_each_set_bit(port, &targets_mask, YT921X_PORT_NUM) {
		if (!dsa_is_user_port(&priv->ds, port))
			continue;

		res = yt921x_reg_read(priv, YT921X_RATE_IGR_BW_ENABLE + 4 * port, &c8);
		if (res)
			return res;

		c8 &= ~(YT921X_RATE_IGR_BW_ENABLE_EN |
			YT921X_RATE_IGR_BW_ENABLE_METER_ID_M);
		if (policer_ports & BIT(port))
			c8 |= YT921X_RATE_IGR_BW_ENABLE_EN |
			      YT921X_RATE_IGR_BW_ENABLE_METER_ID(
				      YT921X_RATE_METER_DEFAULT_ID);

		res = yt921x_reg_write(priv, YT921X_RATE_IGR_BW_ENABLE + 4 * port, c8);
		if (res)
			return res;
	}

	/* Keep legacy storm-path disabled when ingress-meter path is active. */
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

static int yt921x_dsa_policer_apply(struct yt921x_priv *priv)
{
	int res;

	res = yt921x_ingress_meter_policer_apply(priv);
	if (!res)
		return 0;

	dev_warn(yt921x_dev(priv),
		 "ingress policer apply failed (%d), fallback to storm path\n", res);
	return yt921x_storm_policer_apply(priv);
}

static int
yt921x_dsa_port_policer_add(struct dsa_switch *ds, int port,
			    struct dsa_mall_policer_tc_entry *policer)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	u64 old_rate_bytes_per_sec;
	u16 old_ports;
	u32 old_burst;
	int res = 0;

	if (!yt921x_storm_policer_supported_port(ds, port))
		return -EOPNOTSUPP;
	if (!policer->rate_bytes_per_sec)
		return -EINVAL;

	mutex_lock(&priv->reg_lock);

	old_ports = priv->storm_policer_ports;
	old_rate_bytes_per_sec = priv->storm_policer_rate_bytes_per_sec;
	old_burst = priv->storm_policer_burst;

	if (old_ports &&
	    (old_rate_bytes_per_sec != policer->rate_bytes_per_sec ||
	     old_burst != policer->burst)) {
		res = -EOPNOTSUPP;
		goto out_unlock;
	}

	priv->storm_policer_ports |= BIT(port);
	priv->storm_policer_rate_bytes_per_sec = policer->rate_bytes_per_sec;
	priv->storm_policer_burst = policer->burst;

	res = yt921x_dsa_policer_apply(priv);
	if (res) {
		priv->storm_policer_ports = old_ports;
		priv->storm_policer_rate_bytes_per_sec = old_rate_bytes_per_sec;
		priv->storm_policer_burst = old_burst;
	}

out_unlock:
	mutex_unlock(&priv->reg_lock);
	return res;
}

static void yt921x_dsa_port_policer_del(struct dsa_switch *ds, int port)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	u64 old_rate_bytes_per_sec;
	u16 old_ports;
	u32 old_burst;
	int res;

	if (!yt921x_storm_policer_supported_port(ds, port))
		return;

	mutex_lock(&priv->reg_lock);

	old_ports = priv->storm_policer_ports;
	old_rate_bytes_per_sec = priv->storm_policer_rate_bytes_per_sec;
	old_burst = priv->storm_policer_burst;

	priv->storm_policer_ports &= ~BIT(port);
	if (!priv->storm_policer_ports) {
		priv->storm_policer_rate_bytes_per_sec = 0;
		priv->storm_policer_burst = 0;
	}

	res = yt921x_dsa_policer_apply(priv);
	if (res) {
		priv->storm_policer_ports = old_ports;
		priv->storm_policer_rate_bytes_per_sec = old_rate_bytes_per_sec;
		priv->storm_policer_burst = old_burst;
		dev_warn(yt921x_dev(priv), "policer remove failed on port %d: %d\n",
			 port, res);
	}

	mutex_unlock(&priv->reg_lock);
}

static int yt921x_port_down(struct yt921x_priv *priv, int port)
{
	u32 mask;
	int res;

	mask = YT921X_PORT_CTRL_ADMIN_M;
	res = yt921x_reg_clear_bits(priv, YT921X_PORTn_CTRL(port), mask);
	if (res)
		return res;

	if (yt921x_is_external_port(priv, port)) {
		mask = YT921X_SERDES_LINK;
		res = yt921x_reg_clear_bits(priv, YT921X_SERDESn(port), mask);
		if (res)
			return res;

		mask = YT921X_XMII_LINK;
		res = yt921x_reg_clear_bits(priv, YT921X_XMIIn(port), mask);
		if (res)
			return res;

		res = yt921x_mdio_polling_set(priv, port, false, 0, 0);
		if (res)
			return res;
	}

	return 0;
}

static int
yt921x_port_validate_link_mode(struct yt921x_priv *priv, int port,
			       phy_interface_t interface, int speed, int duplex)
{
	if (!yt921x_is_external_port(priv, port)) {
		if (interface != PHY_INTERFACE_MODE_INTERNAL)
			return -EINVAL;

		switch (speed) {
		case SPEED_10:
		case SPEED_100:
		case SPEED_1000:
			return 0;
		default:
			return -EINVAL;
		}
	}

	switch (interface) {
	case PHY_INTERFACE_MODE_SGMII:
		switch (speed) {
		case SPEED_10:
		case SPEED_100:
		case SPEED_1000:
			return 0;
		case SPEED_2500:
			return (duplex == DUPLEX_FULL) ? 0 : -EINVAL;
		default:
			return -EINVAL;
		}
	case PHY_INTERFACE_MODE_100BASEX:
		return (speed == SPEED_100 && duplex == DUPLEX_FULL) ? 0 : -EINVAL;
	case PHY_INTERFACE_MODE_1000BASEX:
		return (speed == SPEED_1000 && duplex == DUPLEX_FULL) ? 0 : -EINVAL;
	case PHY_INTERFACE_MODE_2500BASEX:
		return (speed == SPEED_2500 && duplex == DUPLEX_FULL) ? 0 : -EINVAL;
	default:
		return -EINVAL;
	}
}

static int
yt921x_port_up(struct yt921x_priv *priv, int port, unsigned int mode,
	       phy_interface_t interface, int speed, int duplex,
	       bool tx_pause, bool rx_pause)
{
	struct device *dev = yt921x_dev(priv);
	u32 mask;
	u32 ctrl;
	int res;

	res = yt921x_port_validate_link_mode(priv, port, interface, speed,
					     duplex);
	if (res) {
		dev_err(dev,
			"Unsupported link mode on port %d: if=%d speed=%d duplex=%d\n",
			port, interface, speed, duplex);
		return res;
	}

	switch (speed) {
	case SPEED_10:
		ctrl = YT921X_PORT_SPEED_10;
		break;
	case SPEED_100:
		ctrl = YT921X_PORT_SPEED_100;
		break;
	case SPEED_1000:
		ctrl = YT921X_PORT_SPEED_1000;
		break;
	case SPEED_2500:
		ctrl = YT921X_PORT_SPEED_2500;
		break;
	case SPEED_10000:
		ctrl = YT921X_PORT_SPEED_10000;
		break;
	default:
		return -EINVAL;
	}
	if (duplex == DUPLEX_FULL)
		ctrl |= YT921X_PORT_DUPLEX_FULL;
	if (tx_pause)
		ctrl |= YT921X_PORT_TX_PAUSE;
	if (rx_pause)
		ctrl |= YT921X_PORT_RX_PAUSE;
	ctrl |= YT921X_PORT_RX_MAC_EN | YT921X_PORT_TX_MAC_EN;
	/* Preserve undocumented / strap-controlled bits in PORTn_CTRL
	 * (e.g. pause-an / half-pause), only touch fields we own.
	 */
	mask = YT921X_PORT_SPEED_M | YT921X_PORT_DUPLEX_FULL |
	       YT921X_PORT_RX_PAUSE | YT921X_PORT_TX_PAUSE |
	       YT921X_PORT_RX_MAC_EN | YT921X_PORT_TX_MAC_EN;
	res = yt921x_reg_update_bits(priv, YT921X_PORTn_CTRL(port), mask, ctrl);
	if (res)
		return res;

	/* Keep board-specific non-managed bits stable across link events. */
	res = yt921x_port_ctrl_apply_dt(priv, port, false);
	if (res)
		return res;

	if (yt921x_is_external_port(priv, port)) {
		mask = YT921X_SERDES_SPEED_M;
		switch (speed) {
		case SPEED_10:
			ctrl = YT921X_SERDES_SPEED_10;
			break;
		case SPEED_100:
			ctrl = YT921X_SERDES_SPEED_100;
			break;
		case SPEED_1000:
			ctrl = YT921X_SERDES_SPEED_1000;
			break;
		case SPEED_2500:
			ctrl = YT921X_SERDES_SPEED_2500;
			break;
		case SPEED_10000:
			ctrl = YT921X_SERDES_SPEED_10000;
			break;
		default:
			return -EINVAL;
		}
		mask |= YT921X_SERDES_DUPLEX_FULL;
		if (duplex == DUPLEX_FULL)
			ctrl |= YT921X_SERDES_DUPLEX_FULL;
		mask |= YT921X_SERDES_TX_PAUSE;
		if (tx_pause)
			ctrl |= YT921X_SERDES_TX_PAUSE;
		mask |= YT921X_SERDES_RX_PAUSE;
		if (rx_pause)
			ctrl |= YT921X_SERDES_RX_PAUSE;
		mask |= YT921X_SERDES_LINK;
		ctrl |= YT921X_SERDES_LINK;
		res = yt921x_reg_update_bits(priv, YT921X_SERDESn(port),
					     mask, ctrl);
		if (res)
			return res;

		mask = YT921X_XMII_LINK;
		res = yt921x_reg_set_bits(priv, YT921X_XMIIn(port), mask);
		if (res)
			return res;

		res = yt921x_mdio_polling_set(priv, port, true, speed, duplex);
		if (res)
			return res;
	}

	return 0;
}

static int
yt921x_port_get_serdes_mode(struct yt921x_priv *priv, int port,
			    phy_interface_t interface, u32 *ctrl)
{
	struct device *dev = yt921x_dev(priv);
	struct dsa_port *dp = dsa_to_port(&priv->ds, port);
	const char *mode;
	u32 force_mode;

	if (!dp || !dp->dn)
		goto from_phy_mode;

	if (of_property_read_bool(dp->dn, "motorcomm,serdes-sgmii") &&
	    of_property_read_bool(dp->dn, "motorcomm,serdes-revsgmii")) {
		dev_err(dev, "Port %d has both serdes-sgmii and serdes-revsgmii\n",
			port);
		return -EINVAL;
	}

	/* Legacy bool controls for PHY_INTERFACE_MODE_SGMII */
	if (of_property_read_bool(dp->dn, "motorcomm,serdes-sgmii")) {
		force_mode = YT921X_SERDES_MODE_SGMII;
		goto validate_force_mode;
	}
	if (of_property_read_bool(dp->dn, "motorcomm,serdes-revsgmii")) {
		force_mode = YT921X_SERDES_MODE_REVSGMII;
		goto validate_force_mode;
	}

	/* Explicit serdes mode override */
	if (!of_property_read_string(dp->dn, "motorcomm,serdes-mode", &mode)) {
		if (!strcmp(mode, "sgmii"))
			force_mode = YT921X_SERDES_MODE_SGMII;
		else if (!strcmp(mode, "revsgmii"))
			force_mode = YT921X_SERDES_MODE_REVSGMII;
		else if (!strcmp(mode, "100base-x"))
			force_mode = YT921X_SERDES_MODE_100BASEX;
		else if (!strcmp(mode, "1000base-x"))
			force_mode = YT921X_SERDES_MODE_1000BASEX;
		else if (!strcmp(mode, "2500base-x"))
			force_mode = YT921X_SERDES_MODE_2500BASEX;
		else {
			dev_err(dev, "Port %d has unsupported serdes-mode '%s'\n",
				port, mode);
			return -EINVAL;
		}

validate_force_mode:
		switch (interface) {
		case PHY_INTERFACE_MODE_SGMII:
			if (force_mode == YT921X_SERDES_MODE_SGMII ||
			    force_mode == YT921X_SERDES_MODE_REVSGMII)
				break;
			fallthrough;
		case PHY_INTERFACE_MODE_100BASEX:
			if (force_mode == YT921X_SERDES_MODE_100BASEX)
				break;
			fallthrough;
		case PHY_INTERFACE_MODE_1000BASEX:
			if (force_mode == YT921X_SERDES_MODE_1000BASEX)
				break;
			fallthrough;
		case PHY_INTERFACE_MODE_2500BASEX:
			if (force_mode == YT921X_SERDES_MODE_2500BASEX)
				break;
			fallthrough;
		default:
			dev_err(dev,
				"Port %d serdes-mode incompatible with phy-mode %d\n",
				port, interface);
			return -EINVAL;
		}

		*ctrl = force_mode;
		return 0;
	}

from_phy_mode:
	switch (interface) {
	case PHY_INTERFACE_MODE_SGMII:
		/* This kernel lacks PHY_INTERFACE_MODE_REVSGMII. */
		*ctrl = YT921X_SERDES_MODE_REVSGMII;
		break;
	case PHY_INTERFACE_MODE_100BASEX:
		*ctrl = YT921X_SERDES_MODE_100BASEX;
		break;
	case PHY_INTERFACE_MODE_1000BASEX:
		*ctrl = YT921X_SERDES_MODE_1000BASEX;
		break;
	case PHY_INTERFACE_MODE_2500BASEX:
		*ctrl = YT921X_SERDES_MODE_2500BASEX;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int
yt921x_port_config(struct yt921x_priv *priv, int port, unsigned int mode,
		   phy_interface_t interface)
{
	struct device *dev = yt921x_dev(priv);
	u32 mask;
	u32 ctrl;
	int res;

	if (!yt921x_is_external_port(priv, port)) {
		if (interface != PHY_INTERFACE_MODE_INTERNAL) {
			dev_err(dev, "Wrong mode %d on port %d\n",
				interface, port);
			return -EINVAL;
		}
		return 0;
	}

	switch (interface) {
	/* SERDES */
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_100BASEX:
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_2500BASEX:
		mask = YT921X_SERDES_CTRL_PORTn(port);
		res = yt921x_reg_set_bits(priv, YT921X_SERDES_CTRL, mask);
		if (res)
			return res;

		mask = YT921X_XMII_CTRL_PORTn(port);
		res = yt921x_reg_clear_bits(priv, YT921X_XMII_CTRL, mask);
		if (res)
			return res;

		mask = YT921X_SERDES_MODE_M;
		res = yt921x_port_get_serdes_mode(priv, port, interface, &ctrl);
		if (res)
			return res;
		res = yt921x_reg_update_bits(priv, YT921X_SERDESn(port),
					     mask, ctrl);
		if (res)
			return res;

		break;
	/* add XMII support here */
	default:
		return -EINVAL;
	}

	return 0;
}

static void
yt921x_phylink_mac_link_down(struct phylink_config *config, unsigned int mode,
			     phy_interface_t interface)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);
	struct yt921x_priv *priv = yt921x_to_priv(dp->ds);
	int port = dp->index;
	int res;

	WRITE_ONCE(priv->ports[port].port_up, false);
	cancel_delayed_work_sync(&priv->ports[port].mib_read);

	mutex_lock(&priv->reg_lock);
	res = yt921x_port_down(priv, port);
	mutex_unlock(&priv->reg_lock);

	if (res)
		dev_err(dp->ds->dev, "Failed to %s port %d: %i\n", "bring down",
			port, res);
}

static void
yt921x_phylink_mac_link_up(struct phylink_config *config,
			   struct phy_device *phydev, unsigned int mode,
			   phy_interface_t interface, int speed, int duplex,
			   bool tx_pause, bool rx_pause)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);
	struct yt921x_priv *priv = yt921x_to_priv(dp->ds);
	int port = dp->index;
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_port_up(priv, port, mode, interface, speed, duplex,
			     tx_pause, rx_pause);
	mutex_unlock(&priv->reg_lock);

	if (res)
		dev_err(dp->ds->dev, "Failed to %s port %d: %i\n", "bring up",
			port, res);

	if (!res) {
		WRITE_ONCE(priv->ports[port].port_up, true);
		schedule_delayed_work(&priv->ports[port].mib_read, 0);
	}
}

static void
yt921x_phylink_mac_config(struct phylink_config *config, unsigned int mode,
			  const struct phylink_link_state *state)
{
	struct dsa_port *dp = dsa_phylink_to_port(config);
	struct yt921x_priv *priv = yt921x_to_priv(dp->ds);
	int port = dp->index;
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_port_config(priv, port, mode, state->interface);
	mutex_unlock(&priv->reg_lock);

	if (res)
		dev_err(dp->ds->dev, "Failed to %s port %d: %i\n", "config",
			port, res);
}

static void
yt921x_phylink_set_external_caps(struct phylink_config *config,
				 phy_interface_t interface)
{
	switch (interface) {
	case PHY_INTERFACE_MODE_SGMII:
		/* This kernel lacks PHY_INTERFACE_MODE_REVSGMII.
		 * PHY_INTERFACE_MODE_SGMII defaults to REVSGMII for
		 * compatibility.
		 */
		__set_bit(PHY_INTERFACE_MODE_SGMII,
			  config->supported_interfaces);
		config->mac_capabilities |= MAC_10 | MAC_100 | MAC_1000;
		break;
	case PHY_INTERFACE_MODE_100BASEX:
		__set_bit(PHY_INTERFACE_MODE_100BASEX,
			  config->supported_interfaces);
		config->mac_capabilities |= MAC_100;
		break;
	case PHY_INTERFACE_MODE_1000BASEX:
		__set_bit(PHY_INTERFACE_MODE_1000BASEX,
			  config->supported_interfaces);
		config->mac_capabilities |= MAC_1000;
		break;
	case PHY_INTERFACE_MODE_2500BASEX:
		__set_bit(PHY_INTERFACE_MODE_2500BASEX,
			  config->supported_interfaces);
		config->mac_capabilities |= MAC_2500FD;
		break;
	default:
		break;
	}
}

static void
yt921x_dsa_phylink_get_caps(struct dsa_switch *ds, int port,
			    struct phylink_config *config)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	const struct yt921x_info *info = priv->info;
	struct dsa_port *dp = dsa_to_port(ds, port);
	phy_interface_t interface;

	config->mac_capabilities = MAC_ASYM_PAUSE | MAC_SYM_PAUSE;

	if (info->internal_mask & BIT(port)) {
		/* Port 10 for MCU should probably go here too. But since that
		 * is untested yet, turn it down for the moment by letting it
		 * fall to the default branch.
		 */
		__set_bit(PHY_INTERFACE_MODE_INTERNAL,
			  config->supported_interfaces);
		config->mac_capabilities |= MAC_10 | MAC_100 | MAC_1000;
	} else if (info->external_mask & BIT(port)) {
		/* External interface support is per-port and driven by
		 * devicetree. Advertise only the configured, implemented modes.
		 */
		if (dp && dp->dn && !of_get_phy_mode(dp->dn, &interface)) {
			yt921x_phylink_set_external_caps(config, interface);
		} else {
			/* Compatibility fallback for old trees without
			 * explicit phy-mode on external ports.
			 */
			yt921x_phylink_set_external_caps(config,
							 PHY_INTERFACE_MODE_SGMII);
			yt921x_phylink_set_external_caps(config,
							 PHY_INTERFACE_MODE_100BASEX);
			yt921x_phylink_set_external_caps(config,
							 PHY_INTERFACE_MODE_1000BASEX);
			yt921x_phylink_set_external_caps(config,
							 PHY_INTERFACE_MODE_2500BASEX);
		}
	}
	/* no such port: empty supported_interfaces causes phylink to turn it
	 * down
	 */
}

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

static int yt921x_dsa_setup(struct dsa_switch *ds)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	struct device *dev = yt921x_dev(priv);
	struct device_node *np = dev->of_node;
	struct device_node *child;
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_chip_reset(priv);
	mutex_unlock(&priv->reg_lock);

	if (res)
		return res;

	/* Register the internal mdio bus. Nodes for internal ports should have
	 * proper phy-handle pointing to their PHYs. Not enabling the internal
	 * bus is possible, though pretty wired, if internal ports are not used.
	 */
	child = of_get_child_by_name(np, "mdio");
	if (child) {
		res = yt921x_mbus_int_init(priv, child);
		of_node_put(child);
		if (res)
			return res;
	}

	/* External mdio bus is optional */
	child = of_get_child_by_name(np, "mdio-external");
	if (child) {
		res = yt921x_mbus_ext_init(priv, child);
		of_node_put(child);
		if (res)
			return res;
	}

	mutex_lock(&priv->reg_lock);
	res = yt921x_chip_setup(priv);
	mutex_unlock(&priv->reg_lock);

	if (res)
		return res;

	return 0;
}

static const struct phylink_mac_ops yt921x_phylink_mac_ops = {
	.mac_link_down	= yt921x_phylink_mac_link_down,
	.mac_link_up	= yt921x_phylink_mac_link_up,
	.mac_config	= yt921x_phylink_mac_config,
};

static const struct dsa_switch_ops yt921x_dsa_switch_ops = {
	/* mib */
	.get_strings		= yt921x_dsa_get_strings,
	.get_ethtool_stats	= yt921x_dsa_get_ethtool_stats,
	.get_sset_count		= yt921x_dsa_get_sset_count,
	.get_eth_mac_stats	= yt921x_dsa_get_eth_mac_stats,
	.get_eth_ctrl_stats	= yt921x_dsa_get_eth_ctrl_stats,
	.get_rmon_stats		= yt921x_dsa_get_rmon_stats,
	.get_stats64		= yt921x_dsa_get_stats64,
	.get_pause_stats	= yt921x_dsa_get_pause_stats,
	/* eee */
	.support_eee		= dsa_supports_eee,
	.set_mac_eee		= yt921x_dsa_set_mac_eee,
	/* tc */
	.cls_flower_add		= yt921x_dsa_cls_flower_add,
	.cls_flower_del		= yt921x_dsa_cls_flower_del,
	.cls_flower_stats	= yt921x_dsa_cls_flower_stats,
	.port_setup_tc		= yt921x_dsa_port_setup_tc,
	.port_policer_add	= yt921x_dsa_port_policer_add,
	.port_policer_del	= yt921x_dsa_port_policer_del,
	/* dcb */
	.port_get_default_prio	= yt921x_dsa_port_get_default_prio,
	.port_set_default_prio	= yt921x_dsa_port_set_default_prio,
	.port_get_apptrust	= yt921x_dsa_port_get_apptrust,
	.port_set_apptrust	= yt921x_dsa_port_set_apptrust,
	/* mtu */
	.port_change_mtu	= yt921x_dsa_port_change_mtu,
	.port_max_mtu		= yt921x_dsa_port_max_mtu,
	/* mirror */
	.port_mirror_del	= yt921x_dsa_port_mirror_del,
	.port_mirror_add	= yt921x_dsa_port_mirror_add,
	/* lag */
	.port_lag_leave		= yt921x_dsa_port_lag_leave,
	.port_lag_join		= yt921x_dsa_port_lag_join,
	/* fdb */
	.port_fdb_dump		= yt921x_dsa_port_fdb_dump,
	.port_fast_age		= yt921x_dsa_port_fast_age,
	.set_ageing_time	= yt921x_dsa_set_ageing_time,
	.port_fdb_del		= yt921x_dsa_port_fdb_del,
	.port_fdb_add		= yt921x_dsa_port_fdb_add,
	.port_mdb_del		= yt921x_dsa_port_mdb_del,
	.port_mdb_add		= yt921x_dsa_port_mdb_add,
	/* vlan */
	.port_vlan_filtering	= yt921x_dsa_port_vlan_filtering,
	.port_vlan_del		= yt921x_dsa_port_vlan_del,
	.port_vlan_add		= yt921x_dsa_port_vlan_add,
	/* dscp */
	.port_get_dscp_prio	= yt921x_dsa_port_get_dscp_prio,
	.port_del_dscp_prio	= yt921x_dsa_port_del_dscp_prio,
	.port_add_dscp_prio	= yt921x_dsa_port_add_dscp_prio,
	/* bridge */
	.port_pre_bridge_flags	= yt921x_dsa_port_pre_bridge_flags,
	.port_bridge_flags	= yt921x_dsa_port_bridge_flags,
	.port_bridge_leave	= yt921x_dsa_port_bridge_leave,
	.port_bridge_join	= yt921x_dsa_port_bridge_join,
	/* mst */
	.port_mst_state_set	= yt921x_dsa_port_mst_state_set,
	.vlan_msti_set		= yt921x_dsa_vlan_msti_set,
	.port_stp_state_set	= yt921x_dsa_port_stp_state_set,
	/* port */
	.get_tag_protocol	= yt921x_dsa_get_tag_protocol,
	.port_change_conduit	= yt921x_dsa_port_change_conduit,
	.phylink_get_caps	= yt921x_dsa_phylink_get_caps,
	.port_setup		= yt921x_dsa_port_setup,
	.port_enable		= yt921x_dsa_port_enable,
	.port_disable		= yt921x_dsa_port_disable,
	/* chip */
	.setup			= yt921x_dsa_setup,
};

static void yt921x_mdio_shutdown(struct mdio_device *mdiodev)
{
	struct yt921x_priv *priv = mdiodev_get_drvdata(mdiodev);

	if (!priv)
		return;

#if IS_ENABLED(CONFIG_NET_DSA_YT921X_DEBUG)
	cancel_delayed_work_sync(&priv->storm_guard_work);
#endif

	dsa_switch_shutdown(&priv->ds);
}

static void yt921x_mdio_remove(struct mdio_device *mdiodev)
{
	struct yt921x_priv *priv = mdiodev_get_drvdata(mdiodev);

	if (!priv)
		return;

	for (size_t i = ARRAY_SIZE(priv->ports); i-- > 0; ) {
		struct yt921x_port *pp = &priv->ports[i];

		disable_delayed_work_sync(&pp->mib_read);
	}

#if IS_ENABLED(CONFIG_NET_DSA_YT921X_DEBUG)
	cancel_delayed_work_sync(&priv->storm_guard_work);
#endif

	dsa_unregister_switch(&priv->ds);

	yt921x_proc_exit(priv);
	mutex_destroy(&priv->reg_lock);
}

static int yt921x_mdio_probe(struct mdio_device *mdiodev)
{
	struct device *dev = &mdiodev->dev;
	struct yt921x_reg_mdio *mdio;
	struct yt921x_priv *priv;
	struct dsa_switch *ds;
	int res;
	u32 switchid = 0;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mdio = devm_kzalloc(dev, sizeof(*mdio), GFP_KERNEL);
	if (!mdio)
		return -ENOMEM;

	mdio->bus = mdiodev->bus;
	mdio->addr = mdiodev->addr;
	if (of_property_read_u32(dev->of_node, "motorcomm,switchid",
				 &switchid))
		of_property_read_u32(dev->of_node, "switchid", &switchid);
	if (switchid > 1) {
		dev_err(dev, "Invalid switchid %u\n", switchid);
		return -EINVAL;
	}
	mdio->switchid = switchid;

	mutex_init(&priv->reg_lock);

	res = yt921x_proc_init(priv);
	if (res) {
		mutex_destroy(&priv->reg_lock);
		return res;
	}

	priv->reg_ops = &yt921x_reg_ops_mdio;
	priv->reg_ctx = mdio;
	priv->primary_cpu_port = -1;
	priv->secondary_cpu_port = -1;

	for (size_t i = 0; i < ARRAY_SIZE(priv->ports); i++) {
		struct yt921x_port *pp = &priv->ports[i];

		pp->index = i;
		pp->priv = priv;
		pp->mcast_flood = true;
		pp->bcast_flood = true;
		pp->mcast_fast_leave = false;
		INIT_DELAYED_WORK(&pp->mib_read, yt921x_poll_mib);
	}

#if IS_ENABLED(CONFIG_NET_DSA_YT921X_DEBUG)
	INIT_DELAYED_WORK(&priv->storm_guard_work, yt921x_storm_guard_workfn);
	priv->storm_guard_enabled = false;
	priv->storm_guard_primed = false;
	priv->storm_guard_pps = YT921X_STORM_GUARD_DEFAULT_PPS;
	priv->storm_guard_hold_ms = YT921X_STORM_GUARD_DEFAULT_HOLD_MS;
	priv->storm_guard_interval_ms = YT921X_STORM_GUARD_DEFAULT_INTERVAL_MS;
#endif

	ds = &priv->ds;
	ds->dev = dev;
	ds->assisted_learning_on_cpu_port = true;
	ds->priv = priv;
	ds->ops = &yt921x_dsa_switch_ops;
	ds->ageing_time_min = 1 * 5000;
	ds->ageing_time_max = U16_MAX * 5000;
	ds->phylink_mac_ops = &yt921x_phylink_mac_ops;
	ds->dscp_prio_mapping_is_global = true;
	ds->num_lag_ids = YT921X_LAG_NUM;
	ds->num_ports = YT921X_PORT_NUM;
	ds->num_tx_queues = YT921X_PRIO_NUM;

	mdiodev_set_drvdata(mdiodev, priv);

	res = dsa_register_switch(ds);
	if (res) {
		yt921x_proc_exit(priv);
		mutex_destroy(&priv->reg_lock);
	}

	return res;
}

static const struct of_device_id yt921x_of_match[] = {
	{ .compatible = "motorcomm,yt9215" },
	{}
};
MODULE_DEVICE_TABLE(of, yt921x_of_match);

static struct mdio_driver yt921x_mdio_driver = {
	.probe = yt921x_mdio_probe,
	.remove = yt921x_mdio_remove,
	.shutdown = yt921x_mdio_shutdown,
	.mdiodrv.driver = {
		.name = YT921X_NAME,
		.of_match_table = yt921x_of_match,
	},
};

mdio_module_driver(yt921x_mdio_driver);

MODULE_AUTHOR("David Yang <mmyangfl@gmail.com>");
MODULE_DESCRIPTION("Driver for Motorcomm YT921x Switch");
