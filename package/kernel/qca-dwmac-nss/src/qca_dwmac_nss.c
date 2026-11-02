// SPDX-License-Identifier: GPL-2.0-only
/*
 * qca_dwmac_nss.c
 *	NSS data-plane glue for the IPQ5018 dwmac ethernet stack.
 *
 * qca-nss-drv is built against the nss-dp API but nss-dp itself was removed
 * from OpenWrt. On IPQ807x, qca-ppe-nss supplies that API on top of the
 * qca_ppe/qca_edma stack. IPQ5018 has neither PPE nor EDMA: it has two
 * Synopsys GMACs driven by dwmac-ipq5018, and the NSS firmware accelerates
 * one by driving its DMA controller directly with firmware-owned rings.
 *
 * The port model is therefore inverted relative to IPQ807x. There, six PPE
 * ports map 1:1 to DSA user ports and the glue overrides each user port,
 * claiming only conduit TX. Here the firmware knows nothing of switch
 * ports: phys_if N is GMAC N itself, so the netdev being overridden is the
 * trunk (on GL-B3000: eth0 = gmac1, the DSA conduit of the QCA8337). DSA
 * stays transparent - tag_qca frames pass through the firmware in both
 * directions (measured at line rate), the conduit remains the stack's
 * transmit device, and user-port demux happens in the DSA receive path
 * exactly as with the host data plane.
 *
 * The takeover sequence differs accordingly: the host DMA must be parked
 * BEFORE the firmware opens the interface (both would drive the same DMA
 * controller), so qca_dwmac_dp_claim() comes first, then mac_addr/mtu/
 * open/link. On IPQ807x the claim is last because EDMA keeps serving the
 * other ports. Unwinding hands the controller back by bouncing the
 * interface, which reinits the host DMA from scratch.
 *
 * Also unlike IPQ807x: phys_if numbering starts at 0 (not 1), there are
 * 2 ports (not 6), and there is no 32-byte TX preheader - although
 * nss-drv's generic dp_ops->init() still adds 32B of needed_headroom,
 * which is tracked and undone like on IPQ807x.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/rtnetlink.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/soc/qcom/qca_dwmac.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <net/dsa.h>

#include "nss_dp_api_if.h"

#define DWMAC_NSS_GATE_MAX_DEVS 1	/* IPQ5018 has a single UBI32 core */

/*
 * What nss-drv's generic dp_ops->init() adds to needed_headroom (a literal
 * 32 in __nss_data_plane_init). IPQ5018 has no TX preheader, so our header
 * deliberately lacks NSS_DP_PREHEADER_SIZE - this constant exists only to
 * undo that generic bump symmetrically on unwind.
 */
#define DWMAC_NSS_DP_INIT_HEADROOM 32

static char *ifname = "eth0";
module_param(ifname, charp, 0444);
MODULE_PARM_DESC(ifname, "netdev backing NSS phys_if <fw_if> (default eth0); the initial value of the debugfs 'ifname' file");

static int fw_if = 1;
module_param(fw_if, int, 0444);
MODULE_PARM_DESC(fw_if, "NSS phys_if number of that netdev = its GMAC index (default 1, gmac1)");

static char *ifmap = "";
module_param(ifmap, charp, 0444);
MODULE_PARM_DESC(ifmap, "netdev per NSS phys_if, '<if>:<netdev>[,<if>:<netdev>]' e.g. '1:eth1,0:wan'; overrides ifname/fw_if for the ports it names");

/*
 * The netdev each phys_if arms - phys_if N is GMAC N on this SoC. Seeded
 * from the module parameters, but writable through debugfs for any port
 * that is not armed yet, because the module is not always loaded by
 * whoever knows the board: qca-nss-drv pulls it in as a symbol dependency,
 * and then it comes up with the defaults no matter what the configuration
 * says. Being stuck with a load-time-only parameter meant a board whose
 * trunk is not eth0 could not be armed at all without a rebuild (reported
 * on the Xunison D50, where GMAC0 is renamed 'wan' and the trunk is eth1).
 *
 * One entry per port, not one name, because half the ipq5018 boards have
 * two GMACs in use: the switch on one and a WAN PHY on the other (D50,
 * MX6200), or the switch on GMAC0 and a 2.5G PHY on GMAC1 (SPNMX56,
 * AX6000). Those want both armed, and each needs its own netdev.
 */
static char dwmac_nss_ifnames[NSS_DP_MAX_INTERFACES][IFNAMSIZ];

static bool boot_unarmed;
module_param(boot_unarmed, bool, 0644);
MODULE_PARM_DESC(boot_unarmed,
		 "let qca-nss-drv boot the NSS core with no port armed (debug only)");

static bool fw_csum;
module_param(fw_csum, bool, 0644);
MODULE_PARM_DESC(fw_csum,
		 "let the NSS firmware generate TX checksums and segment GSO frames, and advertise TSO and generic checksum offload on the netdev (default off)");

enum dwmac_nss_port_state {
	DWMAC_NSS_PORT_IDLE = 0,
	DWMAC_NSS_PORT_ARMED,		/* netdev resolved and held */
	DWMAC_NSS_PORT_OVERRIDDEN,	/* nss-drv installed dp_ops */
	DWMAC_NSS_PORT_STARTED,		/* host DMA parked, fw owns the GMAC */
};

struct dwmac_nss_port {
	int if_num;
	struct net_device *netdev;	/* held while armed */
	atomic_t rx_kicks;		/* E181: manual RX poll-demand kicks */
	enum dwmac_nss_port_state state;
	bool start_pending;		/* overridden, fw start waits for carrier */
	bool headroom_added;		/* dp_ops->init() added 32B */
	netdev_features_t saved_wanted_features;
	bool features_trimmed;		/* TX csum/TSO cleared while started */
	netdev_features_t saved_features, saved_hw_features, saved_vlan_features;
	bool features_raised;		/* fw data-plane features applied */
	struct nss_dp_data_plane_ops *dp_ops;
	struct nss_dp_data_plane_ctx *dpc;
	atomic64_t tx_redirect_pkts;
	atomic64_t tx_busy;
	atomic64_t rx_fw_pkts;
	/*
	 * Shadow of what the firmware last acknowledged about the link, so
	 * redundant notifies are skipped and a failed one forces a retry -
	 * same discipline as nss-dp's link-state shadow (and qca-ppe-nss).
	 */
	u8 fw_link_up;
	u8 fw_link_failed;
	atomic_t fw_link_changes;
	atomic_t fw_link_skipped;
	/*
	 * Descriptor ring bases the firmware programmed on its first open
	 * after boot (per GMAC, fixed for the boot: kept across unwinds),
	 * and where the DMA stood in them when the firmware last closed.
	 */
	u32 fw_rx_ring;
	u32 fw_tx_ring;
	bool fw_rings_known;
	u32 fw_rx_resume;
	u32 fw_tx_resume;
	bool fw_resume_valid;
	atomic_t ring_restores;
	unsigned int host_restore_attempts;
	int host_restore_error;
};

static struct dwmac_nss_port dwmac_nss_ports[NSS_DP_MAX_INTERFACES];
static DEFINE_MUTEX(dwmac_nss_lock);
static unsigned long dwmac_nss_fw_mask;
static struct dentry *dwmac_nss_dentry;
static atomic_t dwmac_nss_rx_unexpected = ATOMIC_INIT(0);

static void dwmac_nss_gate_kick(void);

/*
 * ===== firmware-side callbacks (claim owner) =====
 */

static netdev_tx_t dwmac_nss_fw_xmit(struct sk_buff *skb, void *ctx)
{
	struct dwmac_nss_port *port = ctx;
	netdev_tx_t ret;

	/* The firmware cannot checksum a double-tagged frame (nss-dp masked it). */
	if (unlikely(skb->ip_summed == CHECKSUM_PARTIAL && skb_vlan_tagged_multi(skb)) &&
	    skb_checksum_help(skb)) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	ret = port->dp_ops->xmit(port->dpc, skb);
	if (unlikely(ret == NETDEV_TX_BUSY))
		atomic64_inc(&port->tx_busy);
	else
		atomic64_inc(&port->tx_redirect_pkts);
	return ret;
}

