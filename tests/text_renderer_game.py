"""The text renderer in the real game: language packs' stretches, name letters and saved names.

  python text_renderer_game.py GAME.exe [WORK_DIR [english,packs,name,save]]

Set PE_TEST_DISC (and PE_TEST_DISC2); otherwise this asset test is skipped (77). Nothing opens a
window and nothing touches the user's own settings, cards or packs: every run has its own
PE_CONFIG_HOME and memory card folder under WORK_DIR (a fresh temporary folder when not given).

The packs are made here from the player's own disc (the game's text is never stored): the text is
extracted, each test language gets the pelang.py sample text (the game's tags with the language's
words), a name grid in its own letters (sys/117) and a default name outside English (sys/030).

Checks, each from pe_mods.log and frame dumps:
  english    the first two dialogue boxes are the same, pixel for pixel, as the game drew them
             before the text renderer, with every mod off and with the language mod on and no
             pack chosen.
  packs      Greek, Vietnamese, Russian (and Chinese, when a CJK font is on this computer): a New
             Game reaches the first dialogue; the pack's stretches draw, no letter or stretch falls
             back to '?', and the box differs from the English one.
  name       Greek: the next box starts with Aya's default name in the pack's two-byte name
             letters; they draw (not '?') and the name differs from English "Aya".
  save       Greek: the game's save of that moment (the crash report's room snapshot, which is
             what a memory card save holds) keeps the name in the pack's name letters; loading it
             with Continue, with the Greek pack, reads it back with that pack's letters; with no
             pack chosen, the mod finds the Greek pack by its letters.
"""
import binascii
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile

SELECT, START, UP, DOWN, TRIANGLE, CROSS = 1, 8, 16, 64, 4096, 16384
NEW_GAME = [(10000, START), (10010, 0), (11000, UP), (11010, 0), (11500, CROSS), (11510, 0)]
FIRST_BOX = 18000          # dlg/M001/009 "Man: What's wrong?..." is on screen and waits
NAME_BOX = 19000           # dlg/M001/010 "{name}: ...I dunno..." after one Cross
BOX = (10, 160, 310, 230)  # the dialogue box's text, in the 320 x 240 frame
# The English boxes at FIRST_BOX and NAME_BOX (SHA-256 of BOX's pixels), as the game drew them
# before the text renderer (commit 166c2fcdd, every mod off): English must not change.
ENGLISH = {'first': 'f07dba18b7e3ff663533ff8dafb2a3d3946be8eeabc3c48c477cfdf34a32eca2',
           'name': '8b352c89ac52c02cbc56424794ea7817f2a556fd0b423af33d07381102dc9677'}

MODS = ('Cheats', 'PE Enhancements: collection', 'PE Enhancements: economy', 'Hello (example)',
        'PE Enhancements: quality of life', 'Quick save', 'PE Enhancements: rebalance',
        'PE Enhancements: stacked items', 'Warp')
PACKS = [  # code, pack name, default name (sys/030), CJK font
    ('el', 'Renderer Greek', 'Άγια', None),
    ('vi', 'Renderer Vietnamese', 'Ấyà', None),
    ('ru', 'Renderer Russian', 'Айя', None),
    ('zh-Hans', 'Renderer Chinese', '阿雅', 'C:/Windows/Fonts/msyh.ttc'),
]
CHINESE_GRID = ('的一是不了人我在有他这中大来上国个到说们为子和你地出道也时年得就那要下以生会自着去之过家学对'
                '可里后小么心多天而能好都然没日于起还发成事只作当想看文无开手十用主行方又如前所本见经头面公同三'
                '已老从动两长知民样现分将外但身些与高意进把法此实回二理美点月明其种声全工己话儿者向情部正名定女')


class Failed(Exception):
    pass


def check(condition, message):
    if not condition:
        raise Failed(message)


# ---- Packs, from the player's disc ----

