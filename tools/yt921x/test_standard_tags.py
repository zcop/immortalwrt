#!/usr/bin/env python3
"""Exercise the actual legacy yt921x TX tagger with fake kernel/DSA APIs.

The RX tagger is out of scope. Frames are compared against an independent
Python wire oracle, so an unpatched tagger fails on any VLAN metadata case.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TAGGER = ROOT / 'target/linux/generic/files/net/dsa/tag_yt921x.c'
source = Path(os.environ.get('YT921X_TAG_SOURCE', TAGGER)).read_text()
xmit = re.search(r'^static struct sk_buff \*\nyt921x_tag_xmit\(.*?^}', source, re.M | re.S)
raw = re.search(r'^static bool yt921x_conduit_is_raw\(.*?^}', source, re.M | re.S)
codes = re.search(r'^enum yt921x_tag_code \{.*?^\};', source, re.M | re.S)
assert xmit and raw and codes
defines = [line for line in source.splitlines()
           if re.match(r'#define\s+YT921X_TAG_\w+', line)]
assert any('YT921X_TAG_LEN' in line for line in defines)
assert any('YT921X_TAG_PRIO_M' in line for line in defines)

preamble = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint16_t __be16;

#define unlikely(x) (x)
#define ETH_ALEN 6
#define VLAN_HLEN 4
#define ETH_P_YT921X 0x9988
#define CHECKSUM_PARTIAL 3
#define CHECKSUM_NONE 0
#define BIT(x) (1UL << (x))
#define GENMASK(h,l) ((~0U << (l)) & (~0U >> (31-(h))))
#define FIELD_PREP(mask,x) (((u32)(x) << __builtin_ctz(mask)) & (mask))
#define skb_vlan_tag_present(skb) ((skb)->vlan_present)
#define skb_vlan_tag_get(skb) ((skb)->vlan_tci)

enum dsa_tag_protocol { DSA_TAG_PROTO_NONE, DSA_TAG_PROTO_YT921X };

struct sk_buff {
	u8 head[8192];
	u8 *data;
	unsigned int len;
	u16 priority;
	__be16 protocol;
	int ip_summed;
	bool vlan_present;
	__be16 vlan_proto;
	u16 vlan_tci;
	bool freed;
};

struct dsa_switch;
struct dsa_port {
	unsigned int index;
	struct dsa_port *cpu_dp;
	struct dsa_switch *ds;
};
struct dsa_switch_ops {
	enum dsa_tag_protocol (*get_tag_protocol)(struct dsa_switch *, int,
						  enum dsa_tag_protocol);
};
struct dsa_switch {
	const struct dsa_switch_ops *ops;
};
struct net_device {
	struct dsa_port *dsa;
};
#define dsa_user_to_port(netdev) ((netdev)->dsa)

static enum dsa_tag_protocol fake_proto;
static enum dsa_tag_protocol fake_get_protocol(struct dsa_switch *ds, int port,
					       enum dsa_tag_protocol m)
{
	return fake_proto;
}
static const struct dsa_switch_ops fake_ops = {
	.get_tag_protocol = fake_get_protocol,
};

static char events[16];
static size_t nevents;
static bool checksum_fail;
static void log_event(char c)
{
	assert(nevents + 1 < sizeof(events));
	events[nevents++] = c;
	events[nevents] = 0;
}
static int skb_cow_head(struct sk_buff *skb, unsigned int len)
{
	if ((unsigned int)(skb->data - skb->head) < len)
		return -1;
	return 0;
}
static u8 *skb_push(struct sk_buff *skb, unsigned int len)
{
	assert((unsigned int)(skb->data - skb->head) >= len);
	skb->data -= len;
	skb->len += len;
	return skb->data;
}
/* Real kernel clears vlan_all, so proto/tci return to zero as well. */
static void __vlan_hwaccel_clear_tag(struct sk_buff *skb)
{
	skb->vlan_present = false;
	skb->vlan_proto = 0;
	skb->vlan_tci = 0;
}
/* Kernel helper contract: int return, network-order proto, never frees or
 * replaces the skb, and never touches skb->protocol.
 */
static int __vlan_insert_tag(struct sk_buff *skb, __be16 proto, u16 tci)
{
	assert(!skb->freed);
	if (skb_cow_head(skb, VLAN_HLEN))
		return -1;
	skb_push(skb, VLAN_HLEN);
	memmove(skb->data, skb->data + VLAN_HLEN, 2 * ETH_ALEN);
	memcpy(skb->data + 12, &proto, sizeof(proto));
	skb->data[14] = tci >> 8;
	skb->data[15] = tci & 0xff;
	log_event('V');
	return 0;
}
static int skb_checksum_help(struct sk_buff *skb)
{
	assert(!skb->freed);
	assert(skb->ip_summed == CHECKSUM_PARTIAL);
	log_event('C');
	if (checksum_fail)
		return -1;
	skb->ip_summed = CHECKSUM_NONE;
	return 0;
}
static void dsa_alloc_etype_header(struct sk_buff *skb, unsigned int len)
{
	memmove(skb->data, skb->data + len, 2 * ETH_ALEN);
}
static __be16 *dsa_etype_header_pos_tx(struct sk_buff *skb)
{
	return (__be16 *)(skb->data + 2 * ETH_ALEN);
}

static u8 base[4096];
static unsigned int frame_len;
static struct sk_buff skb;
static struct dsa_port user, cpu;
static struct dsa_switch sw;
static struct net_device ndev;
static const u8 DA[6] = { 0x02, 0, 0, 0, 0, 0x01 };
static const u8 SA[6] = { 0x02, 0, 0, 0, 0, 0x02 };
struct tag { u16 tpid; u16 tci; };

static unsigned int build_frame(const struct tag *tags, int ntags, u16 eth_type)
{
	unsigned int off = 0, i;
	memcpy(base, DA, ETH_ALEN); off += ETH_ALEN;
	memcpy(base + off, SA, ETH_ALEN); off += ETH_ALEN;
	for (i = 0; i < (unsigned int)ntags; i++) {
		base[off++] = tags[i].tpid >> 8;
		base[off++] = tags[i].tpid & 0xff;
		base[off++] = tags[i].tci >> 8;
		base[off++] = tags[i].tci & 0xff;
	}
	base[off++] = eth_type >> 8;
	base[off++] = eth_type & 0xff;
	for (i = 0; i < 1500; i++)
		base[off++] = (u8)(0x30 + i % 200);
	return off;
}
static void setup(unsigned int port, int raw_conduit)
{
	memset(&user, 0, sizeof(user));
	memset(&cpu, 0, sizeof(cpu));
	memset(&sw, 0, sizeof(sw));
	memset(&ndev, 0, sizeof(ndev));
	user.index = port;
	user.cpu_dp = &cpu;
	user.ds = &sw;
	cpu.index = 8;
	cpu.ds = &sw;
	sw.ops = &fake_ops;
	fake_proto = raw_conduit ? DSA_TAG_PROTO_NONE : DSA_TAG_PROTO_YT921X;
	ndev.dsa = &user;
}
/* Host-order arguments in, kernel-order fields stored. */
static void reset_skb(unsigned int headroom, bool present, u16 vlan_proto,
		      u16 tci, u16 prio, u16 protocol, int ip_summed)
{
	memset(&skb, 0, sizeof(skb));
	skb.data = skb.head + headroom;
	memcpy(skb.data, base, frame_len);
	skb.len = frame_len;
	skb.priority = prio;
	skb.protocol = htons(protocol);
	skb.ip_summed = ip_summed;
	skb.vlan_present = present;
	skb.vlan_proto = htons(vlan_proto);
	skb.vlan_tci = tci;
	checksum_fail = false;
	nevents = 0;
	events[0] = 0;
}
static void dump(const char *name, struct sk_buff *ret)
{
	unsigned int i;
	printf("BEGIN %s\n", name);
	printf("RET %d\n", ret != NULL);
	printf("FRAME ");
	for (i = 0; i < skb.len; i++)
		printf("%02x", skb.data[i]);
	printf("\nLEN %u\n", skb.len);
	printf("PROTO %04x\n", ntohs(skb.protocol));
	printf("META %d %04x %04x\n", skb.vlan_present,
	       ntohs(skb.vlan_proto), skb.vlan_tci);
	printf("EVENTS %s\n", events);
	printf("FREED %d\n", skb.freed);
	printf("END %s\n", name);
}
'''

main_c = r'''
#define S_TCI 0xd123
#define C_TCI 0x6abc
#define MC_TCI 0xb064
#define MS_TCI 0xf456
#define IS_TCI 0x4789
#define IC_TCI 0x3fed

int main(void)
{
	struct sk_buff *ret;
	struct tag inline_sc[2] = { { 0x88a8, S_TCI }, { 0x8100, C_TCI } };
	struct tag inline_s[1] = { { 0x88a8, IS_TCI } };
	struct tag inline_c[1] = { { 0x8100, IC_TCI } };
	struct tag none[1] = { { 0, 0 } };

	setup(3, 0);
	frame_len = build_frame(inline_sc, 2, 0x0800);
	reset_skb(128, false, 0, 0, 5, 0x88a8, CHECKSUM_NONE);
	ret = yt921x_tag_xmit(&skb, &ndev);
	assert(ret == &skb && !skb.freed && events[0] == 0);
	dump("inline_s_c", ret);

	setup(4, 0);
	frame_len = build_frame(inline_s, 1, 0x0800);
	reset_skb(128, true, 0x8100, MC_TCI, 2, 0x88a8, CHECKSUM_NONE);
	ret = yt921x_tag_xmit(&skb, &ndev);
	assert(ret == &skb && !skb.vlan_present &&
	       ntohs(skb.protocol) == 0x8100);
	dump("meta_c_inline_s", ret);

	setup(8, 0);
	frame_len = build_frame(inline_c, 1, 0x0800);
	reset_skb(128, true, 0x88a8, MS_TCI, 7, 0x8100, CHECKSUM_NONE);
	ret = yt921x_tag_xmit(&skb, &ndev);
	assert(ret == &skb && !skb.vlan_present &&
	       ntohs(skb.protocol) == 0x88a8);
	dump("meta_s_inline_c", ret);

	setup(6, 0);
	frame_len = build_frame(inline_c, 1, 0x0800);
	reset_skb(128, true, 0x88a8, MS_TCI, 4, 0x8100, CHECKSUM_PARTIAL);
	ret = yt921x_tag_xmit(&skb, &ndev);
	assert(ret == &skb && !skb.vlan_present);
	assert(skb.ip_summed == CHECKSUM_NONE && strcmp(events, "VC") == 0);
	dump("meta_checksum", ret);

	setup(4, 0);
	frame_len = build_frame(inline_s, 1, 0x0800);
	reset_skb(128, true, 0x8100, MC_TCI, 2, 0x88a8, CHECKSUM_PARTIAL);
	checksum_fail = true;
	ret = yt921x_tag_xmit(&skb, &ndev);
	assert(ret == NULL && !skb.freed && !skb.vlan_present);
	assert(skb.ip_summed == CHECKSUM_PARTIAL && strcmp(events, "VC") == 0);
	dump("fail_checksum", ret);

	setup(3, 1);
	frame_len = build_frame(inline_sc, 2, 0x0800);
	reset_skb(128, false, 0, 0, 5, 0x88a8, CHECKSUM_NONE);
	ret = yt921x_tag_xmit(&skb, &ndev);
	assert(ret == &skb && !skb.freed && !skb.vlan_present &&
	       ntohs(skb.protocol) == 0x88a8);
	dump("raw_inline_s_c", ret);

	setup(3, 1);
	frame_len = build_frame(inline_s, 1, 0x0800);
	reset_skb(128, true, 0x8100, MC_TCI, 5, 0x88a8, CHECKSUM_NONE);
	ret = yt921x_tag_xmit(&skb, &ndev);
	assert(ret == &skb && skb.vlan_present &&
	       ntohs(skb.vlan_proto) == 0x8100 && skb.vlan_tci == MC_TCI);
	dump("raw_meta_c_inline_s", ret);

	setup(3, 0);
	frame_len = build_frame(none, 0, 0x0800);
	reset_skb(0, true, 0x8100, MC_TCI, 5, 0x0800, CHECKSUM_NONE);
	ret = yt921x_tag_xmit(&skb, &ndev);
	assert(ret == NULL && !skb.freed && skb.vlan_present);
	dump("fail_vlan", ret);

	setup(3, 0);
	frame_len = build_frame(none, 0, 0x0800);
	reset_skb(VLAN_HLEN, true, 0x8100, MC_TCI, 5, 0x0800, CHECKSUM_NONE);
	ret = yt921x_tag_xmit(&skb, &ndev);
	assert(ret == NULL && !skb.freed && !skb.vlan_present);
	dump("fail_private", ret);

	puts("PASS: inline S+C, CTAG/STAG metadata, PCP/DEI, priority, port mask, checksum order/failure, raw cases, ownership");
	return 0;
}
'''

DA = bytes.fromhex('020000000001')
SA = bytes.fromhex('020000000002')
PAYLOAD = bytes((0x30 + i % 200) & 0xff for i in range(1500))


def vlan(tpid, tci):
    return tpid.to_bytes(2, 'big') + tci.to_bytes(2, 'big')


def private(port, prio):
    tag2 = 0x80 | (prio << 8)
    tag3 = 0x8000 | (1 << port)
    return b'\x99\x88\x00\x00' + tag2.to_bytes(2, 'big') + tag3.to_bytes(2, 'big')


def original(tags):
    return DA + SA + b''.join(tags) + b'\x08\x00' + PAYLOAD


def tagged(port, prio, tags):
    return DA + SA + private(port, prio) + b''.join(tags) + b'\x08\x00' + PAYLOAD


S = vlan(0x88a8, 0xd123)
C = vlan(0x8100, 0x6abc)
MC = vlan(0x8100, 0xb064)
MS = vlan(0x88a8, 0xf456)
IS = vlan(0x88a8, 0x4789)
IC = vlan(0x8100, 0x3fed)

expected = {
    'inline_s_c': dict(frame=tagged(3, 5, [S, C]), ret=1, proto=0x88a8,
                       meta=(0, 0, 0), events=''),
    'meta_c_inline_s': dict(frame=tagged(4, 2, [MC, IS]), ret=1, proto=0x8100,
                            meta=(0, 0, 0), events='V'),
    'meta_s_inline_c': dict(frame=tagged(8, 7, [MS, IC]), ret=1, proto=0x88a8,
                            meta=(0, 0, 0), events='V'),
    'meta_checksum': dict(frame=tagged(6, 4, [MS, IC]), ret=1, proto=0x88a8,
                          meta=(0, 0, 0), events='VC'),
    'fail_checksum': dict(frame=original([MC, IS]), ret=0, proto=0x8100,
                          meta=(0, 0, 0), events='VC'),
    'raw_inline_s_c': dict(frame=original([S, C]), ret=1, proto=0x88a8,
                           meta=(0, 0, 0), events=''),
    'raw_meta_c_inline_s': dict(frame=original([IS]), ret=1, proto=0x88a8,
                                meta=(1, 0x8100, 0xb064), events=''),
    'fail_vlan': dict(frame=original([]), ret=0, proto=0x0800,
                      meta=(1, 0x8100, 0xb064), events=''),
    'fail_private': dict(frame=original([MC]), ret=0, proto=0x8100,
                         meta=(0, 0, 0), events='V'),
}

with tempfile.TemporaryDirectory(prefix='yt921x-tag-') as temporary:
    directory = Path(temporary)
    cfile = directory / 'tags.c'
    cfile.write_text(preamble + '\n' + '\n'.join(defines) + '\n' + codes.group(0) +
                     '\n' + raw.group(0) + '\n' + xmit.group(0) + '\n' + main_c)
    executable = directory / 'tags-test'
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Wno-unused-parameter',
                    '-Werror=implicit-function-declaration',
                    '-Werror=incompatible-pointer-types',
                    '-fsanitize=address,undefined', str(cfile), '-o', str(executable)],
                   check=True)
    environment = dict(os.environ)
    environment['ASAN_OPTIONS'] = environment.get('ASAN_OPTIONS', '') + ':detect_leaks=0'
    output = subprocess.run([str(executable)], check=True, env=environment,
                            capture_output=True, text=True).stdout

blocks = {}
name = None
for line in output.splitlines():
    if line.startswith('BEGIN '):
        name = line.split()[1]
        blocks[name] = {}
    elif line.startswith('END '):
        name = None
    elif name:
        key, _, value = line.partition(' ')
        blocks[name][key] = value

assert set(blocks) == set(expected), (set(blocks), set(expected))
for name, want in expected.items():
    got = blocks[name]
    frame = bytes.fromhex(got['FRAME'])
    assert frame == want['frame'], f'{name}: frame mismatch'
    assert int(got['RET']) == want['ret'], f'{name}: return mismatch'
    assert int(got['LEN']) == len(want['frame']), f'{name}: length mismatch'
    assert int(got['PROTO'], 16) == want['proto'], f'{name}: protocol mismatch'
    meta = tuple(int(part, 16) for part in got['META'].split())
    assert meta == want['meta'], f'{name}: metadata mismatch'
    assert got['EVENTS'] == want['events'], f'{name}: event order mismatch'
    assert int(got['FREED']) == 0, f'{name}: tagger freed caller-owned skb'

print('PASS: independent wire oracle, metadata materialization, raw CPU4, checksum failure, ownership')
