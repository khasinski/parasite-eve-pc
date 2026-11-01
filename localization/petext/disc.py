"""Read-only access to a Parasite Eve (USA) disc image: sectors, ISO 9660 root files, PE.IMG,
the room table in the boot executable, the resident pack, and each room's three data runs.

Follows the native port's own loader (src/main/asset/game_state.c, mods/host/pe_disc_assets.c,
src/main/scene/room_load.c). Nothing from the disc is written anywhere by this module.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path

SECTOR = 2048
RAW_SECTOR = 2352
SYNC = b"\x00" + b"\xff" * 10 + b"\x00"
ROOM_COUNT = 450
ROOM_TABLE_ADDR = 0x80093378     # SceneRoomSectorRange[450], 8 bytes each (game_state.c)
SECTOR_TABLE_ADDR = 0x800930D8   # g_PeImageSectorTable: u16[81], PE.IMG entry starts
RESIDENT_ENTRY = 2               # the resident data pack is PE.IMG entry 2
# Boot executable -> (region, room table, sector table). The Japanese tables were found by shape
# (81 rising sector numbers; the room table 0x2A0 after them, as in the US build).
EXECUTABLES = {
    "SLUS_006.62": ("USA", ROOM_TABLE_ADDR, SECTOR_TABLE_ADDR),
    "SLUS_006.68": ("USA", ROOM_TABLE_ADDR, SECTOR_TABLE_ADDR),
    "SLPS_012.30": ("Japan", 0x80092E40, 0x80092BA0),
    "SLPS_012.31": ("Japan", 0x80092E40, 0x80092BA0),
}


def _cue_bin(cue: Path) -> Path:
    for line in cue.read_text(errors="replace").splitlines():
        line = line.strip()
        if line.upper().startswith("FILE"):
            name = line.split('"')[1] if '"' in line else line.split()[1]
            return cue.parent / name
    raise ValueError(f"{cue}: no FILE line")


class Disc:
    def __init__(self, path):
        path = Path(path)
        self.path = path
        image = _cue_bin(path) if path.suffix.lower() == ".cue" else path
        self.f = open(image, "rb")
        self.f.seek(16 * RAW_SECTOR)
        head = self.f.read(16)
        if head[:12] == SYNC:
            self.sector_size, self.data_offset = RAW_SECTOR, 16 if head[15] == 1 else 24
        else:
            self.sector_size, self.data_offset = SECTOR, 0
        self._files = None

    def sector(self, lba: int) -> bytes:
        self.f.seek(lba * self.sector_size + self.data_offset)
        data = self.f.read(SECTOR)
        if len(data) != SECTOR:
            raise EOFError(f"no sector {lba}")
        return data

    def read(self, lba: int, size: int) -> bytes:
        return b"".join(self.sector(lba + i) for i in range((size + SECTOR - 1) // SECTOR))[:size]

    def root_files(self) -> dict[str, tuple[int, int]]:
        """Name -> (first sector, size) for the ISO 9660 root directory."""
        if self._files is None:
            pvd = self.sector(16)
            if pvd[1:6] != b"CD001":
                raise ValueError("not a CD-ROM image")
            lba, size = struct.unpack_from("<I", pvd, 158)[0], struct.unpack_from("<I", pvd, 166)[0]
            files, raw = {}, self.read(lba, size)
            for s in range(0, len(raw), SECTOR):
                sec, at = raw[s:s + SECTOR], 0
                while at < SECTOR and sec[at]:
                    n = sec[at]
                    name = sec[at + 33:at + 33 + sec[at + 32]].split(b";")[0].decode("ascii", "replace")
                    files[name.upper()] = struct.unpack_from("<I", sec, at + 2)[0], struct.unpack_from("<I", sec, at + 10)[0]
                    at += n
            self._files = files
        return self._files

    def file(self, name: str) -> bytes:
        lba, size = self.root_files()[name.upper()]
        return self.read(lba, size)


@dataclass
class Room:
    map: int                 # M001 is 1
    scratch: bytes
    texture: bytes
    data: bytes              # models, rows, scripts, messages (a SceneAssetBlob)


class Game:
    """The tables the port reads at mount, and rooms and the resident pack from PE.IMG."""

    def __init__(self, disc_path):
        self.disc = Disc(disc_path)
        files = self.disc.root_files()
        self.pe_img = files["PE.IMG"]
        config = self.disc.file("SYSTEM.CNF").decode("ascii", "replace")
        boot = config.split("cdrom:", 1)[1] if "cdrom:" in config else config.split("CDROM:", 1)[1]
        exe_name = boot.split(";")[0].strip().lstrip("\\").split("\\")[-1].upper()
        self.exe_name = exe_name
        exe = self.disc.file(exe_name)
        if exe[:8] != b"PS-X EXE":
            raise ValueError("the boot file is not a PS-X EXE")
        if exe_name not in EXECUTABLES:
            raise ValueError(f"unknown boot executable {exe_name}")
        self.region, room_table, sector_table = EXECUTABLES[exe_name]
        base = struct.unpack_from("<I", exe, 24)[0]
        at = lambda addr: 0x800 + addr - base
        self.rooms = []
        for i in range(ROOM_COUNT):
            start, counts = struct.unpack_from("<II", exe, at(room_table) + i * 8)
            self.rooms.append((start, counts & 0xFF, (counts >> 8) & 0xFFF, counts >> 20))
        self.sectors = list(struct.unpack_from("<81H", exe, at(sector_table)))

    def pe(self, sector: int, count: int) -> bytes:
        return self.disc.read(self.pe_img[0] + sector, count * SECTOR)

    def resident_pack(self) -> bytes:
        s = self.sectors
        return self.pe(s[RESIDENT_ENTRY], s[RESIDENT_ENTRY + 1] - s[RESIDENT_ENTRY])

    def room(self, map_no: int) -> Room | None:
        start, scratch, texture, data = self.rooms[map_no - 1]
        if not data:
            return None
        return Room(map_no, self.pe(start, scratch), self.pe(start + scratch, texture),
                    self.pe(start + scratch + texture, data))
