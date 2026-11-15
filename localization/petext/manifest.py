"""The source manifest: everything the pack compiler checks a translation against, without the
game's text, so it can be committed and contributors and CI can build packs without the discs.

Per key it keeps the source's shape, never its words: the control commands a translation must
keep (waits, choices, names, numbers, pauses, highlighting; validation.structure), the byte length
and pixel widths, the boxes a page shows in, the page end, whether the English indents lines after
a break or a wait, the item kind, the item names a page mentions (as keys), and a short hash of
the English (to spot a row submitted untranslated). Besides: the banks' CRC32s (as banks.json)
and the menus' glyph widths (as menu_glyphs.json).

  python -I pelang.py manifest out/text/strings.json -o data/manifest.json
"""
from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path

from . import gametext as gt
from . import validation
from . import wrap

FORMAT = "pelang-manifest"
VERSION = 1
KEEP = ("end", "boxes", "boxes_possible", "boxes_complete", "menu_px", "item_kind")


def english_hash(text: str) -> str:
    return hashlib.sha1(text.encode("utf-8")).hexdigest()[:12]


def item_mentions(rows: dict[str, dict]) -> dict[str, list[str]]:
    """Dialogue key -> the item keys whose English names its English names (whole words)."""
    names = {k: r["text"] for k, r in rows.items()
             if k.startswith("item/") and int(k[5:]) <= 234 and len(r["text"]) >= 4 and "{" not in r["text"]}
    patterns = {k: re.compile(r"(?<![A-Za-z])" + re.escape(n) + r"(?![A-Za-z])") for k, n in names.items()}
    out = {}
    for k, r in rows.items():
        if k.startswith("dlg/"):
            hits = sorted(i for i, p in patterns.items() if p.search(r["text"]))
            if hits:
                out[k] = hits
    return out


def build(rows: dict[str, dict], banks: list[dict], metrics: bytes | None) -> dict:
    mentions = item_mentions(rows)
    entries = {}
    for k, r in sorted(rows.items()):
        raw = bytes.fromhex(r["raw"]) if r.get("raw") else gt.encode(r["text"])
        try:
            shape = [c.hex() for c in validation.structure(raw)]
        except ValueError:
            shape = None                       # unsupported source controls: the build refuses the row
        e = {"structure": shape, "length": len(raw), "indent": list(wrap.indents(raw)),
             "english_hash": english_hash(r["text"])}
        e.update({f: r[f] for f in KEEP if f in r and r[f] not in ([], None)})
        if not k.startswith("dlg/") and r.get("lines_px"):
            e["lines_px"] = [max(r["lines_px"])]       # menu widths' fallback without the metrics
        if k in mentions:
            e["mentions"] = mentions[k]
        entries[k] = e
    return {"format": FORMAT, "version": VERSION, "entries": entries,
            "banks": [{"key": b["key"], "crc32": b["crc32"]} for b in banks],
            "menu_metrics": metrics.hex() if metrics is not None else None}


def is_manifest(path) -> bool:
    try:
        with open(path, encoding="utf-8") as f:
            return f.read(64).lstrip().startswith('{') and json.loads(Path(path).read_text(encoding="utf-8")).get("format") == FORMAT
    except (OSError, ValueError):
        return False


def load(path) -> tuple[dict[str, dict], list[dict], bytes | None]:
    """(rows by key as the compiler reads strings.json rows, banks, menu metrics)."""
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    if data.get("format") != FORMAT or data.get("version") != VERSION:
        raise ValueError(f"{path}: not a version {VERSION} pelang manifest")
    rows = {}
    for k, e in data["entries"].items():
        r = dict(e, key=k)
        r["structure"] = None if e["structure"] is None else [bytes.fromhex(c) for c in e["structure"]]
        rows[k] = r
    metrics = bytes.fromhex(data["menu_metrics"]) if data.get("menu_metrics") else None
    return rows, data["banks"], metrics


def source_structure(source: dict):
    """What validation.check compares a translation's controls with: the manifest's structure, or
    the English text's."""
    if "structure" in source:
        if source["structure"] is None:
            raise ValueError("the source has unsupported control bytes: keep the game's text")
        return source["structure"]
    return source["text"]


def english_shape(source: dict) -> bytes:
    """The English bytes, or (from a manifest) a stand-in with the same indent style: what the
    wrapper reads the indent convention from."""
    if source.get("raw"):
        return bytes.fromhex(source["raw"])
    if "indent" in source:
        after_break, after_wait = source["indent"]
        return (bytes([0xF7, 0x0F]) if after_break else b"") + (bytes([0xF8, 0x0F]) if after_wait else b"")
    return gt.encode(source["text"])


def english_length(source: dict) -> int:
    if "length" in source:
        return source["length"]
    return len(bytes.fromhex(source["raw"])) if source.get("raw") else len(gt.encode(source["text"]))
