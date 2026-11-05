#!/usr/bin/env python3
"""Exercise repeated native sessions in a dedicated headless Android emulator.

Requires an installed debug APK and an already imported disc. Checks the actual
framebuffer and native process retirement, including a normally returning CLI.
"""
import argparse
import hashlib
import json
import subprocess
import time
import uuid

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--serial', required=True)
p.add_argument('--adb', default='adb')
p.add_argument('--disc', default='disc1.cue', help='Imported CUE basename')
a = p.parse_args()
if '/' in a.disc or '\\' in a.disc or not a.disc.lower().endswith('.cue'):
    p.error('--disc must be an imported CUE basename')
pkg = 'com.khasinski.parasiteeve'
files = '/data/user/0/' + pkg + '/files/'
owned = []


def adb(*args, check=True):
    return subprocess.run([a.adb, '-s', a.serial, *args], check=check,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30).stdout


def pid(name):
    return adb('shell', 'pidof', name, check=False).strip()


def await_exit(launcher):
    deadline = time.monotonic() + 45
    while time.monotonic() < deadline:
        assert pid(pkg) == launcher, 'Native session killed/restarted the launcher process'
        if not pid(pkg+':game'):
            return
        time.sleep(.1)
    raise AssertionError('Native game process did not retire')


try:
    adb('shell', 'run-as', pkg, 'test', '-f', 'files/discs/' + a.disc)
    adb('shell', 'am', 'start', '-S', '-W', '-f', '0x10008000',
        '-n', pkg+'/.LauncherActivity')
    launcher = pid(pkg)
    assert launcher, 'Launcher did not start'
    previous = set()
    hashes = []
    for index in range(3):
        name = 'lifecycle-'+uuid.uuid4().hex+'.ppm'
        owned.append(name)
        options = ['--disc', files+'discs/'+a.disc, '--headless-gpu',
                   '--frame-limit', '600', '--dump-frame', files+name]
        adb('shell', 'am', 'start', '-W', '-n', pkg+'/.GameActivity',
            '--esa', 'arguments', ','.join(options))
        game = pid(pkg+':game')
        # A very fast run can already have exited after am start. The output
        # and preserved launcher below remain the authoritative completion proof.
        if game:
            assert game != launcher and game not in previous, 'Native process was reused'
            previous.add(game)
        await_exit(launcher)
        frame = adb('exec-out', 'run-as', pkg, 'cat', 'files/'+name)
        assert frame.startswith(b'P6\n'), 'No native RGB framebuffer'
        hashes.append(hashlib.sha256(frame).hexdigest())
        log = adb('exec-out', 'run-as', pkg, 'cat', 'files/diagnostics.log').decode()
        state = next(json.loads(line) for line in log.splitlines()
                     if line.startswith('{"event":"frame_limit"'))
        assert state['vblanks'] == 600
    assert len(set(hashes)) == 1, 'Fresh sessions render different frames at the same native tick'
    # Exercise the player's real Back action as well as diagnostic exits.
    # This uses normal device timing/audio; the emulator itself has no window.
    adb('shell', 'am', 'start', '-W', '-n', pkg+'/.GameActivity', '--es', 'disc', a.disc)
    time.sleep(.5)
    assert pid(pkg+':game'), 'Interactive game did not start'
    adb('shell', 'input', 'keyevent', '4')
    await_exit(launcher)
    top = adb('shell', 'dumpsys', 'activity', 'activities').decode()
    assert any('topResumedActivity' in line and '.LauncherActivity' in line
               for line in top.splitlines()), 'Back did not return to launcher'
    adb('shell', 'am', 'start', '-W', '-n', pkg+'/.GameActivity',
        '--esa', 'arguments', '--help')
    await_exit(launcher)
    assert b'Usage:' in adb('exec-out', 'run-as', pkg, 'cat', 'files/diagnostics.log')
    print('PASS: fresh sessions, identical framebuffers, player Back, normal return and preserved launcher PID')
finally:
    adb('shell', 'am', 'force-stop', pkg, check=False)
    for name in owned:
        adb('shell', 'run-as', pkg, 'rm', '-f', 'files/'+name, check=False)
