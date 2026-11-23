/* Host transaction tests. Hardware parser/priority behavior needs live tests. */
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
#define BIT(n) (1u << (n))
#define GENMASK(h, l) ((~0u << (l)) & (~0u >> (31 - (h))))
#define ETH_P_8021AD 0x88a8
#define VLAN_N_VID 4096
#define BRIDGE_VLAN_INFO_PVID 2
#define BRIDGE_VLAN_INFO_UNTAGGED 4
#define QCA8K_QINQ_MAX_VLANS 56
#define BITS_TO_LONGS(n) (((n) + sizeof(unsigned long) * 8 - 1) / (sizeof(unsigned long) * 8))
static bool test_bit(unsigned n, const unsigned long *a) { return !!(a[n / (8 * sizeof(*a))] & (1ul << (n % (8 * sizeof(*a))))); }
static void set_bit(unsigned n, unsigned long *a) { a[n / (8 * sizeof(*a))] |= 1ul << (n % (8 * sizeof(*a))); }
static void clear_bit(unsigned n, unsigned long *a) { a[n / (8 * sizeof(*a))] &= ~(1ul << (n % (8 * sizeof(*a)))); }
#define QCA8K_ID_QCA8337 0x13
#define QCA8K_REG_GLOBAL_FW_CTRL1 0x624
#define QCA8K_REG_PORT_VLAN_CTRL0(p) (0x420 + (p) * 8)
#define QCA8K_REG_PORT_VLAN_CTRL1(p) (0x424 + (p) * 8)
#define QCA8K_PORT_VLAN_CVID_MASK 0x0fff0000
#define QCA8K_PORT_VLAN_CVID(vid) ((u32)(vid) << 16)
#define QCA8K_PORT_VLAN_SVID(vid) (vid)
#define QCA8K_PORT_LOOKUP_CTRL(p) (0x660 + (p) * 12)
#define QCA8K_PORT_LOOKUP_VLAN_MODE_MASK 0x300
#define QCA8K_PORT_LOOKUP_VLAN_MODE_FALLBACK 0x100
#define QCA8K_PORT_LOOKUP_VLAN_MODE_SECURE 0x300
#define DSA_TAG_8021Q_MAX_NUM_BRIDGES 7
#define dev_err(...) ((void)0)
#define dev_warn(...) ((void)0)
#define usleep_range(...) ((void)0)
#define lockdep_assert_held(lock) assert(*(lock))

struct net_device { void *master; };
struct dsa_bridge { struct net_device *dev; unsigned int num; };
struct dsa_port {
	int index;
	struct dsa_switch *ds;
	struct dsa_port *cpu_dp;
	struct dsa_bridge *bridge;
	struct net_device *user;
};
struct dsa_switch {
	void *priv, *tag_8021q_ctx;
	unsigned max_num_bridges, cpu_mask, user_mask;
	bool needs_standalone_vlan_filtering, configure_vlan_while_not_filtering;
	bool fdb_isolation;
	struct dsa_port ports[7];
};
struct qca8k_qinq_vlan { u16 vid, flags; u8 port, cpu, slot; bool used, bridge, installed; };
struct qca8k_priv {
	struct dsa_switch *ds;
	int dev, reg_mutex, qinq_status_lock;
	bool qinq, qinq_acl_owned;
	u8 switch_id, qinq_fault_map;
	u16 qinq_native_vid[7], qinq_pvid[7];
	u8 qinq_vlan_filtering;
	struct qca8k_qinq_vlan qinq_vlans[QCA8K_QINQ_MAX_VLANS];
	unsigned long qinq_host_vlans[2][BITS_TO_LONGS(VLAN_N_VID)];
};
static struct dsa_switch ds;
static struct qca8k_priv priv;
static struct net_device users[7];
static bool vtu_member[7][4096], vtu_untagged[7][4096];
#define dsa_cpu_ports(ds) ((ds)->cpu_mask)
#define dsa_switch_for_each_user_port(dp, ds) \
	for (int p = 0; p < 7; p++) if (((ds)->user_mask & BIT(p)) && ((dp) = &(ds)->ports[p]))
#define dsa_switch_for_each_cpu_port(dp, ds) \
	for (int p = 0; p < 7; p++) if (((ds)->cpu_mask & BIT(p)) && ((dp) = &(ds)->ports[p]))
static u32 regs[0x5ae00 / 4];
static int write_count, fail_write, enabled_ports, registration_error, rtnl;
static bool busy_forever;
static int corrupt_read_type;

