"""The text renderer in the real game: language packs' stretches, name letters and saved names.

  python text_renderer_game.py GAME.exe [WORK_DIR [english,packs,name,menus,save,many,menu-letters]]

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
  save       Greek, after the name entry (the same run as menus): a real save to memory card 1,
             slot 1 through the game's own save writer (--test-save-after: what the save menu does
             once a slot is picked, every mod's save step and the checksum) keeps Aya's name in the
             pack's name letters. A fresh run loads it with Continue: with the Greek pack the name
             reads back with its letters, a save made then holds the same name bytes, and the
             status panel draws the name pixel for pixel as the first run did. With no pack chosen
             (the Greek one beside it) the mod finds the Greek pack by the name's letters and shows
             the name in the game's own letters; with no pack at all (English) the load works, the
             name is left as saved and the panel still shows one ('?' for each Greek letter).
  menus      Greek, against the same steps with every mod off: the game's own name entry (it opens
             after the profile box) draws the pack's grid letters (sys/117) and Aya's name in its
             name letters; Cross on the first cell adds the pack's first grid letter, two-byte, to
             the name; the grid's {g4D} cell stays empty as in English (no pack letter there); after
             it, the status panel (Triangle in the field) shows the name in the pack's letters, and
             the save of that moment holds it.
  many       A pack with all of petext/letters.py's catalog, more letters than the 163 one-byte
             codes: the first box holds only letters with no code, which the mod makes in the
             letter cache. They draw (logged, no '?') and look and space exactly as the same box in
             a pack small enough for every letter to have a code.
  menu-letters  The same two packs in a menu: the name entry's title (sys/039, a stretch) holds
             letters with no code in the big pack. They draw in the menu's style (logged, no '?')
             and look and space exactly as the small pack's title, where every letter has a code.
             Neither pack translates the grid; the English grid's {g4D} cell stays empty with both.
"""
import binascii
import csv
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


def presses(events):
    """fixed() steps for (vblank, button, label): the frame kept as label, then the button held for
    10 vblanks (none: wait for the next event)."""
    steps = []
    for tick, mask, label in events:
        steps.append((tick, label, f'advance 10 {mask}' if mask else None))
        if mask:
            steps.append((tick + 10, f'{label}-held', None))
    return steps


# A New Game to the name entry and on to the field's menu. After the first box (dlg/M001/009) and
# "Psycho..." (M001/010) comes the profile box (M001/011); Cross there opens the game's own name
# entry for Aya. Start twice ends it (cursor to END, then done). Five more boxes, then Aya is free
# in front of Carnegie Hall and Triangle opens the menu, whose status panel shows her name.
MENU_STEPS = presses([(FIRST_BOX, CROSS, 'first'), (20630, CROSS, 'box2'), (22140, CROSS, 'profile'),
                      (22450, CROSS, 'grid'), (22760, START, 'typed'), (23070, START, 'end')]
                     + [(23380 + k, CROSS, f'box{3 + n}') for n, k in enumerate((0, 1510, 3020, 4530, 6660))]
                     + [(31550, TRIANGLE, 'field'), (31860, 0, 'status')])
GRID = (72, 76, 250, 226)       # the name entry's letter grid
NAME_ENTRY = (126, 42, 172, 60)  # the name being entered, above the grid
STATUS_NAME = (66, 38, 112, 54)  # Aya's name in the status panel
STATUS_LEVEL = (68, 60, 100, 76)  # the status panel's "Level" picture, beside the name
TITLE = (8, 14, 200, 34)        # the name entry's title, "Enter Name" (sys/039)
EMPTY_CELL = (134, 198, 152, 215)  # sys/117's {g4D} cell, between ',' and '?': English draws it empty
# The boxes after the first one up to the free field stay English in the test packs, so they have
# as many pages as the English run's and MENU_STEPS's presses fit both; the sample's words for a
# box change whenever the extracted keys do. dlg/M001/010 still shows {name} in name letters.
ENGLISH_BOXES = tuple(f'dlg/M001/{n:03d}' for n in range(10, 20))
# The real save: Aya is free in front of Carnegie Hall after box7 and before Triangle (field).
SAVE_AT = 31300

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
        with sample.open(encoding='utf-8') as f:
            rows = [r for r in csv.DictReader(f) if r['key'] not in ('sys/030', 'sys/117') + ENGLISH_BOXES]
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


