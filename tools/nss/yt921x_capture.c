// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * yt921x_capture - bounded diagnostic capture and normalization.
 *
 * raw/qinq: plain Ethernet; enhanced: DA/SA + private 8B tag + inner type.
 * PREFIX.raw.pcap is the wire view (stripped AUXDATA VLAN reinserted),
 * PREFIX.pcap is the normalized Ethernet view (private tag removed).
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <poll.h>
#include <pcap/pcap.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef DLT_DEBUG_ONLY
#define DLT_DEBUG_ONLY 303
#endif
#ifndef TP_STATUS_VLAN_VALID
#define TP_STATUS_VLAN_VALID (1u << 4)
#endif
#ifndef TP_STATUS_VLAN_TPID_VALID
#define TP_STATUS_VLAN_TPID_VALID (1u << 6)
#endif

#define MAX_FRAME 65536u
#define BUFSZ (MAX_FRAME + 4u)
#define ETH_P_YT921X 0x9988
#define YT_TAG_LEN 8
#define ENH_MIN (12 + YT_TAG_LEN + 2)

struct opts {
	const char *mode, *prefix, *iface, *read, *filter;
	unsigned long count;
	int timeout_ms;
};

struct vlan_aux {
	bool present;
	uint16_t tpid, tci;
};

struct ctx {
	struct opts o;
	bool enhanced, have_prog;
	pcap_t *raw_pcap, *norm_pcap;
	pcap_dumper_t *raw_dump, *norm_dump;
	FILE *json;
	struct bpf_program prog;
	unsigned long seen, matched, malformed;
	int dropped;
	bool timed_out;
	unsigned char rawbuf[BUFSZ];
	unsigned char normbuf[BUFSZ];
	char *path_raw, *path_norm, *path_json;
};

static void usage(FILE *f)
{
	fputs("usage: yt921x_capture --mode raw|qinq|enhanced --prefix PATH\n"
	      "       (--iface IFNAME | --read FILE) [--filter EXPR]\n"
	      "       [--count N] [--timeout-ms N]\n", f);
}

static int parse_ulong(const char *s, unsigned long *v)
{
	char *end;
	unsigned long x;

	if (!*s || *s == '-')
		return -1;
	errno = 0;
	x = strtoul(s, &end, 0);
	if (errno || *end)
		return -1;
	*v = x;
	return 0;
}

static int parse_args(int argc, char **argv, struct opts *o)
{
	enum {
		OPT_MODE = 1000, OPT_PREFIX, OPT_IFACE, OPT_READ,
		OPT_FILTER, OPT_COUNT, OPT_TIMEOUT
	};
	static const struct option lo[] = {
		{ "mode", required_argument, NULL, OPT_MODE },
		{ "prefix", required_argument, NULL, OPT_PREFIX },
		{ "iface", required_argument, NULL, OPT_IFACE },
		{ "read", required_argument, NULL, OPT_READ },
		{ "filter", required_argument, NULL, OPT_FILTER },
		{ "count", required_argument, NULL, OPT_COUNT },
		{ "timeout-ms", required_argument, NULL, OPT_TIMEOUT },
		{ NULL, 0, NULL, 0 }
	};
	bool have_mode = false, have_prefix = false, have_iface = false;
	bool have_read = false, have_filter = false, have_count = false;
	bool have_timeout = false;
	int ch;

	memset(o, 0, sizeof *o);
	o->timeout_ms = 5000;
	while ((ch = getopt_long(argc, argv, "", lo, NULL)) != -1) {
		switch (ch) {
		case OPT_MODE:
			if (have_mode) return -1;
			o->mode = optarg; have_mode = true; break;
		case OPT_PREFIX:
			if (have_prefix) return -1;
			o->prefix = optarg; have_prefix = true; break;
		case OPT_IFACE:
			if (have_iface) return -1;
			o->iface = optarg; have_iface = true; break;
		case OPT_READ:
			if (have_read) return -1;
			o->read = optarg; have_read = true; break;
		case OPT_FILTER:
			if (have_filter) return -1;
			o->filter = optarg; have_filter = true; break;
		case OPT_COUNT:
			if (have_count || parse_ulong(optarg, &o->count)) return -1;
			have_count = true; break;
		case OPT_TIMEOUT: {
			unsigned long t;
			if (have_timeout || parse_ulong(optarg, &t) ||
			    t == 0 || t > 3600000UL)
				return -1;
			o->timeout_ms = (int)t; have_timeout = true; break;
		}
		default:
			return -1;
		}
	}
	if (optind != argc || !have_mode || !have_prefix || !*o->prefix)
		return -1;
	if (strcmp(o->mode, "raw") && strcmp(o->mode, "qinq") &&
	    strcmp(o->mode, "enhanced"))
		return -1;
	if (have_iface == have_read)
		return -1;
	if ((have_iface && !*o->iface) || (have_read && !*o->read))
		return -1;
	if (have_read && have_timeout)
		return -1;
	if (have_count && o->count > 100000000UL)
		return -1;
	return 0;
}