/*
 * Synchronous fw message; process context only (phylink resolve worker
 * or under rtnl from our own paths). Not called concurrently with claim
 * release for the same port: release stops TX first and the phylink
 * callbacks read the owner pointer once.
 */
static int dwmac_nss_fw_link_state(struct dwmac_nss_port *port, bool up)
{
	int err;

	if (port->fw_link_up == up && !port->fw_link_failed) {
		atomic_inc(&port->fw_link_skipped);
		return 0;
	}

	err = port->dp_ops->link_state(port->dpc, up ? 1 : 0);
	if (err)
		netdev_warn(port->netdev, "qca-dwmac-nss: fw link %s notify failed\n",
			    up ? "up" : "down");

	/* track what the firmware acknowledged, not what we asked of it */
	if (!err)
		port->fw_link_up = up;
	port->fw_link_failed = !!err;
	atomic_inc(&port->fw_link_changes);

	return err;
}

static void dwmac_nss_fw_link_up(void *ctx)
{
	dwmac_nss_fw_link_state(ctx, true);
}

static void dwmac_nss_fw_link_down(void *ctx)
{
	dwmac_nss_fw_link_state(ctx, false);
}

static const struct qca_dwmac_dp_owner dwmac_nss_dp_owner = {
	.xmit		= dwmac_nss_fw_xmit,
	.link_up	= dwmac_nss_fw_link_up,
	.link_down	= dwmac_nss_fw_link_down,
};

/*
 * ===== port state machine =====
 */

static struct dwmac_nss_port *dwmac_nss_port_by_netdev(struct net_device *netdev)
{
	int i;

	for (i = NSS_DP_START_IFNUM; i < NSS_DP_MAX_INTERFACES; i++)
		if (dwmac_nss_ports[i].netdev == netdev)
			return &dwmac_nss_ports[i];
	return NULL;
}

/*
 * Set across the restore bounce so the netdev notifier ignores the
 * events our own dev_close/dev_open generate - the caller holds
 * dwmac_nss_lock, and the notifier would deadlock taking it again.
 * Coherent without further locking: everything involved runs under rtnl.
 */
static bool dwmac_nss_in_restore;

/*
 * Reinit the host data path after the firmware owned the GMAC DMA. The
 * close/open cycle is the one supported way to get stmmac's rings and
 * DMA registers back to a known state. Caller holds rtnl and
 * dwmac_nss_lock.
 */
static int dwmac_nss_host_restore(struct net_device *netdev)
{
	struct net_device **users = NULL;
	unsigned int nusers = 0, i = 0;
	int err = 0, ret;

	ASSERT_RTNL();

	/* DSA closes its users on NETDEV_GOING_DOWN, but never reopens
	 * them on NETDEV_UP. Snapshot before the bounce destroys that state.
	 * RTNL keeps the tree and its netdevs stable throughout the restore.
	 */
#if IS_ENABLED(CONFIG_NET_DSA)
	if (netdev_uses_dsa(netdev)) {
		struct dsa_port *cpu_dp = netdev->dsa_ptr;
		struct dsa_port *dp;

		list_for_each_entry(dp, &cpu_dp->ds->dst->ports, list)
			if (dsa_port_is_user(dp) && dp->user &&
			    dp->cpu_dp == cpu_dp && netif_running(dp->user))
				nusers++;

		if (nusers) {
			users = kcalloc(nusers, sizeof(*users), GFP_KERNEL);
			if (!users) {
				/* DMA still needs reinitializing. Report the partial
				 * recovery rather than leaving stale rings running.
				 */
				netdev_warn(netdev, "qca-dwmac-nss: cannot snapshot DSA users; explicit port recovery required\n");
				err = -ENOMEM;
				nusers = 0;
			} else {
				list_for_each_entry(dp, &cpu_dp->ds->dst->ports, list)
					if (dsa_port_is_user(dp) && dp->user &&
					    dp->cpu_dp == cpu_dp &&
					    netif_running(dp->user))
						users[i++] = dp->user;
			}
		}
	}
#endif

	dwmac_nss_in_restore = true;
	dev_close(netdev);
	ret = dev_open(netdev, NULL);
	if (ret) {
		netdev_warn(netdev, "qca-dwmac-nss: host data plane reopen failed (%d)\n",
			    ret);
		err = ret;
	} else {
		/* Keep notifier suppression through user NETDEV_UP too: our
		 * notifier otherwise takes the mutex already held by the caller.
		 */
		for (i = 0; i < nusers; i++) {
			ret = dev_open(users[i], NULL);
			if (ret) {
				netdev_warn(users[i], "qca-dwmac-nss: DSA user reopen failed (%d)\n",
					    ret);
				if (!err)
					err = ret;
			}
		}
	}
	dwmac_nss_in_restore = false;
	kfree(users);
	if (err)
		netdev_warn(netdev, "qca-dwmac-nss: host restore incomplete (%d)\n",
			    err);
	else
		netdev_info(netdev, "qca-dwmac-nss: back on host data plane\n");
	return err;
}

/*
 * Bring the firmware data plane up for an overridden port. Order matters:
 * the host DMA is parked first (claim), because fw open hands the same
 * DMA controller to the firmware. Caller holds rtnl and dwmac_nss_lock;
 * nss-drv messages are synchronous, so this sleeps.
 */
/*
 * The config messages right after an arm can hit a firmware that is still
 * initializing (pools, wifi pbuf) and come back failed even though the
 * core reports ready - measured at boot: open failed 2.5s after the arm,
 * while the same call an instant later succeeds. Each message is cheap
 * and idempotent, so retry it a few times before giving up. Keep the
 * count low: each synchronous try can eat its own multi-second timeout,
 * and while port_start retries the host DMA is parked - against a truly
 * dead firmware every extra try extends a total network outage (10 tries
 * held eth0 down for 35s before the host fallback kicked in).
 */
#define DWMAC_NSS_MSG_TRIES	3
#define DWMAC_NSS_MSG_DELAY_MS	500

static int dwmac_nss_msg_retry(int (*msg)(void *), void *arg)
{
	int i, ret = -1;

	for (i = 0; i < DWMAC_NSS_MSG_TRIES; i++) {
		ret = msg(arg);
		if (!ret)
			break;
		msleep(DWMAC_NSS_MSG_DELAY_MS);
	}
	return ret;
}

static int dwmac_nss_msg_mac_addr(void *arg)
{
	struct dwmac_nss_port *port = arg;

	return port->dp_ops->mac_addr(port->dpc,
				      (uint8_t *)port->netdev->dev_addr);
}

static int dwmac_nss_msg_mtu(void *arg)
{
	struct dwmac_nss_port *port = arg;

	return port->dp_ops->change_mtu(port->dpc, port->netdev->mtu);
}

static int dwmac_nss_msg_open(void *arg)
{
	struct dwmac_nss_port *port = arg;

	/*
	 * Ring/mode arguments are zero: the MP firmware allocated its GMAC
	 * descriptor rings at boot (visible in meminfo as gmac_tx_desc_*).
	 */
	return port->dp_ops->open(port->dpc, 0, 0, 0);
}

/*
 * The firmware points the GMAC DMA at its own descriptor rings on its
 * first open after boot, and a later open neither points it back nor
 * rewinds the firmware's ring indices. Any host close/open in between -
 * netifd bouncing the trunk on a config reload, an MTU change - resets
 * the DMA onto the host rings. Measured on an AX6000 after a bridge
 * reload: the firmware reopened with the GMAC0 list addresses on the
 * stmmac rings (0x4567c000/0x43ddc000 instead of gmac_rx_desc_0/
 * gmac_tx_desc_0 from the NSS meminfo) and wired traffic stayed dead
 * until reboot; putting only the ring bases back started the DMA at
 * descriptor 0 while the firmware waited at 7 (RX) and 16 (TX), and TX
 * deadlocked. So: record the ring bases after the first open; on every
 * close record where the DMA stands; before the next open start the DMA
 * there, and once it runs point the list addresses back at the ring
 * bases for the wrap. All under rtnl and dwmac_nss_lock, claim held.
 */

