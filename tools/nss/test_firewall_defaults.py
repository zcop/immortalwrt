#!/usr/bin/env python3
"""Run the shipped first-boot default with an isolated UCI fixture."""
from pathlib import Path
import json
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
script = root / 'package/kernel/qca-nss-qinq/files/99-qca-nss-qinq-firewall'
fake_uci = '''#!/usr/bin/python3
import json,os,sys
p=os.environ['UCI_FIXTURE']
s=json.load(open(p));args=[a for a in sys.argv[1:] if a!='-q']
s['calls'].append(args);code=0
if args==['get','firewall.@defaults[0]']:
 code=0 if s['present'] else 1
elif args[0]=='set':
 key,value=args[1].split('=',1)
 if s.get('fail_set'):code=1
 else:s['pending'][key]=value
elif args==['commit','firewall']:
 if s.get('fail_commit'):code=1
 else:s['saved'].update(s['pending']);s['pending']={}
else:code=2
json.dump(s,open(p,'w'));sys.exit(code)
'''
with tempfile.TemporaryDirectory(prefix='nss-firewall-defaults-') as directory:
    temp = Path(directory)
    (temp / 'uci').write_text(fake_uci)
    (temp / 'uci').chmod(0o755)
    fixture = temp / 'state.json'
    env = dict(os.environ, PATH=str(temp) + ':' + os.environ['PATH'], UCI_FIXTURE=str(fixture))
    subprocess.run(['sh', '-n', str(script)], check=True)

    # Match boot's conditional sourcing, not just standalone shell execution.
    commands = [
        ['sh', str(script)],
        ['sh', '-c', '( . "$1" ) && printf "CONSUMED\\n"', 'boot-default', str(script)],
    ]
    for command in commands:
        for values in [('1', '1'), ('0', '0'), (None, None)]:
            saved = {'unrelated.masq': '1', 'ecm.acceleration_engine': 'auto'}
            for name, value in zip(['flow_offloading', 'flow_offloading_hw'], values):
                if value is not None:
                    saved['firewall.@defaults[0].' + name] = value
            fixture.write_text(json.dumps(dict(present=True, calls=[], pending={}, saved=saved)))
            for _ in range(2):
                result = subprocess.run(command, env=env, capture_output=True, text=True, check=True)
                if command != commands[0]:
                    assert result.stdout == 'CONSUMED\n'
            actual = json.loads(fixture.read_text())
            assert actual['saved'] == dict(saved, **{
                'firewall.@defaults[0].flow_offloading': '0',
                'firewall.@defaults[0].flow_offloading_hw': '0',
            })
            assert not actual['pending']
            assert all(call[0] in ['get', 'set', 'commit'] for call in actual['calls'])

        for present, fail_set, fail_commit, expected in [
            (False, False, False, 0),
            (True, True, False, 1),
            (True, False, True, 1),
        ]:
            state = dict(present=present, fail_set=fail_set, fail_commit=fail_commit,
                         calls=[], pending={}, saved={'existing': 'preserved'})
            fixture.write_text(json.dumps(state))
            result = subprocess.run(command, env=env, capture_output=True, text=True)
            actual = json.loads(fixture.read_text())
            assert result.returncode == expected
            assert actual['saved'] == state['saved']
            if expected:
                assert 'CONSUMED' not in result.stdout
            if not fail_commit:
                assert ['commit', 'firewall'] not in actual['calls']

makefile = (root / 'package/kernel/qca-nss-qinq/Makefile').read_text()
assert 'define KernelPackage/qca-nss-qinq/install' in makefile
assert '$(INSTALL_BIN) ./files/99-qca-nss-qinq-firewall $(1)/etc/uci-defaults/' in makefile
print('PASS: first-boot defaults, conditional sourcing, persistence, idempotence and set/commit failures')
