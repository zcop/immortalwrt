#!/usr/bin/env python3
"""Check the production legacy RX tagger against independent wire oracles.

The saved standard-mode captures contain WAN BPDUs with RX word 0x98fe:
source port3, priority0, code-valid and RMA reason0x3f. Exercise that wire
layout, cross-conduit user identity, payload preservation and forwarding marks.
CPU-source and malformed-tag controls keep the pre-existing rejection policy.
"""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

from test_qinq_control_rx import extract


HARNESS = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint16_t __be16;
#define BIT(n) (1U << (n))
#define GENMASK(h, l) ((~0U << (l)) & (~0U >> (31 - (h))))
#define FIELD_GET(mask, v) (((v) & (mask)) >> __builtin_ctz(mask))
#define unlikely(v) (v)
#define likely(v) (v)
#define ETH_ALEN 6
#define ETH_HLEN 14
#define ETH_P_YT921X 0x9988

enum dsa_tag_protocol { DSA_TAG_PROTO_NONE, DSA_TAG_PROTO_YT921X_ENHANCED };
struct dsa_switch;
struct net_device;
struct device { int unused; };
struct dsa_port {
    unsigned int index;
    struct dsa_switch *ds;
    struct dsa_port *cpu_dp;
    struct net_device *user, *conduit;
    bool bridge;
};
struct dsa_switch_ops {
    enum dsa_tag_protocol (*get_tag_protocol)(struct dsa_switch *, int,
                                             enum dsa_tag_protocol);
};
struct dsa_switch {
    unsigned int num_ports;
    const struct dsa_switch_ops *ops;
    struct dsa_port *ports[11];
};
struct net_device {
    struct device dev;
    struct dsa_port *dsa_ptr;
};
struct sk_buff {
    u8 *data;
    unsigned int len, priority;
    struct net_device *dev;
    bool offload_fwd_mark;
};

static struct dsa_switch sw;
static struct dsa_port ports[11];
static struct net_device users[4], primary, secondary;
static _Alignas(8) u8 frame[256];
static u8 expected[256];
static struct sk_buff skb;
static unsigned int warnings, mark_calls;

static enum dsa_tag_protocol get_protocol(struct dsa_switch *ds, int port,
                                         enum dsa_tag_protocol previous)
{
    (void)ds; (void)previous;
    return port == 4 ? DSA_TAG_PROTO_NONE : DSA_TAG_PROTO_YT921X_ENHANCED;
}
static const struct dsa_switch_ops ops = { .get_tag_protocol = get_protocol };
#define dsa_switch_for_each_user_port(dp, ds) \
    for (unsigned int i = 0; i < 4 && ((dp) = (ds)->ports[i]); i++)
static struct net_device *dsa_port_to_conduit(struct dsa_port *dp)
{
    return dp->cpu_dp->conduit;
}
static bool dsa_is_cpu_port(struct dsa_switch *ds, unsigned int port)
{
    (void)ds;
    return port == 4 || port == 8;
}
/* Model the kernel helper's switch-tree lookup, which permits a WAN source
 * received on the primary CPU conduit and does not require cpu_dp equality.
 */
static struct net_device *dsa_conduit_find_user(struct net_device *dev,
                                               int device, unsigned int port)
{
    assert(dev->dsa_ptr->ds == &sw && device == 0);
    return port < 4 ? sw.ports[port]->user : NULL;
}
static bool pskb_may_pull(struct sk_buff *s, unsigned int len)
{
    return s->len >= len;
}
static void *dsa_etype_header_pos_rx(struct sk_buff *s) { return s->data - 2; }
static void skb_pull_rcsum(struct sk_buff *s, unsigned int len)
{
    assert(s->len >= len);
    s->data += len;
    s->len -= len;
}
static void dsa_strip_etype_header(struct sk_buff *s, unsigned int len)
{
    memmove(s->data - ETH_HLEN, s->data - ETH_HLEN - len, 2 * ETH_ALEN);
}
static void dev_warn_ratelimited(struct device *dev, const char *format, ...)
{
    (void)format;
    assert(dev == &primary.dev || dev == &secondary.dev);
    warnings++;
}
static void dsa_default_offload_fwd_mark(struct sk_buff *s)
{
    mark_calls++;
    for (unsigned int port = 0; port < 4; port++)
        if (s->dev == &users[port]) {
            s->offload_fwd_mark = ports[port].bridge;
            return;
        }
    assert(false);
}

/* PRODUCTION */

