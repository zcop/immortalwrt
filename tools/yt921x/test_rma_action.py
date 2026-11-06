#!/usr/bin/env python3
"""Check both actual RMA helpers against CR881x stock field/action encodings."""
import pathlib, re, subprocess, sys, tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
D = ROOT / "target/linux/generic/files/drivers/net/dsa"
HEADER = D / "yt921x.h"
SOURCES = {"core": D / "yt921x_core.c", "debugfs": D / "yt921x_debugfs.c"}
FN = ("yt921x_rma_ctrl_action_set", "yt921x_stock_rma_ctrl_set")

PRELUDE = r"""
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef unsigned int u32;
typedef unsigned char u8;

#define BIT(n) (1U << (n))
#define GENMASK(h, l) (((~0U) << (l)) & (~0U >> (31 - (h))))
#define FIELD_PREP(mask, val) (((val) << __builtin_ctz(mask)) & (mask))

enum yt921x_rma_action {
	YT921X_RMA_ACT_FORWARD,
	YT921X_RMA_ACT_TRAP_TO_CPU,
	YT921X_RMA_ACT_COPY_TO_CPU,
	YT921X_RMA_ACT_DROP,
};

struct yt921x_priv { int reads, writes, read_err, write_err;
		     u32 read_val, written, last_reg; };

static int yt921x_reg_read(struct yt921x_priv *priv, u32 reg, u32 *valp)
{
	(void)reg; priv->reads++; *valp = priv->read_val; return priv->read_err;
}

static int yt921x_reg_write(struct yt921x_priv *priv, u32 reg, u32 val)
{
	priv->writes++; priv->last_reg = reg; priv->written = val;
	return priv->write_err;
}
"""

EPILOGUE = r"""
static int fails;
#define CK(c, ...) do { if (!(c)) { fails++; if (fails <= 20) { \
	printf("FAIL line %d ", __LINE__); printf(__VA_ARGS__); putchar('\n'); } } } while (0)

static const u32 acts[4] = { 0x000, 0x300, 0x100, 0x200 };
static const u32 pats[] = { 0x000, 0xa55a1fc0U, 0x0200, 0x0100, 0x0300 };
static const u32 starts[] = { 0x000005, 0x001f3f, 0x5a5a12aaU, 0x000ac5 };

static void test_actions(void)
{
	unsigned int c, a, p;

	for (c = 0; c < 64; c++)
		for (a = 0; a < 4; a++)
			for (p = 0; p < 5; p++) {
				u32 s = pats[p] | c, g = s;
				u32 w = (s & ~0x300U) | acts[a];
				if (a==1 || a==2) w=(w & ~0x3fU) | 0x3fU;
				int r = yt921x_rma_ctrl_action_set(&g,
						(enum yt921x_rma_action)a);
				CK(r == 0 && g == w, "act c=%u a=%u p=%u r=%d g=%x w=%x",
				   c, a, p, r, g, w);
			}
	{ u32 g=0xabcdef23U; int r=yt921x_rma_ctrl_action_set(&g,(enum yt921x_rma_action)-1);
	  CK(r==-EINVAL && g==0xabcdef23U,"negative action r=%d g=%x",r,g); }
	for (a = 4; a < 100; a += 95) {
		u32 g = 0x1a5a;
		int r = yt921x_rma_ctrl_action_set(&g, (enum yt921x_rma_action)a);
		CK(r == -EINVAL && g == 0x1a5a, "bad a=%u r=%d g=%x", a, r, g);
	}
}

static void test_wrapper(void)
{
	struct yt921x_priv p;
	unsigned int i, a, iso, vl;
	int r;

	for (i = 0; i < 4; i++)
		for (a = 0; a < 4; a++)
			for (iso = 0; iso < 2; iso++)
				for (vl = 0; vl < 2; vl++) {
					u32 w = (starts[i] & ~0x300U) | acts[a];
				if (a==1 || a==2) w=(w & ~0x3fU) | 0x3fU;
					w = iso ? (w | 0x40) : (w & ~0x40U);
					w = vl ? (w | 0x80) : (w & ~0x80U);
					memset(&p, 0, sizeof(p));
					p.read_val = starts[i];
					r = yt921x_stock_rma_ctrl_set(&p, 0x2f,
						(enum yt921x_rma_action)a, iso, vl);
					CK(r == 0 && p.reads == 1 && p.writes == 1 &&
					   p.written == w &&
					   p.last_reg == 0x1805d0 + 4 * 0x2f,
					   "wrap i=%u a=%u iso=%u vl=%u r=%d g=%x w=%x",
					   i, a, iso, vl, r, p.written, w);
				}
	memset(&p, 0, sizeof(p)); p.read_val = 0x1abc;
	r = yt921x_stock_rma_ctrl_set(&p, 0x2f, (enum yt921x_rma_action)4, 1, 1);
	CK(r == -EINVAL && p.reads == 1 && !p.writes,
	   "bad action r=%d rd=%d wr=%d", r, p.reads, p.writes);
	memset(&p, 0, sizeof(p));
	r = yt921x_stock_rma_ctrl_set(&p, 0x30, YT921X_RMA_ACT_FORWARD, 0, 0);
	CK(r == -ERANGE && !p.reads && !p.writes, "idx 0x30 r=%d", r);
	memset(&p, 0, sizeof(p));
	r = yt921x_stock_rma_ctrl_set(&p, 0xff, YT921X_RMA_ACT_FORWARD, 0, 0);
	CK(r == -ERANGE && !p.reads && !p.writes, "idx 0xff r=%d", r);
	memset(&p, 0, sizeof(p)); p.read_val = 0x1abc; p.read_err = -EIO;
	r = yt921x_stock_rma_ctrl_set(&p, 0, YT921X_RMA_ACT_COPY_TO_CPU, 0, 0);
	CK(r == -EIO && p.reads == 1 && !p.writes, "read err r=%d", r);
	memset(&p, 0, sizeof(p)); p.read_val = 0x1abc; p.write_err = -EIO;
	r = yt921x_stock_rma_ctrl_set(&p, 0, YT921X_RMA_ACT_COPY_TO_CPU, 0, 0);
	CK(r == -EIO && p.reads == 1 && p.writes == 1, "write err r=%d", r);
}

int main(void)
{
	test_actions();
	test_wrapper();
	if (fails) { printf("%d failures\n", fails); return 1; }
	printf("PASS actions+wrappers\n");
	return 0;
}
"""


