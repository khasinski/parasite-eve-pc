"""Extract the game's text for localization, with a stable key and the box it shows in.

  python -I extract_text.py DISC1.cue [DISC2.cue] [--ja JP_DISC1.cue JP_DISC2.cue] [--out out/text]

With --ja, every key also gets the Japanese release's text (the `japanese` column), decoded with
data/ja_glyphs.tsv (build_glyph_table.py): the Japanese scripts open the same pages as the US ones
in 806 of 810 rooms, so the keys line up one to one.

Reads the player's own disc images (nothing from them goes into git: out/ is ignored) and writes:
  strings.json  every entry: key, English text (tagged, see petext/gametext.py), the original bytes,
                its page end, line widths in pixels, the boxes the room scripts show it in, the
                rooms that use it, and the Japanese page with the same number where there is one
  strings.csv   the same, one row per entry, for translators
  report.txt    counts, round-trip check, what couldn't be tied to a box
  items.json, items.csv, room_items.csv, key_items.json
                what the room scripts give, show, check for and take (see docs/LOCALIZATION.md)
  banks.json    each English message bank's key, size and CRC32: how a pack finds the room's
                dialogue at run time (pelang.py build puts them in the pack)
Keys:
  sys/NNN, item/NNN, desc/NNN, misc/NNN   the resident text tables (system messages, item names
                                          and descriptions, the rest); NNN is the table index
  dlg/MNNN/PPP   room dialogue: the message bank first used by room MNNN (banks are shared by
                 rooms of an area), page PPP
Boxes: "default" is the standard dialogue box (style 0, x 20, y 174); "styled x,y,w,h" is a box
whose rectangle the script set with opcode 208 before opening it with 209.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import zlib
import json
import re
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))
from petext import gametext as gt, jatext as ja, menu  # noqa: E402
from petext.disc import Game  # noqa: E402
from petext.script import MODE_IMMEDIATE, Instr, opcodes, task_block, walk  # noqa: E402

OP_BOX, OP_RECT, OP_STYLED_BOX = 13, 208, 209
TABLES = ["sys", "item", "desc", "misc"]       # Text_LookupTable4 / 8 / C / 10
# Item opcodes (src/main/script/system_opcodes.c, actor_opcodes.c); argument 0 is the item id.
ITEM_OPS = {167: "give", 168: "check", 232: "show"}     # Inv_AddItem, Inv_CountByValue, Menu_CreateItemUsePanel
OP_INV_COMMAND = 233                                    # Inv_DispatchCommand(cmd, value, ...)
INV_ITEM_COMMANDS = {1102: "check", 1112: "remove"}     # INV_CMD_COUNT_ITEM, INV_CMD_REMOVE_ITEM
OP_ASSIGN, OP_ITEM_MENU = 10, 169                       # Script_AssignValue, Entity_InitWithTimeout
OP_EVAL = 9                                             # Script_EvalExpr: 0 add, 1 subtract, ...
KEY_ITEMS = set(range(0x38, 0x3F)) | set(range(0xC8, 0xDD))
ITEM_NAME_BASE = -1                                     # item id N is named by item/(N-1)
DEBUG_ROOMS = {"M360"}                                  # the debug menu: "Get Bullets", "Map BGM 1", every key


def table_entries(pack: bytes):
    text = struct.unpack_from("<i", pack, 4)[0]
    offsets = struct.unpack_from("<4i", pack, text + 4)
    for t, toff in enumerate(offsets):
        base = text + toff
        count = struct.unpack_from("<H", pack, base)[0]
        for i in range(count):
            off = struct.unpack_from("<h", pack, base + 2 + 2 * i)[0]
            j = start = base + off
            while j < len(pack) and pack[j] != 0xFF:
                j += 2 if pack[j] in (0xFC, 0xFD) else (3 if pack[j] == 0xFB and pack[j + 1] in (7, 9) else (2 if pack[j] == 0xFB else 1))
            yield f"{TABLES[t]}/{i:03d}", pack[start:j]


def bank_slots(data: bytes) -> dict[int, int]:
    """A room's message banks by task slot (0 Japanese, 1 English): offset in the data run."""
    d = struct.unpack_from("<I", data, 4)[0]
    ts = struct.unpack_from("<I", data, d + 0x20)[0]
    banks = {}
    for i in range(ts >> 22):
        word = struct.unpack_from("<I", data, (ts & 0x3FFFFF) + i * 8 + 4)[0]
        banks[word >> 24] = word & 0xFFFFFF
    return banks


