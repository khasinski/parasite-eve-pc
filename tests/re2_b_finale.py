"""Replay the RE2 B finale in the desktop game from an isolated native card fixture.

Set PE_TEST_DISC, PE_TEST_RE2_LEON_DISC or PE_TEST_RE2_CLAIRE_DISC, and the
matching PE_TEST_RE2_LEON_B_FIXTURE or PE_TEST_RE2_CLAIRE_B_FIXTURE directory.
The directory contains bu00/ with a single slot-A save and its .pe-ext
companion, plus pads.json. Use the last convenient save before the Tyrant,
with a weapon equipped and the required RE2 progression items. pads.json
contains "approach" and "escape" arrays of [vblanks, raw_pad_mask] pairs.
Record approach up to the 7-02 battle cue, and escape from the Tyrant
return pose up to the 7-04 battle cue. An empty array waits for the cue at
the current position. An optional "ending" array continues from the train
fight return pose through any final movement or dialogue. Input masks match the debug harness advance command.
"""

import argparse
import json
import os
import shutil
import struct
from pathlib import Path
from zlib import crc32

from enemy_import_fights import CROSS, Failed, Game, check, wait_home
from re2_alligator_canister import encounter_log, room_state
from re2_rpd_licker import load_card_pad


SQUARE = 0x8000
INI = "Cheats/God mode=1\nManage mods/Play imported games=1\n"
TYRANT = (6, 2)  # Stage is zero-based in the save.
DROP = "battle: Tyrant rocket launcher dropped"
PICKUP = "battle: Aya picked up the Tyrant rocket launcher"
FIRE = "battle: Aya fired the Tyrant rocket launcher"


def progress_from(path):
    """Read the version-2 RE play section from the host's PEEX companion."""
    data = path.read_bytes()
    check(len(data) >= 96 and data[:8] == b"PEEX\x01\x00\x60\x00", f"{path}: invalid PEEX header")
    size, count = struct.unpack_from("<IH", data, 12)
    check(size == len(data) and 96 + count * 64 <= size, f"{path}: invalid PEEX directory")
    for entry in range(96, 96 + count * 64, 64):
        if data[entry:entry + 48].split(b"\0", 1)[0] != b"pe_re_play/progress":
            continue
        version, _, offset, length, checksum = struct.unpack_from("<HHIII", data, entry + 48)
        section = data[offset:offset + length]
        check(version == 2 and length == len(section) == 1808 and crc32(section) == checksum,
              f"{path}: invalid RE play section")
        check(section[:2] == b"\x02\x01", f"{path}: save is outside active RE2 play")
        return section
    raise Failed(f"{path}: no RE play progress section")


