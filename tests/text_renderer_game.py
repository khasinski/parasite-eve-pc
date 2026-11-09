"""The text renderer in the real game: language packs' stretches, name letters and saved names.

  python text_renderer_game.py GAME.exe [WORK_DIR [english,packs,name,menus,save,many,menu-letters,rtl,split,drawn]]

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
  rtl        Right to left, Hebrew: a pack with the language code "he" against a left-to-right pack
             ("xx") holding the same text already in drawing order (petext/rtl.py, which the
             renderer's own test holds to the same cases). The first box's lines (Hebrew words, an
             English name, a year, brackets) draw the same letters in the same places, each line
             moved to end at the box's right side (x 300). The name entry's title, the grid and
             Aya's Hebrew name above it draw pixel for pixel as the left-to-right pack's: menus keep
             their places, only the order inside changes. The status panel shows the name.
             Arabic: a pack with the code "ar" whose first box holds four words (joined letters,
             letters that join only the one before them, lam-alef twice, hamza) and whose name
             entry title is one word. The frames light exactly the pixels petext/rtl.py and the
             pack's letter pictures say they should: each letter in its joined shape, the gaps
             between joined letters closed, right to left, each box line ending at x 300. The
             scene behind the box comes from a pack whose first box is a lone '.'.
  split      A pack built as a fan patch's is (--from-patch) whose message box line (sys/086) is too
             wide for the screen: the build puts it on two lines, not narrower. A save made from the
             first box, one byte changed so its checksum fails, is opened with Continue: the box
             saying so (sys/085, sys/086) shows one more line of text than English's, at full size.
  drawn      A pack that draws letters its own way, as a fan patch's own letters come in (pelang.py
             build --patch-letters): every letter of the first box and of the name entry's title gets
             a test picture and widths unlike the game's, including the game's '0' (code 0), the
             space and 'e'. The box lights exactly the pixels those pictures at those widths say,
             the space still pulled back as the English box pulls it, and 'é', which the pack
             doesn't draw, made from its drawn 'e'; the title lights exactly the pixels the menus
             cut from the pictures by their metrics.
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
    ('he', 'Renderer Hebrew', 'איה', None),
    ('vi', 'Renderer Vietnamese', 'Ấyà', None),
    ('ru', 'Renderer Russian', 'Айя', None),
    ('zh-Hans', 'Renderer Chinese', '阿雅', 'C:/Windows/Fonts/msyh.ttc'),
]
# Part rtl: the first box's lines, the name entry's title (sys/039) and Aya's name, in Hebrew.
HEBREW_BOX = ['שלום Aya, בשנת 1997!',
              '(ניו יורק) ברוכה הבאה',
              'דג סקרן שט בים.']
HEBREW_TITLE = 'הכנס שם'
HEBREW_NAME = 'איה'
RIGHT_EDGE = 300               # a right-to-left line in the default box ends here (Textbox_RightEdge)
LINE_TOP = 174                 # the default box's first line (textbox.c: cursor.y 0xAE), 12 px each
ARABIC_BOX = ['نيويورك', 'مستشفيات',
              'الاعلانات', 'أصدقاء']
ARABIC_TITLE = 'الاسم'
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
    from petext import letters, rtl
    catalog = ''.join(ch for ch in letters.CATALOG if ch.isalpha() and not rtl.is_joining(ord(ch)))
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


def make_rtl_packs(localization, out, visual):
    """The Hebrew pack ('he', right to left) and its mirror ('xx', left to right) with the same text
    in drawing order (`visual`, petext/rtl.py): the first box, the name entry's title, Aya's name and a
    grid of Hebrew letters. The boxes after the first stay English, as in make_packs."""
    strings = out / 'text' / 'strings.json'
    entries = {e['key']: e['text'] for e in json.loads(strings.read_text(encoding='utf-8'))}
    from petext import alphabets
    hebrew = alphabets.BY_CODE['he'].letters
    made = {}
    for code, name, line in (('he', 'Right to left', lambda t: t), ('xx', 'Mirror', visual)):
        rows = [('dlg/M001/009', '{br}'.join(line(t) for t in HEBREW_BOX)), ('sys/039', line(HEBREW_TITLE)),
                ('sys/030', line(HEBREW_NAME)), ('sys/117', grid(entries['sys/117'], hebrew))]
        table = out / f'rtl-{code}.csv'
        with table.open('w', encoding='utf-8', newline='') as f:
            w = csv.DictWriter(f, fieldnames=['key', 'translation', 'notes'])
            w.writeheader()
            w.writerows({'key': k, 'translation': t, 'notes': ''} for k, t in rows)
        pack = out / f'rtl-{code}.pelang'
        subprocess.run([sys.executable, '-I', str(localization / 'pelang.py'), 'build', '--strings', str(strings),
                        '--lang', code, '--name', name, '-o', str(pack), str(table)], check=True,
                       stdout=subprocess.DEVNULL)
        made[code] = (code, name, line(HEBREW_NAME), pack)
    return made


def make_arabic_packs(localization, out):
    """The Arabic pack ('ar') and a pack whose first box is a lone '.' at its left (the scene)."""
    strings = out / 'text' / 'strings.json'
    made = {}
    for code, name, rows in (('ar', 'Arabic', [('dlg/M001/009', '{br}'.join(ARABIC_BOX)), ('sys/039', ARABIC_TITLE)]),
                             ('xx', 'Scene', [('dlg/M001/009', '.')])):
        table = out / f'arabic-{code}.csv'
        with table.open('w', encoding='utf-8', newline='') as f:
            w = csv.DictWriter(f, fieldnames=['key', 'translation', 'notes'])
            w.writeheader()
            w.writerows({'key': k, 'translation': t, 'notes': ''} for k, t in rows)
        pack = out / f'arabic-{code}.pelang'
        subprocess.run([sys.executable, '-I', str(localization / 'pelang.py'), 'build', '--strings', str(strings),
                        '--lang', code, '--name', name, '-o', str(pack), str(table)], check=True,
                       stdout=subprocess.DEVNULL)
        made[code] = (code, name, None, pack)
    return made


# Part drawn: the first box (dlg/M001/009) and the name entry's title (sys/039), in letters the pack
# draws itself with test pictures and widths unlike the game's (DRAWN_WIDTHS: dialogue advance; the
# menus' metrics are its width less one, first column 1). 'é' isn't drawn: pe_lang makes it from the
# drawn 'e'.
DRAWN_BOX = 'Wave 0 wé'
DRAWN_TITLE = 'Wave0'
DRAWN_WIDTHS = {'W': 9, 'a': 6, 'v': 5, 'e': 6, '0': 7, 'w': 8, ' ': 10}
LINE_LEFT = 20                 # where the default box's lines start


def drawn_picture(ch):
    """A test picture: strokes on a pattern of its own in the columns its width leaves, shadow right
    of and below them as the game's letters have it (0 empty, 1 stroke, 2 shadow)."""
    w, k = DRAWN_WIDTHS[ch], ord(ch)
    strokes = {(x, y) for x in range(1, w - 2) for y in range(2, 10) if ch != ' ' and (x * 3 + y + k) % 4}
    return [1 if (x, y) in strokes else 2 if (x - 1, y - 1) in strokes or (x - 1, y) in strokes else 0
            for y in range(12) for x in range(12)]


