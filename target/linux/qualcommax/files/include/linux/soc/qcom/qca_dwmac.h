/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * qca_dwmac.h
 *	NSS data-plane claim API for the IPQ5018 dwmac (stmmac) driver.
 *
 * Counterpart of qca_edma.h on IPQ807x, shaped by a different hardware
 * split: the EDMA claim only redirects conduit TX for one port while the
 * host DMA keeps serving the others, but IPQ5018 has one Synopsys DMA per
 * GMAC and the NSS firmware drives that DMA itself with its own descriptor
 * rings. Claiming a dwmac netdev therefore parks the whole host data path
 * of that MAC - TX queues drained, NAPI off, DMA channels stopped - before
 * the firmware is told to take the controller over.
 *
 * What stays with the host: phylink (PCS/PHY management and the MAC
 * speed/duplex programming on link changes), MDIO, and the netdev itself,
 * which remains the interface the stack transmits on. TX entering
 * ndo_start_xmit while claimed is redirected to the owner; link
 * transitions are relayed through link_up/link_down after (down: before)
 * the MAC-level reprogramming they accompany.
 *
 * Releasing the claim does not restart the host DMA: after the firmware
 * owned the controller the host ring state is stale, and the one correct
 * way back is a full reinit. The claim owner is expected to bounce the
 * interface (dev_close/dev_open under rtnl) after release; TX queues stay
 * disabled in between so nothing touches the dead rings.
 */

#ifndef __LINUX_SOC_QCOM_QCA_DWMAC_H
#define __LINUX_SOC_QCOM_QCA_DWMAC_H

#include <linux/netdevice.h>
#include <linux/skbuff.h>

/**
 * struct qca_dwmac_dp_owner - claimed data-plane callbacks
 * @xmit:	takes over ndo_start_xmit for the claimed netdev. Runs in
 *		the usual transmit contexts (softirq); must not sleep.
 * @link_up:	called from phylink's resolve worker after the MAC has been
 *		programmed and enabled for the new link. May sleep.
 * @link_down:	called from phylink's resolve worker before the MAC is
 *		disabled. May sleep.
 */
struct qca_dwmac_dp_owner {
	netdev_tx_t (*xmit)(struct sk_buff *skb, void *ctx);
	void (*link_up)(void *ctx);
	void (*link_down)(void *ctx);
};

int qca_dwmac_dp_claim(struct net_device *dev,
		       const struct qca_dwmac_dp_owner *owner, void *ctx);
int qca_dwmac_dp_release(struct net_device *dev);

/*
 * Diagnostics for the firmware-owned data plane (dwmac1000 DMA block):
 * qca_dwmac_dp_dma_status() reads the DMA status register,
 * qca_dwmac_dp_rx_kick() clears the sticky RU/TU bits and writes the
 * receive poll demand, so a DMA parked in "suspended - descriptor
 * unavailable" re-reads its ring. The ring belongs to the firmware; the
 * kick only helps once it has refilled it, and is harmless otherwise.
 * Both return the DMA status register after the operation.
 */
u32 qca_dwmac_dp_dma_status(struct net_device *dev);
u32 qca_dwmac_dp_rx_kick(struct net_device *dev);

/*
 * Descriptor rings (dwmac1000 DMA). The firmware points the DMA at its own
 * rings on its first open after boot only, and a later open does not
 * rewind its ring indices; a host close/open in between resets the DMA
 * onto the host rings. With the claim held, the owner:
 *  - qca_dwmac_dp_dma_stop() + qca_dwmac_dp_rings_get() before release:
 *    the current descriptors are where the firmware resumes;
 *  - qca_dwmac_dp_mac_set(false) + qca_dwmac_dp_rings_set(resume points)
 *    before the firmware open: a start loads the current descriptors from
 *    the list addresses;
 *  - qca_dwmac_dp_rings_wrap(ring bases) + qca_dwmac_dp_mac_set(true)
 *    after it: the running DMA reads the list addresses at the ring end.
 * All but _get() fail with -EPERM without a claim; _stop() and _set()
 * with -EBUSY if the DMA does not stop, _wrap() if it has not started.
 */
struct qca_dwmac_dp_rings {
	u32 rx_base;	/* receive descriptor list address */
	u32 tx_base;	/* transmit descriptor list address */
	u32 rx_cur;	/* current receive descriptor */
	u32 tx_cur;	/* current transmit descriptor */
	u32 host_rx;	/* the host's own receive ring */
	u32 host_tx;	/* the host's own transmit ring */
};

int qca_dwmac_dp_rings_get(struct net_device *dev,
			   struct qca_dwmac_dp_rings *rings);
int qca_dwmac_dp_dma_stop(struct net_device *dev);
int qca_dwmac_dp_rings_set(struct net_device *dev, u32 rx, u32 tx);
int qca_dwmac_dp_rings_wrap(struct net_device *dev, u32 rx, u32 tx);
void qca_dwmac_dp_mac_set(struct net_device *dev, bool enable);

#endif /* __LINUX_SOC_QCOM_QCA_DWMAC_H */