static struct dsa_port *dsa_to_port(struct dsa_switch *sw, int port)
{
	assert(port >= 0 && port < 7);
	return &sw->ports[port];
}
static bool dsa_is_user_port(struct dsa_switch *sw, int port)
{
	return sw->user_mask & BIT(port);
}
static u16 dsa_tag_8021q_standalone_vid(const struct dsa_port *dp)
{
	return 3072 + dp->index;
}
static void mutex_lock(int *lock) { assert(!*lock); *lock = 1; }
static void mutex_unlock(int *lock) { assert(*lock); *lock = 0; }
static void rtnl_lock(void) { assert(!rtnl); rtnl = 1; }
static void rtnl_unlock(void) { assert(rtnl); rtnl = 0; }
static int qca8k_read(struct qca8k_priv *p, u32 reg, u32 *value)
{
	assert(!(reg & 3) && reg / 4 < sizeof(regs) / sizeof(regs[0]));
	*value = regs[reg / 4];
	if (reg == 0x400 && busy_forever)
		*value |= BIT(31);
	return 0;
}
static int qca8k_write(struct qca8k_priv *p, u32 reg, u32 value)
{
	assert(!(reg & 3) && reg / 4 < sizeof(regs) / sizeof(regs[0]));
	if (++write_count == fail_write)
		return -EIO;
	regs[reg / 4] = value;
	if (reg == 0x400 && (value & BIT(31))) {
		unsigned int slot = value & 0x7f, type = (value >> 8) & 3;
		unsigned int words = type == 2 ? 3 : 5;
		u32 base = (type == 0 ? 0x58000 : type == 1 ? 0x59000 : 0x5a000) +
			   slot * (type == 2 ? 16 : 32);

		assert(p->reg_mutex && slot <= 95 && type < 3);
		for (unsigned int i = 0; i < words; i++) {
			if (value & BIT(10))
				regs[(0x404 + i * 4) / 4] = regs[(base + i * 4) / 4];
			else
				regs[(base + i * 4) / 4] = regs[(0x404 + i * 4) / 4];
		}
		if (corrupt_read_type == (int)type && slot == 90 &&
		    (value & BIT(10)) && (regs[(0x59000 + 32 * slot + 16) / 4] & 7))
			regs[0x40c / 4] ^= 1;
		regs[reg / 4] &= ~BIT(31);
	}
	return 0;
}
static int qca8k_rmw(struct qca8k_priv *p, u32 reg, u32 mask, u32 value)
{
	return qca8k_write(p, reg, (regs[reg / 4] & ~mask) | value);
}
static void qca8k_port_set_status(struct qca8k_priv *p, int port, int on)
{
	if (on)
		enabled_ports |= BIT(port);
	else
		enabled_ports &= ~BIT(port);
}
static int dsa_tag_8021q_register(struct dsa_switch *sw, u16 proto)
{
	assert(proto == htons(ETH_P_8021AD));
	assert(rtnl);
	if (registration_error)
		return registration_error;
	sw->tag_8021q_ctx = (void *)1;
	return 0;
}
static void dsa_tag_8021q_unregister(struct dsa_switch *sw)
{
	assert(rtnl);
	sw->tag_8021q_ctx = NULL;
}

/* Policy query is mocked here; the real public helper is covered by the
 * tagger/adapter policy tests. This harness exercises hardware transactions.
 */
static bool dsa_qca_qinq_customer_vid_valid(struct dsa_switch *sw, u16 vid)
{
	if (!vid || vid >= 4095 || (vid >= 3073 && vid <= 3076))
		return false;
	for (unsigned n = 1; n <= 7; n++)
		if (vid == (0xc00 | ((n & 3) << 4) | ((n & 4) << 7)))
			return false;
	return true;
}
static struct net_device *dsa_port_bridge_dev_get(struct dsa_port *dp) { return (void *)dp->bridge; }
static void *netdev_master_upper_dev_get(struct net_device *dev) { return dev->master; }
static int qca8k_vlan_add(struct qca8k_priv *p, int port, u16 vid, bool untagged)
{
	int ret = qca8k_write(p, 0x610, vid);
	if (!ret) { vtu_member[port][vid] = true; vtu_untagged[port][vid] = untagged; }
	return ret;
}
static int qca8k_vlan_del(struct qca8k_priv *p, int port, u16 vid)
{
	int ret = qca8k_write(p, 0x610, vid);
	for (int i = 0; i < QCA8K_QINQ_MAX_VLANS; i++) {
		struct qca8k_qinq_vlan *v = &p->qinq_vlans[i];
		if (v->used && v->vid == vid && (v->port == port || v->cpu == port))
			assert(!(regs[(0x59000 + v->slot * 32 + 16) / 4] & 7));
	}
	if (!ret) vtu_member[port][vid] = false;
	return ret;
}

struct phylink_config { struct dsa_port *dp; };
struct phy_device;
typedef int phy_interface_t;
#define dsa_phylink_to_port(config) ((config)->dp)
#define phylink_autoneg_inband(mode) (mode)
#define dsa_port_is_cpu(dp) ((dp)->ds->cpu_mask & BIT((dp)->index))
enum { SPEED_10 = 10, SPEED_100 = 100, SPEED_1000 = 1000, DUPLEX_FULL = 1 };
#include "driver-link.h"
#include "driver-setup.h"

enum { DSA_DB_BRIDGE, DSA_DB_PORT };
struct dsa_db {
	int type;
	union { struct dsa_bridge bridge; struct dsa_port *dp; };
};
struct switchdev_obj_port_mdb { u16 vid; };
struct dsa_notifier_fdb_info {
	struct dsa_port *dp;
	const unsigned char *addr;
	u16 vid;
	struct dsa_db db;
};
struct dsa_notifier_mdb_info {
	const struct dsa_port *dp;
	const struct switchdev_obj_port_mdb *mdb;
	struct dsa_db db;
};
enum { DSA_NOTIFIER_FDB_ADD, DSA_NOTIFIER_FDB_DEL,
	DSA_NOTIFIER_MDB_ADD, DSA_NOTIFIER_MDB_DEL };
static int dsa_port_notify(const struct dsa_port *dp, unsigned long event, void *arg);

#include "driver-database.h"

static unsigned int notified_bridge;
static u16 notified_vid;
static int dsa_port_notify(const struct dsa_port *dp, unsigned long event, void *arg)
{
	struct dsa_db db;
	u16 vid;

	if (event == DSA_NOTIFIER_FDB_ADD || event == DSA_NOTIFIER_FDB_DEL) {
		struct dsa_notifier_fdb_info *info = arg;
		db = info->db;
		vid = info->vid;
	} else {
		struct dsa_notifier_mdb_info *info = arg;
		assert(event == DSA_NOTIFIER_MDB_ADD || event == DSA_NOTIFIER_MDB_DEL);
		db = info->db;
		vid = info->mdb->vid;
	}
	notified_bridge = db.bridge.num;
	notified_vid = qca8k_qinq_db_vid(&priv, dp->index, vid, db);
	return 0;
}

