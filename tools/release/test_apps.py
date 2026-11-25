"""Regression checks for app refreshes in reused firmware build trees."""

import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[2] / 'scripts/release-apps.py'


class ReleaseApps(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='release-apps-')
        self.addCleanup(temporary.cleanup)
        self.base = Path(temporary.name)
        self.root = self.base / 'build'
        self.upstream = self.base / 'upstream'
        self.feed = self.root / 'feeds/zcoptheme'
        self.record = self.base / 'apps.json'
        self.environment = {**os.environ, 'GIT_AUTHOR_NAME': 'Release test',
                            'GIT_AUTHOR_EMAIL': 'test@localhost', 'GIT_COMMITTER_NAME': 'Release test',
                            'GIT_COMMITTER_EMAIL': 'test@localhost', 'PYTHONDONTWRITEBYTECODE': '1'}
        self.upstream.mkdir()
        self.command('git', 'init', '-q', str(self.upstream))
        self.write_recipe(self.upstream, 'luci-app-zcop-dashboard', '0.8.3')
        self.write_recipe(self.upstream, 'luci-theme-zcop', '0.1.14')
        self.commit(self.upstream)
        self.feed.parent.mkdir(parents=True)
        self.command('git', 'clone', '-q', str(self.upstream), str(self.feed))
        self.write_recipe(self.upstream, 'luci-app-zcop-dashboard', '0.8.7')
        self.commit(self.upstream)
        (self.root / '.config').write_text(
            'CONFIG_PACKAGE_luci-app-zcop-dashboard=y\nCONFIG_PACKAGE_luci-theme-zcop=y\n')
        (self.root / 'scripts').mkdir()
        self.executable(self.root / 'scripts/feeds', '''#!/usr/bin/env python3
import os, subprocess, sys
from pathlib import Path
root = Path.cwd()
assert os.environ['TOPDIR'] == str(root)
with (root / 'calls').open('a') as output:
    output.write('feeds ' + ' '.join(sys.argv[1:]) + '\\n')
if sys.argv[1] == 'update':
    if os.environ.get('RELEASE_TEST_FAIL_UPDATE'):
        sys.exit(1)
    subprocess.run(['git', '-C', str(root / 'feeds/zcoptheme'), 'pull', '--ff-only'], check=True)
''')
        (self.base / 'bin').mkdir()
        self.executable(self.base / 'bin/make', '''#!/usr/bin/env python3
import sys
from pathlib import Path
with (Path.cwd() / 'calls').open('a') as output:
    output.write('make ' + ' '.join(sys.argv[1:]) + '\\n')
''')
        self.environment['PATH'] = str(self.base / 'bin') + os.pathsep + os.environ['PATH']

    def command(self, *arguments):
        return subprocess.run(arguments, env=self.environment, text=True, capture_output=True, check=True).stdout

    def commit(self, repository):
        self.command('git', '-C', str(repository), 'add', '.')
        self.command('git', '-C', str(repository), '-c', 'commit.gpgsign=false', 'commit', '-qm', 'UI revision')

    def write_recipe(self, repository, package, version):
        directory = repository / package
        directory.mkdir(exist_ok=True)
        (directory / 'Makefile').write_text(f'PKG_VERSION:={version}\nPKG_RELEASE:=1\n')

    def executable(self, path, content):
        path.write_text(content)
        path.chmod(0o755)

    def run_script(self, action, *extra, success=True):
        result = subprocess.run(['python3', '-B', str(SCRIPT), action, '--root', str(self.root),
                                 '--record', str(self.record), *extra], env=self.environment,
                                text=True, capture_output=True)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0)
        return result

    def manifest(self, version='0.8.7-r1'):
        path = self.base / 'firmware.manifest'
        path.write_text(f'luci-app-zcop-dashboard - {version}\nluci-theme-zcop - 0.1.14-r1\n')
        return path

    def test_refreshes_cached_feed_and_cleans_app_packages(self):
        self.run_script('prepare')
        state = json.loads(self.record.read_text())
        self.assertEqual(state['versions']['luci-app-zcop-dashboard'], '0.8.7-r1')
        self.assertEqual(state['revision'], self.command('git', '-C', str(self.upstream), 'rev-parse', 'HEAD').strip())
        calls = (self.root / 'calls').read_text()
        self.assertIn('feeds update zcoptheme', calls)
        self.assertIn('feeds install -a -p zcoptheme', calls)
        self.assertIn('make defconfig', calls)
        self.assertIn('luci-app-zcop-dashboard/clean', calls)
        self.assertIn('luci-theme-zcop/clean', calls)

    def test_current_manifest_passes(self):
        self.run_script('prepare')
        self.run_script('check', '--manifest', str(self.manifest()))

    def test_r5_stale_dashboard_is_rejected(self):
        self.run_script('prepare')
        result = self.run_script('check', '--manifest', str(self.manifest('0.8.3-r1')), success=False)
        self.assertIn('0.8.3-r1, expected 0.8.7-r1', result.stderr)

    def test_missing_selected_dashboard_is_rejected(self):
        self.run_script('prepare')
        path = self.manifest()
        path.write_text('luci-theme-zcop - 0.1.14-r1\n')
        result = self.run_script('check', '--manifest', str(path), success=False)
        self.assertIn('luci-app-zcop-dashboard missing', result.stderr)

    def test_profiles_without_zcop_apps_are_supported(self):
        self.run_script('prepare')
        (self.root / '.config').write_text('# CONFIG_PACKAGE_luci-app-zcop-dashboard is not set\n')
        path = self.manifest()
        path.write_text('kernel - 6.18.60\n')
        self.run_script('check', '--manifest', str(path))

    def test_feed_changes_after_preparation_are_rejected(self):
        self.run_script('prepare')
        self.write_recipe(self.feed, 'luci-app-zcop-dashboard', '0.8.8')
        self.commit(self.feed)
        result = self.run_script('check', '--manifest', str(self.manifest()), success=False)
        self.assertIn('differs from the release record', result.stderr)

    def test_local_app_edits_survive_failed_preparation(self):
        recipe = self.feed / 'luci-app-zcop-dashboard/Makefile'
        recipe.write_text(recipe.read_text() + '# local work\n')
        self.run_script('prepare', success=False)
        self.assertIn('# local work', recipe.read_text())
        self.assertFalse((self.root / 'calls').exists())

    def test_failed_refresh_invalidates_previous_record(self):
        self.run_script('prepare')
        self.environment['RELEASE_TEST_FAIL_UPDATE'] = '1'
        self.run_script('prepare', success=False)
        self.assertFalse(self.record.exists())

    def test_all_image_manifests_are_checked(self):
        self.run_script('prepare')
        current = self.manifest()
        stale = self.base / 'other-device.manifest'
        stale.write_text('luci-app-zcop-dashboard - 0.8.3-r1\n')
        self.run_script('check', '--manifest', str(current), str(stale), success=False)


if __name__ == '__main__':
    unittest.main()
