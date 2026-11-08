// SPDX-License-Identifier: ISC
/*
 * Native NSS-DP with upstream IPQ5018 PHY/UNIPHY management.
 * Opt-in through qcom,upstream-phylink; no SSDK switch/PCS ownership.
 * Clock rates and speed encoding follow this tree's dwmac-ipq5018 and
 * dwmac1000 drivers. DMA ownership stays in NSS-DP and qca-nss-drv.
 */
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/pcs/pcs.h>
#include <linux/phylink.h>
#include <linux/reset.h>
#include <linux/spinlock.h>

#include "nss_dp_ipq5018.h"

#define GMAC_CONFIG             0x0000
#define GMAC_PS                 BIT(15)
#define GMAC_FES                BIT(14)
#define GMAC_DUPLEX             BIT(11)
#define GMAC_2K                 BIT(27)
#define GMAC_JUMBO              BIT(20)
#define GMAC_CORE_INIT          (BIT(22) | BIT(21) | BIT(16) | BIT(15))

struct nss_dp_ipq5018 {
	struct nss_dp_dev *dp;
	struct clk_bulk_data clks[7];
	struct phylink_config config;
	struct phylink *phylink;
	struct phylink_pcs *pcs;
	struct ethtool_ops ethtool_ops;
	spinlock_t mac_lock;
};

static struct nss_dp_ipq5018 *to_native(struct phylink_config *config)
{
	return container_of(config, struct nss_dp_ipq5018, config);
}

static struct phylink_pcs *native_select_pcs(struct phylink_config *config,
					  phy_interface_t interface)
{
	return to_native(config)->pcs;
}

static void native_mac_config(struct phylink_config *config, unsigned int mode,
			      const struct phylink_link_state *state)
{
	struct nss_dp_dev *dp = to_native(config)->dp;
	void __iomem *base = dp->gmac_hal_ctx->mac_base;
	unsigned long flags;
	u32 value;

	spin_lock_irqsave(&dp->ipq5018->mac_lock, flags);
	value = readl(base + GMAC_CONFIG);
	/* DSA's two transport tags require the 2K frame allowance at MTU 1500. */
	value |= GMAC_CORE_INIT | GMAC_2K;
	if (dp->netdev->mtu > 2000)
		value |= GMAC_JUMBO;
	else
		value &= ~GMAC_JUMBO;
	writel(value, base + GMAC_CONFIG);
	spin_unlock_irqrestore(&dp->ipq5018->mac_lock, flags);
}

/* Called only after the dataplane has accepted the new MTU. */
void nss_dp_ipq5018_change_mtu(struct nss_dp_dev *dp, int mtu)
{
	void __iomem *base;
	unsigned long flags;
	u32 value;

	if (!dp->ipq5018)
		return;

	base = dp->gmac_hal_ctx->mac_base;
	spin_lock_irqsave(&dp->ipq5018->mac_lock, flags);
	value = readl(base + GMAC_CONFIG) | GMAC_2K;
	if (mtu > 2000)
		value |= GMAC_JUMBO;
	else
		value &= ~GMAC_JUMBO;
	writel(value, base + GMAC_CONFIG);
	spin_unlock_irqrestore(&dp->ipq5018->mac_lock, flags);
}

static void native_mac_link_down(struct phylink_config *config,
				 unsigned int mode, phy_interface_t interface)
{
	struct nss_dp_dev *dp = to_native(config)->dp;
	unsigned long flags;

	dp->data_plane_ops->link_state(dp->dpc, 0);
	spin_lock_irqsave(&dp->ipq5018->mac_lock, flags);
	dp->gmac_hal_ops->stop(dp->gmac_hal_ctx);
	spin_unlock_irqrestore(&dp->ipq5018->mac_lock, flags);
	dp->link_state = __NSS_DP_LINK_DOWN;
}