static void put16(unsigned int offset, u16 value)
{
    frame[offset] = value >> 8;
    frame[offset + 1] = value & 0xff;
}
static void reset(unsigned int port, unsigned int reason, unsigned int prio)
{
    memset(&sw, 0, sizeof(sw));
    memset(ports, 0, sizeof(ports));
    memset(users, 0, sizeof(users));
    sw.num_ports = 11;
    sw.ops = &ops;
    for (unsigned int i = 0; i < 11; i++) {
        ports[i].index = i;
        ports[i].ds = &sw;
        sw.ports[i] = &ports[i];
    }
    primary.dsa_ptr = &ports[8];
    secondary.dsa_ptr = &ports[4];
    ports[8].conduit = &primary;
    ports[4].conduit = &secondary;
    for (unsigned int i = 0; i < 4; i++) {
        ports[i].cpu_dp = &ports[i == 3 ? 4 : 8];
        ports[i].user = &users[i];
        ports[i].bridge = true;
    }
    memset(frame, 0, sizeof(frame));
    memcpy(frame, "\x01\x80\xc2\x00\x00\x00\x02\x00\x00\x00\x00\x01", 12);
    put16(12, 0x9988);
    put16(14, 0);
    put16(16, 0x8000 | (port << 11) | (prio << 8) | 0x80 | (reason << 1));
    put16(18, 0);
    put16(20, 38);
    frame[22] = 0x42; frame[23] = 0x42; frame[24] = 0x03;
    for (unsigned int i = 25; i < 60; i++) frame[i] = i;
    memcpy(expected, frame, 12);
    memcpy(expected + 12, frame + 20, 48);
    skb = (struct sk_buff){ .data = frame + 14, .len = 54, .dev = &primary };
    warnings = mark_calls = 0;
}
static void delivered(unsigned int port, unsigned int prio,
                      unsigned int warn, bool forwarded)
{
    assert(yt921x_tag_rcv(&skb, &primary) == &skb);
    assert(skb.dev == &users[port] && skb.priority == prio);
    assert(warnings == warn && mark_calls == (unsigned int)forwarded);
    assert(skb.offload_fwd_mark == forwarded);
    assert(skb.data == frame + 22 && skb.len == 46);
    assert(!memcmp(skb.data - 14, expected, 60));
}
int main(void)
{
    for (unsigned int port = 0; port < 4; port++) {
        reset(port, 0x3f, 0);
        delivered(port, 0, 0, false);
    }
    reset(3, 0x3f, 5);
    assert(ports[3].cpu_dp == &ports[4] && primary.dsa_ptr == &ports[8]);
    delivered(3, 5, 0, false);

    /* Preserve an actual customer tag behind the private CPU header. */
    reset(3, 0x3f, 0);
    put16(20, 0x8100); put16(22, 0xb064); put16(24, 38);
    memcpy(expected + 12, frame + 20, 48);
    delivered(3, 0, 0, false);

    const unsigned int forwarded[] = { 0x00, 0x1b, 0x1c };
    for (unsigned int i = 0; i < 3; i++) {
        reset(3, forwarded[i], 0);
        delivered(3, 0, 0, true);
    }
    const unsigned int trapped[] = { 0x18, 0x19, 0x1a };
    for (unsigned int i = 0; i < 3; i++) {
        reset(3, trapped[i], 0);
        delivered(3, 0, 0, false);
    }
    reset(3, 0x3e, 0);
    delivered(3, 0, 1, false);
    reset(3, 0x3f, 0); frame[17] &= ~0x80;
    delivered(3, 0, 1, false);

    for (unsigned int port = 4; port <= 8; port += 4) {
        reset(port, 0x3f, 0);
        assert(!yt921x_tag_rcv(&skb, &primary) && !warnings && !mark_calls);
    }
    reset(7, 0x3f, 0);
    assert(!yt921x_tag_rcv(&skb, &primary) && warnings == 1);
    reset(3, 0x3f, 0); frame[16] &= ~0x80;
    assert(!yt921x_tag_rcv(&skb, &primary) && warnings == 1);
    reset(3, 0x3f, 0); skb.len = 7;
    assert(!yt921x_tag_rcv(&skb, &primary) && !warnings);

    reset(3, 0x3f, 0); put16(12, 0x0806);
    assert(!yt921x_tag_rcv(&skb, &primary) && !warnings);
    reset(3, 0x3f, 0); put16(12, 0x0806);
    assert(yt921x_tag_rcv(&skb, &secondary) == &skb);
    assert(skb.dev == &users[3] && skb.data == frame + 14 && skb.len == 54);
    assert(!warnings && !mark_calls);
    reset(3, 0x3f, 0); put16(12, 0x0806);
    ports[2].cpu_dp = &ports[4];
    assert(!yt921x_tag_rcv(&skb, &secondary) && warnings == 1);

    puts("PASS: RMA WAN identity, payload/VLAN preservation, forwarding marks, CPU-source and malformed/raw guards");
    return 0;
}
"""


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=root / "target/linux/generic/files/net/dsa/tag_yt921x_enhanced.c")
    parser.add_argument("--sanitize", default="address,undefined")
    args = parser.parse_args()
    source = args.source.read_text()
    definitions = "\n".join(line for line in source.splitlines()
                            if re.match(r"#define\s+YT921X_TAG_\w+", line))
    codes = re.search(r"enum yt921x_tag_code \{.*?^\};", source, re.M | re.S)
    assert codes, "missing production reason-code enum"
    functions = "\n".join(extract(source, name) for name in
                           ("yt921x_conduit_is_raw", "yt921x_tag_rcv"))
    program = HARNESS.replace("/* PRODUCTION */", definitions + "\n" + codes.group() + "\n" + functions)
    flags = ["-fsanitize=" + args.sanitize] if args.sanitize else []
    with tempfile.TemporaryDirectory(prefix="yt921x-legacy-rx-") as temp:
        cfile = Path(temp) / "rx.c"
        exe = Path(temp) / "rx"
        cfile.write_text(program)
        subprocess.run(shlex.split(os.environ.get("CC", "cc")) +
                       ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O0", *flags,
                        str(cfile), "-o", str(exe)], check=True)
        environment = dict(os.environ, ASAN_OPTIONS="detect_leaks=1")
        return subprocess.run([str(exe)], env=environment).returncode


if __name__ == "__main__":
    raise SystemExit(main())