def box_calls(code: list[Instr]):
    """Every text box a room's scripts open: (script, offset, page or None, box, page operand mode,
    the page's variable (mode, index) when it is computed). A styled box uses the rectangle the
    same script last set with opcode 208."""
    calls, rect, script = [], None, None
    for i in code:
        if i.script != script:
            rect, script = None, i.script
        if i.op == OP_RECT:
            r = [i.imm(k) for k in range(4)]
            rect = tuple(a & 0xFFFF for a in r) if None not in r else "computed"
        elif i.op in (OP_BOX, OP_STYLED_BOX) and i.args:
            page = i.imm(0)
            if i.op == OP_BOX:
                box = "default"
            elif isinstance(rect, tuple):
                box = "styled {},{},{},{}".format(*rect)
            else:
                box = "styled (rectangle " + ("computed" if rect else "not set in this script") + ")"
            reg = None if page is not None else (i.modes[0], i.args[0])
            calls.append((i.script, i.at, None if page is None else page & 0xFFFF, box, i.modes[0], reg))
    return calls


def constant_writes(code: list[Instr]) -> dict[tuple[int, int], set[int]]:
    """The constants a room's scripts write into each variable: AssignValue of an immediate, or
    EvalExpr adding or subtracting two immediates. A box that opens the page held in a variable
    can show any of them (the room's scripts share their node and script variables)."""
    out: dict[tuple[int, int], set[int]] = defaultdict(set)
    for i in code:
        if i.op == OP_ASSIGN and len(i.args) == 2 and i.modes[1] == MODE_IMMEDIATE:
            out[(i.modes[0], i.args[0])].add(i.args[1] & 0xFFFF)
        elif (i.op == OP_EVAL and len(i.args) == 4 and i.args[0] in (0, 1) and i.modes[0] == MODE_IMMEDIATE
              and i.modes[2] == MODE_IMMEDIATE and i.modes[3] == MODE_IMMEDIATE):
            v = i.args[2] + i.args[3] if i.args[0] == 0 else i.args[2] - i.args[3]
            out[(i.modes[1], i.args[1])].add(v & 0xFFFF)
    return out


def item_source(code: list[Instr], k: int, arg: int):
    """(item id or None, where it comes from) for argument arg of code[k]. A variable is followed
    back through AssignValue copies within the script, in code order (a chest copies its constant
    through two variables); one the item menu (opcode 169) wrote is the item the player chose to
    leave there, given back later. This doesn't follow branches, so an id is the last constant
    written before the call in the code, which is what every chest in the game does."""
    i = code[k]
    if i.imm(arg) is not None:
        return i.imm(arg), "immediate"
    reg, hops = (i.modes[arg], i.args[arg]), 0
    for j in reversed(code[:k]):
        if j.script != i.script:
            break
        if j.op == OP_ITEM_MENU and len(j.args) > 1 and (j.modes[1], j.args[1]) == reg:
            return None, "player's choice (item menu)"
        if j.op == OP_ASSIGN and (j.modes[0], j.args[0]) == reg:
            if j.modes[1] == MODE_IMMEDIATE:
                return (j.args[1], "assigned constant") if j.args[1] else (None, "computed")
            reg, hops = (j.modes[1], j.args[1]), hops + 1
            if hops > 4:
                break
    return None, "computed"


def item_events(code: list[Instr]):
    """What a room's scripts do with items: (instr, kind, item id or None, its source)."""
    for k, i in enumerate(code):
        if i.op in ITEM_OPS:
            yield (i, ITEM_OPS[i.op]) + item_source(code, k, 0)
        elif i.op == OP_INV_COMMAND and i.imm(0) in INV_ITEM_COMMANDS:
            yield (i, INV_ITEM_COMMANDS[i.imm(0)]) + item_source(code, k, 1)


