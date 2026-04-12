/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Internal split unit for yt921x.c
 *
 * Copyright (c) 2025 David Yang
 * Copyright (c) 2026 zcop <hongson.hn@gmail.com>
 */

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
yt921x_acl_remap_udf(struct yt921x_priv *priv, const u32 *udfs,
		     unsigned int cnt, struct netlink_ext_ack *extack,
		     u8 *remap)
{
	u32 ctrls[YT921X_UDF_NUM];
	unsigned long used = 0;
	unsigned int i;

	if (!cnt)
		return 0;
	if (cnt > YT921X_UDF_NUM) {
		NL_SET_ERR_MSG_MOD(extack, "Too many UDF selectors");
		return -EOPNOTSUPP;
	}

	memcpy(ctrls, priv->udfs_ctrl, sizeof(ctrls));
	for (i = 0; i < YT921X_UDF_NUM; i++)
		if (priv->udfs_refcnt[i])
			used |= BIT(i);

	for (i = 0; i < cnt; i++) {
		unsigned int j;

		for (j = 0; j < YT921X_UDF_NUM; j++)
			if ((used & BIT(j)) && ctrls[j] == udfs[i])
				break;

		if (j >= YT921X_UDF_NUM) {
			j = find_first_zero_bit(&used, YT921X_UDF_NUM);
			if (j >= YT921X_UDF_NUM) {
				NL_SET_ERR_MSG_MOD(extack, "UDF entry limit reached");
				return -EOPNOTSUPP;
			}

			used |= BIT(j);
			ctrls[j] = udfs[i];
		}

		remap[i] = j;
	}

	return 0;
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
		     u32 *udfs_ctrl, unsigned int *udfs_cntp,
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
	      BIT_ULL(FLOW_DISSECTOR_KEY_IP) |
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

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IP)) {
		struct flow_match_ip match;
		bool want_tos;
		bool want_ttl;

		flow_rule_match_ip(rule, &match);
		want_tos = !!match.mask->tos;
		want_ttl = !!match.mask->ttl;
		if (!want_tos && !want_ttl) {
			NL_SET_ERR_MSG_MOD(extack,
					   "FLOW_DISSECTOR_KEY_IP requires ip_tos/ip_ttl mask");
			return 0;
		}
		if (want_tos && want_ttl) {
			NL_SET_ERR_MSG_MOD(extack,
					   "simultaneous ip_tos + ip_ttl match is not supported");
			return 0;
		}

		if (want_tos) {
			u32 udf_ctrl;

			if (!n_proto_is_ipv4) {
				NL_SET_ERR_MSG_MOD(extack,
						   "ip_tos match requires exact protocol ip");
				return 0;
			}
			if (*udfs_cntp >= YT921X_UDF_NUM) {
				NL_SET_ERR_MSG_MOD(extack, "UDF selector limit reached");
				return 0;
			}

			/* UDF selector captures a 32-bit L3 window from configured
			 * offset. Match IPv4 TOS byte via top byte mask in UDF0.
			 */
			udf_ctrl = YT921X_UDF_CTRL_UDF_TYPE_L3 |
				   YT921X_UDF_CTRL_UDF_OFFSET(offsetof(struct iphdr, tos));
			entry_prepare();
			entry->key[1] = YT921X_ACL_KEYb_TYPE(YT921X_ACL_TYPE_UDF0 +
							     *udfs_cntp);
			entry->key[0] = YT921X_ACL_BINa_UDF_UDF0((u16)match.key->tos << 8);
			entry->mask[0] = YT921X_ACL_BINa_UDF_UDF0((u16)match.mask->tos << 8);
			udfs_ctrl[*udfs_cntp] = udf_ctrl;
			(*udfs_cntp)++;
		}

		if (want_ttl) {
			u32 udf_ctrl;

			if (!n_proto_is_ipv4 && !n_proto_is_ipv6) {
				NL_SET_ERR_MSG_MOD(extack,
						   "ip_ttl match requires exact protocol ip/ipv6");
				return 0;
			}
			if (*udfs_cntp >= YT921X_UDF_NUM) {
				NL_SET_ERR_MSG_MOD(extack, "UDF selector limit reached");
				return 0;
			}

			/* UDF selector captures a 32-bit L3 window from configured
			 * offset. Match TTL/Hop-Limit byte via top byte mask in UDF0.
			 */
			udf_ctrl = YT921X_UDF_CTRL_UDF_TYPE_L3 |
				   YT921X_UDF_CTRL_UDF_OFFSET(n_proto_is_ipv4 ?
					offsetof(struct iphdr, ttl) :
					offsetof(struct ipv6hdr, hop_limit));
			entry_prepare();
			entry->key[1] = YT921X_ACL_KEYb_TYPE(YT921X_ACL_TYPE_UDF0 +
							     *udfs_cntp);
			entry->key[0] = YT921X_ACL_BINa_UDF_UDF0((u16)match.key->ttl << 8);
			entry->mask[0] = YT921X_ACL_BINa_UDF_UDF0((u16)match.mask->ttl << 8);
			udfs_ctrl[*udfs_cntp] = udf_ctrl;
			(*udfs_cntp)++;
		}
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
yt921x_acl_parse_mangle_dscp(const struct flow_rule *rule,
			     const struct flow_action_entry *act,
			     struct netlink_ext_ack *extack, u8 *dscp,
			     bool *is_ipv4)
{
	struct flow_match_basic basic = {};
	u32 mask_be, val_be;
	u16 n_proto;
	u8 byte0_mask, byte1_mask;
	u8 byte0_val, byte1_val;
	int i;

	if (!flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_BASIC)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "DSCP rewrite requires explicit protocol match");
		return -EOPNOTSUPP;
	}

	flow_rule_match_basic(rule, &basic);
	if (basic.mask->n_proto != htons(0xffff)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "DSCP rewrite requires exact ethertype match");
		return -EOPNOTSUPP;
	}
	n_proto = ntohs(basic.key->n_proto);

	mask_be = cpu_to_be32(act->mangle.mask);
	val_be = cpu_to_be32(act->mangle.val);
	byte0_mask = (mask_be >> 24) & 0xff;
	byte1_mask = (mask_be >> 16) & 0xff;
	byte0_val = (val_be >> 24) & 0xff;
	byte1_val = (val_be >> 16) & 0xff;

	for (i = 2; i < 4; i++) {
		u8 m = (mask_be >> (24 - i * 8)) & 0xff;
		u8 v = (val_be >> (24 - i * 8)) & 0xff;

		if (m != 0xff || v) {
			NL_SET_ERR_MSG_MOD(extack,
					   "DSCP pedit must not modify bytes beyond DS field");
			return -EOPNOTSUPP;
		}
	}

	if (act->mangle.htype == FLOW_ACT_MANGLE_HDR_TYPE_IP4) {
		unsigned int byte_idx;
		u8 byte_mask, byte_val;

		if (n_proto != ETH_P_IP) {
			NL_SET_ERR_MSG_MOD(extack,
					   "IPv4 DSCP rewrite requires protocol ip");
			return -EOPNOTSUPP;
		}

		if (act->mangle.offset > offsetof(struct iphdr, tos) ||
		    act->mangle.offset + sizeof(u32) <= offsetof(struct iphdr, tos)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Unsupported IPv4 DSCP pedit offset");
			return -EOPNOTSUPP;
		}

		byte_idx = offsetof(struct iphdr, tos) - act->mangle.offset;
		if (byte_idx == 0) {
			byte_mask = byte0_mask;
			byte_val = byte0_val;
		} else if (byte_idx == 1) {
			byte_mask = byte1_mask;
			byte_val = byte1_val;
		} else {
			NL_SET_ERR_MSG_MOD(extack,
					   "Unsupported IPv4 DSCP pedit alignment");
			return -EOPNOTSUPP;
		}

		if (byte_mask != 0x03 || (byte_val & 0x03)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "IPv4 DSCP rewrite requires 'retain 0xfc'");
			return -EOPNOTSUPP;
		}

		*dscp = byte_val >> 2;
		if (is_ipv4)
			*is_ipv4 = true;
		return 0;
	}

	if (act->mangle.htype != FLOW_ACT_MANGLE_HDR_TYPE_IP6) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Only IPv4/IPv6 DSCP pedit offload is supported");
		return -EOPNOTSUPP;
	}

	if (n_proto != ETH_P_IPV6) {
		NL_SET_ERR_MSG_MOD(extack,
				   "IPv6 DSCP rewrite requires protocol ipv6");
		return -EOPNOTSUPP;
	}

	/* IPv6 traffic_class spans byte0[3:0] and byte1[7:4]. tc pedit
	 * encodes 'retain 0xfc' as mask f03fffff / val 0b800000 for DSCP=46.
	 */
	if (act->mangle.offset != 0 ||
	    byte0_mask != 0xf0 || byte1_mask != 0x3f ||
	    (byte0_val & 0xf0) || (byte1_val & 0x3f)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "IPv6 DSCP rewrite requires 'ip6 traffic_class ... retain 0xfc'");
		return -EOPNOTSUPP;
	}

	*dscp = ((byte0_val & 0x0f) << 2) | ((byte1_val & 0xc0) >> 6);
	if (is_ipv4)
		*is_ipv4 = false;
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
	bool trap_seen = false;
	bool police_seen = false;
	bool dscp_seen = false;
	bool dscp_ipv4_seen = false;
	bool dscp_ipv6_seen = false;
	bool csum_seen = false;
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
			if (police_seen) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Trap cannot be combined with police");
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
			/* YT9215 trap is copy-to-CPU; pair with blackhole meter for
			 * strict trap consume semantics expected by tc trap.
			 */
			action[0] |= YT921X_ACL_ACTa_METER_EN;
			action[0] &= ~YT921X_ACL_ACTa_METER_ID_M;
			action[0] |= FIELD_PREP(YT921X_ACL_ACTa_METER_ID_M,
						YT921X_ACL_METER_ID_BLACKHOLE);
			group[0].meter_id = YT921X_ACL_METER_ID_BLACKHOLE;
			trap_seen = true;
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
#if !IS_ENABLED(CONFIG_NET_DSA_YT921X_DEBUG)
			NL_SET_ERR_MSG_MOD(extack,
					   "Ingress policing offload is disabled in non-debug builds");
			return -EOPNOTSUPP;