static int compile_filter(struct ctx *c)
{
	pcap_t *p;

	if (!c->o.filter || !*c->o.filter)
		return 0;
	p = pcap_open_dead(DLT_EN10MB, (int)BUFSZ);
	if (!p) {
		fprintf(stderr, "yt921x_capture: pcap_open_dead failed\n");
		return -1;
	}
	if (pcap_compile(p, &c->prog, c->o.filter, 1,
			 PCAP_NETMASK_UNKNOWN) < 0) {
		fprintf(stderr, "yt921x_capture: filter: %s\n", pcap_geterr(p));
		pcap_close(p);
		return -1;
	}
	c->have_prog = true;
	pcap_close(p);
	return 0;
}

static int parse_aux(const struct msghdr *mh, size_t cap,
		     struct tpacket_auxdata *ax, bool *have)
{
	size_t left;
	const unsigned char *p;

	*have = false;
	if (mh->msg_flags & MSG_CTRUNC)
		return -1;
	if (mh->msg_controllen > cap ||
	    (mh->msg_controllen && !mh->msg_control))
		return -1;
	left = mh->msg_controllen;
	p = mh->msg_control;
	while (left) {
		const struct cmsghdr *cm = (const struct cmsghdr *)p;
		size_t hlen;

		if (left < sizeof(*cm))
			return -1;
		hlen = (size_t)cm->cmsg_len;
		if (hlen < CMSG_LEN(0) || hlen > left)
			return -1;
		if (cm->cmsg_level == SOL_PACKET &&
		    cm->cmsg_type == PACKET_AUXDATA) {
			if (hlen < CMSG_LEN(sizeof(*ax)))
				return -1;
			memcpy(ax, CMSG_DATA(cm), sizeof(*ax));
			*have = true;
		}
		if (hlen == left)
			break;
		hlen = CMSG_ALIGN(hlen);
		if (hlen > left)
			return -1;
		p += hlen;
		left -= hlen;
	}
	return 0;
}

static bool valid_tpid(uint16_t t)
{
	return t == 0x8100 || t == 0x88a8 || t == 0x9100;
}

static int aux_vlan(const struct tpacket_auxdata *ax, bool have,
		    struct vlan_aux *va)
{
	memset(va, 0, sizeof(*va));
	if (!have)
		return 0;
	if ((ax->tp_status & TP_STATUS_VLAN_TPID_VALID) &&
	    !valid_tpid(ax->tp_vlan_tpid))
		return -1;
	if (ax->tp_status & TP_STATUS_VLAN_VALID) {
		va->present = true;
		va->tpid = (ax->tp_status & TP_STATUS_VLAN_TPID_VALID) ?
			   ax->tp_vlan_tpid : 0x8100;
		va->tci = ax->tp_vlan_tci;
	}
	return 0;
}

