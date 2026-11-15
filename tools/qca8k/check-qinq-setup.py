#!/usr/bin/env python3
"""Run the patched QinQ setup against a fault-injecting host register bus.

This exercises the actual driver code, not the hardware ACL parser or packets.
"""
from pathlib import Path
import argparse
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('source', type=Path, help='patched drivers/net/dsa/qca directory')
args = parser.parse_args()
code = (args.source / 'qca8k-qinq.c').read_text()
code = '\n'.join(line for line in code.splitlines() if not line.startswith('#include'))
with tempfile.TemporaryDirectory(prefix='qca8k-qinq-') as directory:
    out = Path(directory)
    (out / 'driver-setup.h').write_text(code)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-Wno-sign-compare',
                    '-fsanitize=undefined', '-fno-sanitize-recover=all',
                    '-I', str(out), str(Path(__file__).with_suffix('.c')),
                    '-o', str(out / 'check')], check=True)
    subprocess.run([str(out / 'check')], check=True)