static void reset(void)
{
	assert(!rtnl);
	memset(&ds, 0, sizeof(ds));
	memset(&priv, 0, sizeof(priv));
	memset(regs, 0, sizeof(regs));
	memset(vtu_member, 0, sizeof(vtu_member));
	memset(vtu_untagged, 0, sizeof(vtu_untagged));
	memset(users, 0, sizeof(users));
	ds.priv = &priv;
	ds.cpu_mask = BIT(0) | BIT(5);
	ds.user_mask = GENMASK(4, 1);
	priv.ds = &ds;
	priv.qinq = true;
	priv.switch_id = 0x13;
	for (int i = 0; i < 7; i++) {
		ds.ports[i].index = i;
		ds.ports[i].ds = &ds;
		ds.ports[i].user = &users[i];
		ds.ports[i].cpu_dp = &ds.ports[0];
	}
	ds.ports[4].cpu_dp = &ds.ports[5];
	write_count = fail_write = registration_error = enabled_ports = 0;
	busy_forever = false;
	corrupt_read_type = -1;
	regs[0x30 / 4] = 0x80000001;
}
static void cleared(void)
{
	assert(!priv.qinq_acl_owned && !ds.tag_8021q_ctx && !priv.reg_mutex);
	assert(!(enabled_ports & GENMASK(4, 1)));
	for (int slot = 89; slot <= 95; slot++)
		assert(regs[QINQ_ACL_M(slot, 4) / 4] == 0);
}
static void test_customer(void)
{
	int before, add_writes, del_writes;
	reset();
	assert(qca8k_qinq_setup(&ds) == 0);
	assert(ds.needs_standalone_vlan_filtering);
	assert(regs[QINQ_ACL_M(93, 2) / 4] == 0xff0fff00);
	assert(regs[QINQ_ACL_M(93, 3) / 4] == 0xf);
	assert(regs[QINQ_ACL_M(93, 4) / 4] == 0xff);
	assert(qca8k_qinq_customer_add(&ds, 4, 0, 0) == 0);
	assert(qca8k_qinq_customer_add(&ds, 4, 3076, 0) == -EOPNOTSUPP);
	assert(qca8k_qinq_customer_add(&ds, 4, 4095, 0) == -EOPNOTSUPP);
	ds.ports[1].bridge = (void *)1;
	assert(qca8k_qinq_customer_add(&ds, 1, 35, 0) == -EOPNOTSUPP);
	ds.ports[1].bridge = NULL;
	assert(qca8k_qinq_customer_add(&ds, 4, 35, 1) == -EOPNOTSUPP);
	assert(qca8k_qinq_customer_add(&ds, 4, 35, 0) == 0);
	assert(qca8k_qinq_customer_find(&priv, 4, 35) && !test_bit(35, priv.qinq_host_vlans[1]));
	assert(vtu_member[4][35] && !vtu_member[5][35]);
	assert(!(regs[QINQ_ACL_M(94, 4) / 4] & 7));
	before = write_count;
	assert(qca8k_qinq_customer_add(&ds, 4, 36, 1) == -EOPNOTSUPP);
	assert(write_count == before);
	assert(qca8k_qinq_customer_add(&ds, 5, 35, 0) == 0);
	add_writes = write_count - before;
	assert(vtu_member[5][35] && test_bit(35, priv.qinq_host_vlans[1]));
	before = write_count;
	assert(qca8k_qinq_check_conduit(&priv, 4, 0) == -EBUSY);
	assert(qca8k_qinq_conduit(&priv, 4, 0) == -EBUSY && write_count == before);
	assert(qca8k_qinq_check_conduit(&priv, 4, 5) == 0);
	assert(regs[QINQ_ACL_M(94, 4) / 4] == 0xff);
	assert(regs[QINQ_ACL_V(94, 2) / 4] == ((35u << 24) | (3076u << 8)));
	assert(regs[QINQ_ACL_A(94, 2) / 4] == 0x12);
	assert(qca8k_qinq_customer_del(&ds, 4, 35) == 0);
	assert(qca8k_qinq_customer_add(&ds, 4, 35, 0) == 0);
	assert(regs[QINQ_ACL_M(94, 4) / 4] == 0xff); /* CPU member survived */
	before = write_count;
	assert(qca8k_qinq_customer_del(&ds, 4, 35) == 0);
	del_writes = write_count - before;
	assert(!(regs[QINQ_ACL_M(94, 4) / 4] & 7));
	assert(!vtu_member[4][35] && vtu_member[5][35]);
	assert(qca8k_qinq_customer_del(&ds, 5, 35) == 0);
	assert(!qca8k_qinq_customer_busy(&priv, 4) && !vtu_member[5][35]);
	assert(qca8k_qinq_check_conduit(&priv, 4, 0) == 0);
	/* Exercise CPU-first cleanup as well as the VLAN core's user-first path. */
	assert(qca8k_qinq_customer_add(&ds, 4, 4094, 0) == 0);
	assert(qca8k_qinq_customer_add(&ds, 5, 4094, 0) == 0);
	assert(qca8k_qinq_customer_del(&ds, 5, 4094) == 0);
	assert(qca8k_qinq_customer_del(&ds, 4, 4094) == 0 && !qca8k_qinq_customer_busy(&priv, 4));
	assert(qca8k_qinq_block_native(&priv, 4) == 0);
	assert(!(regs[QINQ_ACL_M(93, 4) / 4] & 7));
	assert(qca8k_qinq_conduit(&priv, 4, 5) == 0);
	assert(regs[QINQ_ACL_M(93, 4) / 4] == 0xff);
	for (int stage = 0; stage < 2; stage++) {
		int count = stage ? del_writes : add_writes;
		for (int i = 1; i <= count; i++) {
			reset();
			assert(qca8k_qinq_setup(&ds) == 0);
			assert(qca8k_qinq_customer_add(&ds, 4, 35, 0) == 0);
			if (stage) assert(qca8k_qinq_customer_add(&ds, 5, 35, 0) == 0);
			enabled_ports = GENMASK(4, 1);
			fail_write = write_count + i;
			assert((stage ? qca8k_qinq_customer_del(&ds, 4, 35) :
				qca8k_qinq_customer_add(&ds, 5, 35, 0)) == -EIO);
			assert(priv.qinq_fault_map & BIT(4));
			assert(!(enabled_ports & BIT(4)));
			assert(!(regs[QINQ_ACL_M(94, 4) / 4] & 7));
		}
	}
	printf("PASS: customer VID35/4094 ownership, CPU-before-publish, either deletion order, %d add and %d delete write faults close WAN steering\n", add_writes, del_writes);
}

