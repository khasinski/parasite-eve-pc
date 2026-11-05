#!/usr/bin/env python3
"""Load an original save while only one disc is mounted, supplying the other via Android UI.

Requires a debug APK, imported images and a user-provided native 8192-byte save.
Uses an isolated diagnostic profile and never replaces the player's memory card.
"""
import argparse
import json
import re
import subprocess
import struct
import time
import uuid
import xml.etree.ElementTree as ET
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--serial', required=True)
p.add_argument('--adb', default='adb')
p.add_argument('--save', type=Path, required=True)
p.add_argument('--start-disc', required=True, help='Imported CUE basename initially mounted')
p.add_argument('--supply-disc', required=True, help='Imported CUE basename selected on request')
p.add_argument('--expected-scene', type=lambda v:int(v,0), help='Defaults to the original save tail scene token')
p.add_argument('--required-identity', type=int, choices=[1,2], required=True)
p.add_argument('--out', type=Path, required=True)
a = p.parse_args()
pkg = 'com.khasinski.parasiteeve'
profile = 'disc-transition-'+uuid.uuid4().hex
relative = 'files/diagnostics/'+profile
absolute = '/data/user/0/'+pkg+'/'+relative
card = a.save.read_bytes()
assert len(card) == 8192
if a.expected_scene is None:
    a.expected_scene = struct.unpack_from('<I', card, 0x1be8)[0]
for name in [a.start_disc,a.supply_disc]:
    assert '/' not in name and '\\' not in name and ',' not in name and name.lower().endswith('.cue')
a.out.mkdir(parents=True,exist_ok=True)


def adb(*words, check=True):
    return subprocess.run([a.adb,'-s',a.serial,*words],check=check,stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE,timeout=30).stdout


def nodes():
    adb('shell','uiautomator','dump','/data/local/tmp/pe-transition.xml')
    return list(ET.fromstring(adb('shell','cat','/data/local/tmp/pe-transition.xml')).iter('node'))


def click(current,text):
    for n in current:
        if n.get('text','').casefold() == text.casefold():
            x,y,r,b=map(int,re.findall(r'\d+',n.get('bounds')))
            adb('shell','input','tap',str((x+r)//2),str((y+b)//2))
            return
    raise AssertionError('Missing button: '+text)


try:
    adb('shell','am','force-stop',pkg)
    adb('shell','run-as',pkg,'mkdir','-p',relative+'/memory-cards/bu00')
    subprocess.run([a.adb,'-s',a.serial,'shell','-T',
        'run-as '+pkg+' sh -c "cat > '+relative+'/memory-cards/bu00/BASLUS-006620000000A"'],
        input=card,check=True,timeout=30)
    options=['--disc','/data/user/0/'+pkg+'/files/discs/'+a.start_disc,
             '--headless-gpu','--test-android-disc-dialogs','--internal-resolution','1',
             '--frame-limit','21000','--dump-frame',absolute+'/frame.ppm']
    events=[(10000,8),(10010,0),(11000,16),(11010,0),(11500,64),(11510,0),
            (11600,16384),(11610,0),(13500,16384),(13510,0),(14520,16384),
            (14530,0),(15540,4096),(15550,0),(15670,16384),(15680,0)]
    for tick,buttons in events:options+=['--pad-at',f'{tick}:{buttons}']
    adb('shell','am','start','-W','-n',pkg+'/.GameActivity','--es','testProfile',profile,
        '--esa','arguments',','.join(options))
    prompt_seen=False
    deadline=time.monotonic()+120
    while time.monotonic()<deadline:
        if not adb('shell','pidof',pkg+':game',check=False).strip():break
        current=nodes()
        texts=[n.get('text','') for n in current]
        if any('Disc '+str(a.required_identity)+' required' in t for t in texts):
            prompt_seen=True
        if any('Select the Parasite Eve (USA) Disc' in t for t in texts) and 'OK' in texts:
            click(current,'OK')
        elif a.supply_disc in texts:
            click(current,a.supply_disc)
        elif any(t.casefold()=='choose image' for t in texts):
            click(current,'Choose image')
    else:raise AssertionError('Original disc transition did not finish')
    log=adb('exec-out','run-as',pkg,'cat','files/diagnostics.log').decode()
    (a.out/'game.log').write_text(log)
    assert 'Assertion failure' not in log and 'FATAL' not in log, log[-1000:]
    state=next(json.loads(line)for line in log.splitlines()if line.startswith('{"event":"frame_limit"'))
    assert prompt_seen,'The save did not exercise the real missing-disc request'
    assert state['scene']==a.expected_scene,hex(state['scene'])
    assert state['vblanks']==21000 and any(actor['player']for actor in state['actors'])
    assert state['combat'].get('player_hp',0)>0
    (a.out/'state.json').write_text(json.dumps(state,indent=2))
    frame=adb('exec-out','run-as',pkg,'cat',relative+'/frame.ppm')
    assert frame.startswith(b'P6\n')
    (a.out/'frame.ppm').write_bytes(frame)
    print('PASS: original save loaded after native missing-disc callback, scene',hex(state['scene']))
finally:
    adb('shell','am','force-stop',pkg,check=False)
    adb('shell','run-as',pkg,'rm','-rf',relative,check=False)
