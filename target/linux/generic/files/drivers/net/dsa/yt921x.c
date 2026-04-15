// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Motorcomm YT921x Switch
 *
 * Split into logical internal units to keep function groups manageable while
 * preserving original function order and behavior.
 *
 * Copyright (c) 2025 David Yang
 * Copyright (c) 2026 zcop <hongson.hn@gmail.com>
 */

#include "yt921x_internal.h"

static int yt921x_dsa_setup(struct dsa_switch *ds)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);
	struct device *dev = yt921x_dev(priv);
	struct device_node *np = dev->of_node;
	struct device_node *child;
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_chip_reset(priv);
	mutex_unlock(&priv->reg_lock);

	if (res)
		return res;

	/* Register the internal mdio bus. Nodes for internal ports should have
	 * proper phy-handle pointing to their PHYs. Not enabling the internal
	 * bus is possible, though pretty wired, if internal ports are not used.
	 */
	child = of_get_child_by_name(np, "mdio");
	if (child) {
		res = yt921x_mbus_int_init(priv, child);
		of_node_put(child);
		if (res)
			return res;
	}

	/* External mdio bus is optional */
	child = of_get_child_by_name(np, "mdio-external");
	if (child) {
		res = yt921x_mbus_ext_init(priv, child);
		of_node_put(child);
		if (res)
			return res;
	}

	mutex_lock(&priv->reg_lock);
	res = yt921x_chip_setup(priv);
	mutex_unlock(&priv->reg_lock);

	if (res)
		return res;

	res = yt921x_devlink_params_register(ds);
	if (res)
		return res;
	priv->devlink_params_registered = true;

	return 0;
}

static void yt921x_dsa_teardown(struct dsa_switch *ds)
{
	struct yt921x_priv *priv = yt921x_to_priv(ds);

	if (!priv->devlink_params_registered)
		return;

	yt921x_devlink_params_unregister(ds);
	priv->devlink_params_registered = false;
}

static const struct phylink_mac_ops yt921x_phylink_mac_ops = {
	.mac_link_down	= yt921x_phylink_mac_link_down,
	.mac_link_up	= yt921x_phylink_mac_link_up,
	.mac_config	= yt921x_phylink_mac_config,
};