/* after the firmware closed, before the claim is released */
static void dwmac_nss_rings_save(struct dwmac_nss_port *port)
{
	struct qca_dwmac_dp_rings r;
	int err;

	port->fw_resume_valid = false;
	if (!port->fw_rings_known)
		return;

	err = qca_dwmac_dp_dma_stop(port->netdev);
	if (!err)
		err = qca_dwmac_dp_rings_get(port->netdev, &r);
	if (err) {
		netdev_warn(port->netdev, "qca-dwmac-nss: phys_if %d DMA did not stop (%d), no resume point\n",
			    port->if_num, err);
		return;
	}
	if (r.rx_base != port->fw_rx_ring || r.tx_base != port->fw_tx_ring) {
		netdev_warn(port->netdev, "qca-dwmac-nss: phys_if %d DMA not on the fw rings at close (rx %08x tx %08x), no resume point\n",
			    port->if_num, r.rx_base, r.tx_base);
		return;
	}
	port->fw_rx_resume = r.rx_cur;
	port->fw_tx_resume = r.tx_cur;
	port->fw_resume_valid = true;
}

/* after the claim, before the firmware messages */
static int dwmac_nss_rings_prepare(struct dwmac_nss_port *port)
{
	u32 rx = port->fw_rx_ring, tx = port->fw_tx_ring;
	int err;

	/* first open after boot: the firmware programs its rings itself */
	if (!port->fw_rings_known)
		return 0;

	if (port->fw_resume_valid) {
		rx = port->fw_rx_resume;
		tx = port->fw_tx_resume;
	} else {
		netdev_warn(port->netdev, "qca-dwmac-nss: phys_if %d has no resume point, fw DMA rings restart at their base\n",
			    port->if_num);
	}
	port->fw_resume_valid = false;

	/* no frame may move the DMA between its start and the wrap fix */
	qca_dwmac_dp_mac_set(port->netdev, false);
	err = qca_dwmac_dp_rings_set(port->netdev, rx, tx);
	if (err) {
		netdev_warn(port->netdev, "qca-dwmac-nss: fw DMA rings not restored on phys_if %d (%d)\n",
			    port->if_num, err);
		return err;
	}
	port->fw_rx_resume = rx;
	port->fw_tx_resume = tx;
	return 0;
}

/* after a successful firmware open */
static void dwmac_nss_rings_start(struct dwmac_nss_port *port)
{
	struct qca_dwmac_dp_rings r;
	int err;

	if (port->fw_rings_known) {
		err = qca_dwmac_dp_rings_wrap(port->netdev, port->fw_rx_ring,
					      port->fw_tx_ring);
		if (netif_carrier_ok(port->netdev))
			qca_dwmac_dp_mac_set(port->netdev, true);
		if (err) {
			netdev_warn(port->netdev, "qca-dwmac-nss: phys_if %d DMA not started by the fw open (%d)\n",
				    port->if_num, err);
			return;
		}
		atomic_inc(&port->ring_restores);
		netdev_info(port->netdev, "qca-dwmac-nss: fw DMA rings restored on phys_if %d, resumed at rx %08x tx %08x\n",
			    port->if_num, port->fw_rx_resume, port->fw_tx_resume);
		return;
	}

	if (qca_dwmac_dp_rings_get(port->netdev, &r))
		return;
	if (r.rx_base == r.host_rx || r.tx_base == r.host_tx) {
		netdev_warn(port->netdev, "qca-dwmac-nss: fw open left phys_if %d on the host DMA rings (rx %08x tx %08x)\n",
			    port->if_num, r.rx_base, r.tx_base);
		return;
	}
	port->fw_rx_ring = r.rx_base;
	port->fw_tx_ring = r.tx_base;
	port->fw_rings_known = true;
	netdev_info(port->netdev, "qca-dwmac-nss: fw DMA rings of phys_if %d: rx %08x tx %08x\n",
		    port->if_num, r.rx_base, r.tx_base);
}

static int dwmac_nss_port_start(struct dwmac_nss_port *port)
{
	struct net_device *netdev = port->netdev;
	int ret;

	ASSERT_RTNL();

	ret = qca_dwmac_dp_claim(netdev, &dwmac_nss_dp_owner, port);
	if (ret) {
		netdev_warn(netdev, "qca-dwmac-nss: data plane claim failed (%d)\n",
			    ret);
		return ret;
	}

	if (dwmac_nss_rings_prepare(port))
		goto err_release;

	if (dwmac_nss_msg_retry(dwmac_nss_msg_mac_addr, port)) {
		netdev_warn(netdev, "qca-dwmac-nss: fw mac_addr failed\n");
		goto err_release;
	}

	if (dwmac_nss_msg_retry(dwmac_nss_msg_mtu, port)) {
		netdev_warn(netdev, "qca-dwmac-nss: fw change_mtu(%d) failed\n",
			    netdev->mtu);
		goto err_release;
	}

	if (dwmac_nss_msg_retry(dwmac_nss_msg_open, port)) {
		netdev_warn(netdev, "qca-dwmac-nss: fw open failed\n");
		goto err_release;
	}

	dwmac_nss_rings_start(port);

	/*
	 * The stmmac hardware whose checksum engine backed NETIF_F_HW_CSUM is
	 * parked now, and the firmware doesn't fill in for it (see fw_csum).
	 * Drop TX checksum offload and with it TSO so the kernel computes
	 * checksums in software before packets reach the firmware. Cleared in
	 * wanted_features only: hw_features keeps the bits, so ethtool -K can
	 * re-enable them for experiments (e.g. against another fw release).
	 * Forwarded traffic never needed them; this taxes only local flows.
	 */
	if (!fw_csum) {
		port->saved_wanted_features = netdev->wanted_features;
		netdev->wanted_features &= ~(NETIF_F_CSUM_MASK | NETIF_F_ALL_TSO);
		netdev_update_features(netdev);
		port->features_trimmed = true;
	} else if (port->dp_ops->set_features) {
		/*
		 * The firmware checksums and segments what the host hands it, so
		 * advertise its data plane's features (TSO among them, which the
		 * DWMAC1000 itself lacks), as nss-dp did on takeover. Its generic
		 * HW_CSUM replaces the GMAC's IP/IPv6 checksum bits.
		 *
		 * DSA user ports don't follow: they copied the conduit's
		 * vlan_features when they were created and DSA ignores
		 * NETDEV_FEAT_CHANGE, so they keep IP/IPv6 checksum offload and
		 * no TSO. Through a switch port the firmware therefore only
		 * fills in checksums; TSO needs a GMAC that is its own netdev.
		 */
		port->saved_features = netdev->features;
		port->saved_hw_features = netdev->hw_features;
		port->saved_vlan_features = netdev->vlan_features;
		port->saved_wanted_features = netdev->wanted_features;
		netdev->features &= ~NETIF_F_CSUM_MASK;
		netdev->hw_features &= ~NETIF_F_CSUM_MASK;
		netdev->vlan_features &= ~NETIF_F_CSUM_MASK;
		netdev->wanted_features &= ~NETIF_F_CSUM_MASK;
		port->dp_ops->set_features(port->dpc);
		netdev_update_features(netdev);
		port->features_raised = true;
	}

	/*
	 * A freshly opened firmware interface starts with its link down; say
	 * so in the shadow, then feed it the current carrier - phylink only
	 * reports transitions, and this link may already be up.
	 */
	port->fw_link_up = false;
	port->fw_link_failed = 0;
	if (netif_carrier_ok(netdev))
		dwmac_nss_fw_link_state(port, true);

	port->state = DWMAC_NSS_PORT_STARTED;
	netdev_info(netdev, "qca-dwmac-nss: phys_if %d on NSS fw data plane\n",
		    port->if_num);
	return 0;

err_release:
	qca_dwmac_dp_release(netdev);
	port->host_restore_attempts++;
	port->host_restore_error = dwmac_nss_host_restore(netdev);
	return -EIO;
}