static void write_json(struct ctx *c, const struct timeval *tv,
		       const char *dir, uint32_t wirelen, size_t caplen,
		       size_t nlen, const struct vlan_aux *va,
		       const unsigned char *tag)
{
	char hex[YT_TAG_LEN * 2 + 1];
	FILE *f = c->json;

	if (tag)
		for (int i = 0; i < YT_TAG_LEN; i++)
			sprintf(hex + i * 2, "%02x", tag[i]);
	fprintf(f, "{\"idx\":%lu,\"mode\":\"%s\",", c->matched, c->o.mode);
	if (dir)
		fprintf(f, "\"direction\":\"%s\",", dir);
	else
		fputs("\"packet_type\":\"offline\",", f);
	fprintf(f, "\"ts_sec\":%ld,\"ts_usec\":%ld,\"ts_source\":\"%s\",",
		(long)tv->tv_sec, (long)tv->tv_usec,
		dir ? "userspace-realtime" : "pcap");
	fprintf(f, "\"original_len\":%" PRIu32
		   ",\"captured_len\":%zu,\"normalized_len\":%zu,",
		wirelen, caplen, nlen);
	if (va->present)
		fprintf(f, "\"aux_present\":true,\"aux_tpid\":%u,"
			   "\"aux_tci\":%u", va->tpid, va->tci);
	else
		fputs("\"aux_present\":false", f);
	if (tag) {
		uint16_t rx = (uint16_t)((tag[4] << 8) | tag[5]);
		uint16_t tx = (uint16_t)((tag[6] << 8) | tag[7]);

		fprintf(f, ",\"tag\":\"%s\",\"rx_valid\":%s,"
			   "\"rx_port\":%u,\"code_en\":%s,\"code\":%u,"
			   "\"priority\":%u,\"tx_valid\":%s,\"tx_mask\":%u",
			hex, (rx & 0x8000) ? "true" : "false",
			(rx >> 11) & 0xfu, (rx & 0x80) ? "true" : "false",
			(rx >> 1) & 0x3fu, (rx >> 8) & 7u,
			(tx & 0x8000) ? "true" : "false", tx & 0x7ffu);
	}
	fputs("}\n", f);
}

static int process(struct ctx *c, const unsigned char *data, size_t caplen,
		   uint32_t wirelen, struct timeval tv, const char *dir,
		   const struct vlan_aux *va)
{
	const unsigned char *raw = data, *norm, *tag = NULL;
	size_t rawlen = caplen, ncap;
	uint32_t rawwire = wirelen, nwire;

	if (caplen > MAX_FRAME || wirelen > MAX_FRAME || caplen > wirelen) {
		c->malformed++;
		return -1;
	}
	if (c->enhanced) {
		if (caplen < ENH_MIN || data[12] != 0x99 || data[13] != 0x88) {
			c->malformed++;
			return -1;
		}
	} else if (caplen < 14) {
		c->malformed++;
		return -1;
	}
	if (va->present) {
		size_t at = c->enhanced ? 20 : 12;

		memcpy(c->rawbuf, data, at);
		c->rawbuf[at + 0] = (unsigned char)(va->tpid >> 8);
		c->rawbuf[at + 1] = (unsigned char)va->tpid;
		c->rawbuf[at + 2] = (unsigned char)(va->tci >> 8);
		c->rawbuf[at + 3] = (unsigned char)va->tci;
		memcpy(c->rawbuf + at + 4, data + at, caplen - at);
		raw = c->rawbuf;
		rawlen = caplen + 4;
		rawwire = wirelen + 4;
	}
	if (c->enhanced) {
		memcpy(c->normbuf, raw, 12);
		memcpy(c->normbuf + 12, raw + 20, rawlen - 20);
		norm = c->normbuf;
		ncap = rawlen - YT_TAG_LEN;
		nwire = rawwire - YT_TAG_LEN;
		tag = raw + 12;
	} else {
		norm = raw;
		ncap = rawlen;
		nwire = rawwire;
	}
	if (c->have_prog) {
		struct pcap_pkthdr h = {
			.ts = tv,
			.caplen = (bpf_u_int32)ncap,
			.len = (bpf_u_int32)nwire,
		};

		if (!pcap_offline_filter(&c->prog, &h, norm))
			return 0;
	}
	{
		struct pcap_pkthdr rh = {
			.ts = tv,
			.caplen = (bpf_u_int32)rawlen,
			.len = rawwire,
		};
		struct pcap_pkthdr nh = {
			.ts = tv,
			.caplen = (bpf_u_int32)ncap,
			.len = nwire,
		};

		pcap_dump((u_char *)c->raw_dump, &rh, raw);
		pcap_dump((u_char *)c->norm_dump, &nh, norm);
	}
	write_json(c, &tv, dir, rawwire, rawlen, ncap, va, tag);
	c->matched++;
	return 0;
}

