#!/usr/bin/env python3
"""Compile and exercise the shared YT921x QinQ customer-VID predicate.

The test uses the header added by namespace.patch and minimal local DSA stubs.
It checks the complete 12-bit VID space for the stock local topology and for a
shifted switch-index/port layout. No kernel build, device, or private path.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "target/linux/generic/files/include/linux/dsa/yt921x_qinq.h"

TYPES_H = """\
#ifndef YT921X_TEST_TYPES_H
#define YT921X_TEST_TYPES_H
#include <stdbool.h>
#include <stdint.h>
typedef uint16_t u16;
struct dsa_port;
struct dsa_switch;
#endif
"""

IF_VLAN_H = """\
#ifndef YT921X_TEST_IF_VLAN_H
#define YT921X_TEST_IF_VLAN_H
#define VLAN_N_VID 4096
#define VLAN_VID_MASK 0x0fff
#endif
"""

DSA_8021Q_H = """\
#ifndef YT921X_TEST_DSA_8021Q_H
#define YT921X_TEST_DSA_8021Q_H
#include <yt921x_test_types.h>
u16 dsa_tag_8021q_standalone_vid(const struct dsa_port *dp);
u16 dsa_tag_8021q_bridge_vid(unsigned int bridge_num);
#endif
"""

NET_DSA_H = """\
#ifndef YT921X_TEST_NET_DSA_H
#define YT921X_TEST_NET_DSA_H
#include <yt921x_test_types.h>
struct dsa_port {
	unsigned int index;
	struct dsa_switch *ds;
};
struct dsa_switch {
	unsigned int index;
	unsigned int max_num_bridges;
	struct dsa_port *ports;
	unsigned int num_ports;
};
#define dsa_switch_for_each_user_port(dp, ds) \
	for ((dp) = (ds)->ports; (dp) != (ds)->ports + (ds)->num_ports; (dp)++)
#endif
"""

CASE_C = r"""\
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <linux/dsa/yt921x_qinq.h>

u16 dsa_tag_8021q_standalone_vid(const struct dsa_port *dp)
{
	return 0xc00U | ((dp->ds->index & 7U) << 6) | (dp->index & 0xfU);
}

u16 dsa_tag_8021q_bridge_vid(unsigned int bridge_num)
{
	return 0xc00U | ((bridge_num & 4U) << 7) | ((bridge_num & 3U) << 4);
}

static bool expected_valid(const struct dsa_switch *ds, u16 vid)
{
 static const u16 local[] = {3072,3073,3074,3075,3088,3104,3120,3584,3600,3616,3632};
 static const u16 shifted[] = {3268,3269,3270,3271,3088,3104,3120,3584,3600,3616,3632};
 const u16 *reserved = ds->index == 0 ? local : shifted;
 if (vid == 0 || vid >= 4095) return false;
 for (unsigned int i=0;i<11;i++) if (vid==reserved[i]) return false;
 return true;
}

static void expect(struct dsa_switch *ds, u16 vid, bool want, const char *tag)
{
	bool got = dsa_yt921x_qinq_customer_vid_valid(ds, vid);

	if (got != want) {
		fprintf(stderr, "%s: VID %u got %d want %d\n",
			tag, vid, got, want);
		assert(0);
	}
}

static void check_all(struct dsa_switch *ds, const char *tag)
{
	unsigned int vid;

	for (vid = 0; vid < VLAN_N_VID; vid++) {
		bool want = expected_valid(ds, (u16)vid);
		bool got = dsa_yt921x_qinq_customer_vid_valid(ds, (u16)vid);

		if (got != want) {
			fprintf(stderr, "%s: VID %u got %d want %d\n",
				tag, vid, got, want);
			assert(0);
		}
	}
}

static struct dsa_port ports_a[4];
static struct dsa_switch ds_a;
static struct dsa_port ports_b[4];
static struct dsa_switch ds_b;

int main(void)
{
	static const u16 bridges[] = { 3088, 3104, 3120, 3584, 3600, 3616, 3632 };
	static const u16 outside[] = { 4096, 4097, 5000, 0xffff };
	unsigned int i;

	for (i = 0; i < 4; i++) {
		ports_a[i].index = i;
		ports_a[i].ds = &ds_a;
	}
	ds_a.index = 0;
	ds_a.max_num_bridges = 7;
	ds_a.ports = ports_a;
	ds_a.num_ports = 4;

	check_all(&ds_a, "switch0 ports0-3");
	for (i = 0; i < 4; i++)
		expect(&ds_a, (u16)(3072 + i), false, "standalone");
	for (i = 0; i < 7; i++)
		expect(&ds_a, bridges[i], false, "inactive bridge");
	expect(&ds_a, 3500, true, "high customer");
	expect(&ds_a, 4094, true, "top customer");
	expect(&ds_a, 0, false, "zero");
	expect(&ds_a, 4095, false, "invalid");
	for (i = 0; i < 4; i++)
		expect(&ds_a, outside[i], false, "outside 12-bit");

	for (i = 0; i < 4; i++) {
		ports_b[i].index = 4 + i;
		ports_b[i].ds = &ds_b;
	}
	ds_b.index = 3;
	ds_b.max_num_bridges = 7;
	ds_b.ports = ports_b;
	ds_b.num_ports = 4;

	check_all(&ds_b, "switch3 ports4-7");
	expect(&ds_b, 3072, true, "different standalone set");
	for (i = 0; i < 4; i++)
		expect(&ds_b, (u16)(3268 + i), false, "shifted standalone");
	for (i = 0; i < 7; i++)
		expect(&ds_b, bridges[i], false, "inactive bridge");
	expect(&ds_b, 3500, true, "high customer");
	expect(&ds_b, 4094, true, "top customer");

	assert(!dsa_yt921x_qinq_customer_vid_valid(&ds_a, 3072));
	assert(dsa_yt921x_qinq_customer_vid_valid(&ds_b, 3072));

	puts("PASS: shared QinQ VID predicate covers stock and shifted topologies");
	return 0;
}
"""


def write_stub(base: Path, rel: str, text: str) -> None:
    path = base / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def main() -> None:
    if not HEADER.is_file():
        raise SystemExit(
            f"missing {HEADER}; apply logs/qinq-input/vid-namespace/namespace.patch first"
        )

    with tempfile.TemporaryDirectory(prefix="yt921x-qinq-vid-") as tmp:
        work = Path(tmp)
        stubs = work / "stubs"
        write_stub(stubs, "yt921x_test_types.h", TYPES_H)
        write_stub(stubs, "linux/if_vlan.h", IF_VLAN_H)
        write_stub(stubs, "linux/dsa/8021q.h", DSA_8021Q_H)
        write_stub(stubs, "net/dsa.h", NET_DSA_H)

        source = work / "qinq_vid_namespace.c"
        source.write_text(CASE_C)
        binary = work / "qinq_vid_namespace"
        cmd = [
            os.environ.get("CC", "cc"),
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-O2",
            "-I",
            str(stubs),
            "-I",
            str(HEADER.parents[2]),
            str(source),
            "-o",
            str(binary),
        ]
        subprocess.run(cmd, check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
