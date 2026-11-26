"""Parasite Eve (native port) memory card files: read, check, decode the mods' data, and make
test fixtures.

A card file is one 8 KB block (memory-cards/bu00/BASLUS-00662xxxxxxxx):
  0x0000  0x100   card header: product code, icon flags, Shift-JIS title, palette, icon
  0x0100  0x12E4  the game's runtime state (SaveRuntimeState)
  0x13E4  ...     the game's save tail (script variables, Aya's battle record ...)
  0x1C8C  4       the game's CRC-16 (poly 0x1021, init 0xFFFF, inverted) of all 0x2000 bytes,
                  this field read as zero
  0x1C90  0x370   the free tail: the mods' data

The mods' data is either the host's record container (mods/host/pe_save.h, "PEMC") or, in saves
from before it, each mod's own fixed block (PELT, PECT, PESH, PKEY, SORT). The container runs
around the window 0x200..0x287 of the tail, where the PKEY and SORT blocks stay for builds
without the container.
"""
from __future__ import annotations

import struct
import unicodedata
from dataclasses import dataclass, field
from pathlib import Path

BLOCK_SIZE = 0x2000
CRC_AT = 0x1C8C
TAIL_AT = 0x1C90
TAIL_SIZE = BLOCK_SIZE - TAIL_AT          # 880
WINDOW_AT, WINDOW_END = 0x200, 0x288      # tail offsets
WINDOW_SIZE = WINDOW_END - WINDOW_AT
ROOM = TAIL_SIZE - WINDOW_SIZE            # 744
HEADER_SIZE = 20
RECORD_HEADER_SIZE = 12

# Old fixed blocks: name of the record that took each over, tail offset, size, magic.
LEGACY = [
    ("pe_economy.loot", 0x000, 0x020, b"PELT"),
    ("pe_economy.catalog", 0x040, 0x148, b"PECT"),
    ("pe_economy.shop", 0x1C0, 0x014, b"PESH"),
    ("pe_qol.keys", 0x200, 0x054, b"PKEY"),
    ("pe_qol.sort", 0x260, 0x028, b"SORT"),
]
KNOWN_RECORDS = [name for name, *_ in LEGACY]
# Records registered since (pe_save_register in mods/): named in summaries, values not decoded.
LATER_RECORDS = ["pe_challenges.run", "pe_phone.heard", "pe_host.extlink", "pe_stacks.counts"]


def fnv1a(name: str) -> int:
    h = 0x811C9DC5
    for b in name.encode():
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


NAME_OF = {fnv1a(n): n for n in KNOWN_RECORDS + LATER_RECORDS}


def crc32(data: bytes, crc: int = 0xFFFFFFFF, final: bool = True) -> int:
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xEDB88320 if crc & 1 else crc >> 1
    return crc ^ 0xFFFFFFFF if final else crc


def game_crc(block: bytes) -> int:
    crc = 0xFFFF
    for i, b in enumerate(block):
        if CRC_AT <= i < CRC_AT + 4:
            b = 0
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return ~crc & 0xFFFF


def gather(tail: bytes) -> bytes:
    return tail[:WINDOW_AT] + tail[WINDOW_END:]


def scatter(lin: bytes, window: bytes) -> bytes:
    lin = lin.ljust(ROOM, b"\0")
    return lin[:WINDOW_AT] + window + lin[WINDOW_AT:]


def record_crc(header8: bytes, data: bytes) -> int:
    return crc32(header8 + data)


@dataclass
class Record:
    id: int
    version: int
    data: bytes
    ok: bool = True               # its own CRC matched
    source: str = "container"     # container | legacy | window

    @property
    def name(self) -> str:
        return NAME_OF.get(self.id, f"#{self.id:08X}")


@dataclass
class ModData:
    kind: str                     # empty | container | legacy | foreign | damaged
    records: list[Record] = field(default_factory=list)
    container_ok: bool = False
    window_changed: bool = False
    api: int = 0
    writer: int = 0
    total: int = 0
    format: int = 0
    notes: list[str] = field(default_factory=list)

    def get(self, name: str) -> Record | None:
        for r in self.records:
            if r.name == name:
                return r
        return None


