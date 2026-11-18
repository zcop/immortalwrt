#!/usr/bin/env python3
"""Run the patched QinQ setup against a fault-injecting host register bus.

This exercises the actual driver code, not the hardware ACL parser or packets.
"""
from pathlib import Path
import argparse
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('source', type=Path, help='patched drivers/net/dsa/qca directory')
args = parser.parse_args()
code = (args.source / 'qca8k-qinq.c').read_text()
code = '\n'.join(line for line in code.splitlines() if not line.startswith('#include'))


def function(source, name):
    match = re.search(r'^(?:static )?(?:int|u16)\s+' + name + r'\(', source, re.M)
    if not match:
        raise ValueError(f'Function not found: {name}')
    opening = source.index('{', match.start())
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


kernel = args.source.resolve().parents[3]
port = (kernel / 'net/dsa/port.c').read_text()
tag = (kernel / 'net/dsa/tag_8021q.c').read_text()
common = (args.source / 'qca8k-common.c').read_text()
macros = re.findall(r'^#define DSA_8021Q_(?:[^\n]*\\\n)*[^\n]*', tag, re.M)
database = '\n'.join(macros) + '\n' + '\n'.join([
    function(tag, 'dsa_tag_8021q_bridge_vid'),
    function(common, 'qca8k_qinq_db_vid'),
    *(function(port, name) for name in ('dsa_port_fdb_add', 'dsa_port_fdb_del',
                                      'dsa_port_mdb_add', 'dsa_port_mdb_del')),
])
with tempfile.TemporaryDirectory(prefix='qca8k-qinq-') as directory:
    out = Path(directory)
    (out / 'driver-setup.h').write_text(code)
    (out / 'driver-database.h').write_text(database)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-Wno-sign-compare',
                    '-fsanitize=undefined', '-fno-sanitize-recover=all',
                    '-I', str(out), str(Path(__file__).with_suffix('.c')),
                    '-o', str(out / 'check')], check=True)
    subprocess.run([str(out / 'check')], check=True)