static int open_excl(const char *path, int *fd)
{
	*fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	if (*fd < 0)
		return -1;
	if (fchmod(*fd, 0600) < 0) {
		int e = errno;

		close(*fd);
		unlink(path);
		errno = e;
		return -1;
	}
	return 0;
}

static char *make_path(const char *prefix, const char *suffix)
{
	size_t n = strlen(prefix) + strlen(suffix) + 1;
	char *p = malloc(n);

	if (p)
		snprintf(p, n, "%s%s", prefix, suffix);
	return p;
}

static int outputs_open(struct ctx *c)
{
	int fr = -1, fn = -1, fj = -1, e;
	bool mr = false, mn = false, mj = false;
	FILE *rf = NULL, *nf = NULL, *jf = NULL;

	c->path_raw = make_path(c->o.prefix, ".raw.pcap");
	c->path_norm = make_path(c->o.prefix, ".pcap");
	c->path_json = make_path(c->o.prefix, ".jsonl");
	if (!c->path_raw || !c->path_norm || !c->path_json)
		goto fail;
	if (open_excl(c->path_raw, &fr))
		goto fail;
	mr = true;
	if (open_excl(c->path_norm, &fn))
		goto fail;
	mn = true;
	if (open_excl(c->path_json, &fj))
		goto fail;
	mj = true;
	c->raw_pcap = pcap_open_dead(c->enhanced ? DLT_DEBUG_ONLY : DLT_EN10MB,
				     (int)BUFSZ);
	c->norm_pcap = pcap_open_dead(DLT_EN10MB, (int)BUFSZ);
	if (!c->raw_pcap || !c->norm_pcap)
		goto fail;
	rf = fdopen(fr, "wb");
	if (!rf)
		goto fail;
	fr = -1;
	nf = fdopen(fn, "wb");
	if (!nf)
		goto fail;
	fn = -1;
	jf = fdopen(fj, "w");
	if (!jf)
		goto fail;
	fj = -1;
	c->raw_dump = pcap_dump_fopen(c->raw_pcap, rf);
	if (!c->raw_dump)
		goto fail;
	rf = NULL;
	c->norm_dump = pcap_dump_fopen(c->norm_pcap, nf);
	if (!c->norm_dump)
		goto fail;
	nf = NULL;
	c->json = jf;
	return 0;

fail:
	e = errno;
	if (rf)
		fclose(rf);
	else if (fr >= 0)
		close(fr);
	if (nf)
		fclose(nf);
	else if (fn >= 0)
		close(fn);
	if (jf)
		fclose(jf);
	else if (fj >= 0)
		close(fj);
	if (c->raw_dump) {
		pcap_dump_close(c->raw_dump);
		c->raw_dump = NULL;
	}
	if (c->norm_dump) {
		pcap_dump_close(c->norm_dump);
		c->norm_dump = NULL;
	}
	if (c->raw_pcap) {
		pcap_close(c->raw_pcap);
		c->raw_pcap = NULL;
	}
	if (c->norm_pcap) {
		pcap_close(c->norm_pcap);
		c->norm_pcap = NULL;
	}
	if (mr)
		unlink(c->path_raw);
	if (mn)
		unlink(c->path_norm);
	if (mj)
		unlink(c->path_json);
	fprintf(stderr, "yt921x_capture: cannot create outputs for %s: %s\n",
		c->o.prefix, strerror(e));
	return -1;
}

static int outputs_close(struct ctx *c)
{
	int rc = 0;

	if (c->raw_dump) {
		if (pcap_dump_flush(c->raw_dump) < 0)
			rc = -1;
		pcap_dump_close(c->raw_dump);
		c->raw_dump = NULL;
	}
	if (c->norm_dump) {
		if (pcap_dump_flush(c->norm_dump) < 0)
			rc = -1;
		pcap_dump_close(c->norm_dump);
		c->norm_dump = NULL;
	}
	if (c->json) {
		if (fflush(c->json) < 0 || ferror(c->json))
			rc = -1;
		if (fclose(c->json) < 0)
			rc = -1;
		c->json = NULL;
	}
	if (c->raw_pcap)
		pcap_close(c->raw_pcap);
	if (c->norm_pcap)
		pcap_close(c->norm_pcap);
	if (c->have_prog) {
		pcap_freecode(&c->prog);
		c->have_prog = false;
	}
	free(c->path_raw);
	free(c->path_norm);
	free(c->path_json);
	return rc;
}

