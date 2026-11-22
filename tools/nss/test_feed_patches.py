#!/usr/bin/env python3
"""Exercise the real feed-update hook against pristine pinned NSS recipes.

Only package indexing is stubbed. Git patching, revision/conflict checks and
source-patch installation run through the production scripts in a temporary
tree. No configured feed, firmware source or device is modified.
"""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
FEED = ROOT / 'feeds/nss'
PATCHES = ROOT / 'patches/feeds-nss'
RECIPES = ('qca-nss-drv/Makefile', 'qca-nss-drv/Config.in',
           'qca-nss-clients/Makefile', 'qca-nss-ecm/Makefile',
           'qca-mcs/Makefile', 'firmware/nss-firmware/Makefile')


def run(*args, **kwargs):
    return subprocess.run(args, text=True, capture_output=True, **kwargs)


class FeedPatches(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        expected = (PATCHES / 'revision').read_text().strip()
        actual = run('git', '-C', str(FEED), 'rev-parse', 'HEAD', check=True).stdout.strip()
        if actual != expected:
            raise RuntimeError(f'Tests require pristine recipes from feed {expected}, got {actual}')
        cls.original = {name: run('git', '-C', str(FEED), 'show', f'HEAD:{name}',
                                  check=True).stdout for name in RECIPES}

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='nss-feed-patches-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.feed = self.root / 'feeds/nss'
        for name, data in self.original.items():
            path = self.feed / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(data)
        self.env = {**os.environ, 'TOPDIR': str(self.root),
                    'GIT_AUTHOR_NAME': 'Feed test', 'GIT_COMMITTER_NAME': 'Feed test',
                    'GIT_AUTHOR_EMAIL': 'test@localhost', 'GIT_COMMITTER_EMAIL': 'test@localhost'}
        for args in [('init', '-q'), ('add', '.'),
                     ('-c', 'commit.gpgsign=false', 'commit', '-qm', 'Pinned recipe fixture')]:
            run('git', '-C', str(self.feed), *args, env=self.env, check=True)
        revision = run('git', '-C', str(self.feed), 'rev-parse', 'HEAD', check=True).stdout
        shutil.copytree(PATCHES, self.root / 'patches/feeds-nss')
        (self.root / 'patches/feeds-nss/revision').write_text(revision)
        shutil.copytree(ROOT / 'tools/nss/patches/qosmio',
                        self.root / 'tools/nss/patches/qosmio')
        (self.root / 'scripts').mkdir()
        shutil.copy2(ROOT / 'scripts/apply-local-feed-patches.sh', self.root / 'scripts')
        (self.root / 'feeds.conf').write_text(
            'src-git nss https://example.invalid/nss.git^' + revision.strip() + '\n')
        (self.root / 'bin').mkdir()
        make = self.root / 'bin/gmake'
        make.write_text('''#!/usr/bin/env python3
import json, os, sys
from pathlib import Path
if '-v' in sys.argv:
    print('GNU Make 4.4.1')
    sys.exit(0)
root = Path(os.environ['TOPDIR'])
feed = root / 'feeds/nss'
assert 'NOSTDINC_FLAGS=' in (feed / 'qca-nss-drv/Makefile').read_text()
assert 'default NSS_MEM_PROFILE_LOW if' in (feed / 'qca-nss-drv/Config.in').read_text()
assert 'default NSS_FIRMWARE_VERSION_12_2 if TARGET_qualcommax_ipq50xx\\n' in (feed / 'firmware/nss-firmware/Makefile').read_text()
for patch in (root / 'tools/nss/patches/qosmio').glob('*/*.patch'):
    target = feed / patch.parent.name / 'patches' / patch.name
    assert target.is_symlink() and target.read_bytes() == patch.read_bytes()
with (root / 'index-calls').open('a') as f:
    f.write(json.dumps(sys.argv[1:]) + '\\n')
''')
        make.chmod(0o755)
        self.env['PATH'] = str(self.root / 'bin') + os.pathsep + os.environ['PATH']

    def update(self, success=True, index_only=True):
        options = ['-i'] if index_only else []
        result = run(str(ROOT / 'scripts/feeds'), 'update', *options, 'nss', env=self.env)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertFalse((self.root / 'index-calls').exists(), result.stdout + result.stderr)
        return result

    def digest(self):
        return {p: hashlib.sha256((self.feed / p).read_bytes()).hexdigest() for p in RECIPES}

    def test_fresh_update_and_repeat(self):
        unrelated = self.feed / 'private-note'
        unrelated.write_text('preserve unrelated work\n')
        self.update(index_only=False)
        first = self.digest()
        self.update()
        self.assertEqual(self.digest(), first)
        self.assertEqual(unrelated.read_text(), 'preserve unrelated work\n')
        self.assertEqual(len((self.root / 'index-calls').read_text().splitlines()), 6)

    def test_migrate_manual_overlay(self):
        series = (PATCHES / 'series').read_text().splitlines()
        for name in series[:4]:
            run('git', '-C', str(self.feed), 'apply', str(PATCHES / name), check=True)
        for source in (self.root / 'tools/nss/patches/qosmio').glob('*/*.patch'):
            target = self.feed / source.parent.name / 'patches' / source.name
            target.parent.mkdir(exist_ok=True)
            shutil.copy2(source, target)
        self.update()

    def test_migrate_previous_firmware_default(self):
        self.update()
        expected = self.digest()
        recipe = self.feed / 'firmware/nss-firmware/Makefile'
        old_target = ('TARGET_qualcommax_ipq50xx_DEVICE_xiaomi_cr881x_nss || '
                      'TARGET_qualcommax_ipq50xx_DEVICE_redmi_ax3000_nss')
        recipe.write_text(recipe.read_text().replace(
            'default NSS_FIRMWARE_VERSION_12_2 if TARGET_qualcommax_ipq50xx\n',
            f'default NSS_FIRMWARE_VERSION_12_2 if {old_target}\n'))
        self.assertNotEqual(self.digest(), expected)
        self.update(index_only=False)
        self.assertEqual(self.digest(), expected)

    def test_recipe_conflict_stops_index(self):
        path = self.feed / 'qca-nss-drv/Makefile'
        path.write_text(path.read_text().replace('EXTRA_CFLAGS="$(EXTRA_CFLAGS)',
                                                'PRIVATE_FLAGS="$(EXTRA_CFLAGS)'))
        self.assertIn('Patch does not apply cleanly', self.update(False).stderr)
        self.assertIn('PRIVATE_FLAGS=', path.read_text())

    def test_source_conflict_stops_index(self):
        path = self.feed / 'qca-nss-drv/patches/0104-native-dataplane-rtnl-and-remove-clamp.patch'
        path.parent.mkdir()
        path.write_text('private patch edit\n')
        self.assertIn('Conflicting local NSS patch', self.update(False).stderr)
        self.assertEqual(path.read_text(), 'private patch edit\n')

    def test_wrong_revision_stops_index(self):
        (self.root / 'patches/feeds-nss/revision').write_text('0' * 40 + '\n')
        self.assertIn('Unexpected nss feed revision', self.update(False).stderr)

    def test_patch_links_follow_source_and_withdrawal(self):
        self.update()
        source = next((self.root / 'tools/nss/patches/qosmio/qca-nss-drv').glob('*.patch'))
        target = self.feed / 'qca-nss-drv/patches' / source.name
        source.write_text(source.read_text() + '\nLocal patch revision\n')
        self.assertEqual(target.read_bytes(), source.read_bytes())
        recipe = (self.feed / 'qca-nss-drv/Makefile').read_text()
        self.assertIn('PKG_FILE_DEPENDS += $(TOPDIR)/tools/nss/patches/qosmio/qca-nss-drv', recipe)
        source.unlink()
        self.update()
        self.assertFalse(target.is_symlink())

    def test_missing_source_directory_stops_index(self):
        shutil.rmtree(self.root / 'tools/nss/patches/qosmio/qca-nss-drv')
        self.assertIn('Missing repository NSS patches', self.update(False).stderr)


if __name__ == '__main__':
    unittest.main()
