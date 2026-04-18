/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * MBUS/MDIO transport split unit for yt921x
 *
 * Copyright (c) 2025 David Yang
 * Copyright (c) 2026 zcop <hongson.hn@gmail.com>
 */

#include "yt921x_internal.h"

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
