#!/usr/bin/env python3
"""Offline tests for the actual packet probe; creates no network sockets.

Use --sanitize for ASan/UBSan. Actual socket and switch acceptance are separate.
"""
from pathlib import Path
import argparse
import itertools
import json
import os
import struct
import subprocess
import sys
import tempfile

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sanitize',action='store_true')
cli_options=parser.parse_args()
SOURCE=Path(__file__).with_name('qinq_packet_probe.c')
TEMP=tempfile.TemporaryDirectory(prefix='qinq-probe-test-')
RUN=Path(TEMP.name)
EXE=RUN/'probe'
ENV=dict(os.environ,ASAN_OPTIONS=os.environ.get('ASAN_OPTIONS','')+':detect_leaks=0')
FLAGS=['-std=c11','-O1','-g','-Wall','-Wextra','-Werror']
if cli_options.sanitize:
    FLAGS+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
subprocess.run(['cc',*FLAGS,str(SOURCE),'-o',str(EXE)],check=True)
SRC='02:20:00:00:00:01'
DST='02:20:00:00:00:02'
COOKIE=0x20344567

def expected(ot,st,it,ct,seq,length):
    header=bytes.fromhex(DST.replace(':','')+SRC.replace(':',''))
    header+=struct.pack('!HHHHH4sIIHH',ot,st,it,ct,0x88b5,b'NQ20',COOKIE,seq,length,0)
    return header+bytes((i^COOKIE^seq)&255 for i in range(38,length))

assert expected(0x88a8,0xbdac,0x8100,0x7023,2,256)[:38].hex()=='02200000000202200000000188a8bdac8100702388b54e513230203445670000000201000000'

def options(ot,st,it,ct,seq,length,path):
    return ['--src',SRC,'--dst',DST,'--outer-type',hex(ot),'--outer-tci',str(st),
            '--inner-type',hex(it),'--inner-tci',str(ct),'--cookie',str(COOKIE),
            '--seq',str(seq),'--length',str(length),'--file',str(path)]

def invoke(mode,args,success=True):
    p=subprocess.run([str(EXE),mode,*args],capture_output=True,text=True,env=ENV,timeout=5)
    assert 'Sanitizer' not in p.stderr,p.stderr
    assert (p.returncode==0)==success,(mode,args,p.returncode,p.stdout,p.stderr)
    if success:
        assert json.loads(p.stdout)['ok'] is True

passed=negative=0
with tempfile.TemporaryDirectory(dir=RUN) as temp:
    path=Path(temp)/'frame.bin'
    tcis=(0,1,0x1000,0x6023,0xbdac,0xfffe)
    for n,(ot,it,st,ct,length) in enumerate(itertools.product((0x8100,0x88a8),(0x8100,0x88a8),tcis,tcis,(64,256,1514,9216))):
        seq=n&0xffffffff
        opts=options(ot,st,it,ct,seq,length,path)
        wire=expected(ot,st,it,ct,seq,length)
        invoke('emit',opts)
        assert path.read_bytes()==wire,(ot,it,st,ct,length)
        invoke('check',opts)
        passed+=1
    wire=expected(0x88a8,0xbdac,0x8100,0x7023,2,256)
    opts=options(0x88a8,0xbdac,0x8100,0x7023,2,256,path)
    for pos in (0,6,12,14,16,18,20,22,26,30,34,36,63,255):
        changed=bytearray(wire);changed[pos]^=1;path.write_bytes(changed)
        invoke('check',opts,False);negative+=1
    for changed in (b'',wire[:37],wire[:-1],wire+b'\0'):
        path.write_bytes(changed);invoke('check',opts,False);negative+=1
    for name,value in (('--outer-tci','65536'),('--inner-tci','-1'),('--cookie','4294967296'),('--outer-type','0x1000088a8'),('--count','0'),('--count','257'),('--length','63'),('--length','9217'),('--interval-ms','0'),('--timeout-ms','10001'),('--src','02:20:00:00:00:gg')):
        args=opts[:]
        if name in args:
            args[args.index(name)+1]=value
        else:
            args.extend([name,value])
        invoke('emit',args,False);negative+=1

