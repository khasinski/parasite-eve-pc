//! Verify extra boxes, bundled glyphs, and both editor exchange formats.
use std::collections::BTreeSet;
use std::path::Path;

use indexmap::IndexMap;

use crate::catalog::{self, scope_keys};
use crate::cjk::FontSource;
use crate::dialogue_csv::{ai_prompt, export_dialogue, import_translation, tags, COLUMNS};
use crate::dialogue_search::{build_index, dialogue_matches, preferred_scenes};
use crate::error::Result;
use crate::model::{load_source, read_original, sample, Project, KEYS};
use crate::pycompat::decode_utf8_sig;
use crate::pycsv::{universal_lines, write_row, DictReader};
use crate::{layout, validation};

type CsvRows = Vec<IndexMap<String, String>>;

fn parse_rows(data: &[u8]) -> Result<CsvRows> {
    let text = decode_utf8_sig(data)?;
    let mut reader = DictReader::new(universal_lines(&text), false)?;
    let mut rows = Vec::new();
    while let Some(row) = reader.next_row()? {
        rows.push(row.fields.into_iter().map(|(name, value)| (name, value.unwrap_or_default())).collect());
    }
    Ok(rows)
}

fn encoded(rows: &CsvRows) -> Vec<u8> {
    let mut out = String::new();
    write_row(&mut out, &COLUMNS);
    for row in rows {
        let fields: Vec<&str> = COLUMNS.iter().map(|name| row[*name].as_str()).collect();
        write_row(&mut out, &fields);
    }
    out.into_bytes()
}