def make_many_packs(localization, out):
    """Two packs with the same first box (dlg/M001/009) and name entry title (sys/039), whose letters
    come from letters.py's catalog: 'many' holds the whole catalog (243 letters, 80 past the 163
    one-byte codes). The box's and the title's letters are each used once, every other letter in
    later boxes many times, so build gives the codes to those and none to the box's or the title's.
    'few' holds only the box and the title, so all of their letters have codes."""
    from petext import letters
    catalog = ''.join(ch for ch in letters.CATALOG if ch.isalpha())
    rare, common = catalog[::3][:80], ''.join(ch for ch in catalog if ch not in catalog[::3][:80])
    check(len(rare) + 163 <= len(catalog), f'the catalog has only {len(catalog)} letters')
    words = [rare[i:i + 5] for i in range(0, 60, 5)]
    box = '{br}'.join(' '.join(words[i:i + 4]) for i in range(0, 12, 4))
    title = f'{rare[60:65]} {rare[65:70]}'
    strings = out / 'text' / 'strings.json'
    keys = [e['key'] for e in json.loads(strings.read_text(encoding='utf-8'))
            if e['key'].startswith('dlg/') and not e['key'].startswith('dlg/M001/') and '{' not in e['text']]
    later = [rare[70:]] + [(common * 3)[i:i + 20] for i in range(0, 3 * len(common), 20)]
    check(len(keys) >= len(later), 'too few dialogue entries on the disc')
    made = {}
    shared = [('dlg/M001/009', box), ('sys/039', title)]
    for which, rows in (('many', shared + list(zip(keys, later))), ('few', shared)):
        table = out / f'{which}.csv'
        with table.open('w', encoding='utf-8', newline='') as f:
            w = csv.DictWriter(f, fieldnames=['key', 'translation', 'notes'])
            w.writeheader()
            w.writerows({'key': k, 'translation': t, 'notes': ''} for k, t in rows)
        pack = out / f'letters-{which}.pelang'
        subprocess.run([sys.executable, '-I', str(localization / 'pelang.py'), 'build', '--allow-overflow',
                        '--strings', str(strings), '--lang', 'xx', '--name', f'Letters {which}', '-o', str(pack),
                        str(table)], check=True, stdout=subprocess.DEVNULL)
        made[which] = ('xx', f'Letters {which}', None, pack)
    return made, ''.join(words), title.replace(' ', '')


def letter_codes(pack):
    """A v4 pack's table letters: Unicode letter -> its one-byte code, 0 for none (pe_lang_pack.c)."""
    from petext import letters as lettertable
    data = pack.read_bytes()
    at, count = struct.unpack_from('<II', data, 80)
    return {chr(struct.unpack_from('<I', data, at + k * lettertable.RECORD)[0]): data[at + k * lettertable.RECORD + 4]
            for k in range(count)}


def name_letters(pack):
    """A v4 pack's name letters (letter 1 first), from its header (pe_lang_pack.c)."""
    data = pack.read_bytes()
    if data[:4] == b'PK\x03\x04':
        raise Failed(f'{pack} is a bundle, not a plain pack')
    at, count = struct.unpack_from('<II', data, 104)
    return [struct.unpack_from('<I', data, at + 4 * k)[0] for k in range(count)]


# ---- The game ----

def run(game, discs, work, pack=None, present=(), mods=True, pad=(), start=None, driver=None, frame_limit=None,
        crash_after=None, card=None, save_after=None):
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
    if save_after:
        args += ['--test-save-after', str(save_after)]
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
    plain advance to the next step's vblank); after the last step, its command or quit."""
    steps = list(control)

    def drive(vblank, work):
        check(steps, f'an extra control point at {vblank}: {work}')
        tick, label, command = steps.pop(0)
        check(vblank == tick, f'control at {vblank}, expected {tick}: {work}')
        keep(work, label)
        return (command or f'advance {steps[0][0] - tick} 0') if steps else command
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
# Continue from card 1, slot 1 at fixed moments (as m273_crash_replay.py), then the field: the
# loaded save starts in front of Carnegie Hall by vblank 15900. A save at LOADED_SAVE_AT holds the
# loaded name; Triangle opens the status panel.
LOADED_SAVE_AT = 16100