HARNESS = r'''#define _GNU_SOURCE
#include <assert.h>
#include <sys/stat.h>
static int stale_fstat(int fd, struct stat *s) {
    (void)fd; s->st_mode=S_IFREG|0600; s->st_size=256; return 0;
}
#define fstat stale_fstat
#define main probe_program_main
#include "@PROBE_SOURCE@"
#undef main
#undef fstat
int main(int argc, char **argv) {
    (void)argc;
    union { struct cmsghdr align; unsigned char b[256]; } buf={0};
    struct msghdr m={0}; struct tpacket_auxdata a={0}; bool have;
    assert(read_aux(&m,sizeof buf.b,&a,&have)==0 && !have);
    m.msg_control=buf.b;
    struct cmsghdr *c=(void *)buf.b;
    c->cmsg_level=SOL_PACKET; c->cmsg_type=PACKET_AUXDATA;
    c->cmsg_len=CMSG_LEN(sizeof a);
    struct tpacket_auxdata expected={.tp_status=TP_STATUS_VLAN_VALID|TP_STATUS_VLAN_TPID_VALID,.tp_vlan_tci=0xbdac,.tp_vlan_tpid=0x88a8};
    memcpy(CMSG_DATA(c),&expected,sizeof expected);
    m.msg_controllen=CMSG_SPACE(sizeof a);
    assert(read_aux(&m,sizeof buf.b,&a,&have)==0 && have && !memcmp(&a,&expected,sizeof a));
    m.msg_controllen=CMSG_LEN(sizeof a);
    assert(read_aux(&m,sizeof buf.b,&a,&have)==0 && have && !memcmp(&a,&expected,sizeof a));
    m.msg_flags=MSG_CTRUNC;
    assert(read_aux(&m,sizeof buf.b,&a,&have)==-1 && errno==EMSGSIZE);
    m.msg_flags=0; m.msg_controllen=257;
    assert(read_aux(&m,sizeof buf.b,&a,&have)==-2);
    m.msg_controllen=sizeof(struct cmsghdr)-1;
    assert(read_aux(&m,sizeof buf.b,&a,&have)==-2);
    m.msg_controllen=CMSG_LEN(sizeof a); c->cmsg_len=m.msg_controllen+1;
    assert(read_aux(&m,sizeof buf.b,&a,&have)==-2);
    c->cmsg_len=CMSG_LEN(sizeof a)-1;
    assert(read_aux(&m,sizeof buf.b,&a,&have)==-2);
    c->cmsg_len=CMSG_LEN(0)-1;
    assert(read_aux(&m,sizeof buf.b,&a,&have)==-2);
    m.msg_control=NULL;
    assert(read_aux(&m,sizeof buf.b,&a,&have)==-2);
    struct params p={.src={2,0x20,0,0,0,1},.dst={2,0x20,0,0,0,2},.outer_type=0x88a8,.outer_tci=0xbdac,.inner_type=0x8100,.inner_tci=0x7023,.cookie=123,.length=256,.file=argv[1]};
    uint8_t bytes[257]; build_frame(&p,0,bytes,256);bytes[256]=0;
    int fd=open(p.file,O_WRONLY|O_CREAT|O_TRUNC,0600);assert(fd>=0);assert(write_all(fd,bytes,255)==0);assert(close(fd)==0);
    assert(cmd_check(&p)==1);
    fd=open(p.file,O_WRONLY|O_TRUNC);assert(fd>=0);assert(write_all(fd,bytes,257)==0);assert(close(fd)==0);
    assert(cmd_check(&p)==1);
    puts("metadata and stale-size controls passed: 12");
    return 0;
}
'''
harness=RUN/'metadata.c'
harness.write_text(HARNESS.replace('@PROBE_SOURCE@',SOURCE.as_posix()))
subprocess.run(['cc',*FLAGS,str(harness),'-o',str(RUN/'metadata')],check=True)
p=subprocess.run([str(RUN/'metadata'),str(RUN/'stale.bin')],capture_output=True,text=True,env=ENV,timeout=10)
assert p.returncode==0,(p.stdout,p.stderr)
assert 'metadata and stale-size controls passed: 12' in p.stdout
print(json.dumps(dict(independent_wire_vectors=passed,negative_controls=negative,
                      metadata_and_stale_size_controls=12,sanitized=cli_options.sanitize,
                      all_passed=True),indent=2))
TEMP.cleanup()