#else
			if (trap_seen ||
			    ((action[2] & YT921X_ACL_ACTc_REDIR_EN) &&
			     (action[2] & YT921X_ACL_ACTc_REDIR_M) ==
				     YT921X_ACL_ACTc_REDIR_TRAP)) {
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
			police_seen = true;
			break;
#endif
		case FLOW_ACTION_MANGLE: {
			u8 dscp;
			bool is_ipv4;
			int res;

			res = yt921x_acl_parse_mangle_dscp(rule, act, extack, &dscp,
							   &is_ipv4);
			if (res)
				return res;

			if (dscp_seen &&
			    FIELD_GET(YT921X_ACL_ACTa_DSCP_M, action[0]) != dscp) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Multiple DSCP rewrite values are not supported");
				return -EOPNOTSUPP;
			}

			action[0] &= ~YT921X_ACL_ACTa_DSCP_M;
			action[0] |= FIELD_PREP(YT921X_ACL_ACTa_DSCP_M, dscp);
			action[0] |= YT921X_ACL_ACTa_DSCP_REPLACE;
			if (is_ipv4)
				dscp_ipv4_seen = true;
			else
				dscp_ipv6_seen = true;
			dscp_seen = true;
			break;
		}
		case FLOW_ACTION_CSUM:
			/* HW rewrites DSCP in-pipeline; IPv4 checksum is
			 * auto-updated by egress logic. Accept csum ip as a
			 * compatibility no-op for tc action chains.
			 */
			if (act->csum_flags != TCA_CSUM_UPDATE_FLAG_IPV4HDR) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Only csum ip is supported with DSCP rewrite");
				return -EOPNOTSUPP;
			}
			csum_seen = true;
			break;
		default:
			NL_SET_ERR_MSG_MOD(extack, "Action not supported");
			return -EOPNOTSUPP;
		}
	}

	if (csum_seen) {
		if (!dscp_seen) {
			NL_SET_ERR_MSG_MOD(extack,
					   "csum ip requires DSCP pedit rewrite");
			return -EOPNOTSUPP;
		}
		if (dscp_ipv6_seen || !dscp_ipv4_seen) {
			NL_SET_ERR_MSG_MOD(extack,
					   "csum ip is only valid with IPv4 DSCP rewrite");
			return -EOPNOTSUPP;
		}
	}

	if (trap_seen && !(action[0] & YT921X_ACL_ACTa_PRIO_EN)) {
		action[0] |= YT921X_ACL_ACTa_PRIO_EN;
		action[1] |= YT921X_ACL_ACTb_PRIO(YT921X_TRAP_COPP_DEFAULT_PRIO);
	}

	return 0;
}