/* Each selected port must have the same admission and withdrawal contract,
 * irrespective of CPU wiring. The shared slot cannot alias the same VID on
 * another user port, including while only its host reference survives.
 */
static void test_customer_ports(void)
{
	for (int port = 1; port <= 4; port++) {
		int cpu = port == 4 ? 5 : 0;
		int other = port == 4 ? 1 : port + 1;
		reset();
		assert(qca8k_qinq_setup(&ds) == 0);
		for (int slot = 90; slot <= 93; slot++) {
			assert(regs[QINQ_ACL_M(slot, 2) / 4] == 0xff0fff00);
			assert(regs[QINQ_ACL_M(slot, 3) / 4] == 0xf);
			assert(regs[QINQ_ACL_M(slot, 4) / 4] == 0xff);
		}
		users[port].master = (void *)1;
		assert(qca8k_qinq_customer_add(&ds, port, 35, 0) == -EOPNOTSUPP);
		users[port].master = NULL;
		assert(qca8k_qinq_customer_add(&ds, port, 35, 0) == 0);
		assert(qca8k_qinq_customer_find(&priv, port, 35)->cpu == cpu);
		assert(!(regs[QINQ_ACL_M(94, 4) / 4] & 7));
		assert(qca8k_qinq_customer_add(&ds, other, 35, 1) == -EOPNOTSUPP);
		assert(qca8k_qinq_customer_add(&ds, cpu, 35, 0) == 0);
		assert(regs[QINQ_ACL_V(94, 2) / 4] == ((35u << 24) | ((3072u + port) << 8)));
		assert(regs[QINQ_ACL_V(94, 4) / 4] == BIT(cpu));
		assert(regs[QINQ_ACL_A(94, 1) / 4] == ((BIT(port) & 7) << 29));
		assert(regs[QINQ_ACL_A(94, 2) / 4] == (BIT(4) | (BIT(port) >> 3)));
		assert(qca8k_qinq_check_conduit(&priv, port, cpu == 0 ? 5 : 0) == -EBUSY);
		assert(qca8k_qinq_customer_del(&ds, port, 35) == 0);
		assert(!(regs[QINQ_ACL_M(94, 4) / 4] & 7));
		assert(qca8k_qinq_customer_add(&ds, other, 35, 1) == -EOPNOTSUPP);
		assert(qca8k_qinq_customer_add(&ds, port, 35, 0) == 0);
		assert(regs[QINQ_ACL_M(94, 4) / 4] == 0xff);
		assert(qca8k_qinq_customer_del(&ds, cpu, 35) == 0);
		assert(!(regs[QINQ_ACL_M(94, 4) / 4] & 7));
		assert(qca8k_qinq_customer_del(&ds, port, 35) == 0);
		assert(!qca8k_qinq_customer_busy(&priv, port));
		assert(qca8k_qinq_check_conduit(&priv, port, cpu == 0 ? 5 : 0) == 0);
		assert(qca8k_qinq_block_native(&priv, port) == 0);
		assert(!(regs[QINQ_ACL_M(89 + port, 4) / 4] & 7));
		priv.qinq_native_vid[port] = 3088;
		assert(qca8k_qinq_conduit(&priv, port, cpu) == 0);
		assert(regs[QINQ_ACL_V(89 + port, 2) / 4] == ((3088u & 255) << 24 | ((3072u + port) << 8)));
		assert((regs[QINQ_ACL_V(89 + port, 3) / 4] & 15) == 3088 >> 8);
	}
	puts("PASS: all four user ports, exact CPU/port/VID admission, shared-slot isolation, native bridge-domain republish and withdrawal");
}

