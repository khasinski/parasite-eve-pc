"""Verify extra boxes, bundled glyphs, and both editor exchange formats."""
import argparse
import csv
import io
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from dialogue_search import dialogue_matches, normalize, preferred_scenes, readable_dialogue
from dialogue_csv import COLUMNS, TAG, ai_prompt, export_dialogue, import_translation
from model import DEMO_FONT, KEYS, SAMPLE, Project, layout, load_catalog, load_source, read_original, scope_keys, validation


def run(output, disc=None, font=DEMO_FONT):
    source = load_source()
    scenes = load_catalog()[2]
    mapped = preferred_scenes(scenes)
    assert set(mapped) == set(source)
    assert scenes["d1-M002"]["label"].startswith("Carnegie Hall · Stage")
    assert scenes["d2-M273"]["label"].startswith("Statue of Liberty")

    original = read_original(disc) if disc else {}
    if disc:
        assert set(original) == set(source) and "opera" in original[KEYS[0]]
        index = {key: (text, normalize(text)) for key, raw in original.items()
                 if (text := readable_dialogue(raw))}
        matches = dict(dialogue_matches(index, "to come to the OPERA"))
        assert KEYS[0] in matches and mapped[KEYS[0]] == "opera"
        assert not dialogue_matches(index, "this sentence is not in the game")
        print("PASS: readable room names and case-insensitive search across dialogue breaks")

        all_rows = export_dialogue(original, source, "all")
        assert not import_translation(all_rows, source)
        assert len(list(csv.DictReader(io.StringIO(all_rows.decode("utf-8-sig"))))) == len(source)
        room = next(key for key in load_catalog()[2] if key.startswith("d2-"))
        room_rows = export_dialogue(original, source, room)
        assert not import_translation(room_rows, source)
        assert len(list(csv.DictReader(io.StringIO(room_rows.decode("utf-8-sig"))))) == len(scope_keys(room))
        exported = export_dialogue(original, source, "opera")
        rows = list(csv.DictReader(io.StringIO(exported.decode("utf-8-sig"))))
        assert [row["key"] for row in rows] == list(KEYS)
        assert all(not row["translation"] for row in rows)
        tagged = rows[1]
        tagged["translation"] = 'Bonjour, "ami". ' + " ".join(TAG.findall(tagged["english"]))

        def encoded():
            stream = io.StringIO(newline="")
            writer = csv.DictWriter(stream, fieldnames=COLUMNS)
            writer.writeheader()
            writer.writerows(rows)
            return stream.getvalue().encode("utf-8")

        assert import_translation(encoded(), source) == {tagged["key"]: tagged["translation"]}
        assert "{name}" in ai_prompt("fr") and "line ID" in ai_prompt("fr")
        for field, bad in (("key", "dlg/M001/999"), ("english", "Edited source"),
                           ("translation", "Translation with the tags removed")):
            original_value = tagged[field]
            tagged[field] = bad
            try:
                import_translation(encoded(), source)
            except ValueError:
                pass
            else:
                raise AssertionError(f"CSV import accepted changed {field}")
            tagged[field] = original_value
        rows.pop()
        try:
            import_translation(encoded(), source)
        except ValueError:
            pass
        else:
            raise AssertionError("CSV import accepted a missing line")
        print("PASS: full catalog and scene CSV exports, translated quotes and tags, invalid returns refused")

    project = Project(source, rows=SAMPLE.copy(), font=font)
    pack, previews, atlas = project.compile()
    page = previews[KEYS[0]][0]
    assert len(layout.screens(page)) == 2 and len(atlas.chars) == 21
    assert sum(c == b"\xf8" for c in validation.commands(page)) == 1
    editable = project.export_project()
    reopened = Project.import_project(source, editable)
    assert reopened.font is None and reopened.rows == project.rows
    assert reopened.compile()[0] == pack
    project.apply_ai_translations({KEYS[0]: SAMPLE[KEYS[0]]})
    reopened_review = Project.import_project(source, project.export_project())
    assert reopened_review.needs_review == {KEYS[0]}
    assert reopened_review.compile()[0] == pack
    reopened_review.needs_review.remove(KEYS[0])
    assert not Project.import_project(source, reopened_review.export_project()).needs_review
    second = next(key for key, row in source.items()
                  if not key.startswith("dlg/M001/") and row["structure"] == [])
    combined = Project(source, rows={**project.rows, second: "Test"}, font=font)
    assert Project.import_project(source, combined.export_project()).rows == combined.rows

    fixes = project.export_fixes()
    clean = Project(source, font=font)
    assert clean.import_fixes(fixes) == 2
    assert clean.import_fixes(fixes) == 2 and clean.compile()[0] == pack
    conflict = Project(source, rows={KEYS[0]: "Another edit"})
    try:
        conflict.import_fixes(fixes)
    except ValueError:
        assert conflict.rows == {KEYS[0]: "Another edit"}
    else:
        raise AssertionError("Conflict should refuse the entire import")
    missing_name = Project(source, rows={KEYS[1]: "A translation{box}with no name token"})
    try:
        missing_name.compile()
    except ValueError:
        pass
    else:
        raise AssertionError("Missing original controls should refuse compilation")
    if original:
        assert original[KEYS[0]] not in fixes.decode()
    output.mkdir(parents=True, exist_ok=True)
    (output / "opera-demo.pepack").write_bytes(editable)
    (output / "opera-demo.pefix").write_bytes(fixes)
    (output / "opera-demo.pelang").write_bytes(pack)
    print("PASS: extra box, 21 raster glyphs, editable pack round trip, fixes and conflict refusal")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--disc", type=Path)
    parser.add_argument("--font", type=Path, default=DEMO_FONT)
    args = parser.parse_args()
    run(args.output, args.disc, args.font)
