"""Real-disc test of F1 > Fights, the fight picker (mods/pe_enemy_import).

A New Game is played to the first free field (Carnegie Hall, story 9). The picker is then used as
a player uses it, through the debug control channel (docs/DEBUG_CONTROL.md): `ini-set` picks the
area and the fight, `mod-action` presses Start fight, `battle-state` reads the battle.

  win    Day 1 Sewer, M030 spot 2: the battle starts in M030 with its rats (enemy id 2); Cheats >
         Win this battle ends it; after the results Aya is back in Carnegie Hall where she stood,
         with the story variable as it was, and Last fight says "won".
  run    M030 spot 1: the battle starts; Escape from the battle menu (until it succeeds); Aya is
         back where she stood and Last fight says "ran away".
  lose   Aya's HP set to 1, M030 spot 2: the rats win; the game's own Game Over follows and the
         game reaches the title screen; the picker ended the trip ("lost (Game Over)").
  --all  (enemy-import-fights-all) every fight in the list starts a battle with enemies, and the
         trip comes back each time. Prints each fight's enemies (id, HP).

Set PE_TEST_DISC (USA Disc 1); otherwise skipped (77). Every run is headless, in its own folder.
Use --imported-room with PE_TEST_RE2_DISC to check win, escape and loss from an imported room.
Use --street with RE2 disc 1 to approach the opening zombies without the fight picker.
Use --rpd with RE2 disc 1 to test an automatic police station encounter.
"""
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

START, UP, DOWN, TRIANGLE, CROSS = 8, 16, 64, 4096, 16384
FIELD = 32400               # the New Game route below leaves Aya free in front of Carnegie Hall
TITLE_TOKEN = 0xA9400048
HOME = {'map': '2', 'story': '9'}
MODS = 'Manage mods/All mods=1\nManage mods/Enemy import=1\nManage mods/Cheats=1\n'


class Failed(Exception):
    pass


def check(condition, message):
    if not condition:
        raise Failed(message)


def new_game_route():
    pad = [(10000, START), (10010, 0), (11000, UP), (11010, 0), (11500, CROSS), (11510, 0)]
    for tick in range(17000, 22000, 600):
        pad += [(tick, CROSS), (tick + 10, 0)]
    pad += [(23000, START), (23010, 0), (23600, START), (23610, 0)]
    for tick in range(24200, 32200, 600):
        pad += [(tick, CROSS), (tick + 10, 0)]
    return pad


