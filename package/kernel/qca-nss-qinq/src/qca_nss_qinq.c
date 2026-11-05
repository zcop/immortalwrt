// SPDX-License-Identifier: GPL-2.0-only
/*
 * Ported from the preserved Kuncy7/QinQ reference to qosmio native NSS-DP.
 * The stmmac takeover controller is not used. Only YT921x QinQ is admitted.
 *
 * qca-nss-qinq: NSS firmware VLAN interfaces for the user ports of a DSA
 * switch driven by a tag_8021q tagger (qca-8021q, rtl8365mb-8021q).
 *
 * Single-tag mode (port_id_tpid=0): the switch talks to its CPU port in
 * plain 802.1Q: a standalone port carries its own VID (3072 + port), the
 * ports of a VLAN-unaware bridge share the bridge's VID (3088 + n) and the
 * switch forwards between them on its own. The NSS firmware parses 802.1Q
 * natively, so each of those VIDs can be a firmware VLAN interface hanging
 * off the conduit's phys_if - what qca-nss-vlan does for netifd's
 * eth0.<vid>, except that the netdev bound to the node is the DSA user
 * port itself. ECM treats a DSA user port as plain ethernet and only
 * needs an NSS interface number for it to write rules; this is where the
 * number comes from. RX and TX are untouched: exceptions come back on the
 * conduit with the tag on and go through the tagger, host TX goes
 * lan1 -> tagger -> conduit -> phys_if.
 *
 * Port-ID mode (port_id_tpid=0x8100 or 0x88a8, matching the QinQ tagger):
 * the stack on the conduit is outer tag = the physical port's standalone
 * VID, inner tag = the customer VID of a VLAN-aware bridge, or the
 * tag_8021q bridge/standalone VID otherwise. The outer VID names the exact
 * user port, not merely a VLAN domain, so every port gets its own firmware
 * chain: a root node carrying the outer tag on the port's phys_if, and one
 * leaf per inner VID with the root as next_hop. All nodes are bound to the
 * port's own netdev. ECM gets the root through
 * qca_nss_qinq_root_if_num(conduit, svid) and a leaf through
 * qca_nss_qinq_lookup(conduit, svid, vid), and puts the tag pair in the
 * flow's VLAN rule slots; the firmware parses and generates the stack on
 * the accelerated path.
 *
 * A bridge's single-tag node is bound to the port the tagger's imprecise
 * RX delivers to (dsa_tag_8021q_find_user), so the host bridge's FDB and
 * ECM's interface hierarchy resolve to the one port that has the number.
 * The set of nodes is recomputed from the live topology after every
 * relevant netdev event; a node whose VID, port or phys_if no longer
 * matches is torn down and rebuilt.
 *
 * A VLAN-aware bridge is different: its frames cross the conduit with the
 * bridge's own VIDs (the CPU port is a tagged member of every bridge VLAN),
 * not with a tag_8021q one, and a port carries as many VIDs as it is a
 * member of. Such a bridge gets one node per (bridge VID, phys_if) in
 * single-tag mode, bound to the first member port for the node's MAC and
 * MTU. A netdev cannot name the node then - one port, several VIDs - so
 * ECM asks for it by VID through qca_nss_qinq_root_if_num() instead of by netdev.
 * Bridge VLAN changes do not raise netdev events; the switchdev notifier
 * covers them.
 *
 * The firmware hands out dynamic interface numbers next-fit and does not
 * reuse a freed slot until the cursor wraps, which takes a moment: after
 * ~118 allocations since boot an allocation can fail for a few seconds
 * (measured on a GL-B3000, 2026-09-11). A failed allocation is retried
 * with a delay instead of being given up on.
 */
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/if_bridge.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/workqueue.h>
#include <linux/rtnetlink.h>
#include <linux/dsa/8021q.h>
#include <net/dsa.h>
#include <net/switchdev.h>
#include <nss_api_if.h>

/* exported by net/dsa/tag_8021q.c but declared only in its private header */
struct net_device *dsa_tag_8021q_find_user(struct net_device *conduit,
					   int source_port, int switch_id,
					   int vid, int vbid);

/* for ECM (qca-nss-ecm resolves them with symbol_get(), no link dependency) */
int qca_nss_qinq_root_if_num(struct net_device *conduit, u16 vid);
int qca_nss_qinq_lookup(struct net_device *conduit, u16 svid, u16 vid);

/* This native adapter accepts only the YT921x 0x88a8 QinQ transport. */
static unsigned int port_id_tpid = ETH_P_8021AD;
module_param(port_id_tpid, uint, 0444);
MODULE_PARM_DESC(port_id_tpid,
		 "YT921x QinQ outer TPID; must be 0x88a8");

