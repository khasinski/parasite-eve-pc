"""A fan patch made for the Japanese discs (SLPS-01230 / 01231), read for the USA-disc port.

A patch for the Japanese release keeps the Japanese text engine: it writes its own pages over the
Japanese ones and draws its own letters into the Japanese font cells (page 0, and the page 1 each
area loads, see petext/font.py). So a cell's code says nothing about the letter: only its picture
does. This reads the Japanese disc and the patched one side by side and writes:

  draft     each page and table entry the patch changes, on the US key the Japanese one lines up
            with (the same message bank and page number; extract_text.py --ja), as tagged text
            whose letters are pictures: {pic ID}, ID the first 12 hex digits of the SHA-1 of the
            cell's palette indices, the same id data/ja_glyphs.tsv uses. A page of a bank that
            rooms of several areas share, drawn differently by their fonts, also has every
            area's reading ("areas": {room type: text}); "draft" is the first one's
  pictures  every picture the draft uses: its 12 rows (' ' empty, '#' stroke, '+' shadow), how
            often it is drawn, where, and the Japanese character when the patch left a Japanese
            letter as it was
  report    what else the patch changed (font pages, room scripts, pictures, the boot program),
            the rooms whose scripts don't open the same pages as the US ones, rooms whose copy of
            a shared message bank the patch left different from the first room's (the draft reads
            the first room's), and what was left out

Naming the pictures (which character each one is) is the next step (petext/hanzi.py for a
Chinese patch); this step only reads.
No patch, disc data or patched text goes into the repository.
"""
from __future__ import annotations

import csv
import hashlib
import json
from pathlib import Path

from . import font, gametext as gt, jatext as ja, patchimport

CATALOG = Path(__file__).resolve().parent.parent / "editor" / "catalog.json"
DISC_NUMBER = {"SLPS_012.30": "1", "SLPS_012.31": "2"}
US_FOR = {"1": "SLUS_006.62", "2": "SLUS_006.68"}
FONT_ENTRIES = [font.PAGE0_ENTRY, *font.PAGE1_ENTRIES]
JAPANESE_BANK = 0          # task slot 0 holds the Japanese message bank (1 is the English one)


def picture_id(cell) -> str:
    return hashlib.sha1(bytes(cell)).hexdigest()[:12]


def picture_rows(pal, cell) -> list[str]:
    layered = patchimport._layered(pal, cell)
    return ["".join(" #+"[v] for v in layered[y * font.CELL:(y + 1) * font.CELL]) for y in range(font.CELL)]


