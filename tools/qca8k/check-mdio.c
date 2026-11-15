/* Host-only fault injection. Driver functions are extracted without edits. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t u32;
typedef uint16_t u16;
#define BIT(n) (1u << (n))
#define GENMASK(h, l) ((~0u << (l)) & (~0u >> (31 - (h))))
#define FIELD_PREP(mask, value) (((u32)(value) << __builtin_ctz(mask)) & (mask))
#define MDIO_MUTEX_NESTED 0
#define dev_err_ratelimited(...) ((void)0)
#define usleep_range(...) ((void)0)

struct mii_bus {
	int (*read)(struct mii_bus *, int, u32);
	int (*write)(struct mii_bus *, int, u32, u16);
	int mdio_lock;
	int dev;
};
struct qca8k_priv {
	struct mii_bus *bus;
	struct { u16 page; } mdio_cache;
	void *mgmt_conduit;
};
struct transaction { u32 address; u16 value; };
static struct transaction writes[32];
static int write_count, read_count, fail_write, fail_read, busy_error;
static u16 page;

static void mutex_lock_nested(int *lock, int subclass)
{
	assert(*lock == 0);
	*lock = 1;
}
static void mutex_unlock(int *lock)
{
	assert(*lock == 1);
	*lock = 0;
}
static u32 address(int phy, u32 reg)
{
	return ((u32)page << 9) | ((phy & 7) << 6) | ((reg & 31) << 1);
}
static u32 pattern(u32 reg)
{
	return 0x12340000u ^ (reg & ~3u);
}
static int mock_read(struct mii_bus *bus, int phy, u32 reg)
{
	u32 addr = address(phy, reg);
	assert(bus->mdio_lock == 1);
	if (++read_count == fail_read)
		return -EIO;
	return (pattern(addr) >> ((addr & 2) * 8)) & 0xffff;
}
static int mock_write(struct mii_bus *bus, int phy, u32 reg, u16 value)
{
	assert(bus->mdio_lock == 1);
	assert(write_count < 32);
	writes[write_count].address = phy == 0x18 ? ~0u : address(phy, reg);
	writes[write_count].value = value;
	if (++write_count == fail_write)
		return -EIO;
	if (phy == 0x18)
		page = value;
	return 0;
}
static int qca8k_read_eth(struct qca8k_priv *priv, u32 reg, u32 *val, int len)
{
	assert(!"unexpected Ethernet management");
	return -EOPNOTSUPP;
}
static int qca8k_write_eth(struct qca8k_priv *priv, u32 reg, u32 *val, int len)
{
	assert(!"unexpected Ethernet management");
	return -EOPNOTSUPP;
}
static int qca8k_mdio_busy_wait(struct mii_bus *bus, u32 reg, u32 mask)
{
	assert(bus->mdio_lock == 1);
	return busy_error;
}

#include "driver-functions.h"

static struct mii_bus bus = { .read = mock_read, .write = mock_write };
static struct qca8k_priv priv = { .bus = &bus };
static void reset(void)
{
	assert(bus.mdio_lock == 0);
	memset(writes, 0, sizeof(writes));
	write_count = read_count = fail_write = fail_read = busy_error = 0;
	page = 0;
	priv.mdio_cache.page = 0xffff;
}
static void check_extended_and_bulk(void)
{
	u32 start = 0xfffc;
	struct { u32 before, words[3], after; } data = { .before = 0xaabbccdd,
		.after = 0xdeadbeef };
	u32 packet[] = { 0x58be0, 0xaabbccdd, 0xdeadbeef };
	u32 value;
	reset();
	assert(qca8k_bulk_read(&priv, &start, sizeof(start), data.words,
			      sizeof(data.words)) == 0);
	assert(data.before == 0xaabbccdd && data.after == 0xdeadbeef);
	for (int i = 0; i < 3; i++)
		assert(data.words[i] == pattern(start + 4 * i));
	reset();
	assert(qca8k_bulk_write(&priv, packet, sizeof(packet)) == 0);
	assert(write_count == 5);
	assert(writes[1].address == 0x58be0 && writes[1].value == 0xccdd);
	assert(writes[2].address == 0x58be2 && writes[2].value == 0xaabb);
	assert(writes[3].address == 0x58be4 && writes[3].value == 0xbeef);
	assert(writes[4].address == 0x58be6 && writes[4].value == 0xdead);
	reset();
	assert(qca8k_read_mii(&priv, 0x5adfc, &value) == 0);
	assert(value == pattern(0x5adfc));
	assert(bus.mdio_lock == 0);
}
static void check_write_errors(void)
{
	for (int fault = 1; fault <= 3; fault++) {
		reset();
		priv.mdio_cache.page = 0;
		fail_write = fault;
		assert(qca8k_write_mii(&priv, 0x58be0, 0x12345678) == -EIO);
		assert(write_count == fault && bus.mdio_lock == 0);
		if (fault == 1)
			assert(priv.mdio_cache.page == 0xffff);
		reset();
		fail_write = fault;
		assert(qca8k_regmap_update_bits_mii(&priv, 0x58be0, 0xff, 7) == -EIO);
		assert(bus.mdio_lock == 0);
	}
	for (int fault = 1; fault <= 2; fault++) {
		reset();
		fail_read = fault;
		assert(qca8k_regmap_update_bits_mii(&priv, 0x58be0, 0xff, 7) == -EIO);
		assert(write_count == 1 && bus.mdio_lock == 0);
	}
}
static void check_phy_cleanup(void)
{
	for (int fault = 1; fault <= 4; fault++) {
		reset();
		fail_write = fault;
		assert(qca8k_mdio_write(&priv, 3, 4, 0x1234) == -EIO);
		assert(bus.mdio_lock == 0);
		if (fault == 1)
			assert(write_count == 1); /* no clear through an unselected page */
	}
	for (int fault = 1; fault <= 3; fault++) {
		reset();
		fail_write = fault;
		assert(qca8k_mdio_read(&priv, 3, 4) == -EIO);
		assert(bus.mdio_lock == 0);
		if (fault == 1)
			assert(write_count == 1);
	}
	reset();
	busy_error = -ETIMEDOUT;
	fail_write = 4;
	assert(qca8k_mdio_write(&priv, 3, 4, 0) == -ETIMEDOUT);
	assert(write_count == 4 && bus.mdio_lock == 0);
	reset();
	busy_error = -ETIMEDOUT;
	fail_write = 3;
	assert(qca8k_mdio_read(&priv, 3, 4) == -ETIMEDOUT);
	assert(write_count == 3 && bus.mdio_lock == 0);
}
int main(void)
{
	check_extended_and_bulk();
	check_write_errors();
	check_phy_cleanup();
	puts("PASS: extended addresses, bulk layout, MDIO faults, PHY cleanup and locking");
	return 0;
}
