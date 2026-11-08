#!/usr/bin/env python3
"""Run the actual stmmac MTU handler against reopen and error-path fixtures."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
PATCH = ROOT/'target/linux/generic/pending-6.18/823-net-stmmac-use-new-mtu-when-reopening-mac.patch'
STUB = r'''
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#define ETH_DATA_LEN 1500
#define BUF_SIZE_16KiB 16384
#define STMMAC_ALIGN(x) (((x)+15)&~15)
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,y) ((x)=(y))
#define netdev_dbg(...) ((void)0)
#define netdev_err(...) ((void)0)
struct net_device { int mtu, running; void *priv; };
struct platform { int tx_fifo_size, tx_queues_to_use; };
struct stmmac_priv { struct platform *plat; struct { int tx_fifo_size; } dma_cap; struct net_device *dev; };
struct stmmac_dma_conf { int unused; };
static struct stmmac_dma_conf conf;
static struct platform plat;
static struct stmmac_priv priv;
static struct net_device dev;
static int xdp, alloc_err, open_err, seen_setup, seen_open, opens, releases, frees, desc_frees, modes, features;
#define netdev_priv(d) ((struct stmmac_priv *)(d)->priv)
#define IS_ERR(p) ((p)==(void *)-ENOMEM)
#define PTR_ERR(p) (-ENOMEM)
static bool netif_running(struct net_device *d) { return d->running; }
static bool stmmac_xdp_is_enabled(struct stmmac_priv *p) { (void)p; return xdp; }
static struct stmmac_dma_conf *stmmac_setup_dma_desc(struct stmmac_priv *p,int mtu) { (void)p; seen_setup=mtu; return alloc_err?(void *)-ENOMEM:&conf; }
static void __stmmac_release(struct net_device *d) { (void)d; releases++; }
static int __stmmac_open(struct net_device *d,struct stmmac_dma_conf *c) { assert(c==&conf); opens++; seen_open=d->mtu; return open_err; }
static void free_dma_desc_resources(struct stmmac_priv *p,struct stmmac_dma_conf *c) { (void)p; assert(c==&conf); desc_frees++; }
static void kfree(void *c) { assert(c==&conf); frees++; }
static void stmmac_set_rx_mode(struct net_device *d) { assert(d==&dev); modes++; }
static void netdev_update_features(struct net_device *d) { assert(d==&dev); features++; }
'''
TEST = r'''
static void reset(void) {
 plat=(struct platform){.tx_fifo_size=32768,.tx_queues_to_use=1};
 dev=(struct net_device){.mtu=1508,.running=1,.priv=&priv};
 priv=(struct stmmac_priv){.plat=&plat,.dev=&dev};
 xdp=alloc_err=open_err=opens=releases=frees=desc_frees=modes=features=0;
 seen_setup=seen_open=-1;
}
int main(void) {
 reset(); xdp=1; assert(stmmac_change_mtu(&dev,9000)==-EINVAL); assert(dev.mtu==1508 && seen_setup==-1 && !opens);
 reset(); plat.tx_fifo_size=2048; assert(stmmac_change_mtu(&dev,9000)==-EINVAL); assert(dev.mtu==1508 && seen_setup==-1 && !opens);
 reset(); alloc_err=1; assert(stmmac_change_mtu(&dev,9000)==-ENOMEM); assert(dev.mtu==1508 && !releases && !opens && !frees);
 reset(); open_err=-EIO; assert(stmmac_change_mtu(&dev,9000)==-EIO); assert(dev.mtu==1508 && releases==1 && desc_frees==1 && frees==1 && !features);
 reset(); assert(stmmac_change_mtu(&dev,9000)==0); assert(dev.mtu==9000 && seen_setup==9000 && opens==1 && modes==1 && frees==1 && !desc_frees && features==1);
#ifdef EXPECT_STALE
 assert(seen_open==1508);
#else
 assert(seen_open==9000);
#endif
 reset(); dev.mtu=9000; assert(stmmac_change_mtu(&dev,1508)==0); assert(dev.mtu==1508 && seen_setup==1508 && releases==1 && opens==1);
#ifdef EXPECT_STALE
 assert(seen_open==9000);
#else
 assert(seen_open==1508);
#endif
 reset(); dev.running=0; assert(stmmac_change_mtu(&dev,9000)==0); assert(dev.mtu==9000 && seen_setup==-1 && !opens && !releases && features==1);
 puts("PASS: seven actual-handler MTU ordering and failure scenarios");
}
'''

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,required=True,help='stmmac_main.c before this patch')
    source=p.parse_args().source.read_text()
    with tempfile.TemporaryDirectory(prefix='stmmac-mtu-') as directory:
        tmp=Path(directory);f=tmp/'drivers/net/ethernet/stmicro/stmmac/stmmac_main.c'
        f.parent.mkdir(parents=True);f.write_text(source)
        subprocess.run(['patch','--batch','--forward','--fuzz=0','-p1','-i',str(PATCH)],cwd=tmp,check=True,capture_output=True)
        for name,s in [('baseline',source),('patched',f.read_text())]:
            m=re.search(r'^static int stmmac_change_mtu\([^\n]*\)\n\{.*?^\}',s,re.M|re.S)
            assert m,'MTU handler not found'
            c=tmp/(name+'.c');c.write_text(STUB+m.group()+TEST);exe=tmp/name
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror']+(['-DEXPECT_STALE'] if name=='baseline' else [])+[str(c),'-o',str(exe)],check=True)
            r=subprocess.run([str(exe)],capture_output=True,text=True,check=True)
            print(name+': '+r.stdout.strip())
    print('Baseline exposes stale MAC MTU; patched handler exposes requested MTU and restores failures.')
if __name__=='__main__':
    main()
