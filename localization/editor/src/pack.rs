//! The `.pelang` rules shared with `localization/pelang.py`, for dialogue rows. The pack format is
//! read by `mods/pe_lang/pe_lang_pack.c`: a 64-byte header (80 with a CJK atlas), entries sorted by
//! FNV-1a hash of the key, then each entry's glyph bytes followed by FF, then the atlas records.
use indexmap::IndexMap;

use crate::catalog::{english_shape, source_structure, Row};
use crate::cjk::{self, Atlas};
use crate::error::{Error, Result};
use crate::{gametext as gt, layout, validation, wrap};

const NO_GLYPH_HELP: &str = " (the font has A-Z, a-z, digits, basic punctuation, Latin Extended, Russian/Ukrainian/Belarusian Cyrillic, and CJK with --cjk-font)";

pub fn fnv1a(key: &str) -> u32 {
    key.bytes().fold(0x811C_9DC5u32, |hash, byte| (hash ^ byte as u32).wrapping_mul(0x0100_0193))
}

/// (glyph bytes or None, errors, warnings) for one translated dialogue row.
pub fn check_row(text: &str, source: &Row, atlas: Option<&Atlas>) -> Result<(Option<Vec<u8>>, Vec<String>, Vec<String>)> {
    let name_px = layout::name_width();
    let mut raw = match gt::encode(text, true, atlas.map(|a| &a.codes)) {
        Ok(raw) => raw,
        Err(error) => {
            let mut message = format!("invalid text/tag: {error}");
            if message.contains("no glyph") {
                message.push_str(NO_GLYPH_HELP);
            }
            return Ok((None, vec![message], Vec::new()));
        }
    };
    let checked = source_structure(source).and_then(|structure| validation::check(&raw, structure));
    if let Err(error) = checked {
        return Ok((None, vec![error.message], Vec::new()));
    }
    for command in validation::commands(&raw)? {
        if command[0] == 0xF1 && atlas.is_none_or(|a| cjk::index(command) >= a.chars.len()) {
            return Ok((None, vec!["CJK token has no glyph in this pack's dictionary".to_owned()], Vec::new()));
        }
    }
    let mut note = None;
    if let Some((width, lines)) = layout::box_limit(source) {
        let overflow = layout::screens(&raw, name_px)?.iter().any(|screen| {
            screen.iter().copied().max().unwrap_or(0) as i64 > width || screen.len() as i64 > lines
        });
        if overflow {
            let characters = atlas.map(|a| &a.characters);
            let (wrapped, added) = wrap::wrap(&raw, width, lines, &english_shape(source), name_px, characters)?;
            if wrapped != raw {
                raw = wrapped;
                let extra = if added > 0 { format!(", {added} screen(s) added") } else { String::new() };
                note = Some(format!("wrapped to fit the box{extra}; check the breaks"));
            }
        }
    }
    let (errors, mut warnings) = layout::check(&raw, source, name_px)?;
    if let Some(note) = note {
        warnings.insert(0, note);
    }
    Ok((Some(raw), errors, warnings))
}

pub fn write_pack(lang: &str, name: &str, entries: &IndexMap<String, Vec<u8>>, atlas: Option<&Atlas>) -> Result<Vec<u8>> {
    let (lang_b, name_b) = (lang.as_bytes(), name.as_bytes());
    if lang_b.is_empty() || name_b.is_empty() || lang_b.contains(&0) || name_b.contains(&0) || lang_b.len() > 15 || name_b.len() > 31 {
        return Err(Error::value("--lang and --name must be nonempty, without NUL; at most 15 and 31 bytes"));
    }
    let mut order: Vec<(&String, &Vec<u8>)> = entries.iter().collect();
    order.sort_by_key(|(key, _)| fnv1a(key));
    let (mut text, mut table) = (Vec::new(), Vec::new());
    for (key, value) in &order {
        table.extend_from_slice(&fnv1a(key).to_le_bytes());
        table.extend_from_slice(&(text.len() as u32).to_le_bytes());
        text.extend_from_slice(value);
        text.push(0xFF);
    }
    let records: &[u8] = atlas.map_or(&[], |a| &a.records);
    let has_records = !records.is_empty();
    let (version, header_size) = if has_records { (2u32, 80usize) } else { (1, 64) };
    let mut out = Vec::with_capacity(header_size + table.len() + text.len() + records.len());
    out.extend_from_slice(b"PELG");
    out.extend_from_slice(&version.to_le_bytes());
    out.extend_from_slice(&(order.len() as u32).to_le_bytes());
    out.extend_from_slice(&((header_size + table.len()) as u32).to_le_bytes());
    let mut fixed = [0u8; 48];
    fixed[..lang_b.len()].copy_from_slice(lang_b);
    fixed[16..16 + name_b.len()].copy_from_slice(name_b);
    out.extend_from_slice(&fixed);
    if let Some(atlas) = atlas.filter(|_| has_records) {
        out.extend_from_slice(&((header_size + table.len() + text.len()) as u32).to_le_bytes());
        out.extend_from_slice(&(atlas.chars.len() as u32).to_le_bytes());
        out.extend_from_slice(&(cjk::RECORD as u32).to_le_bytes());
        out.extend_from_slice(&0u32.to_le_bytes());
    }
    out.extend_from_slice(&table);
    out.extend_from_slice(&text);
    out.extend_from_slice(records);
    Ok(out)
}