static unsigned int
yt921x_acl_parse(struct yt921x_acl_entry *group, u16 ports_mask,
		 u32 *udfs_ctrl, unsigned int *udfs_cntp,
		 struct yt921x_priv *priv, struct dsa_switch *ds,
		 struct flow_cls_offload *cls, bool ingress)
{
	unsigned int size;
	int res;

	size = yt921x_acl_parse_key(priv, group, ports_mask, udfs_ctrl,
				    udfs_cntp, cls, ingress);
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
		u32 type;
		unsigned int udf;

		if (entries[i].cookie != cookie)
			continue;

		type = FIELD_GET(YT921X_ACL_KEYb_TYPE_M, entries[i].key[1]);
		if (type >= YT921X_ACL_TYPE_UDF0 && type <= YT921X_ACL_TYPE_UDF7) {
			udf = type - YT921X_ACL_TYPE_UDF0;
			WARN_ON(!priv->udfs_refcnt[udf]);
			if (priv->udfs_refcnt[udf])
				priv->udfs_refcnt[udf]--;
		}

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
	       unsigned int size, const u32 *udfs_ctrl, unsigned int udfs_cnt,
	       struct netlink_ext_ack *extack)
{
	struct yt921x_acl_entry *entries;
	u8 udfs_remap[YT921X_UDF_NUM] = {};
	unsigned int used_total = 0;
	unsigned int best_free;
	unsigned int offset = 0;
	unsigned int blkid;
	unsigned int free;
	unsigned int used;
	u8 ents_mask;
	int res;

