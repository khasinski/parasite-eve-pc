"""The Japanese release (SLPS-01230) on the port, from its own Disc 1.

japanese_release.py GAME WORK

PE_TEST_JA_DISC names the Japanese Disc 1 (skipped, 77, without it). PE_TEST_DISC2 (the USA Disc 2)
adds the check that the two releases' discs aren't mixed; PE_TEST_ES_PACK (or any .pelang) adds the
check that a chosen language pack leaves the Japanese game alone. Nothing from the discs is kept.

1. The Japanese disc mounts (its executable's tables), its title leads into a new game with Circle as
   the confirm button, and the first box opens in the Japanese text bank (game flag 0x40000000 stays
   clear, the language index stays Japanese).
2. With a language pack chosen, the same frame is drawn byte for byte: packs translate the USA
   discs' English and are never used on the Japanese ones.
3. A save made there is the Japanese game's card file (BISLPS-01230...), not the USA one's.
4. A Japanese Disc 1 beside a USA Disc 2 is refused.
"""
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'localization'))

START, UP, CIRCLE = 8, 16, 0x2000
# Start, Up, Circle at the title (New Game), then Circle through the opening boxes.
TITLE = [(10000, START), (10010, 0), (11000, UP), (11010, 0), (11500, CIRCLE), (11510, 0)]
FIRST_BOX = 17600           # the opening scene's first box (page 9) is up and fully drawn, nothing pressed
OPENING = [(t, CIRCLE) for t in range(17000, 29901, 150)] + [(t + 6, 0) for t in range(17000, 29901, 150)]
NAMED = OPENING + [(30500, START), (30510, 0), (31000, CIRCLE), (31010, 0)] + \
    [(t, CIRCLE) for t in range(31500, 40001, 150)] + [(t + 6, 0) for t in range(31500, 40001, 150)]
SAVE_AT = 40500              # the opera entrance, Aya free to move


def run(game, discs, work, pad, frame_limit, pack=None, save_after=None):
    work.mkdir(parents=True)
    config = work / 'config'
    (config / 'lang').mkdir(parents=True)
    (config / 'pe_mods.ini').write_text('Manage mods/All mods=1\nManage mods/Language packs=1\n')
    if pack:
        from pelang import read_pack
        code, name, _ = read_pack(pack)
        shutil.copy(pack, config / 'lang' / pack.name)
        (config / 'pe_lang_selection.dat').write_bytes(
            b'PLS1' + code.encode().ljust(16, b'\0') + name.encode().ljust(32, b'\0'))
    args = [str(game), '--disc', discs[0], *(['--disc2', discs[1]] if len(discs) > 1 else []), '--headless',
            '--frame-limit', str(frame_limit), '--dump-frame', str(work / 'frame.ppm')]
    if save_after:
        args += ['--test-save-after', str(save_after)]
    for tick, mask in sorted(pad):
        args += ['--pad-at', f'{tick}:{mask}']
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', SDL_AUDIO_DRIVER='dummy', PSYZ_AUDIO_CLOCK='video',
               PE_CONFIG_HOME=str(config))
    env.pop('PE_HD_BACKGROUNDS', None)
    done = subprocess.run(args, cwd=work, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                          encoding='utf-8', errors='replace', timeout=900)
    (work / 'game.log').write_text(done.stdout, encoding='utf-8')
    state = None
    for line in done.stdout.splitlines():
        if line.startswith('{"event":"frame_limit"'):
            state = json.loads(line)
    return done.returncode, state, done.stdout


def main():
    game, work = Path(sys.argv[1]), Path(sys.argv[2])
    ja = os.environ.get('PE_TEST_JA_DISC')
    if not ja:
        print('SKIP: PE_TEST_JA_DISC (the Japanese Disc 1) is not set')
        return 77
    shutil.rmtree(work, ignore_errors=True)
    failures = []

    # 1. The first box, in Japanese.
    code, state, log = run(game, [ja], work / 'first-box', TITLE, FIRST_BOX)
    if code or not state:
        print(log[-2000:])
        raise SystemExit(f'FAIL: the Japanese disc did not reach vblank {FIRST_BOX} (exit {code})')
    shown = state['dialogue'][0]
    if shown['page'] != 9 or state['game_flags'] & 0x40000000:
        failures.append(f'first box: page {shown["page"]}, game flags {state["game_flags"]:#x} '
                        '(expected page 9 with the English-bank flag clear)')
    else:
        print('PASS: the Japanese disc mounts; Circle starts a new game; the first box opens in the Japanese bank')

    # 2. A chosen language pack changes nothing on the Japanese disc.
    pack = os.environ.get('PE_TEST_ES_PACK') or next((str(p) for p in sorted((HERE.parent / 'localization').rglob('*.pelang'))), None)
    if pack:
        code, packed, log = run(game, [ja], work / 'with-pack', TITLE, FIRST_BOX, pack=Path(pack))
        same = (work / 'with-pack' / 'frame.ppm').read_bytes() == (work / 'first-box' / 'frame.ppm').read_bytes()
        if code or not packed or not same or 'language: ' not in log:
            failures.append(f'with {Path(pack).name} chosen: exit {code}, same frame {same}')
        else:
            print(f'PASS: with {Path(pack).name} chosen the Japanese game draws the same frame')
    else:
        print('SKIP: no language pack to choose (PE_TEST_ES_PACK)')

    # 3. Its saves are the Japanese game's card files.
    code, saved, log = run(game, [ja], work / 'save', TITLE + NAMED, SAVE_AT + 400, save_after=SAVE_AT)
    files = sorted(p.name for p in (work / 'save').rglob('B?SL?S-*') if p.is_file())
    if code or not files or not all(name.startswith('BISLPS-01230') for name in files):
        failures.append(f'save: exit {code}, card files {files}')
    else:
        print(f'PASS: the save is the Japanese card file {files[0]}')

    # 4. Discs of two releases are never mixed.
    us = os.environ.get('PE_TEST_DISC2')
    if us:
        code, mixed, log = run(game, [ja, us], work / 'mixed', [], 200)
        if code == 0 or 'different releases' not in log:
            failures.append(f'mixed discs: exit {code}')
        else:
            print('PASS: a Japanese Disc 1 beside a USA disc is refused')
    if failures:
        raise SystemExit('FAIL: ' + '; '.join(failures))
    return 0


if __name__ == '__main__':
    sys.exit(main())
