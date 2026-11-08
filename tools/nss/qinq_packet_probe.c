/* qinq_packet_probe: exact-frame QinQ test helper. */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef TP_STATUS_VLAN_VALID
#define TP_STATUS_VLAN_VALID (1u << 4)
#endif
#ifndef TP_STATUS_VLAN_TPID_VALID
#define TP_STATUS_VLAN_TPID_VALID (1u << 6)
#endif

#define QINQ_HDR 38u
#define QINQ_MIN 64u
#define QINQ_MAX 9216u
#define QINQ_MAGIC 0x88b5u
#define CAPTURE_MAX (65536u + 4u)

enum command { CMD_EMIT, CMD_CHECK, CMD_SEND, CMD_RECV };

struct params {
    char iface[IF_NAMESIZE];
    uint8_t src[6], dst[6];
    uint16_t outer_type, outer_tci, inner_type, inner_tci;
    uint32_t cookie, seq, count;
    uint16_t length;
    uint32_t interval_ms, timeout_ms;
    const char *file, *ready, *frames;
};

static volatile sig_atomic_t g_stop;

static void on_signal(int sig) { (void)sig; g_stop = 1; }

static void put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t get32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void json_str(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    putchar('"');
    for (; *p; p++) {
        unsigned c = *p;
        if (c == '"' || c == '\\') {
            putchar('\\');
            putchar((int)c);
        } else if (c == '\n') {
            fputs("\\n", stdout);
        } else if (c == '\r') {
            fputs("\\r", stdout);
        } else if (c == '\t') {
            fputs("\\t", stdout);
        } else if (c < 0x20) {
            printf("\\u%04x", c);
        } else {
            putchar((int)c);
        }
    }
    putchar('"');
}

static int finish(void) { return fflush(stdout) == 0 ? 0 : 1; }

static int jerr(const char *cmd, const char *msg, int err) {
    printf("{\"ok\":false,\"command\":");
    json_str(cmd);
    printf(",\"error\":");
    json_str(msg);
    if (err)
        printf(",\"errno\":%d", err);
    puts("}");
    (void)finish();
    return 1;
}

static int sysmsg(const char *cmd, const char *what, int err) {
    char m[256];
    snprintf(m, sizeof m, "%s: %s", what, strerror(err));
    return jerr(cmd, m, err);
}

static int path_err(const char *cmd, const char *what, const char *path, int err) {
    char m[512];
    snprintf(m, sizeof m, "%s %s: %s", what, path, strerror(err));
    return jerr(cmd, m, err);
}

static int hexval(unsigned char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static bool parse_u32(const char *s, uint32_t max, uint32_t *out) {
    if (!s || !*s)
        return false;
    unsigned base = 10;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    }
    if (!*s)
        return false;
    uint64_t v = 0;
    for (; *s; s++) {
        int d = hexval((unsigned char)*s);
        if (d < 0 || (unsigned)d >= base)
            return false;
        v = v * base + (unsigned)d;
        if (v > max)
            return false;
    }
    *out = (uint32_t)v;
    return true;
}

static bool parse_tpid(const char *s, uint16_t *out) {
    if (!s || !*s)
        return false;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2;
    if (!*s)
        return false;
    uint32_t v = 0;
    for (; *s; s++) {
        int d = hexval((unsigned char)*s);
        if (d < 0)
            return false;
        v = (v << 4) | (uint32_t)d;
        if (v > 0xffffu)
            return false;
    }
    *out = (uint16_t)v;
    return true;
}

static bool parse_mac(const char *s, uint8_t out[6]) {
    if (!s || strlen(s) != 17)
        return false;
    for (size_t i = 0; i < 6; i++) {
        int hi = hexval((unsigned char)s[i * 3]);
        int lo = hexval((unsigned char)s[i * 3 + 1]);
        if (hi < 0 || lo < 0)
            return false;
        out[i] = (uint8_t)((hi << 4) | lo);
        if (i < 5 && s[i * 3 + 2] != ':' && s[i * 3 + 2] != '-')
            return false;
    }
    return true;
}

