#!/usr/bin/env python3
"""Run the actual MTU callbacks against fake registers; no silicon claim."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
DRIVER = ROOT / 'target/linux/generic/files/drivers/net/dsa'
source = Path(os.environ.get('YT921X_MTU_SOURCE', DRIVER / 'yt921x_qos_tc.c')).read_text()
functions = []
for name in ('yt921x_dsa_port_change_mtu', 'yt921x_dsa_port_max_mtu'):
    match = re.search(r'^int\s+' + name + r'\(.*?^}', source, re.M | re.S)
    assert match, name
    functions.append(match.group(0))
names = ('YT921X_MACn_FRAME', 'YT921X_MAC_FRAME_SIZE_M',
         'YT921X_MAC_FRAME_SIZE', 'YT921X_FRAME_SIZE_MAX', 'YT921X_TAG_LEN')
defines = []
for line in (DRIVER / 'yt921x_enhanced.h').read_text().splitlines():
    match = re.match(r'#define\s+(\w+)', line)
    if match and match[1] in names:
        defines.append(line)
assert len(defines) == len(names)
preamble = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
typedef uint32_t u32;
#define ETH_HLEN 14
#define ETH_FCS_LEN 4
#define VLAN_HLEN 4
#define GENMASK(h,l) ((~0U << (l)) & (~0U >> (31-(h))))
#define FIELD_PREP(mask,x) (((u32)(x) << __builtin_ctz(mask)) & (mask))
struct dsa_port { bool cpu; };
struct dsa_switch { struct dsa_port ports[11]; };
struct yt921x_priv { struct dsa_switch ds; int reg_lock;
		      bool tag_8021q; bool qinq; int secondary_cpu_port; };
#define yt921x_to_priv(p) ((struct yt921x_priv *)((char *)(p)-offsetof(struct yt921x_priv,ds)))
#define dsa_to_port(ds,p) (&(ds)->ports[p])
#define dsa_port_is_cpu(dp) ((dp)->cpu)
static bool yt921x_is_secondary_cpu_port(const struct yt921x_priv *priv, int port) {
	return port == priv->secondary_cpu_port;
}
static int lock_depth, error, writes;
static u32 value;
#define mutex_lock(p) (lock_depth++)
#define mutex_unlock(p) (lock_depth--)
'''
registers = r'''
static int yt921x_reg_update_bits(struct yt921x_priv *p,u32 reg,u32 mask,u32 bits) {
 assert(lock_depth==1 && (reg==YT921X_MACn_FRAME(3) || reg==YT921X_MACn_FRAME(4)
			  || reg==YT921X_MACn_FRAME(8)));
 assert(mask==YT921X_MAC_FRAME_SIZE_M); writes++;
 if(error)return error;
 value=(value & ~mask)|(bits & mask);return 0;
}
'''
tests = r'''
static struct yt921x_priv priv;
static void check(bool tag_8021q,bool qinq,int port,bool cpu,int mtu,u32 expected) {
 priv.tag_8021q=tag_8021q;priv.qinq=qinq;priv.secondary_cpu_port=4;
 for (int i=0;i<11;i++) priv.ds.ports[i].cpu=false;
 priv.ds.ports[4].cpu=true;priv.ds.ports[8].cpu=true;priv.ds.ports[port].cpu=cpu;
 assert(expected<=YT921X_FRAME_SIZE_MAX);
 value=0xa5c000a5U;writes=0;
 assert(!yt921x_dsa_port_change_mtu(&priv.ds,port,mtu));
 assert(((value & YT921X_MAC_FRAME_SIZE_M)>>8)==expected);
 assert((value & ~YT921X_MAC_FRAME_SIZE_M)==(0xa5c000a5U & ~YT921X_MAC_FRAME_SIZE_M));
 assert(writes==1 && lock_depth==0);
}
int main(void) {
 /* Classic standard DSA: customer S+C on user and raw CPU4; private tag on CPU8. */
 check(false,false,3,false,1500,1526);check(false,false,4,true,1500,1526);
 check(false,false,8,true,1500,1534);
 check(false,false,3,false,9000,9026);check(false,false,4,true,9000,9026);
 check(false,false,8,true,9000,9034);
 int max_classic=yt921x_dsa_port_max_mtu(&priv.ds,3);assert(max_classic==9182);
 check(false,false,3,false,max_classic,9208);check(false,false,4,true,max_classic,9208);
 check(false,false,8,true,max_classic,9216);
 /* Experimental 802.1Q: unchanged. */
 check(true,false,3,false,1500,1518);check(true,false,4,true,1500,1526);
 check(true,false,8,true,1500,1526);
 check(true,false,3,false,9000,9018);check(true,false,4,true,9000,9026);
 check(true,false,8,true,9000,9026);
 int max_exp=yt921x_dsa_port_max_mtu(&priv.ds,3);assert(max_exp==9190);
 check(true,false,3,false,max_exp,9208);check(true,false,4,true,max_exp,9216);
 check(true,false,8,true,max_exp,9216);
 /* QinQ regression: one customer C-tag on user ports. */
 check(true,true,3,false,1500,1522);check(true,true,4,true,1500,1526);
 check(true,true,8,true,1500,1526);
 check(true,true,3,false,9000,9022);check(true,true,4,true,9000,9026);
 check(true,true,8,true,9000,9026);
 check(true,true,3,false,max_exp,9212);check(true,true,4,true,max_exp,9216);
 check(true,true,8,true,max_exp,9216);
 /* A single classic customer tag fits inside the two-tag allowance. */
 assert(1500+ETH_HLEN+ETH_FCS_LEN+VLAN_HLEN < 1526);
 error=-EIO;writes=0;value=0x12345678;
 assert(yt921x_dsa_port_change_mtu(&priv.ds,3,1500)==-EIO);
 assert(value==0x12345678 && writes==1 && lock_depth==0);
 puts("PASS: classic S+C/CPU8-private, QinQ regression, 802.1Q, jumbo bounds, register preservation and I/O errors");
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='yt921x-mtu-') as temporary:
    directory = Path(temporary)
    cfile = directory / 'mtu.c'
    cfile.write_text(preamble + '\n' + '\n'.join(defines) + '\n' + registers +
                     '\n' + '\n'.join(functions) + '\n' + tests)
    executable = directory / 'mtu-test'
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Wno-unused-parameter',
                    '-fsanitize=address,undefined', str(cfile), '-o', str(executable)], check=True)
    environment = dict(os.environ)
    environment['ASAN_OPTIONS'] = environment.get('ASAN_OPTIONS', '') + ':detect_leaks=0'
    subprocess.run([str(executable)], check=True, env=environment)
