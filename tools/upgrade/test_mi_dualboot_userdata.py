#!/usr/bin/env python3
"""Exercise the production Xiaomi upgrade hook with isolated files and UBI fakes."""
from pathlib import Path
import json
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'target/linux/qualcommax/ipq50xx/base-files/lib/upgrade/mi_dualboot.sh'
TOOL = r'''#!/usr/bin/python3
import json,os,pathlib,subprocess,sys
name=pathlib.Path(sys.argv[0]).name;args=sys.argv[1:];base=pathlib.Path(os.environ['FIXTURE'])
with (base/'trace').open('a') as out:out.write(name+' '+ ' '.join(args)+'\n')
fail=os.environ.get('FAIL','')
if fail==name or fail=='remount' and name=='mount' and args[:2]==['-o','remount,rw']:sys.exit(1)
if name=='mount':
 if args[:2]==['-t','ubifs']:
  mp=pathlib.Path(args[-1]);mp.rmdir();mp.symlink_to(base/'volume',target_is_directory=True)
elif name=='umount':
 mp=pathlib.Path(args[-1]);mp.unlink();mp.mkdir()
elif name=='ubiattach':(base/'attached').touch()
elif name=='ubimkvol':(base/'has_volume').touch()
elif name in ['cp','mv','ln','rm','rmdir']:
 # Never allow a test double to touch device nodes or paths outside its fixture.
 for arg in args:
  if arg.startswith('/') and not arg.startswith(str(base)+'/'):raise RuntimeError(arg)
 sys.exit(subprocess.run(['/usr/bin/'+name,*args]).returncode)
elif name=='sync':pass
else:raise RuntimeError(name)
'''
PRELUDE = r'''
record() { printf '%s\n' "$*" >> "$FIXTURE/trace"; }
board_name() { printf '%s\n' "$BOARD"; }
rootfs_type() { printf '%s\n' "$ROOT_TYPE"; }
v() { record "message $*"; }
find_mtd_index() {
 case "$1" in overlay) [ "$HAS_OVERLAY" = 1 ] && echo 20;; rootfs) echo 18;; rootfs_1) echo 19;; esac
}
nand_find_ubi() {
 case "$1" in
 overlay) [ -f "$FIXTURE/attached" ] && echo ubi1;;
 rootfs|rootfs_1) echo ubi2;;
 esac
}
nand_find_volume() {
 case "$2" in
 userdata) [ -f "$FIXTURE/has_volume" ] && echo ubi1_0;;
 kernel) [ "$FAIL" != kernel ] && echo ubi2_0;;
 esac
}
fw_printenv() { record "read-env $*"; return 0; }
fw_setenv() { record "set-env $*"; return 0; }
identify_if_gzip() { return 0; }
nand_do_flash_file() { record "flash $CI_UBIPART"; [ "$FAIL" != flash ]; }
nand_restore_config() { record "legacy-restore $*"; [ "$FAIL" != legacy-restore ]; }
'''

