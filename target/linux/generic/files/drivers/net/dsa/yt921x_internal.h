/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Internal declarations for the split yt921x driver units.
 *
 * Copyright (c) 2025 David Yang
 * Copyright (c) 2026 zcop <hongson.hn@gmail.com>
 */

#ifndef __YT921X_INTERNAL_H
#define __YT921X_INTERNAL_H

#include <linux/dcbnl.h>
#include <linux/debugfs.h>
#include <linux/etherdevice.h>
#include <linux/fs.h>
#include <linux/if_bridge.h>
#include <linux/if_hsr.h>
#include <linux/if_vlan.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/iopoll.h>
#include <linux/jiffies.h>
#include <linux/mdio.h>
#include <linux/mii.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include <linux/tc_act/tc_csum.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>

#include <net/dsa.h>
#include <net/dscp.h>
#include <net/flow_offload.h>
#include <net/ieee8021q.h>
#include <net/pkt_cls.h>
#include <net/pkt_sched.h>

#include "yt921x.h"

struct yt921x_mib_desc {
	unsigned int size;
	unsigned int offset;
	const char *name;
};

struct yt921x_info {
	const char *name;
	u16 major;
	/* Unknown, seems to be plain enumeration */
	u8 mode;
	u8 extmode;
	/* Ports with integral GbE PHYs, not including MCU Port 10 */
	u16 internal_mask;
	/* External ports */
	u16 external_mask;
};

struct yt921x_reg_mdio {
	struct mii_bus *bus;
	int addr;
	/* SWITCH_ID_1 / SWITCH_ID_0 of the device */
	unsigned char switchid;
};

struct yt921x_meter {
	u32 cir;
	u32 cbs;
	u32 ebs;
	int unit;
};

static inline void
yt921x_record_err(struct yt921x_priv *priv, atomic64_t *counter,
		  enum yt921x_telemetry_stage stage, int err, int port,
		  u32 detail0, u32 detail1, unsigned long cookie,
		  const char *func, int line)
{
	if (counter)
		atomic64_inc(counter);

	WRITE_ONCE(priv->telemetry.last_err_stage, stage);
	WRITE_ONCE(priv->telemetry.last_err_code, err);
	WRITE_ONCE(priv->telemetry.last_err_port, port);
	WRITE_ONCE(priv->telemetry.last_err_detail0, detail0);
	WRITE_ONCE(priv->telemetry.last_err_detail1, detail1);
	WRITE_ONCE(priv->telemetry.last_err_cookie, cookie);
	WRITE_ONCE(priv->telemetry.last_err_func, func);
	WRITE_ONCE(priv->telemetry.last_err_line, line);
	WRITE_ONCE(priv->telemetry.last_err_jiffies, jiffies);
}

#define YT921X_RECORD_ERR(_priv, _counter_member, _stage, _err, _port, \
			  _detail0, _detail1, _cookie) \
	yt921x_record_err((_priv), &(_priv)->telemetry._counter_member, \
			  (_stage), (_err), (_port), (_detail0), (_detail1), \
			  (_cookie), __func__, __LINE__)

#define YT921X_METER_PKT_MODE		BIT(0)
#define YT921X_METER_SINGLE_BUCKET	BIT(1)
#define YT921X_ACL_METER_ID_BLACKHOLE	(YT921X_METER_NUM - 1)

static inline void u32p_replace_bits_unaligned(u32 *lo, u32 *hi,
						u64 val, u64 mask)
{
	*lo &= ~lower_32_bits(mask);
	*hi &= ~upper_32_bits(mask);
	*lo |= lower_32_bits(val);
	*hi |= upper_32_bits(val);
}

static inline u32 mac_hi4_to_cpu(const unsigned char *addr)
{
	return ((u32)addr[0] << 24) | ((u32)addr[1] << 16) |
	       ((u32)addr[2] << 8) | addr[3];
}

static inline u16 mac_lo2_to_cpu(const unsigned char *addr)
{
	return ((u16)addr[4] << 8) | addr[5];
}

extern const struct yt921x_mib_desc yt921x_mib_descs[];
extern const size_t yt921x_mib_descs_count;
extern const struct yt921x_info yt921x_infos[];
extern const struct yt921x_reg_ops yt921x_reg_ops_mdio;

bool yt921x_is_external_port(const struct yt921x_priv *priv, int port);
bool yt921x_is_primary_cpu_port(const struct yt921x_priv *priv, int port);
bool yt921x_is_secondary_cpu_port(const struct yt921x_priv *priv, int port);

