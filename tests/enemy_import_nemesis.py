"""Real-disc test of an imported enemy (mods/pe_enemy_import): F1 > Fights with Imported enemy set
to a pack. The imported enemy joins the battle as an enemy of its own (a new actor with its own
battle record and its own fight logic); nothing of the room is replaced.

A New Game is played to the first free field (as enemy_import_fights.py), Aya goes to level 30 and
God mode keeps her up, and the picker starts M075's Bear fight with the pack's enemy.
  alone   Room's enemies "Sent away": the battle's only enemy is the imported one (its own actor
          type, the pack's HP); its model is drawn every frame and its texture is in VRAM.
          Aya waits: it walks to her (the distance shrinks), its time bar fills, it attacks and its
          blow lands through the game's enemy-hit path (Aya loses HP: the game's damage from the
          pack's attack power). Then Aya shoots: its HP goes down, it plays its hurt animation,
          then its death animation; the battle is won, the picker says "won", the status
          "beaten", and the VRAM block is given back.
  with    Room's enemies "Fight too": the Bear and the imported enemy are both in the battle, the
          imported one standing apart; Aya runs away, the picker says "ran away" and the imported
          enemy is gone with the battle ("left behind").
Evidence (frames as PPM and VRAM, the battle trace) is kept in the work folder.

Needs PE_TEST_DISC (USA Disc 1) and PE_TEST_ENEMY_PACK: a pack folder built from the player's own
files (the enemy importer's build_enemy_pack.py). Either missing: skipped (77).
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

AREA, FIGHT, MAP = 1, 10, '75'     # Day 2 Central Park, M075: Bear
TEST_HP = 100
REACH = 900                        # the Nemesis pack's attack reach
FOE_TYPE = 0xEE                    # the imported enemy's actor type
TEX_X, TEX_Y = 320, 256


def status(game):
    """The picker's Imported status row as a dict (hp, anim, frames, tris, seen, hits, damage, dist...)."""
    text = game.picker()['Imported status']
    out = {'text': text}
    for part in text.split():
        if '=' in part:
            k, v = part.split('=', 1)
            out[k] = v
    out['seen'] = set(out.get('seen', '').split('+')) - {''}
    return out


def num(st, key):
    try:
        return int(st.get(key, '-1').split('/')[0])
    except ValueError:
        return -1


def vram_block(path, width, height):
    data = Path(path).read_bytes()
    return [data[((TEX_Y + y) * 1024 + TEX_X) * 2:((TEX_Y + y) * 1024 + TEX_X) * 2 + width] for y in range(height)]


def wait_joined(game, limit=3000):
    """The battle state once the imported enemy is in it."""
    end = game.vblank + limit
    while game.vblank < end:
        b = game.battle()
        if b['active'] and any(e['type'] == FOE_TYPE for e in b['enemies']):
            return b
        game.adv(10)
    raise F.Failed(f'the imported enemy never joined: {game.picker()}')


def part_alone(game, work, rows, tex_w, tex_h):
    game.ask("ini-set Fights/Room's enemies 1")
    F.start_fight(game, AREA, FIGHT)
    F.check(F.wait_battle(game) is not None, 'alone: the M075 battle did not start')
    F.check(game.room()['map'] == MAP, f'alone: the battle is not in M{MAP}: {game.room()}')
    wait_joined(game)
    game.adv(10)
    b = game.battle()
    types = [e['type'] for e in b['enemies']]
    F.check(types == [FOE_TYPE], f"alone: the room's enemies are still there: {b['enemies']}")
    F.check(b['enemies'][0]['max_hp'] == TEST_HP, f"alone: HP {b['enemies'][0]['max_hp']}, not the pack's {TEST_HP}")
    game.adv(30)
    game.raw(f'capture {work / "appears"}')
    st = status(game)
    F.check(num(st, 'tris') > 0 and num(st, 'frames') > 0, f'alone: not drawn: {st["text"]}')
    F.check(vram_block(work / 'appears.vram', tex_w, tex_h) == rows, 'alone: the pack texture is not in VRAM')
    first_dist = num(st, 'dist')
    print(f'alone: joined: {st["text"]}', flush=True)

    # Aya waits: it comes to her and its blow lands
    end, min_dist, captured = game.vblank + 6000, first_dist, False
    while game.vblank < end:
        st = status(game)
        if num(st, 'dist') >= 0:
            min_dist = min(min_dist, num(st, 'dist'))
        if not captured and st.get('anim') == 'attack':
            game.adv(20)
            game.raw(f'capture {work / "attack"}')
            captured = True
        if num(st, 'hits') >= 1 and num(st, 'damage') > 0:
            break
        game.adv(20)
    st = status(game)
    print(f'alone: came from {first_dist} to {min_dist}: {st["text"]}', flush=True)
    F.check('walk' in st['seen'], f'alone: it never walked: {st["text"]}')
    F.check(min_dist < first_dist - 100 or first_dist <= REACH, f'alone: it never came to Aya: {st["text"]}')
    F.check('attack' in st['seen'], f'alone: it never attacked: {st["text"]}')
    F.check(num(st, 'hits') >= 1, f'alone: no blow landed: {st["text"]}')
    F.check(num(st, 'damage') > 0, f'alone: its blow took no HP: {st["text"]}')

    # Aya shoots until it is beaten
    hp_seen, shots, seen, end = [TEST_HP], 0, set(), game.vblank + 40000
    while game.vblank < end:
        b = game.battle()
        if not b['active']:
            break
        ours = [e for e in b['enemies'] if e['type'] == FOE_TYPE]
        if ours:
            hp_seen.append(ours[0]['hp'])
        st = status(game)
        seen |= st['seen']
        if 'die' in st['seen'] and not (work / 'dies.ppm').exists():
            game.adv(15)
            game.raw(f'capture {work / "dies"}')
        if b['player']['atb'] >= 9000:
            game.press(F.CROSS, 20)
            game.press(F.CROSS, 90)
            shots += 1
            if shots == 2:
                game.raw(f'capture {work / "shot"}')
        else:
            game.adv(30)
    print(f'alone: {shots} attacks by Aya, its HP {TEST_HP} -> {min(hp_seen)}, seen {sorted(seen)}', flush=True)
    F.check(min(hp_seen) < TEST_HP, 'alone: Aya never hurt it')
    F.check('hurt' in seen, 'alone: no hurt animation')
    F.check('die' in seen, 'alone: no death animation')
    F.wait_home(game)
    last = game.picker()['Last fight']
    text = game.picker()['Imported status']
    print(f'alone: {last}; {text}', flush=True)
    F.check(last.endswith(': won'), f'alone: Last fight is {last!r}')
    F.check('beaten (beaten 1 of 1)' in text, f'alone: Imported status says {text!r}')
    game.raw(f'capture {work / "after"}')
    F.check(vram_block(work / 'after.vram', tex_w, tex_h) != rows, 'alone: the VRAM block was not given back')


