/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Internal split unit for yt921x.c
 *
 * Copyright (c) 2025 David Yang
 * Copyright (c) 2026 zcop <hongson.hn@gmail.com>
 */

#include "yt921x_internal.h"

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
int
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

int
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

int
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

int
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

void
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

int __maybe_unused
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

int __maybe_unused
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

int __maybe_unused
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

int __maybe_unused
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
				 u32 token_unit, bool rate_mode_pps, u32 *cirp)
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

	/* Vendor HAL conversion:
	 *   cir = (rate * timeslot_ns << (base - 2 * token_unit)) / 1e9
	 * where base is 11 in BPS mode and 21 in PPS mode.
	 * token_level here is timeslot_ns-equivalent for current chip clocks.
	 */
	shift = (rate_mode_pps ? 21 : 11) - 2 * token_unit;
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

static int
yt921x_ingress_meter_profile_apply(struct yt921x_priv *priv, u32 token_level,
				   u32 meter_id)
{
	u32 meter_idx = meter_id + YT921X_RATE_IGR_METER_BASE;
	u32 meter_word1;
	u32 meter_word2;
	u32 cir;
	u32 f4;
	int res;

	res = yt921x_reg_read(priv, YT921X_RATE_METER_CONFIG_WORD2(meter_idx),
			      &meter_word2);
	if (res)
		return res;

	/* Stock ingress path uses BPS mode with token-unit defaults and
	 * color-blind drop on Y/R for out-of-profile traffic.
	 */
	f4 = FIELD_GET(YT921X_RATE_METER_CFG_F4_M, meter_word2);
	if (!f4) {
		f4 = YT921X_RATE_TOKEN_UNIT_DEFAULT;
		meter_word2 &= ~YT921X_RATE_METER_CFG_F4_M;
		meter_word2 |= YT921X_RATE_METER_CFG_F4(f4);
	}

	meter_word2 &= ~(YT921X_RATE_METER_CFG_F6 |
			 YT921X_METER_CTRLc_RFC2698 |
			 YT921X_METER_CTRLc_PKT_MODE |
			 YT921X_METER_CTRLc_DROP_M);
	meter_word2 |= YT921X_METER_CTRLc_METER_EN |
		       YT921X_METER_CTRLc_COLOR_BLIND |
		       YT921X_METER_CTRLc_DROP_YR;

	res = yt921x_stock_ingress_rate_to_cir(priv->storm_policer_rate_bytes_per_sec,
					       token_level, f4, false, &cir);
	if (res)
		return res;

	res = yt921x_reg_read(priv, YT921X_RATE_METER_CONFIG_WORD1(meter_idx),
			      &meter_word1);
	if (res)
		return res;

	meter_word1 &= ~YT921X_RATE_METER_CFG_CIR_M;
	meter_word1 |= YT921X_RATE_METER_CFG_CIR(cir);
	res = yt921x_reg_write(priv, YT921X_RATE_METER_CONFIG_WORD1(meter_idx),
			       meter_word1);
	if (res)
		return res;

	return yt921x_reg_write(priv, YT921X_RATE_METER_CONFIG_WORD2(meter_idx),
				meter_word2);
}

static int yt921x_ingress_meter_policer_apply(struct yt921x_priv *priv)
{
	u32 policer_ports = priv->storm_policer_ports & yt921x_non_cpu_port_mask(priv);
	unsigned long targets_mask = yt921x_non_cpu_port_mask(priv);
	u32 token_level;
	u32 c8;
	int port;
	int res;

	if (!policer_ports)
		goto disable_port_ctrl;

	res = yt921x_stock_ingress_meter_token_level(priv, &token_level);
	if (res)
		return res;

disable_port_ctrl:
	for_each_set_bit(port, &targets_mask, YT921X_PORT_NUM) {
		u32 meter_id = port;

		if (!dsa_is_user_port(&priv->ds, port))
			continue;

		res = yt921x_reg_read(priv, YT921X_RATE_IGR_BW_ENABLE + 4 * port, &c8);
		if (res)
			return res;

		c8 &= ~(YT921X_RATE_IGR_BW_ENABLE_EN |
			YT921X_RATE_IGR_BW_ENABLE_METER_ID_M);

		if (policer_ports & BIT(port)) {
			res = yt921x_ingress_meter_profile_apply(priv, token_level,
								 meter_id);
			if (res)
				return res;

			c8 |= YT921X_RATE_IGR_BW_ENABLE_EN |
			      YT921X_RATE_IGR_BW_ENABLE_METER_ID(meter_id);
		}

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

int
yt921x_dsa_port_policer_add(struct dsa_switch *ds, int port,
			    struct dsa_mall_policer_tc_entry *policer)
{
#if !IS_ENABLED(CONFIG_NET_DSA_YT921X_DEBUG)
	return -EOPNOTSUPP;
#else
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
#endif
}

void yt921x_dsa_port_policer_del(struct dsa_switch *ds, int port)
{
#if !IS_ENABLED(CONFIG_NET_DSA_YT921X_DEBUG)
	return;
#else
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
#endif
}

int yt921x_port_down(struct yt921x_priv *priv, int port)
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

void
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

void
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

void
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

void
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