int yt921x_reg_read(struct yt921x_priv *priv, u32 reg, u32 *valp);
int yt921x_reg_write(struct yt921x_priv *priv, u32 reg, u32 val);
int yt921x_reg_wait(struct yt921x_priv *priv, u32 reg, u32 mask, u32 *valp);
int yt921x_reg_update_bits(struct yt921x_priv *priv, u32 reg, u32 mask, u32 val);
int yt921x_reg_set_bits(struct yt921x_priv *priv, u32 reg, u32 mask);
int yt921x_reg_clear_bits(struct yt921x_priv *priv, u32 reg, u32 mask);
int yt921x_reg_toggle_bits(struct yt921x_priv *priv, u32 reg, u32 mask, bool set);
int yt921x_reg64_read(struct yt921x_priv *priv, u32 reg, u64 *valp);
int yt921x_reg64_write(struct yt921x_priv *priv, u32 reg, u64 val);
int yt921x_reg64_update_bits(struct yt921x_priv *priv, u32 reg, u64 mask, u64 val);
int yt921x_reg64_clear_bits(struct yt921x_priv *priv, u32 reg, u64 mask);
int yt921x_reg96_write(struct yt921x_priv *priv, u32 reg, const u32 vals[3]);

int yt921x_intif_read(struct yt921x_priv *priv, int port, int reg, u16 *valp);
int yt921x_intif_write(struct yt921x_priv *priv, int port, int reg, u16 val);
int yt921x_extif_read(struct yt921x_priv *priv, int port, int reg, u16 *valp);
int yt921x_extif_write(struct yt921x_priv *priv, int port, int reg, u16 val);
int yt921x_mbus_int_init(struct yt921x_priv *priv, struct device_node *mnp);
int yt921x_mbus_ext_init(struct yt921x_priv *priv, struct device_node *mnp);

int yt921x_loop_detect_setup_locked(struct yt921x_priv *priv);
int yt921x_rma_setup_locked(struct yt921x_priv *priv);
int yt921x_ctrlpkt_setup_locked(struct yt921x_priv *priv);
int yt921x_vlan_mode_setup_locked(struct yt921x_priv *priv);
int yt921x_devlink_param_get(struct dsa_switch *ds, u32 id,
			     struct devlink_param_gset_ctx *ctx);
int yt921x_devlink_param_set(struct dsa_switch *ds, u32 id,
			     struct devlink_param_gset_ctx *ctx);
int yt921x_devlink_params_register(struct dsa_switch *ds);
void yt921x_devlink_params_unregister(struct dsa_switch *ds);
int yt921x_apply_flood_filters_locked(struct yt921x_priv *priv);
int yt921x_refresh_flood_masks_locked(struct yt921x_priv *priv);
int yt921x_proc_init(struct yt921x_priv *priv);
void yt921x_proc_exit(struct yt921x_priv *priv);

u32 yt921x_tbf_token_to_rate_kbps(u32 token, unsigned int slot_ns, u8 token_level);
u64 yt921x_tbf_token_to_burst_bytes(u32 token, u8 token_level);
int yt921x_read_mib(struct yt921x_priv *priv, int port);
void yt921x_poll_mib(struct work_struct *work);
int yt921x_meter_tfm(struct yt921x_priv *priv, int port, unsigned int slot_ns,
		     u64 rate, u64 burst, unsigned int flags,
		     u32 cir_max, u32 cbs_max, int unit_max,
		     struct yt921x_meter *meterp);
int yt921x_trap_copp_default_apply(struct yt921x_priv *priv);
void yt921x_storm_guard_workfn(struct work_struct *work);

int yt921x_mirror_add(struct yt921x_priv *priv, int port, bool ingress,
		      int to_local_port, struct netlink_ext_ack *extack);
int yt921x_mirror_del(struct yt921x_priv *priv, int port, bool ingress);
u32 yt921x_non_cpu_port_mask(const struct yt921x_priv *priv);
int yt921x_userport_standalone(struct yt921x_priv *priv, int port);
int yt921x_port_isolation_set(struct yt921x_priv *priv, int port, u32 blocked_mask);
int yt921x_userport_cpu_isolation_set(struct yt921x_priv *priv, int port, int cpu_port);
int yt921x_userport_current_cpu_port_get(struct yt921x_priv *priv, int port, int *cpu_port);
int yt921x_secondary_cpu_isolation_sync(struct yt921x_priv *priv, int primary_cpu_port);
int yt921x_conduit_fdb_retarget(struct yt921x_priv *priv, int user_port,
				int old_cpu_port, int new_cpu_port);
