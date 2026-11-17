#!/usr/bin/env python3
"""Exercise real DSA ageing notification and switch aggregation functions.

Requires a prepared kernel tree. Before preparing the new patch, pass --patch
with 646-net-dsa-update-ageing-time-before-notifying.patch; omit --patch to
exercise the supplied kernel as-is (including the old-code negative control).
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
from test_yt921x_queue_index import extract_function, sanitizer_flags

PREAMBLE = r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>
struct dsa_switch;
struct dsa_port { struct dsa_switch *ds; unsigned int ageing_time; };
struct dsa_notifier_ageing_time_info { unsigned int ageing_time; };
struct dsa_ops { int (*set_ageing_time)(struct dsa_switch *,unsigned int); };
struct dsa_switch { struct dsa_port ports[4]; const struct dsa_ops *ops; unsigned int ageing_time_min,ageing_time_max; };
#define DSA_NOTIFIER_AGEING_TIME 1
#define dsa_switch_for_each_port(dp, ds) \
 for(int i=0;i<4;i++) if (((dp)=&(ds)->ports[i]))
static unsigned long clock_t_to_jiffies(clock_t t) { return t; }
static unsigned int jiffies_to_msecs(unsigned long j) { return j*10; }
static int dsa_port_notify(struct dsa_port *,int,struct dsa_notifier_ageing_time_info *);
static unsigned int hardware, writes;
static int reject;
static int program(struct dsa_switch *ds,unsigned int ms) {
 (void)ds; writes++; if(reject)return -EIO; hardware=ms; return 0;
}
static const struct dsa_ops ops={.set_ageing_time=program};
'''
TESTS = r'''
static int dsa_port_notify(struct dsa_port *p,int event,struct dsa_notifier_ageing_time_info *info) {
 assert(event==DSA_NOTIFIER_AGEING_TIME);
 return dsa_switch_ageing_time(p->ds,info);
}
static void reset(struct dsa_switch *s) {
 *s=(struct dsa_switch){.ops=&ops,.ageing_time_min=5000,.ageing_time_max=327675000};
 for(int i=0;i<4;i++)s->ports[i].ds=s;
 writes=hardware=reject=0;
}
static void set(struct dsa_port *p,unsigned int seconds,unsigned int want) {
 assert(!dsa_port_ageing_time(p,seconds*100));
 assert(hardware==want && p->ageing_time==seconds*1000);
}
int main(void) {
 struct dsa_switch s;
 reset(&s); s.ports[0].ageing_time=10000;
 set(&s.ports[0],300,300000); /* first increase must reach hardware */
 assert(writes==1);
 set(&s.ports[0],16,16000); set(&s.ports[0],300,300000); /* STP recovery */
 reset(&s);
 s.ports[0].ageing_time=s.ports[1].ageing_time=10000;
 s.ports[2].ageing_time=60000; /* another bridge still needs faster ageing */
 set(&s.ports[0],300,10000);
 set(&s.ports[1],300,60000);
 set(&s.ports[2],300,300000); /* final port restores the correct minimum */
 assert(writes==3);
 reject=1;
 assert(dsa_port_ageing_time(&s.ports[0],1000)==-EIO);
 assert(s.ports[0].ageing_time==300000 && hardware==300000);
 reject=0;
 unsigned int oldwrites=writes;
 assert(dsa_port_ageing_time(&s.ports[0],100)==-ERANGE);
 assert(s.ports[0].ageing_time==300000 && writes==oldwrites);
 assert(dsa_port_ageing_time(&s.ports[0],32768000)==-ERANGE);
 assert(s.ports[0].ageing_time==300000 && writes==oldwrites);
 struct dsa_ops no_callback={0}; s.ops=&no_callback;
 assert(!dsa_port_ageing_time(&s.ports[0],1000));
 assert(s.ports[0].ageing_time==10000 && writes==oldwrites);
 puts("PASS: DSA ageing increases, shared minimum and error rollback");
 return 0;
}
'''

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--kernel-dir',type=Path,required=True)
 p.add_argument('--patch',type=Path)
 p.add_argument('--sanitize',nargs='?',const='address,undefined,leak')
 a=p.parse_args();port=(a.kernel_dir/'net/dsa/port.c').read_text();switch=(a.kernel_dir/'net/dsa/switch.c').read_text()
 with tempfile.TemporaryDirectory(prefix='dsa-ageing-') as td:
  td=Path(td)
  if a.patch:
   dest=td/'net/dsa';dest.mkdir(parents=True);(dest/'port.c').write_text(port)
   subprocess.run(['patch','--batch','--fuzz=0','-p1','-d',str(td),'-i',str(a.patch.resolve())],check=True,stdout=subprocess.DEVNULL)
   port=(dest/'port.c').read_text()
  code=PREAMBLE+'\n'+extract_function(switch,'dsa_switch_fastest_ageing_time')+'\n'+extract_function(switch,'dsa_switch_ageing_time')+'\n'+extract_function(port,'dsa_port_ageing_time')+'\n'+TESTS
  c=td/'test.c';exe=td/'test';c.write_text(code)
  flags,env=sanitizer_flags(a.sanitize)
  subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-O0',*flags,str(c),'-o',str(exe)],check=True)
  return subprocess.run([str(exe)],env={**os.environ,**env}).returncode
if __name__=='__main__':raise SystemExit(main())
