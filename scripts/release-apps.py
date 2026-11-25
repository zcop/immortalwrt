#!/usr/bin/env python3
"""Refresh release UI packages and verify the versions included in images."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys


FEED = 'zcoptheme'
PACKAGES = ('luci-app-zcop-dashboard', 'luci-theme-zcop')


def git(feed, *arguments):
    return subprocess.check_output(['git', '-C', str(feed), *arguments], text=True).strip()


def app_state(root):
    feed = root / 'feeds' / FEED
    if git(feed, 'status', '--porcelain'):
        raise ValueError(f'Preserve or commit local changes in {feed} before preparing release apps')
    versions = {}
    for package in PACKAGES:
        recipe = feed / package / 'Makefile'
        fields = dict(re.findall(
            r'^PKG_(VERSION|RELEASE)\s*:?=\s*([A-Za-z0-9.+_~-]+)\s*(?:#.*)?$',
            recipe.read_text(), re.MULTILINE))
        if fields.keys() != {'VERSION', 'RELEASE'}:
            raise ValueError(f'Expected literal PKG_VERSION and PKG_RELEASE in {recipe}')
        versions[package] = f'{fields["VERSION"]}-r{fields["RELEASE"]}'
    return {'feed': FEED, 'revision': git(feed, 'rev-parse', 'HEAD'), 'versions': versions}


def prepare(root, record):
    if not (root / '.config').is_file():
        raise ValueError('Select a device profile and run make defconfig before preparing release apps')
    feed = root / 'feeds' / FEED
    if feed.exists() and git(feed, 'status', '--porcelain'):
        raise ValueError(f'Preserve or commit local changes in {feed} before refreshing it')
    record.unlink(missing_ok=True)
    environment = {**os.environ, 'TOPDIR': str(root)}
    subprocess.run([str(root / 'scripts/feeds'), 'update', FEED], cwd=root, env=environment, check=True)
    subprocess.run([str(root / 'scripts/feeds'), 'install', '-a', '-p', FEED],
                   cwd=root, env=environment, check=True)
    expected = app_state(root)
    subprocess.run(['make', 'defconfig'], cwd=root, env=environment, check=True)
    targets = [f'package/feeds/{FEED}/{package}/clean' for package in PACKAGES]
    subprocess.run(['make', *targets], cwd=root, env=environment, check=True)
    if app_state(root) != expected:
        raise ValueError('The app feed changed during release preparation; prepare it again')
    record.parent.mkdir(parents=True, exist_ok=True)
    temporary = record.with_name(record.name + '.tmp')
    temporary.write_text(json.dumps(expected, indent=2) + '\n')
    temporary.replace(record)
    for package, version in expected['versions'].items():
        print(f'Prepared {package} {version}')


def check(root, record, config, manifests):
    expected = json.loads(record.read_text())
    if app_state(root) != expected:
        raise ValueError('The app feed differs from the release record; prepare it again')
    selected = set(config.read_text().splitlines())
    for manifest in manifests:
        installed = {}
        for line in manifest.read_text().splitlines():
            package, separator, version = line.partition(' - ')
            if separator:
                installed[package] = version
        for package, version in expected['versions'].items():
            required = f'CONFIG_PACKAGE_{package}=y' in selected
            if required or package in installed:
                actual = installed.get(package, 'missing')
                if actual != version:
                    raise ValueError(f'{manifest} contains {package} {actual}, expected {version}')
        print(f'App versions verified in {manifest}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('prepare', 'check'))
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--record', type=Path, required=True)
    parser.add_argument('--config', type=Path)
    parser.add_argument('--manifest', type=Path, nargs='+')
    arguments = parser.parse_args()
    root = arguments.root.resolve()
    if arguments.action == 'check' and not arguments.manifest:
        parser.error('check requires --manifest')
    try:
        if arguments.action == 'prepare':
            prepare(root, arguments.record.resolve())
        else:
            check(root, arguments.record.resolve(), arguments.config or root / '.config', arguments.manifest)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f'Release app check failed — {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
