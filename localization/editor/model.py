"""Dialogue catalog and editor project backed by the pe_lang compiler."""
import hashlib
import io
import json
import re
import struct
import tempfile
import zipfile
import zlib
from dataclasses import dataclass, field
from pathlib import Path
from functools import lru_cache
from types import SimpleNamespace

import pelang
from extract_text import bank_slots
from petext import cjk, gametext, layout, manifest, validation
from petext.disc import Game

LEGACY_SCENE = "M001-opera-arrival"
SCENE = "pe-usa-dialogue"
BANK_CRC = "27e69288"
KEYS = tuple(f"dlg/M001/{n:03d}" for n in (9, 10, 13, 14, 15, 16, 17, 18))
BOX = "{box}"
DEMO_FONT = Path(__file__).with_name("fonts") / "OperaPilotCJK-Regular.otf"
LIMIT = 32 * 1024 * 1024
SAMPLE = {KEYS[0]: "男：你怎麼了？{box}是你想來看歌劇的。", KEYS[3]: "男：走吧，該進去了。"}


def digest(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def read_original(path):
    game = Game(path)
    try:
        serials = game.disc.root_files()
        disc = next((number for number, serial in (("1", "SLUS_006.62"), ("2", "SLUS_006.68"))
                     if serial in serials), None)
        if disc is None:
            raise ValueError("Choose a Parasite Eve USA Disc 1 or Disc 2 image.")
        originals = {}
        source, banks, _ = load_catalog()
        for bank in banks:
            number = bank["locations"].get(disc)
            if number is None:
                continue
            room = game.room(number)
            if room is None or 1 not in (slots := bank_slots(room.data)):
                raise ValueError(f"Room M{number:03d} is missing its dialogue bank.")
            raw = room.data[slots[1]:]
            pages, used = gametext.pages(raw)
            if f"{zlib.crc32(raw[:used]):08x}" != bank["crc32"]:
                raise ValueError(f"Disc {disc}, room M{number:03d} does not match the US dialogue catalog.")
            for number, (page, _) in pages.items():
                key = f"dlg/{bank['key']}/{number:03d}"
                if key in source:
                    original = gametext.decode(page)
                    if manifest.english_hash(original) != source[key]["english_hash"]:
                        raise ValueError(f"{key}: the disc text does not match the catalog.")
                    originals[key] = original
        return originals
    finally:
        game.disc.f.close()


def read_small(path):
    with Path(path).open("rb") as stream:
        data = stream.read(LIMIT + 1)
    if len(data) > LIMIT:
        raise ValueError("This file exceeds the editor’s 32 MiB limit.")
    return data


def check_text(text):
    if not isinstance(text, str) or len(text) > 4096 or "\x00" in text:
        raise ValueError("Invalid or oversized translation.")
    return text


def atlas_from_pack(data):
    if len(data) < 64 or data[:4] != b"PELG":
        raise ValueError("The project is missing its playable language pack.")
    version = struct.unpack_from("<I", data, 4)[0]
    if version == 1:
        return None
    if version != 2 or len(data) < 80:
        raise ValueError("Unsupported playable language pack version.")
    offset, count, stride, reserved = struct.unpack_from("<IIII", data, 64)
    if stride != cjk.RECORD or reserved or not 80 <= offset <= len(data) or not count or offset + count * stride != len(data):
        raise ValueError("Invalid embedded font atlas.")
    records = data[offset:]
    chars = [chr(struct.unpack_from("<I", records, i * stride)[0]) for i in range(count)]
    if any(not cjk.supported(ord(ch)) for ch in chars) or chars != sorted(set(chars)):
        raise ValueError("Invalid embedded font characters.")
    if any((pixel & 15) > 2 or (pixel >> 4) > 2
           for i in range(count) for pixel in records[i * stride + 4:(i + 1) * stride]):
        raise ValueError("Invalid embedded glyph pixels.")
    codes = {ch: cjk.token(i) for i, ch in enumerate(chars)}
    return SimpleNamespace(chars=chars, records=records, codes=codes, characters={v: k for k, v in codes.items()})


def encode_row(key, text, source, atlas):
    if source["structure"] is None:
        raise ValueError("Keep the original dialogue for this line because it uses an unsupported game control.")
    parts = text.split(BOX)
    if len(parts) > 3 or (len(parts) > 1 and any(not part.strip() for part in parts)):
        raise ValueError("Use at most two extra boxes and put text in each box.")
    if len(parts) > 1:
        if any(command.startswith(b"\xfb\x09") for command in source["structure"]):
            raise ValueError("Extra boxes are unavailable for dialogue choices.")
        codes = atlas.codes if atlas else None
        chunks = [gametext.encode(part, extended=True, cjk_codes=codes) for part in parts]
        validation.check(b"".join(chunks), manifest.source_structure(source))
        # Only the editor's box separators add waits. The original script controls still match.
        source = dict(source, structure=validation.structure(b"\xf8".join(chunks)))
    raw, errors, warnings = pelang.check_row(key, text.replace(BOX, "{wait}"), source, {}, atlas=atlas)
    if errors:
        raise ValueError("; ".join(errors))
    return raw, warnings


@dataclass
class Project:
    source: dict
    language: str = "zh-TW"
    title: str = "Opera pilot"
    rows: dict = field(default_factory=dict)
    baseline: dict = field(default_factory=dict)
    font: Path | None = None
    atlas: object = None
    needs_review: set[str] = field(default_factory=set)

    def apply_ai_translations(self, changes):
        self.rows.update(changes)
        self.needs_review.update(changes)

    def metadata(self):
        return {"scene": SCENE, "version": 1, "language": self.language, "title": self.title}

    def compile(self):
        if not re.fullmatch(r"[A-Za-z0-9]+(?:-[A-Za-z0-9]+)*", self.language) or len(self.language) > 15:
            raise ValueError("Enter a language code such as fr or zh-TW, up to 15 characters.")
        if not self.title.strip() or "\x00" in self.title or len(self.title.encode("utf-8")) > 31:
            raise ValueError("Enter a pack name that fits in 31 UTF-8 bytes.")
        needed = {ch for value in self.rows.values() for ch in value if cjk.supported(ord(ch))}
        atlas = self.atlas
        if self.font:
            atlas = cjk.Atlas(self.rows.values(), [self.font])
        elif needed and (not atlas or needed - atlas.codes.keys()):
            raise ValueError("Choose a font containing the custom characters used in this project.")
        bank_keys = {key.split("/")[1] for key, value in self.rows.items() if value} or {"M001"}
        entries = {f"bank/{bank['crc32']}": bank["key"].encode("ascii")
                   for bank in load_catalog()[1] if bank["key"] in bank_keys}
        previews = {}
        for key, value in sorted(self.rows.items()):
            check_text(value)
            if not value:
                continue
            try:
                raw, warnings = encode_row(key, value, self.source[key], atlas)
            except ValueError as error:
                raise ValueError(f"{key}: {error}") from error
            end = int(self.source[key].get("end", "FF"), 16)
            entries[key] = raw + (bytes([end]) if end != 255 else b"")
            previews[key] = (raw, warnings)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "language.pelang"
            pelang.write_pack(path, self.language, self.title, entries, atlas)
            return path.read_bytes(), previews, atlas

    def export_project(self):
        pack, _, _ = self.compile()
        data = io.BytesIO()
        with zipfile.ZipFile(data, "w", zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("project.json", json.dumps(dict(self.metadata(), translations=self.rows, needs_review=sorted(self.needs_review)), ensure_ascii=False))
            archive.writestr(f"{self.language}.pelang", pack)
        return data.getvalue()

    @classmethod
    def import_project(cls, source, data):
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            files = archive.infolist()
            if len(files) != 2 or any(info.file_size > LIMIT for info in files):
                raise ValueError("Invalid or oversized translation project.")
            meta = json.loads(archive.read("project.json"))
            check_header(meta)
            language, title = meta["language"], meta["title"]
            if not isinstance(language, str) or not isinstance(title, str) or set(archive.namelist()) != {"project.json", f"{language}.pelang"}:
                raise ValueError("Invalid project contents.")
            rows = meta["translations"]
            if not isinstance(rows, dict) or set(rows) - source.keys():
                raise ValueError("This project contains unknown dialogue IDs.")
            rows = {key: check_text(value) for key, value in rows.items()}
            pack = archive.read(f"{language}.pelang")
        review = meta.get("needs_review", [])
        if (not isinstance(review, list)
                or any(not isinstance(key, str) or not rows.get(key, "").strip() for key in review)):
            raise ValueError("Review status refers to a line without a translation.")
        result = cls(source, language, title, rows, rows.copy(), atlas=atlas_from_pack(pack),
                     needs_review=set(review))
        if result.compile()[0] != pack:
            raise ValueError("The editable translations and playable pack do not match.")
        return result

    def export_fixes(self):
        changes = [{"key": key, "before": digest(self.baseline.get(key, "")), "text": self.rows.get(key, "")}
                   for key in self.source if self.rows.get(key, "") != self.baseline.get(key, "")]
        if not changes:
            raise ValueError("No edits since this project was opened.")
        return json.dumps(dict(self.metadata(), changes=changes), ensure_ascii=False, indent=2).encode("utf-8")

    def import_fixes(self, data):
        meta = json.loads(data)
        check_header(meta)
        if meta["language"] != self.language:
            raise ValueError(f"These fixes are for {meta['language']}. Open that language first.")
        pending, conflicts = {}, []
        for change in meta["changes"]:
            key, value = change["key"], check_text(change["text"])
            if key not in self.source or key in pending:
                raise ValueError("Unknown or duplicate dialogue key in fixes.")
            current = self.rows.get(key, "")
            if current != value and digest(current) != change["before"]:
                conflicts.append(key)
            pending[key] = value
        if conflicts:
            raise ValueError("Local edits conflict with these lines. No fixes were applied.\n" + "\n".join(conflicts))
        self.rows.update(pending)
        self.needs_review.difference_update(key for key, value in pending.items() if not value.strip())
        return len(pending)


def check_header(meta):
    if not isinstance(meta, dict) or meta.get("version") != 1 or meta.get("scene") not in (SCENE, LEGACY_SCENE):
        raise ValueError("This file belongs to a different editor version or scene.")


@lru_cache(maxsize=1)
def load_catalog():
    path = Path(__file__).with_name("catalog.json")
    data = json.loads(path.read_text(encoding="utf-8"))
    rows, banks, _ = manifest.load(path)
    return rows, banks, data["scenes"]


def load_source():
    return load_catalog()[0]


def scope_keys(scope):
    source, _, scenes = load_catalog()
    if scope == "all":
        return tuple(source)
    if scope not in scenes:
        raise ValueError("The CSV has an unknown dialogue scope.")
    return tuple(scenes[scope]["keys"])