class Fonts:
    """A game's font pages, read once each."""

    def __init__(self, game):
        self.game, self.pages = game, {}

    def page(self, entry: int):
        if entry not in self.pages:
            self.pages[entry] = font.page(self.game, entry)
        return self.pages[entry]

    def cell(self, slot: str, cell: int):
        """(palette, cell pixels) of slot 'p0' or 'eNN' (PE.IMG entry NN), or None past its end."""
        pal, width, height, px = self.page(font.PAGE0_ENTRY if slot == "p0" else int(slot[1:]))
        if (cell // font.PER_ROW + 1) * font.CELL > height:
            return None
        return pal, font.cell(px, width, cell)


def japanese_letters() -> dict[str, str]:
    """picture id -> character, for the Japanese font's own pictures (data/ja_glyphs.tsv)."""
    with open(ja.TABLE, encoding="utf-8") as f:
        return {r["glyph"]: r["fixed"] or r["char"] for r in csv.DictReader(f, delimiter="\t")}


def area_slot(rtype: int | None) -> str | None:
    return None if rtype is None else f"e{rtype + 0x2B}"


def page_text(raw: bytes, fonts: Fonts, rtype: int | None, pictures: dict | None = None,
              count: bool = True) -> str:
    """A page as tagged text with each letter as {pic ID}. A page-1 letter needs the area's font
    (rtype); without one, or past the font's end, it is {?slot:cell}. With `pictures`, each
    picture drawn is added there (id -> palette, cell, slots, and uses unless `count` is off)."""
    parts = []
    for kind, a, b in ja.tokens(raw):
        if kind == "tag":
            parts.append(a)
            continue
        pg, cell = font.glyph_cell(a, b)
        slot = "p0" if pg == 0 else area_slot(rtype)
        found = fonts.cell(slot, cell) if slot else None
        if found is None:
            parts.append(f"{{?{slot or 'e?'}:{cell}}}")
            continue
        pid = picture_id(found[1])
        if pictures is not None:
            pictures.setdefault(pid, {"pal": found[0], "cell": found[1], "slots": set(), "uses": 0})
            pictures[pid]["slots"].add(f"{slot}:{cell}")
            pictures[pid]["uses"] += count
        parts.append(f"{{pic {pid}}}")
    return "".join(parts)


def japanese_bank(room) -> tuple[dict, bytes]:
    """(page id -> (page, end), the bank's bytes) of a room's Japanese message bank."""
    from extract_text import bank_slots
    slots = bank_slots(room.data)
    if JAPANESE_BANK not in slots:
        return {}, b""
    bank = room.data[slots[JAPANESE_BANK]:]
    pages, used = gt.pages(bank)
    return pages, bank[:used]


def whole_bank(room) -> dict:
    """Every page of a room's Japanese bank, also those after a stray byte (patchimport.patched_pages)."""
    from extract_text import bank_slots
    slots = bank_slots(room.data)
    return patchimport.patched_pages(room.data[slots[JAPANESE_BANK]:]) if JAPANESE_BANK in slots else {}


def opened_pages(room, valid) -> list[int | None]:
    """The pages a room's scripts open, in order (None where the page is worked out as it plays)."""
    from extract_text import box_calls
    from petext.script import task_block, walk
    code, _ = walk(task_block(room.data), valid)
    return [page for _, _, page, _, _, _ in box_calls(code)]


def bank_rooms(game, banks: list[dict], disc: str, valid) -> dict[str, dict]:
    """Each US bank key -> the Japanese rooms on this disc holding the same Japanese bank as the
    room the US bank is first found in (read on the unpatched disc): {"home": room number,
    "rooms": {number: room type}, "opens": {page: {room types}}}."""
    by_bank: dict[bytes, list] = {}
    for number in range(1, 451):
        room = game.room(number)
        if room is None:
            continue
        pages, raw = japanese_bank(room)
        if pages:
            by_bank.setdefault(hashlib.sha1(raw).digest(), []).append(room)
    out = {}
    for bank in banks:
        where = bank["locations"].get(disc)
        home = game.room(where) if where else None
        if home is None:
            continue
        rooms = by_bank.get(hashlib.sha1(japanese_bank(home)[1]).digest(), [home])
        opens: dict[int, set] = {}
        for room in rooms:
            for page in opened_pages(room, valid):
                if page is not None:
                    opens.setdefault(page, set()).add(ja.room_type(room.data))
        out[bank["key"]] = {"home": where, "rooms": {r.map: ja.room_type(r.data) for r in rooms}, "opens": opens}
    return out


def not_lined_up(japanese, us, valid) -> list[str]:
    """Rooms whose Japanese scripts don't open the same pages in the same order as the US ones."""
    out = []
    for number in range(1, 451):
        a, b = japanese.room(number), us.room(number)
        if a is None or b is None or not japanese_bank(a)[0]:
            continue
        if opened_pages(a, valid) != opened_pages(b, valid):
            out.append(f"M{number:03d}")
    return out


def font_changes(original, patched) -> list[str]:
    """The font pages the patch redraws: 'p0' or 'eNN' (PE.IMG entry)."""
    out = []
    for entry in FONT_ENTRIES:
        s = original.sectors
        if original.pe(s[entry], s[entry + 1] - s[entry]) != patched.pe(patched.sectors[entry], patched.sectors[entry + 1] - patched.sectors[entry]):
            out.append("p0" if entry == font.PAGE0_ENTRY else f"e{entry}")
    return out


def page_areas(key: str, info: dict, page: int) -> list[int]:
    """The room types whose font a bank page is drawn with: those of the rooms that open it, or
    every room of the bank when none opens it by number."""
    return sorted(info["opens"].get(page) or set(info["rooms"].values()))


def read(original, patched, us, valid) -> tuple[list[dict], dict, dict]:
    """(draft rows, pictures, report) for one Japanese disc and its patched copy; `us` is the USA
    disc of the same number, for the line-up check (None skips it)."""
    from extract_text import bank_slots, table_entries
    disc = DISC_NUMBER.get(original.exe_name)
    if disc is None or patched.exe_name != original.exe_name:
        raise ValueError("Choose an unmodified Japanese disc and its own patch")
    if us is not None and us.exe_name != US_FOR[disc]:
        raise ValueError(f"The USA disc must be disc {disc} too")
    banks = json.loads(CATALOG.read_text(encoding="utf-8"))["banks"]
    manifest = json.loads((CATALOG.parent.parent / "data/manifest.json").read_text(encoding="utf-8"))["entries"]
    fonts_o, fonts_p = Fonts(original), Fonts(patched)
    rows, left, pictures, areas_differ = [], [], {}, []

    def add(key, raw_o, raw_p, rtypes, end=None):
        texts_p = {r: page_text(raw_p, fonts_p, r) for r in rtypes}
        texts_o = {r: page_text(raw_o, fonts_o, r) for r in rtypes}
        if texts_p == texts_o:
            return
        if key not in manifest:
            left.append({"key": key, "reason": "no US key (text the USA release doesn't have)"})
            return
        if len(set(texts_p.values())) > 1:
            areas_differ.append(key)
        rtype = rtypes[0]
        text = page_text(raw_p, fonts_p, rtype, pictures)
        if "{?" in text:
            left.append({"key": key, "reason": "a letter past the end of its font page"})
            return
        row = {"key": key, "draft": text, "area": rtype}
        if end is not None:
            row["end"] = f"{end:02X}"
        if len(set(texts_p.values())) > 1:
            # Every area's reading: which one is the patch's text shows once the letters have
            # names (petext/hanzi.py choose_area); the pictures of the others are listed too.
            row["areas"] = {str(r): page_text(raw_p, fonts_p, r, pictures, count=False)
                            for r in rtypes if "{?" not in texts_p[r]}
        rows.append(row)

    rooms = bank_rooms(original, banks, disc, valid)
    copies_differ = []
    for bank in banks:
        info = rooms.get(bank["key"])
        if info is None:
            continue
        before, after = (whole_bank(game.room(info["home"])) for game in (original, patched))
        home_copy = japanese_bank(patched.room(info["home"]))[1]
        copies_differ += [f"M{n:03d}" for n in sorted(info["rooms"])
                          if n != info["home"] and japanese_bank(patched.room(n))[1] != home_copy]
        prefix = f"dlg/{bank['key']}/"
        left += [{"key": f"{prefix}{p:03}", "reason": "page only in the patch"} for p in sorted(set(after) - set(before))]
        for page, (raw, end) in sorted(before.items()):
            key = f"{prefix}{page:03}"
            if page not in after:
                left.append({"key": key, "reason": "page missing from the patch"})
                continue
            add(key, raw, after[page][0], page_areas(key, info, page) or [None], after[page][1])
    if disc == "1":
        before, after = dict(table_entries(original.resident_pack())), dict(table_entries(patched.resident_pack()))
        for key in sorted(before):
            if key in patchimport.RENAME_GRIDS or key not in after:
                continue
            add(key, before[key], after[key], [None])

    known = japanese_letters()
    out_pictures = {pid: {"rows": picture_rows(p["pal"], p["cell"]), "uses": p["uses"], "slots": sorted(p["slots"]),
                          **({"japanese": known[pid]} if pid in known else {})}
                    for pid, p in sorted(pictures.items(), key=lambda kv: -kv[1]["uses"])}
    report = {
        "disc": disc,
        "draft_rows": len(rows),
        "dialogue_rows": sum(r["key"].startswith("dlg/") for r in rows),
        "table_rows": sum(not r["key"].startswith("dlg/") for r in rows),
        "kept_english": left,
        "pictures": len(out_pictures),
        "pictures_still_japanese": sum("japanese" in p for p in out_pictures.values()),
        "font_pages_redrawn": font_changes(original, patched),
        "pages_drawn_differently_by_area": areas_differ,
        "rooms_whose_bank_copy_differs": copies_differ,
        "rooms_not_lined_up_with_usa": not_lined_up(original, us, valid) if us is not None else "not checked",
        "rooms_with_edited_scripts": patchimport.script_changes(original, patched, lambda d: min(bank_slots(d).values(), default=len(d))),
        "rooms_with_edited_pictures": patchimport.picture_changes(original, patched),
        "boot_program_bytes_changed": patchimport.exe_changes(original, patched),
        "not_read_yet": "battle lines in the boot program (step 3)",
    }
    return rows, out_pictures, report