def make_packs(localization, discs, out):
    """The four test packs (three without a CJK font): pelang.py sample text, a name grid in the language's letters and a default name outside English."""
    text = out / 'text'
    subprocess.run([sys.executable, '-I', str(localization / 'extract_text.py'), *discs, '--out', str(text)],
                   check=True, stdout=subprocess.DEVNULL)
    strings = text / 'strings.json'
    sys.path.insert(0, str(localization))
    from petext import alphabets  # noqa: E402 (the localization folder is on the path only now)
    entries = {e['key']: e['text'] for e in json.loads(strings.read_text(encoding='utf-8'))}
    made = []
    for code, name, default, font in PACKS:
        if font and (not Path(font).exists() or not importlib.util.find_spec('PIL')):
            print(f'SKIP {name}: needs the CJK font {font} and Pillow')
            continue
        letters = CHINESE_GRID if code == 'zh-Hans' else alphabets.BY_CODE[code].letters
        sample = out / f'{code}.csv'
        subprocess.run([sys.executable, '-I', str(localization / 'pelang.py'), 'sample', '-o', str(sample),
                        str(strings), code], check=True, stdout=subprocess.DEVNULL)
        import csv
        with sample.open(encoding='utf-8') as f:
            rows = [r for r in csv.DictReader(f) if r['key'] not in ('sys/030', 'sys/117')]
        rows.append({'key': 'sys/117', 'translation': grid(entries['sys/117'], letters), 'notes': 'test grid'})
        rows.append({'key': 'sys/030', 'translation': default, 'notes': 'test name'})
        with sample.open('w', encoding='utf-8', newline='') as f:
            w = csv.DictWriter(f, fieldnames=['key', 'translation', 'notes'])
            w.writeheader()
            w.writerows(rows)
        pack = out / f'renderer-{code}.pelang'
        cmd = [sys.executable, *([] if font else ['-I']), str(localization / 'pelang.py'), 'build', '--allow-overflow',
               '--strings', str(strings), '--lang', code, '--name', name, '-o', str(pack), str(sample)]
        if font:
            cmd += ['--cjk-font', font]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
        made.append((code, name, default, pack))
    return made


def grid(english, letters):
    """The English name grid with its letters swapped for the language's own, cell for cell."""
    out, i = [], 0
    for tag, ch in re.findall(r'(\{[^}]*\})|(.)', english):
        if tag:
            out.append(tag)
        elif ch.isalpha():
            out.append(letters[i % len(letters)])
            i += 1
        else:
            out.append(ch)
    return ''.join(out)


def name_letters(pack):
    """A v4 pack's name letters (letter 1 first), from its header (pe_lang_pack.c)."""
    data = pack.read_bytes()
    if data[:4] == b'PK\x03\x04':
        raise Failed(f'{pack} is a bundle, not a plain pack')
    at, count = struct.unpack_from('<II', data, 104)
    return [struct.unpack_from('<I', data, at + 4 * k)[0] for k in range(count)]


# ---- The game ----