static void build_frame(const struct params *p, uint32_t seq, uint8_t *f, size_t len) {
    memcpy(f, p->dst, 6);
    memcpy(f + 6, p->src, 6);
    put16(f + 12, p->outer_type);
    put16(f + 14, p->outer_tci);
    put16(f + 16, p->inner_type);
    put16(f + 18, p->inner_tci);
    put16(f + 20, QINQ_MAGIC);
    memcpy(f + 22, "NQ20", 4);
    put32(f + 26, p->cookie);
    put32(f + 30, seq);
    put16(f + 34, (uint16_t)len);
    put16(f + 36, 0);
    for (size_t i = QINQ_HDR; i < len; i++)
        f[i] = (uint8_t)(i ^ p->cookie ^ seq);
}

static int write_all(int fd, const void *buf, size_t len) {
    const uint8_t *p = buf;
    while (len) {
        ssize_t n = write(fd, p, len);
        if (n < 0) {
            if (errno == EINTR && !g_stop)
                continue;
            return -1;
        }
        if (n == 0) {
            errno = EIO;
            return -1;
        }
        p += (size_t)n;
        len -= (size_t)n;
    }
    return 0;
}

static int msleep(uint32_t ms) {
    struct timespec req = { (time_t)(ms / 1000), (long)(ms % 1000) * 1000000L };
    while (nanosleep(&req, &req) < 0) {
        if (errno != EINTR)
            return -1;
        if (g_stop)
            return -1;
    }
    return 0;
}

static int cmd_emit(const struct params *p) {
    uint8_t *f = malloc(p->length);
    if (!f)
        return jerr("emit", "out of memory", ENOMEM);
    build_frame(p, p->seq, f, p->length);
    int fd = open(p->file, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        int e = errno;
        free(f);
        return path_err("emit", "open", p->file, e);
    }
    int rc = write_all(fd, f, p->length);
    int e = errno;
    if (close(fd) < 0 && rc == 0) {
        rc = -1;
        e = errno;
    }
    free(f);
    if (rc < 0)
        return path_err("emit", "write", p->file, e);
    printf("{\"ok\":true,\"command\":\"emit\",\"file\":");
    json_str(p->file);
    printf(",\"seq\":%u,\"length\":%u,\"outer_type\":\"0x%04x\",\"outer_tci\":%u,"
           "\"inner_type\":\"0x%04x\",\"inner_tci\":%u,\"cookie\":%u}\n",
           (unsigned)p->seq, (unsigned)p->length, (unsigned)p->outer_type,
           (unsigned)p->outer_tci, (unsigned)p->inner_type,
           (unsigned)p->inner_tci, (unsigned)p->cookie);
    return finish();
}

static int cmd_check(const struct params *p) {
    uint8_t *exp = malloc(p->length);
    uint8_t *act = malloc((size_t)p->length + 1);
    if (!exp || !act) {
        free(exp);
        free(act);
        return jerr("check", "out of memory", ENOMEM);
    }
    build_frame(p, p->seq, exp, p->length);
    int fd = open(p->file, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        int e = errno;
        free(exp);
        free(act);
        return path_err("check", "open", p->file, e);
    }
    struct stat st;
    bool have_reg = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size >= 0;
    size_t want = (size_t)p->length + 1, total = 0;
    int e = 0;
    while (total < want) {
        ssize_t n = read(fd, act + total, want - total);
        if (n < 0) {
            if (errno == EINTR && !g_stop)
                continue;
            e = errno;
            break;
        }
        if (n == 0)
            break;
        total += (size_t)n;
    }
    int ce = close(fd);
    if (e) {
        free(exp);
        free(act);
        return path_err("check", "read", p->file, e);
    }
    if (ce < 0) {
        e = errno;
        free(exp);
        free(act);
        return path_err("check", "close", p->file, e);
    }
    unsigned long long actual =
        have_reg ? (unsigned long long)st.st_size : (unsigned long long)total;
    bool len_ok = actual == (unsigned long long)p->length && total == (size_t)p->length;
    size_t cmp = total < (size_t)p->length ? total : (size_t)p->length;
    size_t bad = SIZE_MAX;
    for (size_t i = 0; i < cmp; i++) {
        if (act[i] != exp[i]) {
            bad = i;
            break;
        }
    }
    if (bad == SIZE_MAX && !len_ok)
        bad = cmp;
    bool ok = len_ok && bad == SIZE_MAX;
    printf("{\"ok\":%s,\"command\":\"check\",\"file\":", ok ? "true" : "false");
    json_str(p->file);
    printf(",\"seq\":%u,\"expected_length\":%u,\"actual_length\":%llu,"
           "\"first_mismatch\":", (unsigned)p->seq, (unsigned)p->length, actual);
    if (bad == SIZE_MAX)
        printf("null");
    else
        printf("%zu", bad);
    puts("}");
    free(exp);
    free(act);
    int rc = finish();
    return ok ? rc : 1;
}

