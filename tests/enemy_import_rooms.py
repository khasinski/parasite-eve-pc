"""Real-disc test: an imported enemy (mods/pe_enemy_import) joining a fight leaves the room's picture
alone, in every fight of F1 > Fights.

The enemy's texture goes into a block of VRAM the room is not using. Rooms keep their background
pictures in VRAM as small tiles in no fixed place (M152 has some where the first version put the
texture, and its floor broke up into noise when the enemy joined), so the block is chosen per
battle. For each fight in the list, with Room's enemies "Sent away":
  - the frame a moment before the enemy joins and the frame after it joined are compared in
    16x16 cells: the room's enemies go away and the imported one comes in, but the room's picture
    stays: only a few cells may change;
  - the imported enemy is drawn (tris > 0) and its texture is where the mod says it put it (vram=);
  - Cheats > Win this battle ends the fight and the trip comes back.
Frames (PPM) and VRAM of each fight are kept in the work folder.

Needs PE_TEST_DISC (USA Disc 1) and PE_TEST_ENEMY_PACK (a pack folder built from the player's own
files). Either missing: skipped (77). --only AREA:FIGHT,... runs some fights.
"""
import json
import os
import shutil
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import enemy_import_fights as F   # noqa: E402
import enemy_import_nemesis as N  # noqa: E402

CELL = 16
CELL_CHANGED = 24       # mean difference of a cell's channels (0-255) that counts as changed
MAX_CHANGED = 0.25      # share of the screen's cells that may change when the enemy joins (the room's
                        # enemies leaving; M152's broken floor was 62%)


def read_ppm(path):
    data = Path(path).read_bytes()
    parts, at = [], 0
    while len(parts) < 4:   # P6, width, height, max
        while data[at:at + 1].isspace():
            at += 1
        start = at
        while not data[at:at + 1].isspace():
            at += 1
        parts.append(data[start:at])
    return int(parts[1]), int(parts[2]), data[at + 1:]


def changed_share(before, after):
    w, h, a = read_ppm(before)
    w2, h2, b = read_ppm(after)
    if (w, h) != (w2, h2):
        return 1.0
    cells = changed = 0
    for cy in range(0, h - CELL + 1, CELL):
        for cx in range(0, w - CELL + 1, CELL):
            total = 0
            for y in range(cy, cy + CELL):
                row = (y * w + cx) * 3
                total += sum(abs(p - q) for p, q in zip(a[row:row + CELL * 3], b[row:row + CELL * 3]))
            cells += 1
            changed += total / (CELL * CELL * 3) > CELL_CHANGED
    return changed / cells


def texture_at(vram, x, y, rows):
    data = Path(vram).read_bytes()
    width = len(rows[0])
    return all(data[((y + j) * 1024 + x) * 2:((y + j) * 1024 + x) * 2 + width] == rows[j] for j in range(len(rows)))


def one_fight(game, work, area, pick, rows):
    label = f'area {area} fight {pick}'
    folder = work / f'a{area}f{pick}'
    folder.mkdir(parents=True, exist_ok=True)
    F.start_fight(game, area, pick)
    F.check(F.wait_battle(game) is not None, f'{label}: no battle ({game.picker()["Last fight"]})')
    room = game.room()['map']
    end, before = game.vblank + 3000, False
    while game.vblank < end:   # the last frame before it joins
        if 'joining' not in N.status(game)['text']:
            break
        game.raw(f'capture {folder / "before"}')
        before = True
        game.adv(2)
    F.check(before, f'{label}: joined before a frame could be taken')
    N.wait_joined(game)
    game.adv(20)
    game.raw(f'capture {folder / "after"}')
    st = N.status(game)
    share = changed_share(folder / 'before.ppm', folder / 'after.ppm')
    print(f'{label}: M{int(room):03d} vram={st.get("vram")} changed {share:.0%} of the screen', flush=True)
    F.check(N.num(st, 'tris') > 0, f'{label}: the enemy is not drawn: {st["text"]}')
    x, y = (int(v) for v in st.get('vram', '-1,-1').split(','))
    F.check(x >= 0 and texture_at(folder / 'after.vram', x, y, rows), f'{label}: the texture is not at vram={x},{y}')
    F.check(share <= MAX_CHANGED, f'{label}: M{int(room):03d}: {share:.0%} of the screen changed when the enemy joined')
    F.win_when_ready(game)
    F.wait_home(game)
    F.check(game.room()['map'] == F.HOME['map'], f'{label}: not back in Carnegie Hall')


def run(exe, disc, pack, work, only):
    tex = (pack / 'texture.bin').read_bytes()
    tex_w, tex_h, pals = struct.unpack_from('<3H', tex, 6)
    pixels = tex[12 + 512 * pals:]
    rows = [pixels[y * tex_w:(y + 1) * tex_w] for y in range(tex_h)]
    spec = json.loads((pack / 'enemy.json').read_text())

    def setup(config):
        shutil.copytree(pack, config / 'pe_enemies' / 'test-enemy')

    failures, game, n, area, pick = [], None, 0, 0, 0
    try:
        while True:
            if game is None:
                n += 1
                game = F.Game(exe, disc, work / f'game{n}', ini='Cheats/God mode=1\n', setup=setup)
                game.ask('ini-set Fights/Imported enemy 1')
                game.ask("ini-set Fights/Room's enemies 1")
            try:
                game.ask(f'ini-set Fights/Area {area}')
            except F.Failed:
                break
            try:
                game.ask(f'ini-set Fights/Fight {pick}')
            except F.Failed:
                area, pick = area + 1, 0
                continue
            if only is None or (area, pick) in only:
                try:
                    one_fight(game, work, area, pick, rows)
                except F.Failed as error:
                    failures.append(str(error))
                    print(f'FAILED: {error}; a new game for the next fight', flush=True)
                    game.quit()
                    game = None
            pick += 1
    finally:
        if game:
            game.quit()
    F.check(not failures, f'{len(failures)} fights failed ({spec.get("name")}): {failures}')


def main():
    exe, work = Path(sys.argv[1]), Path(sys.argv[2] if len(sys.argv) > 2 and not sys.argv[2].startswith('--')
                                        else tempfile.mkdtemp())
    disc, pack = os.getenv('PE_TEST_DISC'), os.getenv('PE_TEST_ENEMY_PACK')
    if not disc or not Path(disc).exists() or not pack or not (Path(pack) / 'enemy.json').exists():
        print('PE_TEST_DISC or PE_TEST_ENEMY_PACK is not set: skipped')
        return 77
    only = None
    if '--only' in sys.argv:
        only = {tuple(int(v) for v in item.split(':')) for item in sys.argv[sys.argv.index('--only') + 1].split(',')}
    work.mkdir(parents=True, exist_ok=True)
    try:
        run(exe, disc, Path(pack), work, only)
    except F.Failed as error:
        print(f'FAILED: {error}')
        return 1
    print('ok')
    return 0


if __name__ == '__main__':
    sys.exit(main())
