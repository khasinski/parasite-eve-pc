#!/usr/bin/env python3
"""Exercise repeated native sessions in a dedicated headless Android emulator.

Requires an installed debug APK and an already imported disc. Checks the actual
framebuffer and native process retirement, including a normally returning CLI.
"""
import argparse
import hashlib
import json
import shlex
import subprocess
import sys
import time
import uuid
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--serial', required=True)
p.add_argument('--adb', default='adb')
p.add_argument('--disc', default='disc1.cue', help='Imported CUE basename')
p.add_argument('--native-replay', nargs=argparse.REMAINDER,
               help='Run remaining native test executable arguments on Android')
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


def native_replay(options):
    """Adapter for the existing C replay harness; preserve its assertions."""
    # Accept only finite, prerecorded headless tests. Do not expose arbitrary
    # native commands or interactive control through this batch adapter.
    values = {'--disc', '--frame-limit', '--dump-vram', '--dump-frame',
              '--pad-at', '--internal-resolution'}
    flags = {'--headless', '--headless-gpu'}
    profile = 'native-replay-'+uuid.uuid4().hex
    relative = 'files/diagnostics/'+profile
    absolute = '/data/user/0/'+pkg+'/'+relative
    outputs = {}
    remote = []
    limit = None
    mounted = False
    hidden = False
    index = 0
    while index < len(options):
        option = options[index]
        if option in flags:
            hidden = True
            index += 1
            continue
        if option not in values or index+1 >= len(options):
            raise ValueError('Unsupported replay option: '+option)
        value = options[index+1]
        index += 2
        if option == '--disc':
            if mounted:
                raise ValueError('Duplicate --disc')
            value = files+'discs/'+a.disc
            mounted = True
        elif option == '--frame-limit':
            if limit is not None or not value.isdecimal() or not 0 < int(value) <= 1000000:
                raise ValueError('Expected one finite frame limit, at most 1000000')
            limit = int(value)
        elif option in ('--dump-vram', '--dump-frame'):
            if option in outputs:
                raise ValueError('Duplicate output: '+option)
            outputs[option] = Path(value)
            value = absolute+('/frame.vram' if option == '--dump-vram' else '/frame.ppm')
        if ',' in value or '\n' in value:
            raise ValueError('Intent array values cannot contain commas or newlines')
        remote += [option, value]
    if not mounted or not hidden or limit is None or not outputs:
        raise ValueError('Replay requires --disc, --headless, --frame-limit and a dump output')
    if Path('bu00').exists():
        raise ValueError('Run in a fresh test directory: bu00 already exists')
    remote += ['--headless-gpu', '--test-android-disc-dialogs']
    try:
        adb('shell', 'run-as', pkg, 'test', '-f', shlex.quote('files/discs/'+a.disc))
        adb('shell', 'am', 'start', '-S', '-W', '-f', '0x10008000',
            '-n', pkg+'/.LauncherActivity')
        launcher = pid(pkg)
        assert launcher, 'Launcher did not start'
        adb('shell', 'am', 'start', '-W', '-n', pkg+'/.GameActivity',
            '--es', 'testProfile', profile, '--esa', 'arguments', shlex.quote(','.join(remote)))
        deadline = time.monotonic()+max(60, limit/100)
        while pid(pkg+':game'):
            assert pid(pkg) == launcher, 'Native replay killed the launcher'
            if time.monotonic() >= deadline:
                raise AssertionError('Native replay timed out')
            time.sleep(.25)
        assert pid(pkg) == launcher, 'Native replay killed the launcher'
        log = adb('exec-out', 'run-as', pkg, 'cat', 'files/diagnostics.log').decode()
        states = [json.loads(line) for line in log.splitlines()
                  if line.startswith('{"event":"frame_limit"')]
        assert len(states) == 1 and states[0]['vblanks'] == limit, 'Replay did not reach its frame limit'
        for option, destination in outputs.items():
            name = 'frame.vram' if option == '--dump-vram' else 'frame.ppm'
            payload = adb('exec-out', 'run-as', pkg, 'cat', relative+'/'+name)
            if option == '--dump-vram':
                assert len(payload) == 1024*512*2, 'Incomplete native VRAM'
            else:
                assert payload.startswith(b'P6\n'), 'Missing native framebuffer'
            destination.write_bytes(payload)
        # The desktop C harness checks the original phone-created save here.
        card = relative+'/memory-cards/bu00/BASLUS-006620000000A'
        # exec-out uses the raw protocol: a remote cat error may still return
        # success locally. Check existence through shell's exit-status protocol.
        exists = subprocess.run([a.adb, '-s', a.serial, 'shell', 'run-as', pkg,
                                 'test', '-f', card], capture_output=True, timeout=30)
        if exists.returncode not in (0, 1):
            raise RuntimeError('Could not inspect the replay card: '+exists.stderr.decode())
        result = (subprocess.run([a.adb, '-s', a.serial, 'exec-out', 'run-as',
                                  pkg, 'cat', card], check=True, capture_output=True, timeout=30)
                  if exists.returncode == 0 else None)
        if result is not None:
            assert len(result.stdout) == 8192, 'Incomplete native phone save'
            directory = Path('bu00')
            directory.mkdir(exist_ok=True)
            (directory/'BASLUS-006620000000A').write_bytes(result.stdout)
        print(log, end='')
    finally:
        adb('shell', 'am', 'force-stop', pkg, check=False)
        adb('shell', 'run-as', pkg, 'rm', '-rf', relative, check=False)


if a.native_replay is not None:
    native_replay(a.native_replay)
    sys.exit(0)


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
