#!/usr/bin/env python3
"""Apply the explicit core-VLAN and Linux 6.18 build overlay to qosmio.

This does not enable the generic PPE/SSDK VLAN manager on IPQ50xx.
"""
from pathlib import Path
import hashlib
import shutil
import subprocess

root = Path(__file__).resolve().parents[2]
feed = root / 'feeds/nss'
revision = '0d970dbf0185e3f53709bd803e8a466598023c57'
path = 'qca-nss-drv/Config.in'

def git(*args):
    return subprocess.check_output(['git', '-C', str(feed), *args])

assert git('rev-parse', 'HEAD').decode().strip() == revision, 'Unexpected qosmio revision'
assert not git('diff', '--cached', '--name-only').strip(), 'Staged feed changes exist'
changes = {}
path = 'qca-nss-drv/Config.in'
before = git('show', f'HEAD:{path}')
old = (b'config NSS_DRV_VLAN_ENABLE\n\tbool\n\tdefault n\n'
       b'\tprompt "Enable VLAN"\n'
       b'\tdepends on TARGET_qualcommax_ipq807x || TARGET_qualcommax_ipq60xx\n')
new = old.replace(b'TARGET_qualcommax_ipq60xx\n',
                  b'TARGET_qualcommax_ipq60xx || TARGET_qualcommax_ipq50xx\n')
assert before.count(old) == 1, 'Upstream VLAN option changed'
changes[path] = (before, before.replace(old, new, 1))

# EXTRA_CFLAGS no longer reaches ccflags in Linux 6.18. Retain each
# package's intended include paths, feature defines and warning policy.
for package in ('qca-nss-drv', 'qca-nss-clients', 'qca-nss-ecm'):
    path = f'{package}/Makefile'
    before = git('show', f'HEAD:{path}')
    old = b'EXTRA_CFLAGS="$(EXTRA_CFLAGS)'
    new = b'NOSTDINC_FLAGS="$(KERNEL_NOSTDINC_FLAGS) $(EXTRA_CFLAGS)'
    assert before.count(old) == 1, f'Upstream compile flags changed: {path}'
    after = before.replace(old, new, 1)
    # Kernel warnings come after NOSTDINC_FLAGS. Put the package's warning
    # policy last, as EXTRA_CFLAGS did on older Kbuild versions.
    marker = b'\t\tSoC=$(SOC)' if package != 'qca-nss-clients' else b'\t  DTLSMGR_DIR='
    assert after.count(marker) == 1, f'Upstream compile layout changed: {path}'
    indent = b'\t\t' if package != 'qca-nss-clients' else b'\t  '
    after = after.replace(marker, indent + b'CFLAGS_MODULE="$(filter -W%,$(EXTRA_CFLAGS))" \\\n' + marker, 1)
    if package == 'qca-nss-clients':
        # The package source already includes compat.h through ccflags-y,
        # after the kernel's kconfig.h. A NOSTDINC preinclude is too early.
        old_compat = b' $(EXTRA_CFLAGS) -include $(PKG_BUILD_DIR)/compat.h"'
        assert after.count(old_compat) == 1
        after = after.replace(old_compat, b' $(EXTRA_CFLAGS)"', 1)
        old_opts = b'$(strip $(NSS_CLIENTS_MAKE_OPTS))'
        assert after.count(old_opts) == 1
        after = after.replace(old_opts, old_opts + b' NSS_LAG_SUPPORT=$(if $(CONFIG_NSS_DRV_LAG_ENABLE),y,n)', 1)
    if package == 'qca-nss-ecm':
        after = after.replace(b'+PACKAGE_kmod-pppoe:kmod-pptp',
                              b'+NSS_DRV_PPTP_ENABLE:kmod-pptp')
        after = after.replace(b'+PACKAGE_kmod-pppoe:kmod-pppol2tp',
                              b'+NSS_DRV_L2TP_ENABLE:kmod-pppol2tp')
        after = after.replace(b'ECM_INTERFACE_PPTP_ENABLE=y',
                              b'ECM_INTERFACE_PPTP_ENABLE=$(if $(CONFIG_NSS_DRV_PPTP_ENABLE),y,n)')
        after = after.replace(b'ifneq ($(CONFIG_PACKAGE_kmod-pppol2tp),)',
                              b'ifneq ($(CONFIG_NSS_DRV_L2TP_ENABLE),)')
        # The IPQ50xx core exposes no LAG option. Keep host bonding, but
        # only compile ECM bond acceleration with its NSS backend selected.
        after = after.replace(b'ifneq ($(CONFIG_PACKAGE_kmod-bonding),)',
                              b'ifneq ($(CONFIG_NSS_DRV_LAG_ENABLE),)')
        after = after.replace(b'ECM_MAKE_OPTS+=ECM_INTERFACE_BOND_ENABLE=y\nendif',
                              b'ECM_MAKE_OPTS+=ECM_INTERFACE_BOND_ENABLE=y\nelse\nECM_MAKE_OPTS+=ECM_INTERFACE_BOND_ENABLE=n\nendif')
        after = after.replace(b'CONFIG_NSS_DRV_PPPOE_ENABLE\n',
                              b'CONFIG_NSS_DRV_PPPOE_ENABLE \\\n\tCONFIG_NSS_DRV_PPTP_ENABLE \\\n\tCONFIG_NSS_DRV_L2TP_ENABLE \\\n\tCONFIG_NSS_DRV_LAG_ENABLE\n')
    changes[path] = (before, after)

changed = set(git('diff', '--name-only').decode().splitlines())
assert changed <= changes.keys(), f'Unrelated feed changes: {changed}'
# Validate every input before writing any file.
for path, (before, after) in changes.items():
    current = (feed / path).read_bytes()
    assert current in (before, after), f'Conflicting edits: {path}'
for path, (before, after) in changes.items():
    target = feed / path
    if target.read_bytes() != after:
        target.write_bytes(after)
    print(path, hashlib.sha256(after).hexdigest())

# Source API fixes are retained in the main tree rather than only in the
# ignored feed checkout. Refuse a conflicting existing package patch.
for patch in sorted((root / 'tools/nss/patches/qosmio').glob('*/*.patch')):
    target = feed / patch.parent.name / 'patches' / patch.name
    if target.exists():
        assert target.read_bytes() == patch.read_bytes(), f'Conflicting patch: {target}'
    else:
        shutil.copy2(patch, target)
    print(str(patch.relative_to(root)), hashlib.sha256(patch.read_bytes()).hexdigest())
