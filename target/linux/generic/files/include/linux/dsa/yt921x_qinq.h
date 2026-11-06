/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __LINUX_DSA_YT921X_QINQ_H
#define __LINUX_DSA_YT921X_QINQ_H

#include <linux/if_vlan.h>
#include <linux/dsa/8021q.h>
#include <net/dsa.h>

/* Customer VIDs share the VTU with tag_8021q inner-domain VIDs. */
static inline bool dsa_yt921x_qinq_customer_vid_valid(struct dsa_switch *ds,
						      u16 vid)
{
	struct dsa_port *dp;
	unsigned int bridge_num;

	if (!vid || vid >= VLAN_VID_MASK)
		return false;

	dsa_switch_for_each_user_port(dp, ds)
		if (vid == dsa_tag_8021q_standalone_vid(dp))
			return false;

	for (bridge_num = 1; bridge_num <= ds->max_num_bridges; bridge_num++)
		if (vid == dsa_tag_8021q_bridge_vid(bridge_num))
			return false;

	return true;
}

/* Caller holds RCU. Only a registered one-level 802.1Q upper on a
 * standalone port admits customer-tagged traffic outside a bridge. The
 * VLAN core publishes this device after driver setup and unpublishes it
 * before driver deletion, so stale NSS leaves alone never grant admission.
 */
static inline bool dsa_yt921x_qinq_direct_vid_valid(struct dsa_port *dp, u16 vid)
{
	struct net_device *upper;

	if (!dsa_yt921x_qinq_customer_vid_valid(dp->ds, vid) ||
	    !(dp->user->features & NETIF_F_HW_VLAN_CTAG_FILTER) ||
	    dsa_port_bridge_dev_get(dp) || netif_is_bridge_port(dp->user) ||
	    netif_is_lag_port(dp->user))
		return false;

	upper = __vlan_find_dev_deep_rcu(dp->user, htons(ETH_P_8021Q), vid);
	return upper && vlan_dev_real_dev(upper) == dp->user;
}

#endif /* __LINUX_DSA_YT921X_QINQ_H */