int yt921x_dsa_conduit_to_cpu_port(struct dsa_switch *ds, struct net_device *conduit,
				   struct netlink_ext_ack *extack);

int yt921x_stp_encode_state(int port, u8 state, u32 *ctrl);
int yt921x_port_ctrl_apply_dt(struct yt921x_priv *priv, int port, bool allow_managed);
int yt921x_port_down(struct yt921x_priv *priv, int port);
void yt921x_phylink_mac_link_down(struct phylink_config *config, unsigned int mode,
				  phy_interface_t interface);
void yt921x_phylink_mac_link_up(struct phylink_config *config,
				struct phy_device *phydev, unsigned int mode,
				phy_interface_t interface, int speed, int duplex,
				bool tx_pause, bool rx_pause);
void yt921x_phylink_mac_config(struct phylink_config *config, unsigned int mode,
			       const struct phylink_link_state *state);

int yt921x_chip_reset(struct yt921x_priv *priv);
int yt921x_chip_setup(struct yt921x_priv *priv);
int yt921x_qos_remark_dscp_set(struct yt921x_priv *priv, u8 prio, u8 dp, u8 dscp);

void yt921x_dsa_get_strings(struct dsa_switch *ds, int port, u32 stringset, u8 *data);
void yt921x_dsa_get_ethtool_stats(struct dsa_switch *ds, int port, u64 *data);
int yt921x_dsa_get_sset_count(struct dsa_switch *ds, int port, int sset);
void yt921x_dsa_get_eth_mac_stats(struct dsa_switch *ds, int port,
				  struct ethtool_eth_mac_stats *mac_stats);
void yt921x_dsa_get_eth_ctrl_stats(struct dsa_switch *ds, int port,
				   struct ethtool_eth_ctrl_stats *ctrl_stats);
void yt921x_dsa_get_rmon_stats(struct dsa_switch *ds, int port,
			       struct ethtool_rmon_stats *rmon_stats,
			       const struct ethtool_rmon_hist_range **ranges);
void yt921x_dsa_get_stats64(struct dsa_switch *ds, int port,
			    struct rtnl_link_stats64 *s);
void yt921x_dsa_get_pause_stats(struct dsa_switch *ds, int port,
				struct ethtool_pause_stats *pause_stats);
int yt921x_dsa_set_mac_eee(struct dsa_switch *ds, int port, struct ethtool_keee *e);
int yt921x_dsa_get_mac_eee(struct dsa_switch *ds, int port, struct ethtool_keee *e);
void yt921x_dsa_get_wol(struct dsa_switch *ds, int port,
			struct ethtool_wolinfo *w);
int yt921x_dsa_set_wol(struct dsa_switch *ds, int port,
		       struct ethtool_wolinfo *w);

int yt921x_dsa_cls_flower_add(struct dsa_switch *ds, int port,
			      struct flow_cls_offload *cls, bool ingress);
int yt921x_dsa_cls_flower_del(struct dsa_switch *ds, int port,
			      struct flow_cls_offload *cls, bool ingress);
int yt921x_dsa_cls_flower_stats(struct dsa_switch *ds, int port,
				struct flow_cls_offload *cls, bool ingress);

int yt921x_dsa_port_setup_tc(struct dsa_switch *ds, int port,
			     enum tc_setup_type type, void *type_data);
int yt921x_dsa_port_policer_add(struct dsa_switch *ds, int port,
				struct dsa_mall_policer_tc_entry *policer);
void yt921x_dsa_port_policer_del(struct dsa_switch *ds, int port);

int yt921x_dsa_port_get_default_prio(struct dsa_switch *ds, int port);
int yt921x_dsa_port_set_default_prio(struct dsa_switch *ds, int port, u8 prio);
int yt921x_dsa_port_get_apptrust(struct dsa_switch *ds, int port, u8 *sel,
				 int *nsel);
int yt921x_dsa_port_set_apptrust(struct dsa_switch *ds, int port, const u8 *sel,
				 int nsel);