/*
 * A port-ID port costs a root plus one leaf per inner VID, and the firmware
 * pool of 128 dynamic interfaces is shared with PPPoE and VAPs, so the glue
 * stops well short of it.
 */
#define DSA_NSS_MAX_NODES	64
#define DSA_NSS_COALESCE_MS	100
#define DSA_NSS_RETRY_MS	2000
#define DSA_NSS_MAX_RETRIES	30

struct dsa_nss_node {
	bool ready;			/* Published only after firmware configuration ACK */
	u16 svid;			/* outer port-ID VID, 0 = single-tag node */
	u16 vid;			/* node VID, 0 = port root in port-ID mode */
	int parent_if_num;		/* root node of a leaf, -1 otherwise */
	int if_num;			/* NSS dynamic interface, -1 = none */
	int phys_if;			/* conduit's NSS physical interface */
	struct net_device *ndev;	/* bound DSA user port, held */
	u8 mac[ETH_ALEN];
	unsigned int mtu;
};

struct dsa_nss_want {
	u16 svid;
	u16 vid;
	int phys_if;
	struct net_device *ndev;
};

static struct dsa_nss_node dsa_nss_nodes[DSA_NSS_MAX_NODES];
static DEFINE_MUTEX(dsa_nss_lock);	/* nodes[] */
static DEFINE_SPINLOCK(dsa_nss_table_lock);	/* if_num publish, for lookups from softirq */
static struct delayed_work dsa_nss_work;
static unsigned int dsa_nss_retries;
static bool dsa_nss_waiting_phys;
static bool dsa_nss_reconfiguring;
static bool dsa_nss_stopping;
static atomic_t dsa_nss_changed = ATOMIC_INIT(1);

/* Optional ECM module: invalidate accelerated rules before topology changes. */
void ecm_nss_qinq_defunct_all(void);

static void dsa_nss_defunct(void)
{
	void (*fn)(void) = symbol_get(ecm_nss_qinq_defunct_all);

	if (fn) {
		fn();
		symbol_put(ecm_nss_qinq_defunct_all);
	}
}

static atomic_t dsa_nss_resyncs = ATOMIC_INIT(0);
static atomic_t dsa_nss_alloc_failures = ATOMIC_INIT(0);
static atomic_t dsa_nss_creates = ATOMIC_INIT(0);
static atomic_t dsa_nss_destroys = ATOMIC_INIT(0);
static struct dentry *dsa_nss_dentry;

static bool dsa_nss_is_root(const struct dsa_nss_node *n)
{
	return n->vid == 0;
}

/*
 * ===== firmware node create / destroy (dsa_nss_lock held) =====
 */

static void dsa_nss_node_destroy(struct dsa_nss_node *n, const char *why)
{
	if (n->if_num < 0)
		return;

	/* Stop new lookups before unregistering or returning the firmware slot. */
	spin_lock_bh(&dsa_nss_table_lock);
	n->ready = false;
	spin_unlock_bh(&dsa_nss_table_lock);

	if (n->svid)
		pr_info("qca-nss-qinq: %s: drop vlan node if_num %d svid %u vid %u (%s)\n",
			netdev_name(n->ndev), n->if_num, n->svid, n->vid, why);
	else
		pr_info("qca-nss-qinq: %s: drop vlan node if_num %d vid %u (%s)\n",
			netdev_name(n->ndev), n->if_num, n->vid, why);
	nss_unregister_vlan_if(n->if_num);
	if (nss_dynamic_interface_dealloc_node(n->if_num,
			NSS_DYNAMIC_INTERFACE_TYPE_VLAN) != NSS_TX_SUCCESS)
		pr_warn("qca-nss-qinq: %s: dealloc of if_num %d failed\n",
			netdev_name(n->ndev), n->if_num);
	dev_put(n->ndev);
	n->ndev = NULL;
	spin_lock_bh(&dsa_nss_table_lock);
	n->if_num = -1;
	n->parent_if_num = -1;
	spin_unlock_bh(&dsa_nss_table_lock);
	atomic_inc(&dsa_nss_destroys);
}

/* Leaves first: the firmware unhooks a parent only after its children. */
static void dsa_nss_destroy_nodes(struct net_device *dev, const char *why)
{
	int i, pass;

	for (pass = 0; pass < 2; pass++)
		for (i = 0; i < DSA_NSS_MAX_NODES; i++) {
			struct dsa_nss_node *n = &dsa_nss_nodes[i];

			if (n->if_num < 0 || dsa_nss_is_root(n) != pass)
				continue;
			if (dev && n->ndev != dev)
				continue;
			dsa_nss_node_destroy(n, why);
		}
}

