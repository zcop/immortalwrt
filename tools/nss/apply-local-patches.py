#!/usr/bin/env python3
"""Install this tree's explicit patch overlay on the pinned NSS feed."""
from pathlib import Path
import hashlib
import shutil
import subprocess

root = Path(__file__).resolve().parents[2]
feed = root / 'feeds/nss'
expected = '4fa071ab14de0694c341b0c6f1970ac94aca3e62'
assert subprocess.check_output(['git','-C',str(feed),'rev-parse','HEAD']).decode().strip()==expected, 'Unexpected NSS feed revision'
assert not subprocess.check_output(['git','-C',str(feed),'diff','--name-only']), 'Tracked NSS feed edits exist'
assert not subprocess.check_output(['git','-C',str(feed),'diff','--cached','--name-only']), 'Staged NSS feed edits exist'
source = Path(__file__).resolve().parent / 'patches'
for patch in sorted(source.glob('*.patch')):
    target = feed / 'qca-nss-drv/patches' / patch.name
    if target.exists():
        assert target.read_bytes()==patch.read_bytes(), f'Conflicting existing patch: {target}'
    else:
        shutil.copy2(patch,target)
    print(patch.name,hashlib.sha256(patch.read_bytes()).hexdigest())