/*
 * Start the firmware plane of an overridden port only on a live link.
 * Started while the carrier is down, the firmware opens the GMAC, TX
 * works, and not one ingress frame is ever delivered. nss-dwmac-up keeps
 * the boot arm clear of that by waiting for operstate up, but a trunk
 * bounced after boot - netifd rebuilding the bridge on a config reload
 * bounces the DSA conduit - comes back through NETDEV_UP a couple of
 * seconds before phylink brings the link up (measured on an AX6000:
 * claimed at 515.99s, link up at 518.09s, and no ingress on the wired
 * ports until reboot). So a port without carrier stays on the host
 * plane, marked pending, and the linkwatch NETDEV_CHANGE that reports
 * the carrier starts it.
 *
 * Only a pending port is started from NETDEV_CHANGE: a start that failed
 * has already bounced back to the host, and retrying it on every carrier
 * event would park the host DMA against a dead firmware over and over.
 * Caller holds rtnl and dwmac_nss_lock.
 */
static void dwmac_nss_port_start_on_link(struct dwmac_nss_port *port)
{
	struct net_device *netdev = port->netdev;

	if (!netif_running(netdev) || !netif_carrier_ok(netdev)) {
		if (!port->start_pending)
			netdev_info(netdev, "qca-dwmac-nss: phys_if %d waits for link before the NSS fw data plane\n",
				    port->if_num);
		port->start_pending = true;
		return;
	}

	port->start_pending = false;
	dwmac_nss_port_start(port);
}

/*
 * Unwind a port down the state ladder to @target. With @fw_alive the
 * firmware is messaged (link down, close); without, only host-side state
 * is undone - the rmmod path must not message a dead firmware.
 * @restart_host bounces the interface to reinit the host DMA; pass false
 * when the interface is going down anyway (NETDEV_GOING_DOWN, unregister).
 * Caller holds rtnl and dwmac_nss_lock.
 */
static void dwmac_nss_port_unwind(struct dwmac_nss_port *port,
				  enum dwmac_nss_port_state target,
				  bool fw_alive, bool restart_host)
{
	if (port->state == DWMAC_NSS_PORT_STARTED &&
	    target < DWMAC_NSS_PORT_STARTED) {
		if (fw_alive) {
			dwmac_nss_fw_link_state(port, false);
			if (port->dp_ops->close(port->dpc))
				netdev_warn(port->netdev, "qca-dwmac-nss: fw close failed\n");
			dwmac_nss_rings_save(port);
		} else {
			port->fw_resume_valid = false;
		}

		/* returns with TX stopped; nothing runs fw_xmit after this */
		qca_dwmac_dp_release(port->netdev);

		if (port->features_trimmed) {
			port->netdev->wanted_features |=
				port->saved_wanted_features &
				(NETIF_F_CSUM_MASK | NETIF_F_ALL_TSO);
			netdev_update_features(port->netdev);
			port->features_trimmed = false;
		}

		if (port->features_raised) {
			port->netdev->features = port->saved_features;
			port->netdev->hw_features = port->saved_hw_features;
			port->netdev->vlan_features = port->saved_vlan_features;
			port->netdev->wanted_features = port->saved_wanted_features;
			netdev_update_features(port->netdev);
			port->features_raised = false;
		}

		if (restart_host) {
			port->host_restore_attempts++;
			port->host_restore_error =
				dwmac_nss_host_restore(port->netdev);
		}

		port->state = DWMAC_NSS_PORT_OVERRIDDEN;
	}

	if (port->state == DWMAC_NSS_PORT_OVERRIDDEN &&
	    target < DWMAC_NSS_PORT_OVERRIDDEN) {
		if (port->headroom_added) {
			port->netdev->needed_headroom -= DWMAC_NSS_DP_INIT_HEADROOM;
			port->headroom_added = false;
		}
		port->dp_ops = NULL;
		port->dpc = NULL;
		port->start_pending = false;
		port->state = DWMAC_NSS_PORT_ARMED;
		netdev_info(port->netdev, "qca-dwmac-nss: NSS data plane released on phys_if %d\n",
			    port->if_num);
	}

	if (port->state == DWMAC_NSS_PORT_ARMED &&
	    target < DWMAC_NSS_PORT_ARMED) {
		dev_put(port->netdev);
		port->netdev = NULL;
		port->state = DWMAC_NSS_PORT_IDLE;
	}
}

/*
 * ===== nss-dp API surface (consumed by qca-nss-drv) =====
 */

struct net_device *nss_dp_get_netdev_by_nss_if_num(int if_num)
{
	struct net_device *netdev = NULL;

	if (if_num < NSS_DP_START_IFNUM || if_num >= NSS_DP_MAX_INTERFACES)
		return NULL;

	mutex_lock(&dwmac_nss_lock);
	if (dwmac_nss_ports[if_num].state >= DWMAC_NSS_PORT_ARMED)
		netdev = dwmac_nss_ports[if_num].netdev;
	mutex_unlock(&dwmac_nss_lock);

	return netdev;
}
EXPORT_SYMBOL(nss_dp_get_netdev_by_nss_if_num);

bool nss_dp_is_in_open_state(struct net_device *netdev)
{
	return netif_running(netdev);
}
EXPORT_SYMBOL(nss_dp_is_in_open_state);

int nss_dp_override_data_plane(struct net_device *netdev,
			       struct nss_dp_data_plane_ops *dp_ops,
			       struct nss_dp_data_plane_ctx *dpc)
{
	struct dwmac_nss_port *port;
	int ret = NSS_DP_FAILURE;

	if (!netdev || !dp_ops || !dpc || !dp_ops->init || !dp_ops->open ||
	    !dp_ops->close || !dp_ops->link_state || !dp_ops->mac_addr ||
	    !dp_ops->change_mtu || !dp_ops->xmit)
		return NSS_DP_FAILURE;

	mutex_lock(&dwmac_nss_lock);
	port = dwmac_nss_port_by_netdev(netdev);
	if (!port || port->state < DWMAC_NSS_PORT_ARMED)
		goto out;

	if (port->state >= DWMAC_NSS_PORT_OVERRIDDEN) {
		/* idempotent for nss-drv's per-core loops */
		ret = (port->dpc == dpc) ? NSS_DP_SUCCESS : NSS_DP_FAILURE;
		goto out;
	}

	port->dp_ops = dp_ops;
	port->dpc = dpc;

	/* adds 32B to needed_headroom (generic nss-drv code; no preheader
	 * exists on IPQ5018, but tracking it keeps the unwind symmetric) */
	if (dp_ops->init(dpc)) {
		netdev_warn(netdev, "qca-dwmac-nss: dp init failed\n");
		port->dp_ops = NULL;
		port->dpc = NULL;
		goto out;
	}
	port->headroom_added = true;
	port->state = DWMAC_NSS_PORT_OVERRIDDEN;

	netdev_info(netdev, "qca-dwmac-nss: NSS data plane override on phys_if %d\n",
		    port->if_num);
	ret = NSS_DP_SUCCESS;
out:
	mutex_unlock(&dwmac_nss_lock);
	return ret;
}
EXPORT_SYMBOL(nss_dp_override_data_plane);

/*
 * nss-drv signals "registration done, port was open" here (process
 * context). For a port that is down, the NETDEV_UP notifier does this
 * instead; for one without carrier, the NETDEV_CHANGE that raises it.
 */