def make_drawn_packs(localization, out):
    """The drawn pack ('xx', its pictures file from drawn_picture) and the scene pack, whose first box
    is a lone '.' at its left and whose title is English."""
    strings = out / 'text' / 'strings.json'
    pictures = out / 'drawn-letters.json'
    pictures.write_text(json.dumps({'letters': [
        {'letter': ch, 'code': '00', 'dialogue': w, 'menu': (w - 1) << 4 | 1,
         'picture': [''.join(' #+'[v] for v in drawn_picture(ch)[y * 12:(y + 1) * 12]) for y in range(12)]}
        for ch, w in DRAWN_WIDTHS.items()]}, ensure_ascii=False), encoding='utf-8')
    made = {}
    for name, rows, extra in (('Drawn', [('dlg/M001/009', DRAWN_BOX), ('sys/039', DRAWN_TITLE)],
                               ['--patch-letters', str(pictures)]),
                              ('Scene', [('dlg/M001/009', '.')], [])):
        table = out / f'drawn-{name.lower()}.csv'
        with table.open('w', encoding='utf-8', newline='') as f:
            w = csv.DictWriter(f, fieldnames=['key', 'translation', 'notes'])
            w.writeheader()
            w.writerows({'key': k, 'translation': t, 'notes': ''} for k, t in rows)
        pack = out / f'drawn-{name.lower()}.pelang'
        subprocess.run([sys.executable, '-I', str(localization / 'pelang.py'), 'build', '--strings', str(strings),
                        '--lang', 'xx', '--name', name, '-o', str(pack), str(table), *extra], check=True,
                       stdout=subprocess.DEVNULL)
        made[name.lower()] = ('xx', name, None, pack)
    return made