/* -EAGAIN: no interface number right now, worth a retry; -EIO: hard fail */
static int dsa_nss_node_create(struct dsa_nss_node *n,
			       const struct dsa_nss_want *w, int parent_if_num)
{
	struct net_device *ndev = w->ndev;
	u32 tag;
	int if_num, parent;

	if_num = nss_dynamic_interface_alloc_node(NSS_DYNAMIC_INTERFACE_TYPE_VLAN);
	if (if_num < 0) {
		atomic_inc(&dsa_nss_alloc_failures);
		return -EAGAIN;
	}

	if (w->svid && w->vid)
		tag = (ETH_P_8021Q << 16) | w->vid;
	else if (w->svid)
		tag = (port_id_tpid << 16) | w->svid;
	else
		tag = (ETH_P_8021Q << 16) | w->vid;
	parent = parent_if_num >= 0 ? parent_if_num : w->phys_if;

	n->svid = w->svid;
	n->vid = w->vid;
	n->parent_if_num = parent_if_num;
	n->phys_if = w->phys_if;
	n->ndev = ndev;
	dev_hold(ndev);
	ether_addr_copy(n->mac, ndev->dev_addr);
	n->mtu = ndev->mtu;

	if (!nss_register_vlan_if(if_num, NULL, ndev, 0, n)) {
		pr_warn("qca-nss-qinq: %s: register of if_num %d failed\n",
			netdev_name(ndev), if_num);
		nss_dynamic_interface_dealloc_node(if_num,
					NSS_DYNAMIC_INTERFACE_TYPE_VLAN);
		dev_put(ndev);
		n->ndev = NULL;
		n->parent_if_num = -1;
		return -EIO;
	}
	spin_lock_bh(&dsa_nss_table_lock);
	n->if_num = if_num;
	spin_unlock_bh(&dsa_nss_table_lock);

	if (nss_vlan_tx_set_mac_addr_msg(if_num, n->mac) != NSS_TX_SUCCESS ||
	    nss_vlan_tx_set_mtu_msg(if_num, n->mtu) != NSS_TX_SUCCESS ||
	    nss_vlan_tx_add_tag_msg(if_num, tag, parent, w->phys_if) !=
			NSS_TX_SUCCESS) {
		pr_warn("qca-nss-qinq: %s: firmware refused vlan node if_num %d tag %#x on phys_if %d parent %d\n",
			netdev_name(ndev), if_num, tag, w->phys_if, parent);
		dsa_nss_node_destroy(n, "setup failed");
		return -EIO;
	}

	spin_lock_bh(&dsa_nss_table_lock);
	n->ready = true;
	spin_unlock_bh(&dsa_nss_table_lock);

	if (w->svid)
		pr_info("qca-nss-qinq: %s: vlan node if_num %d svid %u vid %u parent %d phys_if %d\n",
			netdev_name(ndev), if_num, w->svid, w->vid,
			parent_if_num, w->phys_if);
	else
		pr_info("qca-nss-qinq: %s: vlan node if_num %d vid %u phys_if %d\n",
			netdev_name(ndev), if_num, w->vid, w->phys_if);
	atomic_inc(&dsa_nss_creates);
	return 0;
}

/*
 * ===== desired topology (rtnl held) =====
 */

/* A port whose switch runs a tag_8021q tagger (qca-8021q on qca8k,
 * rtl8365mb-8021q on rtl8365mb): the switch talks to the conduit in plain
 * 802.1Q, and the VIDs are the framework's regardless of the switch.
 */
static bool dsa_nss_port_is_8021q(const struct dsa_port *dp)
{
	return dp->cpu_dp && dp->ds && dp->ds->tag_8021q_ctx &&
	       dp->ds->dst && dp->ds->dst->tag_ops &&
	       dp->ds->dst->tag_ops->proto == DSA_TAG_PROTO_YT921X_QINQ;
}

/* The tag_8021q DSA port behind @dev: @dev itself, or the real device of an
 * 802.1Q upper of one (wan.35). NULL for anything else.
 */
static struct dsa_port *dsa_nss_port_of(struct net_device *dev, bool *upper)
{
	struct dsa_port *dp;

	*upper = false;
	if (is_vlan_dev(dev)) {
		dev = vlan_dev_real_dev(dev);
		*upper = true;
	}
	if (!dsa_user_dev_check(dev))
		return NULL;
	dp = dsa_port_from_netdev(dev);
	if (IS_ERR_OR_NULL(dp) || !dsa_nss_port_is_8021q(dp))
		return NULL;
	return dp;
}

/* One entry per (svid, vid, phys_if). False when the table is full. */
static bool dsa_nss_want_add(struct dsa_nss_want *want, int *n, int max,
			     u16 svid, u16 vid, int phys_if,
			     struct net_device *ndev)
{
	int i;