static void test_shared_customers(void)
{
	struct qca8k_qinq_vlan *a, *b, *c;
	int count, before;
	reset();
	assert(qca8k_qinq_setup(&ds) == 0);
	assert(qca8k_qinq_customer_add(&ds, 1, 35, 0) == 0);
	assert(qca8k_qinq_customer_add(&ds, 2, 35, 0) == 0);
	a = qca8k_qinq_customer_find(&priv, 1, 35);
	b = qca8k_qinq_customer_find(&priv, 2, 35);
	assert(a && b && a->slot != b->slot);
	assert(!(regs[QINQ_ACL_M(a->slot, 4) / 4] & 7));
	assert(!(regs[QINQ_ACL_M(b->slot, 4) / 4] & 7));
	assert(qca8k_qinq_customer_add(&ds, 0, 35, 0) == 0);
	assert(regs[QINQ_ACL_M(a->slot, 4) / 4] == 0xff);
	assert(regs[QINQ_ACL_M(b->slot, 4) / 4] == 0xff);
	assert(regs[QINQ_ACL_A(a->slot, 1) / 4] == 0x40000000);
	assert(regs[QINQ_ACL_A(b->slot, 1) / 4] == 0x80000000);
	assert(qca8k_qinq_customer_add(&ds, 4, 35, 0) == 0);
	c = qca8k_qinq_customer_find(&priv, 4, 35);
	assert(c && !(regs[QINQ_ACL_M(c->slot, 4) / 4] & 7));
	assert(qca8k_qinq_customer_add(&ds, 5, 35, 0) == 0);
	assert(regs[QINQ_ACL_M(c->slot, 4) / 4] == 0xff);
	assert(qca8k_qinq_customer_del(&ds, 1, 35) == 0);
	assert(regs[QINQ_ACL_M(b->slot, 4) / 4] == 0xff);
	assert(qca8k_qinq_customer_add(&ds, 1, 35, 0) == 0);
	a = qca8k_qinq_customer_find(&priv, 1, 35);
	assert(regs[QINQ_ACL_M(a->slot, 4) / 4] == 0xff); /* CPU ref survived */
	assert(qca8k_qinq_customer_del(&ds, 0, 35) == 0);
	assert(!(regs[QINQ_ACL_M(a->slot, 4) / 4] & 7));
	assert(!(regs[QINQ_ACL_M(b->slot, 4) / 4] & 7));
	assert(regs[QINQ_ACL_M(c->slot, 4) / 4] == 0xff); /* other CPU survives */
	assert(qca8k_qinq_customer_add(&ds, 0, 35, 0) == 0);
	assert(regs[QINQ_ACL_M(a->slot, 4) / 4] == 0xff);
	assert(regs[QINQ_ACL_M(b->slot, 4) / 4] == 0xff);
	assert(qca8k_qinq_customer_add(&ds, 0, 4094, 0) == 0); /* host first */
	assert(qca8k_qinq_customer_add(&ds, 3, 4094, 0) == 0);
	assert(regs[QINQ_ACL_M(qca8k_qinq_customer_find(&priv, 3, 4094)->slot, 4) / 4] == 0xff);
	/* Every write fault while withdrawing a shared CPU gates every affected
	 * user, while the same VID's other CPU remains usable.
	 */
	before = write_count;
	assert(qca8k_qinq_customer_del(&ds, 0, 35) == 0);
	count = write_count - before;
	for (int fault = 1; fault <= count; fault++) {
		reset(); assert(qca8k_qinq_setup(&ds) == 0);
		assert(qca8k_qinq_customer_add(&ds, 1, 35, 0) == 0);
		assert(qca8k_qinq_customer_add(&ds, 2, 35, 0) == 0);
		assert(qca8k_qinq_customer_add(&ds, 0, 35, 0) == 0);
		assert(qca8k_qinq_customer_add(&ds, 4, 35, 0) == 0);
		assert(qca8k_qinq_customer_add(&ds, 5, 35, 0) == 0);
		c = qca8k_qinq_customer_find(&priv, 4, 35);
		enabled_ports = GENMASK(4, 1);
		fail_write = write_count + fault;
		assert(qca8k_qinq_customer_del(&ds, 0, 35) == -EIO);
		assert((priv.qinq_fault_map & (BIT(1) | BIT(2))) == (BIT(1) | BIT(2)));
		assert(!(enabled_ports & (BIT(1) | BIT(2))));
		assert(enabled_ports & BIT(4));
		assert(regs[QINQ_ACL_M(c->slot, 4) / 4] == 0xff);
	}
	reset(); assert(qca8k_qinq_setup(&ds) == 0);
	regs[QINQ_ACL_M(0, 4) / 4] = QINQ_RULE_COMPLETE; /* another owner */
	for (int vid = 100; vid < 100 + QCA8K_QINQ_MAX_VLANS; vid++)
		assert(qca8k_qinq_customer_add(&ds, 1, vid, 0) == 0);
	before = write_count;
	assert(qca8k_qinq_customer_add(&ds, 1, 200, 0) == -ENOSPC);
	assert(write_count == before && !vtu_member[1][200]);
	assert(regs[QINQ_ACL_M(0, 4) / 4] == QINQ_RULE_COMPLETE);
	bool owned[96] = {};
	for (int i = 0; i < QCA8K_QINQ_MAX_VLANS; i++)
		owned[priv.qinq_vlans[i].slot] = true;
	qca8k_qinq_teardown(&ds);
	assert(regs[QINQ_ACL_M(0, 4) / 4] == QINQ_RULE_COMPLETE);
	assert(!qca8k_qinq_customer_busy(&priv, 1));
	for (int i = 0; i < 96; i++)
		if (owned[i]) assert(!(regs[QINQ_ACL_M(i, 4) / 4] & 7));
	printf("PASS: shared VID across ports/CPUs, host-first/user-first ordering, row reuse, %d shared-CPU withdrawal faults, occupied ACL preservation and 56-row limit\n", count);
}