def part_with(game, work):
    game.ask("ini-set Fights/Room's enemies 0")
    F.start_fight(game, AREA, FIGHT)
    F.check(F.wait_battle(game) is not None, 'with: the M075 battle did not start')
    wait_joined(game)
    game.adv(30)
    b = game.battle()
    ours = [e for e in b['enemies'] if e['type'] == FOE_TYPE]
    room = [e for e in b['enemies'] if e['type'] != FOE_TYPE]
    F.check(len(ours) == 1 and room, f"with: the room's enemies and the imported one should both fight: {b['enemies']}")
    gap = min(((ours[0]['position'][0] - r['position'][0]) ** 2 + (ours[0]['position'][2] - r['position'][2]) ** 2) ** 0.5
              / 65536 for r in room)
    game.raw(f'capture {work / "with"}')
    print(f'with: room enemies {[(e["id"], e["hp"]) for e in room]}, imported {ours[0]["hp"]} HP, {gap:.0f} apart', flush=True)
    F.check(gap > 300, f'with: the imported enemy stands inside a room enemy ({gap:.0f} apart)')
    F.escape(game)
    F.wait_home(game)
    last = game.picker()['Last fight']
    text = game.picker()['Imported status']
    print(f'with: {last}; {text}', flush=True)
    F.check(last.endswith(': ran away'), f'with: Last fight is {last!r}')
    F.check('left behind' in text, f'with: Imported status says {text!r}')


def run(exe, disc, pack, work):
    tex = (pack / 'texture.bin').read_bytes()
    tex_w, tex_h, pals = struct.unpack_from('<3H', tex, 6)
    pixels = tex[12 + 512 * pals:]
    rows = [pixels[y * tex_w:(y + 1) * tex_w] for y in range(tex_h)]
    spec = json.loads((pack / 'enemy.json').read_text())

    def setup(config):
        dst = config / 'pe_enemies' / 'test-enemy'
        shutil.copytree(pack, dst)
        (dst / 'enemy.json').write_text(json.dumps(dict(spec, hp=TEST_HP)))

    game = F.Game(exe, disc, work / 'fight', ini='Cheats/God mode=1\nCheats/Infinite ammo and PE=1\n', limit=F.FIELD + 120000, setup=setup)
    try:
        game.ask('set-stat level 30')
        game.ask('trace on')
        game.ask('ini-set Fights/Imported enemy 1')
        part_alone(game, work, rows, tex_w, tex_h)
        part_with(game, work)
        losses = []
        trace = game.ctl / 'battle.jsonl'
        for line in (trace.read_text().splitlines() if trace.exists() else []):
            e = json.loads(line)
            if e.get('event') == 'hp_loss_admitted' and e.get('source') == 1:
                losses.append(e['loss'])
        print(f"Aya's HP losses to enemy blows (trace): {losses}")
    finally:
        game.quit()


def main():
    exe, work = Path(sys.argv[1]), Path(sys.argv[2] if len(sys.argv) > 2 else tempfile.mkdtemp())
    disc, pack = os.getenv('PE_TEST_DISC'), os.getenv('PE_TEST_ENEMY_PACK')
    if not disc or not Path(disc).exists() or not pack or not (Path(pack) / 'enemy.json').exists():
        print('PE_TEST_DISC or PE_TEST_ENEMY_PACK is not set: skipped')
        return 77
    work.mkdir(parents=True, exist_ok=True)
    try:
        run(exe, disc, Path(pack), work)
    except F.Failed as error:
        print(f'FAILED: {error}')
        return 1
    print('ok')
    return 0


if __name__ == '__main__':
    sys.exit(main())