	for (i = 0; i < *n; i++)
		if (want[i].svid == svid && want[i].vid == vid &&
		    want[i].phys_if == phys_if)
			return true;	/* the bridge already has its entry */
	if (*n == max) {
		pr_warn_once("qca-nss-qinq: more than %d nodes wanted, rest ignored\n", max);
		return false;
	}
	want[*n].svid = svid;
	want[*n].vid = vid;
	want[*n].phys_if = phys_if;
	want[*n].ndev = ndev;
	(*n)++;
	return true;
}

/*
 * Port-ID mode: the outer tag is the physical port's standalone VID, so
 * every port gets its own root, and the leaf VIDs are the ones the QinQ
 * tagger puts inside. A VLAN upper has no wire path in this transport (the
 * tagger refuses tagged frames outside a VLAN-aware bridge), so uppers are
 * skipped.
 */
static int dsa_nss_collect_port_id(struct dsa_nss_want *want, int max)
{
	struct net_device *dev;
	int n = 0;

	for_each_netdev(&init_net, dev) {
		struct net_device *conduit;
		struct dsa_port *dp;
		int phys_if;
		bool upper;
		u16 svid;

		dp = dsa_nss_port_of(dev, &upper);
		if (!dp || upper)
			continue;

		conduit = dp->cpu_dp->conduit;
		phys_if = nss_cmn_get_interface_number_by_dev(conduit);
		if (phys_if < 0 || phys_if >= NSS_MAX_PHYSICAL_INTERFACES) {
			dsa_nss_waiting_phys = true;
			continue;
		}	/* conduit not armed as a firmware port */

		svid = dsa_tag_8021q_standalone_vid(dp);
		if (!dsa_nss_want_add(want, &n, max, svid, 0, phys_if, dev))
			return n;

		if (dp->bridge &&
		    br_vlan_enabled(dsa_port_bridge_dev_get(dp))) {
			/* one leaf per customer VID the port carries */
			struct bridge_vlan_info vinfo;
			u16 vid;

			for (vid = 1; vid < VLAN_N_VID &&
				      !vid_is_dsa_8021q(vid); vid++) {
				if (br_vlan_get_info(dev, vid, &vinfo))
					continue;
				if (!dsa_nss_want_add(want, &n, max, svid, vid,
						      phys_if, dev))
					return n;
			}
		} else if (dp->bridge) {
			unsigned int vbid = dsa_port_bridge_num_get(dp);

			/* the QinQ tagger keys the inner tag on bridge_num */
			if (vbid &&
			    !dsa_nss_want_add(want, &n, max, svid,
					      dsa_tag_8021q_bridge_vid(vbid),
					      phys_if, dev))
				return n;
		} else {
			/* the tagger's inner tag is the port's own VID */
			if (!dsa_nss_want_add(want, &n, max, svid, svid,
					      phys_if, dev))
				return n;
		}
	}
	return n;
}

/*
 * One entry per standalone tag_8021q port, one per VLAN-unaware bridge on
 * such ports, one per VLAN of a VLAN-aware bridge on such ports, and one
 * per 802.1Q upper of such a port (its own VID - the switch carries that
 * VLAN through, the tagger leaves it alone, and it is on the conduit as a
 * single tag, exactly like eth0.<vid> on a trunk). Returns the count.
 */
static int dsa_nss_collect_single(struct dsa_nss_want *want, int max)
{
	struct net_device *dev;
	int n = 0;

	for_each_netdev(&init_net, dev) {
		struct net_device *conduit, *bound;
		struct dsa_port *dp;
		int phys_if;
		bool upper;
		u16 vid;

		dp = dsa_nss_port_of(dev, &upper);
		if (!dp)
			continue;

		conduit = dp->cpu_dp->conduit;
		phys_if = nss_cmn_get_interface_number_by_dev(conduit);
		if (phys_if < 0 || phys_if >= NSS_MAX_PHYSICAL_INTERFACES) {
			dsa_nss_waiting_phys = true;
			continue;
		}	/* conduit not armed as a firmware port */

		if (upper) {
			vid = vlan_dev_vlan_id(dev);
			bound = dev;
		} else if (dp->bridge &&
			   br_vlan_enabled(dsa_port_bridge_dev_get(dp))) {
			/* A VLAN-aware bridge's frames carry the bridge's own
			 * VIDs on the conduit: one node per VID the port is a
			 * member of, the first port found in a VLAN lending
			 * the node its MAC and MTU. The tag_8021q range is the
			 * tagger's and never a bridge VLAN.
			 */
			struct bridge_vlan_info vinfo;

			for (vid = 1; vid < VLAN_N_VID && !vid_is_dsa_8021q(vid); vid++) {
				if (br_vlan_get_info(dev, vid, &vinfo))
					continue;
				if (!dsa_nss_want_add(want, &n, max, 0, vid,
						      phys_if, dev))
					return n;
			}
			continue;
		} else if (dp->bridge && dp->bridge->tx_fwd_offload) {
			unsigned int vbid = dsa_port_bridge_num_get(dp);

			vid = dsa_tag_8021q_bridge_vid(vbid);
			/* the port imprecise RX delivers to, if any is live */
			bound = dsa_tag_8021q_find_user(conduit, -1, -1, vid, vbid);
			if (!bound)
				bound = dev;
		} else {
			vid = dsa_tag_8021q_standalone_vid(dp);
			bound = dev;
		}

		if (!dsa_nss_want_add(want, &n, max, 0, vid, phys_if, bound))
			break;
	}
	return n;
}

