#!/usr/bin/env python3
"""Headless Android SAF import regression; requires an installed debug APK.

Uses only a tiny synthetic CUE/BIN fixture, never a copyrighted disc. Leaves the
app stopped and removes only its uniquely named test files on completion.
"""
import argparse
import re
import subprocess
import time
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--serial', required=True)
parser.add_argument('--adb', default='adb')
args = parser.parse_args()
pkg = 'com.khasinski.parasiteeve'
folder = 'pe-saf-regression'
basename = 'pe-saf-regression-fixture'


def adb(*words):
    return subprocess.check_output([args.adb, '-s', args.serial, *words], timeout=30)


def nodes():
    adb('shell', 'uiautomator', 'dump', '/data/local/tmp/pe-saf-test.xml')
    return list(ET.fromstring(adb('shell', 'cat', '/data/local/tmp/pe-saf-test.xml')).iter('node'))


def click(text):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        for node in nodes():
            if node.get('text', '').casefold() == text.casefold() and (node.get('enabled') == 'true' or node.get('resource-id', '').endswith('/breadcrumb_text')):
                x1, y1, x2, y2 = map(int, re.findall(r'\d+', node.get('bounds')))
                adb('shell', 'input', 'tap', str((x1+x2)//2), str((y1+y2)//2))
                return
    raise AssertionError('UI control missing: ' + text)


def contents(name):
    return adb('exec-out', 'run-as', pkg, 'cat', 'files/discs/' + name)


def pick():
    click('Import disc images')
    # The document picker remembers its last folder. Navigate back to the
    # storage root through its breadcrumb when needed.
    current = nodes()
    if not any(n.get('text') == folder for n in current):
        roots = [n.get('text') for n in current if n.get('resource-id', '').endswith('/breadcrumb_text')]
        if roots:
            click(roots[0])
        click('Download')
        click(folder)
    click('Use this folder')
    click('Allow')


cue = ('FILE "' + basename + '.bin" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n').encode()
binary = b'Synthetic Android import regression, not a game disc.\n'
paths = [basename + '.cue', basename + '.bin']
owned = False
try:
    adb('shell', 'am', 'force-stop', pkg)
    # Refuse to remove pre-existing data even with our test-specific name.
    for name in paths:
        exists = subprocess.run([args.adb, '-s', args.serial, 'shell', 'run-as', pkg,
                                 'test', '-e', 'files/discs/' + name]).returncode == 0
        if exists:
            raise RuntimeError('Remove stale test fixture manually before running: ' + name)
    owned = True
    adb('shell', 'mkdir', '-p', '/sdcard/Download/' + folder)
    for name, data in zip(paths, [cue, binary]):
        subprocess.run([args.adb, '-s', args.serial, 'shell', '-T',
                        'cat > /sdcard/Download/' + folder + '/' + name],
                       input=data, check=True, timeout=30)
    adb('shell', 'am', 'start', '-S', '-W', '-f', '0x10008000', '-n', pkg + '/.LauncherActivity')
    pick()
    click('Play')
    # The chooser proves the newly imported CUE has reached launcher state.
    assert any(n.get('text') == paths[0] for n in nodes()), 'Imported CUE absent from chooser'
    adb('shell', 'input', 'keyevent', '4')
    assert contents(paths[0]) == cue
    assert contents(paths[1]) == binary
    pick()
    text = '\n'.join(n.get('text', '') for n in nodes())
    assert 'Import failed' in text and 'already imported' in text, text
    assert contents(paths[0]) == cue
    assert contents(paths[1]) == binary
    print('PASS: SAF copy, CUE discovery, duplicate rejection and byte preservation')
finally:
    adb('shell', 'am', 'force-stop', pkg)
    for name in paths:
        # Cleanup is safe only for fixtures created by this run.
        if owned:
            adb('shell', 'run-as', pkg, 'rm', '-f', 'files/discs/' + name)
    adb('shell', 'rm', '-f', *('/sdcard/Download/' + folder + '/' + name for name in paths))