def add_japanese(entries: dict, discs: list[str], room_bank: dict, room_boxes: dict, valid: set[int]):
    """Put the Japanese discs' text on the same keys: the resident tables by index, each room's
    Japanese bank (task slot 0) by the US bank key of the same room. A room counts as aligned when
    its Japanese scripts open the same pages in the same order as the US ones (806 of 810 rooms
    with text do); its pages are flagged otherwise. Returns (counts, Japanese pages with no US key)."""
    table = ja.GlyphTable()
    stats = {"keys": 0, "table": 0, "dialogue": 0, "rooms": 0, "aligned": 0, "unaligned": [], "conflicts": 0}
    only = []

    def put(key, raw, rtype, aligned):
        text = table.decode(raw, rtype)
        e = entries[key]
        if "ja" in e:
            if e["ja"] != text:
                e.setdefault("ja_variants", set()).add(text)
            return
        e.update(ja=text, ja_raw=raw.hex(" "), ja_aligned=aligned)
        stats["keys"] += 1
        stats["dialogue" if key.startswith("dlg/") else "table"] += 1

    for n, path in enumerate(discs, 1):
        g = Game(path)
        if g.region != "Japan":
            raise SystemExit(f"--ja {path}: not a Japanese disc ({g.exe_name})")
        if n == 1:
            for key, raw in table_entries(g.resident_pack()):
                if key in entries:
                    put(key, raw, None, True)
        for m in range(1, 451):
            room = g.room(m)
            slots = bank_slots(room.data) if room else {}
            if 0 not in slots or (n, m) not in room_bank:
                continue
            stats["rooms"] += 1
            code, _ = walk(task_block(room.data), valid)
            aligned = [(s, p) for s, _, p, _, _, _ in box_calls(code)] == room_boxes[(n, m)]
            if aligned:
                stats["aligned"] += 1
            else:
                stats["unaligned"].append(f"disc{n}/M{m:03d}")
            rtype = ja.room_type(room.data)
            for pid, (raw, end) in sorted(gt.pages(room.data[slots[0]:])[0].items()):
                key = f"dlg/{room_bank[(n, m)]}/{pid:03d}"
                if key in entries:
                    put(key, raw, rtype, aligned)
                elif not any(o["key"] == key for o in only):
                    only.append({"key": key, "room": f"M{m:03d}", "japanese": table.decode(raw, rtype)})
    for e in entries.values():
        if e.pop("ja_variants", None):
            stats["conflicts"] += 1
    return stats, only


def item_name(entries: dict, item) -> str:
    if item is None:
        return "(computed)"
    e = entries.get(f"item/{item + ITEM_NAME_BASE:03d}")
    return gt.decode(e["raw"]) if e else f"(item {item})"