static int dsa_nss_collect(struct dsa_nss_want *want, int max)
{
	if (port_id_tpid)
		return dsa_nss_collect_port_id(want, max);
	return dsa_nss_collect_single(want, max);
}

/* Called with the table spinlock held. */
static bool dsa_nss_parent_ready(const struct dsa_nss_node *leaf)
{
	int i;

	if (!leaf->vid)
		return true;
	for (i = 0; i < DSA_NSS_MAX_NODES; i++) {
		const struct dsa_nss_node *root = &dsa_nss_nodes[i];

		if (root->ready && !root->vid && root->if_num >= 0 &&
		    root->if_num == leaf->parent_if_num &&
		    root->svid == leaf->svid && root->phys_if == leaf->phys_if)
			return true;
	}
	return false;
}

static int dsa_nss_lookup(int phys_if, u16 svid, u16 vid)
{
	int i, if_num = -1;

	spin_lock_bh(&dsa_nss_table_lock);
	if (dsa_nss_reconfiguring) {
		spin_unlock_bh(&dsa_nss_table_lock);
		return -1;
	}
	for (i = 0; i < DSA_NSS_MAX_NODES; i++) {
		const struct dsa_nss_node *n = &dsa_nss_nodes[i];

		if (n->ready && dsa_nss_parent_ready(n) && n->if_num >= 0 && n->svid == svid && n->vid == vid &&
		    n->phys_if == phys_if) {
			if_num = n->if_num;
			break;
		}
	}
	spin_unlock_bh(&dsa_nss_table_lock);
	return if_num;
}

/*
 * qca_nss_qinq_root_if_num()
 *	The port node of @vid on the phys_if of @conduit, -1 when there is none.
 *
 * Single-tag mode: @vid is the tag the switch puts on the conduit. Port-ID
 * mode: @vid is the outer port ID (the port's standalone VID) and the
 * result is that port's root node; an inner-VLAN flow asks for its leaf
 * through qca_nss_qinq_lookup().
 *
 * For ECM, callable from softirq.
 */
int qca_nss_qinq_root_if_num(struct net_device *conduit, u16 vid)
{
	int phys_if;

	phys_if = nss_cmn_get_interface_number_by_dev(conduit);
	if (phys_if < 0)
		return -1;
	return dsa_nss_lookup(phys_if, port_id_tpid ? vid : 0,
			      port_id_tpid ? 0 : vid);
}
EXPORT_SYMBOL_GPL(qca_nss_qinq_root_if_num);

/*
 * qca_nss_qinq_lookup()
 *	The leaf of @vid inside port @svid on @conduit's phys_if, -1 when
 *	there is none. @vid == 0 returns the port root.
 *
 * Port-ID mode only: a VLAN-aware bridge port has one node per customer
 * VID, so the flow's VLAN pair, not the netdev alone, names its node.
 */
int qca_nss_qinq_lookup(struct net_device *conduit, u16 svid, u16 vid)
{
	int phys_if;

	if (!port_id_tpid)
		return -1;
	phys_if = nss_cmn_get_interface_number_by_dev(conduit);
	if (phys_if < 0)
		return -1;
	return dsa_nss_lookup(phys_if, svid, vid);
}
EXPORT_SYMBOL_GPL(qca_nss_qinq_lookup);

/*
 * Resolve an exact leaf. A zero customer VID is accepted only if this
 * port has one ready leaf; never guess among several customer VLANs.
 */
int qca_nss_qinq_resolve(struct net_device *conduit, u16 svid, u16 *vid);
int qca_nss_qinq_resolve(struct net_device *conduit, u16 svid, u16 *vid)
{
	int phys_if = nss_cmn_get_interface_number_by_dev(conduit);
	int result = -1, i;
	u16 selected = 0;

	if (phys_if < 0)
		return -1;
	spin_lock_bh(&dsa_nss_table_lock);
	if (dsa_nss_reconfiguring)
		goto out;
	for (i = 0; i < DSA_NSS_MAX_NODES; i++) {
		struct dsa_nss_node *n = &dsa_nss_nodes[i];

		if (!n->ready || !dsa_nss_parent_ready(n) || n->if_num < 0 || !n->vid ||
		    n->phys_if != phys_if || n->svid != svid ||
		    (*vid && n->vid != *vid))
			continue;
		if (result >= 0 && selected != n->vid) {
			result = -1;
			goto out;
		}
		selected = n->vid;
		result = n->if_num;
	}
	if (result >= 0)
		*vid = selected;
out:
	spin_unlock_bh(&dsa_nss_table_lock);
	return result;
}
EXPORT_SYMBOL_GPL(qca_nss_qinq_resolve);