pub fn run(output: &Path, disc: Option<&Path>, font: &FontSource) -> Result<()> {
    let source = load_source();
    let scenes = &catalog::load().scenes;
    let mapped = preferred_scenes(scenes);
    assert_eq!(mapped.len(), source.len());
    assert!(source.keys().all(|key| mapped.contains_key(key)));
    assert!(scenes["d1-M002"].label.starts_with("Carnegie Hall · Stage"));
    assert!(scenes["d2-M273"].label.starts_with("Statue of Liberty"));

    let original = match disc {
        Some(disc) => read_original(disc)?,
        None => IndexMap::new(),
    };
    if disc.is_some() {
        assert_eq!(original.len(), source.len());
        assert!(source.keys().all(|key| original.contains_key(key)) && original[KEYS[0]].contains("opera"));
        let index = build_index(&original);
        let matches = dialogue_matches(&index, "to come to the OPERA");
        assert!(matches.iter().any(|(key, _)| key == KEYS[0]) && mapped[KEYS[0]] == "opera");
        assert!(dialogue_matches(&index, "this sentence is not in the game").is_empty());
        println!("PASS: readable room names and case-insensitive search across dialogue breaks");

        let all_rows = export_dialogue(&original, source, "all")?;
        assert!(import_translation(&all_rows, source)?.is_empty());
        assert_eq!(parse_rows(&all_rows)?.len(), source.len());
        let room = scenes.keys().find(|key| key.starts_with("d2-")).expect("a Disc 2 scene");
        let room_rows = export_dialogue(&original, source, room)?;
        assert!(import_translation(&room_rows, source)?.is_empty());
        assert_eq!(parse_rows(&room_rows)?.len(), scope_keys(room)?.len());
        let exported = export_dialogue(&original, source, "opera")?;
        let mut rows = parse_rows(&exported)?;
        assert_eq!(rows.iter().map(|row| row["key"].as_str()).collect::<Vec<_>>(), KEYS);
        assert!(rows.iter().all(|row| row["translation"].is_empty()));
        let translation = format!("Bonjour, \"ami\". {}", tags(&rows[1]["english"]).join(" "));
        rows[1].insert("translation".to_owned(), translation.clone());
        let imported = import_translation(&encoded(&rows), source)?;
        assert_eq!(imported.len(), 1);
        assert_eq!(imported[&rows[1]["key"]], translation);
        assert!(ai_prompt("fr").contains("{name}") && ai_prompt("fr").contains("line ID"));
        for (field, bad) in [("key", "dlg/M001/999"), ("english", "Edited source"), ("translation", "Translation with the tags removed")] {
            let kept = rows[1][field].clone();
            rows[1].insert(field.to_owned(), bad.to_owned());
            assert!(import_translation(&encoded(&rows), source).is_err(), "CSV import accepted changed {field}");
            rows[1].insert(field.to_owned(), kept);
        }
        rows.pop();
        assert!(import_translation(&encoded(&rows), source).is_err(), "CSV import accepted a missing line");
        println!("PASS: full catalog and scene CSV exports, translated quotes and tags, invalid returns refused");
    }

    let mut project = Project::new(source);
    project.rows = sample();
    project.font = Some(font.clone());
    let (pack, previews, atlas) = project.compile()?;
    let page = &previews[KEYS[0]].0;
    assert_eq!(layout::screens(page, layout::name_width())?.len(), 2);
    assert_eq!(atlas.as_ref().expect("an atlas").chars.len(), 21);
    assert_eq!(validation::commands(page)?.iter().filter(|c| **c == [0xF8]).count(), 1);
    let editable = project.export_project()?;
    let reopened = Project::import_project(source, &editable)?;
    assert!(reopened.font.is_none() && reopened.rows == project.rows);
    assert_eq!(reopened.compile()?.0, pack);
    project.apply_ai_translations(&IndexMap::from([(KEYS[0].to_owned(), sample()[KEYS[0]].clone())]));
    let mut reopened_review = Project::import_project(source, &project.export_project()?)?;
    assert_eq!(reopened_review.needs_review, BTreeSet::from([KEYS[0].to_owned()]));
    assert_eq!(reopened_review.compile()?.0, pack);
    reopened_review.needs_review.remove(KEYS[0]);
    assert!(Project::import_project(source, &reopened_review.export_project()?)?.needs_review.is_empty());
    let second = source
        .iter()
        .find(|(key, row)| !key.starts_with("dlg/M001/") && row.structure.as_ref().is_some_and(Vec::is_empty))
        .expect("a line without controls")
        .0;
    let mut combined = Project::new(source);
    combined.rows = project.rows.clone();
    combined.rows.insert(second.clone(), "Test".to_owned());
    combined.font = Some(font.clone());
    assert_eq!(Project::import_project(source, &combined.export_project()?)?.rows, combined.rows);

    let fixes = project.export_fixes()?;
    let mut clean = Project::new(source);
    clean.font = Some(font.clone());
    assert_eq!(clean.import_fixes(&fixes)?, 2);
    assert_eq!(clean.import_fixes(&fixes)?, 2);
    assert_eq!(clean.compile()?.0, pack);
    let mut conflict = Project::new(source);
    conflict.rows.insert(KEYS[0].to_owned(), "Another edit".to_owned());
    assert!(conflict.import_fixes(&fixes).is_err(), "Conflict should refuse the entire import");
    assert_eq!(conflict.rows.len(), 1);
    assert_eq!(conflict.rows[KEYS[0]], "Another edit");
    let mut missing_name = Project::new(source);
    missing_name.rows.insert(KEYS[1].to_owned(), "A translation{box}with no name token".to_owned());
    assert!(missing_name.compile().is_err(), "Missing original controls should refuse compilation");
    if !original.is_empty() {
        assert!(!String::from_utf8_lossy(&fixes).contains(&original[KEYS[0]]));
    }
    std::fs::create_dir_all(output)?;
    std::fs::write(output.join("opera-demo.pepack"), &editable)?;
    std::fs::write(output.join("opera-demo.pefix"), &fixes)?;
    std::fs::write(output.join("opera-demo.pelang"), &pack)?;
    println!("PASS: extra box, 21 raster glyphs, editable pack round trip, fixes and conflict refusal");
    Ok(())
}
