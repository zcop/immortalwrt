#!/usr/bin/env python3
"""Exercise actual qca8k MDIO functions with a fault-injecting host bus.

Pass a patched drivers/net/dsa/qca source directory. This checks software
transactions and buffer layout; it does not establish hardware behavior.
"""
from pathlib import Path
import argparse
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('source', type=Path)
args = parser.parse_args()
source = (args.source / 'qca8k-8xxx.c').read_text()
header = (args.source / 'qca8k.h').read_text()


def function(name):
    match = re.search(r'^static\s+(?:int|void)\s+' + name + r'\s*\(', source, re.M)
    if not match:
        raise ValueError('function not found: ' + name)
    start = source.index('{', match.end())
    depth = 0
    for index in range(start, len(source)):
        if source[index] == '{':
            depth += 1
        elif source[index] == '}':
            depth -= 1
            if depth == 0:
                return source[match.start():index + 1] + '\n'
    raise ValueError('unterminated function: ' + name)


macros = ''.join(line + '\n' for line in header.splitlines()
                 if re.match(r'#define\s+QCA8K_MDIO_MASTER_', line))
names = ('qca8k_split_addr', 'qca8k_mii_write_lo', 'qca8k_mii_write_hi',
         'qca8k_mii_read_lo', 'qca8k_mii_read_hi', 'qca8k_mii_read32',
         'qca8k_mii_write32', 'qca8k_set_page', 'qca8k_read_mii',
         'qca8k_write_mii', 'qca8k_regmap_update_bits_mii', 'qca8k_bulk_read',
         'qca8k_bulk_gather_write', 'qca8k_bulk_write', 'qca8k_mdio_write',
         'qca8k_mdio_read')
with tempfile.TemporaryDirectory(prefix='qca8k-mdio-') as directory:
    output = Path(directory)
    (output / 'driver-functions.h').write_text(macros + '\n'.join(map(function, names)))
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-Wno-sign-compare',
                    '-fsanitize=undefined', '-fno-sanitize-recover=all',
                    '-I', str(output), str(Path(__file__).with_suffix('.c')),
                    '-o', str(output / 'check')], check=True)
    subprocess.run([str(output / 'check')], check=True)