static const struct dsa_switch_ops yt921x_dsa_switch_ops = {
	/* mib */
	.get_strings		= yt921x_dsa_get_strings,
	.get_ethtool_stats	= yt921x_dsa_get_ethtool_stats,
	.get_sset_count		= yt921x_dsa_get_sset_count,
	.get_eth_mac_stats	= yt921x_dsa_get_eth_mac_stats,
	.get_eth_ctrl_stats	= yt921x_dsa_get_eth_ctrl_stats,
	.get_rmon_stats		= yt921x_dsa_get_rmon_stats,
	.get_stats64		= yt921x_dsa_get_stats64,
	.get_pause_stats	= yt921x_dsa_get_pause_stats,
	/* eee */
	.support_eee		= dsa_supports_eee,
	.set_mac_eee		= yt921x_dsa_set_mac_eee,
	.get_mac_eee		= yt921x_dsa_get_mac_eee,
	.get_wol		= yt921x_dsa_get_wol,
	.set_wol		= yt921x_dsa_set_wol,
	/* tc */
	.cls_flower_add		= yt921x_dsa_cls_flower_add,
	.cls_flower_del		= yt921x_dsa_cls_flower_del,
	.cls_flower_stats	= yt921x_dsa_cls_flower_stats,
	.port_setup_tc		= yt921x_dsa_port_setup_tc,
	.port_policer_add	= yt921x_dsa_port_policer_add,
	.port_policer_del	= yt921x_dsa_port_policer_del,
	/* dcb */
	.port_get_default_prio	= yt921x_dsa_port_get_default_prio,
	.port_set_default_prio	= yt921x_dsa_port_set_default_prio,
	.port_get_apptrust	= yt921x_dsa_port_get_apptrust,
	.port_set_apptrust	= yt921x_dsa_port_set_apptrust,
	/* mtu */
	.port_change_mtu	= yt921x_dsa_port_change_mtu,
	.port_max_mtu		= yt921x_dsa_port_max_mtu,
	/* mirror */
	.port_mirror_del	= yt921x_dsa_port_mirror_del,
	.port_mirror_add	= yt921x_dsa_port_mirror_add,
	/* lag */
	.port_lag_leave		= yt921x_dsa_port_lag_leave,
	.port_lag_join		= yt921x_dsa_port_lag_join,
	/* fdb */
	.port_fdb_dump		= yt921x_dsa_port_fdb_dump,
	.port_fast_age		= yt921x_dsa_port_fast_age,
	.set_ageing_time	= yt921x_dsa_set_ageing_time,
	.port_fdb_del		= yt921x_dsa_port_fdb_del,
	.port_fdb_add		= yt921x_dsa_port_fdb_add,
	.port_mdb_del		= yt921x_dsa_port_mdb_del,
	.port_mdb_add		= yt921x_dsa_port_mdb_add,
	/* vlan */
	.port_vlan_filtering	= yt921x_dsa_port_vlan_filtering,
	.port_vlan_del		= yt921x_dsa_port_vlan_del,
	.port_vlan_add		= yt921x_dsa_port_vlan_add,
	/* dscp */
	.port_get_dscp_prio	= yt921x_dsa_port_get_dscp_prio,
	.port_del_dscp_prio	= yt921x_dsa_port_del_dscp_prio,
	.port_add_dscp_prio	= yt921x_dsa_port_add_dscp_prio,
	/* bridge */
	.port_pre_bridge_flags	= yt921x_dsa_port_pre_bridge_flags,
	.port_bridge_flags	= yt921x_dsa_port_bridge_flags,
	.port_mrouter		= yt921x_dsa_port_mrouter,
	.port_bridge_leave	= yt921x_dsa_port_bridge_leave,
	.port_bridge_join	= yt921x_dsa_port_bridge_join,
	/* mst */
	.port_mst_state_set	= yt921x_dsa_port_mst_state_set,
	.vlan_msti_set		= yt921x_dsa_vlan_msti_set,
	.port_stp_state_set	= yt921x_dsa_port_stp_state_set,
	/* port */
	.get_tag_protocol	= yt921x_dsa_get_tag_protocol,
	.port_change_conduit	= yt921x_dsa_port_change_conduit,
	.phylink_get_caps	= yt921x_dsa_phylink_get_caps,
	.port_setup		= yt921x_dsa_port_setup,
	.port_enable		= yt921x_dsa_port_enable,
	.port_disable		= yt921x_dsa_port_disable,
	/* chip */
	.setup			= yt921x_dsa_setup,
	.teardown		= yt921x_dsa_teardown,
	.devlink_param_get	= yt921x_devlink_param_get,
	.devlink_param_set	= yt921x_devlink_param_set,
};

static void yt921x_mdio_shutdown(struct mdio_device *mdiodev)
{
	struct yt921x_priv *priv = mdiodev_get_drvdata(mdiodev);

	if (!priv)
		return;

#if IS_ENABLED(CONFIG_NET_DSA_YT921X_DEBUG)
	cancel_delayed_work_sync(&priv->storm_guard_work);
#endif

	dsa_switch_shutdown(&priv->ds);
}

static void yt921x_mdio_remove(struct mdio_device *mdiodev)
{
	struct yt921x_priv *priv = mdiodev_get_drvdata(mdiodev);

	if (!priv)
		return;

	for (size_t i = ARRAY_SIZE(priv->ports); i-- > 0; ) {
		struct yt921x_port *pp = &priv->ports[i];

		disable_delayed_work_sync(&pp->mib_read);
	}

#if IS_ENABLED(CONFIG_NET_DSA_YT921X_DEBUG)
	cancel_delayed_work_sync(&priv->storm_guard_work);
#endif

	dsa_unregister_switch(&priv->ds);

	yt921x_proc_exit(priv);
	mutex_destroy(&priv->reg_lock);
}

