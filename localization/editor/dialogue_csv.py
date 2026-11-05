"""The CSV exchange for the editor's AI translation workflow."""
import csv
import io
import re

from model import KEYS, check_text, scope_keys
from petext import cjk, gametext, manifest, validation

LEGACY_COLUMNS = ["key", "english", "translation"]
COLUMNS = ["scope", "key", "english", "translation", "notes"]
UNSUPPORTED = "Keep translation blank. This line uses an unsupported game control."
TAG = re.compile(r"\{[^{}]+\}")


def export_dialogue(original, source, scope):
    keys = scope_keys(scope)
    if any(key not in original for key in keys):
        raise ValueError("Load the required USA disc images before exporting this dialogue.")
    output = io.StringIO(newline="")
    writer = csv.writer(output)
    writer.writerow(COLUMNS)
    writer.writerows((scope, key, original[key], "", UNSUPPORTED if source[key]["structure"] is None else "")
                     for key in keys)
    return output.getvalue().encode("utf-8-sig")


def ai_prompt(language):
    return (
        f"Translate the attached Parasite Eve dialogue CSV into {language}.\n"
        "Fill in only the translation column. Keep the header, every row, and the original row order. "
        "Copy the scope, key (line ID), english and notes columns exactly. "
        "Leave translation blank where the notes say to keep it blank.\n"
        "Keep all game tags unchanged and in their original order, including {name}, {br}, {wait}, "
        "and any tag with a number. Translate the words around them. "
        "Use {br} for game line breaks already in the source and do not add new tags.\n"
        "Return only valid UTF-8 CSV. Quote fields containing commas or double quotes, "
        "double any quotes inside a quoted field, and omit Markdown fences and commentary."
    )


def import_translation(data, source):
    reader = csv.DictReader(io.StringIO(data.decode("utf-8-sig"), newline=""), strict=True)
    legacy = reader.fieldnames == LEGACY_COLUMNS
    if not legacy and reader.fieldnames != COLUMNS:
        raise ValueError("Keep all columns from the exported CSV.")
    scope = "opera" if legacy else None
    expected = set(KEYS) if legacy else None
    rows = {}
    for row in reader:
        key, english, value = (row.get(column) for column in LEGACY_COLUMNS)
        if None in row or any(field is None for field in row.values()):
            raise ValueError(f"Row {reader.line_num} has the wrong number of CSV fields.")
        if not legacy:
            if scope is None:
                scope = row["scope"]
                expected = set(scope_keys(scope))
            if row["scope"] != scope:
                raise ValueError("Use one exported scene or all-dialogue file per import.")
        if key not in expected or key not in source or key in rows:
            raise ValueError(f"Unknown or repeated line ID: {key}. Keep each key exactly as exported.")
        if manifest.english_hash(english) != source[key]["english_hash"]:
            raise ValueError(f"{key}: the English column changed. Copy it exactly from the export.")
        unsupported = source[key]["structure"] is None
        if not legacy and row["notes"] != (UNSUPPORTED if unsupported else ""):
            raise ValueError(f"{key}: keep the notes column unchanged.")
        value = check_text(value)
        if unsupported and value.strip():
            raise ValueError(f"{key}: leave this translation blank to keep the original game controls.")
        if value.strip():
            if TAG.findall(english) != TAG.findall(value):
                raise ValueError(f"{key}: keep the original game tags unchanged and in order.")
            # Check the controls independently of any font installed on this computer.
            codes = {ch: cjk.token(i) for i, ch in enumerate(
                sorted({ch for ch in value if cjk.supported(ord(ch))})
            )}
            try:
                raw = gametext.encode(value, extended=True, cjk_codes=codes)
                validation.check(raw, manifest.source_structure(source[key]))
            except ValueError as error:
                raise ValueError(f"{key}: {error}") from error
        rows[key] = value
    if not rows or set(rows) != expected:
        raise ValueError("Some line IDs are missing. Return every row from the exported CSV.")
    return {key: value for key, value in rows.items() if value.strip()}
