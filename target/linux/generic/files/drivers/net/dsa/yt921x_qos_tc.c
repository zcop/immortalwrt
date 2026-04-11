/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Internal split unit for yt921x.c
 *
 * Copyright (c) 2025 David Yang
 * Copyright (c) 2026 zcop <hongson.hn@gmail.com>
 */

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
yt921x_qsch_tbf_apply_raw(struct yt921x_priv *priv, int port, u8 qid,
			  u64 rate_bytes_ps, u32 burst_bytes)
{
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

	res = yt921x_tbf_rate_to_eir(rate_bytes_ps, &cir);
	if (res)
		return res;

	res = yt921x_tbf_burst_to_ebs(burst_bytes, &cbs);
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
yt921x_qsch_tbf_add(struct yt921x_priv *priv, int port, u8 qid,
		    struct tc_tbf_qopt_offload *qopt)
{
	const struct tc_tbf_qopt_offload_replace_params *params = &qopt->replace_params;

	return yt921x_qsch_tbf_apply_raw(priv, port, qid,
					 params->rate.rate_bytes_ps,
					 params->max_size);
}

static int yt921x_trap_copp_default_apply(struct yt921x_priv *priv)
{
	unsigned long cpu_ports_mask = priv->cpu_ports_mask;
	u8 prio = YT921X_TRAP_COPP_DEFAULT_PRIO;
	u8 qid = YT921X_TRAP_COPP_DEFAULT_QID;
	int port;

	for_each_set_bit(port, &cpu_ports_mask, YT921X_PORT_NUM) {
		u32 ucast;
		u32 mcast;
		int res;

		res = yt921x_reg_read(priv, YT921X_QOS_QUEUE_MAP_UCASTn(port),
				      &ucast);
		if (res)
			return res;

		res = yt921x_reg_read(priv, YT921X_QOS_QUEUE_MAP_MCASTn(port),
				      &mcast);
		if (res)
			return res;

		ucast &= ~YT921X_QOS_UCAST_QMAP_PRIO_M(prio);
		ucast |= YT921X_QOS_UCAST_QMAP_PRIO(prio, qid);
		mcast &= ~YT921X_QOS_MCAST_QMAP_PRIO_M(prio);
		mcast |= YT921X_QOS_MCAST_QMAP_PRIO(prio, min_t(u8, qid, 3));

		res = yt921x_reg_write(priv, YT921X_QOS_QUEUE_MAP_UCASTn(port),
				       ucast);
		if (res)
			return res;

		res = yt921x_reg_write(priv, YT921X_QOS_QUEUE_MAP_MCASTn(port),
				       mcast);
		if (res)
			return res;

		res = yt921x_qsch_tbf_apply_raw(priv, port, qid,
						YT921X_TRAP_COPP_DEFAULT_RATE_BYTES_PER_SEC,
						YT921X_TRAP_COPP_DEFAULT_BURST_BYTES);
		if (res)
			return res;
	}

	return 0;
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
