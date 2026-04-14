/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Internal split unit for yt921x.c
 *
 * Copyright (c) 2025 David Yang
 * Copyright (c) 2026 zcop <hongson.hn@gmail.com>
 */

#include "yt921x_internal.h"

static u16 yt921x_vlan_fid_for_vid(const struct yt921x_priv *priv, u16 vid)
{
	if (priv->vlan_fid_mode == YT921X_VLAN_FID_MODE_SVL)
		return priv->vlan_svl_fid ? : 1;

	return vid;
}

void
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

int
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

int
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

int
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
yt921x_l2_fdb_aging_port_en_set(struct yt921x_priv *priv, int port, bool enable)
{
	if (port < 0 || port >= YT921X_PORT_NUM)
		return -EINVAL;

	return yt921x_reg_toggle_bits(priv, YT921X_L2_FDB_AGING_PORT_EN,
				      YT921X_L2_FDB_AGING_PORT_EN_PORTn(port),
				      enable);
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

int
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

void yt921x_dsa_port_fast_age(struct dsa_switch *ds, int port)
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

int
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

int
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

int
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

static u16 yt921x_mdb_resolve_vid(struct yt921x_priv *priv, int port, u16 vid)
{
	struct dsa_port *dp;
	struct net_device *bdev;
	u16 pvid = 0;

	if (vid)
		return vid;

	dp = dsa_to_port(&priv->ds, port);
	if (!dsa_port_is_vlan_filtering(dp))
		return YT921X_VID_UNAWARE;

	bdev = dsa_port_bridge_dev_get(dp);
	if (bdev && !br_vlan_get_pvid(bdev, &pvid) && pvid)
		return pvid;

	return 0;
}

int
yt921x_dsa_port_mdb_del(struct dsa_switch *ds, int port,
			const struct switchdev_obj_port_mdb *mdb,
			struct dsa_db db)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	struct device *dev = yt921x_dev(priv);
	const unsigned char *addr = mdb->addr;
	u16 vid = yt921x_mdb_resolve_vid(priv, port, mdb->vid);
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_fdb_leave(priv, addr, vid, BIT(port));
	mutex_unlock(&priv->reg_lock);

	dev_dbg(dev, "mdb del: grp=%pM vid=%u (orig %u) port=%d res=%d\n",
		addr, vid, mdb->vid, port, res);

	return res;
}

int
yt921x_dsa_port_mdb_add(struct dsa_switch *ds, int port,
			const struct switchdev_obj_port_mdb *mdb,
			struct dsa_db db)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	struct device *dev = yt921x_dev(priv);
	const unsigned char *addr = mdb->addr;
	u16 vid = yt921x_mdb_resolve_vid(priv, port, mdb->vid);
	int res;

	/* Bridge core rejects static MDB programming when multicast snooping
	 * is disabled; userspace must enable bridge multicast_snooping first.
	 */
	mutex_lock(&priv->reg_lock);
	res = yt921x_fdb_join(priv, addr, vid, BIT(port));
	mutex_unlock(&priv->reg_lock);

	dev_dbg(dev, "mdb add: grp=%pM vid=%u (orig %u) port=%d res=%d\n",
		addr, vid, mdb->vid, port, res);

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
		ctrl64 |= YT921X_VLAN_CTRL_FID(yt921x_vlan_fid_for_vid(priv,
									vid));
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

int
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

int
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

int
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


u32 yt921x_non_cpu_port_mask(const struct yt921x_priv *priv)
{
	return GENMASK(YT921X_PORT_NUM - 1, 0) & ~priv->cpu_ports_mask;
}

int
yt921x_port_isolation_set(struct yt921x_priv *priv, int port, u32 blocked_mask)
{
	u32 mask;

	mask = yt921x_non_cpu_port_mask(priv);

	return yt921x_reg_update_bits(priv, YT921X_PORTn_ISOLATION(port),
				      mask, blocked_mask & mask);
}

int
yt921x_userport_cpu_isolation_set(struct yt921x_priv *priv, int port, int cpu_port)
{
	u32 mask = priv->cpu_ports_mask;
	u32 blocked_mask;

	blocked_mask = mask & ~BIT(cpu_port);

	return yt921x_reg_update_bits(priv, YT921X_PORTn_ISOLATION(port),
				      mask, blocked_mask);
}

int
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

int yt921x_secondary_cpu_isolation_sync(struct yt921x_priv *priv,
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

int
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

int
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

int yt921x_userport_standalone(struct yt921x_priv *priv, int port)
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

		res = yt921x_l2_fdb_aging_port_en_set(priv, port, learning);
		if (res)
			return res;

		if (!learning) {
			res = yt921x_fdb_flush_port(priv, port, false);
			if (res)
				return res;
		}
	}

	refresh_flood = false;
	refresh_mcast_policy = false;
	if (flags.mask & BR_FLOOD) {
		pp->ucast_flood = flags.val & BR_FLOOD;
		refresh_flood = true;
	}
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

int
yt921x_dsa_port_pre_bridge_flags(struct dsa_switch *ds, int port,
				 struct switchdev_brport_flags flags,
				 struct netlink_ext_ack *extack)
{
	(void)extack;

	if (flags.mask & ~(BR_HAIRPIN_MODE | BR_LEARNING |
			   BR_FLOOD | BR_MCAST_FLOOD | BR_BCAST_FLOOD |
			   BR_MULTICAST_FAST_LEAVE | BR_ISOLATED))
		return -EINVAL;
	return 0;
}

int
yt921x_dsa_port_bridge_flags(struct dsa_switch *ds, int port,
			     struct switchdev_brport_flags flags,
			     struct netlink_ext_ack *extack)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	int res;

	(void)extack;

	if (dsa_is_cpu_port(ds, port))
		return 0;

	mutex_lock(&priv->reg_lock);
	res = yt921x_bridge_flags(priv, port, flags);
	mutex_unlock(&priv->reg_lock);

	return res;
}

void
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

int
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