	for (unsigned int i = 0; i < size; i++) {
		u32 type = FIELD_GET(YT921X_ACL_KEYb_TYPE_M, group[i].key[1]);

		if (type < YT921X_ACL_TYPE_UDF0 || type > YT921X_ACL_TYPE_UDF7)
			continue;
		if (type - YT921X_ACL_TYPE_UDF0 < udfs_cnt)
			continue;

		NL_SET_ERR_MSG_MOD(extack, "UDF selector mapping is missing");
		return -EOPNOTSUPP;
	}

	res = yt921x_acl_remap_udf(priv, udfs_ctrl, udfs_cnt, extack, udfs_remap);
	if (res)
		return res;
	for (unsigned int i = 0; i < udfs_cnt; i++) {
		unsigned int o = udfs_remap[i];

		if (priv->udfs_refcnt[o])
			continue;
		res = yt921x_reg_write(priv, YT921X_UDFn_CTRL(o), udfs_ctrl[i]);
		if (res)
			return res;
	}

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
		u32 type;

		if (entries[i].cookie)
			continue;

		entries[i] = group[j];
		type = FIELD_GET(YT921X_ACL_KEYb_TYPE_M, entries[i].key[1]);
		if (type >= YT921X_ACL_TYPE_UDF0 && type <= YT921X_ACL_TYPE_UDF7) {
			unsigned int udf = type - YT921X_ACL_TYPE_UDF0;
			u32 remap_type = YT921X_ACL_TYPE_UDF0 + udfs_remap[udf];

			entries[i].key[1] &= ~YT921X_ACL_KEYb_TYPE_M;
			entries[i].key[1] |= YT921X_ACL_KEYb_TYPE(remap_type);
		}
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

	res = yt921x_acl_commit(priv, blkid, ents_mask, BIT(offset));
	if (res)
		return res;

	for (unsigned int i = 0; i < udfs_cnt; i++) {
		unsigned int o = udfs_remap[i];

		if (!priv->udfs_refcnt[o])
			priv->udfs_ctrl[o] = udfs_ctrl[i];
		priv->udfs_refcnt[o]++;
	}

	return 0;
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
	u32 udfs_ctrl[YT921X_UDF_NUM] = {};
	const struct flow_rule *rule = flow_cls_offload_flow_rule(cls);
	const struct flow_action_entry *act;
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	unsigned int udfs_cnt = 0;
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

	size = yt921x_acl_parse(group, BIT(port), udfs_ctrl, &udfs_cnt,
				priv, ds, cls, ingress);
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

	res = yt921x_acl_add(priv, group, size, udfs_ctrl, udfs_cnt,
			     cls->common.extack);
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
