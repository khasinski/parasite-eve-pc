#!/usr/bin/env python3
"""Headless Android SAF import/export regressions; requires an installed debug APK.

Uses only a tiny synthetic CUE/BIN fixture, never a copyrighted disc. Leaves the
app stopped and removes only its uniquely named test files on completion.
"""
import argparse
import io
import re
import subprocess
import sys
import time
import uuid
import xml.etree.ElementTree as ET
import zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--serial', required=True)
parser.add_argument('--adb', default='adb')
parser.add_argument('--save-import-only', action='store_true', help='Test save import and validation through the system picker')
parser.add_argument('--saves-only', action='store_true', help='Test save ZIP export through the system picker')
parser.add_argument('--report-only', action='store_true', help='Test diagnostic ZIP export through the system picker')
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
            if text.casefold() in (node.get('text', '').casefold(), node.get('content-desc', '').casefold()) and (node.get('enabled') == 'true' or node.get('resource-id', '').endswith('/breadcrumb_text')):
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


def report_test():
    token = 'pe-report-test-'+uuid.uuid4().hex
    logs = 'files/.local/state/parasite-eve/logs/'
    fixtures = [f'crash-{token}-{i}.txt' for i in range(7)]
    ignored = token+'.bin'
    output = '/sdcard/Download/'+token+'.zip'
    try:
        adb('shell', 'am', 'force-stop', pkg)
        adb('shell', 'run-as', pkg, 'mkdir', '-p', logs)
        for i, name in enumerate(fixtures+[ignored]):
            subprocess.run([args.adb, '-s', args.serial, 'shell', '-T',
                            'run-as '+pkg+' sh -c "cat > '+logs+name+'"'],
                           input=f'diagnostic fixture {i}\n'.encode(), check=True, timeout=30)
            # Future timestamps keep the seven owned reports ahead of older
            # emulator reports without modifying any existing game file.
            adb('shell', 'run-as', pkg, 'touch', '-t', f'20300101000{i}.00', logs+name)
        adb('shell', 'am', 'start', '-S', '-W', '-f', '0x10008000', '-n', pkg+'/.LauncherActivity')
        launcher = adb('shell', 'pidof', pkg).strip()
        click('Export diagnostic report')
        assert any(n.get('class') == 'android.widget.EditText' for n in nodes()), 'System save picker did not open'
        for _ in range(3):
            adb('shell', 'input', 'keyevent', '4')
            if any(n.get('text','').casefold() == 'export diagnostic report'
                   and n.get('enabled') == 'true' for n in nodes()):
                break
        else:
            raise AssertionError('Cancel did not restore launcher')
        click('Export diagnostic report')
        current = nodes()
        crumbs = [n.get('text') for n in current if n.get('resource-id','').endswith('/breadcrumb_text')]
        if not crumbs or crumbs[-1] != 'Downloads':
            click('Show roots')
            click('Downloads')
        current = nodes()
        field = next(n for n in current if n.get('class') == 'android.widget.EditText')
        click(field.get('text'))
        adb('shell', 'input', 'keyevent', '123')
        adb('shell', 'input', 'keyevent', *(['67'] * (len(field.get('text'))+1)))
        adb('shell', 'input', 'text', token+'.zip')
        click('Save')
        click('OK')
        assert adb('shell', 'pidof', pkg).strip() == launcher, 'Export restarted the launcher'
        payload = adb('exec-out', 'cat', output)
        with zipfile.ZipFile(io.BytesIO(payload)) as report:
            assert report.testzip() is None
            names = report.namelist()
            expected = {'logs/'+name for name in fixtures[2:]}
            assert set(name for name in names if name.startswith('logs/crash-')) == expected, names
            for i in range(2,7):
                assert report.read('logs/'+fixtures[i]) == f'diagnostic fixture {i}\n'.encode()
            assert set(names) <= expected | {'system.txt', 'diagnostics.log', 'logs/parasite-eve-log.txt'}, names
            assert b'Android API' in report.read('system.txt')
        for i, name in enumerate(fixtures+[ignored]):
            assert adb('exec-out', 'run-as', pkg, 'cat', logs+name) == f'diagnostic fixture {i}\n'.encode()
        print('PASS: real document-picker ZIP export, cancellation, five newest reports, exact bytes, whitelist and source preservation')
    finally:
        adb('shell', 'am', 'force-stop', pkg)
        adb('shell', 'run-as', pkg, 'rm', '-f', *(logs+name for name in fixtures+[ignored]))
        adb('shell', 'rm', '-f', output)


