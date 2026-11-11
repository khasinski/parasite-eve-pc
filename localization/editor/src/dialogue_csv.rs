//! The CSV exchange for the editor's AI translation workflow.
use std::collections::{HashMap, HashSet};

use indexmap::IndexMap;

use crate::catalog::{english_hash, scope_keys, source_structure, Source};
use crate::dialogue_search::tag_pattern;
use crate::error::{Error, Result};
use crate::model::{check_text, KEYS};
use crate::pycompat::{decode_utf8_sig, strip};
use crate::pycsv::{splitlines_keepends, universal_lines, write_row, DictReader};
use crate::{cjk, gametext, validation};

pub const LEGACY_COLUMNS: [&str; 3] = ["key", "english", "translation"];
pub const COLUMNS: [&str; 5] = ["scope", "key", "english", "translation", "notes"];
pub const UNSUPPORTED: &str = "Keep translation blank. This line uses an unsupported game control.";

pub fn tags(text: &str) -> Vec<&str> {
    tag_pattern().find_iter(text).map(|m| m.as_str()).collect()
}

pub fn export_dialogue(original: &IndexMap<String, String>, source: &Source, scope: &str) -> Result<Vec<u8>> {
    let keys = scope_keys(scope)?;
    if keys.iter().any(|key| !original.contains_key(key)) {
        return Err(Error::value("Load the required USA disc images before exporting this dialogue."));
    }
    let mut out = String::from("\u{feff}");
    write_row(&mut out, &COLUMNS);
    for key in &keys {
        let notes = if source[key].structure.is_none() { UNSUPPORTED } else { "" };
        write_row(&mut out, &[scope, key, &original[key], "", notes]);
    }
    Ok(out.into_bytes())
}

pub fn ai_prompt(language: &str) -> String {
    format!(
        "Translate the attached Parasite Eve dialogue CSV into {language}.\n\
         Fill in only the translation column. Keep the header, every row, and the original row order. \
         Copy the scope, key (line ID), english and notes columns exactly. \
         Leave translation blank where the notes say to keep it blank.\n\
         Keep all game tags unchanged and in their original order, including {{name}}, {{br}}, {{wait}}, \
         and any tag with a number. Translate the words around them. \
         Use {{br}} for game line breaks already in the source and do not add new tags.\n\
         Return only valid UTF-8 CSV. Quote fields containing commas or double quotes, \
         double any quotes inside a quoted field, and omit Markdown fences and commentary."
    )
}

/// A plain translation CSV with key and translation columns: (rows in the catalog, rows skipped).
pub fn read_plain_csv(data: &[u8], source: &Source) -> Result<(IndexMap<String, String>, usize)> {
    let text = decode_utf8_sig(data)?;
    let mut reader = DictReader::new(splitlines_keepends(&text), false)?;
    let names = reader.fieldnames.clone().unwrap_or_default();
    if !names.iter().any(|name| name == "key") || !names.iter().any(|name| name == "translation") {
        return Err(Error::value("The CSV needs key and translation columns."));
    }
    let (mut rows, mut ignored) = (IndexMap::new(), 0);
    while let Some(row) = reader.next_row()? {
        let value = row.get("translation").cloned().flatten();
        match row.get("key").cloned().flatten().filter(|key| source.contains_key(key)) {
            None => ignored += 1,
            Some(key) => {
                let Some(value) = value.filter(|_| !rows.contains_key(&key)) else {
                    return Err(Error::value("Duplicate or incomplete scene entry in the CSV."));
                };
                rows.insert(key, value);
            }
        }
    }
    if rows.is_empty() {
        return Err(Error::value("This CSV has no entries in the dialogue catalog."));
    }
    Ok((rows, ignored))
}

pub fn import_translation(data: &[u8], source: &Source) -> Result<IndexMap<String, String>> {
    let text = decode_utf8_sig(data)?;
    let mut reader = DictReader::new(universal_lines(&text), true)?;
    let names: Vec<String> = reader.fieldnames.clone().unwrap_or_default();
    let legacy = reader.fieldnames.is_some() && names == LEGACY_COLUMNS;
    if !legacy && names != COLUMNS {
        return Err(Error::value("Keep all columns from the exported CSV."));
    }
    let mut scope: Option<String> = legacy.then(|| "opera".to_owned());
    let mut expected: Option<HashSet<String>> = legacy.then(|| KEYS.iter().map(|&k| k.to_owned()).collect());
    let mut rows: IndexMap<String, String> = IndexMap::new();
    while let Some(row) = reader.next_row()? {
        if row.extra || row.fields.iter().any(|(_, value)| value.is_none()) {
            return Err(Error::value(format!("Row {} has the wrong number of CSV fields.", reader.line_num())));
        }
        let field = |name: &str| row.get(name).and_then(Clone::clone).unwrap_or_default();
        let (key, english, value) = (field("key"), field("english"), field("translation"));
        if !legacy {
            let row_scope = field("scope");
            if scope.is_none() {
                expected = Some(scope_keys(&row_scope)?.into_iter().collect());
                scope = Some(row_scope.clone());
            }
            if scope.as_deref() != Some(row_scope.as_str()) {
                return Err(Error::value("Use one exported scene or all-dialogue file per import."));
            }
        }
        let is_expected = expected.as_ref().is_some_and(|keys| keys.contains(&key));
        let Some(entry) = source.get(&key).filter(|_| is_expected && !rows.contains_key(&key)) else {
            return Err(Error::value(format!("Unknown or repeated line ID: {key}. Keep each key exactly as exported.")));
        };
        if english_hash(&english) != entry.english_hash {
            return Err(Error::value(format!("{key}: the English column changed. Copy it exactly from the export.")));
        }
        let unsupported = entry.structure.is_none();
        if !legacy && field("notes") != if unsupported { UNSUPPORTED } else { "" } {
            return Err(Error::value(format!("{key}: keep the notes column unchanged.")));
        }
        check_text(&value)?;
        let filled = !strip(&value).is_empty();
        if unsupported && filled {
            return Err(Error::value(format!("{key}: leave this translation blank to keep the original game controls.")));
        }
        if filled {
            if tags(&english) != tags(&value) {
                return Err(Error::value(format!("{key}: keep the original game tags unchanged and in order.")));
            }
            // Check the controls independently of any font installed on this computer.
            let mut unique: Vec<char> = value.chars().filter(|&ch| cjk::supported(ch as u32)).collect();
            unique.sort_unstable();
            unique.dedup();
            let codes: HashMap<char, Vec<u8>> =
                unique.iter().enumerate().map(|(i, &ch)| Ok((ch, cjk::token(i)?))).collect::<Result<_>>()?;
            let checked = gametext::encode(&value, true, Some(&codes))
                .and_then(|raw| validation::check(&raw, source_structure(entry)?));
            checked.map_err(|error| error.for_key(&key))?;
        }
        rows.insert(key, value);
    }
    let complete = expected.is_some_and(|keys| keys.len() == rows.len() && rows.keys().all(|key| keys.contains(key)));
    if rows.is_empty() || !complete {
        return Err(Error::value("Some line IDs are missing. Return every row from the exported CSV."));
    }
    rows.retain(|_, value| !strip(value).is_empty());
    Ok(rows)
}
