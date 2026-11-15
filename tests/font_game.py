"""F1 > Font in the real game: Original keeps the game's letters, Chicago puts its own in.

  python font_game.py GAME.exe [WORK_DIR]

Set PE_TEST_DISC (and PE_TEST_DISC2); otherwise this asset test is skipped (77). GAME.exe must have
mods/fonts/Chicago.png beside it (the build copies it there). Every run has its own PE_CONFIG_HOME
and memory card folder under WORK_DIR, so nothing touches the player's settings or cards.

Checks, from frame and video memory dumps:
  Disc 1, a New Game to the first dialogue box (dlg/M001/009):
    Original  the box is the same, pixel for pixel, as the game drew it before the text renderer.
    Chicago   the dialogue font page in video memory holds exactly Chicago.png's letters (light
              pixels stroke, dark ones shadow, see-through ones empty), and every other byte of the
              page is the Original run's; the box differs from Original's.
  Disc 2 (PE_TEST_DISC2; else skipped after Disc 1), Continue from the save editor's sample save:
    the same page check on Disc 2's first dialogue, and both the load screen (menu letters) and the
    dialogue differ from the Original run's.
"""
import hashlib
import importlib.util
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location('text_renderer_game', HERE / 'text_renderer_game.py')
trg = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(trg)
check, Failed = trg.check, trg.Failed

FONT_FILE = 'Chicago.png'
CODES, CELL, COLUMNS = 77, 12, 21
PAGE_X, PAGE_W, PAGE_H = 0x140, 64, 256          # halfwords: 256 4-bit pixels a row
LOAD_SCREEN = (0, 0, 320, 240)


def png_rgba(path):
    """Width, height and RGBA rows of an 8-bit RGBA PNG (what the fonts folder holds)."""
    data = path.read_bytes()
    check(data[:8] == b'\x89PNG\r\n\x1a\n', f'not a PNG: {path}')
    pos, idat, head = 8, b'', None
    while pos < len(data):
        size, kind = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + size]
        if kind == b'IHDR':
            head = struct.unpack('>IIBBBBB', body)
        elif kind == b'IDAT':
            idat += body
        pos += 12 + size
    width, height, depth, kind, _, _, interlace = head
    check(depth == 8 and kind == 6 and not interlace, f'{path}: expected 8-bit RGBA, not interlaced')
    raw, stride, rows, prev = zlib.decompress(idat), width * 4, [], bytearray(width * 4)
    for y in range(height):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - 4] if i >= 4 else 0
            b, c = prev[i], prev[i - 4] if i >= 4 else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line))
        prev = line
    return width, height, rows


def font_cells(path):
    """{code: 144 nibbles} for every cell of the picture holding ink, as pe_font_sheet_read makes it."""
    _, _, rows = png_rgba(path)
    cells = {}
    for code in range(CODES):
        cx, cy, cell, ink = code % COLUMNS * CELL, code // COLUMNS * CELL, [], False
        for y in range(CELL):
            for x in range(CELL):
                r, g, b, a = rows[cy + y][(cx + x) * 4:(cx + x) * 4 + 4]
                v = 0
                if a >= 128 and (r, g, b) != (255, 0, 255):
                    v = 1 if (r * 3 + g * 6 + b) // 10 >= 128 else 2
                    ink = True
                cell.append(v)
        if ink:
            cells[code] = cell
    return cells


def font_page(work, label):
    """The dialogue font page (4-bit, 128 bytes a row) from a video memory dump."""
    vram = (work / f'{label}.vram').read_bytes()
    check(len(vram) == 1024 * 512 * 2, f'not a video memory dump: {work / label}.vram')
    return b''.join(vram[(y * 1024 + PAGE_X) * 2:(y * 1024 + PAGE_X + PAGE_W) * 2] for y in range(PAGE_H))