def save_import_test():
    import binascii
    raw = bytearray((i * 37 + 11) & 255 for i in range(8192))
    raw[:2] = b'SC'
    raw[0x1C8C:0x1C90] = b'\0' * 4
    raw[0x1C8C:0x1C90] = ((~binascii.crc_hqx(raw, 0xFFFF)) & 0xFFFF).to_bytes(4, 'little')
    raw = bytes(raw)
    paths = ['bu00/BASLUS-006620000000A', 'bu10/BASLUS-006620000001B', 'bu00/BASLUS-006620000000C']
    remote = '/sdcard/Download/pe-import-saves-'+uuid.uuid4().hex+'.zip'
    raw_source = '/sdcard/Download/'+paths[2].split('/')[-1]
    owned, sources = [], [remote]

    def exists(path):
        return subprocess.run([args.adb, '-s', args.serial, 'shell', 'run-as', pkg,
                               'test', '-e', 'files/memory-cards/'+path]).returncode == 0

    def bundle(payloads):
        output = io.BytesIO()
        with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
            for name, data in payloads:
                archive.writestr(name, data)
        return output.getvalue()

    def select(data, expected, source=remote):
        subprocess.run([args.adb, '-s', args.serial, 'shell', '-T', 'cat > '+source],
                       input=data, check=True, timeout=30)
        click('Import saves')
        if not any(n.get('text') == source.split('/')[-1] for n in nodes()):
            click('Show roots')
            click('Downloads')
        click(source.split('/')[-1])
        text = '\n'.join(n.get('text', '') for n in nodes())
        assert expected in text, text
        click('OK')

    try:
        for path in paths:
            assert not exists(path), 'Refusing to replace existing save fixture: '+path
        owned.extend(paths)
        adb('shell', 'am', 'start', '-S', '-W', '-f', '0x10008000', '-n', pkg+'/.LauncherActivity')
        click('Import saves')
        assert any(n.get('package', '').endswith('.documentsui') for n in nodes()), 'System picker did not open'
        for _ in range(4):
            adb('shell', 'input', 'keyevent', '4')
            if any(n.get('text', '').casefold() == 'import saves' and n.get('enabled') == 'true' for n in nodes()):
                break
        else:
            raise AssertionError('Cancel did not restore save import')
        damaged = bytearray(raw)
        damaged[0x123] ^= 1
        select(bundle([(paths[0], raw), (paths[1], damaged)]), 'Save checksum is invalid')
        assert not any(exists(path) for path in paths), 'Partial import after checksum failure'
        print('PASS: invalid game checksum in second entry publishes no saves', flush=True)
        select(bundle([('../'+paths[0], raw)]), 'Import failed')
        print('PASS: traversal rejected', flush=True)
        select(bundle([(paths[0], raw)])[:-12], 'Import failed')
        print('PASS: truncated ZIP rejected', flush=True)
        select(bundle([(path, raw) for path in paths[:2]]), '2 save(s) imported')
        for path in paths[:2]:
            assert adb('exec-out', 'run-as', pkg, 'cat', 'files/memory-cards/'+path) == raw
        print('PASS: real SAF import both cards, exact save bytes', flush=True)
        select(bundle([(paths[2], raw), (paths[0], raw)]), 'Save already exists')
        assert not exists(paths[2]), 'Partial import before a collision'
        for path in paths[:2]:
            assert adb('exec-out', 'run-as', pkg, 'cat', 'files/memory-cards/'+path) == raw
        print('PASS: collision rejects whole selection and preserves both cards', flush=True)
        source_exists = subprocess.run([args.adb, '-s', args.serial, 'shell', 'test', '-e', raw_source]).returncode == 0
        assert not source_exists, 'Refusing to replace existing raw source fixture'
        sources.append(raw_source)
        select(raw, '1 save(s) imported', raw_source)
        assert adb('exec-out', 'run-as', pkg, 'cat', 'files/memory-cards/'+paths[2]) == raw
        assert adb('exec-out', 'cat', raw_source) == raw
        print('PASS: raw save to card1, source preserved, cancellation', flush=True)
    finally:
        adb('shell', 'am', 'force-stop', pkg)
        for path in owned:
            adb('shell', 'run-as', pkg, 'rm', '-f', 'files/memory-cards/'+path)
        adb('shell', 'rm', '-f', *sources)