static int yt921x_mdio_probe(struct mdio_device *mdiodev)
{
	struct device *dev = &mdiodev->dev;
	struct yt921x_reg_mdio *mdio;
	struct yt921x_priv *priv;
	struct dsa_switch *ds;
	int res;
	u32 switchid = 0;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mdio = devm_kzalloc(dev, sizeof(*mdio), GFP_KERNEL);
	if (!mdio)
		return -ENOMEM;

	mdio->bus = mdiodev->bus;
	mdio->addr = mdiodev->addr;
	if (of_property_read_u32(dev->of_node, "motorcomm,switchid",
				 &switchid))
		of_property_read_u32(dev->of_node, "switchid", &switchid);
	if (switchid > 1) {
		dev_err(dev, "Invalid switchid %u\n", switchid);
		return -EINVAL;
	}
	mdio->switchid = switchid;

	mutex_init(&priv->reg_lock);

	res = yt921x_proc_init(priv);
	if (res) {
		mutex_destroy(&priv->reg_lock);
		return res;
	}

	priv->reg_ops = &yt921x_reg_ops_mdio;
	priv->reg_ctx = mdio;
	priv->primary_cpu_port = -1;
	priv->secondary_cpu_port = -1;
	priv->dt_rma_slow_action = -1;
	priv->dt_ctrlpkt_lldp_act = -1;
	priv->dt_ctrlpkt_lldp_eee_act = -1;
	priv->dt_led_ctrl_enabled = false;
	priv->dt_temp_sensor_supported = false;

	if (dev->of_node) {
		bool has_rma_slow_action;
		bool has_ctrlpkt_lldp_act;
		bool has_ctrlpkt_lldp_eee_act;
		bool has_secondary_conduit_user_mask = false;
		bool has_temp_sensor_supported;
		u32 val;

		has_rma_slow_action =
			of_property_present(dev->of_node, "motorcomm,rma-slow-action") ||
			of_property_present(dev->of_node, "rma-slow-action");
		has_ctrlpkt_lldp_act =
			of_property_present(dev->of_node, "motorcomm,ctrlpkt-lldp-act") ||
			of_property_present(dev->of_node, "ctrlpkt-lldp-act");
		has_ctrlpkt_lldp_eee_act =
			of_property_present(dev->of_node, "motorcomm,ctrlpkt-lldp-eee-act") ||
			of_property_present(dev->of_node, "ctrlpkt-lldp-eee-act");
		priv->dt_led_ctrl_enabled =
			of_property_read_bool(dev->of_node, "motorcomm,led-controller") ||
			of_property_read_bool(dev->of_node, "led-controller");
		has_temp_sensor_supported =
			of_property_present(dev->of_node, "motorcomm,temp-sensor-supported") ||
			of_property_present(dev->of_node, "temp-sensor-supported");
		priv->dt_temp_sensor_supported =
			of_property_read_bool(dev->of_node, "motorcomm,temp-sensor-supported") ||
			of_property_read_bool(dev->of_node, "temp-sensor-supported");
#if IS_ENABLED(CONFIG_NET_DSA_YT921X_CR881X)
		has_secondary_conduit_user_mask =
			of_machine_is_compatible("xiaomi,cr881x") &&
			(of_property_present(dev->of_node, "motorcomm,secondary-conduit-user-mask") ||
			 of_property_present(dev->of_node, "secondary-conduit-user-mask"));
#endif

		if (!of_property_read_u32(dev->of_node,
					  "motorcomm,rma-slow-action", &val))
			priv->dt_rma_slow_action = val;
		else if (!of_property_read_u32(dev->of_node,
					       "rma-slow-action", &val))
			priv->dt_rma_slow_action = val;

		if (!of_property_read_u32(dev->of_node,
					  "motorcomm,ctrlpkt-lldp-act", &val))
			priv->dt_ctrlpkt_lldp_act = val;
		else if (!of_property_read_u32(dev->of_node,
					       "ctrlpkt-lldp-act", &val))
			priv->dt_ctrlpkt_lldp_act = val;

		if (!of_property_read_u32(dev->of_node,
					  "motorcomm,ctrlpkt-lldp-eee-act", &val))
			priv->dt_ctrlpkt_lldp_eee_act = val;
		else if (!of_property_read_u32(dev->of_node,
					       "ctrlpkt-lldp-eee-act", &val))
			priv->dt_ctrlpkt_lldp_eee_act = val;

#if IS_ENABLED(CONFIG_NET_DSA_YT921X_CR881X)
		if (has_secondary_conduit_user_mask &&
		    (!of_property_read_u32(dev->of_node,
					   "motorcomm,secondary-conduit-user-mask", &val) ||
		     !of_property_read_u32(dev->of_node,
					   "secondary-conduit-user-mask", &val))) {
			priv->dt_secondary_conduit_user_mask =
				val & GENMASK(YT921X_PORT_NUM - 1, 0);
			priv->dt_secondary_conduit_user_mask_valid = true;
		}
#endif

		if (has_rma_slow_action || has_ctrlpkt_lldp_act ||
		    has_ctrlpkt_lldp_eee_act || has_secondary_conduit_user_mask ||
		    has_temp_sensor_supported)
			dev_info(dev,
				 "DT policy overrides parsed: rma-slow-action=%d ctrlpkt-lldp-act=0x%x ctrlpkt-lldp-eee-act=0x%x temp-sensor-supported=%u secondary-conduit-user-mask=0x%x\n",
				 priv->dt_rma_slow_action,
				 priv->dt_ctrlpkt_lldp_act,
				 priv->dt_ctrlpkt_lldp_eee_act,
				 priv->dt_temp_sensor_supported,
				 priv->dt_secondary_conduit_user_mask_valid ?
				 priv->dt_secondary_conduit_user_mask : 0U);
	}

	for (size_t i = 0; i < ARRAY_SIZE(priv->ports); i++) {
		struct yt921x_port *pp = &priv->ports[i];

		pp->index = i;
		pp->priv = priv;
		pp->mcast_flood = true;
		pp->bcast_flood = true;
		pp->mcast_fast_leave = false;
		INIT_DELAYED_WORK(&pp->mib_read, yt921x_poll_mib);
	}

#if IS_ENABLED(CONFIG_NET_DSA_YT921X_DEBUG)
	INIT_DELAYED_WORK(&priv->storm_guard_work, yt921x_storm_guard_workfn);
	priv->storm_guard_enabled = false;
	priv->storm_guard_primed = false;
	priv->storm_guard_pps = YT921X_STORM_GUARD_DEFAULT_PPS;
	priv->storm_guard_hold_ms = YT921X_STORM_GUARD_DEFAULT_HOLD_MS;
	priv->storm_guard_interval_ms = YT921X_STORM_GUARD_DEFAULT_INTERVAL_MS;
#endif

	ds = &priv->ds;
	ds->dev = dev;
	ds->assisted_learning_on_cpu_port = true;
	ds->priv = priv;
	ds->ops = &yt921x_dsa_switch_ops;
	ds->ageing_time_min = 1 * 5000;
	ds->ageing_time_max = U16_MAX * 5000;
	ds->phylink_mac_ops = &yt921x_phylink_mac_ops;
	ds->dscp_prio_mapping_is_global = true;
	ds->num_lag_ids = YT921X_LAG_NUM;
	ds->num_ports = YT921X_PORT_NUM;
	ds->num_tx_queues = YT921X_PRIO_NUM;

	mdiodev_set_drvdata(mdiodev, priv);

	res = dsa_register_switch(ds);
	if (res) {
		yt921x_proc_exit(priv);
		mutex_destroy(&priv->reg_lock);
	}

	return res;
}

static const struct of_device_id yt921x_of_match[] = {
	{ .compatible = "motorcomm,yt9215" },
	{}
};
MODULE_DEVICE_TABLE(of, yt921x_of_match);

static struct mdio_driver yt921x_mdio_driver = {
	.probe = yt921x_mdio_probe,
	.remove = yt921x_mdio_remove,
	.shutdown = yt921x_mdio_shutdown,
	.mdiodrv.driver = {
		.name = YT921X_NAME,
		.of_match_table = yt921x_of_match,
	},
};

mdio_module_driver(yt921x_mdio_driver);

MODULE_AUTHOR("David Yang <mmyangfl@gmail.com>");
MODULE_AUTHOR("zcop <hongson.hn@gmail.com>");
MODULE_DESCRIPTION("YT921x Enhanced DSA Driver");
MODULE_LICENSE("GPL");
