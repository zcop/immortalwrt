// SPDX-License-Identifier: GPL-2.0-only

/* Reserved ACL block steering double-tagged Q-in-Q frames by source CPU
 * conduit + standalone SVID to the owning user port. All other frames hit the
 * unmatched-permit table, which stays closed on the CPU ports while active.
 */

#include "yt921x_internal.h"

#define QINQ_ACL_BLK		YT921X_QINQ_ACL_BLOCK
#define QINQ_ACL_ENTS		YT921X_ACL_ENT_PER_BLK
#define QINQ_ACL_MAX_RULES	4

#define QINQ_CPU_PERMIT_BITS	(BIT(4) | BIT(8))

struct yt921x_qinq_rule {
	unsigned int cpu_port;
	u16 svid;
	int dst_port;
};

static const u32 yt921x_qinq_zero_action[3];

static int
yt921x_qinq_expected_cpu(struct yt921x_priv *priv, int user_port)
{
	if (priv->dt_secondary_conduit_user_mask_valid &&
	    (priv->dt_secondary_conduit_user_mask & BIT(user_port)))
		return 4;

	return 8;
}

static int
yt921x_qinq_write_slot(struct yt921x_priv *priv, unsigned int slot,
		       const struct yt921x_qinq_rule *rule)
{
	u32 action[3];
	u64 key, mask;
	int res;

	key = ((u64)(YT921X_ACL_KEYb_SPORTn(rule->cpu_port) |
		     YT921X_ACL_KEYb_TYPE(YT921X_ACL_TYPE_VLAN)) << 32) |
	      (YT921X_ACL_BINa_VLAN_CTAG_FMT(2) |
	       YT921X_ACL_BINa_VLAN_STAG_FMT(2) |
	       YT921X_ACL_BINa_VLAN_SVID(rule->svid));
	mask = ((u64)(YT921X_ACL_KEYb_SPORTS_M | YT921X_ACL_KEYb_TYPE_M) << 32) |
	       (YT921X_ACL_BINa_VLAN_CTAG_FMT_M |
		YT921X_ACL_BINa_VLAN_STAG_FMT_M |
		YT921X_ACL_BINa_VLAN_SVID_M);

	res = yt921x_reg64_write(priv, YT921X_ACLn_KEYm(QINQ_ACL_BLK, slot),
				 key);
	if (res)
		return res;
	res = yt921x_reg64_write(priv, YT921X_ACLn_MASKm(QINQ_ACL_BLK, slot),
				 mask);
	if (res)
		return res;

	action[0] = 0;
	action[1] = 0;
	action[2] = YT921X_ACL_ACTc_REDIR_EN | YT921X_ACL_ACTc_REDIR_STEER |
		    YT921X_ACL_ACTc_REDIR_DPORTn(rule->dst_port);

	return yt921x_reg96_write(priv,
				  YT921X_ACLn_ACT(YT921X_ACL_ENT_PER_BLK *
						  QINQ_ACL_BLK + slot),
				  action);
}

/* Full eight-slot commit; slots outside enable_mask are cleared. */
static int
yt921x_qinq_block_commit(struct yt921x_priv *priv, u8 enable_mask,
			 const struct yt921x_qinq_rule *rules)
{
	unsigned int i;
	u32 ctrl;
	int res;

	res = yt921x_reg_write(priv, YT921X_ACL_BLK_CMD,
			       YT921X_ACL_BLK_CMD_MODIFY |
			       YT921X_ACL_BLK_CMD_BLKID(QINQ_ACL_BLK));
	if (res)
		return res;

	/* KEEP=0: every slot is rewritten. */
	res = yt921x_reg_write(priv, YT921X_ACL_BLK_KEEP, 0);
	if (res)
		return res;

	for (i = 0; i < QINQ_ACL_ENTS; i++) {
		if (enable_mask & BIT(i)) {
			res = yt921x_qinq_write_slot(priv, i, &rules[i]);
			if (res)
				return res;
			continue;
		}

		res = yt921x_reg64_write(priv,
					 YT921X_ACLn_KEYm(QINQ_ACL_BLK, i), 0);
		if (res)
			return res;
		res = yt921x_reg64_write(priv,
					 YT921X_ACLn_MASKm(QINQ_ACL_BLK, i), 0);
		if (res)
			return res;
		res = yt921x_reg96_write(priv,
					 YT921X_ACLn_ACT(YT921X_ACL_ENT_PER_BLK *
							 QINQ_ACL_BLK + i),
					 yt921x_qinq_zero_action);
		if (res)
			return res;
	}

	ctrl = 0;
	for (i = 0; i < QINQ_ACL_ENTS; i++)
		if (enable_mask & BIT(i))
			ctrl |= YT921X_ACL_ENTRY_ENm(i) |
				YT921X_ACL_ENTRY_GRPIDm(i, i);
	res = yt921x_reg_write(priv, YT921X_ACLn_ENTRY(QINQ_ACL_BLK), ctrl);
	if (res)
		return res;

	return yt921x_reg_write(priv, YT921X_ACL_BLK_CMD,
				YT921X_ACL_BLK_CMD_BLKID(QINQ_ACL_BLK));
}