static int run_live(struct ctx *c)
{
	struct timespec dl, now, rt;
	unsigned char *buf = NULL;
	int fd = -1, rc = -1;
	unsigned int idx;

	buf = malloc(BUFSZ);
	if (!buf) {
		fprintf(stderr, "yt921x_capture: out of memory\n");
		goto out;
	}
	idx = if_nametoindex(c->o.iface);
	if (!idx) {
		fprintf(stderr, "yt921x_capture: %s: no such interface\n",
			c->o.iface);
		goto out;
	}
	fd = socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK,
		    htons(ETH_P_ALL));
	if (fd < 0) {
		fprintf(stderr, "yt921x_capture: socket: %s\n", strerror(errno));
		goto out;
	}
	{
		int one = 1;
		struct sockaddr_ll sa = {
			.sll_family = AF_PACKET,
			.sll_protocol = htons(ETH_P_ALL),
			.sll_ifindex = (int)idx,
		};

		if (setsockopt(fd, SOL_PACKET, PACKET_AUXDATA, &one,
			       sizeof(one)) < 0 ||
		    bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
			fprintf(stderr, "yt921x_capture: setup: %s\n",
				strerror(errno));
			goto out;
		}
	}
	clock_gettime(CLOCK_MONOTONIC, &dl);
	dl.tv_sec += c->o.timeout_ms / 1000;
	dl.tv_nsec += (long)(c->o.timeout_ms % 1000) * 1000000L;
	if (dl.tv_nsec >= 1000000000L) {
		dl.tv_sec++;
		dl.tv_nsec -= 1000000000L;
	}
	for (;;) {
		struct pollfd pfd = { .fd = fd, .events = POLLIN };
		long long rem;
		int pr;

		if (c->o.count && c->matched >= c->o.count) {
			rc = 0;
			break;
		}
		clock_gettime(CLOCK_MONOTONIC, &now);
		rem = (long long)(dl.tv_sec - now.tv_sec) * 1000 +
		      (dl.tv_nsec - now.tv_nsec) / 1000000;
		if (rem <= 0) {
			c->timed_out = true;
			rc = 0;
			break;
		}
		pr = poll(&pfd, 1, (int)rem);
		if (pr < 0) {
			if (errno == EINTR)
				continue;
			fprintf(stderr, "yt921x_capture: poll: %s\n",
				strerror(errno));
			break;
		}
		if (pr == 0) {
			c->timed_out = true;
			rc = 0;
			break;
		}
		if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
			fprintf(stderr, "yt921x_capture: poll: interface error\n");
			break;
		}
		if (!(pfd.revents & POLLIN))
			continue;
		{
			union {
				struct cmsghdr h;
				unsigned char b[CMSG_SPACE(
					sizeof(struct tpacket_auxdata))];
			} ctrl;
			struct sockaddr_ll from;
			struct iovec iov = { .iov_base = buf, .iov_len = BUFSZ };
			struct msghdr mh = {
				.msg_name = &from,
				.msg_namelen = sizeof(from),
				.msg_iov = &iov,
				.msg_iovlen = 1,
				.msg_control = ctrl.b,
				.msg_controllen = sizeof(ctrl.b),
			};
			struct tpacket_auxdata ax;
			struct vlan_aux va;
			struct timeval tv;
			const char *dir;
			bool have = false;
			uint32_t wire;
			ssize_t n;

			n = recvmsg(fd, &mh, 0);
			if (n < 0) {
				if (errno == EINTR || errno == EAGAIN ||
				    errno == EWOULDBLOCK)
					continue;
				fprintf(stderr, "yt921x_capture: recvmsg: %s\n",
					strerror(errno));
				break;
			}
			c->seen++;
			if (parse_aux(&mh, sizeof(ctrl.b), &ax, &have) < 0 ||
			    (mh.msg_flags & MSG_TRUNC) ||
			    aux_vlan(&ax, have, &va) < 0) {
				c->malformed++;
				continue;
			}
			wire = (uint32_t)n;
			if (have) {
				if (ax.tp_len < (uint32_t)n ||
				    ax.tp_snaplen < (uint32_t)n ||
				    ax.tp_len > MAX_FRAME) {
					c->malformed++;
					continue;
				}
				wire = ax.tp_len;
			}
			clock_gettime(CLOCK_REALTIME, &rt);
			tv.tv_sec = rt.tv_sec;
			tv.tv_usec = rt.tv_nsec / 1000;
			dir = (mh.msg_namelen >= sizeof(struct sockaddr_ll) &&
			       from.sll_pkttype == PACKET_OUTGOING) ? "tx" : "rx";
			process(c, buf, (size_t)n, wire, tv, dir, &va);
		}
	}
	if (rc == 0) {
		struct tpacket_stats st;
		socklen_t sl = sizeof(st);

		if (getsockopt(fd, SOL_PACKET, PACKET_STATISTICS, &st, &sl) < 0)
			c->dropped = -1;
		else
			c->dropped = (int)st.tp_drops;
	}
