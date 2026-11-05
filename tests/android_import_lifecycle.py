#!/usr/bin/env python3
"""Close/reopen the real SAF importer during a failed large-file transaction.

Dedicated headless emulator only. Uses a synthetic 512 MiB BIN without a CUE;
the expected failure must reach the live launcher without crashing its process.
"""
import argparse
import re
import subprocess
import time
import uuid
import xml.etree.ElementTree as ET

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--serial', required=True)
p.add_argument('--adb', default='adb')
a = p.parse_args()
pkg = 'com.khasinski.parasiteeve'
folder = 'pe-import-lifecycle-' + uuid.uuid4().hex
name = folder + '.bin'
source = '/sdcard/Download/' + folder
size = 512 * 1024 * 1024


def adb(*args, check=True):
    return subprocess.run([a.adb, '-s', a.serial, *args], check=check,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30).stdout


def nodes():
    adb('shell', 'uiautomator', 'dump', '/data/local/tmp/pe-import-lifecycle.xml')
    return list(ET.fromstring(adb('shell', 'cat', '/data/local/tmp/pe-import-lifecycle.xml')).iter('node'))


def click(text):
    for node in nodes():
        if node.get('text', '').casefold() == text.casefold():
            x1,y1,x2,y2 = map(int, re.findall(r'\d+', node.get('bounds')))
            adb('shell', 'input', 'tap', str((x1+x2)//2), str((y1+y2)//2))
            return
    raise AssertionError('Missing control: ' + text)


try:
    adb('shell', 'am', 'force-stop', pkg)
    adb('shell', 'mkdir', '-p', source)
    adb('shell', 'truncate', '-s', str(size), source + '/' + name)
    adb('logcat', '-c')
    adb('shell', 'am', 'start', '-W', '-n', pkg+'/.LauncherActivity')
    original_pid = adb('shell', 'pidof', pkg).strip()
    # Use the missing-disc picker: unlike Android's root launcher, Back
    # actually finishes this Activity rather than merely backgrounding it.
    adb('shell', 'am', 'start', '-W', '-n', pkg+'/.GameActivity',
        '--esa', 'arguments', '--test-select-disc,2')
    assert any('Disc 2 required' in n.get('text','') for n in nodes())
    click('Import disc images')
    current = nodes()
    roots = [n.get('text') for n in current if n.get('resource-id', '').endswith('/breadcrumb_text')]
    if roots:
        click(roots[0])
    click('Download')
    click(folder)
    click('Use this folder')
    click('Allow')
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        result = adb('shell', 'run-as', pkg, 'stat', '-c', '%s', 'files/disc-import/'+name, check=False).strip()
        if result.isdigit() and 0 < int(result) < size:
            print('Closing importer while copied bytes =', int(result), flush=True)
            break
    else:
        raise AssertionError('Did not observe an in-flight copy; cannot validate lifecycle')
    adb('shell', 'input', 'keyevent', '4')
    adb('shell', 'am', 'start', '-W', '-n', pkg+'/.LauncherActivity')
    deadline = time.monotonic() + 90
    while time.monotonic() < deadline:
        assert adb('shell', 'pidof', pkg, check=False).strip() == original_pid, 'Importer crashed/restarted launcher'
        current = nodes()
        text = '\n'.join(n.get('text','') for n in current)
        if 'Import failed' in text:
            assert 'No CUE image found' in text, text
            click('OK')
            current = nodes()
            assert any(n.get('text','').casefold() == 'import disc images' and n.get('enabled') == 'true' for n in current)
            break
    else:
        raise AssertionError('Import completion did not reach the replacement launcher')
    logs = adb('logcat', '-d', '-s', 'AndroidRuntime:E').decode(errors='replace')
    assert 'FATAL EXCEPTION' not in logs and 'BadTokenException' not in logs, logs
    print('PASS: Back during import, replacement launcher, failure reporting and retry controls')
finally:
    adb('shell', 'am', 'force-stop', pkg)
    adb('shell', 'rm', '-f', source+'/'+name)
    adb('shell', 'rmdir', source, check=False)
    adb('shell', 'run-as', pkg, 'rm', '-f', 'files/disc-import/'+name)