def parse_tail(tail: bytes) -> ModData:
    """The mods' data in a free tail, as this host would read it."""
    if tail[:4] == b"PEMC":
        fmt, flags, total = tail[4], tail[5], struct.unpack_from("<H", tail, 6)[0]
        md = ModData("container", format=fmt, total=total)
        if fmt > 1 or (fmt == 1 and flags):
            md.kind = "foreign"
            md.notes.append(f"format {fmt} flags {flags}: newer than this layout, kept as it is")
            return md
        if fmt != 1 or total < HEADER_SIZE or total > ROOM:
            md.kind = "damaged"
            md.notes.append(f"bad header (format {fmt}, total {total})")
            return md
        lin = bytearray(gather(tail))
        want = struct.unpack_from("<I", lin, 8)[0]
        lin[8:12] = b"\0\0\0\0"
        md.container_ok = crc32(bytes(lin[:total])) == want
        lin[8:12] = struct.pack("<I", want)
        md.api, md.writer = struct.unpack_from("<HH", lin, 16)
        at = HEADER_SIZE
        while at < total:
            if total - at < RECORD_HEADER_SIZE:
                md.notes.append(f"truncated record header at {at}")
                break
            rid, ver, size, rcrc = struct.unpack_from("<IHHI", lin, at)
            if size > total - at - RECORD_HEADER_SIZE:
                md.notes.append(f"record #{rid:08X} runs past the container")
                break
            data = bytes(lin[at + RECORD_HEADER_SIZE: at + RECORD_HEADER_SIZE + size])
            ok = record_crc(bytes(lin[at:at + 8]), data) == rcrc
            md.records.append(Record(rid, ver, data, ok))
            at += RECORD_HEADER_SIZE + size
        if not md.container_ok:
            md.notes.append("container CRC fails: records load only where their own CRC matches")
        window_crc = struct.unpack_from("<I", lin, 12)[0]
        md.window_changed = crc32(tail[WINDOW_AT:WINDOW_END]) != window_crc
        if md.window_changed:
            for name, at, size, magic in LEGACY:
                if WINDOW_AT <= at < WINDOW_END and tail[at:at + 4] == magic:
                    md.records = [r for r in md.records if r.name != name]
                    md.records.append(Record(fnv1a(name), 0, tail[at:at + size], True, "window"))
            md.notes.append("window changed since written: another build saved here; its blocks win")
        return md
    md = ModData("legacy")
    for name, at, size, magic in LEGACY:
        if tail[at:at + 4] == magic:
            md.records.append(Record(fnv1a(name), 0, tail[at:at + size], True, "legacy"))
    if not md.records:
        md.kind = "empty" if not any(tail) else "unknown"
    return md


def window_blocks(tail: bytes) -> dict[str, bytes]:
    out = {}
    for name, at, size, magic in LEGACY:
        if WINDOW_AT <= at < WINDOW_END and tail[at:at + 4] == magic:
            out[name] = tail[at:at + size]
    return out


# ---- payload decoders: what a person wants to see of each record ----

def _legacy_payload(data: bytes, header: int = 16) -> bytes:
    return data[header:]


def decode(rec: Record) -> dict:
    d = rec.data
    try:
        if rec.name == "pe_economy.loot":
            if rec.version == 0:      # PELT v1..4
                magic, ver, junk = struct.unpack_from("<4sII", d, 0)
                out = {"layout": f"PELT v{ver}", "junk": junk}
                if ver >= 2:
                    packed = struct.unpack_from("<I", d, 12)[0]
                    out.update(rare_dry=packed & 0xFFFF, rare_carry=packed >> 16)
                if ver >= 3:
                    extra = struct.unpack_from("<I", d, 16)[0]
                    out.update(charm_left=extra & 0xFFFF, preset=(extra >> 16) & 0xFF, disabled=(extra >> 24) & 1)
                return out
            junk, = struct.unpack_from("<I", d, 0)
            rare_dry, rare_carry = struct.unpack_from("<HH", d, 20)
            charm, preset, disabled, econ_since, permit_since = struct.unpack_from("<HBBBB", d, 28)
            return {"junk": junk, "rare_dry": rare_dry, "rare_carry": rare_carry, "charm_left": charm,
                    "preset": preset, "disabled": disabled, "econ_since": econ_since, "permit_since": permit_since}
        if rec.name == "pe_economy.catalog":
            s = _legacy_payload(d) if rec.version == 0 else d
            gun, armor, count = struct.unpack_from("<IIB", s, 0)
            return {"gun_effects": bin(gun).count("1"), "armor_effects": bin(armor).count("1"),
                    "gun_bits": f"{gun:08X}", "armor_bits": f"{armor:08X}", "rebuild_count": count}
        if rec.name == "pe_economy.shop":
            s = _legacy_payload(d) if rec.version == 0 else d
            return {"tools_bought": struct.unpack_from("<H", s, 0)[0]}
        if rec.name == "pe_qol.keys":
            s = _legacy_payload(d) if rec.version == 0 else d
            count, = struct.unpack_from("<H", s, 0)
            ids = list(struct.unpack_from(f"<{min(count, 32)}H", s, 4)) if count else []
            return {"count": count, "keys": [f"0x{i:X}" for i in ids]}
        if rec.name == "pe_qol.sort":
            s = _legacy_payload(d) if rec.version == 0 else d
            mode, rank, typ, wstat, astat, hidden = struct.unpack_from("<6I", s, 0)
            return {"mode": ["Manual", "Auto"][mode] if mode < 2 else mode, "rank_table": f"{rank:08X}",
                    "type_table": f"{typ:08X}", "weapon_stat": wstat, "armor_stat": astat}
    except struct.error:
        return {"error": "too short"}
    return {"bytes": len(d)}


# ---- a card file ----