static struct dsa_nss_node *dsa_nss_find_root(u16 svid, int phys_if)
{
	int i;

	for (i = 0; i < DSA_NSS_MAX_NODES; i++) {
		struct dsa_nss_node *n = &dsa_nss_nodes[i];

		if (n->ready && n->if_num >= 0 && dsa_nss_is_root(n) &&
		    n->svid == svid && n->phys_if == phys_if)
			return n;
	}
	return NULL;
}

static bool dsa_nss_node_matches(const struct dsa_nss_node *n,
				 const struct dsa_nss_want *w)
{
	const struct dsa_nss_node *root;

	if (n->if_num < 0 || n->svid != w->svid || n->vid != w->vid ||
	    n->phys_if != w->phys_if || n->ndev != w->ndev ||
	    n->mtu != w->ndev->mtu ||
	    !ether_addr_equal(n->mac, w->ndev->dev_addr))
		return false;

	if (!n->svid || !n->vid)
		return true;	/* single-tag node or port root */

	/* a leaf is only good while its root is the published one */
	root = dsa_nss_find_root(n->svid, n->phys_if);
	return root && root->if_num == n->parent_if_num;
}

static bool dsa_nss_root_wanted(const struct dsa_nss_node *root,
				const struct dsa_nss_want *want, int nwant)
{
	int j;

	for (j = 0; j < nwant; j++)
		if (want[j].vid == 0 && dsa_nss_node_matches(root, &want[j]))
			return true;
	return false;
}

/* returns true when something could not be allocated and a retry is due */
static bool dsa_nss_resync(void)
{
	struct dsa_nss_want want[DSA_NSS_MAX_NODES];
	bool again = false;
	int nwant, i, j;

	atomic_inc(&dsa_nss_resyncs);
	rtnl_lock();
	mutex_lock(&dsa_nss_lock);

	spin_lock_bh(&dsa_nss_table_lock);
	dsa_nss_reconfiguring = true;
	spin_unlock_bh(&dsa_nss_table_lock);
	if (atomic_xchg(&dsa_nss_changed, 0))
		dsa_nss_defunct();
	dsa_nss_waiting_phys = false;
	nwant = dsa_nss_collect(want, DSA_NSS_MAX_NODES);
	again = dsa_nss_waiting_phys;

	/* tear down stale leaves before their roots */
	for (i = 0; i < DSA_NSS_MAX_NODES; i++) {
		struct dsa_nss_node *n = &dsa_nss_nodes[i];
		struct dsa_nss_node *root;

		if (n->if_num < 0 || dsa_nss_is_root(n))
			continue;
		for (j = 0; j < nwant; j++)
			if (dsa_nss_node_matches(n, &want[j]))
				break;
		if (j < nwant) {
			if (!n->svid)
				continue;
			root = dsa_nss_find_root(n->svid, n->phys_if);
			if (root && dsa_nss_root_wanted(root, want, nwant))
				continue;
		}
		dsa_nss_node_destroy(n, "topology changed");
	}

	for (i = 0; i < DSA_NSS_MAX_NODES; i++) {
		struct dsa_nss_node *n = &dsa_nss_nodes[i];

		if (n->if_num < 0 || !dsa_nss_is_root(n) ||
		    dsa_nss_root_wanted(n, want, nwant))
			continue;
		for (j = 0; j < DSA_NSS_MAX_NODES; j++) {
			struct dsa_nss_node *l = &dsa_nss_nodes[j];

			if (l->if_num >= 0 && !dsa_nss_is_root(l) &&
			    l->parent_if_num == n->if_num)
				dsa_nss_node_destroy(l, "parent rebuilt");
		}
		dsa_nss_node_destroy(n, "topology changed");
	}

	/* build roots before their leaves */
	for (j = 0; j < nwant; j++) {
		struct dsa_nss_node *free = NULL;
		int ret;

		if (want[j].vid)
			continue;
		for (i = 0; i < DSA_NSS_MAX_NODES; i++) {
			struct dsa_nss_node *n = &dsa_nss_nodes[i];

			if (n->if_num >= 0) {
				if (dsa_nss_node_matches(n, &want[j]))
					break;
				continue;
			}
			if (!free)
				free = n;
		}
		if (i < DSA_NSS_MAX_NODES)
			continue;	/* already there */
		if (!free)
			break;
		ret = dsa_nss_node_create(free, &want[j], -1);
		if (ret)
			again = true;
	}

	for (j = 0; j < nwant; j++) {
		struct dsa_nss_node *free = NULL, *root = NULL;
		int parent = -1, ret;

		if (!want[j].vid)
			continue;
		if (want[j].svid) {
			root = dsa_nss_find_root(want[j].svid, want[j].phys_if);
			if (!root) {
				again = true;	/* its root did not come up */
				continue;
			}
			parent = root->if_num;
		}

		for (i = 0; i < DSA_NSS_MAX_NODES; i++) {
			struct dsa_nss_node *n = &dsa_nss_nodes[i];

			if (n->if_num >= 0) {
				if (dsa_nss_node_matches(n, &want[j]))
					break;
				continue;
			}
			if (!free)
				free = n;
		}
		if (i < DSA_NSS_MAX_NODES)
			continue;	/* already there */
		if (!free)
			break;
		ret = dsa_nss_node_create(free, &want[j], parent);
		if (ret)
			again = true;
	}

	spin_lock_bh(&dsa_nss_table_lock);
	dsa_nss_reconfiguring = again;
	spin_unlock_bh(&dsa_nss_table_lock);
	mutex_unlock(&dsa_nss_lock);
	rtnl_unlock();
	return again;
}