static int bind_to(int fd, unsigned idx) {
    struct sockaddr_ll s = { .sll_family = AF_PACKET, .sll_protocol = htons(ETH_P_ALL), .sll_ifindex = (int)idx };
    return bind(fd, (struct sockaddr *)&s, (socklen_t)sizeof s);
}

static int make_ready(const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (fd < 0) return -1;
    if (close(fd) < 0) { int e = errno; unlink(path); errno = e; return -1; }
    return 0;
}

static int write_frame(const char *pre, uint32_t seq, const uint8_t *b, size_t n) {
    char path[4096];
    int w = snprintf(path, sizeof path, "%s.%u.bin", pre, (unsigned)seq), fd, rc, e;
    if (w < 0 || (size_t)w >= sizeof path) { errno = ENAMETOOLONG; return -1; }
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return -1;
    rc = write_all(fd, b, n); e = errno;
    if (close(fd) < 0 && rc == 0) { rc = -1; e = errno; }
    if (rc < 0) errno = e;
    return rc;
}

static bool find_candidate(const uint8_t *f, size_t n, uint32_t cookie, uint32_t *seq) {
    for (unsigned k = 0; k <= 2; k++) {
        size_t p = 12u + 4u * k;
        if (n < p + 14 || f[p] != 0x88 || f[p + 1] != 0xb5 || memcmp(f + p + 2, "NQ20", 4)) continue;
        if (get32(f + p + 6) != cookie) continue;
        *seq = get32(f + p + 10);
        return true;
    }
    return false;
}

static void add_ms(struct timespec *t, uint32_t ms) {
    t->tv_sec += (time_t)(ms / 1000); t->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (t->tv_nsec >= 1000000000L) { t->tv_sec++; t->tv_nsec -= 1000000000L; }
}

static bool expired(const struct timespec *d) {
    struct timespec n;
    if (clock_gettime(CLOCK_MONOTONIC, &n) < 0) return true;
    return n.tv_sec > d->tv_sec || (n.tv_sec == d->tv_sec && n.tv_nsec >= d->tv_nsec);
}

static int poll_ms(const struct timespec *d) {
    struct timespec n; long long x;
    if (clock_gettime(CLOCK_MONOTONIC, &n) < 0) return 0;
    x = ((long long)d->tv_sec - (long long)n.tv_sec) * 1000000000LL + (long long)d->tv_nsec - (long long)n.tv_nsec;
    if (x <= 0) return 0;
    x = x / 1000000 + (x % 1000000 != 0);
    return (int)(x > INT_MAX ? INT_MAX : x);
}