def run(game, discs, work, pack=None, present=(), mods=True, pad=(), start=None, driver=None, frame_limit=None,
        crash_after=None, card=None):
    """One headless run in its own folder, with `pack` chosen and the `present` packs beside it. From vblank `start` the game pauses at each control point;
    driver(vblank, work) keeps frames (keep()) and returns the next command, or None to quit."""
    work.mkdir(parents=True)
    config = work / 'config'
    (config / 'lang').mkdir(parents=True)
    ini = f'Manage mods/All mods={int(mods)}\nManage mods/Language packs={int(mods)}\n'
    (config / 'pe_mods.ini').write_text(ini + ''.join(f'Manage mods/{m}=0\n' for m in MODS))
    if pack:
        code, name, _, path = pack
        shutil.copy(path, config / 'lang' / path.name)
        (config / 'pe_lang_selection.dat').write_bytes(
            b'PLS1' + code.encode().ljust(16, b'\0') + name.encode().ljust(32, b'\0'))
    for other in present:
        shutil.copy(other[3], config / 'lang' / other[3].name)
    if card:
        (work / 'bu00').mkdir()
        (work / 'bu00' / 'BASLUS-006620000000A').write_bytes(card)
    args = [str(game), '--disc', discs[0], *(['--disc2', discs[1]] if len(discs) > 1 else []), '--headless',
            '--frame-limit', str(frame_limit), '--dump-frame', str(work / 'frame.ppm')]
    if start:
        args += ['--control-at', str(start)]
    if crash_after:
        args += ['--test-crash-after', str(crash_after)]
    for tick, mask in pad:
        args += ['--pad-at', f'{tick}:{mask}']
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', SDL_AUDIO_DRIVER='dummy', PSYZ_AUDIO_CLOCK='video',
               PE_CONFIG_HOME=str(config))
    env.pop('PE_HD_BACKGROUNDS', None)
    child = subprocess.Popen(args, cwd=work, env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace')
    try:
        with (work / 'game.log').open('w', encoding='utf-8') as log:
            for line in child.stdout:
                log.write(line)
                if not line.startswith('{"event":"control"'):
                    continue
                log.flush()
                command = driver(json.loads(line)['vblanks'], work)
                child.stdin.write((command or 'quit') + '\n')
                child.stdin.flush()
        child.wait(timeout=30)
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
    return work


def keep(work, label):
    shutil.copy(work / 'frame.ppm', work / f'{label}.ppm')


def fixed(control):
    """A driver for (vblank, label, command) steps: the frame kept as label, then command (or a
    plain advance to the next step's vblank); quits after the last."""
    steps = list(control)

    def drive(vblank, work):
        check(steps, f'an extra control point at {vblank}: {work}')
        tick, label, command = steps.pop(0)
        check(vblank == tick, f'control at {vblank}, expected {tick}: {work}')
        keep(work, label)
        return (command or f'advance {steps[0][0] - tick} 0') if steps else None
    drive.steps = steps
    return drive


def run_fixed(game, discs, work, control, **kw):
    driver = fixed(control)
    run(game, discs, work, start=control[0][0], driver=driver, frame_limit=control[-1][0] + 100, **kw)
    check(not driver.steps, f'the run stopped before vblank {driver.steps[0][0] if driver.steps else 0}: {work}')
    return work


def until_logged(text, label, limit, every=300):
    """A driver that taps Cross every `every` vblanks until pe_mods.log has `text`, then keeps the
    frame as label and quits; fails at vblank `limit`."""
    def drive(vblank, work):
        if text in mods_log(work):
            keep(work, label)
            drive.found = vblank
            return None
        check(vblank < limit, f'"{text}" not logged by vblank {limit}: {work}')
        drive.taps += 1
        return 'advance 10 16384' if drive.taps % 2 else f'advance {every} 0'
    drive.found, drive.taps = None, 0
    return drive


def mods_log(work):
    path = work / 'config' / 'pe_mods.log'
    return path.read_text(encoding='utf-8', errors='replace') if path.exists() else ''


def frame(work, label):
    """A dumped frame (binary PPM): width, height, RGB bytes."""
    data = (work / f'{label}.ppm').read_bytes()
    match = re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s', data)
    check(match, f'not a frame dump: {work / label}.ppm')
    return int(match[1]), int(match[2]), data[match.end():]


def crop(work, label, box):
    width, _, rgb = frame(work, label)
    x0, y0, x1, y1 = box
    return b''.join(rgb[(y * width + x0) * 3:(y * width + x1) * 3] for y in range(y0, y1))


def lit(work, label, box):
    """How many pixels in the box are bright (letters), not the scene behind."""
    pixels = crop(work, label, box)
    return sum(1 for i in range(0, len(pixels), 3) if min(pixels[i:i + 3]) > 200)


def card_from_report(report):
    """A memory card file from a crash report's room snapshot (as m273_crash_replay.py)."""
    section = report.split('== Save data (save file bytes from 0x0100) ==', 1)[1].split('== Log', 1)[0]
    card = bytearray(8192)
    card[:4] = b'SC\x11\x01'
    for line in section.splitlines():
        match = re.fullmatch(r'([0-9A-F]{4}): ((?:[0-9A-F]{2} ?)+)', line.strip())
        if match:
            offset, data = int(match[1], 16), bytes.fromhex(match[2])
            card[offset:offset + len(data)] = data
    struct.pack_into('<I', card, 0x1c8c, binascii.crc_hqx(card, 0xffff) ^ 0xffff)
    return bytes(card)


def crash_report(work):
    """The text of the crash report the run printed (it is written beside the game; removed once read)."""
    for line in (work / 'game.log').read_text(encoding='utf-8', errors='replace').splitlines():
        if line.startswith('Crash report: ') and Path(line[14:].strip()).exists():
            path = Path(line[14:].strip())
            text = path.read_text(encoding='utf-8', errors='replace')
            (work / 'crash-report.txt').write_text(text, encoding='utf-8')
            path.unlink()
            return text
    raise Failed(f'no crash report: {work}')


# ---- The checks ----

def saved_name(card, pack):
    """Aya's name in a card file (metadata window 1, 0x10 in the save, which starts at 0x100), with
    the pack's name letters for two-byte letters and the game's own for the rest."""
    from petext import gametext as gt
    letters, window = name_letters(pack[3]), card[0x110:0x11D]
    name, at = '', 0
    while at < len(window) and window[at] != 0xFF:
        if 0xFB <= window[at] <= 0xFE and at + 1 < len(window):
            n = (window[at] - 0xFB) * 250 + window[at + 1]
            name += chr(letters[n - 1]) if 0 < n <= len(letters) else '?'
            at += 2
        else:
            name += gt.CHARS.get(window[at], '?')
            at += 1
    return name, window


CONTINUE = [(10000, START), (10010, 0), (11000, UP), (11010, 0), (11500, DOWN), (11510, 0), (11600, CROSS),
            (11610, 0)]
CONTINUE_AT = 12500


def continue_until(text, limit):
    """A driver for Continue from card 1, slot 1 (as m273_crash_replay.py), then waiting until
    pe_mods.log has `text`."""
    steps = ['advance 1000 0', 'advance 10 16384', 'advance 10 0', 'advance 1000 0', 'advance 10 16384',
             'advance 10 0', 'advance 1000 0', 'advance 10 4096', 'advance 120 0', 'advance 10 16384',
             'advance 10 0']

    def drive(vblank, work):
        if text in mods_log(work):
            keep(work, 'loaded')
            drive.found = vblank
            return None
        check(vblank < limit, f'"{text}" not logged by vblank {limit}: {work}')
        return steps.pop(0) if steps else 'advance 300 0'
    drive.found = None
    return drive


def main(game, work=None, parts='english,packs,name,save'):
    disc = os.getenv('PE_TEST_DISC')
    if not disc:
        print('PE_TEST_DISC is not set: skipped')
        return 77
    discs = [str(Path(disc).resolve())] + ([str(Path(os.environ['PE_TEST_DISC2']).resolve())]
                                           if os.getenv('PE_TEST_DISC2') else [])
    parts = parts.split(',')
    game = Path(game).resolve()
    localization = Path(__file__).resolve().parent.parent / 'localization'
    out = Path(work).resolve() if work else Path(tempfile.mkdtemp(prefix='pe-text-renderer-'))
    if out.exists() and any(out.iterdir()):
        shutil.rmtree(out)
    out.mkdir(parents=True, exist_ok=True)
    packs = make_packs(localization, discs, out / 'packs')
    greek = next((p for p in packs if p[0] == 'el'), None)
    check(greek, 'no Greek pack')
    first = [(FIRST_BOX, 'first', 'advance 10 16384'), (FIRST_BOX + 10, 'pressed', None)]
    english = first + [(NAME_BOX, 'name', None)]

    # English: the mod on with no pack draws the same as the game with every mod off.
    off = run_fixed(game, discs, out / 'english-mods-off', english, mods=False, pad=NEW_GAME)
    if 'english' in parts:
        on = run_fixed(game, discs, out / 'english-no-pack', english, pad=NEW_GAME)
        for label in ('first', 'name'):
            check(lit(off, label, BOX) > 200, f'no English dialogue at {label}: {off}')
            check(hashlib.sha256(crop(off, label, BOX)).hexdigest() == ENGLISH[label],
                  f'the English {label} box is not as the game drew it before the renderer: {off}')
            check(crop(off, label, BOX) == crop(on, label, BOX), f'English {label} box changed with the mod on: {on}')
        check('pe_lang: started' in mods_log(on), f'the language mod did not start: {on}')
        print('PASS english: the first two boxes are as before the renderer, mods off and with no pack')

    # Each pack: stretches draw, nothing falls back to '?'.
    for pack in packs if 'packs' in parts else []:
        code, name = pack[0], pack[1]
        work = run_fixed(game, discs, out / f'pack-{code}', first, pack=pack, pad=NEW_GAME)
        log = mods_log(work)
        label = f'{name} ({code})'
        check(f'{label}, ' in log, f'{label} not loaded: {work}')
        check(f"text: {label}'s stretches in use" in log, f'no stretch drew with {label}: {work}')
        missing = [line for line in log.splitlines()
                   if 'text: ' in line and ("shows '?'" in line or 'no stretch' in line)]
        check(not missing, f'{label}: letters fell back to "?": {missing[:5]}: {work}')
        check(lit(work, 'first', BOX) > 200, f'{label}: no dialogue drawn: {work}')
        check(crop(work, 'first', BOX) != crop(off, 'first', BOX), f'{label}: the box is still English: {work}')
        print(f'PASS {label}: stretches drew, no letter shown as "?"')

    # Greek: Aya's default name in the pack's name letters, in a box a few Cross presses on.
    if 'name' in parts:
        named = until_logged('text: name letters in use', 'name', FIRST_BOX + 6000)
        work = run(game, discs, out / 'name-greek', pack=greek, pad=NEW_GAME, start=FIRST_BOX, driver=named,
                   frame_limit=FIRST_BOX + 7000)
        check(named.found, f'no name letter drew: {work}')
        missing = [line for line in mods_log(work).splitlines() if 'text: ' in line and "shows '?'" in line]
        check(not missing, f'name letters fell back to "?": {missing[:5]}: {work}')
        check(lit(work, 'name', BOX) > 100, f'no dialogue with the name drawn: {work}')
        print(f"PASS name: Aya's Greek default name drew in the pack's name letters (vblank {named.found})")

    # Save and load: the save keeps the name in the pack's letters; Continue reads it back.
    if 'save' in parts:
        saved = run(game, discs, out / 'save-greek', pack=greek, pad=NEW_GAME, frame_limit=NAME_BOX + 1000,
                    crash_after=NAME_BOX)
        card = card_from_report(crash_report(saved))
        name, window = saved_name(card, greek)
        check(name == greek[2] and any(0xFB <= b <= 0xFE for b in window),
              f'saved name {window.hex(" ")} reads "{name}", not "{greek[2]}" in name letters: {saved}')
        print(f'PASS save: the save keeps "{name}" in the pack\'s name letters ({window.hex(" ")})')

        read = f"names: Aya's name read with {greek[1]} ({greek[0]})'s letters ({len(greek[2])} letters)"
        loaded = continue_until(read, 20000)
        work = run(game, discs, out / 'load-greek', pack=greek, pad=CONTINUE, start=CONTINUE_AT, driver=loaded,
                   frame_limit=21000, card=card)
        check(loaded.found, f'not read back: {work}')
        check("names: Aya's name put in" not in mods_log(work), f'the name was rewritten for its own pack: {work}')
        print(f'PASS load: Continue with the Greek pack reads the name with its letters (vblank {loaded.found})')

        # No pack chosen (the Greek one is there): the mod finds it by the name's letters and shows
        # the name in the game's own letters, '?' for the Greek ones.
        found = continue_until(read, 20000)
        work = run(game, discs, out / 'load-no-pack', present=[greek], pad=CONTINUE, start=CONTINUE_AT,
                   driver=found, frame_limit=21000, card=card)
        check(found.found, f'the Greek pack was not found by the name\'s letters: {work}')
        check("names: Aya's name put in the game's own text's letters" in mods_log(work),
              f'the name was not put in the game\'s own letters: {work}')
        print('PASS load: with no pack chosen, the Greek pack is found by the name\'s letters')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main(*sys.argv[1:4]))
    except Failed as failure:
        print(f'FAIL {failure}')
        sys.exit(1)