out:
	if (fd >= 0)
		close(fd);
	free(buf);
	return rc;
}

static int run_file(struct ctx *c)
{
	char errbuf[PCAP_ERRBUF_SIZE];
	struct vlan_aux va = { 0 };
	struct pcap_pkthdr *h = NULL;
	const u_char *d = NULL;
	pcap_t *p;
	int want, rc;

	p = pcap_open_offline(c->o.read, errbuf);
	if (!p) {
		fprintf(stderr, "yt921x_capture: %s: %s\n", c->o.read, errbuf);
		return -1;
	}
	want = c->enhanced ? DLT_DEBUG_ONLY : DLT_EN10MB;
	if (pcap_datalink(p) != want) {
		fprintf(stderr, "yt921x_capture: %s: DLT %d, need %d\n",
			c->o.read, pcap_datalink(p), want);
		pcap_close(p);
		return -1;
	}
	while ((rc = pcap_next_ex(p, &h, &d)) >= 0) {
		if (rc == 0)
			continue;
		c->seen++;
		if (h->caplen > h->len || h->caplen > MAX_FRAME ||
		    h->len > MAX_FRAME) {
			c->malformed++;
			continue;
		}
		process(c, d, h->caplen, h->len, h->ts, NULL, &va);
		if (c->o.count && c->matched >= c->o.count)
			break;
	}
	if (rc == -1) {
		fprintf(stderr, "yt921x_capture: %s: %s\n", c->o.read,
			pcap_geterr(p));
		pcap_close(p);
		return -1;
	}
	pcap_close(p);
	return 0;
}

int main(int argc, char **argv)
{
	struct ctx *c = calloc(1, sizeof(*c));
	int rc, cr, bad;

	if (!c)
		return 1;
	if (parse_args(argc, argv, &c->o)) {
		fprintf(stderr, "yt921x_capture: invalid arguments\n");
		usage(stderr);
		free(c);
		return 2;
	}
	c->enhanced = !strcmp(c->o.mode, "enhanced");
	if (compile_filter(c)) {
		free(c);
		return 2;
	}
	if (outputs_open(c)) {
		outputs_close(c);
		free(c);
		return 1;
	}
	rc = c->o.iface ? run_live(c) : run_file(c);
	cr = outputs_close(c);
	if (rc < 0) {
		free(c);
		return 1;
	}
	bad = cr < 0 || c->malformed || c->dropped != 0 ||
	      (c->o.count && c->matched < c->o.count);
	printf("{\"matched\":%lu,\"seen\":%lu,\"malformed\":%lu,"
	       "\"dropped\":%d,\"timeout\":%s}\n",
	       c->matched, c->seen, c->malformed, c->dropped,
	       c->timed_out ? "true" : "false");
	free(c);
	return bad ? 1 : 0;
}
