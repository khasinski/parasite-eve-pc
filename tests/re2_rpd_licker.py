"""Run the 2-04 scripted Licker through battle and a native card reload."""

import argparse
import os
import shutil
from pathlib import Path

from enemy_import_fights import CROSS, DOWN, START, TRIANGLE, UP, Failed, Game, check, wait_home
from re2_alligator_canister import TESTING, encounter_log, room_state


INI = "Cheats/God mode=1\nManage mods/Play imported games=1\n"
ROOM = "Room 2-04"
SAVE_ROOM = "Room 2-08"
ENCOUNTER = "room 2-04: encounter with enemy slot 1 at -14869,-3479"
RIGHT = 0x0080  # Raw pad Right.
CIRCLE = 0x2000  # Back out of the native memory-card menu.


def load_card_pad():
    # Title > Continue > card 1 > slot A > confirm.
    presses = [(10000, START), (11000, UP), (11500, DOWN), (11600, CROSS),
               (13500, CROSS), (14520, CROSS), (15540, TRIANGLE), (15670, CROSS)]
    return [(frame + offset, mask if offset == 0 else 0)
            for frame, mask in presses for offset in (0, 10)]


def fight_licker(game):
    until = game.vblank + 4000
    while game.vblank < until:
        enemies = game.battle()["enemies"]
        if enemies and all(enemy["type"] == 0xee for enemy in enemies):
            break
        game.adv(15)
    else:
        raise Failed(f"Licker never joined the battle: {game.picker()}")
    check(len(enemies) == 1 and "battle: EM22" in encounter_log(game),
          f"Unexpected imported roster: {enemies}")

    until = game.vblank + 14000
    while game.vblank < until:
        battle = game.battle()
        if not battle["active"] or not any(enemy["hp"] > 0 for enemy in battle["enemies"]):
            return
        if battle["player"]["atb"] < 9000 and battle["mode"] == 0:
            game.adv(30)
        else:
            game.press(CROSS, after=55)
    raise Failed(f"The Licker battle did not end: {game.battle()}")


def jump_to_licker(game):
    game.ask(f"ini-set {TESTING}/Stage 2")
    game.ask(f"ini-set {TESTING}/Room 4")
    game.ask(f"mod-action {TESTING}/Go to that room")


def save_at_typewriter(game):
    game.ask(f"ini-set {TESTING}/Room 8")
    game.ask(f"mod-action {TESTING}/Go to that room")
    game.adv(250)
    check(room_state(game)["Status"].startswith(SAVE_ROOM), room_state(game))
    game.ask(f"mod-action {TESTING}/Give an Ink Ribbon")
    game.ask(f"mod-action {TESTING}/Go to the next typewriter")
    state = room_state(game)
    check("Ink Ribbon" in state["Pocket"], state)
    game.press(CROSS)
    game.press(CROSS)
    check("typewriter: save screen" in encounter_log(game),
          f"The typewriter did not open the native save screen: {room_state(game)}")
    card_dir = game.work / "bu00"
    for _ in range(15):
        if (card_dir / "BASLUS-006620000000A").is_file() and any((card_dir / ".pe-ext").glob("*.pex")):
            break
        game.press(CROSS, after=120)
    else:
        raise Failed("The native save card or its companion was not written")
    for _ in range(6):
        state = room_state(game)
        if state["Status"].startswith("Saved at the typewriter."):
            check("Ink Ribbon" not in state["Pocket"], state)
            return
        game.press(CIRCLE, after=100)
    raise Failed(f"The native save menu did not return to the room: {room_state(game)}")


def run_route(exe, disc, re2_disc, work):
    def import_disc(config):
        folders = config / "import" / "folders.txt"
        folders.parent.mkdir()
        folders.write_text(f"{re2_disc.parent}\n", encoding="utf-8")

    game = Game(exe, str(disc), work / "fight", setup=import_disc, ini=INI)
    try:
        game.ask("set-stat level 99")
        game.ask("mod-action Play imported games/Start")
        jump_to_licker(game)
        game.adv(250)
        before = room_state(game)
        check(before["Status"].startswith(ROOM) and before["Enemies"].count("0x22") == 1, before)
        check(game.picker()["Status"] == "Ready" and ENCOUNTER not in encounter_log(game),
              "The parked Licker started a battle before the room cue")

        # The third live event zone is the automatic Licker entrance in this room.
        for _ in range(3):
            game.ask(f"mod-action {TESTING}/Go to the next event zone")
        game.adv(350)
        scene = room_state(game)
        check("x -15550, z -8371" in scene["Status"] and "0 tasks running" in scene["Scripts"], scene)
        check(game.picker()["Status"] == "Ready", "The Licker started a battle before Aya approached")
        game.adv(53, DOWN)  # Along the corridor from the cutscene's release point.
        if ENCOUNTER not in encounter_log(game):
            game.adv(24, RIGHT)  # Turn across the corridor after the camera changes.
        check(ENCOUNTER in encounter_log(game), f"No Licker encounter on approach: {room_state(game)}")
        fight_licker(game)
        wait_home(game)
        after = room_state(game)
        check(after["Status"].startswith(ROOM) and "0x22" not in after["Enemies"], after)
        check(game.picker()["Last fight"] == "RE2 RPD 2-04: won", game.picker())
        check(after["Enemies"].count("0x1F") == before["Enemies"].count("0x1F"), after)
        save_at_typewriter(game)
    finally:
        game.quit()

    def install_card(config):
        # The second process uses the same imported disc catalog and its own card directory.
        shutil.move(game.config / "import", config / "import")
        shutil.copytree(game.work / "bu00", config.parent / "bu00")

    loaded = Game(exe, str(disc), work / "reload", setup=install_card, ini=INI,
                  boot_pad=load_card_pad(), control_at=16400)
    try:
        restored = room_state(loaded)
        check(restored["Status"].startswith(SAVE_ROOM) and "Ink Ribbon" not in restored["Pocket"],
              f"Native reload did not restore the typewriter save: {restored}")
        jump_to_licker(loaded)
        loaded.adv(250)
        revisited = room_state(loaded)
        check(revisited["Status"].startswith(ROOM) and revisited["Enemies"] == after["Enemies"], revisited)
        check(ENCOUNTER not in encounter_log(loaded), "The defeated Licker started a new encounter")
        print("RPD 2-04: Licker defeated in battle and still gone after reloading a native typewriter save")
    finally:
        loaded.quit()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path)
    parser.add_argument("work", type=Path)
    args = parser.parse_args()
    discs = {}
    for name in ("PE_TEST_DISC", "PE_TEST_RE2_DISC"):
        path = os.getenv(name)
        if not path or not Path(path).is_file():
            print(f"{name} is not set to a disc: skipped")
            return 77
        discs[name] = Path(path).resolve()
    try:
        run_route(args.exe.resolve(), discs["PE_TEST_DISC"], discs["PE_TEST_RE2_DISC"], args.work.resolve())
    except Failed as error:
        print(f"FAILED: {error}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