static void dsa_nss_work_fn(struct work_struct *work)
{
	if (READ_ONCE(dsa_nss_stopping))
		return;
	if (dsa_nss_resync()) {
		if (!READ_ONCE(dsa_nss_stopping) &&
		    dsa_nss_retries++ < DSA_NSS_MAX_RETRIES) {
			pr_info("qca-nss-qinq: no free firmware interface number, retry %u in %d ms\n",
				dsa_nss_retries, DSA_NSS_RETRY_MS);
			schedule_delayed_work(&dsa_nss_work,
					      msecs_to_jiffies(DSA_NSS_RETRY_MS));
		} else {
			pr_warn("qca-nss-qinq: giving up after %u retries; write 1 to debugfs 'resync' to try again\n",
				dsa_nss_retries);
		}
		return;
	}
	dsa_nss_retries = 0;
}

static void dsa_nss_schedule(void)
{
	if (READ_ONCE(dsa_nss_stopping))
		return;
	/* Stop new rule lookups immediately, before the coalesced work runs. */
	spin_lock_bh(&dsa_nss_table_lock);
	dsa_nss_reconfiguring = true;
	spin_unlock_bh(&dsa_nss_table_lock);
	atomic_set(&dsa_nss_changed, 1);
	WRITE_ONCE(dsa_nss_retries, 0);
	mod_delayed_work(system_wq, &dsa_nss_work,
			 msecs_to_jiffies(DSA_NSS_COALESCE_MS));
}

/*
 * ===== netdev events =====
 */