static void test_customer_failed_add_rollback(void)
{
	reset(); assert(qca8k_qinq_setup(&ds) == 0);
	assert(qca8k_qinq_customer_add(&ds, 2, 35, 0) == 0);
	enabled_ports = GENMASK(4, 1);
	fail_write = write_count + 2; /* CPU VTU succeeds, ACL publish fails */
	assert(qca8k_qinq_customer_add(&ds, 0, 35, 0) == -EIO);
	assert(!test_bit(35, priv.qinq_host_vlans[0]) && !vtu_member[0][35]);
	assert(!(enabled_ports & BIT(2)));
	assert(qca8k_qinq_customer_add(&ds, 0, 35, 0) == -EIO);
	assert(qca8k_qinq_customer_add(&ds, 2, 35, 0) == -EIO);
	assert(qca8k_qinq_customer_del(&ds, 2, 35) == 0); /* unwind still works */
	reset(); assert(qca8k_qinq_setup(&ds) == 0);
	assert(qca8k_qinq_customer_add(&ds, 0, 35, 0) == 0);
	enabled_ports = GENMASK(4, 1);
	fail_write = write_count + 3; /* slot read + user VTU + ACL write */
	assert(qca8k_qinq_customer_add(&ds, 2, 35, 0) == -EIO);
	assert(!qca8k_qinq_customer_find(&priv, 2, 35) && !vtu_member[2][35]);
	assert(test_bit(35, priv.qinq_host_vlans[0]) && vtu_member[0][35]);
	assert(qca8k_qinq_customer_add(&ds, 2, 35, 0) == -EIO);
	qca8k_qinq_teardown(&ds);
	assert(!test_bit(35, priv.qinq_host_vlans[0]));
	puts("PASS: failed user/CPU adds roll back only new membership; faulted-port retries cannot falsely succeed; teardown clears software ownership");
}

static void bridge_setup(void)
{
	reset(); assert(qca8k_qinq_setup(&ds) == 0);
	ds.ports[2].bridge = (void *)&users[6]; users[2].master = &users[6];
	priv.qinq_native_vid[2] = 3088; vtu_member[2][3088] = true;
	assert(qca8k_qinq_customer_add(&ds, 2, 35, BRIDGE_VLAN_INFO_PVID | BRIDGE_VLAN_INFO_UNTAGGED) == 0);
	assert(qca8k_qinq_customer_add(&ds, 2, 36, 0) == 0);
	assert(qca8k_qinq_customer_add(&ds, 0, 35, BRIDGE_VLAN_INFO_UNTAGGED) == 0);
	assert(qca8k_qinq_customer_add(&ds, 0, 36, 0) == 0);
	assert(!vtu_member[2][35] && !vtu_member[2][36]);
}
static void test_bridge_vlans(void)
{
	struct qca8k_qinq_vlan *a, *b;
	int before, on_writes, off_writes;
	bridge_setup();
	assert(ds.configure_vlan_while_not_filtering);
	a = qca8k_qinq_customer_find(&priv, 2, 35);
	b = qca8k_qinq_customer_find(&priv, 2, 36);
	assert(a->bridge && !a->installed && b->bridge && !b->installed);
	before = write_count;
	assert(qca8k_qinq_port_vlan_filtering(&ds, 2, true) == 0);
	on_writes = write_count - before;
	assert(!vtu_member[2][3088] && vtu_member[2][35] && vtu_member[2][36]);
	assert(vtu_untagged[2][35] && !vtu_untagged[2][36]);
	assert(priv.qinq_pvid[2] == 35 && (priv.qinq_vlan_filtering & BIT(2)));
	assert((regs[QCA8K_REG_PORT_VLAN_CTRL0(2) / 4] & 0xfff) == 3074); /* SVID never replaced */
	assert((regs[QCA8K_REG_PORT_VLAN_CTRL0(2) / 4] >> 16) == 35);
	assert(!(regs[QINQ_ACL_M(91, 4) / 4] & 7));
	assert(regs[QINQ_ACL_M(a->slot, 4) / 4] == 0xff && regs[QINQ_ACL_M(b->slot, 4) / 4] == 0xff);
	assert(qca8k_qinq_conduit(&priv, 2, 0) == 0 && !(regs[QINQ_ACL_M(91, 4) / 4] & 7));
	assert(qca8k_qinq_customer_add(&ds, 2, 36, BRIDGE_VLAN_INFO_PVID) == 0);
	assert(priv.qinq_pvid[2] == 36 && !(a->flags & BRIDGE_VLAN_INFO_PVID));
	before = write_count;
	assert(qca8k_qinq_port_vlan_filtering(&ds, 2, false) == 0);
	off_writes = write_count - before;
	assert(!vtu_member[2][35] && !vtu_member[2][36] && vtu_member[2][3088]);
	assert(!a->installed && !b->installed && !priv.qinq_pvid[2]);
	assert(regs[QINQ_ACL_M(91, 4) / 4] == 0xff);
	assert(!(regs[QINQ_ACL_M(a->slot, 4) / 4] & 7) && !(regs[QINQ_ACL_M(b->slot, 4) / 4] & 7));
	assert(qca8k_qinq_port_vlan_filtering(&ds, 2, true) == 0 && priv.qinq_pvid[2] == 36);
	assert(qca8k_qinq_customer_del(&ds, 2, 36) == 0 && !priv.qinq_pvid[2]);
	assert(qca8k_qinq_port_vlan_filtering(&ds, 2, false) == 0);
	qca8k_qinq_customer_bridge_leave(&ds, 2);
	assert(!qca8k_qinq_customer_busy(&priv, 2));
	for (int phase = 0; phase < 2; phase++) {
		int count = phase ? off_writes : on_writes;
		for (int f = 1; f <= count; f++) {
			bridge_setup();
			if (phase) {
				assert(qca8k_qinq_port_vlan_filtering(&ds, 2, true) == 0);
				assert(qca8k_qinq_customer_add(&ds, 2, 36, BRIDGE_VLAN_INFO_PVID) == 0);
			}
			enabled_ports = GENMASK(4, 1); fail_write = write_count + f;
			assert(qca8k_qinq_port_vlan_filtering(&ds, 2, !phase) == -EIO);
			assert((priv.qinq_fault_map & BIT(2)) && !(enabled_ports & BIT(2)));
			assert(qca8k_qinq_port_vlan_filtering(&ds, 2, !phase) == -EIO);
		}
	}
	printf("PASS: cached bridge VLAN replay, independent PVID/SVID, tagged/untagged membership, filtering on/off/on, PVID deletion, bridge leave, %d enable and %d disable write faults hold only test port down\n", on_writes, off_writes);
}