def nibble(page, x, y):
    b = page[y * 128 + x // 2]
    return b >> 4 if x & 1 else b & 15


def check_page(chicago_page, original_page, cells, where):
    """The page holds Chicago's letters where it has ink, and the Original page everywhere else."""
    for code in range(CODES):
        cx, cy = code % COLUMNS * CELL, code // COLUMNS * CELL
        for i in range(CELL * CELL):
            x, y = cx + i % CELL, cy + i // CELL
            want = cells[code][i] if code in cells else nibble(original_page, x, y)
            got = nibble(chicago_page, x, y)
            check(got == want, f'{where}: code {code:02X} pixel ({i % CELL},{i // CELL}) is {got}, expected {want}')
    covered = set()
    for code in range(CODES):
        cx, cy = code % COLUMNS * CELL, code // COLUMNS * CELL
        covered.update((cx + i % CELL, cy + i // CELL) for i in range(CELL * CELL))
    for y in range(PAGE_H):
        for x in range(256):
            if (x, y) not in covered:
                check(nibble(chicago_page, x, y) == nibble(original_page, x, y),
                      f'{where}: page pixel ({x},{y}) past the letters changed')


def run(game, discs, work, font, steps, pad=(), card=None, crash_after=None):
    """One headless run with `font` picked (None: Original); the frame and video memory are kept
    at each of `steps` (vblank, label, command)."""
    work.mkdir(parents=True)
    config = work / 'config'
    config.mkdir()
    (config / 'pe_mods.ini').write_text(''.join(f'Manage mods/{m}=0\n' for m in trg.MODS))
    if font:
        (config / 'pe_fonts_selection.txt').write_text(font)
    if card:
        (work / 'bu00').mkdir()
        (work / 'bu00' / 'BASLUS-006620000000A').write_bytes(card)
    end = steps[-1][0]
    args = [str(game), '--disc', discs[0], *(['--disc2', discs[1]] if len(discs) > 1 else []), '--headless',
            '--frame-limit', str(end + (300 if crash_after else 100)), '--dump-frame', str(work / 'frame.ppm'),
            '--dump-vram', str(work / 'vram.bin'), '--control-at', str(steps[0][0])]
    if crash_after:
        args += ['--test-crash-after', str(crash_after)]
    for tick, mask in pad:
        args += ['--pad-at', f'{tick}:{mask}']
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', SDL_AUDIO_DRIVER='dummy', PSYZ_AUDIO_CLOCK='video',
               PE_CONFIG_HOME=str(config))
    env.pop('PE_HD_BACKGROUNDS', None)
    driver = trg.fixed(steps)

    def drive(vblank, w):
        command = driver(vblank, w)
        label = [s for s in steps if s[0] == vblank][0][1]
        shutil.copy(w / 'vram.bin', w / f'{label}.vram')
        return command
    child = subprocess.Popen(args, cwd=work, env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace')
    try:
        with (work / 'game.log').open('w', encoding='utf-8') as log:
            for line in child.stdout:
                log.write(line)
                if not line.startswith('{"event":"control"'):
                    continue
                log.flush()
                command = drive(__import__('json').loads(line)['vblanks'], work)
                child.stdin.write((command or 'quit') + '\n')
                child.stdin.flush()
        child.wait(timeout=30)
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
    check(not driver.steps, f'the run stopped before vblank {driver.steps[0][0] if driver.steps else 0}: {work}')
    return work


def main(game, work=None):
    disc = os.getenv('PE_TEST_DISC')
    if not disc:
        print('PE_TEST_DISC is not set: skipped')
        return 77
    discs = [str(Path(disc).resolve())] + ([str(Path(os.environ['PE_TEST_DISC2']).resolve())]
                                           if os.getenv('PE_TEST_DISC2') else [])
    game = Path(game).resolve()
    shipped = game.parent / 'mods' / 'fonts' / FONT_FILE
    check(shipped.is_file(), f'{shipped} is not beside the game (the build copies mods/fonts there)')
    cells = font_cells(HERE.parent / 'mods' / 'fonts' / FONT_FILE)
    check(len(cells) >= 70, f'{FONT_FILE} has only {len(cells)} letters')
    out = Path(work).resolve() if work else Path(tempfile.mkdtemp(prefix='pe-font-'))
    if out.exists() and any(out.iterdir()):
        shutil.rmtree(out)
    out.mkdir(parents=True, exist_ok=True)

    first = [(trg.FIRST_BOX, 'first', None)]
    original = run(game, discs, out / 'disc1-original', None, first, pad=trg.NEW_GAME)
    chicago = run(game, discs, out / 'disc1-chicago', FONT_FILE, first, pad=trg.NEW_GAME)
    check(trg.lit(original, 'first', trg.BOX) > 200, f'no dialogue at the first box: {original}')
    check(hashlib.sha256(trg.crop(original, 'first', trg.BOX)).hexdigest() == trg.ENGLISH['first'],
          f'Original: the first box is not as the game draws it: {original}')
    print('PASS disc 1 original: the first box is the game\'s own, pixel for pixel')
    log = trg.mods_log(chicago)
    check('font "Chicago"' in log, f'Chicago was not loaded from beside the game: {chicago}')
    check_page(font_page(chicago, 'first'), font_page(original, 'first'), cells, f'disc 1 ({chicago})')
    check(trg.lit(chicago, 'first', trg.BOX) > 200, f'Chicago: no dialogue at the first box: {chicago}')
    check(trg.crop(chicago, 'first', trg.BOX) != trg.crop(original, 'first', trg.BOX),
          f'Chicago: the first box looks as Original\'s: {chicago}')
    print(f'PASS disc 1 chicago: the font page holds Chicago\'s {len(cells)} letters, the rest is the game\'s; '
          'the first box changed')

    if len(discs) < 2:
        print('SKIP disc 2: PE_TEST_DISC2 is not set')
        return 77
    check(trg.DISC2_SAVE.is_file(), f'no sample save {trg.DISC2_SAVE}')
    route = trg.disc2_steps()
    steps = route[:-1] + [(route[-1][0], 'after', 'advance 200 0')]
    runs = {}
    for name, font in (('original', None), ('chicago', FONT_FILE)):
        runs[name] = run(game, discs, out / f'disc2-{name}', font, steps, pad=trg.CONTINUE,
                         card=trg.DISC2_SAVE.read_bytes(), crash_after=steps[-1][0] + 100)
        where = re.search(r'Game: room (\d+), .*?, disc (\d+),', trg.crash_report(runs[name]))
        check(where and where[2] == '2', f'not on Disc 2 after the boxes: {runs[name]}')
    o, c = runs['original'], runs['chicago']
    check_page(font_page(c, 'box008'), font_page(o, 'box008'), cells, f'disc 2 ({c})')
    check(trg.crop(c, 'continue1', LOAD_SCREEN) != trg.crop(o, 'continue1', LOAD_SCREEN),
          f'Chicago: the load screen looks as Original\'s: {c}')
    check(trg.lit(c, 'box008', trg.BOX) > 100, f'Chicago: no dialogue on Disc 2: {c}')
    check(trg.crop(c, 'box008', trg.BOX) != trg.crop(o, 'box008', trg.BOX),
          f'Chicago: the Disc 2 box looks as Original\'s: {c}')
    print('PASS disc 2 chicago: the font page holds Chicago\'s letters; the load screen and the dialogue changed')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main(*sys.argv[1:]))
    except Failed as failure:
        print(f'FAIL {failure}')
        sys.exit(1)
