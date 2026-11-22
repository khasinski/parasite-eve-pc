"""Exercise both outcomes of RE2 room 4-0A with the imported battle runtime."""

import argparse
import json
import os
from pathlib import Path

from enemy_import_fights import CROSS, Failed, Game, check, wait_battle, wait_home, win_when_ready


RIGHT = 0x0020  # Raw pad Right, used by the imported room's choice box.
CHOICE_OPENED = "Alligator: approach choice opened"
CANISTER_SHOT = "Aya shot the Alligator's gas canister"
TESTING = "Play imported games/Testing"


def room_state(game):
    rows = json.loads(game.ask("mod-state pe_re_play"))["rows"]
    return {row["label"]: row["value"] for row in rows}


def encounter_log(game):
    return (game.config / "pe_mods.log").read_text(encoding="utf-8", errors="replace")


def open_choice(game):
    for _ in range(7):
        game.ask(f"mod-action {TESTING}/Go to the next event zone")
        for _ in range(14):
            if CHOICE_OPENED in encounter_log(game):
                return
            game.press(CROSS, after=25)
    check(CHOICE_OPENED in encounter_log(game), f"Alligator choice did not open: {room_state(game)}")


def shoot_canister(game):
    for _ in range(80):
        if CANISTER_SHOT in encounter_log(game):
            return
        battle = game.battle()
        check(battle["active"], "The battle ended before a canister hit was recorded")
        if battle["player"]["atb"] < 9000 and battle["mode"] == 0:
            game.adv(30)
        else:
            game.press(CROSS, after=55)
    check(CANISTER_SHOT in encounter_log(game), f"No canister hit recorded: {game.battle()}")


def run_route(exe, disc, re2_disc, work, shortcut):
    def setup(config):
        folders = config / "import" / "folders.txt"
        folders.parent.mkdir()
        folders.write_text(f"{re2_disc.parent}\n", encoding="utf-8")

    route = "canister" if shortcut else "standard"
    game = Game(exe, str(disc), work / f"alligator-{route}", setup=setup,
                ini="Cheats/God mode=1\nManage mods/Play imported games=1\n")
    try:
        game.ask("mod-action Play imported games/Start")
        game.ask(f"ini-set {TESTING}/Stage 4")
        game.ask(f"ini-set {TESTING}/Room 10")
        jump = f"mod-action {TESTING}/Go to that room"
        game.ask(jump)
        before = room_state(game)
        check(before["Status"].startswith("Room 4-0A") and "0x23" in before["Enemies"], before)
        check(game.picker()["Status"] == "Ready", "The fight started before the room event")
        open_choice(game)
        if shortcut:
            game.press(RIGHT, after=10)
        game.press(CROSS, after=65)
        check(wait_battle(game, 2200) is not None, f"{route}: the Alligator battle did not start")
        if shortcut:
            shoot_canister(game)
        else:
            win_when_ready(game)
        wait_home(game)
        check(game.picker()["Last fight"] == "RE2 room 4-0A: won", game.picker())
        check((CANISTER_SHOT in encounter_log(game)) == shortcut, f"{route}: wrong battle outcome")
        check("0x23" not in room_state(game)["Enemies"], f"{route}: Alligator still present after victory")
        game.ask(jump)
        after = room_state(game)
        check(after["Status"].startswith("Room 4-0A") and "0x23" not in after["Enemies"], after)
        print(f"Alligator {route} victory persisted after room re-entry")
    finally:
        game.quit()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path)
    parser.add_argument("work", type=Path)
    parser.add_argument("--route", choices=("both", "standard", "canister"), default="both")
    args = parser.parse_args()
    discs = {}
    for name in ("PE_TEST_DISC", "PE_TEST_RE2_DISC"):
        path = os.getenv(name)
        if not path or not Path(path).is_file():
            print(f"{name} is not set to a disc: skipped")
            return 77
        discs[name] = Path(path).resolve()
    try:
        for route in ("standard", "canister"):
            if args.route in ("both", route):
                run_route(args.exe.resolve(), discs["PE_TEST_DISC"], discs["PE_TEST_RE2_DISC"],
                          args.work.resolve(), route == "canister")
    except Failed as error:
        print(f"FAILED: {error}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