static void test_database_domains(void)
{
	const unsigned char mac[6] = { 2, 0x17, 0, 0x83, 0, 0x35 };
	struct dsa_bridge bridge = { .dev = &users[6] };
	struct switchdev_obj_port_mdb mdb;
	u16 vids[] = { 0, 1, 35, 4094 };
	struct dsa_port *dp;

	reset();
	assert(qca8k_qinq_setup(&ds) == 0);
	dp = &ds.ports[1];
	dp->bridge = &bridge;
	for (bridge.num = 1; bridge.num <= 7; bridge.num++) {
		for (unsigned int v = 0; v < sizeof(vids) / sizeof(vids[0]); v++) {
			u16 expected = vids[v] ?: dsa_tag_8021q_bridge_vid(bridge.num);
			mdb.vid = vids[v];
			assert(dsa_port_fdb_add(dp, mac, vids[v]) == 0);
			assert(notified_bridge == bridge.num && notified_vid == expected);
			assert(dsa_port_fdb_del(dp, mac, vids[v]) == 0);
			assert(notified_bridge == bridge.num && notified_vid == expected);
			assert(dsa_port_mdb_add(dp, &mdb) == 0);
			assert(notified_bridge == bridge.num && notified_vid == expected);
			assert(dsa_port_mdb_del(dp, &mdb) == 0);
			assert(notified_bridge == bridge.num && notified_vid == expected);
			/* A CPU port has no native user VID; its database is decisive. */
			struct dsa_db db = { .type = DSA_DB_BRIDGE, .bridge = bridge };
			assert(qca8k_qinq_db_vid(&priv, 5, vids[v], db) == expected);
		}
	}
	struct dsa_db db = { .type = DSA_DB_PORT, .dp = &ds.ports[2] };
	assert(qca8k_qinq_db_vid(&priv, 5, 0, db) == 3074);

	/* Standard tagging still uses DSA's shared database and explicit VIDs. */
	reset();
	priv.qinq = false;
	bridge.num = 7;
	dp = &ds.ports[1];
	dp->bridge = &bridge;
	assert(dsa_port_fdb_add(dp, mac, 0) == 0);
	assert(notified_bridge == 0 && notified_vid == 0);
	mdb.vid = 35;
	assert(dsa_port_mdb_del(dp, &mdb) == 0);
	assert(notified_bridge == 0 && notified_vid == 35);
	puts("PASS: real DSA FDB/MDB add/delete preserve all seven QinQ bridge domains, explicit VLANs and CPU database identity; standard shared database unchanged");
}

static void check_cpu_steering(int slot, int port, int cpu, u16 vid)
{
	u32 low = regs[QINQ_ACL_V(slot, 2) / 4];
	u32 high = regs[QINQ_ACL_V(slot, 3) / 4];
	u32 sources = regs[QINQ_ACL_V(slot, 4) / 4];
	u32 destinations = (regs[QINQ_ACL_A(slot, 1) / 4] >> 29) |
		((regs[QINQ_ACL_A(slot, 2) / 4] & 7) << 3);

	assert(((low >> 8) & 4095) == (unsigned)(3072 + port));
	assert(((low >> 24) | ((high & 15) << 8)) == vid);
	assert(high >> 16 == 0xc100);
	assert(regs[QINQ_ACL_M(slot, 2) / 4] == 0xff0fff00);
	assert(regs[QINQ_ACL_M(slot, 3) / 4] == 15);
	assert(regs[QINQ_ACL_M(slot, 4) / 4] == 255);
	assert(sources == BIT(cpu) && !(sources & GENMASK(4, 1)));
	assert(destinations == BIT(port));
	assert(regs[QINQ_ACL_A(slot, 0) / 4] == 0);
	assert(!(regs[QINQ_ACL_A(slot, 2) / 4] & (7 << 6)));
}

static void test_cpu_only_steering(void)
{
	const u16 native_vids[] = { 3088, 3104, 3632 };
	const u16 customer_vids[] = { 1, 35, 4094 };
	const int cpus[] = { 0, 5 };

	for (int port = 1; port <= 4; port++) {
		for (unsigned c = 0; c < sizeof(cpus) / sizeof(cpus[0]); c++) {
			int cpu = cpus[c];

			reset();
			ds.ports[port].cpu_dp = &ds.ports[cpu];
			assert(qca8k_qinq_setup(&ds) == 0);
			check_cpu_steering(89 + port, port, cpu, 3072 + port);
			for (unsigned v = 0; v < sizeof(native_vids) / sizeof(native_vids[0]); v++) {
				priv.qinq_native_vid[port] = native_vids[v];
				assert(qca8k_qinq_conduit(&priv, port, cpu) == 0);
				check_cpu_steering(89 + port, port, cpu, native_vids[v]);
			}
			for (unsigned v = 0; v < sizeof(customer_vids) / sizeof(customer_vids[0]); v++) {
				u16 vid = customer_vids[v];

				assert(qca8k_qinq_customer_add(&ds, port, vid, 0) == 0);
				assert(qca8k_qinq_customer_add(&ds, cpu, vid, 0) == 0);
				check_cpu_steering(qca8k_qinq_customer_find(&priv, port, vid)->slot,
						   port, cpu, vid);
			}
		}
	}
	puts("PASS: native/customer steering binds only the exact CPU/SVID/CVID tuple; front-panel LAN ingress is excluded");
}