@dataclass
class Card:
    path: Path | None
    block: bytearray

    @classmethod
    def load(cls, path) -> "Card":
        data = Path(path).read_bytes()
        if len(data) != BLOCK_SIZE:
            raise ValueError(f"{path}: {len(data)} bytes, not a card block")
        return cls(Path(path), bytearray(data))

    @property
    def title(self) -> str:
        raw = bytes(self.block[4:0x44]).split(b"\0", 1)[0]
        text = unicodedata.normalize("NFKC", raw.decode("shift_jis", "replace"))
        return text.replace("−", "-").replace("　", " ").strip()

    @property
    def stored_crc(self) -> int:
        return struct.unpack_from("<I", self.block, CRC_AT)[0]

    @property
    def crc_ok(self) -> bool:
        return self.stored_crc == game_crc(self.block)

    @property
    def tail(self) -> bytes:
        return bytes(self.block[TAIL_AT:])

    def set_tail(self, tail: bytes) -> None:
        assert len(tail) == TAIL_SIZE
        self.block[TAIL_AT:] = tail
        self.fix_crc()

    def fix_crc(self) -> None:
        struct.pack_into("<I", self.block, CRC_AT, game_crc(self.block))

    def mods(self) -> ModData:
        return parse_tail(self.tail)

    def save(self, path) -> None:
        Path(path).write_bytes(bytes(self.block))

    def summary(self) -> dict:
        md = self.mods()
        return {
            "file": self.path.name if self.path else None,
            "title": self.title,
            "game_crc": "ok" if self.crc_ok else "BAD",
            "mods": md.kind,
            "writer": f"mod API {md.api} / save writer {md.writer}" if md.kind == "container" else None,
            "container_crc": ("ok" if md.container_ok else "BAD") if md.kind == "container" else None,
            "window_changed": md.window_changed if md.kind == "container" else None,
            "records": {r.name: {"v": r.version, "from": r.source, "crc": "ok" if r.ok else "BAD", **decode(r)}
                        for r in md.records},
            "window": sorted(window_blocks(self.tail)),
            "notes": md.notes,
        }


# ---- fixtures: build tails the way other builds would have ----

def build_container(records: list[tuple[str | int, int, bytes]], window: bytes = b"\0" * WINDOW_SIZE,
                    api: int = 1, writer: int = 1) -> bytes:
    """A container tail as the host writes it (records sorted by id)."""
    items = sorted(((fnv1a(n) if isinstance(n, str) else n, v, d) for n, v, d in records))
    body = bytearray()
    for rid, ver, data in items:
        h8 = struct.pack("<IHH", rid, ver, len(data))
        body += h8 + struct.pack("<I", record_crc(h8, data)) + data
    total = HEADER_SIZE + len(body)
    if total > ROOM:
        raise ValueError("records don't fit")
    head = bytearray(b"PEMC" + bytes([1, 0]) + struct.pack("<H", total) + b"\0" * 4 +
                     struct.pack("<I", crc32(window)) + struct.pack("<HH", api, writer))
    lin = bytes(head) + bytes(body)
    lin = lin[:8] + struct.pack("<I", crc32(lin)) + lin[12:]
    return scatter(lin, window)


def legacy_block(magic: bytes, payload: bytes, seed: int) -> bytes:
    """pe_qol's PKEY / SORT block: magic, version 1, size, checksum(seed), payload."""
    s = seed
    for b in payload:
        s = (s * 33 + b) & 0xFFFFFFFF
    return magic + struct.pack("<III", 1, len(payload), s) + payload


def pkey_block(keys: list[int]) -> bytes:
    ids = (list(keys) + [0] * 32)[:32]
    return legacy_block(b"PKEY", struct.pack("<HH32H", len(keys), 0, *ids), 0x6B657973)


def write_window_block(tail: bytes, name: str, block: bytes) -> bytes:
    for n, at, size, _ in LEGACY:
        if n == name:
            t = bytearray(tail)
            t[at:at + size] = block.ljust(size, b"\0")[:size]
            return bytes(t)
    raise KeyError(name)


# ---- the companion file (.pe-ext/<card>.<generation>.pex, mods/host/pe_extension_store.c) ----

COMPANION_HEADER, COMPANION_ENTRY = 96, 64


def parse_companion(data: bytes) -> dict[str, tuple[int, bytes]]:
    """A save's companion file: {section key: (version, bytes)}. ValueError when it isn't one."""
    if len(data) < COMPANION_HEADER or data[:4] != b"PEEX" or struct.unpack_from("<H", data, 4)[0] != 1:
        raise ValueError("not a PEEX companion file")
    count = struct.unpack_from("<H", data, 16)[0]
    out = {}
    for i in range(count):
        p = COMPANION_HEADER + i * COMPANION_ENTRY
        key = data[p:p + 48].split(b"\0", 1)[0].decode()
        version = struct.unpack_from("<H", data, p + 48)[0]
        offset, size = struct.unpack_from("<II", data, p + 52)
        out[key] = (version, data[offset:offset + size])
    return out
