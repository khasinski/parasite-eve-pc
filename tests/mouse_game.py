"""Mouse support in the real game: the title, dialogue, the name entry, the Triangle menu, battle.

  python mouse_game.py GAME.exe [WORK_DIR [title,story,battle]]

Set PE_TEST_DISC; otherwise this asset test is skipped (77). Every run is headless, with its own
config folder and memory card under WORK_DIR (a fresh temporary folder when not given). The mouse
is driven with the --control-at command `mouse X Y ACTION FRAMES` at points of the game's screen;
what it did is read from the game's own state (the focused menu list's cursor, the dialogue
boxes) and from frame dumps.

  title   From the attract screen, mouse only: a click leaves "press Start", pointing at
          Tutorial and then New Game moves the menu's highlight there (the green band is read off
          the frame), and a click on New Game starts it: the first dialogue box comes up.
  story   A New Game on the route the text renderer's test plays, with the mouse in place of the
          pad where it can be: a click turns the first dialogue page; in the name entry the
          cursor follows the mouse over the letter grid and a click types the letter under it;
          in the field the middle button opens the Triangle menu. There, pointing moves the
          cursor along the icon column, the wheel steps it, a click opens the items screen
          (its list of two columns: pointing reaches the second), a right click goes back and a
          right click (the pad's Circle) closes the menu.
  battle  The recorded route into the first battle: with Aya's time bar full, the middle button
          opens the battle menu and pointing moves its cursor.
"""
import json
import os
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import text_renderer_game as game_run  # noqa: E402  (the headless runner and the recorded routes)

CROSS, START = 16384, 8


class Failed(Exception):
    pass


def check(condition, message):
    if not condition:
        raise Failed(message)


def last_state(work):
    """The game's last diagnostic state (printed at each control point)."""
    for line in reversed((work / 'game.log').read_text(encoding='utf-8', errors='replace').splitlines()):
        if line.startswith('{') and '"menu":' in line:
            return json.loads(line)
    raise Failed(f'no game state in {work / "game.log"}')


def driven(game, discs, work, start, steps, frame_limit, pad=()):
    """A run that pauses at `start` and then follows `steps`: (command, check) pairs. Each check
    gets the game's state after its command (and the frame, kept as frame.ppm); a step's command
    may be a callable taking that state. Returns the states seen."""
    steps = list(steps)
    seen = []
    pending = {'check': None, 'label': None}

    def drive(vblank, run_work):
        state = last_state(run_work)
        seen.append(state)
        if pending['check']:
            game_run.keep(run_work, pending['label'])
            pending['check'](state, run_work)
        if not steps:
            return None
        label, command, test = steps.pop(0)
        pending['check'], pending['label'] = test, label
        return command(state) if callable(command) else command

    game_run.run(game, discs, work, start=start, driver=drive, frame_limit=frame_limit, pad=pad)
    check(not steps, f'the run stopped before step {steps[0][0] if steps else ""}: {work}')
    return seen


def menu(state):
    return state.get('menu')


def expect_cursor(cell, selection=None):
    def test(state, work):
        m = menu(state)
        check(m is not None, f'no menu at vblank {state.get("vblanks")}: {work}')
        check(m['cursor'] == list(cell), f'cursor {m["cursor"]}, expected {list(cell)}: {work}')
        if selection is not None:
            check(m['selection'] == selection, f'menu {m["selection"]}, expected {selection}: {work}')
    return test


def cell_point(m, column, row):
    """The screen point at the middle of a cell of the focused list (its geometry from the game)."""
    x, y, w, h = m['list'][:4]
    return x + column * w + w // 2, y + (row - m['list'][7]) * h + h // 2


# ---- title ----------------------------------------------------------------------------------

def green_rows(work, label):
    """The rows of the title menu's area where the highlight's green is, as (first, last)."""
    width, height, pixels = game_run.frame(work, label)
    rows = [y for y in range(140, height) if sum(
        1 for x in range(96, 224)
        if pixels[(y * width + x) * 3 + 1] > 120 and pixels[(y * width + x) * 3 + 1] > 2 * pixels[(y * width + x) * 3]
    ) > 20]
    return (rows[0], rows[-1]) if rows else None


def title(game, discs, out):
    work = out / 'title'
    bands = {}

    def band(name):
        def test(state, w):
            bands[name] = green_rows(w, name)
        return test

    def started(state, w):
        boxes = state.get('dialogue') or []
        check(boxes and boxes[0].get('state') == 2 and boxes[0].get('page') == 9,
              f'a click on New Game did not reach the first dialogue box ({boxes[:1]}): {w}')

    driven(game, discs, work, 9990, [
        ('start', 'mouse 160 120 left 400', band('start')),
        ('tutorial', 'mouse 160 212 move 30', band('tutorial')),
        ('new-game', 'mouse 160 172 move 30', band('new-game')),
        ('box', 'mouse 160 172 left 7000', started),
    ], frame_limit=18000)
    check(bands['start'], f'no menu after the click on "press Start": {work}')
    check(bands['tutorial'] and bands['tutorial'][0] > 200,
          f'pointing at Tutorial: the highlight is at rows {bands["tutorial"]}: {work}')
    check(bands['new-game'] and 160 <= bands['new-game'][0] < 180,
          f'pointing at New Game: the highlight is at rows {bands["new-game"]}: {work}')
    print(f'PASS title: a click leaves "press Start"; the highlight follows the mouse '
          f'(Tutorial rows {bands["tutorial"]}, New Game rows {bands["new-game"]}); a click starts a New Game')


# ---- story: dialogue, name entry, the Triangle menu ----------------------------------------