def case(**options):
    env_values = dict(BOARD='xiaomi,cr881x', ROOT_TYPE='tmpfs', HAS_OVERLAY='1', FAIL='')
    env_values.update(options)
    with tempfile.TemporaryDirectory(prefix='mi-userdata-test-') as directory:
        base = Path(directory)
        (base/'bin').mkdir(); volume=base/'volume'; volume.mkdir(); (volume/'upper').mkdir()
        (volume/'upper/keep-or-drop').write_text('old overlay library/config sentinel')
        (volume/'sysupgrade.tgz').write_text('stale root archive')
        (volume/'upper/sysupgrade.tgz').write_text('stale upper archive')
        (volume/'.fs_state').symlink_to('2')
        (base/'trace').write_text('')
        if options.get('ATTACHED','1')=='1':(base/'attached').touch()
        if options.get('VOLUME','1')=='1':(base/'has_volume').touch()
        backup=base/'backup.tgz';backup.write_text('selected configuration backup')
        if options.get('MISSING_BACKUP')=='1':backup.unlink()
        for name in ['mount','umount','ubiattach','ubimkvol','cp','mv','ln','rm','rmdir','sync']:
            p=base/'bin'/name;p.write_text(TOOL);p.chmod(0o755)
        text=SOURCE.read_text();assert text.startswith('. /lib/functions.sh\n')
        # All storage/boot logic is production code. Only host fixture paths and
        # the generic functions import are substituted; no real devices used.
        text=text.replace('. /lib/functions.sh\n',PRELUDE,1)
        assert text.count('/tmp/mi-userdata')==1
        text=text.replace('/tmp/mi-userdata',str(base/'mount'))
        text=text.replace('/proc/cmdline',str(base/'cmdline'))
        (base/'cmdline').write_text('ubi.mtd='+options.get('SLOT','rootfs')+' rootwait\n')
        p=base/'hook.sh';p.write_text(text+'\n'+options.get('CALL','mi_dualboot_do_upgrade test-image')+'\n')
        env=dict(os.environ,**env_values,FIXTURE=str(base),PATH=str(base/'bin')+':'+os.environ['PATH'],UPGRADE_BACKUP=str(backup) if options.get('KEEP','0')=='1' else '')
        shell=['busybox','ash'] if shutil.which('busybox') else ['bash']
        result=subprocess.run([*shell,str(p)],env=env,capture_output=True,text=True)
        trace=(base/'trace').read_text();events=trace.splitlines()
        if options.get('EXPECT_FAIL')=='1':
            assert result.returncode!=0,(options,result.stdout,result.stderr,trace)
            assert not any(e.startswith('set-env') for e in events),trace
        else:
            assert result.returncode==0,(options,result.stdout,result.stderr,trace)
        shared=env_values['BOARD'] in ['xiaomi,cr881x','redmi,ax3000'] and env_values['HAS_OVERLAY']=='1'
        if result.returncode==0 and shared:
            assert (volume/'.fs_state').readlink()==Path('1')
            assert (volume/'upper/keep-or-drop').read_text()=='old overlay library/config sentinel','Files erased before reboot'
            assert not (volume/'upper/sysupgrade.tgz').exists()
            if options.get('KEEP','0')=='1':assert (volume/'sysupgrade.tgz').read_text()=='selected configuration backup'
            else:assert not (volume/'sysupgrade.tgz').exists()
            assert not any(e.startswith('legacy-restore') for e in events)
            assert events.index(next(e for e in events if e.startswith('flash ')))<events.index(next(e for e in events if e.startswith('mount ')))
            assert events.index(next(e for e in events if e.startswith('umount ')))<events.index(next(e for e in events if e.startswith('set-env ')))
        if not shared:
            assert (volume/'.fs_state').readlink()==Path('2')
            assert not any(e.startswith('mount ') for e in events)
            assert any(e.startswith('legacy-restore') for e in events)==(options.get('KEEP','0')=='1')
        if options.get('FAIL') in ['flash','kernel']:
            assert not any(e.startswith('mount ') for e in events)
        assert not (base/'mount').exists(),('Mount cleanup missing',trace)
        assert all('ubiformat' not in e and 'ubiupdatevol' not in e for e in events)
        if result.returncode==0:
            expected='rootfs_1' if options.get('SLOT','rootfs')=='rootfs' else 'rootfs'
            assert 'flash '+expected in events,trace

for board in ['xiaomi,cr881x','redmi,ax3000']:
    for slot in ['rootfs','rootfs_1']:
        for keep in ['0','1']:case(BOARD=board,SLOT=slot,KEEP=keep)
case(ATTACHED='0',VOLUME='0',KEEP='1')
case(HAS_OVERLAY='0',KEEP='1')
case(BOARD='other,board',KEEP='0')
case(BOARD='other,board',KEEP='1')
for failure in ['flash','kernel','ubiattach','ubimkvol','mount','remount','rm','cp','mv','ln','sync']:
    kwargs=dict(FAIL=failure,EXPECT_FAIL='1',KEEP='1')
    if failure=='ubiattach':kwargs['ATTACHED']='0'
    if failure=='ubimkvol':kwargs['VOLUME']='0'
    case(**kwargs)
case(ROOT_TYPE='overlay',EXPECT_FAIL='1')
case(KEEP='1',MISSING_BACKUP='1',EXPECT_FAIL='1')
case(HAS_OVERLAY='0',KEEP='1',FAIL='legacy-restore',EXPECT_FAIL='1')
platform=(ROOT/'target/linux/qualcommax/ipq50xx/base-files/lib/upgrade/platform.sh').read_text()
assert 'ln readlink' in platform
print('PASS: both slots, keep/reset, lazy next-boot cleanup, exact backup location, layout guards and failure-before-boot-handoff')