static int cmd_send(const struct params *p) {
    errno = 0;
    unsigned idx = if_nametoindex(p->iface);
    if (!idx) return path_err("send", "if_nametoindex", p->iface, errno ? errno : ENODEV);
    int fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (fd < 0) return sysmsg("send", "socket", errno);
    if (bind_to(fd, idx) < 0) { int e = errno; close(fd); return path_err("send", "bind", p->iface, e); }
    uint8_t *f = malloc(p->length);
    if (!f) { close(fd); return jerr("send", "out of memory", ENOMEM); }
    struct sockaddr_ll a = { .sll_family = AF_PACKET, .sll_protocol = htons(ETH_P_ALL), .sll_ifindex = (int)idx, .sll_halen = 6 };
    memcpy(a.sll_addr, p->dst, 6);
    unsigned sent = 0, errs = 0; int first = 0;
    for (uint32_t s = 0; s < p->count && !g_stop; s++) {
        build_frame(p, s, f, p->length);
        ssize_t n = sendto(fd, f, p->length, 0, (struct sockaddr *)&a, (socklen_t)sizeof a);
        if (n == (ssize_t)p->length) sent++;
        else { errs++; if (!first) first = n < 0 ? errno : EIO; }
        if (s + 1 < p->count && msleep(p->interval_ms) < 0) { if (!g_stop) { errs++; if (!first) first = errno ? errno : EINTR; } break; }
    }
    close(fd); free(f);
    bool ok = sent == p->count && !errs && !g_stop;
    printf("{\"ok\":%s,\"command\":\"send\",\"iface\":", ok ? "true" : "false"); json_str(p->iface);
    printf(",\"sent\":%u,\"errors\":%u,\"count\":%u,\"length\":%u,\"interval_ms\":%u",
           sent, errs, (unsigned)p->count, (unsigned)p->length, (unsigned)p->interval_ms);
    if (first) { printf(",\"first_errno\":%d,\"first_error\":", first); json_str(strerror(first)); }
    if (g_stop) printf(",\"interrupted\":true");
    puts("}"); int rc = finish(); return ok ? rc : 1;
}

static int read_aux(const struct msghdr *mh, size_t cap, struct tpacket_auxdata *ax, bool *have) {
    *have = false;
    if (mh->msg_flags & MSG_CTRUNC) { errno = EMSGSIZE; return -1; }
    if (mh->msg_controllen > cap || (mh->msg_controllen && !mh->msg_control)) { errno = EBADMSG; return -2; }
    size_t left = mh->msg_controllen;
    const unsigned char *p = mh->msg_control;
    while (left) {
        if (left < sizeof(struct cmsghdr)) { errno = EBADMSG; return -2; }
        const struct cmsghdr *cm = (const struct cmsghdr *)p;
        size_t hlen = (size_t)cm->cmsg_len;
        if (hlen < CMSG_LEN(0) || hlen > left) { errno = EBADMSG; return -2; }
        if (cm->cmsg_level == SOL_PACKET && cm->cmsg_type == PACKET_AUXDATA) {
            if (hlen < CMSG_LEN(sizeof *ax)) { errno = EBADMSG; return -2; }
            memcpy(ax, CMSG_DATA(cm), sizeof *ax);
            *have = true;
        }
        /* A complete final cmsg need not include its alignment padding. */
        if (hlen == left) break;
        size_t step = CMSG_ALIGN(hlen);
        if (step > left) { errno = EBADMSG; return -2; }
        p += step;
        left -= step;
    }
    return 0;
}