void nss_dp_start_data_plane(struct net_device *netdev,
			     struct nss_dp_data_plane_ctx *dpc)
{
	struct dwmac_nss_port *port;

	rtnl_lock();
	mutex_lock(&dwmac_nss_lock);
	port = dwmac_nss_port_by_netdev(netdev);
	if (port && port->state == DWMAC_NSS_PORT_OVERRIDDEN) {
		if (port->dpc != dpc)
			netdev_warn(netdev, "qca-dwmac-nss: start with foreign dpc, ignored\n");
		else
			dwmac_nss_port_start_on_link(port);
	}
	mutex_unlock(&dwmac_nss_lock);
	rtnl_unlock();
}
EXPORT_SYMBOL(nss_dp_start_data_plane);

/*
 * nss-drv detaches at rmmod. Its IRQs are already torn down by the time
 * this runs, so firmware messages could only time out: host-side cleanup
 * only, and the interface is bounced to give the host its DMA back.
 */
void nss_dp_restore_data_plane(struct net_device *netdev)
{
	struct dwmac_nss_port *port;

	rtnl_lock();
	mutex_lock(&dwmac_nss_lock);
	port = dwmac_nss_port_by_netdev(netdev);
	if (port && port->state >= DWMAC_NSS_PORT_OVERRIDDEN)
		dwmac_nss_port_unwind(port, DWMAC_NSS_PORT_ARMED, false, true);
	mutex_unlock(&dwmac_nss_lock);
	rtnl_unlock();
}
EXPORT_SYMBOL(nss_dp_restore_data_plane);

/*
 * N2H receive: every packet the firmware delivers for a registered
 * phys_if lands here (nss-drv NAPI). The netdev is the trunk; delivering
 * on it runs the normal conduit receive path, so DSA demuxes tag_qca
 * frames to user ports exactly as if the host DMA had received them.
 */
void nss_dp_receive(struct net_device *netdev, struct sk_buff *skb,
		    struct napi_struct *napi)
{
	struct dwmac_nss_port *port = NULL;

	if (likely(netdev))
		port = dwmac_nss_port_by_netdev(netdev);

	if (unlikely(!port)) {
		atomic_inc(&dwmac_nss_rx_unexpected);
		dev_kfree_skb_any(skb);
		return;
	}

	atomic64_inc(&port->rx_fw_pkts);

	/*
	 * No dev_sw_netstats_rx_add() here, unlike qca-ppe-nss: it requires
	 * per-cpu dev->tstats, which DSA user ports allocate but stmmac does
	 * not (it counts in its own DMA rings, which this path bypasses).
	 * Deref of the absent tstats = panic on first fw-delivered packet
	 * (measured). Firmware-path RX is counted in our debugfs instead.
	 */
	skb->dev = netdev;
	skb->protocol = eth_type_trans(skb, netdev);
	napi_gro_receive(napi, skb);
}
EXPORT_SYMBOL(nss_dp_receive);

/*
 * PTP timestamp RX delivery, registered by nss-drv as service code 0x9.
 */
void nss_phy_tstamp_rx_buf(void *app_data, struct sk_buff *skb)
{
	struct net_device *ndev = skb->dev;

	if (ndev && phy_has_rxtstamp(ndev->phydev))
		if (phy_rxtstamp(ndev->phydev, skb, 0))
			return;

	netif_receive_skb(skb);
}
EXPORT_SYMBOL(nss_phy_tstamp_rx_buf);

/*
 * ===== nss-drv deferred probe gate =====
 *
 * qca-nss-drv defers its platform probe - and with it the NSS firmware
 * boot - until a port is armed, so a module dependency chain (e.g. from
 * Wi-Fi offload) cannot boot the firmware behind our back. Deferred core
 * devices are recorded and re-attached when fw_mask first goes non-zero.
 */

static DEFINE_SPINLOCK(dwmac_nss_gate_lock);
static struct device *dwmac_nss_gate_devs[DWMAC_NSS_GATE_MAX_DEVS];

int nss_dp_probe_gate(struct device *dev)
{
	bool recorded = false;
	int i;

	spin_lock(&dwmac_nss_gate_lock);
	if (READ_ONCE(dwmac_nss_fw_mask) || boot_unarmed) {
		spin_unlock(&dwmac_nss_gate_lock);
		return 0;
	}
	for (i = 0; i < DWMAC_NSS_GATE_MAX_DEVS; i++) {
		if (dwmac_nss_gate_devs[i] == dev)
			break;
		if (!dwmac_nss_gate_devs[i]) {
			dwmac_nss_gate_devs[i] = get_device(dev);
			recorded = true;
			break;
		}
	}
	spin_unlock(&dwmac_nss_gate_lock);

	/* the driver core retries deferred probes often - log once */
	if (recorded)
		dev_info(dev, "qca-dwmac-nss: deferring NSS core probe until a port is armed\n");
	else if (i == DWMAC_NSS_GATE_MAX_DEVS)
		dev_warn_once(dev,
			      "qca-dwmac-nss: more NSS core devices than cores, this one will stay deferred\n");
	return -EPROBE_DEFER;
}
EXPORT_SYMBOL(nss_dp_probe_gate);

static void dwmac_nss_gate_kick(void)
{
	struct device *devs[DWMAC_NSS_GATE_MAX_DEVS];
	int i;

	spin_lock(&dwmac_nss_gate_lock);
	memcpy(devs, dwmac_nss_gate_devs, sizeof(devs));
	memset(dwmac_nss_gate_devs, 0, sizeof(dwmac_nss_gate_devs));
	spin_unlock(&dwmac_nss_gate_lock);

	for (i = 0; i < DWMAC_NSS_GATE_MAX_DEVS; i++) {
		if (!devs[i])
			continue;
		if (device_attach(devs[i]) < 0)
			dev_warn(devs[i],
				 "qca-dwmac-nss: deferred NSS core attach failed\n");
		put_device(devs[i]);
	}
}

static void dwmac_nss_gate_drop(void)
{
	int i;

	spin_lock(&dwmac_nss_gate_lock);
	for (i = 0; i < DWMAC_NSS_GATE_MAX_DEVS; i++) {
		if (dwmac_nss_gate_devs[i]) {
			put_device(dwmac_nss_gate_devs[i]);
			dwmac_nss_gate_devs[i] = NULL;
		}
	}
	spin_unlock(&dwmac_nss_gate_lock);
}

/*
 * ===== arming (fw_mask) =====
 */

static int dwmac_nss_fw_arm(int if_num)
{
	struct dwmac_nss_port *port = &dwmac_nss_ports[if_num];
	struct net_device *netdev;

	if (port->state >= DWMAC_NSS_PORT_ARMED)
		return 0;

	if (!dwmac_nss_ifnames[if_num][0]) {
		pr_warn("qca-dwmac-nss: no netdev configured for phys_if %d - set it with 'echo %d:<netdev> > /sys/kernel/debug/qca-dwmac-nss/ifmap'\n",
			if_num, if_num);
		return -ENODEV;
	}
	netdev = dev_get_by_name(&init_net, dwmac_nss_ifnames[if_num]);
	if (!netdev) {
		pr_warn("qca-dwmac-nss: no netdev '%s' to arm phys_if %d - set the right one with 'echo %d:<netdev> > /sys/kernel/debug/qca-dwmac-nss/ifmap'\n",
			dwmac_nss_ifnames[if_num], if_num, if_num);
		return -ENODEV;
	}

	port->if_num = if_num;
	port->netdev = netdev;
	port->state = DWMAC_NSS_PORT_ARMED;
	netdev_info(netdev, "qca-dwmac-nss: armed as NSS phys_if %d\n", if_num);
	return 0;
}