int yt921x_dsa_port_change_mtu(struct dsa_switch *ds, int port, int new_mtu);
int yt921x_dsa_port_max_mtu(struct dsa_switch *ds, int port);
int yt921x_dsa_port_get_dscp_prio(struct dsa_switch *ds, int port, u8 dscp);
int yt921x_dsa_port_del_dscp_prio(struct dsa_switch *ds, int port, u8 dscp, u8 prio);
int yt921x_dsa_port_add_dscp_prio(struct dsa_switch *ds, int port, u8 dscp, u8 prio);

void yt921x_dsa_port_mirror_del(struct dsa_switch *ds, int port,
				struct dsa_mall_mirror_tc_entry *mirror);
int yt921x_dsa_port_mirror_add(struct dsa_switch *ds, int port,
			       struct dsa_mall_mirror_tc_entry *mirror,
			       bool ingress, struct netlink_ext_ack *extack);

int yt921x_dsa_port_lag_leave(struct dsa_switch *ds, int port, struct dsa_lag lag);
int yt921x_dsa_port_lag_join(struct dsa_switch *ds, int port, struct dsa_lag lag,
			     struct netdev_lag_upper_info *info,
			     struct netlink_ext_ack *extack);

int yt921x_dsa_port_fdb_dump(struct dsa_switch *ds, int port,
			     dsa_fdb_dump_cb_t *cb, void *data);
void yt921x_dsa_port_fast_age(struct dsa_switch *ds, int port);
int yt921x_dsa_set_ageing_time(struct dsa_switch *ds, unsigned int msecs);
int yt921x_dsa_port_fdb_del(struct dsa_switch *ds, int port,
			    const unsigned char *addr, u16 vid,
			    struct dsa_db db);
int yt921x_dsa_port_fdb_add(struct dsa_switch *ds, int port,
			    const unsigned char *addr, u16 vid,
			    struct dsa_db db);
int yt921x_dsa_port_mdb_del(struct dsa_switch *ds, int port,
			    const struct switchdev_obj_port_mdb *mdb,
			    struct dsa_db db);
int yt921x_dsa_port_mdb_add(struct dsa_switch *ds, int port,
			    const struct switchdev_obj_port_mdb *mdb,
			    struct dsa_db db);

int yt921x_dsa_port_vlan_filtering(struct dsa_switch *ds, int port,
				   bool vlan_filtering,
				   struct netlink_ext_ack *extack);
int yt921x_dsa_port_vlan_del(struct dsa_switch *ds, int port,
			     const struct switchdev_obj_port_vlan *vlan);
int yt921x_dsa_port_vlan_add(struct dsa_switch *ds, int port,
			     const struct switchdev_obj_port_vlan *vlan,
			     struct netlink_ext_ack *extack);

int yt921x_dsa_port_pre_bridge_flags(struct dsa_switch *ds, int port,
				     struct switchdev_brport_flags flags,
				     struct netlink_ext_ack *extack);
int yt921x_dsa_port_bridge_flags(struct dsa_switch *ds, int port,
				 struct switchdev_brport_flags flags,
				 struct netlink_ext_ack *extack);
int yt921x_dsa_port_mrouter(struct dsa_switch *ds, int port, bool mrouter);
void yt921x_dsa_port_bridge_leave(struct dsa_switch *ds, int port,
				  struct dsa_bridge bridge);
int yt921x_dsa_port_bridge_join(struct dsa_switch *ds, int port,
				struct dsa_bridge bridge, bool *tx_fwd_offload,
				struct netlink_ext_ack *extack);

int yt921x_dsa_port_mst_state_set(struct dsa_switch *ds, int port,
				  const struct switchdev_mst_state *st);
int yt921x_dsa_vlan_msti_set(struct dsa_switch *ds, struct dsa_bridge bridge,
			     const struct switchdev_vlan_msti *vlan_msti);
void yt921x_dsa_port_stp_state_set(struct dsa_switch *ds, int port, u8 state);

enum dsa_tag_protocol yt921x_dsa_get_tag_protocol(struct dsa_switch *ds, int port,
						   enum dsa_tag_protocol mp);
int yt921x_dsa_port_change_conduit(struct dsa_switch *ds, int port,
				   struct net_device *conduit,
				   struct netlink_ext_ack *extack);
int yt921x_dsa_port_setup(struct dsa_switch *ds, int port);
int yt921x_dsa_port_enable(struct dsa_switch *ds, int port,
			   struct phy_device *phy);
void yt921x_dsa_port_disable(struct dsa_switch *ds, int port);
void yt921x_dsa_phylink_get_caps(struct dsa_switch *ds, int port,
				 struct phylink_config *config);

#endif