static int cmd_recv(const struct params *p) {
    errno = 0;
    unsigned idx = if_nametoindex(p->iface);
    if (!idx) return path_err("recv", "if_nametoindex", p->iface, errno ? errno : ENODEV);
    int fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (fd < 0) return sysmsg("recv", "socket", errno);
    int one = 1;
    if (setsockopt(fd, SOL_PACKET, PACKET_AUXDATA, &one, (socklen_t)sizeof one) < 0) { int e = errno; close(fd); return sysmsg("recv", "PACKET_AUXDATA", e); }
    struct packet_mreq m = { .mr_ifindex = (int)idx, .mr_type = PACKET_MR_PROMISC };
    if (setsockopt(fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &m, (socklen_t)sizeof m) < 0) { int e = errno; close(fd); return sysmsg("recv", "PACKET_MR_PROMISC", e); }
    if (bind_to(fd, idx) < 0) { int e = errno; close(fd); return path_err("recv", "bind", p->iface, e); }
    uint8_t *raw = malloc(CAPTURE_MAX), *rec = malloc((size_t)CAPTURE_MAX + 4), *exp = malloc(p->length);
    if (!raw || !rec || !exp) { free(raw); free(rec); free(exp); close(fd); return jerr("recv", "out of memory", ENOMEM); }
    if (p->ready && make_ready(p->ready) < 0) { int e = errno; free(raw); free(rec); free(exp); close(fd); return path_err("recv", "ready", p->ready, e); }
    struct timespec dl;
    if (clock_gettime(CLOCK_MONOTONIC, &dl) < 0) { int e = errno; free(raw); free(rec); free(exp); close(fd); return sysmsg("recv", "clock_gettime", e); }
    add_ms(&dl, p->timeout_ms);
    bool seen[256] = { false };
    unsigned uq = 0, bad = 0, dup = 0, out = 0, unrel = 0, auxn = 0;
    bool fatal = false; int fe = 0; char fm[256] = "";
    union { struct cmsghdr align; unsigned char bytes[256]; } cb;
    while (!g_stop && !fatal && !expired(&dl)) {
        int pms = poll_ms(&dl);
        if (pms <= 0) break;
        struct pollfd pf = { .fd = fd, .events = POLLIN };
        int pr = poll(&pf, 1, pms);
        if (pr < 0) { if (errno == EINTR) continue; snprintf(fm, sizeof fm, "poll: %s", strerror(errno)); fe = errno; fatal = true; break; }
        if (!pr) continue;
        if (pf.revents & POLLNVAL) { snprintf(fm, sizeof fm, "poll: invalid socket"); fe = EINVAL; fatal = true; break; }
        while (!g_stop && !fatal && !expired(&dl)) {
            struct sockaddr_ll sa = { 0 };
            struct iovec io = { raw, CAPTURE_MAX };
            struct msghdr mh = { 0 };
            mh.msg_name = &sa; mh.msg_namelen = (socklen_t)sizeof sa; mh.msg_iov = &io; mh.msg_iovlen = 1;
            mh.msg_control = cb.bytes; mh.msg_controllen = sizeof cb.bytes;
            ssize_t n = recvmsg(fd, &mh, MSG_DONTWAIT);
            if (n < 0) {
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                snprintf(fm, sizeof fm, "recvmsg: %s", strerror(errno)); fe = errno; fatal = true; break;
            }
            struct tpacket_auxdata ax = { 0 }; bool ha = false;
            int mr = read_aux(&mh, sizeof cb.bytes, &ax, &ha);
            if (mr < 0) {
                fe = errno;
                snprintf(fm, sizeof fm, "metadata: %s", mr == -1 ? "ancillary data truncated" : "malformed ancillary data");
                fatal = true; break;
            }
            if (sa.sll_pkttype == PACKET_OUTGOING) { out++; continue; }
            size_t rn = (size_t)n, rl;
            if (rn > CAPTURE_MAX) rn = CAPTURE_MAX;
            if (ha && (ax.tp_status & TP_STATUS_VLAN_VALID)) {
                if (rn < 12) { unrel++; continue; }
                uint16_t tp = (ax.tp_status & TP_STATUS_VLAN_TPID_VALID) ? ax.tp_vlan_tpid : 0x8100u;
                memcpy(rec, raw, 12); put16(rec + 12, tp); put16(rec + 14, ax.tp_vlan_tci);
                memcpy(rec + 16, raw + 12, rn - 12); rl = rn + 4; auxn++;
            } else { memcpy(rec, raw, rn); rl = rn; }
            uint32_t ws;
            if (!find_candidate(rec, rl, p->cookie, &ws)) { unrel++; continue; }
            if ((mh.msg_flags & MSG_TRUNC) || ws >= p->count) { bad++; continue; }
            build_frame(p, ws, exp, p->length);
            if (rl != (size_t)p->length || memcmp(rec, exp, (size_t)p->length)) { bad++; continue; }
            if (seen[ws]) { dup++; continue; }
            seen[ws] = true; uq++;
            if (p->frames && write_frame(p->frames, ws, rec, rl) < 0) { snprintf(fm, sizeof fm, "frames: %s", strerror(errno)); fe = errno; fatal = true; break; }
        }
    }
    close(fd); free(raw); free(rec); free(exp);
    bool ok = !fatal && !g_stop && uq == p->count && !bad && !dup;
    printf("{\"ok\":%s,\"command\":\"recv\",\"iface\":", ok ? "true" : "false"); json_str(p->iface);
    printf(",\"unique\":%u,\"invalid\":%u,\"duplicates\":%u,\"outgoing_ignored\":%u,"
           "\"unrelated\":%u,\"aux_reconstructed\":%u,\"count\":%u,\"length\":%u,"
           "\"timeout_ms\":%u,\"missing\":%u", uq, bad, dup, out, unrel, auxn,
           (unsigned)p->count, (unsigned)p->length, (unsigned)p->timeout_ms, p->count - uq);
    if (p->frames) { printf(",\"frames_prefix\":"); json_str(p->frames); }
    if (g_stop) printf(",\"interrupted\":true");
    if (fatal) { printf(",\"error\":"); json_str(fm); printf(",\"errno\":%d", fe); }
    puts("}"); int rc = finish(); return ok ? rc : 1;
}