def write_items(out: Path, items: list[dict], entries: dict, script_pages: dict) -> dict:
    """items.json / items.csv: every item event with the text its script shows nearest after it;
    room_items.csv: per room; key_items.json: for each key item, where it is given, checked and removed."""
    for ev in items:
        ev["item_hex"] = None if ev["item"] is None else f"{ev['item']:02X}"
        ev["name"] = item_name(entries, ev["item"])
        ev["key_item"] = ev["item"] in KEY_ITEMS
        ev["debug_room"] = ev["room"] in DEBUG_ROOMS
        pages = sorted(script_pages.get((ev["discs"][0], int(ev["room"][1:]), ev["script"]), ()))
        after = [k for at, k in pages if at > ev["offset"]]
        before = [k for at, k in pages if at < ev["offset"]]
        ev["text_key"] = after[0] if after else (before[-1] if before else None)
        ev["text"] = gt.decode(entries[ev["text_key"]]["raw"]) if ev["text_key"] else None
    (out / "items.json").write_text(json.dumps(items, indent=1, ensure_ascii=False), encoding="utf-8")
    with open(out / "items.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["discs", "room", "script", "offset", "kind", "item", "name", "source", "key_item", "debug_room",
                    "text_key", "text"])
        for ev in items:
            w.writerow([" ".join(map(str, ev["discs"])), ev["room"], ev["script"], f"{ev['offset']:X}", ev["kind"],
                        ev["item_hex"], ev["name"], ev["source"], "yes" if ev["key_item"] else "",
                        "yes" if ev["debug_room"] else "", ev["text_key"] or "", ev["text"] or ""])
    keys = {}
    for ev in items:
        if ev["key_item"] and not ev["debug_room"]:
            k = keys.setdefault(ev["item_hex"], {"item": ev["item_hex"], "name": ev["name"],
                                                 "give": [], "show": [], "check": [], "remove": []})
            where = f"{ev['room']}#{ev['script']}"
            if where not in k[ev["kind"]]:
                k[ev["kind"]].append(where)
    # Per room: what its boxes and people hand over (a known id), the spots where the player can
    # leave an item to pick up later, and the items it checks for or takes.
    rooms = {}
    for ev in items:
        if ev["debug_room"]:
            continue
        r = rooms.setdefault(ev["room"], {"discs": set(), "gives": [], "leave_spots": set(), "checks": [], "removes": []})
        r["discs"].update(ev["discs"])
        if ev["source"] == "player's choice (item menu)":
            r["leave_spots"].add(ev["script"])
        elif ev["kind"] in ("give", "check", "remove") and ev["item"] is not None:
            r[ev["kind"] + "s"].append(ev["name"])
    with open(out / "room_items.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["room", "discs", "gives", "leave_item_spots", "checks", "removes"])
        for room, r in sorted(rooms.items()):
            w.writerow([room, " ".join(map(str, sorted(r["discs"]))), "; ".join(r["gives"]), len(r["leave_spots"]),
                        "; ".join(dict.fromkeys(r["checks"])), "; ".join(r["removes"])])
    keys = dict(sorted(keys.items()))
    (out / "key_items.json").write_text(json.dumps(list(keys.values()), indent=1, ensure_ascii=False), encoding="utf-8")
    return keys


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("discs", nargs="+")
    ap.add_argument("--out", default=str(ROOT / "out" / "text"))
    ap.add_argument("--src", default=str(ROOT.parent), help="the port checkout, for the opcode table")
    ap.add_argument("--ja", nargs="*", default=[], metavar="JP_DISC",
                    help="the Japanese discs, in order: adds each key's Japanese text (needs data/ja_glyphs.tsv)")
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    valid = opcodes(Path(a.src))

    entries: dict[str, dict] = {}
    banks: dict[str, dict] = {}            # bank hash -> {key, rooms, pages}
    unresolved, unclean, stats = [], [], defaultdict(int)
    items, script_pages = {}, defaultdict(list)     # item events; text pages each script opens
    room_bank, room_boxes = {}, {}         # (disc, map) -> its bank key; the pages its scripts open

    for n, path in enumerate(a.discs, 1):
        g = Game(path)
        if n == 1:
            pack = g.resident_pack()
            for key, raw in table_entries(pack):
                entries[key] = {"key": key, "raw": raw, "end": 0xFF, "rooms": [], "boxes": []}
            at = struct.unpack_from("<i", pack, 0x28)[0]        # ResidentTableDirectory.glyphMetrics
            metrics = pack[at:at + 444]
            # ItemDataRecord[] (0x20 bytes: itemId at 4, kind at 6) up to statGrowth: the item
            # list draws a count after the names of some kinds (Menu_DrawSlotRow).
            begin, end = struct.unpack_from("<2i", pack, 0x0C)
            for rec in range(begin, end - 0x1F, 0x20):
                key = f"item/{pack[rec + 4] - 1:03d}"
                if pack[rec + 4] and key in entries:
                    entries[key]["item_kind"] = pack[rec + 6]
        for m in range(1, 451):
            room = g.room(m)
            if not room:
                continue
            stats["rooms read"] += 1
            payloads = bank_slots(room.data)
            code, clean = walk(task_block(room.data), valid)
            if not clean:
                unclean.append(f"M{m:03d}")
            for i, kind, item, source in item_events(code):
                ev = (f"M{m:03d}", i.script, i.at, kind, item)
                if ev in items:          # the same room script on the other disc
                    items[ev]["discs"].append(n)
                    continue
                items[ev] = {"discs": [n], "room": ev[0], "script": i.script, "offset": i.at,
                             "kind": kind, "item": item, "source": source}
            if 1 not in payloads:
                continue
            en = room.data[payloads[1]:]
            pages, used = gt.pages(en)
            h = hashlib.sha1(en[:used]).hexdigest()
            b = banks.get(h)
            if b is None:
                b = banks[h] = {"key": f"M{m:03d}", "rooms": [], "pages": pages,
                                "crc32": zlib.crc32(en[:used]), "size": used}
            if f"M{m:03d}" not in b["rooms"]:
                b["rooms"].append(f"M{m:03d}")
            calls = box_calls(code)
            room_bank[(n, m)] = b["key"]
            room_boxes[(n, m)] = [(s, p) for s, _, p, _, _, _ in calls]
            # Every box this bank can appear in: only opcodes 13 and 209 open a page of a room's
            # bank (Render_SetupColorTable's other callers read other sources), so a page shown at
            # all is shown in one of these. Rooms whose scripts don't decode to the end may hold more.
            b.setdefault("possible", set()).update(box for *_, box, _, _ in calls)
            b["complete"] = b.get("complete", True) and clean
            written = constant_writes(code)
            for script, at, page, box, mode, reg in calls:
                if page is not None and page in b["pages"]:
                    script_pages[(n, m, script)].append((at, f"dlg/{b['key']}/{page:03d}"))
                if page is None:
                    traced = sorted(p for p in written.get(reg, ()) if p in b["pages"])
                    for p in traced:                 # the constants the room puts in that variable
                        b.setdefault("boxes", defaultdict(set))[p].add(box)
                    stats["computed openings"] += 1
                    stats["computed openings traced"] += bool(traced)
                    if not traced:
                        unresolved.append({"room": f"M{m:03d}", "script": script, "offset": at, "box": box,
                                           "why": "page computed at run time (operand mode %d)" % mode})
                    continue
                if page not in b["pages"]:
                    unresolved.append({"room": f"M{m:03d}", "script": script, "offset": at, "box": box,
                                       "why": f"page {page} not in the room's bank"})
                    continue
                b.setdefault("boxes", defaultdict(set))[page].add(box)

    for b in banks.values():
        for pid, (raw, end) in sorted(b["pages"].items()):
            key = f"dlg/{b['key']}/{pid:03d}"
            entries[key] = {"key": key, "raw": raw, "end": end, "rooms": b["rooms"],
                            "boxes": sorted(b.get("boxes", {}).get(pid, ())),
                            "boxes_possible": sorted(b.get("possible", ())),
                            "boxes_complete": b.get("complete", False)}
    ja_stats, ja_only = add_japanese(entries, a.ja, room_bank, room_boxes, valid) if a.ja else ({}, [])

    # Table slots the game doesn't use: many indices sharing one filler of codes the font can't
    # draw (misc 89..127 repeat one 10-byte string; 143..251 all point past the text, at other
    # data). Repetition alone isn't enough: 34 handguns share the description "Handgun".
    seen = defaultdict(int)
    for e in entries.values():
        if not e["key"].startswith("dlg/"):
            seen[(e["key"].split("/")[0], e["raw"])] += 1
    for e in entries.values():
        filler = re.search(r"\{[gx][0-9A-F]{2}\}", gt.decode(e["raw"]))
        e["unused"] = bool(not e["key"].startswith("dlg/") and seen[(e["key"].split("/")[0], e["raw"])] >= 4 and filler)

    bad = []
    for e in entries.values():
        e["text"] = gt.decode(e["raw"])
        e["lines_px"] = gt.lines(e["raw"])
        if not e["key"].startswith("dlg/"):
            e["menu_px"] = menu.width(e["raw"], metrics)
        if gt.encode(e["text"]) != e["raw"]:
            bad.append(e["key"])
    rows = []
    for e in sorted(entries.values(), key=lambda e: e["key"]):
        rows.append({"key": e["key"], "unused": e["unused"], "text": e["text"], "raw": e["raw"].hex(" "), "end": f"{e['end']:02X}",
                     "lines_px": e["lines_px"], "boxes": e["boxes"],
                     "boxes_possible": e.get("boxes_possible", []), "boxes_complete": e.get("boxes_complete", False),
                     **{k: e[k] for k in ("menu_px", "item_kind") if k in e},
                     "rooms": e["rooms"], "japanese": e.get("ja"), "ja_raw": e.get("ja_raw"), "ja_aligned": e.get("ja_aligned")})
    (out / "strings.json").write_text(json.dumps(rows, indent=1, ensure_ascii=False), encoding="utf-8")
    # The menus' glyph widths (resident glyph metrics, game data): petext/menu.py measures with them.
    (out / "menu_glyphs.json").write_text(json.dumps({"metrics": metrics.hex()}), encoding="utf-8")
    with open(out / "strings.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["key", "english", "japanese", "translation", "lines", "widest_px", "box", "rooms"])
        for r in rows:
            if r["unused"]:
                continue
            w.writerow([r["key"], r["text"], r["japanese"] or "", "", len(r["lines_px"]), max(r["lines_px"]),
                        "; ".join(r["boxes"]), " ".join(r["rooms"][:6]) + (" ..." if len(r["rooms"]) > 6 else "")])
    if a.ja:
        (out / "japanese_only.json").write_text(json.dumps(ja_only, indent=1, ensure_ascii=False), encoding="utf-8")
    items = list(items.values())
    key_summary = write_items(out, items, entries, script_pages)
    unknown = defaultdict(int)
    for e in entries.values():
        if e["unused"]:
            continue
        for tag in __import__("re").findall(r"\{(g[0-9A-F]{2}|x[0-9A-F]{2}|fb [0-9A-F]{2})\}", e["text"]):
            unknown[tag] += 1
    dlg = [e for e in entries.values() if e["key"].startswith("dlg/")]
    report = [
        f"discs: {', '.join(a.discs)}",
        f"rooms read: {stats['rooms read']}; message banks: {len(banks)} (shared by rooms of an area)",
        f"entries: {len(entries)} ({sum(1 for k in entries if not k.startswith('dlg/'))} table, {len(dlg)} dialogue pages); "
        f"{sum(1 for e in entries.values() if e['unused'])} table slots unused (filler), left out of strings.csv",
        f"round-trip (encode(decode(bytes)) == bytes): {len(entries) - len(bad)} of {len(entries)}" + (f"; failed: {bad[:10]}" if bad else ""),
        f"dialogue pages tied to a box by a script: {sum(1 for e in dlg if e['boxes'])} of {len(dlg)}",
        f"  of which by a page the room puts in a variable: {stats['computed openings traced']} of "
        f"{stats['computed openings']} computed openings traced",
        f"the rest, whose bank can only use the default box: "
        f"{sum(1 for e in dlg if not e['boxes'] and e['boxes_complete'] and e['boxes_possible'] == ['default'])}; "
        f"whose bank also opens styled boxes: {sum(1 for e in dlg if not e['boxes'] and e['boxes_complete'] and len(e['boxes_possible']) > 1)}; "
        f"bank has undecoded rooms: {sum(1 for e in dlg if not e['boxes'] and not e['boxes_complete'])}",
        f"box openings not tied to a page: {len(unresolved)}",
        f"rooms whose scripts didn't decode to the end: {len(unclean)} {unclean[:12]}",
        f"item events: {len(items)} ({', '.join(f'{k} {v}' for k, v in sorted(Counter(i['kind'] for i in items).items()))}); "
        "item id from: " + ", ".join(f"{k} {v}" for k, v in Counter(i["source"] for i in items).most_common()) + "; "
        f"{sum(1 for i in items if i['key_item'])} on key items ({len(key_summary)} different)",
        *([f"japanese: {', '.join(a.ja)}",
           f"japanese text on {ja_stats['keys']} keys ({ja_stats['table']} table, {ja_stats['dialogue']} dialogue); "
           f"rooms whose scripts open the same pages as the US ones: {ja_stats['aligned']} of {ja_stats['rooms']} "
           f"(not: {ja_stats['unaligned'][:8]}); Japanese pages with no US page: {len(ja_only)}; "
           f"keys whose rooms disagree on the Japanese: {ja_stats['conflicts']}; "
           "glyphs missing from the table: "
           f"{sum((e.get('ja') or '').count('{?') for e in entries.values() if not e['unused'])}"] if a.ja else []),
        "codes without a name yet (count): " + ", ".join(f"{{{k}}} {v}" for k, v in sorted(unknown.items(), key=lambda x: -x[1])[:20]),
    ]
    (out / "report.txt").write_text("\n".join(report) + "\n", encoding="utf-8")
    (out / "unresolved.json").write_text(json.dumps(unresolved, indent=1), encoding="utf-8")
    # How pe_lang knows a room's bank in memory: the CRC32 of the bytes its pages span.
    (out / "banks.json").write_text(json.dumps(
        [{"key": b["key"], "crc32": f"{b['crc32']:08x}", "size": b["size"], "rooms": b["rooms"]}
         for b in sorted(banks.values(), key=lambda b: b["key"])], indent=1), encoding="utf-8")
    print("\n".join(report))
    print(f"\nwritten to {out}")


if __name__ == "__main__":
    main()