int yt921x_qinq_acl_setup(struct yt921x_priv *priv)
{
	struct yt921x_qinq_rule rules[QINQ_ACL_MAX_RULES];
	struct dsa_switch *ds = &priv->ds;
	struct dsa_port *dp;
	unsigned int nr = 0;
	unsigned int cpu_ports = 0, user_ports = 0;
	u32 entry;
	u8 mask;
	int res;

	if (!priv->qinq)
		return 0;
	if (priv->primary_cpu_port != 8 || priv->secondary_cpu_port != 4 ||
	    !priv->dt_secondary_conduit_user_mask_valid ||
	    priv->dt_secondary_conduit_user_mask != BIT(3))
		return -EINVAL;

	dsa_switch_for_each_user_port(dp, ds) {
		unsigned int cpu;
		u16 svid;

		if (!dp->cpu_dp)
			return -ENODEV;
		if (nr >= QINQ_ACL_MAX_RULES)
			return -EINVAL;
		if (dp->index < 0 || dp->index >= QINQ_ACL_MAX_RULES)
			return -EINVAL;

		/* DSA initially assigns every user port its first local CPU.
		 * netifd applies the DT-pinned split after switch setup. Match
		 * that final policy here; port_change_conduit rejects other moves.
		 */
		cpu = yt921x_qinq_expected_cpu(priv, dp->index);
		if (!dsa_is_cpu_port(ds, cpu))
			return -EINVAL;

		svid = dsa_tag_8021q_standalone_vid(dp);
		if (!svid)
			return -EINVAL;

		rules[nr].cpu_port = cpu;
		rules[nr].svid = svid;
		rules[nr].dst_port = dp->index;
		nr++;
		cpu_ports |= BIT(cpu);
		user_ports |= BIT(dp->index);
	}

	if (nr != QINQ_ACL_MAX_RULES || user_ports != GENMASK(3, 0) ||
	    cpu_ports != QINQ_CPU_PERMIT_BITS)
		return -EINVAL;

	res = yt921x_reg_read(priv, YT921X_ACLn_ENTRY(QINQ_ACL_BLK), &entry);
	if (res)
		return res;
	if (entry)
		return -EBUSY;

	/* Fail closed on the CPU ports before any rule is enabled. */
	res = yt921x_reg_clear_bits(priv, YT921X_ACL_PERMIT_UNMATCH,
				    QINQ_CPU_PERMIT_BITS);
	if (res)
		return res;

	mask = (u8)(BIT(nr) - 1);

	res = yt921x_qinq_block_commit(priv, mask, rules);
	if (res) {
		/* A partially applied commit cannot be rolled back safely; keep
		 * the CPU ports denied and best-effort disable the block.
		 */
		(void)yt921x_qinq_block_commit(priv, 0, NULL);
		return res;
	}

	return 0;
}

int yt921x_qinq_acl_teardown(struct yt921x_priv *priv)
{
	int res;

	if (!priv->qinq)
		return 0;

	res = yt921x_qinq_block_commit(priv, 0, NULL);
	if (res)
		return res;

	return yt921x_reg_set_bits(priv, YT921X_ACL_PERMIT_UNMATCH,
				   QINQ_CPU_PERMIT_BITS);
}