def loaded_steps():
    steps, tick = [], CONTINUE_AT
    for n, command in enumerate(['advance 1000 0', 'advance 10 16384', 'advance 10 0', 'advance 1000 0',
                                 'advance 10 16384', 'advance 10 0', 'advance 1000 0', 'advance 10 4096',
                                 'advance 120 0', 'advance 10 16384', 'advance 10 0']):
        steps.append((tick, f'continue{n}', command))
        tick += int(command.split()[1])
    return steps + [(tick, 'loaded', None), (16300, 'field', 'advance 10 4096'), (16310, 'pressed', None),
                    (16610, 'status', None)]


def letters_only(work, label, box):
    """The box's bright pixels (letters) as they are and every other pixel black: the status panel
    is see-through, so the scene behind it differs between runs while the letters must not."""
    pixels = crop(work, label, box)
    return bytes(b for i in range(0, len(pixels), 3)
                 for b in (pixels[i:i + 3] if min(pixels[i:i + 3]) > 200 else b'\0\0\0'))


def card_file(work):
    """Card 1, slot 1 as the game wrote it, checked like the card load checks it."""
    log = (work / 'game.log').read_text(encoding='utf-8', errors='replace')
    check('Test save: wrote' in log, f'the game did not save: {work}')
    card = (work / 'bu00' / 'BASLUS-006620000000A').read_bytes()
    stored = struct.unpack_from('<I', card, 0x1c8c)[0]
    check(len(card) == 8192 and card[:2] == b'SC'
          and stored == binascii.crc_hqx(card[:0x1c8c] + bytes(4) + card[0x1c90:], 0xffff) ^ 0xffff,
          f'the saved card file is not a whole save with its checksum: {work}')
    return card


