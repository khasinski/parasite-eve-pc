"""Scan Parasite Eve room scripts for key item give (op 167) and check (op 168) steps.
Usage: python -I keyscan.py <disc.bin> [...]   (raw 2352-byte sectors, USA discs only)"""
import os, struct, sys

GIVE, COUNT = 167, 168
KEYS = {
    0xC8: "Dressing Room Key", 0xC9: "Rehearsal Room Key", 0xCA: "Central Park Key",
    0xCB: "Good Luck Charm", 0xCC: "Helpful Good Luck Charm", 0xCD: "Special Good Luck Charm",
    0xCE: "Fuse 1", 0xCF: "Fuse 2", 0xD0: "Fuse 3", 0xD1: "St. Francis Hospital Key",
    0xD2: "Blue Magnetic Cardkey", 0xD3: "Green Magnetic Cardkey", 0xD4: "Pumping Station Key",
    0xD5: "Klamp Key", 0xD6: "NYPD Weapons Storage Key", 0xD7: "NYPD Locker Key",
    0xD8: "Weapon Memo", 0xD9: "Shiny Medal", 0xDA: "Elevator Key", 0xDB: "Subway Gate Key",
    0xDC: "Port Storage Key",
}
# The room table address is only right for the USA executables (disc 1 / disc 2).
USA_EXES = ("SLUS_006.62;1", "SLUS_006.68;1")
ROOM_TABLE, ROOMS = 0x80093378, 450


class Disc:
    def __init__(self, path):
        if os.path.getsize(path) % 2352:
            raise ValueError(f"{path}: not a raw 2352-byte-sector image (pass the .bin, not the .cue or an .iso)")
        self.f = open(path, "rb")
        if self.sector(16)[:6] != b"\x01CD001":
            raise ValueError(f"{path}: no ISO 9660 volume descriptor (not a MODE2/2352 data track?)")

    def sector(self, lba):
        self.f.seek(lba * 2352 + 24)
        data = self.f.read(2048)
        if len(data) != 2048:
            raise ValueError(f"sector {lba} is past the end of the image (truncated dump?)")
        return data

    def read(self, lba, size):
        out = bytearray()
        while len(out) < size:
            out += self.sector(lba)
            lba += 1
        return bytes(out[:size])

    def root(self):
        pvd = self.sector(16)
        rec = pvd[156:190]
        lba, size = struct.unpack_from("<I", rec, 2)[0], struct.unpack_from("<I", rec, 10)[0]
        data, files, i = self.read(lba, size), {}, 0
        while i < len(data):
            n = data[i]
            if n == 0:
                i = (i // 2048 + 1) * 2048
                continue
            flba, fsize = struct.unpack_from("<I", data, i + 2)[0], struct.unpack_from("<I", data, i + 10)[0]
            name = data[i + 33:i + 33 + data[i + 32]].decode("ascii", "replace")
            files[name] = (flba, fsize)
            i += n
        return files


def layout(d):
    """(exe name, PE.IMG first sector, room table): checked, so a wrong disc fails loudly instead of
    scanning garbage. Table entry m-1 is room M<m>: start sector, counts (scratch | tex << 8 | room << 20)."""
    files = d.root()
    exe = next((n for n in files if n in USA_EXES), None)
    if exe is None:
        found = [n for n in files if n[:4] in ("SLUS", "SCUS", "SLPS", "SLES", "SCES")]
        raise ValueError(f"not a Parasite Eve USA disc (executable {found or 'missing'}, expected one of {USA_EXES})")
    if "PE.IMG;1" not in files:
        raise ValueError("PE.IMG is missing from the disc")
    exe_lba, _ = files[exe]
    img_lba, img_size = files["PE.IMG;1"]
    hdr = d.read(exe_lba, 0x800)
    if hdr[:8] != b"PS-X EXE":
        raise ValueError(f"{exe} is not a PS-X EXE")
    o = 0x800 + ROOM_TABLE - struct.unpack_from("<I", hdr, 0x18)[0]
    table = d.read(exe_lba, o + ROOMS * 8)[o:]
    img_sectors = -(-img_size // 2048)
    for m in range(ROOMS):
        start, c = struct.unpack_from("<II", table, m * 8)
        if start + (c & 255) + ((c >> 8) & 4095) + (c >> 20) > img_sectors:
            raise ValueError(f"room table entry M{m + 1:03d} points past PE.IMG (modified or unknown executable?)")
    return exe, img_lba, table


def scan(path):
    d = Disc(path)
    exe_name, img_lba, table = layout(d)
    hits = []
    for m in range(ROOMS):
        start, counts = struct.unpack_from("<II", table, m * 8)
        scratch, tex, room = counts & 255, (counts >> 8) & 4095, counts >> 20
        if not room:
            continue
        for part, (off, n) in (("scratch", (0, scratch)), ("room", (scratch + tex, room))):
            if not n:
                continue
            blob = d.read(img_lba + start + off, n * 2048)
            for i in range(0, len(blob) - 16, 4):
                h = struct.unpack_from("<I", blob, i)[0]
                op, argc = h & 0x1FFF, (h >> 13) & 15
                if op not in (GIVE, COUNT) or argc != 2:
                    continue
                mode0 = (h >> 17) & 7
                val0, val1 = struct.unpack_from("<II", blob, i + 8)
                if mode0 == 0 and val0 in KEYS:
                    hits.append((m + 1, part, i, "GIVE" if op == GIVE else "CHECK", val0))
    return exe_name, hits


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for p in sys.argv[1:]:
        exe, hits = scan(p)
        print(f"== {p} ({exe}): {len(hits)} hits")
        for m, part, off, kind, item in hits:
            print(f"M{m:03d}\t{part}+{off:06X}\t{kind}\t{item:#04x}\t{KEYS[item]}")