static int dsa_nss_netdev_event(struct notifier_block *nb, unsigned long event,
				void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	bool upper;

	if (!dsa_nss_port_of(dev, &upper))
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_UNREGISTER:
		/*
		 * The port is going away now; release its nodes here, with
		 * rtnl held, rather than from the work item, so the netdev
		 * is not kept alive by our reference.
		 */
		mutex_lock(&dsa_nss_lock);
		dsa_nss_defunct();
		dsa_nss_destroy_nodes(dev, "port unregistered");
		mutex_unlock(&dsa_nss_lock);
		fallthrough;
	case NETDEV_REGISTER:
	case NETDEV_UP:
	case NETDEV_DOWN:
	case NETDEV_CHANGE:
	case NETDEV_CHANGEUPPER:
	case NETDEV_CHANGEADDR:
	case NETDEV_CHANGEMTU:
		dsa_nss_schedule();
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block dsa_nss_netdev_nb = {
	.notifier_call = dsa_nss_netdev_event,
};

/*
 * ===== switchdev events =====
 *
 * A bridge VLAN added to or removed from a port, and vlan_filtering
 * toggled on a bridge, reach the switch driver through switchdev and raise
 * no netdev event; both change the nodes a VLAN-aware bridge wants.
 */
static int dsa_nss_switchdev_event(struct notifier_block *nb,
				   unsigned long event, void *ptr)
{
	struct net_device *dev = switchdev_notifier_info_to_dev(ptr);
	bool upper;

	switch (event) {
	case SWITCHDEV_PORT_OBJ_ADD:
	case SWITCHDEV_PORT_OBJ_DEL: {
		const struct switchdev_notifier_port_obj_info *info = ptr;

		if (info->obj->id != SWITCHDEV_OBJ_ID_PORT_VLAN)
			return NOTIFY_DONE;
		break;
	}
	case SWITCHDEV_PORT_ATTR_SET: {
		const struct switchdev_notifier_port_attr_info *info = ptr;

		if (info->attr->id != SWITCHDEV_ATTR_ID_BRIDGE_VLAN_FILTERING)
			return NOTIFY_DONE;
		break;
	}
	default:
		return NOTIFY_DONE;
	}

	/* the port itself, or a bridge (its own VLANs, vlan_filtering) */
	if (dsa_nss_port_of(dev, &upper) || netif_is_bridge_master(dev))
		dsa_nss_schedule();
	return NOTIFY_DONE;
}

static struct notifier_block dsa_nss_switchdev_nb = {
	.notifier_call = dsa_nss_switchdev_event,
};

/*
 * ===== debugfs =====
 */

static int dsa_nss_status_show(struct seq_file *m, void *v)
{
	int i, nodes = 0;

	mutex_lock(&dsa_nss_lock);
	for (i = 0; i < DSA_NSS_MAX_NODES; i++) {
		struct dsa_nss_node *n = &dsa_nss_nodes[i];

		if (n->if_num < 0)
			continue;
		seq_printf(m, "%-16s if_num=%d svid=%u vid=%u parent=%d phys_if=%d mtu=%u mac=%pM\n",
			   netdev_name(n->ndev), n->if_num, n->svid, n->vid,
			   n->parent_if_num, n->phys_if, n->mtu, n->mac);
		nodes++;
	}
	mutex_unlock(&dsa_nss_lock);
	seq_printf(m, "mode=%s tpid=%#x nodes=%d resyncs=%d creates=%d destroys=%d alloc_failures=%d retries_pending=%u\n",
		   port_id_tpid ? "port-id" : "single-tag", port_id_tpid, nodes,
		   atomic_read(&dsa_nss_resyncs),
		   atomic_read(&dsa_nss_creates), atomic_read(&dsa_nss_destroys),
		   atomic_read(&dsa_nss_alloc_failures), dsa_nss_retries);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(dsa_nss_status);

static ssize_t dsa_nss_resync_write(struct file *fp, const char __user *ubuf,
				    size_t sz, loff_t *ppos)
{
	dsa_nss_schedule();
	return sz;
}

static const struct file_operations dsa_nss_resync_fops = {
	.owner = THIS_MODULE,
	.write = dsa_nss_resync_write,
	.llseek = noop_llseek,
};

/*
 * ===== module =====
 */

static int __init qca_nss_qinq_init(void)
{
	int i, ret;

	if (port_id_tpid != ETH_P_8021AD) {
		pr_err("qca-nss-qinq: port_id_tpid must be %#x\n", ETH_P_8021AD);
		return -EINVAL;
	}

	for (i = 0; i < DSA_NSS_MAX_NODES; i++) {
		dsa_nss_nodes[i].if_num = -1;
		dsa_nss_nodes[i].parent_if_num = -1;
	}
	INIT_DELAYED_WORK(&dsa_nss_work, dsa_nss_work_fn);

	ret = register_netdevice_notifier(&dsa_nss_netdev_nb);
	if (ret) {
		WRITE_ONCE(dsa_nss_stopping, true);
		cancel_delayed_work_sync(&dsa_nss_work);
		return ret;
	}
	ret = register_switchdev_blocking_notifier(&dsa_nss_switchdev_nb);
	if (ret) {
		WRITE_ONCE(dsa_nss_stopping, true);
		unregister_netdevice_notifier(&dsa_nss_netdev_nb);
		cancel_delayed_work_sync(&dsa_nss_work);
		return ret;
	}

	dsa_nss_dentry = debugfs_create_dir("qca-nss-qinq", NULL);
	debugfs_create_file("status", 0444, dsa_nss_dentry, NULL,
			    &dsa_nss_status_fops);
	debugfs_create_file("resync", 0200, dsa_nss_dentry, NULL,
			    &dsa_nss_resync_fops);

	pr_info("qca-nss-qinq: firmware vlan nodes for tag_8021q DSA ports (%s)\n",
		port_id_tpid ? "QinQ port-ID" : "single-tag");
	dsa_nss_schedule();
	return 0;
}

static void __exit qca_nss_qinq_exit(void)
{
	WRITE_ONCE(dsa_nss_stopping, true);
	unregister_switchdev_blocking_notifier(&dsa_nss_switchdev_nb);
	unregister_netdevice_notifier(&dsa_nss_netdev_nb);
	cancel_delayed_work_sync(&dsa_nss_work);
	debugfs_remove_recursive(dsa_nss_dentry);

	mutex_lock(&dsa_nss_lock);
	dsa_nss_defunct();
	dsa_nss_destroy_nodes(NULL, "module unload");
	mutex_unlock(&dsa_nss_lock);
}

module_init(qca_nss_qinq_init);
module_exit(qca_nss_qinq_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("NSS firmware VLAN interfaces for tag_8021q DSA user ports");
