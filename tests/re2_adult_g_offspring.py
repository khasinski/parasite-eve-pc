"""Exercise the adult G's in-battle offspring using the Leon disc's room placements."""

import argparse
import json
import os
import re
import struct
from pathlib import Path

from enemy_import_fights import Failed, Game, check, start_fight, wait_home, win_when_ready
from re2_alligator_canister import TESTING, encounter_log, room_state


ROOM = "Room 3-03"
SUMMON = re.compile(r"\bbattle: EM28\S* summoned EM27\S*")


def release_placements(config):
    # Seed the room with its own adult G and initial offspring. The room-session test
    # exercises the original entrance; this fixture isolates the native fight and return.
    catalog = config / "import/store/re2/catalog.json"
    source = json.loads(catalog.read_text(encoding="utf-8"))["re2:logic/303-0"]["sources"][0]
    logic = catalog.parent / source["sha256"] / source["item"] / "roomlogic.bin"
    fixture = bytearray(logic.read_bytes())
    at = 28
    while fixture[at:at + 4] != b"RDT0":
        check(at + 12 <= len(fixture), "No RDT section in room 3-03")
        length, = struct.unpack_from("<I", fixture, at + 8)
        at += 12 + (length + 3 & ~3)
    rdt = at + 12
    init, main, end = struct.unpack_from("<3I", fixture, rdt + 8 + 16 * 4)
    placements = {}
    for i in range(rdt + main, rdt + end - 21):
        row = fixture[i:i + 22]
        if row[:6] in (b"\x44\0\0\x28\0\0", b"\x44\0\x01\x27\0\0") and row[9] in (0x62, 0xff):
            placements[row[2]] = row
    check(set(placements) == {0, 1}, "Missing the adult G's first placements in room 3-03")
    # Init proc 0: place both actors, select enemy 0, set Sce_free0 to release the adult.
    script = b"\x02\0" + placements[0] + placements[1] + b"\x2e\x03\0\x34\x17\x01\0\x01\0"
    check(len(script) <= main - init, "The room's init section is too short for the fixture")
    fixture[rdt + init:rdt + init + len(script)] = script
    logic.write_bytes(fixture)


def run_route(exe, disc, re2_disc, work):
    def setup(config):
        folders = config / "import" / "folders.txt"
        folders.parent.mkdir()
        folders.write_text(f"{re2_disc.parent}\n", encoding="utf-8")

    game = Game(exe, str(disc), work, setup=setup, ini="Cheats/God mode=1\nManage mods/Play imported games=1\n")
    try:
        game.ask("mod-action Play imported games/Start")
        release_placements(game.config)
        game.ask(f"ini-set {TESTING}/Stage 3")
        game.ask(f"ini-set {TESTING}/Room 3")
        jump = f"mod-action {TESTING}/Go to that room"
        game.ask(jump)
        before = room_state(game)
        check(before["Status"].startswith(ROOM) and "0x28" in before["Enemies"] and "0x27" in before["Enemies"], before)
        start_fight(game, 0, 1)

        end = game.vblank + 4000
        while game.vblank < end:
            enemies = game.battle()["enemies"]
            if len(enemies) == 2 and all(enemy["type"] == 0xee for enemy in enemies):
                break
            game.adv(15)
        else:
            raise Failed(f"Adult G and its placed offspring never joined: {room_state(game)}")
        log = encounter_log(game)
        check(re.search(r"\bbattle: EM28\S* joins", log) and re.search(r"\bbattle: EM27\S* joins", log), enemies)
        check(not SUMMON.search(log), "A summoned offspring was present at battle entry")

        end = game.vblank + 4000
        while game.vblank < end and not SUMMON.search(encounter_log(game)):
            game.adv(12)
        enemies = game.battle()["enemies"]
        check(SUMMON.search(encounter_log(game)) and len(enemies) == 3, f"No third combatant: {enemies}")
        check(all(enemy["type"] == 0xee and enemy["hp"] > 0 for enemy in enemies), enemies)
        win_when_ready(game)
        wait_home(game)
        check(game.picker()["Last fight"].endswith(": won"), game.picker())
        after = room_state(game)
        check(after["Status"].startswith(ROOM) and not any(kind in after["Enemies"] for kind in ("0x28", "0x27")), after)
        game.ask(jump)
        check("0x28" not in room_state(game)["Enemies"], "The adult G returned after re-entering its room")
        print("Adult G: an offspring joined during battle and the adult's defeat persisted on re-entry")
    finally:
        game.quit()


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