def story(game, discs, out):
    work = out / 'story'
    first = game_run.FIRST_BOX
    log = []

    def box_open(state, w):
        boxes = state.get('dialogue') or []
        check(boxes and boxes[0].get('state') == 2 and boxes[0].get('page') == 9,
              f'the first box is not up at {state.get("vblanks")}: {w}')

    def page_turned(state, w):
        boxes = state.get('dialogue') or []
        check(boxes and boxes[0].get('state') == 0, f'a click did not turn the first box\'s page ({boxes[:1]}): {w}')
        log.append('dialogue')

    def advance_to(tick):
        return lambda state: f'advance {tick - state["vblanks"]} 0'

    def grid_point(column, row, action='move', frames=10):
        return lambda state: (lambda p: f'mouse {p[0]} {p[1]} {action} {frames}')(cell_point(menu(state), column, row))

    def press_at(tick, mask):
        return lambda state: f'advance {tick - state["vblanks"]} 0' if state['vblanks'] < tick else f'advance 10 {mask}'

    steps = [
        ('first', 'advance 1 0', box_open),
        ('clicked', 'mouse 160 100 left 60', page_turned),
        ('box2', advance_to(20630), None), ('box2-cross', f'advance 10 {CROSS}', None),
        ('profile', advance_to(22140), None), ('profile-cross', f'advance 10 {CROSS}', None),
        ('grid', advance_to(22450), lambda s, w: check(menu(s) and menu(s)['list'][4] == 11,
                                                         f'no 11-wide name grid: {menu(s)}: {w}')),
        ('grid-w', grid_point(8, 5), expect_cursor((8, 5))),
        ('grid-b', grid_point(1, 0), expect_cursor((1, 0))),
        ('typed', grid_point(1, 0, 'left', 40), expect_cursor((1, 0))),
        ('end1', advance_to(22760), None), ('end1-start', f'advance 10 {START}', None),
        ('end2', advance_to(23070), None), ('end2-start', f'advance 10 {START}', None),
    ]
    for n, k in enumerate((0, 1510, 3020, 4530, 6660)):
        steps += [(f'box{3 + n}', advance_to(23380 + k), None), (f'box{3 + n}-cross', f'advance 10 {CROSS}', None)]

    def menu_open(state, w):
        m = menu(state)
        check(m and m['selection'] == 0 and m['list'][:6] == [10, 34, 16, 16, 1, 5],
              f'the middle button did not open the Triangle menu\'s icon column: {m}: {w}')
        log.append('middle opens the menu')

    def items_open(state, w):
        m = menu(state)
        check(m and m['selection'] == 1 and m['list'][4] == 2, f'a click on the first icon did not open the items: {m}: {w}')

    def closed(state, w):
        check(menu(state) is None, f'a right click on the menu top did not close it: {menu(state)}: {w}')

    steps += [
        ('field', advance_to(31550), None),
        ('middle', 'mouse 160 112 middle 310', menu_open),
        ('icon2', 'mouse 18 74 move 10', expect_cursor((0, 2), 0)),
        ('wheel-up', 'mouse 18 74 up 20', expect_cursor((0, 1), 0)),
        ('wheel-down', 'mouse 18 74 down 20', expect_cursor((0, 2), 0)),
        ('items', 'mouse 18 42 left 30', items_open),
        ('items-right', grid_point(1, 0), expect_cursor((1, 0), 1)),
        ('back', 'mouse 200 150 right 30', expect_cursor((0, 0), 0)),
        ('close', 'mouse 200 150 right 60', closed),
    ]
    driven(game, discs, work, first, steps, frame_limit=34000, pad=game_run.NEW_GAME)
    before, after = (game_run.lit(work, label, game_run.NAME_ENTRY) for label in ('grid-b', 'typed'))
    check(after > before, f'a click on a letter typed nothing: the name has {before} lit pixels, then {after}: {work}')
    print('PASS story: a click turns the dialogue page; the name grid\'s cursor follows the mouse and a click '
          'types; the middle button opens the Triangle menu, pointing and the wheel move its cursor, a click '
          'opens the items (both columns reachable), a right click goes back and closes it')


# ---- battle -------------------------------------------------------------------------------

def battle(game, discs, out):
    work = out / 'battle'

    def opened(state, w):
        m = menu(state)
        check(m and m['list'][:5] == [10, 34, 16, 16, 1] and (state.get('combat') or {}).get('mode') == 1,
              f'the middle button did not open the battle menu: {m} {state.get("combat")}: {w}')

    driven(game, discs, work, 65600, [
        ('battle-menu', 'mouse 160 112 middle 60', opened),
        ('row3', 'mouse 18 34 move 1', None),
        ('row3b', 'mouse 18 90 move 10', expect_cursor((0, 3))),
    ], frame_limit=66000, pad=game_run.BATTLE_ROUTE)
    print('PASS battle: the middle button opens the battle menu and its cursor follows the mouse')


PARTS = {'title': title, 'story': story, 'battle': battle}


def main(game, work=None, parts='title,story,battle'):
    disc = os.getenv('PE_TEST_DISC')
    if not disc or not Path(disc).exists():
        print('PE_TEST_DISC is not set: skipped')
        return 77
    out = (Path(work) if work else Path(tempfile.mkdtemp(prefix='pe-mouse-'))).resolve()
    for part in parts.split(','):
        if (out / part).exists():
            shutil.rmtree(out / part)
    try:
        for part in parts.split(','):
            PARTS[part](Path(game).resolve(), [disc], out)
    except (Failed, game_run.Failed) as error:
        print(f'FAIL {error}')
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(*sys.argv[1:]))