def main(game, work=None, parts='english,packs,name,menus,save,many,menu-letters'):
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

    # The name entry and the status panel, against the same steps with every mod off.
    status = MENU_STEPS[-1][0]
    if 'menus' in parts or 'save' in parts:
        work = out / 'menus-greek'
        driver = fixed(MENU_STEPS[:-1] + [(status, 'status', 'advance 300 0')])
        run(game, discs, work, pack=greek, pad=NEW_GAME, start=FIRST_BOX, driver=driver, frame_limit=status + 400,
            crash_after=status + 100, save_after=SAVE_AT)
        check(not driver.steps, f'the run stopped before vblank {driver.steps[0][0] if driver.steps else 0}: {work}')
        menus_greek = work
    if 'menus' in parts:
        english = run_fixed(game, discs, out / 'menus-english', MENU_STEPS, mods=False, pad=NEW_GAME)
        log = mods_log(work)
        missing = [line for line in log.splitlines()
                   if 'text: ' in line and ("shows '?'" in line or 'no stretch' in line)]
        check(not missing, f'menus: letters fell back to "?": {missing[:5]}: {work}')
        check(lit(english, 'grid', GRID) > 500, f'no English name entry grid at vblank 22450: {english}')
        check(lit(work, 'grid', GRID) > lit(english, 'grid', GRID) // 2, f'the Greek name entry grid is not drawn: {work}')
        check(crop(work, 'grid', GRID) != crop(english, 'grid', GRID), f'the name entry grid is still English: {work}')
        check(lit(work, 'grid', NAME_ENTRY) > 20, f'no name above the Greek grid: {work}')
        check(crop(work, 'grid', NAME_ENTRY) != crop(english, 'grid', NAME_ENTRY),
              f'the name entry shows the English name: {work}')
        check(crop(work, 'typed', NAME_ENTRY) != crop(work, 'grid', NAME_ENTRY),
              f'Cross on the grid added no letter to the name: {work}')
        check(lit(english, 'grid', EMPTY_CELL) == 0, f'the English grid\'s {{g4D}} cell is not empty: {english}')
        check(crop(work, 'grid', EMPTY_CELL) == crop(english, 'grid', EMPTY_CELL),
              f'the Greek grid\'s {{g4D}} cell is not empty as in English: {work}')
        print("PASS name entry: the pack's grid letters and Aya's name in its name letters drew")

        for label, frame_of in (('English', english), ('Greek', work)):
            check(lit(frame_of, 'status', STATUS_NAME) > 20
                  and crop(frame_of, 'status', STATUS_LEVEL) != crop(frame_of, 'field', STATUS_LEVEL),
                  f'the {label} status panel is not open at vblank {status}: {frame_of}')
        check(crop(work, 'status', STATUS_LEVEL) == crop(english, 'status', STATUS_LEVEL),
              f'the Greek and English frames at {status} are not the same panel: {work}')
        check(crop(work, 'status', STATUS_NAME) != crop(english, 'status', STATUS_NAME),
              f'the status panel shows the English name: {work}')
        card = card_from_report(crash_report(work))
        name, window = saved_name(card, greek)
        from petext import alphabets
        # the default name, then the grid's first cell (as grid() fills it) once for each Cross that
        # reached the grid: one or two, as in the English run, by when the name entry opens
        first_cell = alphabets.BY_CODE['el'].letters[0]
        added = name[len(greek[2]):]
        check(name.startswith(greek[2]) and added and added == first_cell * len(added),
              f'the name after the name entry reads "{name}" ({window.hex(" ")}), not "{greek[2]}" and '
              f'"{first_cell}"s: {work}')
        print(f'PASS status: the panel shows Aya\'s name in the pack\'s letters; the save holds "{name}" '
              f'({window.hex(" ")})')

    # A real save after the name entry, loaded back by a fresh run with Continue.
    if 'save' in parts:
        saved = menus_greek
        log = (saved / 'game.log').read_text(encoding='utf-8', errors='replace')
        at = re.search(r'Test save: wrote .* at VBlank (\d+)', log)
        check(at and int(at[1]) < next(tick for tick, label, _ in MENU_STEPS if label == 'field'),
              f'the save was not made in the field before Triangle: {saved}')
        card = card_file(saved)
        name, window = saved_name(card, greek)
        check(name.startswith(greek[2]) and len(name) > len(greek[2]) and any(0xFB <= b <= 0xFE for b in window),
              f'the saved name {window.hex(" ")} reads "{name}", not the name entry\'s Greek name in name letters: {saved}')
        print(f'PASS save: the game\'s save writer kept "{name}" in the pack\'s name letters ({window.hex(" ")}, '
              f'vblank {at[1]})')

        steps = loaded_steps()
        loads = {}
        for which, kw in (('greek', {'pack': greek}), ('no-pack', {'present': [greek]}), ('english', {})):
            loads[which] = run_fixed(game, discs, out / f'load-{which}', steps, pad=CONTINUE, card=card,
                                     save_after=LOADED_SAVE_AT, **kw)
            check(lit(loads[which], 'status', STATUS_NAME) > 20
                  and crop(loads[which], 'status', STATUS_LEVEL) != crop(loads[which], 'field', STATUS_LEVEL),
                  f'{which}: no status panel after loading the save: {loads[which]}')
        work = loads['greek']
        read = f"names: Aya's name read with {greek[1]} ({greek[0]})'s letters ({len(name)} letters)"
        check(read in mods_log(work), f'the name was not read back with the Greek pack\'s letters: {work}')
        check("names: Aya's name put in" not in mods_log(work), f'the name was rewritten for its own pack: {work}')
        again = card_file(work)
        check(again[0x110:0x11D] == card[0x110:0x11D],
              f'a save after loading holds {again[0x110:0x11D].hex(" ")}, not {window.hex(" ")}: {work}')
        check(letters_only(work, 'status', STATUS_NAME) == letters_only(saved, 'status', STATUS_NAME),
              f'the status panel draws the loaded name unlike before the save: {work} vs {saved}')
        print('PASS load: Continue with the Greek pack reads the name back; saved again it is the same bytes, and '
              'the status panel draws it pixel for pixel as before')

        work = loads['no-pack']
        check(read in mods_log(work), f'with no pack chosen the Greek pack was not found by the name\'s letters: {work}')
        check("names: Aya's name put in the game's own text's letters" in mods_log(work),
              f'the name was not put in the game\'s own letters: {work}')
        check(letters_only(work, 'status', STATUS_NAME) != letters_only(saved, 'status', STATUS_NAME),
              f'with no pack chosen the panel still draws the Greek letters: {work}')
        print('PASS load: with no pack chosen, the Greek pack is found by the name\'s letters and the name shown in '
              'the game\'s own letters')

        work = loads['english']
        check("names: Aya's name has name letters no loaded pack knows; left as saved" in mods_log(work),
              f'with no pack the name was not left as saved: {work}')
        check(card_file(work)[0x110:0x11D] == card[0x110:0x11D],
              f'with no pack a save after loading changed the name bytes: {work}')
        check("text: no pack chosen has no name letter FB " in mods_log(work),
              f'with no pack the name letters were not shown as "?": {work}')
        print('PASS load: with no pack (English) the save loads, the name is left as saved and the panel shows it '
              'with "?" for each Greek letter')

    # More letters than the 163 codes: the first box's letters have none and come from the cache.
    if 'many' in parts or 'menu-letters' in parts:
        made, box_letters, title_letters = make_many_packs(localization, out / 'packs')
        codes = letter_codes(made['many'][3])
        check(len(codes) > 163 and all(codes.get(ch) == 0 for ch in box_letters + title_letters),
              f'the box\'s and title\'s letters are not all without a code in {made["many"][3]} ({len(codes)} letters)')
        check(all(letter_codes(made['few'][3]).get(ch) for ch in box_letters + title_letters),
              f'the small pack lacks codes for the box\'s or title\'s letters: {made["few"][3]}')
    if 'many' in parts:
        boxes = {}
        for which, pack in made.items():
            boxes[which] = run_fixed(game, discs, out / f'letters-{which}', first, pack=pack, pad=NEW_GAME)
            log = mods_log(boxes[which])
            missing = [line for line in log.splitlines() if 'text: ' in line and "shows '?'" in line
                       or 'cache unavailable' in line or 'no stretch' in line]
            check(not missing, f'{which}: letters fell back to "?": {missing[:5]}: {boxes[which]}')
            check(lit(boxes[which], 'first', BOX) > 200, f'{which}: no dialogue drawn: {boxes[which]}')
        uncoded = "'s letters with no code in use in dialogue"
        check(f"text: {made['many'][1]} (xx){uncoded}" in mods_log(boxes['many']),
              f'no letter without a code drew: {boxes["many"]}')
        check(uncoded not in mods_log(boxes['few']), f'the small pack drew a letter without a code: {boxes["few"]}')
        check(crop(boxes['many'], 'first', BOX) == crop(boxes['few'], 'first', BOX),
              f'letters without a code look or space unlike coded ones: {boxes["many"]} vs {boxes["few"]}')
        print(f'PASS many letters: {len(codes)} letters; the box\'s {len(box_letters)}, none with a code, drew from '
              'the letter cache the same as coded ones')

    # The same in a menu: the name entry's title, whose letters have no code in the big pack.
    if 'menu-letters' in parts:
        grid_at = next(tick for tick, label, _ in MENU_STEPS if label == 'grid')
        steps = [step for step in MENU_STEPS if step[0] < grid_at] + [(grid_at, 'grid', None)]
        titles = {}
        for which, pack in made.items():
            titles[which] = run_fixed(game, discs, out / f'menu-letters-{which}', steps, pack=pack, pad=NEW_GAME)
            log = mods_log(titles[which])
            missing = [line for line in log.splitlines() if 'text: ' in line and "shows '?'" in line
                       or 'cache unavailable' in line or 'no stretch' in line]
            check(not missing, f'menu {which}: letters fell back to "?": {missing[:5]}: {titles[which]}')
            check(lit(titles[which], 'grid', TITLE) > 30, f'menu {which}: no name entry title drawn: {titles[which]}')
        uncoded = "'s letters with no code in use in menus"
        check(f"text: {made['many'][1]} (xx){uncoded}" in mods_log(titles['many']),
              f'no letter without a code drew in a menu: {titles["many"]}')
        check(uncoded not in mods_log(titles['few']), f'the small pack drew a letter without a code: {titles["few"]}')
        check(crop(titles['many'], 'grid', TITLE) == crop(titles['few'], 'grid', TITLE),
              f'menu letters without a code look or space unlike coded ones: {titles["many"]} vs {titles["few"]}')
        # neither pack translates the grid: the English one's {g4D} cell stays empty with both
        for which, work in titles.items():
            check(lit(work, 'grid', EMPTY_CELL) == 0 and 'name entry grid cells kept empty' in mods_log(work),
                  f'menu {which}: the English grid\'s {{g4D}} cell shows a pack letter: {work}')
        print(f'PASS menu letters: the name entry title\'s {len(title_letters)} letters, none with a code, drew '
              'the same as coded ones')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main(*sys.argv[1:4]))
    except Failed as failure:
        print(f'FAIL {failure}')
        sys.exit(1)
