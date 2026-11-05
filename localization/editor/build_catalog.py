"""Generate editor scene metadata from the USA discs without including their text."""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import sys
import struct
import zlib

EDITOR = Path(__file__).resolve().parent
sys.path.insert(0, str(EDITOR.parent))
from extract_text import bank_slots, box_calls, constant_writes
from petext import gametext
from petext.disc import Game
from petext.script import opcodes, task_block, walk


# Location IDs and long forms are documented in docs/ROOMS.md.
AREAS = {
    1: "Carnegie Hall", 2: "Theater sewers", 3: "NYPD 28th Precinct",
    4: "Central Park", 5: "Soho", 6: "St. Francis Hospital", 7: "Chinatown",
    8: "Chinatown sewers", 9: "Subway", 10: "Museum of Natural History",
    11: "Pier No. 4 Warehouse", 12: "Aircraft carrier", 13: "Statue of Liberty",
    14: "Cruiser",
}
# These checkpoints are named in mods/pe_warp/pe_warp.c.
CHECKPOINTS = {
    1: "Opera arrival", 2: "Stage", 23: "Eve’s first fight", 26: "Sewer entrance",
    34: "Alligator fight", 47: "Press conference", 52: "Wayne’s first visit",
    68: "Zoo",
}


def room_area(pack, map_number):
    # Resident header word 9 points to Text_GetMapLocationName's map-to-place table.
    offset = struct.unpack_from("<I", pack, 9 * 4)[0]
    area = AREAS.get(pack[offset + map_number])
    if area:
        return area
    if 288 <= map_number <= 318 or 382 <= map_number <= 424 or 434 <= map_number <= 437:
        return "Chrysler Building"  # The elevator bank is identified in docs/ROOMS.md.
    return None


def generate(discs):
    source = json.loads((EDITOR.parent / "data" / "manifest.json").read_text(encoding="utf-8"))
    source["entries"] = {key: row for key, row in source["entries"].items() if key.startswith("dlg/")}
    source["menu_metrics"] = None
    banks = {bank["crc32"]: bank for bank in source["banks"]}
    locations = defaultdict(dict)
    scenes = {}
    bank_areas = defaultdict(Counter)
    valid = opcodes(EDITOR.parents[1])
    for disc, path in enumerate(discs, 1):
        game = Game(path)
        try:
            serial = ("SLUS_006.62", "SLUS_006.68")[disc - 1]
            if serial not in game.disc.root_files():
                raise ValueError(f"Expected USA Disc {disc}.")
            pack = game.resident_pack()
            for number in range(1, 451):
                room = game.room(number)
                if room is None or 1 not in (slots := bank_slots(room.data)):
                    continue
                raw = room.data[slots[1]:]
                pages, size = gametext.pages(raw)
                if not size:
                    continue
                checksum = f"{zlib.crc32(raw[:size]):08x}"
                bank = banks[checksum]["key"]
                locations[checksum].setdefault(str(disc), number)
                code, complete = walk(task_block(room.data), valid)
                constants = constant_writes(code)
                selected = set()
                dynamic = not complete
                for _, _, page, _, _, register in box_calls(code):
                    if page is None:
                        candidates = constants.get(register, set()) & pages.keys()
                        dynamic |= not candidates
                        selected.update(candidates)
                    elif page in pages:
                        selected.add(page)
                shared = dynamic or not selected
                # A computed selector or a native caller can use the shared bank.
                keys = [f"dlg/{bank}/{page:03d}" for page in sorted(pages if shared else selected)]
                if not set(keys) <= source["entries"].keys():
                    raise ValueError(f"Scene M{number:03d} disagrees with the dialogue manifest.")
                area = room_area(pack, number)
                if area:
                    bank_areas[bank][area] += 1
                name = area or "Unlabelled scene"
                if number in CHECKPOINTS:
                    name += f" · {CHECKPOINTS[number]}"
                scenes[f"d{disc}-M{number:03d}"] = {
                    "label": f"{name} · M{number:03d} · Disc {disc}" + (" (shared dialogue)" if shared else ""),
                    "keys": keys,
                    "area": area,
                    "shared": shared,
                }
        finally:
            game.disc.f.close()
    if set(locations) != set(banks):
        raise ValueError("Both discs must cover every dialogue bank in the manifest.")
    for bank in source["banks"]:
        bank["locations"] = locations[bank["crc32"]]
        prefix = f"dlg/{bank['key']}/"
        areas = bank_areas[bank["key"]]
        area = areas.most_common(1)[0][0] if areas else None
        scenes[f"bank-{bank['key']}"] = {
            "label": f"{area or 'Unlabelled dialogue'} · Shared bank {bank['key']}",
            "area": area,
            "shared": True,
            "keys": [key for key in source["entries"] if key.startswith(prefix)],
        }
    demo = [f"dlg/M001/{number:03d}" for number in (9, 10, 13, 14, 15, 16, 17, 18)]
    source["scenes"] = {"opera": {"label": "Carnegie Hall · Opening opera conversation", "keys": demo,
                                    "area": "Carnegie Hall", "shared": False}, **scenes}
    return source


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("discs", nargs=2, type=Path)
    args = parser.parse_args()
    catalog = generate(args.discs)
    (EDITOR / "catalog.json").write_text(json.dumps(catalog, separators=(",", ":")) + "\n", encoding="utf-8")
    print(f"{len(catalog['entries'])} dialogue entries, {len(catalog['scenes'])} scene and bank views")
