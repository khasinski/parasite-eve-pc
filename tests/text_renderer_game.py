"""The text renderer in the real game: language packs' stretches, name letters and saved names.

  python text_renderer_game.py GAME.exe [WORK_DIR [english,packs,name,menus,save,many,menu-letters,rtl,split,drawn,battle,chinese,spanish,italian,german,french,english-pack,translator,translator-pictures,translator-map,translator-battle,translator-cards,translator-tutorial,translator-game-over,translator-look]]

(battle is left out by default: it runs as its own test, original-game-text-renderer-battle; so are
chinese, spanish, italian, german, french and english-pack, original-game-text-renderer-chinese,
-spanish, ...)

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
  battle     Right to left in a battle: the recorded route into the first battle (Melissa), then Escape
             from the battle menu; she can't be run from, so battle/010 shows on the bottom bar. Three
             packs translate only that line, run side by side: Hebrew ("he", with an English name, a
             year and brackets) draws the same letters as its drawing-order mirror ("xx"), moved to end
             at x 300 (the mirror starts at x 20, as English does, and is not English's line); Arabic
             ("ar") lights exactly the pixels of the joined word, ending at x 300.
  chinese    The Chinese pack made from a fan patch (pelang.py build --cjk-letters: its letters are the
             patch's own 12x12 pictures; plain or bundled as players get it), at PE_TEST_ZH_PACK
             (skipped, 77, when it isn't there: the
             patch's text and letters stay out of the repository). A New Game with it reaches the first
             box: the pack loads, its stretches draw and no letter falls back to '?'. The first box's
             two lines light exactly the stroke pixels of the pack's own letter pictures, 12 pixels
             apart, line under line; bright pixels of the scene behind (a pack whose first box is a
             lone '.') are not counted against it.
             On Disc 2 (PE_TEST_DISC2; else 77 after Disc 1): the Spanish part's route, Continue from
             the Day 6 sample save; the game is on Disc 2 after the boxes, no letter falls back to '?',
             and the five boxes on the ship (dlg/M275/008-012) light exactly the pack's letter
             pictures, each line up to {name} (Aya's name is in the game's letters).
             Its pictures (the zipped player file's: the title menu's words and the map's place names,
             which the USA discs hold as the Japanese ones do): the title screen's words are the
             patch's picture, checked as part spanish checks them. The patch redrew no picture on the
             Disc 2 route, so the day's title card stays the game's.
  spanish    The Spanish pack made from the USA-disc fan patch (pelang.py import-patch / build
             --patch-letters: its letters are the patch's own 12x12 pictures at the patch's widths), at
             PE_TEST_ES_PACK, best the zipped player file with its pictures (skipped, 77, when it isn't
             there: the patch's text, letters and pictures stay out of the repository). On Disc 1, a New
             Game: the pack loads, its own letter pictures and stretches are in use, no letter falls back
             to '?', and the first box's lines (the patch's credit page) light exactly the stroke pixels of
             the pack's letter pictures at the pack's widths (the space pulled back 7 px as English's);
             a line holding a button or colour is left out, and the scene behind is not counted against it.
             Five Cross presses on, the game's name entry shows the patch's grid (sys/117: Ñ ñ Ü ü Á É Í Ó Ú
             á é í ó ú ¿ ¡ among the letters, two-byte name letters where English has none): every cell
             lights exactly its letter picture as the menus cut it, 16 px apart; the {g0D} cell is the
             game's own icon. The title screen's words, a picture the patch redrew: the frame with the
             pack on the player's disc is pixel for pixel the frame of a copy of the disc with the pack's
             picture pieces written in (where the patched disc has them; the copy is made and removed
             here) and the pack's text alone, and its words aren't the English ones.
             On Disc 2 (PE_TEST_DISC2; else 77 after Disc 1): Continue from the save editor's sample save F
             (Day 6, in the repository), whose story is past the disc change; the crash report after the
             boxes says the game is on Disc 2. The day's title card is the patch's picture: the frame is
             the one of the real Disc 1 with a pieces-written-in copy of Disc 2 (so it was read from Disc 2)
             and not the text-only pack's. Five boxes on the ship (dlg/M275/008-012, Aya's name put in from
             the save) light exactly the pack's letter pictures at its widths, every other pixel of their
             lines dark (the night behind has none bright there), and no letter falls back to '?'.
             Not checked: later Disc 1 boxes and the status panel (the pack's pages and box endings move
             the route; the first box and the name entry are the fixed moments), and boxes past the first
             page of an entry.
  italian    The same checks for the Italian pack made from the USA-disc fan patch, at PE_TEST_IT_PACK.
             The patch redraws only à è é ì ò ù (each with its own width); the pack draws the game's
             other letters as the game does, so a line is predicted from the pack's six pictures and the
             game's own letters read from the disc. Its first pages are as many as English's, so a New
             Game reaches the name entry by English's route (the grid: the game's letters, its {g4D}
             cell empty). Its pictures are the title screen's; none is on the Disc 2 route, whose frames
             still equal those of Disc 2 with the pieces written in.
             localization/tests/test_it_matches_patch.py checks the pack's text against the patched
             game itself, every row.
  german     The same for the German pack (PE_TEST_DE_PACK), and a help line the patch writes on two lines.
  french     The same for the French pack (PE_TEST_FR_PACK): the patch keeps the game's letters (as the
             Italian), adds its own, and draws its menus' spaces 5 px wide, which the pack's space is in
             the menus: on Disc 2, the load screen's help line (sys/080) must light exactly the pack's
             letters with those spaces. test_fr_matches_patch.py checks the text against the patched game.
  english-pack  The game's own English as a pack (pelang.py english, made here from the player's discs: the
             pack a new language starts from). With it chosen the game must be the game: a New Game
             through the first boxes, the name entry, the boxes up to the free field and the status panel
             (the menus part's route), every kept frame whole and pixel for pixel the frame of the same
             route with no pack, while the log says the pack is the language in use and its text drew
             (no letter falls back to '?'). On Disc 2 (PE_TEST_DISC2; else 77 after Disc 1): the Spanish
             part's route from the Day 6 sample save, the day's title card and the five boxes on the
             ship, again every frame the game's own. localization/tests/test_en_pack.py checks every row
             of the pack against the disc's bytes.
  translator  A file saved by the Translator (localization/translator/): the English pack opened as the
             English, every row typed into a new translation as it is, and saved. The file must hold
             the English pack's own bytes but for the few pages the compiler rewraps to their box,
             carry the Translator's work part, and reopen as saved; and with it chosen the game must
             be the game on the english-pack part's routes, both discs, every frame pixel for pixel.
             So the game opens what the Translator writes, work part and all.
  translator-pictures  Pictures redrawn in the Translator: three title screen pictures saved from
             the disc as PNGs by the tool, redrawn (two 24-bit ones in new colours, the 16-colour one
             with its own palette), loaded back through the window's scripted run (--picture) and
             saved. The file must change those three pictures and no other; and with it chosen, the
             title screen started from each disc must be, pixel for pixel, the title screen of a copy
             of that disc with the file's pieces written in, and not the English one.
  translator-map  The city map's place names redrawn in the Translator: the picture saved from the disc
             as a PNG by the tool, three names redrawn in both their copies (the letters, and the letters
             on the dark band), loaded back through the scripted run (--picture) and saved. The file must
             change that picture and no other, to exactly the PNG's pixels; and with it chosen, the city
             map (Continue from the save editor's Day 5 sample save, one Cross at the day's title card)
             must be, pixel for pixel, the map of a copy of Disc 1 with the file's pieces written in, and
             not the English one. Disc 1 only: no sample save reaches the map on Disc 2 (the Day 5 save
             needs Disc 1; PE.IMG, and so the picture, is byte for byte the same on both discs).
  translator-battle  The battle words redrawn in the Translator: the picture (PE.IMG entry 11: the words,
             numbers and marks of a fight) saved from the disc as a PNG by the tool, every inked pixel's
             grey step turned over (so every word and number drawn from it changes), loaded back
             through the scripted run (--picture) and saved. The file must change that picture and no
             other, to exactly the PNG's pixels; and with it chosen, the first fight (part battle's
             route to Melissa, Aya's time bar full) must be, pixel for pixel, the fight on a copy of
             Disc 1 with the file's pieces written in, and not the English one. Disc 1 only: the route
             starts a New Game, which Disc 1 holds; PE.IMG, and so the picture, is the same on both.
  translator-cards  The day title cards redrawn in the Translator: Days 2-5 (Disc 1) and Day 6 (Disc 2) saved
             from the disc as PNGs by the tool, each with a new dark band of white and red "letters" over
             its word (colours the squares there don't have, so they take other colour sets), loaded
             back through the scripted run (--picture) and saved in one file. The file must change
             those cards and no other picture, each to the PNG's pixels exactly; and Continuing each
             day's sample save (A-D on Disc 1, F on Disc 2), the day's card on the screen must be, pixel
             for pixel, the Translator's picture of it, the card on a copy of the disc with the file's
             pieces written in, and with the file's text alone the Translator's picture of the
             English card.
  translator-tutorial  The Tutorial redrawn in the Translator: all 30 screens (the menu and the 29 pages of
             words) saved from Disc 1 as PNGs by the tool, each with a dark band of white bars and a red
             line over its words (the shadowed top rows of About Icons 5-9 and How to Save included), its
             screenshot (but About Icons 8's, so the Tune-up screenshot loaded from 7's PNG must be kept)
             and the menu's lines, loaded back through the scripted run (--picture) and saved in one file.
             The file must change those screens, as the tool makes them from the PNGs, and no other
             picture; and on the pad route of CTest original-game-tutorial (the whole Tutorial: both
             controllers, icons, battle, save, each menu choice) every screen must be, pixel for pixel,
             the Translator's picture of it (the menu with each choice picked: the copy with its green
             ring), the screen on a copy of Disc 1 with the file's pieces written in, and with the file's
             text alone the Translator's picture of the English screen. Disc 1 only.
  translator-game-over  The Game Over words redrawn in the Translator: saved from Disc 1 as a PNG by the tool,
             wiped and redrawn as pale bars over a red line (red is none of its 16 greens, so the nearest
             is taken), loaded back through the scripted run (--picture) and saved. The file must change
             Game Over, to the PNG's pixels and with its colours as they were, and no other picture; and
             on part battle's route, with Aya's HP set to 1 by the game's own test switch so Melissa fells
             her, two frames of the still Game Over (one at each of its two brightnesses) must be, pixel for
             pixel, the screen on a copy of Disc 1 with the file's pieces written in, every ink pixel of the
             words the Translator's in-game picture of them, and with the file's text alone the
             Translator's English one. Disc 1 only: PE.IMG, and so the picture, is the same on Disc 2.
  translator-look  The Translator's "As in the game" preview (localization/translator/gamelook.py)
             against the game's own frames: the first box with the English pack (made from the discs)
             and with each player pack given (PE_TEST_ES_PACK, _IT_, _DE_, _FR_, _ZH_), and the five
             boxes on the ship on Disc 2: every letter pixel the tool draws is the frame's, and every
             text pixel of the frame is one the tool draws. The first box's room (M010, found from the
             room scripts) is the frame's background in its top rows, pixel for pixel.
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
import typing

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
# Part battle: the recorded route into the first battle (Melissa on the stage), as the actor animation
# test's input 13 (tests/actor_animation/native_replay_schedules.inc): New Game, Cross through the
# boxes and the name entry, the walk to the stage. Then, with Aya's time bar full, Triangle opens the
# battle menu, Down three times reaches "Escape from Battle", Cross: Melissa can't be run from, so the
# game shows battle/010 ("Can't escape!") along the bottom bar, from x 20.
def _battle_route():
    events = list(NEW_GAME)
    press = lambda tick, mask: events.extend([(tick, mask), (tick + 10, 0)])
    for tick in range(17000, 22000, 600):
        press(tick, CROSS)
    press(23000, START)
    press(23600, START)
    for tick in range(24200, 32200, 600):
        press(tick, CROSS)
    events.append((33000, UP))
    for tick in range(35000, 36201, 600):
        events.extend([(tick, UP | CROSS), (tick + 10, UP)])
    events.append((39000, 0))
    for tick in range(52000, 57000, 600):
        press(tick, CROSS)
    events.extend([(58000, UP), (58200, UP), (59769, 32), (59869, 32), (59994, UP), (60634, 128), (60734, 192)])
    for tick in range(61334, 64554, 510):
        press(tick, CROSS)
    return events


BATTLE_ROUTE = _battle_route()
BATTLE_STEPS = presses([(65600, TRIANGLE, 'battle'), (65700, DOWN, 'items'), (65760, DOWN, 'weapon'),
                        (65820, DOWN, 'armor'), (65880, CROSS, 'escape'), (66000, 0, 'line')])
BATTLE_BAR = (0, 190, 320, 220)  # the bottom bar holding the battle line
# The English battle line's letters (bright() of BATTLE_BAR at 'line', SHA-256 of its 0/1 bytes):
# a pack's line must not be this.
ENGLISH_BATTLE = '1c93a2616df09c244885b4192196b2ebac6887789b4e32951f23b61149a34ff7'
BATTLE_HEBREW = 'אין בריחה (Melissa) 1997!'
BATTLE_ARABIC = 'الهروب'
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


def make_battle_packs(localization, out, visual):
    """Hebrew ('he'), its drawing-order mirror ('xx') and Arabic ('ar'), each translating only the
    battle line battle/010, so the route into the battle is the English one."""
    strings = out / 'text' / 'strings.json'
    made = {}
    for code, name, line in (('he', 'Battle Hebrew', BATTLE_HEBREW), ('xx', 'Battle mirror', visual(BATTLE_HEBREW)),
                             ('ar', 'Battle Arabic', BATTLE_ARABIC)):
        table = out / f'battle-{code}.csv'
        with table.open('w', encoding='utf-8', newline='') as f:
            w = csv.DictWriter(f, fieldnames=['key', 'translation', 'notes'])
            w.writeheader()
            w.writerow({'key': 'battle/010', 'translation': line, 'notes': ''})
        pack = out / f'battle-{code}.pelang'
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


def pack_letters(data):
    """A pack's dictionary letters, by index: 144 values each (0 empty, 1 stroke, 2 shadow;
    petext/cjk.py, pe_lang_pack.c). `data` is the pack itself (a zipped one's text.bin)."""
    at, count = struct.unpack_from('<II', data, 64)
    return [[b >> shift & 15 for b in data[at + k * 76 + 4:at + (k + 1) * 76] for shift in (0, 4)]
            for k in range(count)]


def dictionary_lines(raw, letters):
    """A box's first page as lines of the dictionary letters they hold (F1 C5 tokens; F7 starts a
    line, F8 ends the page). Anything else on a line ({name}, a game letter) is None: the line's
    layout is checked only up to it."""
    lines, at = [[]], 0
    while at < len(raw):
        if raw[at:at + 2] == b'\xf1\xc5':
            lines[-1].append(letters[raw[at + 2] * 240 + raw[at + 3]])
            at += 4
        else:
            if raw[at] in (0xF8, 0xF9, 0xFF):
                break
            if raw[at] == 0xF7:
                lines.append([])
            else:
                lines[-1].append(None)
            at += 1
    return lines


def check_dictionary_box(work, label, scene, raw, letters):
    """The default box in `work`'s frame `label` lights exactly the stroke pixels of the pack's
    dictionary letter pictures, 12 px apart, line under line from (LINE_LEFT, LINE_TOP); each line up
    to the first thing that isn't one ({name}, a game letter). A bright pixel of the scene behind
    (`scene`'s frame; None: there is none) is not counted against it. Returns the letters checked."""
    box = bright(work, label, BOX)
    behind = bright(scene, label, BOX) if scene else [[0] * len(row) for row in box]
    left, checked = LINE_LEFT - BOX[0], 0
    for n, line in enumerate(dictionary_lines(raw, letters)):
        line = line[:line.index(None)] if None in line else line
        if not line:
            continue
        top = LINE_TOP - BOX[1] + 12 * n
        want = {(left + 12 * k + p % 12, top + p // 12) for k, cell in enumerate(line)
                for p, v in enumerate(cell) if v == 1}
        check(want, f"{label} line {n + 1}: the pack's letters have no strokes")
        wrong = [(x + BOX[0], y + BOX[1]) for y in range(top, top + 12)
                 for x in range(left, left + 12 * len(line))
                 if ((x, y) in want or not behind[y][x]) and box[y][x] != ((x, y) in want)]
        check(not wrong, f"{label} line {n + 1} does not light the pack's letter pictures "
                         f'(first differences at {wrong[:4]}): {work}')
        checked += len(line)
    return checked


def make_chinese_scene(localization, out):
    """A pack whose first box is a lone '.' at its left: the scene behind the box."""
    table = out / 'chinese-scene.csv'
    with table.open('w', encoding='utf-8', newline='') as f:
        w = csv.DictWriter(f, fieldnames=['key', 'translation', 'notes'])
        w.writeheader()
        w.writerow({'key': 'dlg/M001/009', 'translation': '.', 'notes': ''})
    pack = out / 'chinese-scene.pelang'
    subprocess.run([sys.executable, '-I', str(localization / 'pelang.py'), 'build', '--strings',
                    str(localization / 'data' / 'manifest.json'), '--lang', 'xx', '--name', 'Scene', '-o', str(pack),
                    str(table)], check=True, stdout=subprocess.DEVNULL)
    return ('xx', 'Scene', None, pack)


def chinese_patch(game, discs, out, localization):
    """Part chinese (see the module notes); 77 when PE_TEST_ZH_PACK isn't there."""
    path = os.getenv('PE_TEST_ZH_PACK')
    if not path or not Path(path).is_file():
        print('SKIP chinese: PE_TEST_ZH_PACK is not set or not a file (the Chinese pack from the fan patch)')
        return 77
    sys.path.insert(0, str(localization))
    import pelang  # noqa: E402 (the localization folder is on the path only now)
    path = Path(path).resolve()
    code, name, entries = pelang.read_pack(path)
    pack = (code, name, None, path)
    letters = pack_letters(pelang.pack_bytes(path))     # a plain pack, or the zip players download
    (out / 'packs').mkdir(parents=True, exist_ok=True)
    scene_pack = make_chinese_scene(localization, out / 'packs')
    # The first box only: the next boxes' pages and timing differ from English's with a real translation.
    steps = [(FIRST_BOX, 'first', None)]
    work = run_fixed(game, discs, out / 'chinese', steps, pack=pack, pad=NEW_GAME)
    scene = run_fixed(game, discs, out / 'chinese-scene', steps, pack=scene_pack, pad=NEW_GAME)
    label = f'{name} ({code})'

    def no_placeholders(work, what):
        log = mods_log(work)
        check(f'{label}, ' in log, f'{what}: {label} not loaded: {work}')
        check(f"text: {label}'s stretches in use" in log, f'{what}: no stretch drew with {label}: {work}')
        missing = [line for line in log.splitlines()
                   if 'text: ' in line and ("shows '?'" in line or 'no stretch' in line or 'cache unavailable' in line)]
        check(not missing, f'{what}: letters fell back to "?": {missing[:5]}: {work}')

    no_placeholders(work, 'disc 1')
    raw = entries.get(pelang.fnv1a('dlg/M001/009'))
    check(raw, f'the pack has no dlg/M001/009: {path}')
    checked = check_dictionary_box(work, 'first', scene, raw, letters)
    check(checked >= 20, f'only {checked} Chinese letters checked on screen: {work}')
    print(f'PASS chinese: {label} loaded, its stretches drew, no "?", and the {checked} letters of the first box '
          "lit exactly the patch's letter pictures")

    # Its pictures with words (the zipped player file's): the title screen's "Press Start Button" is the
    # patch's picture.
    import zipfile
    from petext import pictures
    text, pieces, skipped = pelang.pack_bytes(path), [], []
    if text != path.read_bytes():
        with zipfile.ZipFile(path) as z:
            pieces = pictures.read(z.read('pictures.bin')) if 'pictures.bin' in z.namelist() else []
    if not pieces:
        skipped.append('pictures (a plain pack has none; PE_TEST_ZH_PACK names the zipped player file for them)')
    else:
        plain = out / 'packs' / 'chinese-text.pelang'       # the same pack without its pictures
        plain.write_bytes(text)
        check_title_pictures(game, discs, out, 'chinese', label, pack, (code, name, None, plain), pieces, PRESS_START)

    # Disc 2: Continue from the Day 6 save, the boxes on the ship (the patch redrew no picture on this
    # route: the day's title card stays the game's).
    if len(discs) < 2:
        skipped.append('Disc 2 (PE_TEST_DISC2 is not set)')
        print('SKIP chinese: not checked: ' + '; '.join(skipped))
        return 77
    boxes = {frame_label: entries.get(pelang.fnv1a(key)) for frame_label, key in DISC2_BOXES.items()}
    check(all(boxes.values()), f'the pack lacks some of {list(DISC2_BOXES.values())}')
    work, room = run_disc2(game, discs, out / 'chinese-disc2', pack)
    no_placeholders(work, 'disc 2')
    checked = 0
    for frame_label, raw in boxes.items():
        check(lit(work, frame_label, BOX) > 100, f'no box at {frame_label} (has the route changed?): {work}')
        checked += check_dictionary_box(work, frame_label, None, raw, letters)
    check(checked >= 40, f'only {checked} letters of the Disc 2 boxes checked: {work}')
    print(f'PASS chinese disc 2: Continue from a Day 6 save plays on Disc 2 (in room {room} after the boxes); '
          f"no \"?\", and the {checked} letters of {len(boxes)} boxes on the ship lit exactly the patch's letter "
          'pictures')
    if skipped:
        print('SKIP chinese: not checked: ' + '; '.join(skipped))
        return 77
    return 0


# ---- Part spanish: the real Spanish fan pack on both USA discs ----

# The title screen's words (a picture): "Press Start Button", or the patch's "Pulsa Start".
TITLE_AT = 9600
PRESS_START = (60, 180, 260, 200)
# A New Game with the Spanish pack: the first box (dlg/M001/009, the patch's credit page) at FIRST_BOX,
# then Cross through its next two pages, a dark moment and the profile box; the game's name entry
# is open at NAME_ENTRY_AT (found by playing the pack once: its pages differ from English's).
SPANISH_STEPS = presses([(FIRST_BOX, CROSS, 'first'), (18310, CROSS, 'page2'), (18620, CROSS, 'page3'),
                         (18930, CROSS, 'dark'), (19240, CROSS, 'profile'), (19550, 0, 'grid')])
GRID_CELLS = (72, 70, 250, 218)   # all nine rows of the name entry's grid, 16 px apart
HELP_LINE = (8, 16, 260, 50)      # the load screen's help line at the top, left of the clock
GRID_PITCH, GRID_COLUMNS = 16, 11
# Disc 2: the save editor's sample save F (save-editor/samples, in the repository; a save holds no
# disc data), Day 6 on the ship (M275). Its story is past the point where the game changes discs,
# so Continue mounts Disc 2. After the load: the day's title card (a picture the
# patch redrew), two dark scenes, then the boxes below, one Cross each. Any pack's Disc 2 check can
# use this route (disc2_steps(), DISC2_BOXES).
DISC2_SAVE = Path(__file__).resolve().parent.parent / 'save-editor' / 'samples' / 'bu00' / 'BASLUS-006620000000F'


def disc2_steps():
    steps = loaded_steps()[:-3]                 # Continue from card 1, slot 1, up to the loaded game
    loaded = steps[-1][0]
    steps[-1] = (loaded, 'loaded', 'advance 600 0')
    labels = ['day-card', 'ship', 'deck'] + [f'box{n:03d}' for n in range(8, 13)]
    return steps + presses([(loaded + 600 + 300 * k, CROSS, label) for k, label in enumerate(labels)]
                           + [(loaded + 600 + 300 * len(labels), 0, 'after')])


DISC2_BOXES = {f'box{n:03d}': f'dlg/M275/{n:03d}' for n in range(8, 13)}   # "Daniel: ¡{name}!" ...
# The Italian pack's first pages are as many as English's, so English's route (MENU_STEPS) reaches
# its name entry.
ITALIAN_STEPS = MENU_STEPS[:MENU_STEPS.index((22450, 'grid', f'advance 10 {CROSS}'))] + [(22450, 'grid', None)]


class FanPack(typing.NamedTuple):
    """What part spanish and part italian check for one fan patch's pack (see the module notes)."""
    part: str
    env: str
    steps: list                      # a New Game to the name entry, its frame kept as 'grid'
    drawn_at_least: int              # letters the pack draws with the patch's pictures
    grid_letters: str                # letters the patch's name grid must offer
    game_letters: bool               # the patch draws the game's other letters as the game does
    title_words: tuple | None        # where the title screen's words change (None: somewhere)
    disc2_picture: str | None        # a frame of the Disc 2 route showing a picture the patch redrew
    first_box_letters: int = 60      # letters of the first box (dlg/M001/009) at least
    menu_space: bool = False         # the pack draws its space its own width in the menus


SPANISH = FanPack('spanish', 'PE_TEST_ES_PACK', SPANISH_STEPS, 60, 'ÑñÜüÁÉÍÓÚáéíóú¿¡', False, PRESS_START,
                  'day-card')
# The Italian patch redraws only à è é ì ò ù; its pictures are the title screen's and the opening's,
# none on the Disc 2 route.
ITALIAN = FanPack('italian', 'PE_TEST_IT_PACK', ITALIAN_STEPS, 6, '', True, None, None)
# The German patch redraws every letter and adds ä ö ü Ä Ö Ü ß and an up arrow; its first pages
# are as many screens as Italian's, so English's route reaches its name entry. Its first box is
# shorter ("Mann: Was ist los?").
GERMAN = FanPack('german', 'PE_TEST_DE_PACK', ITALIAN_STEPS, 80, 'ÄÖÜäöüß↑', False, None, 'day-card', 40)
# The French patch keeps the game's letters and adds its own accented ones, ( ) № and …; it writes many
# letter pairs with one code, which the pack writes out (so as many letters of the first box are
# checked as English draws). The game counts 17 letters redrawn (the space and 16 accented ones;
# its ( ) № … are letters of the pack's own table). Its menus' spaces are 5 px: the pack's space is, in the menus (checked on
# the load screen's help line on the Disc 2 route).
FRENCH = FanPack('french', 'PE_TEST_FR_PACK', ITALIAN_STEPS, 17, 'çéèêëàâäîïôöùûü', True, None, 'day-card',
                 menu_space=True)


def run_disc2(game, discs, work, pack):
    """The Disc 2 route (disc2_steps()) with `pack`: Continue from DISC2_SAVE; checks the game is on
    Disc 2 after the boxes (its crash report). Returns `work` and the room it is in then."""
    check(DISC2_SAVE.is_file(), f'no sample save {DISC2_SAVE}')
    route = disc2_steps()
    end = route[-1][0]
    steps = route[:-1] + [(end, 'after', 'advance 200 0')]
    driver = fixed(steps)
    run(game, discs, work, pack=pack, pad=CONTINUE, card=DISC2_SAVE.read_bytes(), start=steps[0][0], driver=driver,
        frame_limit=end + 300, crash_after=end + 100)
    check(not driver.steps, f'the run stopped before vblank {driver.steps[0][0] if driver.steps else 0}: {work}')
    where = re.search(r'Game: room (\d+), .*?, disc (\d+),', crash_report(work))
    check(where and where[2] == '2', f'the game is not on Disc 2 after the boxes ({where[0] if where else "?"}): '
                                     f'{work}')
    return work, where[1]


def drawn_records(data):
    """A v4 pack's letters drawn with its own pictures (pelang.py build --patch-letters): code ->
    (dialogue advance, menu metrics byte, 144 values: 0 empty, 1 stroke, 2 shadow)."""
    at, count = struct.unpack_from('<II', data, 80)
    out = {}
    for k in range(count):
        r = data[at + k * 80:at + (k + 1) * 80]
        if r[6] == 0xFF:                        # a picture of its own, not a game letter plus marks
            out[r[4]] = (r[5], r[7], [b >> shift & 15 for b in r[8:80] for shift in (0, 4)])
    return out


def pack_box_line(codes, drawn):
    """{(x, row): 1 or 2} of a box line in the pack's own letters from its pen's start, as the box draws
    them (each picture at the pen, the pen moved by the letter's advance, the space pulled back 7 px as
    the English box pulls it: see the drawn part); None when a letter isn't the pack's drawing."""
    pixels, pen = {}, 0
    for code in codes:
        if code not in drawn:
            return None
        advance, _, cell = drawn[code]
        for p, v in enumerate(cell):
            if v:
                pixels[(pen + p % 12, p // 12)] = v
        pen += advance - (7 if code == 0x0F else 0)
    return pixels


def menu_letter(cell, metrics, digit):
    """{(x, row): 1} of one letter as the menus draw it: cut by its metrics byte (width << 4 | first
    column), its first column at the pen, a digit half a spacing further right than a letter (see
    drawn_line, whose letters start at column 1)."""
    first, width = metrics & 15, metrics >> 4
    x0 = (2 if digit else 1) // 2 - first
    return {(x0 + p % 12, p // 12) for p, v in enumerate(cell) if v == 1 and first <= p % 12 < first + width}


def box_pages(raw, name):
    """The first page of a dialogue entry as lines of one-byte codes, {name} (FA) put in as `name`'s
    codes; a two-byte control stays in the line (so the line isn't predicted)."""
    lines = [[]]
    for b in raw.replace(b'\xfa', name):
        if b == 0xF8 or b in (0xF9, 0xFF):        # {wait}: the next page
            break
        if b == 0xF7:
            lines.append([])
        else:
            lines[-1].append(b)
    return lines


def check_pack_box(work, label, scene, raw, drawn, name=b''):
    """The default box in `work`'s frame `label` lights exactly the stroke pixels of the pack's own
    letters at their widths, line under line from (LINE_LEFT, LINE_TOP); a bright pixel of the scene
    behind (`scene`'s frame, None: none) is not counted against it. Lines holding anything but the
    pack's letters (a colour or button) are left out. Returns the letters checked."""
    box = bright(work, label, BOX)
    behind = bright(scene, label, BOX) if scene else [[0] * len(row) for row in box]
    checked = 0
    for n, codes in enumerate(box_pages(raw, name)):
        pixels = pack_box_line(codes, drawn) if codes else None
        if not pixels:
            continue
        left, top = LINE_LEFT - BOX[0], LINE_TOP - BOX[1] + 12 * n
        want = {(left + x, top + y) for (x, y), v in pixels.items() if v == 1}
        right = max(x for x, _ in pixels) + 1
        wrong = [(x + BOX[0], y + BOX[1]) for y in range(top, min(top + 12, len(box))) for x in range(left, right)
                 if ((x, y) in want or not behind[y][x]) and box[y][x] != ((x, y) in want)]
        check(not wrong, f"{label}: line {n + 1} does not light the pack's own letters at their widths "
                         f'(first differences at {wrong[:4]}): {work}')
        checked += len(codes)
    return checked


def pieces_written_in(cue, pieces, out):
    """A copy of a disc with the pack's picture pieces written into PE.IMG, where a patched disc has
    them (each piece only where its English bytes are, as pe_lang puts it in). Returns the copy's
    cue sheet and how many pieces went in."""
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'localization'))
    from petext.disc import Game, _cue_bin
    import zlib
    game = Game(cue)
    try:
        if game.disc.sector_size != 2352:
            raise Failed(f'{cue} is not a raw 2352-byte sector image')
        base, offset = game.pe_img[0], game.disc.data_offset
        writes = [((base + p.sector) * 2352 + offset + p.offset, p.data) for p in pieces
                  if zlib.crc32(game.pe(p.sector, 1)[p.offset:p.offset + len(p.data)]) == p.original_crc]
    finally:
        game.disc.f.close()
    out.mkdir(parents=True, exist_ok=True)
    image = out / 'pieces-written-in.bin'
    shutil.copyfile(_cue_bin(Path(cue)), image)
    with image.open('r+b') as f:
        for at, data in writes:
            f.seek(at)
            f.write(data)
    sheet = out / 'pieces-written-in.cue'
    sheet.write_text(f'FILE "{image.name}" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n')
    return str(sheet), len(writes)


def check_title_pictures(game, discs, out, part, label, pack, plain_pack, pieces, title_words, disc=0):
    """The title screen with the pack on the player's disc against the game on a copy of the disc with
    the pack's picture pieces written in (as a patched disc holds them) and the pack's text alone
    (`plain_pack`): the same frame, pixel for pixel; with the text alone on the player's disc, the
    English words (`title_words`: where they are, or None for anywhere on the frame). `disc`: which
    of `discs` the game starts from (0: Disc 1)."""
    log_pieces = re.compile(rf'{re.escape(label)}, \d+ entries, {len(pieces)} picture pieces')
    number, tag = disc + 1, '' if disc == 0 else f'-disc{disc + 1}'
    try:
        written = [pieces_written_in(discs[disc], pieces, out / 'discs' / f'disc{number}')]
        check(written[0][1] == len(pieces), f'only {written[0][1]} of {len(pieces)} pieces land on Disc {number}')
        title = [(TITLE_AT, 'title', None)]
        mine = discs[disc:disc + 1]
        frames = {which: run_fixed(game, use, out / f'{part}-title{tag}-{which}', title, pack=p)
                  for which, use, p in (('pack', mine, pack), ('written-in', [written[0][0]], plain_pack),
                                        ('text-only', mine, plain_pack))}
        check(log_pieces.search(mods_log(frames['pack'])) and 'its pictures are in use' in mods_log(frames['pack']),
              f'the pack\'s {len(pieces)} picture pieces were not loaded and put in: {frames["pack"]}')
        check(lit(frames['text-only'], 'title', PRESS_START) > 100, f'no title screen at vblank {TITLE_AT}: '
                                                                      f'{frames["text-only"]}')
        check(frame(frames['pack'], 'title') == frame(frames['written-in'], 'title'),
              f'the title screen with the pack is not the one of the disc with its pieces written in: '
              f'{frames["pack"]} vs {frames["written-in"]}')
        words = (lambda w: crop(w, 'title', title_words)) if title_words else (lambda w: frame(w, 'title'))
        check(words(frames['pack']) != words(frames['text-only']),
              f'the title screen\'s words are still English: {frames["pack"]}')
        print(f'PASS {part} pictures, disc {number}: the {len(pieces)} pieces loaded; the title screen\'s words '
              "are the pack's picture, pixel for pixel as on the disc with the pieces written in")
    finally:
        shutil.rmtree(out / 'discs', ignore_errors=True)


def game_letters(cue, localization):
    """The game's own letters on `cue` as drawn_records() gives a pack's: code -> (dialogue advance,
    menu metrics byte, 144 values), for the codes below 0x4D."""
    sys.path.insert(0, str(localization))
    from petext import font, gametext as gt, patchimport
    from petext.disc import Game
    disc = Game(cue)
    try:
        pal, width, _, px = font.page(disc, font.PAGE0_ENTRY)
        metrics = patchimport.menu_metrics(disc)
        return {code: (gt.ADVANCE[code], metrics[code], list(patchimport._layered(pal, font.cell(px, width, code))))
                for code in range(0x4D)}
    finally:
        disc.disc.f.close()


def spanish_patch(game, discs, out, localization):
    return fan_patch(game, discs, out, localization, SPANISH)


def italian_patch(game, discs, out, localization):
    return fan_patch(game, discs, out, localization, ITALIAN)


def german_patch(game, discs, out, localization):
    return fan_patch(game, discs, out, localization, GERMAN)


def french_patch(game, discs, out, localization):
    return fan_patch(game, discs, out, localization, FRENCH)


def same_frames(plain, packed, labels, what):
    """Every kept frame of the run with the pack is, whole and pixel for pixel, the run's without."""
    for label in labels:
        a, b = frame(plain, label), frame(packed, label)
        if a != b:
            width, rgb_a, rgb_b = a[0], a[2], b[2]
            at = next((i // 3 for i in range(0, min(len(rgb_a), len(rgb_b)), 3) if rgb_a[i:i + 3] != rgb_b[i:i + 3]), 0)
            raise Failed(f'{what}: frame "{label}" with the pack is not the game\'s own (first '
                         f'difference at x {at % width}, y {at // width}): {packed}')


def make_english_pack(discs, out, localization):
    """(the English pack `pelang.py english` makes from the discs, the rows it holds, the game's rows)."""
    text = out / 'text'
    subprocess.run([sys.executable, '-I', str(localization / 'extract_text.py'), *discs, '--out', str(text)],
                   check=True, stdout=subprocess.DEVNULL)
    path = out / 'en.pelang'
    made = subprocess.run([sys.executable, '-I', str(localization / 'pelang.py'), 'english',
                           str(text / 'strings.json'), '-o', str(path)], capture_output=True, text=True)
    check(made.returncode == 0, f'pelang.py english failed: {made.stdout[-600:]}{made.stderr[-600:]}')
    rows = re.search(r'(\d+) of (\d+) rows', made.stdout)
    check(rows and int(rows[1]) >= 2800, f'the English pack holds too few rows: {made.stdout[:300]}')
    return path, rows[1], rows[2]


def game_is_its_own(game, discs, out, pack, tag, what):
    """With `pack` (the game's own text) chosen, every kept frame of the menus part's route, and of
    the Disc 2 route, is the frame without it. 77 after the Disc 1 checks when PE_TEST_DISC2 isn't set."""
    name = f'{pack[1]} ({pack[0]})'

    def in_use(work, where):
        log = mods_log(work)
        check(f'pe_lang: language: {name}' in log, f'{where}: {pack[1]} is not the language in use: {work}')
        check(f"text: {name}'s stretches in use" in log, f"{where}: none of the pack's text drew: {work}")
        missing = [line for line in log.splitlines()
                   if 'text: ' in line and ("shows '?'" in line or 'no stretch' in line)]
        check(not missing, f'{where}: letters fell back to "?": {missing[:5]}: {work}')

    plain = run_fixed(game, discs, out / f'{tag}-off', MENU_STEPS, pad=NEW_GAME)
    packed = run_fixed(game, discs, out / f'{tag}-on', MENU_STEPS, pack=pack, pad=NEW_GAME)
    in_use(packed, 'Disc 1')
    check(lit(plain, 'first', BOX) > 200 and lit(plain, 'grid', GRID) > 200 and lit(plain, 'status', STATUS_NAME) > 20,
          f'the route did not reach the first box, the name entry and the status panel: {plain}')
    labels = [label for _, label, _ in MENU_STEPS]
    same_frames(plain, packed, labels, 'Disc 1')
    print(f'PASS {what}, Disc 1: a New Game through the first boxes, the name entry and the status panel is the '
          f'game\'s own, all {len(labels)} frames pixel for pixel')
    if len(discs) < 2:
        print(f'SKIP {what} on Disc 2: PE_TEST_DISC2 is not set')
        return 77
    plain, _ = run_disc2(game, discs, out / f'{tag}-disc2-off', None)
    packed, room = run_disc2(game, discs, out / f'{tag}-disc2-on', pack)
    in_use(packed, 'Disc 2')
    labels = [label for _, label, _ in disc2_steps()]
    check(all(lit(plain, label, BOX) > 100 for label in DISC2_BOXES), f"the ship's boxes are not on screen: {plain}")
    same_frames(plain, packed, labels, 'Disc 2')
    print(f'PASS {what}, Disc 2 (room {room}): the day\'s title card and the five boxes on the ship are the '
          f'game\'s own, all {len(labels)} frames pixel for pixel')
    return 0


def english_pack(game, discs, out, localization):
    """Part english-pack (see the module notes); 77 after the Disc 1 checks when PE_TEST_DISC2 isn't set."""
    path, held, total = make_english_pack(discs, out, localization)
    print(f'english pack: {held} of {total} rows, each byte for byte the game\'s')
    return game_is_its_own(game, discs, out, ('en', 'English pack', None, path), 'english-pack', 'english pack')


def translator_file(game, discs, out, localization):
    """Part translator (see the module notes); 77 after the Disc 1 checks when PE_TEST_DISC2 isn't set."""
    path, _, _ = make_english_pack(discs, out, localization)
    import zipfile
    sys.path.insert(0, str(localization))
    import pelang
    from translator import project
    rows = project.Game()
    english, wrong = project.read_english(path, rows)
    check(wrong == 0 and len(english) >= 2800, f'the Translator reads {wrong} rows of the English pack wrong')
    work = project.Translation(rows, 'en', 'Translator file')
    for key, text in english.items():
        work.set_text(key, text)
    saved = out / 'translator.pelang'
    report = work.save(saved)
    check(not report.left_out and not report.whole,
          f'the Translator left rows of the game\'s own text out: {list(report.left_out.items())[:5]} {report.whole}')
    with zipfile.ZipFile(saved) as z:
        check(project.WORK in z.namelist() and 'text.bin' in z.namelist(), f'not a Translator file: {z.namelist()}')
    # It holds the game's own bytes, but for the pages the compiler rewrapped (English that runs past its box).
    was, now = pelang.read_pack(path)[2], pelang.read_pack(saved)[2]
    changed = sorted(rows.by_hash[h] for h in was if h in rows.by_hash and now.get(h) != was[h])
    wrapped = sorted(k for k, said in report.warnings.items() if any('wrapped to fit' in w for w in said))
    check(set(now) == set(was) and changed == wrapped and len(changed) < 10,
          f'the Translator\'s file differs from the English pack in {changed[:10]}; rewrapped: {wrapped[:10]}')
    again = project.Translation.open(saved, rows)
    check(again.from_work and again.texts == english and not again.locked, 'the file does not reopen as it was saved')
    print(f'translator file: {report.in_pack} rows saved by the Translator, {len(now) - len(changed)} entries byte for '
          f'byte the English pack\'s, {len(changed)} pages rewrapped to their box ({", ".join(changed)}); reopens as saved')
    return game_is_its_own(game, discs, out, ('en', 'Translator file', None, saved), 'translator', 'translator file')


def translator_pictures(game, discs, out, localization):
    """Part translator-pictures (see the module notes); 77 after the Disc 1 checks when PE_TEST_DISC2
    isn't set."""
    import json
    import zipfile
    sys.path.insert(0, str(localization))
    import pelang
    from petext import pictures
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        print('SKIP translator pictures: needs Pillow (the Translator reads and writes PNGs with it)')
        return 77
    from translator import artwork
    disc = artwork.Disc(discs[0])
    # Each picture saved from the disc by the Translator, redrawn as a person would in a paint
    # program: the 24-bit words on black in new colours, the 16-colour line with its own colours.
    redraw = {'title/press-start': (255, 220, 40), 'title/new-game': (80, 200, 255), 'title/published-by': None}
    loads = []
    for pid, colour in redraw.items():
        pic = artwork.BY_ID[pid]
        saved = out / f'{pid.replace("/", "-")}.png'
        saved.write_bytes(artwork.png(pic, disc.english[pid]))
        img = Image.open(saved)
        if colour:
            img = img.convert('RGB')
            draw = ImageDraw.Draw(img)
            draw.rectangle((0, 0, img.width - 1, img.height - 1), fill=(0, 0, 0))
            for x in range(4, img.width - 4, 6):
                draw.rectangle((x, 5, x + 2, img.height - 6), fill=colour)
        else:
            draw = ImageDraw.Draw(img)                 # a palette picture: drawn with palette number 15
            draw.rectangle((8, 8, img.width // 2, 20), fill=15)
        img.save(saved)
        loads += ['--picture', f'{pid}={saved}']
    path, report = out / 'translator-pictures.pelang', out / 'translator-pictures-report.json'
    ran = subprocess.run([sys.executable, '-I', str(localization / 'translate.py'), str(path), '--new', 'es',
                          'Pictures test', '--settings', str(out / 'translator-settings.json'), '--report', str(report),
                          '--disc', discs[0], *loads], capture_output=True, text=True, timeout=300)
    said = json.loads(report.read_text(encoding='utf-8')) if report.exists() else {}
    check(ran.returncode == 0 and said.get('ok'), f'the Translator did not save the pictures: {said} {ran.stderr[-600:]}')
    with zipfile.ZipFile(path) as z:
        pieces = pictures.read(z.read('pictures.bin'))
    for pid in redraw:
        check(artwork.redrawn(disc, artwork.BY_ID[pid], pieces), f'{pid} is not in the saved file')
    others = [p.id for p in artwork.PICTURES if p.id not in redraw and artwork.redrawn(disc, p, pieces)]
    check(not others, f'pictures nobody loaded changed: {others}')
    plain = out / 'translator-pictures-text.pelang'           # the same file's text alone
    plain.write_bytes(pelang.pack_bytes(path))
    pack, text_only = ('es', 'Pictures test', None, path), ('es', 'Pictures test', None, plain)
    print(f'translator pictures: the Translator saved {len(redraw)} redrawn pictures as {len(pieces)} pieces')
    for n in range(len(discs)):
        check_title_pictures(game, discs, out, 'translator-pictures', 'Pictures test (es)', pack, text_only, pieces,
                             PRESS_START, disc=n)
    if len(discs) < 2:
        print('SKIP translator pictures on Disc 2: PE_TEST_DISC2 is not set')
        return 77
    return 0


# ---- Part translator-map: the city map's place names ----

# The save editor's sample save D (save-editor/samples, in the repository; a save holds no disc data):
# Day 5. After the load the day's title card waits for Cross; then the city map (scene MAP_SCENE) is
# on screen from about vblank 16400 and waits for a choice, eight place names showing (Subway and
# Chrysler BLDG. are not offered that day), N.Y.P.D. #17 in gold as the place Aya is at.
MAP_SAVE = Path(__file__).resolve().parent.parent / 'save-editor' / 'samples' / 'bu00' / 'BASLUS-006620000000D'
MAP_SCENE = 0xA8000048
MAP_AT = 16800
# Rows of the picture (13 each, from 0) redrawn by part translator-map: Chinatown, N.Y.P.D. #17 (the
# gold one that day) and Central Park.
MAP_REDRAWN = (1, 7, 8)


def map_steps():
    steps = loaded_steps()[:-3]                 # Continue from card 1, slot 1, up to the loaded game
    return steps + presses([(16200, CROSS, 'day-card'), (MAP_AT, 0, 'map')])


def scene_at(work, vblank):
    """The scene the game was in at that control point (its game.log)."""
    for line in (work / 'game.log').read_text(encoding='utf-8', errors='replace').splitlines():
        if line.startswith('{"event":"control"'):
            event = json.loads(line)
            if event['vblanks'] == vblank:
                return event['scene']
    return None


def translator_map(game, discs, out, localization):
    """Part translator-map (see the module notes). Disc 1 only."""
    import zipfile
    sys.path.insert(0, str(localization))
    import pelang
    from petext import pictures, tim as timlib
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        print('SKIP translator map: needs Pillow (the Translator reads and writes PNGs with it)')
        return 77
    from translator import artwork
    check(MAP_SAVE.is_file(), f'no sample save {MAP_SAVE}')
    disc = artwork.Disc(discs[0])
    pic = artwork.BY_ID['map/place-names']
    # Saved from the disc by the Translator and redrawn as a person would in a paint program: in each
    # redrawn name's row, the letters copy (left) and the dark band copy (right) painted over with
    # the picture's own colours, then kept as an ordinary RGB PNG.
    saved = out / 'map-place-names.png'
    saved.write_bytes(artwork.png(pic, disc.english[pic.id]))
    img = Image.open(saved)
    check(img.mode == 'P' and img.size == (256, 143), f'the saved map picture is {img.mode} {img.size}')
    draw = ImageDraw.Draw(img)
    for row in MAP_REDRAWN:
        top = row * 13
        draw.rectangle((0, top, 126, top + 11), fill=0)
        draw.rectangle((128, top, 254, top + 11), fill=0)
        for x in range(2, 110, 9):                      # new "letters": bars, and under them the band
            draw.rectangle((x, top + 2, x + 4, top + 10), fill=15)
            draw.rectangle((128 + x, top + 1, 128 + x + 7, top + 11), fill=5)
            draw.rectangle((128 + x, top + 2, 128 + x + 4, top + 10), fill=15)
    img.convert('RGB').save(saved)
    expected = artwork.from_png(pic, disc.english[pic.id], saved.read_bytes()).tim
    path, report = out / 'translator-map.pelang', out / 'translator-map-report.json'
    ran = subprocess.run([sys.executable, '-I', str(localization / 'translate.py'), str(path), '--new', 'es',
                          'Map test', '--settings', str(out / 'translator-settings.json'), '--report', str(report),
                          '--disc', discs[0], '--picture', f'{pic.id}={saved}'], capture_output=True, text=True,
                         timeout=300)
    said = json.loads(report.read_text(encoding='utf-8')) if report.exists() else {}
    check(ran.returncode == 0 and said.get('ok'), f'the Translator did not save the map picture: {said} '
                                                 f'{ran.stderr[-600:]}')
    with zipfile.ZipFile(path) as z:
        pieces = pictures.read(z.read('pictures.bin'))
    shown = artwork.shown(disc, pic, pieces)
    check(shown == expected, "the saved map picture is not the PNG's pixels")
    now, was = timlib.indices(timlib.read(shown)), timlib.indices(timlib.read(disc.english[pic.id]))
    changed = sorted({k // 256 // 13 for k, (a, b) in enumerate(zip(now, was)) if a != b})
    check(changed == list(MAP_REDRAWN), f'the saved map picture changed rows {changed}, not {list(MAP_REDRAWN)}')
    others = [p.id for p in artwork.PICTURES if p.id != pic.id and artwork.redrawn(disc, p, pieces)]
    check(not others, f'pictures nobody loaded changed: {others}')
    plain = out / 'translator-map-text.pelang'                # the same file's text alone
    plain.write_bytes(pelang.pack_bytes(path))
    pack, text_only = ('es', 'Map test', None, path), ('es', 'Map test', None, plain)
    print(f'translator map: the Translator saved the redrawn place names (rows {list(MAP_REDRAWN)}) as '
          f'{len(pieces)} pieces')
    log_pieces = re.compile(rf'Map test \(es\), \d+ entries, {len(pieces)} picture pieces')
    try:
        written, landed = pieces_written_in(discs[0], pieces, out / 'discs' / 'disc1')
        check(landed == len(pieces), f'only {landed} of {len(pieces)} pieces land on Disc 1')
        steps = map_steps()
        frames = {which: run_fixed(game, use, out / f'translator-map-{which}', steps, pack=p, pad=CONTINUE,
                                   card=MAP_SAVE.read_bytes())
                  for which, use, p in (('pack', discs[:1], pack), ('written-in', [written], text_only),
                                        ('text-only', discs[:1], text_only))}
        for which, work in frames.items():
            check(scene_at(work, MAP_AT) == MAP_SCENE, f'{which}: the city map is not on screen at vblank {MAP_AT}: '
                                                       f'{work}')
        check(log_pieces.search(mods_log(frames['pack'])) and 'its pictures are in use' in mods_log(frames['pack']),
              f"the file's {len(pieces)} picture pieces were not loaded and put in: {frames['pack']}")
        check(frame(frames['pack'], 'map') == frame(frames['written-in'], 'map'),
              f'the city map with the file is not the one of the disc with its pieces written in: '
              f"{frames['pack']} vs {frames['written-in']}")
        _, _, mine = frame(frames['pack'], 'map')
        _, _, english = frame(frames['text-only'], 'map')
        differ = sum(1 for k in range(0, len(mine), 3) if mine[k:k + 3] != english[k:k + 3])
        check(differ > 300, f"the city map's place names are still English ({differ} pixels differ): "
                            f"{frames['pack']}")
        print(f"PASS translator map, disc 1: the {len(pieces)} pieces loaded; the city map's place names are the "
              f"file's picture, pixel for pixel as on the disc with the pieces written in ({differ} pixels differ "
              'from the English map)')
    finally:
        shutil.rmtree(out / 'discs', ignore_errors=True)
    return 0


# ---- Part translator-battle: the battle words ----

FIGHT_AT = 65600                # part battle's route: in the Melissa fight, Aya's time bar full


def translator_battle(game, discs, out, localization):
    """Part translator-battle (see the module notes). Disc 1 only."""
    import zipfile
    sys.path.insert(0, str(localization))
    import pelang
    from petext import pictures, tim as timlib
    try:
        from PIL import Image
    except ImportError:
        print('SKIP translator battle: needs Pillow (the Translator reads and writes PNGs with it)')
        return 77
    from translator import artwork
    disc = artwork.Disc(discs[0])
    pic = artwork.BY_ID['battle/words']
    # Saved from the disc by the Translator and redrawn in a paint program: every inked pixel's grey step
    # turned over (step n to 16 - n; 0, the see-through black, stays), kept as an ordinary RGB PNG.
    saved = out / 'battle-words.png'
    saved.write_bytes(artwork.png(pic, disc.english[pic.id]))
    img = Image.open(saved)
    check(img.mode == 'P' and img.size == (256, 32), f'the saved battle words picture is {img.mode} {img.size}')
    img = img.point(lambda n: 16 - n if 0 < n < 16 else n)
    img.convert('RGB').save(saved)
    expected = artwork.from_png(pic, disc.english[pic.id], saved.read_bytes()).tim
    path, report = out / 'translator-battle.pelang', out / 'translator-battle-report.json'
    ran = subprocess.run([sys.executable, '-I', str(localization / 'translate.py'), str(path), '--new', 'es',
                          'Battle test', '--settings', str(out / 'translator-settings.json'), '--report', str(report),
                          '--disc', discs[0], '--picture', f'{pic.id}={saved}'], capture_output=True, text=True,
                         timeout=300)
    said = json.loads(report.read_text(encoding='utf-8')) if report.exists() else {}
    check(ran.returncode == 0 and said.get('ok'), f'the Translator did not save the battle words: {said} '
                                                 f'{ran.stderr[-600:]}')
    with zipfile.ZipFile(path) as z:
        pieces = pictures.read(z.read('pictures.bin'))
    shown = artwork.shown(disc, pic, pieces)
    check(shown == expected, "the saved battle words are not the PNG's pixels")
    now, was = timlib.indices(timlib.read(shown)), timlib.indices(timlib.read(disc.english[pic.id]))
    check(all(a == (16 - b if b else 0) for a, b in zip(now, was)), 'the saved battle words are not turned over')
    check(timlib.read(shown).palettes == timlib.read(disc.english[pic.id]).palettes, 'the colours changed')
    others = [p.id for p in artwork.PICTURES if p.id != pic.id and artwork.redrawn(disc, p, pieces)]
    check(not others, f'pictures nobody loaded changed: {others}')
    plain = out / 'translator-battle-text.pelang'             # the same file's text alone
    plain.write_bytes(pelang.pack_bytes(path))
    pack, text_only = ('es', 'Battle test', None, path), ('es', 'Battle test', None, plain)
    print(f'translator battle: the Translator saved the redrawn battle words as {len(pieces)} pieces')
    log_pieces = re.compile(rf'Battle test \(es\), \d+ entries, {len(pieces)} picture pieces')
    try:
        written, landed = pieces_written_in(discs[0], pieces, out / 'discs' / 'disc1')
        check(landed == len(pieces), f'only {landed} of {len(pieces)} pieces land on Disc 1')
        steps = presses([(FIGHT_AT, 0, 'fight')])
        from concurrent.futures import ThreadPoolExecutor
        with ThreadPoolExecutor(max_workers=3) as pool:
            runs = {which: pool.submit(run_fixed, game, use, out / f'translator-battle-{which}', steps, pack=p,
                                       pad=BATTLE_ROUTE)
                    for which, use, p in (('pack', discs[:1], pack), ('written-in', [written], text_only),
                                          ('text-only', discs[:1], text_only))}
            frames = {which: run.result() for which, run in runs.items()}
        check(log_pieces.search(mods_log(frames['pack'])) and 'its pictures are in use' in mods_log(frames['pack']),
              f"the file's {len(pieces)} picture pieces were not loaded and put in: {frames['pack']}")
        check(frame(frames['pack'], 'fight') == frame(frames['written-in'], 'fight'),
              f'the fight with the file is not the one on the disc with its pieces written in: '
              f"{frames['pack']} vs {frames['written-in']}")
        _, _, mine = frame(frames['pack'], 'fight')
        _, _, english = frame(frames['text-only'], 'fight')
        differ = sum(1 for k in range(0, len(mine), 3) if mine[k:k + 3] != english[k:k + 3])
        check(differ > 100, f'the fight still shows the English battle words ({differ} pixels differ): '
                            f"{frames['pack']}")
        print(f"PASS translator battle, disc 1: the {len(pieces)} pieces loaded; the fight draws the file's battle "
              f'words, pixel for pixel as on the disc with the pieces written in ({differ} pixels differ from the '
              'English fight)')
    finally:
        shutil.rmtree(out / 'discs', ignore_errors=True)
    return 0


# ---- Part translator-cards: the day title cards ----

SAMPLES = Path(__file__).resolve().parent.parent / 'save-editor' / 'samples' / 'bu00'
# Card, sample save (save-editor/samples: a save holds no disc data), disc (0: Disc 1): Continue shows
# the day's card from before CARD_AT until Cross.
CARDS = (('day/2', 'A', 0), ('day/3', 'B', 0), ('day/4', 'C', 0), ('day/5', 'D', 0), ('day/6', 'F', 1))
CARD_AT = 16190


def translator_cards(game, discs, out, localization):
    """Part translator-cards (see the module notes). Day 6 needs Disc 2."""
    import zipfile
    from concurrent.futures import ThreadPoolExecutor
    sys.path.insert(0, str(localization))
    import pelang
    from petext import pictures
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        print('SKIP translator cards: needs Pillow (the Translator reads and writes PNGs with it)')
        return 77
    from translator import artwork
    cards = [c for c in CARDS if c[2] < len(discs)]
    if len(cards) < len(CARDS):
        print('translator cards: no PE_TEST_DISC2, Day 6 (Disc 2) left out')
    disc = artwork.Disc(discs[0])
    args, expected = [], {}
    for pid, _, _ in cards:
        pic, layout = artwork.BY_ID[pid], disc.layout[pid]
        # Saved by the Translator and redrawn in a paint program: over the word at the bottom left, a
        # dark band with white bars and a red line (colours the squares there don't have), saved as RGB.
        saved = out / f"card-{pid.replace('/', '-')}.png"
        saved.write_bytes(artwork.png(pic, disc.english[pid], layout))
        img = Image.open(saved)
        check(img.size == (320, 224), f'the saved card {pid} is {img.size}')
        img = img.convert('RGB')
        draw = ImageDraw.Draw(img)
        draw.rectangle((36, 158, 196, 186), fill=(8, 8, 16))
        for x in range(42, 190, 10):
            draw.rectangle((x, 163, x + 5, 180), fill=(255, 255, 255))
        draw.line((40, 183, 192, 183), fill=(230, 20, 20), width=2)
        img.save(saved)
        expected[pid] = artwork.from_png(pic, disc.english[pid], saved.read_bytes(), None, layout)
        check(any('took another set of colours' in n for n in expected[pid].notes),
              f'no square of {pid} took another colour set: {expected[pid].notes}')
        args += ['--picture', f'{pid}={saved}']
    path, report = out / 'translator-cards.pelang', out / 'translator-cards-report.json'
    ran = subprocess.run([sys.executable, '-I', str(localization / 'translate.py'), str(path), '--new', 'es',
                          'Cards test', '--settings', str(out / 'translator-settings.json'), '--report', str(report),
                          '--disc', discs[0], *args], capture_output=True, text=True, timeout=600)
    said = json.loads(report.read_text(encoding='utf-8')) if report.exists() else {}
    check(ran.returncode == 0 and said.get('ok'), f'the Translator did not save the cards: {said} {ran.stderr[-600:]}')
    with zipfile.ZipFile(path) as z:
        pieces = pictures.read(z.read('pictures.bin'))
    for pid, _, _ in cards:
        check(artwork.shown(disc, artwork.BY_ID[pid], pieces) == expected[pid].tim,
              f"the saved card {pid} is not the PNG's pixels")
    redrawn = {pid for pid, _, _ in cards}
    others = [p.id for p in artwork.PICTURES if p.id not in redrawn and artwork.redrawn(disc, p, pieces)]
    check(not others, f'pictures nobody loaded changed: {others}')
    plain = out / 'translator-cards-text.pelang'              # the same file's text alone
    plain.write_bytes(pelang.pack_bytes(path))
    pack, text_only = ('es', 'Cards test', None, path), ('es', 'Cards test', None, plain)
    print(f'translator cards: the Translator saved {len(cards)} redrawn day cards as {len(pieces)} pieces')
    log_pieces = re.compile(rf'Cards test \(es\), \d+ entries, {len(pieces)} picture pieces')
    steps = loaded_steps()[:-3] + presses([(CARD_AT, 0, 'card')])
    try:
        written = {}
        for k in sorted({c[2] for c in cards}):
            written[k], landed = pieces_written_in(discs[k], pieces, out / 'discs' / f'disc{k + 1}')
            check(landed == len(pieces), f'only {landed} of {len(pieces)} pieces land on Disc {k + 1}')
        jobs = {}
        with ThreadPoolExecutor(max_workers=6) as pool:
            for pid, save, k in cards:
                card = (SAMPLES / f'BASLUS-006620000000{save}').read_bytes()
                copy = list(discs[:k + 1])
                copy[k] = written[k]
                for which, use, p in (('pack', discs[:k + 1], pack), ('written-in', copy, text_only),
                                      ('text-only', discs[:k + 1], text_only)):
                    jobs[(pid, which)] = pool.submit(run_fixed, game, use, out / f"cards-{pid.replace('/', '-')}-{which}",
                                                     steps, pack=p, pad=CONTINUE, card=card)
            works = {key: job.result() for key, job in jobs.items()}
        for pid, save, k in cards:
            pic, layout = artwork.BY_ID[pid], disc.layout[pid]
            frames = {which: frame(works[(pid, which)], 'card') for which in ('pack', 'written-in', 'text-only')}
            mine = artwork.image(pic, expected[pid].tim, layout).tobytes()
            english = artwork.image(pic, disc.english[pid], layout).tobytes()
            log = mods_log(works[(pid, 'pack')])
            check(log_pieces.search(log) and 'its pictures are in use' in log,
                  f"{pid}: the file's {len(pieces)} picture pieces were not loaded and put in: {works[(pid, 'pack')]}")
            check(frames['text-only'][2] == english,
                  f"{pid}: with the text alone the screen is not the Translator's picture of the English card: "
                  f"{works[(pid, 'text-only')]}")
            check(frames['pack'] == frames['written-in'],
                  f'{pid}: the card with the file is not the one on the disc with its pieces written in: '
                  f"{works[(pid, 'pack')]} vs {works[(pid, 'written-in')]}")
            check(frames['pack'][2] == mine, f"{pid}: the card with the file is not the Translator's picture of it: "
                                             f"{works[(pid, 'pack')]}")
            differ = sum(1 for i in range(0, len(mine), 3) if mine[i:i + 3] != english[i:i + 3])
            print(f"PASS translator cards, {pid} (save {save}, Disc {k + 1}): the card on the screen is the "
                  f"Translator's picture of it, pixel for pixel, and the one on the disc with the pieces written in "
                  f"({differ} pixels differ from the English card)")
    finally:
        shutil.rmtree(out / 'discs', ignore_errors=True)
    return 0


# ---- Part translator-tutorial: the Tutorial's screens ----

# The pad route of CTest original-game-tutorial (tests/actor_animation/test_native_replays.inc,
# original_native_tutorial: real input through the whole Tutorial), read from that file so the two stay one.
REPLAYS = Path(__file__).resolve().parent / 'actor_animation' / 'test_native_replays.inc'
# The screen on show at each of the route's control points kept (index: picture id, # the menu's choice),
# found by matching the game's frames with the Translator's pictures (every page, 0 pixels differing).
TUTORIAL_AT = {0: 'tutorial/menu#1', 2: 'tutorial/controller-1', 3: 'tutorial/controller-2',
               5: 'tutorial/controller-3', 7: 'tutorial/controller-4', 9: 'tutorial/controller-5',
               11: 'tutorial/controller-6', 15: 'tutorial/analog-1', 17: 'tutorial/analog-2', 19: 'tutorial/analog-3',
               21: 'tutorial/analog-4', 23: 'tutorial/analog-5', 25: 'tutorial/analog-6', 27: 'tutorial/analog-7',
               68: 'tutorial/menu#2', 71: 'tutorial/icons-1', 72: 'tutorial/icons-2', 74: 'tutorial/icons-3',
               76: 'tutorial/icons-4', 80: 'tutorial/icons-5', 81: 'tutorial/icons-6', 83: 'tutorial/icons-7',
               85: 'tutorial/icons-8', 87: 'tutorial/icons-9', 92: 'tutorial/menu#3', 95: 'tutorial/battle-1',
               96: 'tutorial/battle-2', 98: 'tutorial/battle-3', 100: 'tutorial/battle-4', 102: 'tutorial/battle-5',
               105: 'tutorial/battle-6', 109: 'tutorial/menu#4', 112: 'tutorial/save', 115: 'tutorial/menu#5'}


def tutorial_route():
    """(pad presses before the first control point, the commands at the control points, the first one's
    vblank, the frame limit) of original_native_tutorial."""
    body = REPLAYS.read_text(encoding='utf-8')
    body = body[body.index('static int original_native_tutorial'):]
    body = body[:body.index('SDL_Environment')]
    text = ''.join(re.findall(r'"([^"]*)"', body[body.index('const char *commands='):]))
    commands = [c for c in text.split('\\n') if c]
    pads = [(int(a), int(b, 0)) for a, b in re.findall(r'"--pad-at","(\d+):(0x[0-9a-fA-F]+|\d+)"', body)]
    start = int(re.search(r'"--control-at","(\d+)"', body)[1])
    limit = int(re.search(r'"--frame-limit","(\d+)"', body)[1])
    check(len(commands) == 123 and commands[-1] == 'quit' and len(pads) == 6,
          f'the route of original_native_tutorial is not the one this part was made for: {len(commands)} commands')
    return pads, commands, start, limit


def tutorial_driver(commands):
    """Keeps the frame at each control point of TUTORIAL_AT, then gives the route's command."""
    state = {'i': 0}

    def drive(vblank, work):
        i = state['i']
        state['i'] += 1
        if i in TUTORIAL_AT:
            keep(work, f'c{i:03}')
        return commands[i] if i < len(commands) and commands[i] != 'quit' else None
    drive.state = state
    return drive


def tutorial_redraw(img, pid):
    """A paint program's redrawing of a Tutorial screen saved by the Translator: over the words at the
    bottom (the shadowed top rows of About Icons 5-9 and How to Save included) a dark band with white bars
    and a red line, colours the squares there mostly don't have; the same over the screenshot (and the Game
    Over screen), but not on About Icons 8, whose Tune-up screenshot (shown on 7 as well) stays as the
    game has it in its PNG, so 7's change must be kept; and over the menu's lines."""
    from PIL import ImageDraw
    draw = ImageDraw.Draw(img)

    def band(x0, y0, x1, y1):
        draw.rectangle((x0, y0, x1, y1), fill=(8, 8, 16))
        for x in range(x0 + 6, x1 - 8, 10):
            draw.rectangle((x, y0 + 3, x + 5, y1 - 6), fill=(255, 255, 255))
        draw.line((x0 + 4, y1 - 3, x1 - 4, y1 - 3), fill=(230, 20, 20), width=2)

    if pid == 'tutorial/menu':
        band(80, 70, 240, 100)
        band(80, 130, 240, 160)
        return
    band(20, 152, 300, 186)
    if pid.startswith(('tutorial/icons', 'tutorial/battle')) and pid != 'tutorial/icons-8':
        band(90, 50, 230, 80)


def translator_tutorial(game, discs, out, localization):
    """Part translator-tutorial (see the module notes). Disc 1 only (the user's choice: the Tutorial is
    on both discs, PE.IMG the same bytes)."""
    import zipfile
    from concurrent.futures import ThreadPoolExecutor
    sys.path.insert(0, str(localization))
    import pelang
    from petext import pictures
    try:
        from PIL import Image
    except ImportError:
        print('SKIP translator tutorial: needs Pillow (the Translator reads and writes PNGs with it)')
        return 77
    from translator import artwork
    disc = artwork.Disc(discs[0])
    screens = [p for p in artwork.PICTURES if isinstance(p, artwork.Screen)]
    check(len(screens) == 30, f'{len(screens)} Tutorial screens, not 30')
    args, pngs = [], {}
    for pic in screens:
        saved = out / f"{pic.id.replace('/', '-')}.png"
        saved.write_bytes(artwork.png(pic, disc.english[pic.id], disc.layout[pic.id]))
        img = Image.open(saved)
        check(img.size == (320, 224), f'the saved screen {pic.id} is {img.size}')
        img = img.convert('RGB')
        tutorial_redraw(img, pic.id)
        img.save(saved)
        pngs[pic.id] = saved.read_bytes()
        args += ['--picture', f'{pic.id}={saved}']
    # What the Translator must make of them, loaded in the same order here.
    expected = []
    for pic in screens:
        layout = disc.layout[pic.id]
        loaded = artwork.from_png(pic, disc.english[pic.id], pngs[pic.id], artwork.shown(disc, pic, expected), layout)
        if pic.id == 'tutorial/icons-8':
            check(any('the change loaded there is kept' in n for n in loaded.notes),
                  f"About Icons 8's PNG did not keep 7's Tune-up screenshot: {loaded.notes}")
        expected = artwork.put(disc, pic, expected, loaded.tim)
    path, report = out / 'translator-tutorial.pelang', out / 'translator-tutorial-report.json'
    ran = subprocess.run([sys.executable, '-I', str(localization / 'translate.py'), str(path), '--new', 'es',
                          'Tutorial test', '--settings', str(out / 'translator-settings.json'), '--report', str(report),
                          '--disc', discs[0], *args], capture_output=True, text=True, timeout=1200)
    said = json.loads(report.read_text(encoding='utf-8')) if report.exists() else {}
    check(ran.returncode == 0 and said.get('ok'), f'the Translator did not save the Tutorial: {said} {ran.stderr[-600:]}')
    with zipfile.ZipFile(path) as z:
        pieces = pictures.read(z.read('pictures.bin'))
    for pic in screens:
        check(artwork.shown(disc, pic, pieces) == artwork.shown(disc, pic, expected),
              f'the saved screen {pic.id} is not what its PNG makes')
        check(artwork.redrawn(disc, pic, pieces), f'the saved screen {pic.id} is still the English one')
    others = [p.id for p in artwork.PICTURES if not isinstance(p, artwork.Screen) and artwork.redrawn(disc, p, pieces)]
    check(not others, f'pictures nobody loaded changed: {others}')
    seven, eight = (artwork.BY_ID[f'tutorial/icons-{k}'] for k in (7, 8))
    shot = (60, 4, 268, 148)
    check(artwork.image(eight, artwork.shown(disc, eight, pieces), disc.layout[eight.id]).crop(shot).tobytes() ==
          artwork.image(seven, artwork.shown(disc, seven, pieces), disc.layout[seven.id]).crop(shot).tobytes(),
          "About Icons 8's Tune-up screenshot is not the one loaded from 7's PNG")
    plain = out / 'translator-tutorial-text.pelang'           # the same file's text alone
    plain.write_bytes(pelang.pack_bytes(path))
    pack, text_only = ('es', 'Tutorial test', None, path), ('es', 'Tutorial test', None, plain)
    print(f'translator tutorial: the Translator saved {len(screens)} redrawn Tutorial screens as {len(pieces)} pieces')
    log_pieces = re.compile(rf'Tutorial test \(es\), \d+ entries, {len(pieces)} picture pieces')
    pads, commands, start, limit = tutorial_route()
    try:
        written, landed = pieces_written_in(discs[0], pieces, out / 'discs' / 'disc1')
        check(landed == len(pieces), f'only {landed} of {len(pieces)} pieces land on Disc 1')
        runs = {}
        with ThreadPoolExecutor(max_workers=3) as pool:
            for which, use, p in (('pack', discs[:1], pack), ('written-in', [written], text_only),
                                  ('text-only', discs[:1], text_only)):
                driver = tutorial_driver(commands)
                runs[which] = (driver, pool.submit(run, game, use, out / f'tutorial-{which}', pack=p, pad=pads,
                                                   start=start, driver=driver, frame_limit=limit))
            works = {which: job.result() for which, (_, job) in runs.items()}
        for which, (driver, _) in runs.items():
            check(driver.state['i'] >= max(TUTORIAL_AT) + 1,
                  f'{which}: the run stopped at control point {driver.state["i"]}: {works[which]}')
        log = mods_log(works['pack'])
        check(log_pieces.search(log) and 'its pictures are in use' in log,
              f"the file's {len(pieces)} picture pieces were not loaded and put in: {works['pack']}")

        def screen_of(f, name):
            w, h, rgb = f
            check(w == 320 and h >= 224, f'{name}: a frame of {w}x{h}')
            return rgb[:320 * 224 * 3]

        for i, name in sorted(TUTORIAL_AT.items()):
            pid, _, choice = name.partition('#')
            pic, layout = artwork.BY_ID[pid], disc.layout[pid]
            label = f'c{i:03}'
            frames = {which: frame(works[which], label) for which in works}
            mine = artwork.screen_image(artwork.shown(disc, pic, pieces), layout, int(choice) if choice else None)
            english = artwork.screen_image(disc.english[pid], layout, int(choice) if choice else None)
            check(screen_of(frames['text-only'], name) == english.tobytes(),
                  f"{name}: with the text alone the screen is not the Translator's picture of the English one: "
                  f"{works['text-only']} {label}")
            check(frames['pack'] == frames['written-in'],
                  f"{name}: the screen with the file is not the one on the disc with its pieces written in: "
                  f"{works['pack']} vs {works['written-in']} {label}")
            check(screen_of(frames['pack'], name) == mine.tobytes(),
                  f"{name}: the screen with the file is not the Translator's picture of it: {works['pack']} {label}")
            a, b = mine.tobytes(), english.tobytes()
            differ = sum(1 for k in range(0, len(a), 3) if a[k:k + 3] != b[k:k + 3])
            print(f"PASS translator tutorial, {name} (control point {i}): the screen is the Translator's picture "
                  f'of it, pixel for pixel, and the one on the disc with the pieces written in ({differ} pixels '
                  'differ from the English screen)')
    finally:
        shutil.rmtree(out / 'discs', ignore_errors=True)
    return 0


# ---- Part translator-game-over: the Game Over words ----

# Part battle's route to the Melissa fight, where Aya's HP is 35 at FIGHT_AT; the game's own test switch sets
# it to 1 there, and Melissa's next hit fells her: no Preraise, no Revive, so Battle_StepPostBattle plays the
# game over. The words stand still at x 100-220, y 100-124 from vblank 66164 to 66221 (found frame by frame),
# drawn with brightness 120 and 116 on alternate frames (the two draw buffers); these two are one of each.
GAME_OVER_HP = f'{FIGHT_AT}:35:1'
GAME_OVER_AT = (66200, 66202)


def translator_game_over(game, discs, out, localization):
    """Part translator-game-over (see the module notes). Disc 1 only (the user's choice: PE.IMG, and so the
    picture, is the same bytes on Disc 2)."""
    import zipfile
    from concurrent.futures import ThreadPoolExecutor
    sys.path.insert(0, str(localization))
    import pelang
    from petext import pictures
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        print('SKIP translator game over: needs Pillow (the Translator reads and writes PNGs with it)')
        return 77
    from translator import artwork
    disc = artwork.Disc(discs[0])
    pic = artwork.BY_ID['game-over']
    english = disc.english[pic.id]
    # Saved by the Translator and redrawn in a paint program: the words wiped to black (see-through), then
    # five pale bars and a red line under them (red is none of its 16 greens: the nearest is taken).
    saved = out / 'game-over.png'
    saved.write_bytes(artwork.png(pic, english))
    img = Image.open(saved)
    check(img.mode == 'P' and img.size == (120, 24), f'the saved Game Over is {img.mode} {img.size}')
    pale = tuple(img.getpalette()[3:6])                         # its colour 1, the palest green
    img = img.convert('RGB')
    draw = ImageDraw.Draw(img)
    draw.rectangle((0, 0, 119, 23), fill=(0, 0, 0))
    for x in range(4, 110, 22):
        draw.rectangle((x, 3, x + 13, 16), fill=pale)
    draw.line((2, 20, 117, 20), fill=(230, 20, 20), width=2)
    img.save(saved)
    expected = artwork.from_png(pic, english, saved.read_bytes())
    check(any('not one of the picture' in n for n in expected.notes), f'no colour was fitted: {expected.notes}')
    path, report = out / 'translator-game-over.pelang', out / 'translator-game-over-report.json'
    ran = subprocess.run([sys.executable, '-I', str(localization / 'translate.py'), str(path), '--new', 'es',
                          'Game Over test', '--settings', str(out / 'translator-settings.json'), '--report', str(report),
                          '--disc', discs[0], '--picture', f'{pic.id}={saved}'], capture_output=True, text=True,
                         timeout=600)
    said = json.loads(report.read_text(encoding='utf-8')) if report.exists() else {}
    check(ran.returncode == 0 and said.get('ok'), f'the Translator did not save Game Over: {said} {ran.stderr[-600:]}')
    with zipfile.ZipFile(path) as z:
        pieces = pictures.read(z.read('pictures.bin'))
    check(artwork.shown(disc, pic, pieces) == expected.tim, "the saved Game Over is not the PNG's pixels")
    check(expected.tim[-32:] == english[-32:], 'the colours changed')
    others = [p.id for p in artwork.PICTURES if p.id != pic.id and artwork.redrawn(disc, p, pieces)]
    check(not others, f'pictures nobody loaded changed: {others}')
    plain = out / 'translator-game-over-text.pelang'           # the same file's text alone
    plain.write_bytes(pelang.pack_bytes(path))
    pack, text_only = ('es', 'Game Over test', None, path), ('es', 'Game Over test', None, plain)
    print(f'translator game over: the Translator saved the redrawn Game Over as {len(pieces)} pieces')
    log_pieces = re.compile(rf'Game Over test \(es\), \d+ entries, {len(pieces)} picture pieces')
    steps = presses([(FIGHT_AT, 0, 'fight')] + [(at, 0, f'still-{at}') for at in GAME_OVER_AT])
    try:
        written, landed = pieces_written_in(discs[0], pieces, out / 'discs' / 'disc1')
        check(landed == len(pieces), f'only {landed} of {len(pieces)} pieces land on Disc 1')
        with ThreadPoolExecutor(max_workers=3) as pool:
            runs = {which: pool.submit(run_fixed, game, use, out / f'translator-game-over-{which}', steps, pack=p,
                                       pad=BATTLE_ROUTE, hp=GAME_OVER_HP)
                    for which, use, p in (('pack', discs[:1], pack), ('written-in', [written], text_only),
                                          ('text-only', discs[:1], text_only))}
            works = {which: run.result() for which, run in runs.items()}
        log = mods_log(works['pack'])
        check(log_pieces.search(log) and 'its pictures are in use' in log,
              f"the file's {len(pieces)} picture pieces were not loaded and put in: {works['pack']}")
        x0, y0 = pic.at
        w, h = pic.size
        ink = {which: [n != 0 for n in artwork.glyph_numbers(pic, data)]
               for which, data in (('mine', expected.tim), ('english', english))}

        def box(f):
            width, _, rgb = f
            return [tuple(rgb[((y0 + y) * width + x0 + x) * 3:((y0 + y) * width + x0 + x) * 3 + 3])
                    for y in range(h) for x in range(w)]

        def rgb_list(img):
            data = img.tobytes()
            return [tuple(data[k:k + 3]) for k in range(0, len(data), 3)]

        seen = set()
        for at in GAME_OVER_AT:
            label = f'still-{at}'
            frames = {which: frame(works[which], label) for which in works}
            check(frames['pack'] == frames['written-in'],
                  f"{label}: the screen with the file is not the one on the disc with its pieces written in: "
                  f"{works['pack']} vs {works['written-in']}")
            plain_box, my_box = box(frames['text-only']), box(frames['pack'])
            # The brightness of this frame: the one at which the English words are the frame's, every ink pixel.
            fits = [b for b in pic.brightness
                    if all(p == q for p, q, i in zip(plain_box, rgb_list(artwork.in_game(pic, english, b)),
                                                     ink['english']) if i)]
            check(len(fits) == 1, f"{label}: with the text alone the words are not the Translator's English picture "
                                  f"at brightness 120 or 116 ({fits}): {works['text-only']}")
            b = fits[0]
            seen.add(b)
            mine = rgb_list(artwork.in_game(pic, expected.tim, b))
            wrong = sum(1 for p, q, i in zip(my_box, mine, ink['mine']) if i and p != q)
            check(wrong == 0, f"{label}: {wrong} of the words' pixels on the screen with the file are not the "
                              f"Translator's picture at brightness {b}: {works['pack']}")
            same = sum(1 for p, q, i, j in zip(my_box, plain_box, ink['mine'], ink['english']) if not i and not j and p != q)
            check(same == 0, f'{label}: {same} see-through pixels show something else than the English screen does')
            differ = sum(1 for p, q in zip(my_box, plain_box) if p != q)
            print(f"PASS translator game over, vblank {at} (brightness {b}): the words on the screen are the "
                  f"Translator's picture of them, pixel for pixel, and the screen is the one on the disc with the "
                  f"pieces written in ({differ} pixels of the box differ from the English screen)")
        check(seen == set(pic.brightness), f'the frames kept show only brightness {seen}')
    finally:
        shutil.rmtree(out / 'discs', ignore_errors=True)
    return 0


def look_is_the_frame(work, label, look, font, raw, name, gl):
    """The Translator's game look (translator/gamelook.py) of `raw` against the game's frame: every
    pixel it draws is the frame's, and every pixel of the frame's text area in the letters' own
    colours is one it draws (a scene pixel or two of those colours aside). Returns the pixels drawn."""
    width, height, rgb = frame(work, label)
    check((width, height) == (gl.W, gl.H), f'{label}: a {width} x {height} frame, not the game screen: {work}')
    placed = look.screens[0]
    over = gl.draw(font, placed, rgb)
    alone = gl.draw(font, placed, bytes(gl.W * gl.H * 3))
    changed = [(k // 3 % gl.W, k // 3 // gl.W) for k in range(0, len(rgb), 3) if over[k:k + 3] != rgb[k:k + 3]]
    check(not changed, f"{label}: the game look's letters are not the frame's at {changed[:6]} "
                       f'({len(changed)} pixels): {work}')
    inks = {bytes(gl.rgb_of(font.clut[1]))}          # the letters' strokes (their dark shadow is in scenes too)
    missed = [(x, y) for y in range(160, gl.H) for x in range(gl.W)
              if rgb[(y * gl.W + x) * 3:(y * gl.W + x) * 3 + 3] in inks
              and alone[(y * gl.W + x) * 3:(y * gl.W + x) * 3 + 3] == b'\0\0\0']
    check(len(missed) <= 3, f"{label}: the frame has text the game look doesn't draw at {missed[:6]} "
                            f'({len(missed)} pixels): {work}')
    drawn = sum(1 for k in range(0, len(alone), 3) if alone[k:k + 3] != b'\0\0\0')
    check(drawn > 300, f'{label}: the game look drew only {drawn} pixels: {work}')
    return drawn


def translator_look(game, discs, out, localization):
    """Part translator-look (see the module notes); 77 after the Disc 1 checks when PE_TEST_DISC2 isn't
    set, or when none of the player packs is given."""
    sys.path.insert(0, str(localization))
    import pelang
    try:
        import PIL  # noqa: F401
    except ImportError:
        print('SKIP translator look: needs Pillow (the game look makes its picture with it)')
        return 77
    from translator import gamelook as gl
    disc = gl.DiscLook(discs[0])
    shown = disc.shown_in()
    check(shown.get('dlg/M001/009') == [10], f'the first box is not shown in room M010: {shown.get("dlg/M001/009")}')
    english, _, _ = make_english_pack(discs, out, localization)

    def look_of(path, key, name='Aya'):
        pack = gl.Pack.read(pelang.pack_bytes(path))
        font = gl.Font(disc, pack)
        raw = pack.text(key)
        check(raw, f'{path.name} has no {key}')
        return gl.lay_out(font, raw, gl.name_bytes(pack, name)), font, raw

    first = [(FIRST_BOX, 'first', None)]
    work = run_fixed(game, discs, out / 'look-english', first, pack=('en', 'English pack', None, english), pad=NEW_GAME)
    look, font, raw = look_of(english, 'dlg/M001/009')
    drawn = look_is_the_frame(work, 'first', look, font, raw, 'Aya', gl)
    # The room behind it: the scene's top rows (no one stands there), every pixel the room picture's.
    _, _, rgb = frame(work, 'first')
    views = disc.views(10)
    check(views and views[0][:40 * gl.W * 3] == rgb[:40 * gl.W * 3],
          f"the top of room M010's picture is not the frame's: {work}")
    print(f'PASS translator look, English: the first box drawn by the Translator is the game\'s frame, all {drawn} '
          "pixels of its letters; room M010's picture is the frame's background")

    # "à" (code 4D, a 0 in the game's own width table): the language mod draws it 8 px wide, and the
    # pack compiler must measure every line as the game draws it.
    sheet = out / 'a-grave.csv'
    with sheet.open('w', encoding='utf-8', newline='') as f:
        w = csv.DictWriter(f, fieldnames=['key', 'translation', 'notes'])
        w.writeheader()
        w.writerow({'key': 'dlg/M001/009', 'translation': 'Çà et là: voilà, à à à!{br}àààà, déjà.', 'notes': ''})
    grave = out / 'a-grave.pelang'
    made = subprocess.run([sys.executable, '-I', str(localization / 'pelang.py'), 'build', '--strings',
                           str(localization / 'data' / 'manifest.json'), '--lang', 'fr', '--name', 'A grave', '-o',
                           str(grave), str(sheet)], capture_output=True, text=True)
    check(made.returncode == 0, f'the "à" pack did not build: {made.stdout[-400:]}{made.stderr[-400:]}')
    work = run_fixed(game, discs, out / 'look-a-grave', first, pack=('fr', 'A grave', None, grave), pad=NEW_GAME)
    look, font, raw = look_of(grave, 'dlg/M001/009')
    drawn = look_is_the_frame(work, 'first', look, font, raw, 'Aya', gl)
    lines = {}
    for p in look.screens[0]:
        lines.setdefault(p.y, []).append(p)
    ends = [ps[-1].x - ps[-1].letter.x + ps[-1].letter.advance - gl.LINE_X for _, ps in sorted(lines.items())]
    from petext import gametext as gt
    measured = gt.lines(pelang.read_pack(grave)[2][pelang.fnv1a('dlg/M001/009')].rstrip(b'\xff\xf9'))
    check(ends == measured, f'the compiler measures the "à" lines {measured}, the game draws them {ends}')
    print(f'PASS translator look, "à": the game draws it as the Translator does (all {drawn} pixels), and the '
          f'compiler measures its lines as drawn ({ends} px)')

    skipped = []
    for env, label in (('PE_TEST_ES_PACK', 'Spanish'), ('PE_TEST_IT_PACK', 'Italian'), ('PE_TEST_DE_PACK', 'German'),
                       ('PE_TEST_FR_PACK', 'French'), ('PE_TEST_ZH_PACK', 'Chinese')):
        path = Path(os.getenv(env) or '')
        if not path.is_file():
            skipped.append(f'{label} ({env} is not set)')
            continue
        code, name, _ = pelang.read_pack(path)
        work = run_fixed(game, discs, out / f'look-{code}', first, pack=(code, name, None, path), pad=NEW_GAME)
        look, font, raw = look_of(path, 'dlg/M001/009')
        drawn = look_is_the_frame(work, 'first', look, font, raw, 'Aya', gl)
        print(f'PASS translator look, {label}: the first box with the pack\'s own letters is the game\'s frame, all '
              f'{drawn} pixels')

    if len(discs) < 2:
        skipped.append('Disc 2 (PE_TEST_DISC2 is not set)')
    else:
        work, room = run_disc2(game, discs, out / 'look-disc2', ('en', 'English pack', None, english))
        total = 0
        for label, key in DISC2_BOXES.items():
            look, font, raw = look_of(english, key)
            total += look_is_the_frame(work, label, look, font, raw, 'Aya', gl)
        print(f'PASS translator look, Disc 2 (room {room}): the {len(DISC2_BOXES)} boxes on the ship drawn by the '
              f'Translator are the game\'s frames, all {total} pixels')
    if skipped:
        print('SKIP translator look: not checked: ' + '; '.join(skipped))
        return 77
    return 0


def menu_line(codes, drawn):
    """{(x, row)} of the stroke pixels of a menu line from its pen's start, as the menus draw it: each
    letter cut by its metrics (menu_letter), the pen moved by its drawn width and 1 px, 2 px after a
    digit or the space (text_render.c MenuMeasure)."""
    pixels, pen = set(), 0
    for code in codes:
        _, metrics, picture = drawn[code]
        narrow = code <= 9 or code == 0x0F
        pixels |= {(pen + x, y) for x, y in menu_letter(picture, metrics, narrow)}
        pen += (metrics >> 4) + (2 if narrow else 1)
    return pixels


def fan_patch(game, discs, out, localization, fan):
    """Part spanish or italian (see the module notes); 77 when the pack (fan.env) isn't there, and
    after the Disc 1 checks when PE_TEST_DISC2 isn't set or the pack has no pictures (a plain pack)."""
    part = fan.part
    path = os.getenv(fan.env)
    if not path or not Path(path).is_file():
        print(f'SKIP {part}: {fan.env} is not set or not a file (the {part.title()} pack from the fan patch)')
        return 77
    sys.path.insert(0, str(localization))
    import pelang  # noqa: E402 (the localization folder is on the path only now)
    import zipfile
    from petext import pictures
    path = Path(path).resolve()
    code, name, entries = pelang.read_pack(path)
    label = f'{name} ({code})'
    text = pelang.pack_bytes(path)
    own = drawn_records(text)
    check(len(own) >= fan.drawn_at_least, f'{path}: only {len(own)} letters drawn with its own pictures; '
                                          "not this fan patch's pack")
    drawn = {**game_letters(discs[0], localization), **own} if fan.game_letters else own
    pack = (code, name, None, path)
    (out / 'packs').mkdir(parents=True, exist_ok=True)
    plain = out / 'packs' / f'{part}-text.pelang'       # the same pack without its pictures
    plain.write_bytes(text)
    plain_pack = (code, name, None, plain)
    pieces = []
    if text != path.read_bytes():
        with zipfile.ZipFile(path) as z:
            pieces = pictures.read(z.read('pictures.bin')) if 'pictures.bin' in z.namelist() else []
    skipped = []

    def no_placeholders(work, what):
        log = mods_log(work)
        check(f'{label}, ' in log, f'{what}: {label} not loaded: {work}')
        missing = [line for line in log.splitlines()
                   if 'text: ' in line and ("shows '?'" in line or 'no stretch' in line or 'cache unavailable' in line)]
        check(not missing, f'{what}: letters fell back to "?": {missing[:5]}: {work}')
        return log

    # Disc 1, New Game: the first box, then the name entry.
    scene = run_fixed(game, discs, out / f'{part}-scene', [(FIRST_BOX, 'first', None)],
                      pack=make_chinese_scene(localization, out / 'packs'), pad=NEW_GAME)
    work = run_fixed(game, discs, out / part, fan.steps, pack=pack, pad=NEW_GAME)
    log = no_placeholders(work, 'disc 1')
    check(f"text: {label}'s stretches in use" in log, f'no stretch drew with {label}: {work}')
    in_use = re.search(rf'text: {re.escape(label)} draws (\d+) letters with its own pictures and widths', log)
    check(in_use and int(in_use[1]) >= fan.drawn_at_least, f"the pack's own letter pictures are not in use: {work}")
    checked = check_pack_box(work, 'first', scene, entries[pelang.fnv1a('dlg/M001/009')], drawn)
    check(checked >= fan.first_box_letters, f'only {checked} letters of the first box checked: {work}')
    print(f'PASS {part} first box: {label} loaded, no "?", and the {checked} letters of the first box '
          "lit exactly the patch's letter pictures at the patch's widths")

    # The name entry's grid (sys/117): every cell the pack's letter, two-byte name letters (Ñ ñ Ü ü
    # Á É Í Ó Ú á é í ó ú ¿ ¡) as the pack's pictures of them; {g0D} is the game's own icon.
    raw = entries[pelang.fnv1a('sys/117')]
    names = name_letters(plain)
    by_char = {}
    at, count = struct.unpack_from('<II', text, 80)
    for k in range(count):
        r = text[at + k * 80:at + (k + 1) * 80]
        if r[4] in drawn:
            by_char[chr(struct.unpack_from('<I', r, 0)[0])] = r[4]
    cells, i = [], 0
    while i < len(raw):
        if 0xFB <= raw[i] <= 0xFE and i + 1 < len(raw):
            n = (raw[i] - 0xFB) * 250 + raw[i + 1]
            letter = chr(names[n - 1]) if 0 < n <= len(names) else '?'
            check(letter in by_char, f'sys/117: the name letter {letter} has no picture in the pack')
            cells.append((by_char[letter], letter))
            i += 2
        else:
            cells.append((raw[i], None))
            i += 1
    check(len(cells) == 99, f'sys/117 has {len(cells)} cells, not 9 rows of {GRID_COLUMNS}')
    wanted = fan.grid_letters
    check(all(ch in {letter for _, letter in cells} for ch in wanted),
          f'the pack\'s grid lacks some of {wanted}: {[letter for _, letter in cells if letter]}')
    want, icon = set(), None
    for k, (cell_code, _) in enumerate(cells):
        row, col = divmod(k, GRID_COLUMNS)
        if cell_code == 0x0D:
            icon = (row, col)
            continue
        if cell_code == 0x0F:
            continue
        check(cell_code in drawn, f'sys/117 cell {k} (code {cell_code:02X}) is not a letter the pack draws')
        _, metrics, picture = drawn[cell_code]
        want |= {(GRID_PITCH * col + x, GRID_PITCH * row + y)
                 for x, y in menu_letter(picture, metrics, cell_code <= 9)}
    grid_lit = {(x + GRID_CELLS[0], y + GRID_CELLS[1]) for y, row in enumerate(bright(work, 'grid', GRID_CELLS))
                for x, v in enumerate(row) if v}
    check(len(grid_lit) > 1000, f'no name entry grid at vblank {fan.steps[-1][0]}: {work}')
    dx = min(x for x, _ in grid_lit) - min(x for x, _ in want)
    dy = min(y for _, y in grid_lit) - min(y for _, y in want)
    want = {(x + dx, y + dy) for x, y in want}
    if icon:                                   # the game's own icon in its cell is not the pack's
        x0, y0 = dx + GRID_PITCH * icon[1], dy + GRID_PITCH * icon[0]
        grid_lit = {(x, y) for x, y in grid_lit if not (x0 - 4 <= x < x0 + 12 and y0 - 4 <= y < y0 + 12)}
    check(grid_lit == want, f"the name entry grid is not the pack's grid in its letter pictures "
                            f'(missing {sorted(want - grid_lit)[:4]}, extra {sorted(grid_lit - want)[:4]}): {work}')
    among = f' ({wanted} among them)' if wanted else ''
    print(f"PASS {part} name entry: all {len(cells) - (1 if icon else 0) - sum(c == 0x0F for c, _ in cells)} "
          f"cells of the patch's grid{among} lit exactly the pack's letter pictures, cut as the "
          f'menus cut them, {GRID_PITCH} px apart (at x {dx}, y {dy})')

    # The pictures: the title screen with the pack on the player's disc against the game on a copy of
    # the disc with the pack's pieces written in (as the patched disc holds them) and the pack's text
    # alone: the same frame, pixel for pixel; with the text alone on the player's disc, English words.
    if not pieces:
        skipped.append(f'pictures (a plain pack has none; {fan.env} names the zipped player file for them)')
    else:
        check_title_pictures(game, discs, out, part, label, pack, plain_pack, pieces, fan.title_words)

    # Disc 2: Continue from a Day 6 save; the day's title card and the boxes on the ship.
    if len(discs) < 2:
        skipped.append('Disc 2 (PE_TEST_DISC2 is not set)')
    else:
        check(DISC2_SAVE.is_file(), f'no sample save {DISC2_SAVE}')
        card = DISC2_SAVE.read_bytes()
        boxes = {label_: entries.get(pelang.fnv1a(key)) for label_, key in DISC2_BOXES.items()}
        check(all(boxes.values()), f'the pack lacks some of {list(DISC2_BOXES.values())}')
        aya, at_ = b'', 0x110                    # Aya's name on the card, in the game's letters
        while card[at_] != 0xFF and at_ < 0x11D:
            aya += card[at_:at_ + 1]
            at_ += 1
        route = disc2_steps()
        work, room = run_disc2(game, discs, out / f'{part}-disc2', pack)
        no_placeholders(work, 'disc 2')
        # the night behind the ship's boxes has no bright pixel on their lines: checked with no scene
        checked = 0
        for frame_label, raw in boxes.items():
            check(lit(work, frame_label, BOX) > 100, f'no box at {frame_label} (has the route changed?): {work}')
            checked += check_pack_box(work, frame_label, None, raw, drawn, aya)
        check(checked >= 100, f'only {checked} letters of the Disc 2 boxes checked: {work}')
        print(f'PASS {part} disc 2: Continue from a Day 6 save plays on Disc 2 (in room {room} after the boxes); '
              f"no \"?\", and the {checked} letters of {len(boxes)} boxes on the ship lit exactly the patch's letter "
              'pictures at its widths')
        # A help line the patch writes on two lines (the German patch's sys/080 while the card loads,
        # frame continue6): two rows of letters 12 px apart from the same left edge, as the patch's own
        # code draws them on the PS1 (the port: Draw_TextToEnd).
        from petext.stretch import LINE_BREAK
        notice = entries.get(pelang.fnv1a('sys/080'))
        if notice and LINE_BREAK in notice:
            rows = text_rows(work, 'continue6', HELP_LINE)
            check(len(rows) == 2 and rows[1] - rows[0] == 12,
                  f'the two-line help line is not two rows 12 px apart (rows at {rows}): {work}')
            lit_rows = bright(work, 'continue6', HELP_LINE)
            lefts = [min(x for y in range(top - HELP_LINE[1], top - HELP_LINE[1] + 9) for x, v in enumerate(lit_rows[y]) if v)
                     for top in rows]
            check(lefts[0] == lefts[1], f"the help line's second row starts at x {lefts[1]}, the first at {lefts[0]}: {work}")
            print(f"PASS {part} help line: sys/080 is two rows 12 px apart from the same left edge (x {HELP_LINE[0] + lefts[0]}), "
                  'as the patch draws its line break')
        if fan.menu_space:
            # The help line while the card loads (sys/080, frame continue6), its first row: exactly the
            # pack's letters cut as the menus cut them, each space the pack's own width (a space the
            # game's width would move every letter after it).
            space = drawn.get(0x0F)
            check(space and (space[1] >> 4) + 2 != (game_letters(discs[0], localization)[0x0F][1] >> 4) + 2,
                  "the pack doesn't draw its space its own width in the menus")
            codes = list(notice or b'')
            check(codes and all(c in drawn for c in codes) and 0x0F in codes,
                  f"sys/080 is not a line of the pack's letters with a space: {codes}")
            want = menu_line(codes, drawn)
            top = min(y for y in range(HELP_LINE[3] - HELP_LINE[1]) if any(bright(work, 'continue6', HELP_LINE)[y]))
            lit_line = {(x, y) for y, row in enumerate(bright(work, 'continue6', HELP_LINE)) for x, v in enumerate(row)
                        if v and top <= y < top + 12}
            want_top = min(y for _, y in want)
            want = {(x, y) for x, y in want if y < want_top + 12}
            dx = min(x for x, _ in lit_line) - min(x for x, _ in want)
            dy = top - want_top
            want = {(x + dx, y + dy) for x, y in want}
            check(lit_line == want, f"the help line is not the pack's letters with its own space width "
                                    f'(missing {sorted(want - lit_line)[:4]}, extra {sorted(lit_line - want)[:4]}): {work}')
            print(f"PASS {part} menu space: the help line sys/080 ({len(codes)} letters, {codes.count(0x0F)} spaces) is "
                  f"exactly the pack's letters, each space {(space[1] >> 4) + 2} px as the patch draws its menus")
        if pieces:
            try:
                written = pieces_written_in(discs[1], pieces, out / 'discs' / 'disc2')
                # the real Disc 1 with the copy of Disc 2: the card can only be Spanish if it came from Disc 2
                frames = {which: run_fixed(game, use, out / f'{part}-disc2-{which}', route, pack=p,
                                           pad=CONTINUE, card=card)
                          for which, use, p in (('written-in', [discs[0], written[0]], plain_pack),
                                                ('text-only', discs, plain_pack))}
                for frame_label in ['day-card'] + list(boxes):
                    check(frame(work, frame_label) == frame(frames['written-in'], frame_label),
                          f'{frame_label}: the frame with the pack is not the one of Disc 2 with the pieces written '
                          f'in: {work} vs {frames["written-in"]}')
                if fan.disc2_picture:
                    check(frame(work, fan.disc2_picture) != frame(frames['text-only'], fan.disc2_picture),
                          f'{fan.disc2_picture} is still English: {work}')
                    print(f"PASS {part} pictures, disc 2: {fan.disc2_picture} is the patch's picture read from Disc 2, "
                          'pixel for pixel as on Disc 2 with the pieces written in (and so are the boxes\' frames)')
                else:
                    print(f"PASS {part} pictures, disc 2: the route's frames are pixel for pixel those of Disc 2 with "
                          "the pieces written in (none of the patch's pictures is on this route)")
            finally:
                shutil.rmtree(out / 'discs', ignore_errors=True)
    if skipped:
        print(f'SKIP {part}: not checked: ' + '; '.join(skipped))
        return 77
    return 0


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
        crash_after=None, card=None, save_after=None, all_mods=None, hp=None):
    """One headless run in its own folder, with `pack` chosen and the `present` packs beside it. From vblank `start` the game pauses at each control point;
    driver(vblank, work) keeps frames (keep()) and returns the next command, or None to quit. `hp`: the game's
    own test switch --diagnostic-hp-at VBLANK:EXPECTED:VALUE (Aya's HP set at that vblank)."""
    work.mkdir(parents=True)
    config = work / 'config'
    (config / 'lang').mkdir(parents=True)
    # all_mods: the master switch by itself (False: a new player's game, Language packs' own row on).
    ini = (f'Manage mods/All mods={int(mods if all_mods is None else all_mods)}\n'
           f'Manage mods/Language packs={int(mods)}\n')
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
    if hp:
        args += ['--diagnostic-hp-at', hp]
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
    for part, check_patch in (('chinese', chinese_patch), ('spanish', spanish_patch), ('italian', italian_patch),
                              ('german', german_patch), ('french', french_patch), ('english-pack', english_pack),
                              ('translator', translator_file), ('translator-pictures', translator_pictures),
                              ('translator-map', translator_map), ('translator-battle', translator_battle),
                              ('translator-cards', translator_cards), ('translator-tutorial', translator_tutorial),
                              ('translator-game-over', translator_game_over), ('translator-look', translator_look)):
        if part in parts:                   # a fan patch's pack: needs none of the test packs below
            result = check_patch(game, discs, out, localization)
            parts = [p for p in parts if p != part]
            if not parts:
                return result
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

    # A new player's game: All mods off. A picked language still draws, exactly as with All mods on,
    # and no other mod starts.
    if 'packs' in parts:
        work = run_fixed(game, discs, out / 'pack-el-all-mods-off', first, pack=greek, pad=NEW_GAME, all_mods=False)
        log = mods_log(work)
        check('All mods off' in log, f'All mods is not off in this run: {work}')
        check('pe_lang: started' in log, f'the language mod did not start with All mods off: {work}')
        started = [line for line in log.splitlines() if ': started ' in line and not line.startswith('pe_lang:')]
        check(not started, f'another mod started with All mods off: {started}: {work}')
        check(f'pe_lang: language: {greek[1]} ({greek[0]})' in log, f'the saved language is not the one in use: {work}')
        check(crop(work, 'first', BOX) == crop(out / 'pack-el', 'first', BOX),
              f'the Greek box with All mods off is not the one with All mods on: {work}')
        print('PASS all mods off: a picked language draws as with All mods on, and no other mod starts')

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

    # A battle line in right-to-left packs, on the battle's bottom bar.
    if 'battle' in parts:
        from concurrent.futures import ThreadPoolExecutor
        from petext import rtl
        made = make_battle_packs(localization, out / 'packs', rtl.visual)
        with ThreadPoolExecutor(len(made)) as pool:   # each run has its own folder; the game counts vblanks, not time
            runs = {code: pool.submit(run_fixed, game, discs, out / f'battle-{code}', BATTLE_STEPS, pack=pack,
                                      pad=BATTLE_ROUTE) for code, pack in made.items()}
            works = {code: run.result() for code, run in runs.items()}
        masks = {}
        for code, work in works.items():
            log = mods_log(work)
            missing = [line for line in log.splitlines() if 'text: ' in line and "shows '?'" in line
                       or 'cache unavailable' in line or 'no stretch' in line]
            check(not missing, f'battle {code}: letters fell back to "?": {missing[:5]}: {work}')
            check(('reads right to left' in log) == (code != 'xx'),
                  f'battle {code}: read {"left to right" if code != "xx" else "right to left"}: {work}')
            masks[code] = bright(work, 'line', BATTLE_BAR)
            check(any(map(any, masks[code])), f'battle {code}: no battle line on the bottom bar (has the route into '
                                              f'the battle changed?): {work}')
        flat = lambda mask: hashlib.sha256(bytes(v for row in mask for v in row)).hexdigest()
        check(flat(masks['xx']) != ENGLISH_BATTLE, f'battle: the pack\'s line is not shown, English is: {works["xx"]}')
        cols = lambda mask: [x for x in range(BATTLE_BAR[2] - BATTLE_BAR[0]) if any(row[x] for row in mask)]
        l_he, l_xx = cols(masks['he']), cols(masks['xx'])
        check(l_xx[0] + BATTLE_BAR[0] <= 24, f'battle: the mirror\'s line starts at x {l_xx[0] + BATTLE_BAR[0]}: {works["xx"]}')
        check(RIGHT_EDGE - 8 <= l_he[-1] + BATTLE_BAR[0] <= RIGHT_EDGE,
              f'battle: the Hebrew line ends at x {l_he[-1] + BATTLE_BAR[0]}, not at the right side: {works["he"]}')
        shift, width = l_he[-1] - l_xx[-1], len(masks['he'][0])
        wrong = [(x + BATTLE_BAR[0], y + BATTLE_BAR[1]) for y, row in enumerate(masks['xx']) for x in range(width)
                 if (masks['he'][y][x + shift] if 0 <= x + shift < width else 0) != row[x]]
        check(not wrong, f'battle: the Hebrew line is not the drawing-order letters moved {shift} px right '
                         f'(first differences at {wrong[:4]}): {works["he"]}')
        print(f'PASS rtl battle: a Hebrew battle line (with English, a year and brackets) drew right to left, letter '
              f'for letter as in drawing order, ending at x {l_he[-1] + BATTLE_BAR[0]}')
        width_px, pixels = joined_line(BATTLE_ARABIC)
        lit_px = {(x, y) for y, row in enumerate(masks['ar']) for x, v in enumerate(row) if v}
        want = {xy for xy, v in pixels.items() if v == 1}
        left = RIGHT_EDGE - width_px - BATTLE_BAR[0]
        top = min(y for _, y in lit_px) - min(y for _, y in want)
        check({(x + left, y + top) for x, y in want} == lit_px,
              f'battle: the Arabic line does not light the joined word\'s pixels ending at x {RIGHT_EDGE}: {works["ar"]}')
        print(f'PASS rtl arabic battle: the Arabic battle line drew joined, right to left, pixel for pixel, ending at '
              f'x {RIGHT_EDGE}')
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