enum { O_IFACE = 1000, O_SRC, O_DST, O_OT, O_OTC, O_IT, O_ITC, O_COOKIE, O_SEQ, O_COUNT, O_LEN, O_INT, O_TO, O_FILE, O_READY, O_FRAMES };
static const struct option lopts[] = {
    { "iface", 1, NULL, O_IFACE }, { "src", 1, NULL, O_SRC }, { "dst", 1, NULL, O_DST },
    { "outer-type", 1, NULL, O_OT }, { "outer-tci", 1, NULL, O_OTC }, { "inner-type", 1, NULL, O_IT },
    { "inner-tci", 1, NULL, O_ITC }, { "cookie", 1, NULL, O_COOKIE }, { "seq", 1, NULL, O_SEQ },
    { "count", 1, NULL, O_COUNT }, { "length", 1, NULL, O_LEN }, { "interval-ms", 1, NULL, O_INT },
    { "timeout-ms", 1, NULL, O_TO }, { "file", 1, NULL, O_FILE }, { "ready", 1, NULL, O_READY },
    { "frames", 1, NULL, O_FRAMES }, { NULL, 0, NULL, 0 }
};

static void usage(void) {
    fputs("usage: qinq_packet_probe <emit|check|send|recv> [options]\n"
          "  wire: --src MAC --dst MAC --outer-type TPID --outer-tci U16\n"
          "        --inner-type TPID --inner-tci U16 --cookie U32 (all required)\n"
          "  TPID is hex 8100/88a8; --length 64..9216 (default 256)\n"
          "  emit/check: --seq U32 (default 0) --file PATH\n"
          "  send: --count 1..256 (default 3) --interval-ms 1..1000 (default 50)\n"
          "  recv: --timeout-ms 1..10000 (default 1500) [--ready PATH] [--frames PREFIX]\n"
          "  send/recv: --iface IFACE\n", stderr);
}

static int arg_err(const char *o) { fprintf(stderr, "qinq_packet_probe: invalid --%s\n", o); return 2; }