class Game:
    """One headless game paused at each control point; commands go through the debug channel."""

    def __init__(self, exe, disc, work, ini='', limit=900000, setup=None, headless='--headless',
                 *, boot_pad=None, control_at=FIELD):
        if work.exists():
            shutil.rmtree(work)
        self.work, self.ctl, config = work, work / 'ctl', work / 'config'
        config.mkdir(parents=True)
        self.ctl.mkdir()
        (config / 'pe_mods.ini').write_text(MODS + ini)
        self.config = config
        if setup:
            setup(config)
        args = [str(exe), '--disc', disc, headless, '--frame-limit', str(limit), '--control-at', str(control_at)]
        for tick, mask in new_game_route() if boot_pad is None else boot_pad:
            args += ['--pad-at', f'{tick}:{mask}']
        env = dict(os.environ, SDL_AUDIODRIVER='dummy', SDL_AUDIO_DRIVER='dummy', PSYZ_AUDIO_CLOCK='video',
                   PE_CONFIG_HOME=str(config), PE_DEBUG_CONTROL=str(self.ctl))
        env.pop('PE_HD_BACKGROUNDS', None)
        self.log = (work / 'game.log').open('w', encoding='utf-8')
        self.child = subprocess.Popen(args, cwd=work, env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                      stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace')
        self.seen = 0
        self.vblank, self.scene = 0, 0
        self._next()

    def _next(self):
        for line in self.child.stdout:
            if line.startswith('{"event":"control"'):
                state = json.loads(line)
                self.vblank, self.scene = state['vblanks'], state['scene']
                return
            if 'ConfigureVSync' not in line:
                self.log.write(line)
        raise Failed(f'the game stopped at vblank {self.vblank}: {self.work / "game.log"}')

    def adv(self, frames, mask=0):
        self.child.stdin.write(f'advance {frames} {mask}\n')
        self.child.stdin.flush()
        self._next()

    def raw(self, line):
        """A control-mode command (capture PATH, ...): answered by the next control point."""
        self.child.stdin.write(line + '\n')
        self.child.stdin.flush()
        self._next()

    def press(self, mask, after=50):
        self.adv(10, mask)
        self.adv(after)

    def ask(self, command, limit=600):
        out = self.ctl / 'out'
        (self.ctl / 'cmd.tmp').write_text(command + '\n')
        (self.ctl / 'cmd.tmp').replace(self.ctl / 'cmd')
        name = command.split()[0]
        for _ in range(0, limit, 10):
            self.adv(10)
            lines = out.read_text(encoding='utf-8').splitlines() if out.exists() else []
            for line in lines[self.seen:]:
                self.seen += 1
                if line.split()[1:2] == [name]:
                    if line.startswith('err'):
                        raise Failed(f'{command}: {line}')
                    return line[len('ok ') + len(name) + 1:]
        raise Failed(f'no answer to {command} by vblank {self.vblank}')

    def room(self):
        return dict(kv.split('=', 1) for kv in self.ask('room').split() if '=' in kv)

    def aya(self):
        for actor in self.ask('actors').split():
            if '*' in actor:
                return tuple(int(v) for v in re.search(r'@(-?\d+),(-?\d+)', actor).groups())
        raise Failed('no Aya in the actor list')

    def battle(self):
        return json.loads(self.ask('battle-state'))

    def picker(self):
        rows = json.loads(self.ask('mod-state pe_enemy_import'))['rows']
        return {row['label']: row['value'] for row in rows}

    def picker_log(self):
        return ''.join(path.read_text(encoding='utf-8', errors='replace') for path in self.config.glob('*.log')
                       if 'fight_picker' in path.read_text(encoding='utf-8', errors='replace'))

    def quit(self):
        try:
            self.child.stdin.write('quit\n')
            self.child.stdin.flush()
            self.child.wait(timeout=30)
        except Exception:
            self.child.kill()
            self.child.wait()
        self.log.close()


def start_fight(game, area, pick):
    game.ask(f'ini-set Fights/Area {area}')
    game.ask(f'ini-set Fights/Fight {pick}')
    game.ask('mod-action Fights/Start fight')


def wait_battle(game, limit=4000):
    """The battle's state once its enemies are in, or None when the picker gave up first."""
    end, n = game.vblank + limit, 0
    while game.vblank < end:
        state = game.battle()
        if state['active'] and state['enemies']:
            return state
        n += 1
        if not state['active'] and n % 10 == 0 and game.picker()['Status'] == 'Ready':
            return None
        game.adv(20)
    raise Failed(f'no battle and no answer from the picker by vblank {game.vblank}')


def wait_home(game, limit=4000):
    """Cross through the results (still the battle until then) until the picker is back to Ready."""
    end = game.vblank + limit
    while game.vblank < end:
        if not game.battle()['active'] and game.picker()['Status'] == 'Ready':
            return
        game.press(CROSS, 200)
    raise Failed(f'the picker never came back by vblank {game.vblank}: {game.picker()}')


def check_home(game, where, label):
    room = game.room()
    check(room['map'] == HOME['map'] and room['story'] == HOME['story'] and room['free'] == '1',
          f'{label}: not back in Carnegie Hall with the story as it was: {room}')
    check(game.aya() == where, f'{label}: Aya at {game.aya()}, not where she stood ({where})')


def win_when_ready(game, limit=3000):
    """Cheats > Win this battle once the battle is under way (Aya's time bar filling): killed
    during the opening, some rooms' battles never reach their end."""
    end = game.vblank + limit
    while game.vblank < end and game.battle()['player']['atb'] == 0:
        game.adv(30)
    game.ask('mod-action Cheats/Win this battle')


def part_win(game):
    where = game.aya()
    start_fight(game, 0, 1)
    state = wait_battle(game)
    check(state is not None, 'win: the M030 battle did not start')
    check(game.room()['map'] == '30', 'win: the battle is not in M030')
    ids = sorted(e['id'] for e in state['enemies'])
    check(ids and set(ids) == {2}, f'win: M030 spot 2 should be its rats (id 2), got {ids}')
    win_when_ready(game)
    wait_home(game)
    check(game.picker()['Last fight'] == 'M030 spot 2: Rat x2: won', f'win: Last fight is {game.picker()["Last fight"]!r}')
    check_home(game, where, 'win')
    print(f'win: M030 spot 2 started with {ids}, won, back home at {where}')


def part_profile_editor(game, profile_path):
    before = profile_path.read_bytes()
    game.ask('ini-set Fights/Imported enemy 17')
    page = 'Fights/Enemy profile'
    game.ask(f'mod-action {page}/Load selected enemy')
    game.ask(f'mod-set {page}/HP 123')
    game.ask(f'mod-set {page}/Speed 5')
    game.ask(f'mod-set {page}/Attack move 0')
    game.ask(f'mod-action {page}/Preview attack')
    check(wait_battle(game) is not None, 'profile editor: the preview did not start')
    end = game.vblank + 3000
    while game.vblank < end:
        state = game.battle()
        if len(state['enemies']) == 1 and state['enemies'][0]['type'] == 0xEE:
            check(state['enemies'][0]['max_hp'] == 123, state)
            break
        game.adv(10)
    else:
        raise Failed('profile editor: no preview actor')
    check(profile_path.read_bytes() == before, 'profile editor: preview wrote to the profile')
    win_when_ready(game)
    wait_home(game)
    game.ask(f'mod-action {page}/Apply and rebuild')
    check(game.picker()['Editor status'] == 'Profile saved and enemy pack rebuilt.', game.picker())
    edited = json.loads(profile_path.read_text(encoding='utf-8'))
    check(edited['hp'] == 123 and edited['behavior']['speed'] == 5, edited)
    check(edited['anims'][edited['attacks'][0]['anim']] == 0, edited)
    built = json.loads(profile_path.with_name('enemy.json').read_text(encoding='utf-8'))
    check(built['hp'] == 123 and built['behavior']['speed'] == 5, built)
    game.ask('ini-set Fights/Imported enemy 0')
    print('profile editor: previewed a source move without saving, then applied HP, speed and attack')


def part_room_variants(exe, disc, work, sources):
    for player, source in enumerate(sources):
        def setup(config):
            folder = config / 'import'
            folder.mkdir()
            (folder / 'folders.txt').write_text(f'{source.parent}\n', encoding='utf-8')

        game = Game(exe, disc, work / f'room-variants-{player}', setup=setup,
                    ini='Cheats/God mode=1\nManage mods/Play imported games=1\n', limit=FIELD + 45000)
        try:
            game.ask(f'ini-set Play imported games/Scenario {player}')
            game.ask('mod-action Play imported games/Start')
            # Remove the main archive alias from this isolated fixture to exercise the real alternate's fallback.
            catalog_path = game.config / 'import/store/re2/catalog.json'
            catalog = json.loads(catalog_path.read_text(encoding='utf-8'))
            alternate = f're2:enemy/em1f-cdemd{player + 2}'
            check(alternate in catalog, f'no {alternate} in the disc catalog')
            catalog.pop(f're2:enemy/em1f-cdemd{player}', None)
            catalog_path.write_text(json.dumps(catalog), encoding='utf-8')
            source = catalog[f're2:logic/112-{player}']['sources'][0]
            logic = catalog_path.parent / source['sha256'] / source['item'] / 'roomlogic.bin'
            fixture = bytearray(logic.read_bytes())
            at = 28
            while fixture[at:at + 4] != b'RDT0':
                length, = struct.unpack_from('<I', fixture, at + 8)
                at += 12 + (length + 3 & ~3)
                check(at + 12 <= len(fixture), 'room variants: no RDT section')
            rdt = at + 12
            init, main, end = struct.unpack_from('<3I', fixture, rdt + 8 + 16 * 4)
            placements = {}
            for i in range(rdt + init, rdt + end - 21):
                row = fixture[i:i + 22]
                if row[:2] == b'\x44\0' and row[2] in (1, 2, 3) and row[3] == 0x1F and row[4] & 63 == 10:
                    placements[row[2]] = row
            check(len(placements) == 3, 'room variants: missing real feeding placements')
            # Spawn the existing placements on entry, bypassing the room's story prerequisites.
            script = b'\x02\0' + b''.join(placements[slot] for slot in (1, 2, 3)) + b'\x01\0'
            check(len(script) < main - init, 'room variants: short init section')
            fixture[rdt + init:rdt + init + len(script)] = script
            logic.write_bytes(fixture)
            game.ask('ini-set Play imported games/Testing/Stage 1')
            game.ask('ini-set Play imported games/Testing/Room 18')
            game.ask('mod-action Play imported games/Testing/Go to that room')
            game.adv(120)
            rows = json.loads(game.ask('mod-state pe_re_play'))['rows']
            info = {row['label']: row['value'] for row in rows}
            check(info['Status'].startswith('Room 1-12') and '0x1F' in info['Enemies'], info)
            start_fight(game, 0, 1)
            check(wait_battle(game) is not None, 'room variants: no native fight')
            end = game.vblank + 3000
            while game.vblank < end:
                state = game.battle()
                if sum(e['type'] == 0xEE for e in state['enemies']) >= 3:
                    break
                game.adv(10)
            log = (game.config / 'pe_mods.log').read_text(encoding='utf-8', errors='replace')
            check(f'selected {alternate} ' in log, f'room variants: did not select {alternate}')
            check(re.search(rf'starts with feeding_112-\d+-p{player}', log),
                  'room variants: did not play the room feeding animation')
            win_when_ready(game)
            wait_home(game)
            print(f'room variants: {alternate} joined with room 1-12 feeding moves')
        finally:
            game.quit()


def part_flight(exe, disc, work, pack):
    def setup(config):
        shutil.copytree(pack, config / 'pe_enemies' / 're2-em21')

    game = Game(exe, disc, work / 'flyer', setup=setup, ini='Cheats/God mode=1\n')
    try:
        game.ask('ini-set Fights/Imported enemy 1')
        game.ask('ini-set Fights/Room\'s enemies 1')
        start_fight(game, 0, 1)
        check(wait_battle(game) is not None, 'flyer: no battle')
        cruise = dive = False
        end = game.vblank + 7000
        while game.vblank < end:
            status = game.picker()['Imported status']
            lift = re.search(r'\blift=(\d+)', status)
            if lift:
                height = int(lift[1])
                cruise |= height >= 400
                dive |= cruise and height < 400 and 'anim=attack' in status
            if dive and re.search(r'\bsounds=[1-9]\d*', status):
                break
            game.adv(10)
        else:
            raise Failed(f'flyer: expected cruise, dive and a played cue, got {status}')
        win_when_ready(game)
        wait_home(game)
        check(game.picker()['Last fight'].endswith(': won'), 'flyer: death did not finish the fight')
        print('flyer: crow climbed, dived and played a sound in battle, then died')
    finally:
        game.quit()


def part_imported_room(exe, disc, work, re2_disc, boss_forms=False, profile_editor=False):
    def setup(config):
        folders = config / 'import' / 'folders.txt'
        folders.parent.mkdir()
        folders.write_text(f'{re2_disc.parent}\n', encoding='utf-8')
        # Fill the manual menu so the room's pack must load directly by name.
        for i in range(16):
            folder = config / 'pe_enemies' / f'aaa-test-{i:02}'
            folder.mkdir(parents=True)
            (folder / 'enemy.json').write_text('{}', encoding='utf-8')

    def room_rows(game):
        rows = json.loads(game.ask('mod-state pe_re_play'))['rows']
        return {row['label']: row['value'] for row in rows}

    game = Game(exe, disc, work / 'imported-room', setup=setup,
                ini='Cheats/God mode=1\nManage mods/Play imported games=1\nFights/RE2 street encounters=0\n',
                limit=FIELD + 60000)
    try:
        game.ask('mod-action Play imported games/Start')
        before = room_rows(game)
        check(before['Status'].startswith('Room ') and before['Enemies'] != '-', before)
        where = game.aya()
        start_fight(game, 0, 1)
        check(wait_battle(game) is not None, 'imported room: the native battle did not start')
        paused = room_rows(game)
        check(paused['Status'].startswith('Paused for battle.') and paused['Enemies'] == '-', paused)
        end = game.vblank + 3000
        while game.vblank < end and 'hp=' not in game.picker()['Imported status']:
            game.adv(10)
        status = game.picker()['Imported status']
        check('hp=' in status and 'foes=3' in status, f'imported room: expected three street foes, got {status}')
        log = (game.config / 'pe_mods.log').read_text(encoding='utf-8', errors='replace')
        check(log.count('battle: EM1F-CDEMD0 starts with start_prone_2') == 1,
              'imported room: expected one prone zombie in the first street fight')
        packs = list((game.config / 'pe_enemies').glob('re2-em*/enemy.json'))
        check(len(packs) == 1 and (packs[0].parent / 'profile.json').is_file(),
              'imported room: no automatically built enemy pack and profile')
        check(packs[0].parent.name == 're2-em1f-cdemd0', f'imported room: wrong Leon archive {packs[0]}')
        end = game.vblank + 1000
        while game.vblank < end:
            status = game.picker()['Imported status']
            if re.search(r'\bseen=\S*\bwake\b', status) and 'anim=wake' not in status:
                break
            game.adv(10)
        else:
            raise Failed(f'imported room: prone zombie did not finish getting up: {status}')
        log = (game.config / 'pe_mods.log').read_text(encoding='utf-8', errors='replace')
        check('EM1F-CDEMD0 wakes with start_prone_2_wake' in log, 'imported room: wrong get-up move')
        win_when_ready(game)
        wait_home(game)
        check_home(game, where, 'imported room')
        after = room_rows(game)
        check(after['Status'] == before['Status'] and
              int(after['Enemies'].split()[0]) == int(before['Enemies'].split()[0]) - 3, (before, after))
        check('imported room: 3 selected placements beaten' in game.picker_log(), 'imported room: wrong defeat count')
        print('imported room: three street foes joined and were defeated, and Aya returned to the same room and pose')
        if profile_editor:
            part_profile_editor(game, packs[0].parent / 'profile.json')
            resumed = room_rows(game)
            check(resumed['Status'] == after['Status'] and resumed['Enemies'] == after['Enemies'],
                  'profile editor: preview changed the source room')
            check_home(game, where, 'profile editor preview')
        after_win = after
        part_run(game)
        after = room_rows(game)
        check(after['Status'] == after_win['Status'] and after['Enemies'] == after_win['Enemies'], (after_win, after))
        print('imported room: escape also restored the same room, pose and placements')

        game.ask('ini-set Play imported games/Testing/Stage 1')
        game.ask('ini-set Play imported games/Testing/Room 6')
        game.ask('mod-action Play imported games/Testing/Go to that room')
        mixed = room_rows(game)
        check(mixed['Status'].startswith('Room 1-06') and
              all(kind in mixed['Enemies'] for kind in ('0x12', '0x13')), mixed)
        if boss_forms:
            folder = game.config / 'pe_enemies' / 're2-em12-cdemd0'
            folder.mkdir(parents=True, exist_ok=True)
            (folder / 'profile.json').write_text(json.dumps({
                'format': 1, 'name': 'EM12-CDEMD0', 'moves': 'enemy/em12-cdemd0/moves',
                'anims': {'idle': 0}, 'next_form': 're2:enemy/em31',
            }), encoding='utf-8')
        start_fight(game, 0, 1)
        check(wait_battle(game) is not None, 'mixed room: the battle did not start')
        end = game.vblank + 3000
        while game.vblank < end:
            state = game.battle()
            if sum(e['type'] == 0xEE for e in state['enemies']) == 2:
                break
            game.adv(10)
        else:
            raise Failed('mixed room: both imported enemies did not join')
        log = (game.config / 'pe_mods.log').read_text(encoding='utf-8', errors='replace')
        check(all(f'battle: EM{kind}-CDEMD0 joins' in log for kind in ('12', '13')), 'mixed room: wrong actor models')
        textures = re.findall(r'battle: EM1[23]-CDEMD0 texture at (\d+,\d+)', log)
        check(len(set(textures)) == 2, f'mixed room: expected separate texture origins, got {textures}')
        if boss_forms:
            boss_slot = None
            for kind, hp in (('31', 700), ('33', 900), ('34', 1100), ('36', 600)):
                win_when_ready(game)
                state = game.battle()
                bosses = [e for e in state['enemies'] if e['type'] == 0xEE and e['hp'] == hp]
                check(state['active'] and len(bosses) == 1, f'EM{kind}: {state}')
                slot = bosses[0]['actor']
                check(boss_slot is None or boss_slot == slot, 'the transformed boss changed actors')
                boss_slot = slot
                check(room_rows(game)['Status'].startswith('Paused for battle.'), 'room resumed before final form')
            print('boss forms: EM31, EM33, EM34 and EM36 used one battle slot')
        win_when_ready(game)
        wait_home(game)
        after_mixed = room_rows(game)
        check(after_mixed['Status'] == mixed['Status'] and after_mixed['Enemies'] == 'None placed here.', after_mixed)
        if boss_forms:
            log = (game.config / 'pe_mods.log').read_text(encoding='utf-8', errors='replace')
            check(log.count('EM36-CDEMD0 played its death cue') == 1,
                  'boss forms: final form should play its death cue exactly once')
        print('imported room: EM12 and EM13 shared a battle and their own room slots were cleared')

        game.ask('ini-set Play imported games/Testing/Room 0')
        game.ask('mod-action Play imported games/Testing/Go to that room')
        game.ask('ini-set Cheats/God mode 0')
        game.ask('set-stat hp 1')
        start_fight(game, 1, 10)
        check(wait_battle(game) is not None, 'imported room: the fatal battle did not start')
        wait_game_over(game)
        print('imported room: losing returned to the title screen')
    finally:
        game.quit()


def part_street(exe, disc, work, re2_disc):
    def setup(config):
        folders = config / 'import' / 'folders.txt'
        folders.parent.mkdir()
        folders.write_text(f'{re2_disc.parent}\n', encoding='utf-8')

    game = Game(exe, disc, work / 'opening-street', setup=setup,
                ini='Cheats/God mode=1\nManage mods/Play imported games=1\n', limit=FIELD + 60000)

    def placement_count():
        rows = json.loads(game.ask('mod-state pe_re_play'))['rows']
        values = {row['label']: row['value'] for row in rows}
        check(values['Status'].startswith('Room 1-00'), values)
        return values['Enemies'].count('0x1F')

    def approach():
        game.adv(55, 128)  # Right in the opening camera, along the accessible side of the street.
        check(wait_battle(game) is not None, 'opening street: approach did not start a battle')
        until = game.vblank + 2500
        while game.vblank < until:
            enemies = game.battle()['enemies']
            if enemies and all(enemy['type'] == 0xee for enemy in enemies):
                return len(enemies)
            game.adv(15)
        raise Failed('opening street: the imported roster did not take over')

    try:
        game.ask('mod-action Play imported games/Start')
        initial = placement_count()
        check(initial == 6, f'opening street: expected six living zombies, got {initial}')
        game.adv(90)
        check(game.picker()['Status'] == 'Ready', 'opening street: an encounter started while standing at the entrance')
        count = approach()
        escape(game)
        wait_home(game)
        game.adv(120)
        check(game.picker()['Last fight'] == 'RE2 opening street: ran away', game.picker())
        check(game.picker()['Status'] == 'Ready' and placement_count() == initial,
              'opening street: an escape removed a survivor or immediately restarted the fight')

        game.adv(48, 32)  # Left, far enough to leave the encounter radius before returning.
        check(approach() == count, 'opening street: the escaped group did not return')
        win_when_ready(game)
        wait_home(game)
        game.adv(120)
        check(game.picker()['Last fight'] == 'RE2 opening street: won', game.picker())
        check(placement_count() == initial - count, 'opening street: wrong zombies cleared after victory')
        game.ask('save-block 1')
        check((game.ctl / 'block.bin').stat().st_size > 0, 'opening street: could not build the native save')
        print(f'opening street: escaped safely, re-approached, won and saved with {initial - count} zombies left')
    finally:
        game.quit()


def part_rpd(exe, disc, work, re2_disc):
    def setup(config):
        folders = config / 'import' / 'folders.txt'
        folders.parent.mkdir()
        folders.write_text(f'{re2_disc.parent}\n', encoding='utf-8')

    game = Game(exe, disc, work / 'rpd', setup=setup,
                ini='Cheats/God mode=1\nManage mods/Play imported games=1\nFights/RE2 RPD encounters=0\n')

    def room_rows():
        rows = json.loads(game.ask('mod-state pe_re_play'))['rows']
        return {row['label']: row['value'] for row in rows}

    try:
        game.ask('mod-action Play imported games/Start')
        game.ask('ini-set Play imported games/Testing/Stage 2')
        game.ask('ini-set Play imported games/Testing/Room 9')
        game.ask('mod-action Play imported games/Testing/Go to that room')
        before = room_rows()
        check(before['Status'].startswith('Room 2-09') and before['Enemies'].count('0x1F') == 6, before)
        game.adv(90)
        check(game.picker()['Status'] == 'Ready', 'RPD fight started with the toggle off')
        # The debug arrival point is already within reach of the nearest group.
        game.ask('ini-set Fights/RE2 RPD encounters 1')
        check(wait_battle(game) is not None, 'RPD: the nearby zombies did not start a fight')
        until = game.vblank + 2500
        while game.vblank < until:
            enemies = game.battle()['enemies']
            if enemies and all(enemy['type'] == 0xee for enemy in enemies):
                break
            game.adv(15)
        else:
            raise Failed('RPD: the imported roster did not take over')
        win_when_ready(game)
        wait_home(game)
        game.adv(120)
        after = room_rows()
        check(after['Status'] == before['Status'], after)
        check(game.picker()['Last fight'] == 'RE2 RPD 2-09: won', game.picker())
        check(after['Enemies'].count('0x1F') == 6 - len(enemies), 'RPD: wrong zombies cleared')
        print(f'RPD 2-09: defeated {len(enemies)} zombies and returned to the same position')
    finally:
        game.quit()


def escape(game, limit=12000):
    """Battle menu, Down three times (Escape from Battle), Cross; again until it works.
    Only with Aya's time bar full: before that the menu holds just Use Item and Change Weapon, so
    the Downs land on Change Weapon and the Cross opens the weapon screen instead."""
    end = game.vblank + limit
    while game.vblank < end:
        b = game.battle()
        if not b['active']:
            return
        if b['player']['atb'] < 9000:
            game.adv(30)
            continue
        game.press(TRIANGLE, 60)
        for _ in range(3):
            game.press(DOWN, 50)
        game.press(CROSS, 400)
    try:
        game.raw(f'capture {game.work / "escape-failed"}')
    except Exception:
        pass
    raise Failed(f'escape never worked by vblank {game.vblank}: {game.battle()}')


def part_run(game):
    where = game.aya()
    start_fight(game, 0, 0)
    state = wait_battle(game)
    check(state is not None, 'run: the M030 battle did not start')
    escape(game)
    wait_home(game)
    last = game.picker()['Last fight']
    check(last.endswith(': ran away'), f'run: Last fight is {last!r}')
    check_home(game, where, 'run')
    print(f'run: M030 spot 1 started with {sorted(e["id"] for e in state["enemies"])}, ran away, back home')


def wait_game_over(game):
    end = game.vblank + 20000
    while game.vblank < end:   # Aya stands; the rats attack. Game Over answers no commands.
        try:
            if not game.battle()['active']:
                break
        except Failed:
            break
        game.adv(200)
    while game.vblank < end and game.scene != TITLE_TOKEN:
        game.press(CROSS, 300)
    check(game.scene == TITLE_TOKEN, f'lose: no title screen by vblank {game.vblank} (scene {game.scene:08X}); '
          f'{game.work / "config"}')
    check('lost (Game Over)' in game.picker_log(), 'lose: the picker did not give up the trip')
    game.adv(600)   # the title keeps running


def part_lose(exe, disc, work):
    game = Game(exe, disc, work / 'lose', limit=FIELD + 40000)
    try:
        game.ask('set-stat hp 1')
        start_fight(game, 0, 1)
        check(wait_battle(game) is not None, 'lose: the M030 battle did not start')
        wait_game_over(game)
        print(f'lose: Game Over, title at vblank {game.vblank}; the picker gave up the trip')
    finally:
        game.quit()


def part_all(exe, disc, work, only=None):
    """Every fight in the list (area by area, as the F1 rows count them). A fight that leaves Aya
    stuck ends that game; the next fight starts in a new one."""
    failures, game, n = [], None, 0
    area = pick = 0
    try:
        while True:
            if game is None:
                n += 1
                game = Game(exe, disc, work / f'all{n}', ini='Cheats/God mode=1\n')
            try:
                game.ask(f'ini-set Fights/Area {area}')
            except Failed:
                break
            try:
                game.ask(f'ini-set Fights/Fight {pick}')
            except Failed:
                area, pick = area + 1, 0
                continue
            label = f'area {area} fight {pick}'
            if only is not None and (area, pick) not in only:
                pick += 1
                continue
            try:
                game.ask('mod-action Fights/Start fight')
                state = wait_battle(game)
                if state is None:
                    failures.append((area, pick, game.picker()['Last fight']))
                    print(f'{label}: NO BATTLE ({game.picker()["Last fight"]})', flush=True)
                else:
                    room = game.room()
                    enemies = ' '.join(f'{e["id"]}/{e["max_hp"]}' for e in state['enemies'])
                    print(f'{label}: M{int(room["map"]):03d} story {room["story"]} enemies {enemies}', flush=True)
                    win_when_ready(game)
                wait_home(game)
                check(game.room()['map'] == HOME['map'], f'{label}: not back in Carnegie Hall')
            except Failed as error:
                failures.append((area, pick, str(error)))
                print(f'{label}: STUCK ({error}); a new game for the next fight', flush=True)
                game.quit()
                game = None
            pick += 1
    finally:
        if game:
            game.quit()
    check(not failures, f'{len(failures)} fights failed: {failures}')


def main():
    exe, work = Path(sys.argv[1]), Path(sys.argv[2] if len(sys.argv) > 2 else tempfile.mkdtemp())
    disc = os.getenv('PE_TEST_DISC')
    if not disc or not Path(disc).exists():
        print('PE_TEST_DISC is not set: skipped')
        return 77
    work.mkdir(parents=True, exist_ok=True)
    try:
        if '--flight-pack' in sys.argv:
            part_flight(exe, disc, work, Path(sys.argv[sys.argv.index('--flight-pack') + 1]).resolve())
        elif '--room-variants' in sys.argv:
            discs = [os.getenv(key) for key in ('PE_TEST_RE2_DISC', 'PE_TEST_RE2_DISC2')]
            if not all(path and Path(path).is_file() for path in discs):
                print('Both RE2 discs are needed for --room-variants: skipped')
                return 77
            part_room_variants(exe, disc, work, [Path(path).resolve() for path in discs])
        elif '--street' in sys.argv or '--rpd' in sys.argv:
            re2_disc = os.getenv('PE_TEST_RE2_DISC')
            if not re2_disc or not Path(re2_disc).is_file():
                print('PE_TEST_RE2_DISC is not set: skipped')
                return 77
            test = part_rpd if '--rpd' in sys.argv else part_street
            test(exe, disc, work, Path(re2_disc).resolve())
        elif any(flag in sys.argv for flag in ('--imported-room', '--boss-forms', '--profile-editor')):
            re2_disc = os.getenv('PE_TEST_RE2_DISC')
            if not re2_disc or not Path(re2_disc).is_file():
                print('PE_TEST_RE2_DISC is not set: skipped')
                return 77
            part_imported_room(exe, disc, work, Path(re2_disc).resolve(), '--boss-forms' in sys.argv,
                               '--profile-editor' in sys.argv)
        elif '--all' in sys.argv:
            only = None   # --only AREA:FIGHT,... reruns some fights
            if '--only' in sys.argv:
                only = {tuple(int(v) for v in item.split(':'))
                        for item in sys.argv[sys.argv.index('--only') + 1].split(',')}
            part_all(exe, disc, work, only)
        else:
            game = Game(exe, disc, work / 'win-run', ini='Cheats/God mode=1\n', limit=FIELD + 60000)
            try:
                part_win(game)
                part_run(game)
            finally:
                game.quit()
            part_lose(exe, disc, work)
    except Failed as error:
        print(f'FAILED: {error}')
        return 1
    print('ok')
    return 0


if __name__ == '__main__':
    sys.exit(main())