static int dwmac_nss_fw_mask_apply(unsigned long mask)
{
	unsigned long old = dwmac_nss_fw_mask;
	unsigned long armed_now = 0;
	int i, ret = 0;

	if (mask & ~GENMASK(NSS_DP_MAX_INTERFACES - 1, NSS_DP_START_IFNUM)) {
		pr_warn("qca-dwmac-nss: fw_mask %#lx has bits outside phys_if %d..%d\n",
			mask, NSS_DP_START_IFNUM, NSS_DP_MAX_INTERFACES - 1);
		return -EINVAL;
	}

	/*
	 * Any port with a netdev configured can be armed, and several at
	 * once: bit N = GMAC N. All-or-nothing - a mask that names a port
	 * without a netdev leaves the state as it was, instead of half the
	 * boards' worth of ports armed under a mask that says otherwise.
	 */
	for (i = NSS_DP_START_IFNUM; i < NSS_DP_MAX_INTERFACES; i++) {
		bool want = !!(mask & BIT(i));
		bool have = !!(old & BIT(i));

		if (want && !have) {
			ret = dwmac_nss_fw_arm(i);
			if (ret)
				break;
			armed_now |= BIT(i);
		} else if (!want && have) {
			dwmac_nss_port_unwind(&dwmac_nss_ports[i],
					      DWMAC_NSS_PORT_IDLE, true, true);
		}
	}

	if (ret) {
		for (i = NSS_DP_START_IFNUM; i < NSS_DP_MAX_INTERFACES; i++)
			if (armed_now & BIT(i))
				dwmac_nss_port_unwind(&dwmac_nss_ports[i],
						      DWMAC_NSS_PORT_IDLE,
						      false, false);
		return ret;
	}

	WRITE_ONCE(dwmac_nss_fw_mask, mask);
	if (mask && !old)
		dwmac_nss_gate_kick();
	return 0;
}

static ssize_t dwmac_nss_fw_mask_write(struct file *file,
				       const char __user *ubuf,
				       size_t count, loff_t *ppos)
{
	unsigned long mask;
	char buf[24];
	int ret;

	if (count >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, ubuf, count))
		return -EFAULT;
	buf[count] = '\0';

	ret = kstrtoul(strim(buf), 0, &mask);
	if (ret)
		return ret;

	rtnl_lock();
	mutex_lock(&dwmac_nss_lock);
	ret = dwmac_nss_fw_mask_apply(mask);
	mutex_unlock(&dwmac_nss_lock);
	rtnl_unlock();

	return ret ? ret : count;
}

static int dwmac_nss_fw_mask_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%#lx\n", READ_ONCE(dwmac_nss_fw_mask));
	return 0;
}

static int dwmac_nss_fw_mask_open(struct inode *inode, struct file *file)
{
	return single_open(file, dwmac_nss_fw_mask_show, NULL);
}

static const struct file_operations dwmac_nss_fw_mask_fops = {
	.owner		= THIS_MODULE,
	.open		= dwmac_nss_fw_mask_open,
	.read		= seq_read,
	.write		= dwmac_nss_fw_mask_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/*
 * ===== ifname / ifmap =====
 *
 * Which netdev each phys_if hands to the firmware. A port's entry is
 * refused once that port is armed: it already holds a reference to the
 * old netdev, and the name is what the unwind path is written around.
 * Other ports stay writable.
 *
 * Syntax, shared by the ifmap module parameter and both debugfs files:
 * entries separated by commas or whitespace, each '<if>:<netdev>'; a bare
 * '<netdev>' means phys_if <fw_if>, which is what the old single-port
 * 'ifname' file always meant. The whole string is checked before any of
 * it is applied. Caller holds dwmac_nss_lock.
 */
static int dwmac_nss_ifmap_apply(const char *spec)
{
	char names[NSS_DP_MAX_INTERFACES][IFNAMSIZ];
	unsigned long touched = 0;
	char *buf, *tok, *cur;
	int ret = 0;

	buf = kstrdup(spec, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	cur = buf;
	while ((tok = strsep(&cur, ", \t\n;")) != NULL) {
		char *name;
		int n = fw_if;

		if (!*tok)
			continue;
		name = strchr(tok, ':');
		if (name) {
			*name++ = '\0';
			if (kstrtoint(tok, 10, &n) || n < NSS_DP_START_IFNUM ||
			    n >= NSS_DP_MAX_INTERFACES) {
				ret = -EINVAL;
				break;
			}
		} else {
			name = tok;
		}
		if (!*name || strlen(name) >= IFNAMSIZ) {
			ret = -EINVAL;
			break;
		}
		if (dwmac_nss_ports[n].state >= DWMAC_NSS_PORT_ARMED) {
			ret = -EBUSY;
			break;
		}
		strscpy(names[n], name, IFNAMSIZ);
		touched |= BIT(n);
	}

	if (!ret) {
		int i;

		for (i = NSS_DP_START_IFNUM; i < NSS_DP_MAX_INTERFACES; i++)
			if (touched & BIT(i))
				strscpy(dwmac_nss_ifnames[i], names[i], IFNAMSIZ);
	}
	kfree(buf);
	return ret;
}

static ssize_t dwmac_nss_ifmap_store(const char __user *ubuf, size_t count)
{
	char buf[NSS_DP_MAX_INTERFACES * (IFNAMSIZ + 4)];
	int ret;

	if (count >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, ubuf, count))
		return -EFAULT;
	buf[count] = '\0';

	mutex_lock(&dwmac_nss_lock);
	ret = dwmac_nss_ifmap_apply(strim(buf));
	mutex_unlock(&dwmac_nss_lock);

	return ret ? ret : count;
}

static ssize_t dwmac_nss_ifname_write(struct file *file,
				      const char __user *ubuf,
				      size_t count, loff_t *ppos)
{
	return dwmac_nss_ifmap_store(ubuf, count);
}

static int dwmac_nss_ifname_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s\n", dwmac_nss_ifnames[fw_if]);
	return 0;
}

static ssize_t dwmac_nss_ifmap_write(struct file *file,
				     const char __user *ubuf,
				     size_t count, loff_t *ppos)
{
	return dwmac_nss_ifmap_store(ubuf, count);
}

static int dwmac_nss_ifmap_show(struct seq_file *m, void *v)
{
	int i;

	mutex_lock(&dwmac_nss_lock);
	for (i = NSS_DP_START_IFNUM; i < NSS_DP_MAX_INTERFACES; i++)
		if (dwmac_nss_ifnames[i][0])
			seq_printf(m, "%d:%s\n", i, dwmac_nss_ifnames[i]);
	mutex_unlock(&dwmac_nss_lock);
	return 0;
}

static int dwmac_nss_ifmap_open(struct inode *inode, struct file *file)
{
	return single_open(file, dwmac_nss_ifmap_show, NULL);
}