if args.save_import_only:
    save_import_test()
    sys.exit(0)


def saves_test():
    token = 'pe-save-test-'+uuid.uuid4().hex
    output = '/sdcard/Download/'+token+'.zip'
    cards = 'files/memory-cards/'
    paths = ['bu00/BASLUS-006620000000A', 'bu10/BASLUS-006620000001B', 'bu00/'+token+'.bin']
    data = bytes((i * 37 + 11) & 255 for i in range(8192))
    owned = []
    try:
        adb('shell', 'am', 'force-stop', pkg)
        for path in paths:
            exists = subprocess.run([args.adb, '-s', args.serial, 'shell', 'run-as', pkg,
                                     'test', '-e', cards+path]).returncode == 0
            if exists:
                raise RuntimeError('Refusing to replace existing save fixture: '+path)
            adb('shell', 'run-as', pkg, 'mkdir', '-p', cards+path.split('/')[0])
            subprocess.run([args.adb, '-s', args.serial, 'shell', '-T',
                            'run-as '+pkg+' sh -c "cat > '+cards+path+'"'],
                           input=data, check=True, timeout=30)
            owned.append(path)
        adb('shell', 'am', 'start', '-S', '-W', '-f', '0x10008000', '-n', pkg+'/.LauncherActivity')
        click('Export saves')
        assert any(n.get('class') == 'android.widget.EditText' for n in nodes())
        for _ in range(3):
            adb('shell', 'input', 'keyevent', '4')
            if any(n.get('text','').casefold() == 'export saves' and n.get('enabled') == 'true' for n in nodes()):
                break
        else:
            raise AssertionError('Cancel did not restore save export')
        click('Export saves')
        current = nodes()
        crumbs = [n.get('text') for n in current if n.get('resource-id','').endswith('/breadcrumb_text')]
        if not crumbs or crumbs[-1] != 'Downloads':
            click('Show roots')
            click('Downloads')
        field = next(n for n in nodes() if n.get('class') == 'android.widget.EditText')
        click(field.get('text'))
        adb('shell', 'input', 'keyevent', '123')
        adb('shell', 'input', 'keyevent', *(['67'] * (len(field.get('text'))+1)))
        adb('shell', 'input', 'text', token+'.zip')
        click('Save')
        assert any('Saves exported.' in n.get('text','') for n in nodes()), 'Export did not succeed'
        click('OK')
        with zipfile.ZipFile(io.BytesIO(adb('exec-out', 'cat', output))) as archive:
            assert archive.testzip() is None
            assert set(paths[:2]) <= set(archive.namelist())
            assert all(re.fullmatch(r'bu[01]0/BASLUS-00662000000[01][A-O]', n)
                       for n in archive.namelist()), archive.namelist()
            for path in paths[:2]:
                assert archive.read(path) == data
        for path in owned:
            assert adb('exec-out', 'run-as', pkg, 'cat', cards+path) == data
        print('PASS: SAF save export, both cards, exact 8192-byte payloads, ZIP CRC, cancellation, whitelist and source preservation')
    finally:
        adb('shell', 'am', 'force-stop', pkg)
        for path in owned:
            adb('shell', 'run-as', pkg, 'rm', '-f', cards+path)
        adb('shell', 'rm', '-f', output)


if args.saves_only:
    saves_test()
    sys.exit(0)


if args.report_only:
    report_test()
    sys.exit(0)


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
