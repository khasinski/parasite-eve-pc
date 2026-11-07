#!/usr/bin/env python3
"""Replay issue #58 from its user-supplied room snapshot through native Continue.

Retail images and crash reports stay outside the repository. Set PE_TEST_DISC,
PE_TEST_DISC2 and PE_TEST_CRASH58_REPORT; otherwise CTest skips this asset test.
"""
from pathlib import Path
import binascii
import json
import os
import re
import struct
import subprocess
import sys
import tempfile


def restore_snapshot(report):
    marker = '== Save data (save file bytes from 0x0100) =='
    section = report.split(marker, 1)[1].split('== Log', 1)[0]
    # Cosmetic card header: no game textures, icons or other fixture assets.
    card = bytearray(8192)
    card[:4] = b'SC\x11\x01'
    seen = set()
    for line in section.splitlines():
        match = re.fullmatch(r'([0-9A-F]{4}): ((?:[0-9A-F]{2} ?)+)', line.strip())
        if not match:
            continue
        offset = int(match[1], 16)
        data = bytes.fromhex(match[2])
        if offset < 0x100 or offset + len(data) > len(card):
            raise ValueError('Snapshot address outside card file')
        addresses = set(range(offset, offset + len(data)))
        if seen & addresses:
            raise ValueError('Snapshot contains overlapping data')
        seen |= addresses
        card[offset:offset + len(data)] = data
    # The report deliberately omits the original CRC between runtime and mods.
    if set(range(0x100, 8192)) - seen != set(range(0x1c8c, 0x1c90)):
        raise ValueError('Incomplete room snapshot or missing mod tail')
    if struct.unpack_from('<hh', card, 0x15c) != (5, 273):
        raise ValueError('Expected the reported Day 5 / M273 snapshot')
    crc = binascii.crc_hqx(card, 0xffff) ^ 0xffff
    struct.pack_into('<I', card, 0x1c8c, crc)
    return card


def replay(game, evidence, disc1, disc2, report):
    report_text = report.read_text()
    card = restore_snapshot(report_text)
    hit_marker = 'pe_rebalance: first gun hit:'
    reported_hit = next((line.split(hit_marker, 1)[1].strip()
                         for line in report_text.splitlines() if hit_marker in line), None)
    evidence.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='m273-report-', dir=evidence))
    (work/'bu00').mkdir()
    (work/'bu00/BASLUS-006620000000A').write_bytes(card)
    (work/'config').mkdir()
    mods = ('PE Enhancements: quality of life', 'PE Enhancements: rebalance',
            'PE Enhancements: economy', 'PE Enhancements: collection',
            'PE Enhancements: stacked items', 'Warp', 'Quick save', 'Cheats')
    (work/'config/pe_mods.ini').write_text('Manage mods/All mods=1\n' +
        ''.join('Manage mods/'+name+'=1\n' for name in mods) +
        'Manage mods/Language packs=0\nCheats/God mode=1\n')
    # God mode changes Aya's received damage only. Enemy HP, weapon statistics,
    # Rebalance rules and the original battle scripts run unmodified.
    args = [str(game), '--disc', str(disc1), '--disc2', str(disc2),
            '--headless-gpu', '--internal-resolution', '1', '--frame-limit',
            '42000', '--control-at', '12500', '--dump-frame', str(work/'frame.ppm')]
    for tick, buttons in [(10000,8),(10010,0),(11000,16),(11010,0),
                          (11500,64),(11510,0),(11600,16384),(11610,0)]:
        args += ['--pad-at', f'{tick}:{buttons}']
    commands = ['advance 1000 0','advance 10 16384','advance 10 0',
                'advance 1000 0','advance 10 16384','advance 10 0',
                'advance 1000 0','advance 10 4096','advance 120 0',
                'advance 10 16384','advance 10 0','advance 1000 0',
                'advance 600 16','advance 30 0']
    env = dict(os.environ, SDL_AUDIO_DRIVER='dummy', PSYZ_AUDIO_CLOCK='video',
               PE_CONFIG_HOME=str(work/'config'))
    env.pop('PE_HD_BACKGROUNDS', None)
    states = []
    completed = False
    gun_hit = False
    battle_ended = False
    rebalanced_battle = False
    matched_reported_hit = False
    child = subprocess.Popen(args, cwd=work, env=env, stdin=subprocess.PIPE,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        with (work/'game.log').open('w') as log:
            for line in child.stdout:
                log.write(line)
                if 'pe_rebalance: first gun hit:' in line:
                    gun_hit = True
                    matched_reported_hit |= reported_hit is None or reported_hit in line
                if 'pe_rebalance: battle start (' in line and 'rules Rebalanced' in line:
                    rebalanced_battle = True
                if 'pe_rebalance: battle end:' in line:
                    battle_ended = True
                if not line.startswith('{"event":"control"'):
                    continue
                state = json.loads(line)
                states.append(state)
                if commands:
                    command = commands.pop(0)
                elif len(states) == 15 and state['scene'] != 0xa80471c8:
                    raise AssertionError('Reporter snapshot did not load M273')
                elif state['vblanks'] > 40000:
                    command = 'quit'
                elif gun_hit and battle_ended and state['scene'] != 0xa80471c8:
                    completed = True
                    command = 'quit'
                else:
                    command = 'advance 30 ' + str(16384 if len(states) % 2 else 0)
                child.stdin.write(command+'\n')
                child.stdin.flush()
        status = child.wait(timeout=10)
        assert status == 0, f'Native game exit {status}; see {work}'
        assert completed, f'Reported fight did not finish; see {work}'
        assert rebalanced_battle and matched_reported_hit, 'Rebalance settings or first hit differ from report'
        assert any(enemy['hp'] < 1000000 for state in states
                   if state['scene'] == 0xa80471c8
                   for enemy in state['battle_detail']['enemies']
                   if enemy['model_bank'] in (5,6,7)), 'No Eve part damaged through scripted threshold'
        assert all(state['combat'].get('player_hp', 1) > 0 for state in states)
        print('PASS: reporter M273 snapshot, Rebalance gun damage, scripted defeat and next room:', work)
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
        (work/'states.json').write_text(json.dumps(states))


if __name__ == '__main__':
    paths = [os.getenv(key) for key in ('PE_TEST_DISC','PE_TEST_DISC2','PE_TEST_CRASH58_REPORT')]
    if not all(paths):
        print('Set PE_TEST_DISC, PE_TEST_DISC2 and PE_TEST_CRASH58_REPORT to run the reported fight')
        sys.exit(77)
    replay(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve(),
           *(Path(p).resolve() for p in paths))