static const struct file_operations dwmac_nss_ifmap_fops = {
	.owner		= THIS_MODULE,
	.open		= dwmac_nss_ifmap_open,
	.read		= seq_read,
	.write		= dwmac_nss_ifmap_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int dwmac_nss_ifname_open(struct inode *inode, struct file *file)
{
	return single_open(file, dwmac_nss_ifname_show, NULL);
}

static const struct file_operations dwmac_nss_ifname_fops = {
	.owner		= THIS_MODULE,
	.open		= dwmac_nss_ifname_open,
	.read		= seq_read,
	.write		= dwmac_nss_ifname_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/*
 * ===== rx_kick (E181) =====
 *
 * Write 1: for every armed port clear the sticky RU/TU bits and write the
 * GMAC receive poll demand, so a DMA parked in "suspended - descriptor
 * unavailable" re-reads the firmware's ring. Read: the DMA status per port.
 */
static ssize_t dwmac_nss_rx_kick_write(struct file *file,
				       const char __user *ubuf,
				       size_t count, loff_t *ppos)
{
	unsigned long val;
	char buf[24];
	int i, ret;

	if (count >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, ubuf, count))
		return -EFAULT;
	buf[count] = '\0';
	ret = kstrtoul(strim(buf), 0, &val);
	if (ret)
		return ret;
	if (!val)
		return count;

	mutex_lock(&dwmac_nss_lock);
	for (i = NSS_DP_START_IFNUM; i < NSS_DP_MAX_INTERFACES; i++) {
		struct dwmac_nss_port *port = &dwmac_nss_ports[i];
		u32 before, after;

		if (port->state < DWMAC_NSS_PORT_ARMED || !port->netdev)
			continue;
		before = qca_dwmac_dp_dma_status(port->netdev);
		after = qca_dwmac_dp_rx_kick(port->netdev);
		atomic_inc(&port->rx_kicks);
		netdev_info(port->netdev,
			    "qca-dwmac-nss: rx_kick: dma_status %08x -> %08x (rs %u->%u ru %u->%u)\n",
			    before, after, (before >> 17) & 7, (after >> 17) & 7,
			    !!(before & 0x80), !!(after & 0x80));
	}
	mutex_unlock(&dwmac_nss_lock);
	return count;
}

static int dwmac_nss_rx_kick_show(struct seq_file *m, void *v)
{
	int i;

	mutex_lock(&dwmac_nss_lock);
	for (i = NSS_DP_START_IFNUM; i < NSS_DP_MAX_INTERFACES; i++) {
		struct dwmac_nss_port *port = &dwmac_nss_ports[i];

		if (port->state < DWMAC_NSS_PORT_ARMED || !port->netdev)
			continue;
		seq_printf(m, "%s dma_status=%08x kicks=%d\n",
			   netdev_name(port->netdev),
			   qca_dwmac_dp_dma_status(port->netdev),
			   atomic_read(&port->rx_kicks));
	}
	mutex_unlock(&dwmac_nss_lock);
	return 0;
}

static int dwmac_nss_rx_kick_open(struct inode *inode, struct file *file)
{
	return single_open(file, dwmac_nss_rx_kick_show, NULL);
}

static const struct file_operations dwmac_nss_rx_kick_fops = {
	.owner		= THIS_MODULE,
	.open		= dwmac_nss_rx_kick_open,
	.read		= seq_read,
	.write		= dwmac_nss_rx_kick_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/*
 * ===== netdev events =====
 */

static int dwmac_nss_netdev_event(struct notifier_block *nb,
				  unsigned long event, void *ptr)
{
	struct net_device *netdev = netdev_notifier_info_to_dev(ptr);
	struct dwmac_nss_port *port;

	/*
	 * Filter BEFORE taking the lock: events we don't handle can be fired
	 * synchronously from paths that already hold it. The concrete case:
	 * port_start's netdev_update_features() raises NETDEV_FEAT_CHANGE
	 * with dwmac_nss_lock (and rtnl) held - taking the lock here again
	 * deadlocked the nss-drv registration workqueue with rtnl pinned,
	 * freezing all network configuration (measured). NETDEV_CHANGE and
	 * NETDEV_PRECHANGEMTU are safe to let through: none of those paths
	 * changes an MTU, and carrier changes reach the notifier from the
	 * linkwatch work, which takes rtnl on its own.
	 */
	switch (event) {
	case NETDEV_UP:
	case NETDEV_CHANGE:
	case NETDEV_GOING_DOWN:
	case NETDEV_PRECHANGEMTU:
	case NETDEV_CHANGEMTU:
	case NETDEV_CHANGEADDR:
	case NETDEV_UNREGISTER:
		break;
	default:
		return NOTIFY_DONE;
	}

	/* our own restore bounce; the lock is already held by its caller */
	if (dwmac_nss_in_restore)
		return NOTIFY_DONE;

	mutex_lock(&dwmac_nss_lock);
	port = dwmac_nss_port_by_netdev(netdev);
	if (!port) {
		mutex_unlock(&dwmac_nss_lock);
		return NOTIFY_DONE;
	}

	switch (event) {
	case NETDEV_UP:
		if (port->state == DWMAC_NSS_PORT_OVERRIDDEN)
			dwmac_nss_port_start_on_link(port);
		break;
	case NETDEV_CHANGE:
		if (port->state == DWMAC_NSS_PORT_OVERRIDDEN &&
		    port->start_pending)
			dwmac_nss_port_start_on_link(port);
		break;
	case NETDEV_GOING_DOWN:
		port->start_pending = false;
		/*
		 * stmmac_release would tear the driver down around a
		 * firmware-owned DMA; hand the port back first. No bounce -
		 * the interface is closing anyway, and the next NETDEV_UP
		 * restarts the firmware path over the fresh host state.
		 */
		if (port->state == DWMAC_NSS_PORT_STARTED)
			dwmac_nss_port_unwind(port, DWMAC_NSS_PORT_OVERRIDDEN,
					      true, false);
		break;
	case NETDEV_PRECHANGEMTU:
		/*
		 * stmmac changes the MTU of a running interface by reopening
		 * the DMA itself (__stmmac_release + __stmmac_open), without
		 * NETDEV_GOING_DOWN/UP. On a claimed GMAC that napi_disable()s
		 * the queues the claim already disabled - which blocks forever
		 * with rtnl held - and then leaves the DMA on the new host
		 * rings. Hand the port back first, as on GOING_DOWN;
		 * NETDEV_CHANGEMTU brings the firmware back once there is link.
		 */
		port->start_pending = false;
		if (port->state == DWMAC_NSS_PORT_STARTED)
			dwmac_nss_port_unwind(port, DWMAC_NSS_PORT_OVERRIDDEN,
					      true, false);
		break;
	case NETDEV_CHANGEMTU:
		if (port->state == DWMAC_NSS_PORT_OVERRIDDEN)
			dwmac_nss_port_start_on_link(port);
		else if (port->state == DWMAC_NSS_PORT_STARTED &&
			 port->dp_ops->change_mtu(port->dpc, netdev->mtu))
			netdev_warn(netdev, "qca-dwmac-nss: fw change_mtu(%d) failed\n",
				    netdev->mtu);
		break;
	case NETDEV_CHANGEADDR:
		if (port->state == DWMAC_NSS_PORT_STARTED &&
		    port->dp_ops->mac_addr(port->dpc, (uint8_t *)netdev->dev_addr))
			netdev_warn(netdev, "qca-dwmac-nss: fw mac_addr failed\n");
		break;
	case NETDEV_UNREGISTER:
		dwmac_nss_port_unwind(port, DWMAC_NSS_PORT_IDLE,
				      port->state == DWMAC_NSS_PORT_STARTED,
				      false);
		WRITE_ONCE(dwmac_nss_fw_mask,
			   dwmac_nss_fw_mask & ~BIT(port->if_num));
		break;
	}
	mutex_unlock(&dwmac_nss_lock);
	return NOTIFY_DONE;
}

static struct notifier_block dwmac_nss_netdev_nb = {
	.notifier_call = dwmac_nss_netdev_event,
};

/*
 * ===== debugfs status =====
 */

static const char * const dwmac_nss_state_names[] = {
	"idle", "armed", "overridden", "started",
};

static int dwmac_nss_status_show(struct seq_file *m, void *v)
{
	int i;

	seq_printf(m, "fw_mask: %#lx\n", READ_ONCE(dwmac_nss_fw_mask));
	seq_printf(m, "rx_unexpected: %d\n",
		   atomic_read(&dwmac_nss_rx_unexpected));

	mutex_lock(&dwmac_nss_lock);
	for (i = NSS_DP_START_IFNUM; i < NSS_DP_MAX_INTERFACES; i++) {
		struct dwmac_nss_port *port = &dwmac_nss_ports[i];

		if (port->state == DWMAC_NSS_PORT_IDLE) {
			/*
			 * A port that has a netdev in the map but no state is
			 * the confusing case: the map says it should be a
			 * firmware port, yet nothing armed it. Say which of
			 * the two reasons it is instead of staying silent.
			 */
			if (!dwmac_nss_ifnames[i][0])
				continue;
			seq_printf(m, "phys_if %d: idle dev=%s (%s)\n", i,
				   dwmac_nss_ifnames[i],
				   (READ_ONCE(dwmac_nss_fw_mask) & BIT(i)) ?
				   "in fw_mask, netdev not up yet" :
				   "not in fw_mask");
			seq_printf(m, "  host_restore_attempts=%u host_restore_error=%d\n",
				   port->host_restore_attempts, port->host_restore_error);
			continue;
		}
		seq_printf(m, "phys_if %d: %s dev=%s fw_link=%s%s%s\n",
			   i, dwmac_nss_state_names[port->state],
			   netdev_name(port->netdev),
			   port->fw_link_up ? "up" : "down",
			   port->fw_link_failed ? " (notify-failed)" : "",
			   port->start_pending ? " (waiting for link)" : "");
		seq_printf(m, "  host_restore_attempts=%u host_restore_error=%d\n",
			   port->host_restore_attempts, port->host_restore_error);
		seq_printf(m, "  tx_redirect=%lld tx_busy=%lld rx_fw=%lld link_changes=%d link_skipped=%d\n",
			   (long long)atomic64_read(&port->tx_redirect_pkts),
			   (long long)atomic64_read(&port->tx_busy),
			   (long long)atomic64_read(&port->rx_fw_pkts),
			   atomic_read(&port->fw_link_changes),
			   atomic_read(&port->fw_link_skipped));
		if (port->state >= DWMAC_NSS_PORT_ARMED && port->netdev) {
			u32 st = qca_dwmac_dp_dma_status(port->netdev);
			struct qca_dwmac_dp_rings r;

			seq_printf(m, "  dma_status=%08x rs=%u ts=%u ru=%u tu=%u rx_kicks=%d\n",
				   st, (st >> 17) & 7, (st >> 20) & 7,
				   !!(st & 0x80), !!(st & 0x4),
				   atomic_read(&port->rx_kicks));
			if (!qca_dwmac_dp_rings_get(port->netdev, &r))
				seq_printf(m, "  rings rx=%08x tx=%08x cur rx=%08x tx=%08x host rx=%08x tx=%08x fw %s rx=%08x tx=%08x resume rx=%08x tx=%08x%s restores=%d\n",
					   r.rx_base, r.tx_base, r.rx_cur, r.tx_cur,
					   r.host_rx, r.host_tx,
					   port->fw_rings_known ? "known" : "unknown",
					   port->fw_rx_ring, port->fw_tx_ring,
					   port->fw_rx_resume, port->fw_tx_resume,
					   port->fw_resume_valid ? " (pending)" : "",
					   atomic_read(&port->ring_restores));
		}
	}
	mutex_unlock(&dwmac_nss_lock);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(dwmac_nss_status);

/*
 * TCSR GMAC AXI cache-attribute override, ported from nss-dp's
 * nss_dp_hal_tcsr_set() (hal/soc_ops/ipq50xx). The QSDK ethernet driver
 * programs this on every ipq50xx boot; without it the GMAC masters run
 * with the reset-default AXI cache attributes. The register sits in a
 * TZ-protected TCSR page, so writes must go through SCM; the readback
 * is logged so a boot records what the value was before and after.
 */
#define DWMAC_NSS_TCSR_BASE			0x01937000
#define DWMAC_NSS_TCSR_GMAC_AXI_CACHE_OVERRIDE	0x6224
#define DWMAC_NSS_TCSR_GMAC_AXI_CACHE_VALUE	0x05050505

static void qca_dwmac_nss_tcsr_set(void)
{
	phys_addr_t addr = DWMAC_NSS_TCSR_BASE +
			   DWMAC_NSS_TCSR_GMAC_AXI_CACHE_OVERRIDE;
	unsigned int old = 0, cur = 0;
	int err;

	if (!qcom_scm_is_available()) {
		pr_warn("qca-dwmac-nss: SCM unavailable, TCSR override not programmed\n");
		return;
	}

	err = qcom_scm_io_readl(addr, &old);
	if (err)
		pr_warn("qca-dwmac-nss: TCSR readback failed: %d\n", err);

	err = qcom_scm_io_writel(addr, DWMAC_NSS_TCSR_GMAC_AXI_CACHE_VALUE);
	if (err) {
		pr_err("qca-dwmac-nss: SCM TCSR write error: %d\n", err);
		return;
	}

	qcom_scm_io_readl(addr, &cur);
	pr_info("qca-dwmac-nss: TCSR GMAC_AXI_CACHE_OVERRIDE @%pap: 0x%08x -> 0x%08x\n",
		&addr, old, cur);
}

static int __init qca_dwmac_nss_init(void)
{
	int ret;

	if (fw_if < NSS_DP_START_IFNUM || fw_if >= NSS_DP_MAX_INTERFACES) {
		pr_err("qca-dwmac-nss: fw_if %d out of range 0..%d\n",
		       fw_if, NSS_DP_MAX_INTERFACES - 1);
		return -EINVAL;
	}

	/* the legacy pair first, then ifmap on top for the ports it names */
	strscpy(dwmac_nss_ifnames[fw_if], ifname, IFNAMSIZ);
	if (*ifmap) {
		ret = dwmac_nss_ifmap_apply(ifmap);
		if (ret) {
			pr_err("qca-dwmac-nss: bad ifmap '%s' (%d)\n", ifmap, ret);
			return ret;
		}
	}

	qca_dwmac_nss_tcsr_set();

	ret = register_netdevice_notifier(&dwmac_nss_netdev_nb);
	if (ret)
		return ret;

	dwmac_nss_dentry = debugfs_create_dir("qca-dwmac-nss", NULL);
	debugfs_create_file("status", 0444, dwmac_nss_dentry, NULL,
			    &dwmac_nss_status_fops);
	debugfs_create_file("fw_mask", 0644, dwmac_nss_dentry, NULL,
			    &dwmac_nss_fw_mask_fops);
	debugfs_create_file("ifname", 0644, dwmac_nss_dentry, NULL,
			    &dwmac_nss_ifname_fops);
	debugfs_create_file("ifmap", 0644, dwmac_nss_dentry, NULL,
			    &dwmac_nss_ifmap_fops);
	debugfs_create_file("rx_kick", 0644, dwmac_nss_dentry, NULL,
			    &dwmac_nss_rx_kick_fops);

	{
		char map[NSS_DP_MAX_INTERFACES * (IFNAMSIZ + 4)] = "";
		int i, n = 0;

		for (i = NSS_DP_START_IFNUM; i < NSS_DP_MAX_INTERFACES; i++)
			if (dwmac_nss_ifnames[i][0])
				n += scnprintf(map + n, sizeof(map) - n, "%s%s = phys_if %d",
					       n ? ", " : "", dwmac_nss_ifnames[i], i);
		pr_info("qca-dwmac-nss: nss-dp glue for IPQ5018 loaded (%s)\n", map);
	}
	return 0;
}

static void __exit qca_dwmac_nss_exit(void)
{
	int i;

	/*
	 * nss-drv holds symbol references on this module, so it is gone by
	 * the time rmmod succeeds and every port is at most ARMED.
	 */
	rtnl_lock();
	mutex_lock(&dwmac_nss_lock);
	for (i = NSS_DP_START_IFNUM; i < NSS_DP_MAX_INTERFACES; i++)
		dwmac_nss_port_unwind(&dwmac_nss_ports[i],
				      DWMAC_NSS_PORT_IDLE, false, false);
	mutex_unlock(&dwmac_nss_lock);
	rtnl_unlock();

	debugfs_remove_recursive(dwmac_nss_dentry);
	unregister_netdevice_notifier(&dwmac_nss_netdev_nb);
	dwmac_nss_gate_drop();
}

module_init(qca_dwmac_nss_init);
module_exit(qca_dwmac_nss_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("NSS data-plane glue for the IPQ5018 dwmac stack");