int main(int argc, char **argv) {
    struct params p; memset(&p, 0, sizeof p);
    p.count = 3; p.length = 256; p.interval_ms = 50; p.timeout_ms = 1500;
    if (argc < 2) { usage(); return 2; }
    enum command cmd;
    if (!strcmp(argv[1], "emit")) cmd = CMD_EMIT;
    else if (!strcmp(argv[1], "check")) cmd = CMD_CHECK;
    else if (!strcmp(argv[1], "send")) cmd = CMD_SEND;
    else if (!strcmp(argv[1], "recv")) cmd = CMD_RECV;
    else { usage(); return 2; }
    bool hs = false, hd = false, hot = false, hotc = false, hit = false, hitc = false, hc = false;
    int oi = 0, c; opterr = 0;
    while ((c = getopt_long(argc - 1, argv + 1, "+", lopts, &oi)) != -1) {
        uint32_t v;
        switch (c) {
        case O_IFACE:
            if (!optarg[0] || strlen(optarg) >= sizeof p.iface) return arg_err(lopts[oi].name);
            memcpy(p.iface, optarg, strlen(optarg) + 1); break;
        case O_SRC: if (!parse_mac(optarg, p.src)) return arg_err(lopts[oi].name); hs = true; break;
        case O_DST: if (!parse_mac(optarg, p.dst)) return arg_err(lopts[oi].name); hd = true; break;
        case O_OT:
            if (!parse_tpid(optarg, &p.outer_type) || (p.outer_type != 0x8100u && p.outer_type != 0x88a8u)) return arg_err(lopts[oi].name);
            hot = true; break;
        case O_OTC: if (!parse_u32(optarg, 0xffffu, &v)) return arg_err(lopts[oi].name); p.outer_tci = (uint16_t)v; hotc = true; break;
        case O_IT:
            if (!parse_tpid(optarg, &p.inner_type) || (p.inner_type != 0x8100u && p.inner_type != 0x88a8u)) return arg_err(lopts[oi].name);
            hit = true; break;
        case O_ITC: if (!parse_u32(optarg, 0xffffu, &v)) return arg_err(lopts[oi].name); p.inner_tci = (uint16_t)v; hitc = true; break;
        case O_COOKIE: if (!parse_u32(optarg, UINT32_MAX, &p.cookie)) return arg_err(lopts[oi].name); hc = true; break;
        case O_SEQ: if (!parse_u32(optarg, UINT32_MAX, &p.seq)) return arg_err(lopts[oi].name); break;
        case O_COUNT: if (!parse_u32(optarg, 256, &p.count) || !p.count) return arg_err(lopts[oi].name); break;
        case O_LEN: if (!parse_u32(optarg, QINQ_MAX, &v) || v < QINQ_MIN) return arg_err(lopts[oi].name); p.length = (uint16_t)v; break;
        case O_INT: if (!parse_u32(optarg, 1000, &p.interval_ms) || !p.interval_ms) return arg_err(lopts[oi].name); break;
        case O_TO: if (!parse_u32(optarg, 10000, &p.timeout_ms) || !p.timeout_ms) return arg_err(lopts[oi].name); break;
        case O_FILE: if (!optarg[0]) return arg_err(lopts[oi].name); p.file = optarg; break;
        case O_READY: if (!optarg[0]) return arg_err(lopts[oi].name); p.ready = optarg; break;
        case O_FRAMES: if (!optarg[0]) return arg_err(lopts[oi].name); p.frames = optarg; break;
        case '?': fputs("qinq_packet_probe: unknown or incomplete option\n", stderr); usage(); return 2;
        default: usage(); return 2;
        }
    }
    if (optind != argc - 1) { usage(); return 2; }
    if (!(hs && hd && hot && hotc && hit && hitc && hc)) { fputs("qinq_packet_probe: missing wire options\n", stderr); return 2; }
    if (cmd == CMD_EMIT || cmd == CMD_CHECK) {
        if (!p.file) { fputs("qinq_packet_probe: --file is required\n", stderr); return 2; }
    } else if (!p.iface[0]) {
        fputs("qinq_packet_probe: --iface is required\n", stderr); return 2;
    }
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_handler = on_signal; sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) < 0 || sigaction(SIGTERM, &sa, NULL) < 0 || sigaction(SIGHUP, &sa, NULL) < 0)
        return jerr(argv[1], "sigaction failed", errno);
    switch (cmd) {
    case CMD_EMIT: return cmd_emit(&p);
    case CMD_CHECK: return cmd_check(&p);
    case CMD_SEND: return cmd_send(&p);
    case CMD_RECV: return cmd_recv(&p);
    }
    return 2;
}