def drawn_line(text, style):
    """{(x, row): 1 or 2} of a line of the drawn pack from its pen's start, as the box (style 'box')
    or a menu ('menu') draws it; later letters are drawn over earlier ones."""
    from petext import latin
    pixels, pen = {}, 0
    for ch in text:
        if ch in DRAWN_WIDTHS:
            cell, w = drawn_picture(ch), DRAWN_WIDTHS[ch]
        else:                                      # é, made by pe_lang from the drawn e
            g = next(g for g in latin.GLYPHS if g.char == ch)
            cell, w = latin.compose(g, drawn_picture(g.base)), DRAWN_WIDTHS[g.base]
        if style == 'box':
            cols, x0, advance = range(12), 0, w - (7 if ch == ' ' else 0)
        else:                                      # cut from column 1, metrics width w - 1, half the spacing in
            spacing = 2 if ch.isdigit() or ch == ' ' else 1
            cols, x0, advance = range(1, w), spacing // 2 - 1, (w - 1) + spacing
        for p, v in enumerate(cell):
            if v and p % 12 in cols:
                pixels[(pen + x0 + p % 12, p // 12 + (1 if style == 'menu' else 0))] = v
        pen += advance
    return pixels, pen


def joined_line(text):
    """(width, {(x, row): 1 stroke or 2 shadow}) of an Arabic word as the renderer draws it, from
    its left edge and the cell's top: petext/rtl.py's shapes and order, each letter cut to its strokes
    and shadow (pe_lang), one pixel apart, or touching where they join. The leftmost letter is drawn
    last, over the shadow of the one on its right."""
    from petext import letters, rtl
    joined, nxt = rtl.join(text)
    order, _ = rtl.order(joined)
    placed, pen = [], 0
    for k, i in enumerate(order):
        letter = letters.CATALOG[joined[i]]
        first, w = letter.cut()
        advance = w + 1
        if k + 1 < len(order) and order[k + 1] + 1 == i and nxt[order[k + 1]]:
            advance = w - 1
        placed.append((pen, first, w, letter.compose()))
        pen += advance
    pixels = {}
    for at, first, w, cell in reversed(placed):
        for p, v in enumerate(cell):
            if v and first <= p % 12 < first + w:
                pixels[(at + p % 12 - first, p // 12)] = v
    return pen, pixels


def turned(a, b):
    """How many columns of b's line (bright masks of the same box) moved from its right end to the
    left make a, counting the gap after b's last letter (0 to 3 px); 0 when they don't."""
    lit_cols = [x for x in range(len(a[0])) if any(row[x] for row in a)]
    other = [x for x in range(len(b[0])) if any(row[x] for row in b)]
    if not lit_cols or lit_cols[0] != other[0] or lit_cols[-1] != other[-1]:
        return 0
    x0, length = lit_cols[0], lit_cols[-1] - lit_cols[0] + 1
    for gap in range(4):
        period = length + gap
        for moved in range(1, period):
            def at(row, x):
                k = (x - moved) % period
                return row[x0 + k] if k < length else 0
            if all(ra[x0 + x] == at(rb, x) for ra, rb in zip(a, b) for x in range(length)):
                return moved
    return 0


def bright(work, label, box):
    """The box's bright pixels (letters) as rows of 0/1."""
    x0, _, x1, _ = box
    pixels = crop(work, label, box)
    w = x1 - x0
    return [[1 if min(pixels[(y * w + x) * 3:(y * w + x) * 3 + 3]) > 200 else 0 for x in range(w)]
            for y in range(len(pixels) // (3 * w))]      # the frame may end above the box's bottom


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
            name += chr(letters[n - 1]) if 0 < n <= len(letters) and letters[n - 1] else '?'
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


# Continue to the save list and open card 1, slot 1 (the title's Continue, as the earlier
# squeeze pictures did it): the list at LIST_AT, the box about the save at OPENED_AT.
OPEN_SAVE = [(10000, START), (10010, 0), (11500, CROSS), (11510, 0)]
LIST_AT, OPENED_AT = 12170, 12480
OPEN_STEPS = [(11850, 'slots', 'advance 10 16384'), (11860, 'pressed', 'advance 310 0'),
              (LIST_AT, 'list', 'advance 10 16384'), (LIST_AT + 10, 'pressed2', 'advance 300 0'),
              (OPENED_AT, 'opened', None)]
MESSAGE_BOX = (40, 100, 290, 182)  # inside the box about the save: under the slot's row, above "Unused File"
# A test line too wide for one line of a message box (more than 300 px), and the box's title.
SPLIT_TEXT = {'sys/085': 'Save unreadable', 'sys/086': 'Erase this broken save from the memory card manager screen'}


def text_rows(work, label, box):
    """The top of each row of text in the box: bands of bright pixels 5 or more rows tall (a
    button's 2-pixel edge is not text)."""
    width, _, rgb = frame(work, label)
    x0, y0, x1, y1 = box
    bands = []
    for y in range(y0, y1):
        row = rgb[(y * width + x0) * 3:(y * width + x1) * 3]
        if any(min(row[i:i + 3]) > 200 for i in range(0, len(row), 3)):
            if bands and bands[-1][1] == y - 1:
                bands[-1][1] = y
            else:
                bands.append([y, y])
    return [top for top, bottom in bands if bottom - top >= 4]


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


def main(game, work=None, parts='english,packs,name,menus,save,many,menu-letters,rtl,split,drawn'):
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
        # every name letter of the saved name shows '?' (the shared numbers put Greek on page FC)
        shown = set(re.findall(r"text: no pack chosen has no name letter (F[B-E] [0-9A-F]{2}); it shows '\?'",
                               mods_log(work)))
        saved_letters, window, at = set(), card[0x110:0x11D], 0
        while at < len(window) and window[at] != 0xFF:
            if 0xFB <= window[at] <= 0xFE and at + 1 < len(window):
                saved_letters.add(f'{window[at]:02X} {window[at + 1]:02X}')
                at += 1
            at += 1
        check(shown and saved_letters <= shown,
              f'with no pack the name letters were not shown as "?" ({sorted(saved_letters - shown)}): {work}')
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

    # Right to left: Hebrew against the same text in drawing order, drawn left to right.
    if 'rtl' in parts:
        from petext import rtl
        made = make_rtl_packs(localization, out / 'packs', rtl.visual)
        boxes = {code: run_fixed(game, discs, out / f'rtl-box-{code}', first, pack=pack, pad=NEW_GAME)
                 for code, pack in made.items()}
        for code, work in boxes.items():
            log = mods_log(work)
            missing = [line for line in log.splitlines() if 'text: ' in line and "shows '?'" in line
                       or 'cache unavailable' in line or 'no stretch' in line]
            check(not missing, f'rtl {code}: letters fell back to "?": {missing[:5]}: {work}')
        check('reads right to left (he)' in mods_log(boxes['he']), f'the Hebrew pack is not read right to left: {boxes["he"]}')
        check('reads right to left' not in mods_log(boxes['xx']), f'the mirror pack is read right to left: {boxes["xx"]}')
        width = BOX[2] - BOX[0]
        he, xx = bright(boxes['he'], 'first', BOX), bright(boxes['xx'], 'first', BOX)
        # the scene behind the see-through box is the same in both runs: a pixel bright in both at
        # one place is taken for the scene (or letters of both lines on one spot) and not compared
        scene = [[a & b for a, b in zip(r1, r2)] for r1, r2 in zip(he, xx)]
        for n in range(len(HEBREW_BOX)):
            top = LINE_TOP - BOX[1] + n * 12
            rows = range(top, top + 12)
            cols = lambda mask: [x for x in range(width) if any(mask[y][x] and not scene[y][x] for y in rows)]
            l_he, l_xx = cols(he), cols(xx)
            check(l_he and l_xx, f'rtl: line {n + 1} is not drawn: {boxes["he"]} {boxes["xx"]}')
            shift = l_he[-1] - l_xx[-1]
            check(l_xx[0] + BOX[0] <= 24, f'rtl: the mirror\'s line {n + 1} starts at x {l_xx[0] + BOX[0]}: {boxes["xx"]}')
            check(RIGHT_EDGE - 8 <= l_he[-1] + BOX[0] <= RIGHT_EDGE,
                  f'rtl: line {n + 1} ends at x {l_he[-1] + BOX[0]}, not at the box\'s right side: {boxes["he"]}')
            wrong = [(x + BOX[0], y + BOX[1]) for y in rows for x in range(width)
                     if 0 <= x + shift < width and not scene[y][x] and not scene[y][x + shift]
                     and he[y][x + shift] != xx[y][x]]
            check(not wrong,
                  f'rtl: line {n + 1} does not draw the drawing-order letters moved {shift} px right '
                  f'(first differences at {wrong[:4]}): {boxes["he"]}')
        print(f'PASS rtl dialogue: {len(HEBREW_BOX)} Hebrew lines (with English, a year and brackets) drew right to left, '
              'letter for letter as in drawing order, ending at the box\'s right side')

        menus = {code: run_fixed(game, discs, out / f'rtl-menus-{code}', MENU_STEPS, pack=pack, pad=NEW_GAME)
                 for code, pack in made.items()}
        for code, work in menus.items():
            log = mods_log(work)
            missing = [line for line in log.splitlines() if 'text: ' in line and "shows '?'" in line
                       or 'cache unavailable' in line or 'no stretch' in line]
            check(not missing, f'rtl menus {code}: letters fell back to "?": {missing[:5]}: {work}')
            check(lit(work, 'grid', TITLE) > 30 and lit(work, 'grid', NAME_ENTRY) > 20,
                  f'rtl menus {code}: no name entry title or name: {work}')
            check(lit(work, 'status', STATUS_NAME) > 20
                  and crop(work, 'status', STATUS_LEVEL) != crop(work, 'field', STATUS_LEVEL),
                  f'rtl menus {code}: the status panel is not open: {work}')
        for label, box, what in (('grid', TITLE, 'the name entry title'), ('grid', GRID, 'the grid')):
            check(crop(menus['he'], label, box) == crop(menus['xx'], label, box),
                  f'rtl: {what} differs from the drawing-order pack\'s: {menus["he"]} vs {menus["xx"]}')
        # Aya's name: the default name, then the grid letters the Cross presses added, at its end in
        # reading order. Right to left those come first from the left; in the mirror they follow on the
        # right. Both start where the game puts the name, so one is the other turned round by the
        # typed letters' width.
        for label, box, where in (('grid', NAME_ENTRY, 'above the grid'), ('status', STATUS_NAME, 'in the status panel')):
            typed = turned(bright(menus['he'], label, box), bright(menus['xx'], label, box))
            check(typed, f"rtl: Aya's name {where} is not the drawing-order name with the typed letters on the left: "
                         f'{menus["he"]} vs {menus["xx"]}')
        print("PASS rtl menus: the name entry title and the grid drew as in drawing order, in the places the game gives "
              "them; Aya's Hebrew name grows to the left as letters are typed, above the grid and in the status panel")

        # Arabic: the frames light exactly the pixels the joined words should.
        arabic = make_arabic_packs(localization, out / 'packs')
        boxes = {code: run_fixed(game, discs, out / f'arabic-box-{code}', first, pack=pack, pad=NEW_GAME)
                 for code, pack in arabic.items()}
        log = mods_log(boxes['ar'])
        missing = [line for line in log.splitlines() if 'text: ' in line and "shows '?'" in line
                   or 'cache unavailable' in line or 'no stretch' in line]
        check(not missing, f'arabic: letters fell back to "?": {missing[:5]}: {boxes["ar"]}')
        check('reads right to left (ar)' in log, f'the Arabic pack is not read right to left: {boxes["ar"]}')
        ar, scene = bright(boxes['ar'], 'first', BOX), bright(boxes['xx'], 'first', BOX)
        for n, word in enumerate(ARABIC_BOX):
            width_px, pixels = joined_line(word)
            left, top = RIGHT_EDGE - width_px - BOX[0], LINE_TOP - BOX[1] + n * 12
            wrong = []
            for y in range(top, top + 12):
                for x in range(16, len(ar[0])):        # right of the scene pack's '.'
                    want = pixels.get((x - left, y - top)) == 1
                    if not scene[y][x] and ar[y][x] != want:
                        wrong.append((x + BOX[0], y + BOX[1]))
                    elif want and not ar[y][x]:
                        wrong.append((x + BOX[0], y + BOX[1]))
            check(not wrong, f'arabic: box line {n + 1} does not light the joined word\'s pixels '
                             f'(first differences at {wrong[:4]}): {boxes["ar"]}')
        print(f'PASS rtl arabic dialogue: {len(ARABIC_BOX)} words drew joined, right to left, ending at the box\'s '
              'right side, pixel for pixel as the letter pictures and the joining rules say')

        steps = [step for step in MENU_STEPS if step[0] <= next(t for t, label, _ in MENU_STEPS if label == 'grid')]
        steps[-1] = (steps[-1][0], 'grid', None)
        work = run_fixed(game, discs, out / 'arabic-menus', steps, pack=arabic['ar'], pad=NEW_GAME)
        title = bright(work, 'grid', TITLE)
        width_px, pixels = joined_line(ARABIC_TITLE)
        lit_px = {(x, y) for y, row in enumerate(title) for x, v in enumerate(row) if v}
        want = {xy for xy, v in pixels.items() if v == 1}
        found = None
        if lit_px and want:
            dx = min(x for x, _ in lit_px) - min(x for x, _ in want)
            dy = min(y for _, y in lit_px) - min(y for _, y in want)
            found = (dx, dy) if {(x + dx, y + dy) for x, y in want} == lit_px else None
        check(found, f'arabic: the name entry title does not light the joined word\'s pixels: {work}')
        print(f'PASS rtl arabic menus: the name entry title drew joined, right to left, pixel for pixel '
              f'(at x {found[0] + TITLE[0]})')

    # A message box line too wide for the screen goes on two lines at full size.
    if 'split' in parts:
        strings = out / 'packs' / 'text' / 'strings.json'
        sample = out / 'packs' / 'split.csv'
        with sample.open('w', encoding='utf-8', newline='') as f:
            w = csv.DictWriter(f, fieldnames=['key', 'translation', 'notes'])
            w.writeheader()
            w.writerows({'key': k, 'translation': v, 'notes': 'test line'} for k, v in SPLIT_TEXT.items())
        path = out / 'packs' / 'renderer-split.pelang'
        built = subprocess.run([sys.executable, '-I', str(localization / 'pelang.py'), 'build', '--from-patch',
                                '--strings', str(strings), '--lang', 'xx', '--name', 'Renderer split', '-o', str(path),
                                str(sample)], check=True, capture_output=True, text=True, encoding='utf-8').stdout
        check('sys/086: split onto two lines to fit' in built and 'sys/086: drawn at' not in built,
              f'the build did not put sys/086 on two lines: {built[-2000:]}')
        made = run(game, discs, out / 'split-card', pad=NEW_GAME, frame_limit=FIRST_BOX + 200,
                   crash_after=FIRST_BOX + 100)
        broken = bytearray(card_from_report(crash_report(made)))
        broken[0x900] ^= 0x5A                       # in the saved game: the checksum no longer matches
        split = ('xx', 'Renderer split', '', path)
        boxes = {}
        for which, kw in (('english', {}), ('split', {'pack': split})):
            boxes[which] = run_fixed(game, discs, out / f'split-{which}', OPEN_STEPS, pad=OPEN_SAVE,
                                     card=bytes(broken), **kw)
            check(lit(boxes[which], 'opened', MESSAGE_BOX) > 200, f'{which}: no box about the save: {boxes[which]}')
        work = boxes['split']
        missing = [line for line in mods_log(work).splitlines() if 'text: ' in line and "shows '?'" in line]
        check(not missing, f'split: letters fell back to "?": {missing[:5]}: {work}')
        english, split_rows = (text_rows(boxes[which], 'opened', MESSAGE_BOX) for which in ('english', 'split'))
        check(len(english) == 3, f'the English box has text rows at {english}, not its two lines and the button: '
              f'{boxes["english"]}')
        # the same title and first line, one more line 14 px under it, and the button 14 px lower
        check(len(split_rows) == 4 and split_rows[:2] == english[:2] and split_rows[2] == english[1] + 14
              and split_rows[3] == english[2] + 14,
              f'the box\'s text rows are at {split_rows}, not English\'s {english} with a line more: {work}')
        print(f'PASS split: the too-wide message box line is on two lines at full size (text rows at {split_rows}, '
              f'English {english})')
    # A pack that draws letters its own way (a fan patch's look).
    if 'drawn' in parts:
        made = make_drawn_packs(localization, out / 'packs')
        boxes = {which: run_fixed(game, discs, out / f'drawn-box-{which}', first, pack=pack, pad=NEW_GAME)
                 for which, pack in made.items()}
        log = mods_log(boxes['drawn'])
        missing = [line for line in log.splitlines() if 'text: ' in line and "shows '?'" in line]
        check(not missing, f'drawn: letters fell back to "?": {missing[:5]}: {boxes["drawn"]}')
        check(f'text: Drawn (xx) draws {len(DRAWN_WIDTHS)} letters with its own pictures and widths' in log,
              f"drawn: the pack's own letters are not logged: {boxes['drawn']}")
        box, scene = bright(boxes['drawn'], 'first', BOX), bright(boxes['scene'], 'first', BOX)
        pixels, _ = drawn_line(DRAWN_BOX, 'box')
        left, top = LINE_LEFT - BOX[0], LINE_TOP - BOX[1]
        wrong = []
        for y in range(top, top + 12):
            for x in range(16, len(box[0])):            # right of the scene pack's '.'
                want = pixels.get((x - left, y - top)) == 1
                if (want or not scene[y][x]) and box[y][x] != want:
                    wrong.append((x + BOX[0], y + BOX[1]))
        check(not wrong, f"drawn: the box line does not light the drawn letters' pixels at their widths "
                         f'(first differences at {wrong[:4]}): {boxes["drawn"]}')
        print(f'PASS drawn dialogue: "{DRAWN_BOX}" lit exactly its own pictures at its own widths, the space pulled '
              "back as English's, é made from the drawn e")
        steps = [step for step in MENU_STEPS if step[0] <= next(t for t, label, _ in MENU_STEPS if label == 'grid')]
        steps[-1] = (steps[-1][0], 'grid', None)
        work = run_fixed(game, discs, out / 'drawn-menus', steps, pack=made['drawn'], pad=NEW_GAME)
        title = bright(work, 'grid', TITLE)
        pixels, _ = drawn_line(DRAWN_TITLE, 'menu')
        lit_px = {(x, y) for y, row in enumerate(title) for x, v in enumerate(row) if v}
        want = {xy for xy, v in pixels.items() if v == 1}
        found = None
        if lit_px and want:
            dx = min(x for x, _ in lit_px) - min(x for x, _ in want)
            dy = min(y for _, y in lit_px) - min(y for _, y in want)
            found = (dx, dy) if {(x + dx, y + dy) for x, y in want} == lit_px else None
        check(found, f'drawn: the name entry title does not light the drawn letters cut by their metrics: {work}')
        print(f'PASS drawn menus: the name entry title "{DRAWN_TITLE}" lit exactly its pictures cut by its metrics '
              f'(at x {found[0] + TITLE[0]})')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main(*sys.argv[1:4]))
    except Failed as failure:
        print(f'FAIL {failure}')
        sys.exit(1)