def extract_func(text, name):
    m = re.search(r"static\s+int\s+" + re.escape(name) + r"\s*\([^;{]*\)\s*\{", text)
    if not m:
        sys.exit(f"missing {name}")
    depth = 0
    for pos in range(m.end() - 1, len(text)):
        depth += text[pos] == "{"
        depth -= text[pos] == "}"
        if depth == 0:
            return text[m.start():pos + 1]
    sys.exit(f"unterminated {name}")


def extract_macros(text):
    lines = [ln for ln in text.splitlines()
             if ln.startswith("#define") and "YT921X_RMA_CTRL" in ln]
    assert any("YT921X_RMA_CTRL_CPU_CODE_M" in ln for ln in lines), "no CPU code"
    assert not any("FWD_MASK" in ln for ln in lines), "stale FWD_MASK"
    return "\n".join(lines)


def main():
    macros = extract_macros(HEADER.read_text())
    with tempfile.TemporaryDirectory(prefix="yt921x-rma-") as td:
        tmp = pathlib.Path(td)
        for name, path in SOURCES.items():
            text = path.read_text()
            assert name != "debugfs" or ("fwd_mask=" not in text and
                                         "cpu_code=" in text), "readout name"
            funcs = "\n\n".join(extract_func(text, fn) for fn in FN)
            src, exe = tmp / f"{name}.c", tmp / name
            env = PRELUDE + "\n" + macros + "\n\n" + funcs + "\n" + EPILOGUE
            src.write_text(env)
            subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                            "-O0", str(src), "-o", str(exe)], check=True)
            run = subprocess.run([str(exe)], text=True, capture_output=True)
            sys.stdout.write(run.stdout); sys.stderr.write(run.stderr)
            if run.returncode:
                sys.exit(f"{name}: FAILED")
            print(f"{name}: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
