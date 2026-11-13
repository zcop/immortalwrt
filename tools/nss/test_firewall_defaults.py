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
elif args==['commit','firewall']:s['saved'].update(s['pending']);s['pending']={}
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

    for values in [('1', '1'), ('0', '0'), (None, None)]:
        saved = {'unrelated.masq': '1', 'ecm.acceleration_engine': 'auto'}
        for name, value in zip(['flow_offloading', 'flow_offloading_hw'], values):
            if value is not None:
                saved['firewall.@defaults[0].' + name] = value
        fixture.write_text(json.dumps(dict(present=True, calls=[], pending={}, saved=saved)))
        for _ in range(2):
            subprocess.run(['sh', str(script)], env=env, check=True)
        actual = json.loads(fixture.read_text())
        assert actual['saved'] == dict(saved, **{
            'firewall.@defaults[0].flow_offloading': '0',
            'firewall.@defaults[0].flow_offloading_hw': '0',
        })
        assert not actual['pending']
        assert all(call[0] in ['get', 'set', 'commit'] for call in actual['calls'])

    for present, fail_set, expected in [(False, False, 0), (True, True, 1)]:
        state = dict(present=present, fail_set=fail_set, calls=[], pending={}, saved={})
        fixture.write_text(json.dumps(state))
        result = subprocess.run(['sh', str(script)], env=env)
        actual = json.loads(fixture.read_text())
        assert result.returncode == expected
        assert not actual['saved']
        assert ['commit', 'firewall'] not in actual['calls']

makefile = (root / 'package/kernel/qca-nss-qinq/Makefile').read_text()
assert 'define KernelPackage/qca-nss-qinq/install' in makefile
assert '$(INSTALL_BIN) ./files/99-qca-nss-qinq-firewall $(1)/etc/uci-defaults/' in makefile
print('PASS: NSS-only first-boot firewall defaults, persistence, idempotence, missing config and write failures')