def flag(progress, group, bit):
    return bool(progress[304 + group * 32 + bit // 8] & (1 << (bit % 8)))


def take_snapshot(game, destination=None):
    card_name, sep, companion_name = game.ask("save-block 1").partition(" companion=")
    check(sep and card_name and companion_name, "The native save has no companion")
    progress = progress_from(game.ctl / "companion.pex")
    if destination is not None:
        companions = destination / ".pe-ext"
        companions.mkdir(parents=True)
        shutil.copy2(game.ctl / "block.bin", destination / card_name)
        shutil.copy2(game.ctl / "companion.pex", companions / companion_name)
    return progress


def check_campaign(progress, player):
    check(progress[2] == player and flag(progress, 1, 1) and not flag(progress, 1, 6),
          "The native card did not restore the requested B campaign")


def check_inventory(before, after):
    check(before[16:304] == after[16:304] and before[1040:1296] == after[1040:1296],
          "Door unlocks, pocket inventory or item-box inventory changed across reload")


def wait_for_boss(game, kind, pads):
    for frames, mask in pads:
        game.adv(frames, mask)
        if f"battle: EM{kind:02X}" in encounter_log(game):
            break
    until = game.vblank + 4000
    while game.vblank < until:
        battle = game.battle()
        if battle["active"] and battle["enemies"] and f"battle: EM{kind:02X}" in encounter_log(game):
            check(len(battle["enemies"]) == 1 and battle["enemies"][0]["type"] == 0xee,
                  f"Unexpected boss battle roster: {battle['enemies']}")
            return
        game.adv(20)
    raise Failed(f"EM{kind:02X} did not enter the native battle: {room_state(game)} / {game.picker()}")


def fight(game, launcher=False):
    until = game.vblank + 24000
    while game.vblank < until:
        log = encounter_log(game) if launcher else ""
        if launcher and FIRE in log:
            return
        battle = game.battle()
        if not battle["active"] or not any(enemy["hp"] > 0 for enemy in battle["enemies"]):
            check(not launcher, "The Tyrant battle ended before the launcher hit")
            return
        ready = battle["player"]["atb"] >= 9000
        if launcher and DROP in log and battle["mode"] == 0:
            if PICKUP not in log or ready:
                game.press(SQUARE, after=25)
            else:
                game.adv(30)
        elif battle["mode"] == 0 and not ready:
            game.adv(30)
        else:
            game.press(CROSS, after=55)
    raise Failed(f"Battle did not finish: {game.battle()}")


def run_route(exe, disc, re2_disc, fixture, work, player):
    ports = json.loads((fixture / "pads.json").read_text(encoding="utf-8"))
    check(isinstance(ports, dict), "pads.json must contain a replay object")
    ports.setdefault("ending", [])
    for name in ("approach", "escape", "ending"):
        pads = ports.get(name)
        check(isinstance(pads, list) and all(isinstance(p, list) and len(p) == 2 and
              all(type(v) is int for v in p) and 0 < p[0] <= 600 and 0 <= p[1] <= 65535 for p in pads),
              f"pads.json: {name} must contain [vblanks (1–600), raw_pad_mask] pairs")
    original_card = fixture / "bu00"
    check((original_card / "BASLUS-006620000000A").is_file() and (original_card / ".pe-ext").is_dir(),
          f"Missing slot A or its companion in {original_card}")
    # Game recreates each output directory. Reject overlapping fixture or disc inputs.
    for output in (work / "tyrant", work / "reload"):
        for source in (fixture, disc, re2_disc):
            check(not source.is_relative_to(output) and not output.is_relative_to(source),
                  f"Replay output overlaps an input: {output}")

    def setup(config):
        folders = config / "import" / "folders.txt"
        folders.parent.mkdir()
        folders.write_text(f"{re2_disc.parent}\n", encoding="utf-8")
        shutil.copytree(original_card, config.parent / "bu00")

    game = Game(exe, str(disc), work / "tyrant", setup=setup, ini=INI,
                boot_pad=load_card_pad(), control_at=16400)
    try:
        before = take_snapshot(game)
        check_campaign(before, player)
        check(not flag(before, 0, 0x0a), "The fixture has already reached its ending")
        game.ask("set-stat level 99")
        wait_for_boss(game, 0x2b, ports["approach"])
        fight(game, launcher=True)
        wait_home(game)
        check(game.picker()["Last fight"] == "RE2 room 7-02: won", game.picker())
        check("0x2B" not in room_state(game)["Enemies"], "Tyrant still present after victory")
        after = take_snapshot(game, game.work / "post-tyrant-card")
        check(after[3:5] == bytes(TYRANT), "The Tyrant battle returned Aya to the wrong room")
        check_campaign(after, player)
    finally:
        game.quit()

    def install(config):
        shutil.move(game.config / "import", config / "import")
        shutil.copytree(game.work / "post-tyrant-card", config.parent / "bu00")

    loaded = Game(exe, str(disc), work / "reload", setup=install, ini=INI,
                  boot_pad=load_card_pad(), control_at=16400)
    try:
        restored = take_snapshot(loaded)
        check_campaign(restored, player)
        check(restored[3:5] == bytes(TYRANT), "Native reload did not restore the Tyrant room")
        check_inventory(after, restored)
        check(restored[496:560] == after[496:560], "Enemy defeat flags changed across native reload")
        check("0x2B" not in room_state(loaded)["Enemies"], "Tyrant respawned after native reload")
        wait_for_boss(loaded, 0x36, ports["escape"])
        fight(loaded)
        wait_home(loaded)
        check(loaded.picker()["Last fight"] == "RE2 room 7-04: won", loaded.picker())
        for frames, mask in ports["ending"]:
            loaded.adv(frames, mask)
        for _ in range(12):
            loaded.press(CROSS, after=120)
            ended = take_snapshot(loaded)
            if flag(ended, 0, 0x0a):
                check_campaign(ended, player)
                print("Tyrant launcher win, native reload and B ending verified")
                return
        raise Failed(f"The ending flag never set after the train fight: {room_state(loaded)}")
    finally:
        loaded.quit()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path)
    parser.add_argument("work", type=Path)
    parser.add_argument("campaign", choices=("claire", "leon"))
    args = parser.parse_args()
    campaign = args.campaign.upper()
    inputs = {}
    for name in ("PE_TEST_DISC", f"PE_TEST_RE2_{campaign}_DISC", f"PE_TEST_RE2_{campaign}_B_FIXTURE"):
        value = os.getenv(name)
        if not value:
            print(f"{name} is unset: skipped")
            return 77
        inputs[name] = Path(value).resolve()
    disc, re2_disc, fixture = inputs.values()
    try:
        check(disc.is_file() and re2_disc.is_file() and fixture.is_dir(),
              "Configured discs or fixture directory do not exist")
        run_route(args.exe.resolve(), disc, re2_disc, fixture, args.work.resolve(), int(campaign == "CLAIRE"))
    except (Failed, OSError, ValueError) as error:
        print(f"FAILED ({args.campaign} B): {error}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
