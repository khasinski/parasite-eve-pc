#!/usr/bin/env python3
"""Exercise the native Android disc picker and original CUE/BIN validator.

Requires a debug APK and imported real images; no save or game state is edited.
"""
import argparse
import json
import re
import subprocess
import time
import xml.etree.ElementTree as ET

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--serial', required=True)
p.add_argument('--adb', default='adb')
p.add_argument('--disc1', default='disc1.cue')
p.add_argument('--disc2')
p.add_argument('--import-folder', help='Optional Download subfolder to import Disc 2 through SAF')
a = p.parse_args()
pkg = 'com.khasinski.parasiteeve'


def adb(*words, check=True):
    return subprocess.run([a.adb, '-s', a.serial, *words], check=check,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30).stdout


def nodes():
    adb('shell', 'uiautomator', 'dump', '/data/local/tmp/pe-disc-request.xml')
    return list(ET.fromstring(adb('shell', 'cat', '/data/local/tmp/pe-disc-request.xml')).iter('node'))


def click(text):
    for node in nodes():
        if node.get('text', '').casefold() == text.casefold():
            x1,y1,x2,y2 = map(int, re.findall(r'\d+', node.get('bounds')))
            adb('shell', 'input', 'tap', str((x1+x2)//2), str((y1+y2)//2))
            return
    raise AssertionError('Missing control: '+text)


def request(identity, filename, expected):
    adb('shell', 'am', 'start', '-S', '-W', '-n', pkg+'/.GameActivity',
        '--esa', 'arguments', '--test-select-disc,'+str(identity))
    current = nodes()
    assert any('Disc '+str(identity)+' required' in n.get('text','') for n in current)
    if filename is None:
        adb('shell', 'input', 'keyevent', '4')
    else:
        if filename == a.disc2 and a.import_folder:
            present = subprocess.run([a.adb,'-s',a.serial,'shell','run-as',pkg,
                'test','-f','files/discs/'+filename]).returncode == 0
            if not present:
                click('Import disc images')
                current = nodes()
                if not any(n.get('text') == a.import_folder for n in current):
                    roots = [n.get('text') for n in current
                             if n.get('resource-id','').endswith('/breadcrumb_text')]
                    if roots: click(roots[0])
                    click('Download')
                    click(a.import_folder)
                click('Use this folder')
                click('Allow')
                deadline = time.monotonic()+180
                while time.monotonic()<deadline:
                    current = nodes()
                    assert not any(n.get('text') == 'Import failed' for n in current)
                    if any(n.get('text','').casefold() == 'choose image' and
                           n.get('enabled') == 'true' for n in current): break
                else: raise AssertionError('Real disc import did not complete')
        click('Choose image')
        # With one imported CUE the launcher returns it directly. With two or
        # more, choose it explicitly from the native Android dialog.
        current = nodes()
        if any(n.get('text') == filename for n in current):
            click(filename)
    deadline = time.monotonic()+45
    while time.monotonic()<deadline:
        if not adb('shell', 'pidof', pkg+':game', check=False).strip():
            break
        time.sleep(.1)
    else:
        raise AssertionError('Native request did not finish')
    log = adb('exec-out','run-as',pkg,'cat','files/diagnostics.log').decode()
    assert 'Assertion failure' not in log and 'FATAL' not in log, log
    events = [json.loads(line) for line in log.splitlines()
              if line.startswith('{"event":"android_disc_selection"')]
    assert events == [dict(event='android_disc_selection',requested=identity,
                           selected=expected[0],identity=expected[1])], events
    print(json.dumps(events[0]), flush=True)


try:
    request(1,a.disc1,(1,1))
    request(2,a.disc1,(-1,1))
    request(2,None,(0,0))
    if a.disc2:
        request(2,a.disc2,(1,2))
        request(1,a.disc2,(-1,2))
    print('PASS: named missing-disc UI, JNI path, original image identity, wrong-disc rejection and cancellation')
finally:
    adb('shell', 'am', 'force-stop', pkg, check=False)