static void native_mac_link_up(struct phylink_config *config,
			       struct phy_device *phy, unsigned int mode,
			       phy_interface_t interface, int speed, int duplex,
			       bool tx_pause, bool rx_pause)
{
	struct nss_dp_ipq5018 *native = to_native(config);
	struct nss_dp_dev *dp = native->dp;
	void __iomem *base = dp->gmac_hal_ctx->mac_base;
	unsigned long rate;
	unsigned long flags;
	u32 value;
	int ret;

	switch (speed) {
	case SPEED_10: rate = 2500000; break;
	case SPEED_100: rate = 25000000; break;
	case SPEED_1000: rate = 125000000; break;
	case SPEED_2500: rate = 312500000; break;
	default:
		netdev_err(dp->netdev, "unsupported native link speed %d\n", speed);
		return;
	}
	ret = clk_set_rate(native->clks[5].clk, rate);
	if (!ret)
		ret = clk_set_rate(native->clks[6].clk, rate);
	if (ret) {
		netdev_err(dp->netdev, "native link clock setup failed: %d\n", ret);
		return;
	}

	spin_lock_irqsave(&native->mac_lock, flags);
	value = readl(base + GMAC_CONFIG) & ~(GMAC_PS | GMAC_FES | GMAC_DUPLEX);
	if (speed == SPEED_10 || speed == SPEED_100)
		value |= GMAC_PS;
	if (speed == SPEED_100)
		value |= GMAC_FES;
	if (duplex == DUPLEX_FULL)
		value |= GMAC_DUPLEX;
	writel(value, base + GMAC_CONFIG);
	dp->gmac_hal_ops->rxflowcontrol(dp->gmac_hal_ctx, rx_pause);
	dp->gmac_hal_ops->txflowcontrol(dp->gmac_hal_ctx, tx_pause);
	dp->gmac_hal_ops->setmacaddr(dp->gmac_hal_ctx, (u8 *)dp->netdev->dev_addr);
	dp->gmac_hal_ops->start(dp->gmac_hal_ctx);
	spin_unlock_irqrestore(&native->mac_lock, flags);
	if (dp->data_plane_ops->link_state(dp->dpc, 1)) {
		spin_lock_irqsave(&native->mac_lock, flags);
		dp->gmac_hal_ops->stop(dp->gmac_hal_ctx);
		spin_unlock_irqrestore(&native->mac_lock, flags);
		netdev_err(dp->netdev, "native dataplane link-up failed\n");
		return;
	}
	dp->link_state = __NSS_DP_LINK_UP;
}

static const struct phylink_mac_ops native_mac_ops = {
	.mac_select_pcs = native_select_pcs,
	.mac_config = native_mac_config,
	.mac_link_down = native_mac_link_down,
	.mac_link_up = native_mac_link_up,
};

static void native_disable_clocks(void *data)
{
	struct nss_dp_ipq5018 *native = data;

	clk_bulk_disable_unprepare(ARRAY_SIZE(native->clks), native->clks);
}

