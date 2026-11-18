#!/usr/bin/env python3
"""Compatibility entrypoint; normal feeds update now applies the NSS patches."""
from pathlib import Path
import os
import subprocess

root = Path(__file__).resolve().parents[2]
subprocess.run([str(root / 'scripts/apply-local-feed-patches.sh'), 'nss'],
               env={**os.environ, 'TOPDIR': str(root)}, check=True)