static void test_invalid_conduit(void)
{
	const int invalid[][2] = {
		{ -1, 0 }, { 0, 0 }, { 5, 0 }, { 7, 0 }, { 32, 0 },
		{ 1, -1 }, { 1, 1 }, { 1, 4 }, { 1, 6 }, { 1, 32 },
	};

	reset();
	assert(qca8k_qinq_setup(&ds) == 0);
	int before = write_count;
	for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		assert(qca8k_qinq_conduit(&priv, invalid[i][0], invalid[i][1]) == -EOPNOTSUPP);
		assert(write_count == before && !priv.qinq_fault_map && !priv.reg_mutex);
	}
	puts("PASS: invalid conduit selectors are rejected before indexing ports, shifting bits or touching registers");
}

int main(void)
{
	int successful_writes, update_writes, before_update;
	reset();
	assert(qca8k_qinq_setup(&ds) == 0);
	successful_writes = write_count;
	assert(ds.tag_8021q_ctx && priv.qinq_acl_owned && !priv.reg_mutex);
	assert(regs[QINQ_ACL_V(91, 2) / 4] == ((3074u << 8) | (2u << 24)));
	assert(regs[QINQ_ACL_A(91, 1) / 4] == 0x80000000);
	assert(regs[QINQ_ACL_A(91, 2) / 4] == 0x10);
	assert(regs[QINQ_ACL_A(93, 1) / 4] == 0);
	assert(regs[QINQ_ACL_A(93, 2) / 4] == 0x12);
	assert(regs[QINQ_ACL_V(89, 3) / 4] == 0xc1000000);
	assert(regs[QINQ_ACL_M(89, 4) / 4] == 0xcf);
	assert(regs[QINQ_ACL_M(95, 4) / 4] == 0xc9);
	/* Matching the CPU default must not OR a drop command into steering. */
	assert(regs[QINQ_ACL_A(95, 1) / 4] == 0);
	assert(regs[QINQ_ACL_A(95, 2) / 4] == BIT(4));
	assert(!((regs[QINQ_ACL_A(91, 2) / 4] |
		  regs[QINQ_ACL_A(95, 2) / 4]) & (7u << 6)));
	assert(regs[QCA8K_REG_GLOBAL_FW_CTRL1 / 4] == 0x3f3f3f3f);
	before_update = write_count;
	assert(qca8k_qinq_conduit(&priv, 2, 5) == 0);
	update_writes = write_count - before_update;
	assert(regs[QINQ_ACL_V(91, 4) / 4] == BIT(5));
	qca8k_qinq_teardown(&ds);
	cleared();
	for (int fault = 1; fault <= successful_writes; fault++) {
		reset();
		fail_write = fault;
		assert(qca8k_qinq_setup(&ds) == -EIO);
		cleared();
	}
	reset();
	registration_error = -ENOMEM;
	assert(qca8k_qinq_setup(&ds) == -ENOMEM);
	cleared();
	reset();
	regs[QINQ_ACL_M(92, 4) / 4] = 0xcf;
	assert(qca8k_qinq_setup(&ds) == -EBUSY);
	assert(regs[QINQ_ACL_M(92, 4) / 4] == 0xcf);
	assert(!priv.qinq_acl_owned);
	reset();
	regs[QINQ_ACL_M(94, 4) / 4] = QINQ_RULE_COMPLETE;
	assert(qca8k_qinq_setup(&ds) == -EBUSY && !priv.qinq_acl_owned);
	assert(regs[QINQ_ACL_M(94, 4) / 4] == QINQ_RULE_COMPLETE);
	reset();
	busy_forever = true;
	assert(qca8k_qinq_setup(&ds) == -ETIMEDOUT && write_count == 0);
	assert(!priv.reg_mutex && !priv.qinq_acl_owned);
	for (int type = 0; type < 3; type++) {
		reset();
		corrupt_read_type = type;
		assert(qca8k_qinq_setup(&ds) == -EIO);
		cleared();
	}
	/* A runtime failure starts with an active old rule, unlike initial setup. */
	for (int fault = 1; fault <= update_writes; fault++) {
		reset();
		assert(qca8k_qinq_setup(&ds) == 0);
		enabled_ports = GENMASK(4, 1);
		fail_write = write_count + fault;
		assert(qca8k_qinq_conduit(&priv, 2, 5) == -EIO);
		assert(priv.qinq_fault_map == BIT(2));
		struct phylink_config config = { &ds.ports[2] };
		int before = write_count;
		qca8k_phylink_mac_link_up(&config, NULL, 1, 0, 1000, 1, true, true);
		assert(write_count == before && !priv.qinq_status_lock);
		config.dp = &ds.ports[1];
		qca8k_phylink_mac_link_up(&config, NULL, 1, 0, 1000, 1, true, true);
		assert(write_count == before + 1);
		assert(!(enabled_ports & BIT(2)) && !priv.reg_mutex);
		assert(regs[QINQ_ACL_M(91, 4) / 4] == 0);
		qca8k_qinq_teardown(&ds);
		cleared();
	}
	reset();
	ds.cpu_mask = BIT(0) | BIT(6);
	assert(qca8k_qinq_setup(&ds) == -EOPNOTSUPP && write_count == 0);
	printf("PASS: %d setup-write faults, %d active-rule update faults, busy timeout, values/actions/mask readback corruption, registration failure, ACL ownership and steering encoding\n",
	       successful_writes, update_writes);
	test_customer();
	test_customer_ports();
	test_shared_customers();
	test_customer_failed_add_rollback();
	test_bridge_vlans();
	test_database_domains();
	test_cpu_only_steering();
	test_invalid_conduit();
	return 0;
}