int nss_dp_ipq5018_init(struct nss_dp_dev *dp)
{
	static const char * const ids[] = {
		"stmmaceth", "pclk", "ptp_ref", "ahb", "axi", "rx", "tx"
	};
	struct device *dev = &dp->pdev->dev;
	struct nss_dp_ipq5018 *native;
	struct reset_control *reset;
	int i, ret;

	if (!of_property_read_bool(dev->of_node, "qcom,upstream-phylink"))
		return 0;
	native = devm_kzalloc(dev, sizeof(*native), GFP_KERNEL);
	if (!native)
		return -ENOMEM;
	native->dp = dp;
	spin_lock_init(&native->mac_lock);
	for (i = 0; i < ARRAY_SIZE(ids); i++)
		native->clks[i].id = ids[i];
	ret = devm_clk_bulk_get(dev, ARRAY_SIZE(native->clks), native->clks);
	if (ret)
		return ret;
	ret = clk_bulk_prepare_enable(ARRAY_SIZE(native->clks), native->clks);
	if (ret)
		return ret;
	ret = devm_add_action_or_reset(dev, native_disable_clocks, native);
	if (ret)
		return ret;
	reset = devm_reset_control_get_exclusive(dev, "stmmaceth");
	if (IS_ERR(reset))
		return PTR_ERR(reset);
	ret = reset_control_reset(reset);
	if (ret)
		return ret;

	if (of_find_property(dev->of_node, "pcs-handle", NULL)) {
		native->pcs = fwnode_pcs_get(dev_fwnode(dev), 0);
		if (IS_ERR(native->pcs)) {
			ret = PTR_ERR(native->pcs);
			return ret == -ENODEV ? -EPROBE_DEFER : ret;
		}
	}
	native->config.dev = &dp->netdev->dev;
	native->config.type = PHYLINK_NETDEV;
	native->config.mac_capabilities = MAC_SYM_PAUSE | MAC_ASYM_PAUSE |
		MAC_10 | MAC_100 | MAC_1000FD;
	__set_bit(PHY_INTERFACE_MODE_SGMII, native->config.supported_interfaces);
	if (native->pcs) {
		native->config.mac_capabilities |= MAC_2500FD;
		__set_bit(PHY_INTERFACE_MODE_1000BASEX, native->config.supported_interfaces);
		__set_bit(PHY_INTERFACE_MODE_2500BASEX, native->config.supported_interfaces);
	}
	dp->ipq5018 = native;
	dp->netdev->max_mtu = 16357;
	return 0;
}

int nss_dp_ipq5018_connect(struct nss_dp_dev *dp)
{
	struct nss_dp_ipq5018 *native = dp->ipq5018;
	int ret;

	if (!native)
		return 0;
	native->phylink = phylink_create(&native->config,
		dev_fwnode(&dp->pdev->dev), dp->phy_mii_type, &native_mac_ops);
	if (IS_ERR(native->phylink)) {
		ret = PTR_ERR(native->phylink);
		native->phylink = NULL;
		return ret;
	}
	ret = phylink_of_phy_connect(native->phylink, dp->pdev->dev.of_node, 0);
	if (ret) {
		phylink_destroy(native->phylink);
		native->phylink = NULL;
	}
	return ret;
}

void nss_dp_ipq5018_cleanup(struct nss_dp_dev *dp)
{
	if (dp->ipq5018 && dp->ipq5018->phylink) {
		phylink_disconnect_phy(dp->ipq5018->phylink);
		phylink_destroy(dp->ipq5018->phylink);
		dp->ipq5018->phylink = NULL;
	}
	dp->ipq5018 = NULL;
}

void nss_dp_ipq5018_start(struct nss_dp_dev *dp)
{
	phylink_start(dp->ipq5018->phylink);
}

void nss_dp_ipq5018_stop(struct nss_dp_dev *dp)
{
	phylink_stop(dp->ipq5018->phylink);
}

static int native_get_ksettings(struct net_device *dev,
				struct ethtool_link_ksettings *cmd)
{
	struct nss_dp_dev *dp = netdev_priv(dev);

	return phylink_ethtool_ksettings_get(dp->ipq5018->phylink, cmd);
}

static int native_set_ksettings(struct net_device *dev,
				const struct ethtool_link_ksettings *cmd)
{
	struct nss_dp_dev *dp = netdev_priv(dev);

	return phylink_ethtool_ksettings_set(dp->ipq5018->phylink, cmd);
}

void nss_dp_ipq5018_set_ethtool(struct nss_dp_dev *dp)
{
	struct ethtool_ops *ops;

	if (!dp->ipq5018)
		return;
	ops = &dp->ipq5018->ethtool_ops;
	*ops = *dp->netdev->ethtool_ops;
	ops->get_link_ksettings = native_get_ksettings;
	ops->set_link_ksettings = native_set_ksettings;
	/* SSDK does not own this MAC, so its EEE/pause/flag callbacks do not apply. */
	ops->get_eee = NULL;
	ops->set_eee = NULL;
	ops->get_priv_flags = NULL;
	ops->set_priv_flags = NULL;
	ops->get_pauseparam = NULL;
	ops->set_pauseparam = NULL;
	dp->netdev->ethtool_ops = ops;
}
