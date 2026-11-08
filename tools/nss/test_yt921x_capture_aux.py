#!/usr/bin/env python3
"""Exercise the actual capture helper with stripped VLAN and ancillary vectors."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r'''
#define main capture_cli_main
#include "@SOURCE@"
#undef main
#include <assert.h>

static void ancillary(void)
{
 union { struct cmsghdr align; unsigned char b[CMSG_SPACE(sizeof(struct tpacket_auxdata))]; } ctl = {0};
 struct cmsghdr *cm = (struct cmsghdr *)ctl.b;
 struct tpacket_auxdata expected = {.tp_status=TP_STATUS_VLAN_VALID, .tp_vlan_tci=0};
 struct msghdr mh = {.msg_control=ctl.b, .msg_controllen=CMSG_LEN(sizeof(expected))};
 struct tpacket_auxdata actual; struct vlan_aux va; bool have;
 cm->cmsg_level=SOL_PACKET; cm->cmsg_type=PACKET_AUXDATA; cm->cmsg_len=CMSG_LEN(sizeof(expected));
 memcpy(CMSG_DATA(cm), &expected, sizeof(expected));
 assert(parse_aux(&mh,sizeof(ctl.b),&actual,&have)==0 && have);
 assert(aux_vlan(&actual,have,&va)==0 && va.present && va.tci==0 && va.tpid==0x8100);
 mh.msg_controllen=sizeof(ctl.b);
 assert(parse_aux(&mh,sizeof(ctl.b),&actual,&have)==0 && have);
 mh.msg_flags=MSG_CTRUNC; assert(parse_aux(&mh,sizeof(ctl.b),&actual,&have)<0); mh.msg_flags=0;
 cm->cmsg_len=CMSG_LEN(sizeof(expected))-1; assert(parse_aux(&mh,sizeof(ctl.b),&actual,&have)<0);
 cm->cmsg_len=0; assert(parse_aux(&mh,sizeof(ctl.b),&actual,&have)<0);
 mh.msg_controllen=sizeof(ctl.b)+1; assert(parse_aux(&mh,sizeof(ctl.b),&actual,&have)<0);
 mh.msg_control=NULL; mh.msg_controllen=1; assert(parse_aux(&mh,sizeof(ctl.b),&actual,&have)<0);
 mh.msg_controllen=0; assert(parse_aux(&mh,sizeof(ctl.b),&actual,&have)==0 && !have);
 expected.tp_status=0; expected.tp_vlan_tci=0xbeef;
 assert(aux_vlan(&expected,true,&va)==0 && !va.present);
 expected.tp_status=TP_STATUS_VLAN_VALID|TP_STATUS_VLAN_TPID_VALID; expected.tp_vlan_tpid=0x1234;
 assert(aux_vlan(&expected,true,&va)<0);
}

int main(int argc, char **argv)
{
 const unsigned char mac[12]={2,0,0,0,0,1,2,0,0,0,0,2};
 const unsigned char private_tag[8]={0x99,0x88,0x0f,0xff,0x90,0x80,0,0};
 assert(argc==2); ancillary();
 for (int n=0;n<6;n++) {
  unsigned char packet[80]={0}; size_t len=12; struct timeval tv={123,456};
  struct tpacket_auxdata ax={0}; struct vlan_aux va; char *prefix;
  bool enhanced=n==3 || n==4, inner=n==2 || n==4;
  struct ctx *c=calloc(1,sizeof(*c)); assert(c);
  memcpy(packet,mac,12);
  if(enhanced) {memcpy(packet+len,private_tag,8);len+=8;}
  if(inner) {const unsigned char tag[4]={0x81,0,0x70,0x23};memcpy(packet+len,tag,4);len+=4;}
  packet[len++]=8;packet[len++]=6;len+=28;
  ax.tp_vlan_tci=n==0?0xbeef:(n==1 || n==5?0:0xbdac);
  ax.tp_status=n==0?0:TP_STATUS_VLAN_VALID;
  if(n>=2) {ax.tp_status|=TP_STATUS_VLAN_TPID_VALID;ax.tp_vlan_tpid=0x88a8;}
  assert(aux_vlan(&ax,true,&va)==0);
  assert(asprintf(&prefix,"%s/case%d",argv[1],n)>0);
  c->o.mode=enhanced?"enhanced":(n==2?"qinq":"raw");c->o.prefix=prefix;c->enhanced=enhanced;
  assert(outputs_open(c)==0);
  assert(process(c,packet,len,(uint32_t)len,tv,"rx",&va)==0 && c->matched==1 && !c->malformed);
  assert(outputs_close(c)==0);free(prefix);free(c);
 }
 puts("PASS: six exact-frame AUXDATA cases and ancillary rejection controls");
}
'''


def record(path):
    b = path.read_bytes()
    assert b[:4] == bytes.fromhex('d4c3b2a1')
    dlt = struct.unpack_from('<I', b, 20)[0]
    sec, usec, cap, original = struct.unpack_from('<IIII', b, 24)
    assert len(b) == 40 + cap and cap == original
    assert (sec, usec) == (123, 456)
    return dlt, b[40:]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--pcap-source', type=Path, required=True)
    p.add_argument('--pcap-build', type=Path, required=True)
    p.add_argument('--sanitize', action='store_true')
    args = p.parse_args()
    source = ROOT/'tools/nss/yt921x_capture.c'
    with tempfile.TemporaryDirectory(prefix='yt921x-aux-') as directory:
        t = Path(directory)
        c = t/'aux.c'
        c.write_text(HARNESS.replace('@SOURCE@', str(source)))
        exe = t/'aux'
        flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if args.sanitize else []
        subprocess.run(['cc', '-std=c11', '-g', '-O1', '-Wall', '-Wextra', '-Werror', *flags,
                        '-I', str(args.pcap_source), str(c), '-L', str(args.pcap_build),
                        '-Wl,-rpath,'+str(args.pcap_build.resolve()), '-lpcap', '-o', str(exe)], check=True)
        result = subprocess.run([str(exe), str(t)], capture_output=True, text=True)
        assert not result.returncode, (result.stdout, result.stderr)
        assert not any(x in result.stderr for x in ('Sanitizer', 'runtime error:')), result.stderr
        mac = bytes.fromhex('020000000001020000000002')
        private = bytes.fromhex('99880fff90800000')
        payload = bytes.fromhex('0806') + bytes(28)
        ctag = bytes.fromhex('81007023')
        for n in range(6):
            enhanced, inner = n in (3, 4), n in (2, 4)
            stag = b'' if n == 0 else (bytes.fromhex('81000000') if n == 1 else
                    bytes.fromhex('88a80000') if n == 5 else bytes.fromhex('88a8bdac'))
            norm = mac + stag + (ctag if inner else b'') + payload
            raw = mac + (private if enhanced else b'') + norm[12:]
            assert record(t/f'case{n}.raw.pcap') == (303 if enhanced else 1, raw)
            assert record(t/f'case{n}.pcap') == (1, norm)
            meta = json.loads((t/f'case{n}.jsonl').read_text())
            assert meta['aux_present'] == (n != 0)
            assert meta['captured_len'] == len(raw) and meta['normalized_len'] == len(norm)
            if enhanced:
                assert meta['tag'] == private.hex() and meta['rx_valid'] and meta['rx_port'] == 2
        print(result.stdout.strip())


if __name__ == '__main__':
    main()
