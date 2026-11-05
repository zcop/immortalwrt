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

#endif /* __LINUX_DSA_YT921X_QINQ_H */
