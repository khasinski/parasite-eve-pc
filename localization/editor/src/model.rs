//! Dialogue catalog access and the editor project, backed by the pack compiler.
use std::collections::{BTreeSet, HashMap};
use std::io::{Cursor, Read, Write};
use std::path::Path;
use std::sync::Arc;

use indexmap::IndexMap;
use serde_json::{json, Map, Value};
use sha2::{Digest, Sha256};
use zip::write::SimpleFileOptions;

use crate::catalog::{self, english_hash, source_structure, Row, Source};
use crate::cjk::{self, Atlas, FontSource};
use crate::disc::{bank_slots, Game};
use crate::error::{Error, Result};
use crate::pycompat::{dumps, strip, JSON_DEFAULT, JSON_INDENTED};
use crate::{gametext, pack, validation};

const LEGACY_SCENE: &str = "M001-opera-arrival";
const SCENE: &str = "pe-usa-dialogue";
pub const KEYS: [&str; 8] =
    ["dlg/M001/009", "dlg/M001/010", "dlg/M001/013", "dlg/M001/014", "dlg/M001/015", "dlg/M001/016", "dlg/M001/017", "dlg/M001/018"];
pub const BOX: &str = "{box}";
pub const LIMIT: usize = 32 * 1024 * 1024;

pub fn sample() -> IndexMap<String, String> {
    IndexMap::from([
        (KEYS[0].to_owned(), "男：你怎麼了？{box}是你想來看歌劇的。".to_owned()),
        (KEYS[3].to_owned(), "男：走吧，該進去了。".to_owned()),
    ])
}

fn digest(text: &str) -> String {
    Sha256::digest(text.as_bytes()).iter().map(|b| format!("{b:02x}")).collect()
}

/// `key -> decoded English` for the lines on this USA disc, checked against the catalog.
pub fn read_original(path: &Path) -> Result<IndexMap<String, String>> {
    let mut game = Game::open(path)?;
    let disc = if game.has_root_file("SLUS_006.62")? {
        "1"
    } else if game.has_root_file("SLUS_006.68")? {
        "2"
    } else {
        return Err(Error::value("Choose a Parasite Eve USA Disc 1 or Disc 2 image."));
    };
    let catalog = catalog::load();
    let mut originals = IndexMap::new();
    for bank in &catalog.banks {
        let Some(&number) = bank.locations.get(disc) else {
            continue;
        };
        let missing = || Error::value(format!("Room M{number:03} is missing its dialogue bank."));
        let data = game.room_data(number)?.ok_or_else(missing)?;
        let slot = *bank_slots(&data)?.get(&1).ok_or_else(missing)?;
        let raw = data.get(slot..).unwrap_or_default();
        let (pages, used) = gametext::pages(raw);
        if format!("{:08x}", crc32fast::hash(&raw[..used])) != bank.crc32 {
            return Err(Error::value(format!("Disc {disc}, room M{number:03} does not match the US dialogue catalog.")));
        }
        for (page_number, (page, _)) in pages {
            let key = format!("dlg/{}/{page_number:03}", bank.key);
            if let Some(row) = catalog.rows.get(&key) {
                let original = gametext::decode(&page);
                if english_hash(&original) != row.english_hash {
                    return Err(Error::value(format!("{key}: the disc text does not match the catalog.")));
                }
                originals.insert(key, original);
            }
        }
    }
    Ok(originals)
}

pub fn read_small(path: &Path) -> Result<Vec<u8>> {
    let mut data = Vec::new();
    std::fs::File::open(path)?.take(LIMIT as u64 + 1).read_to_end(&mut data)?;
    if data.len() > LIMIT {
        return Err(Error::value("This file exceeds the editor’s 32 MiB limit."));
    }
    Ok(data)
}

pub fn check_text(text: &str) -> Result<()> {
    if text.chars().count() > 4096 || text.contains('\0') {
        return Err(Error::value("Invalid or oversized translation."));
    }
    Ok(())
}

fn read_u32(data: &[u8], at: usize) -> u32 {
    u32::from_le_bytes(data[at..at + 4].try_into().unwrap())
}

fn atlas_from_pack(data: &[u8]) -> Result<Option<Atlas>> {
    if data.len() < 64 || !data.starts_with(b"PELG") {
        return Err(Error::value("The project is missing its playable language pack."));
    }
    let version = read_u32(data, 4);
    if version == 1 {
        return Ok(None);
    }
    if version != 2 || data.len() < 80 {
        return Err(Error::value("Unsupported playable language pack version."));
    }
    let (offset, count, stride, reserved) =
        (read_u32(data, 64) as usize, read_u32(data, 68) as usize, read_u32(data, 72) as usize, read_u32(data, 76));
    if stride != cjk::RECORD
        || reserved != 0
        || !(80..=data.len()).contains(&offset)
        || count == 0
        || count.checked_mul(stride).and_then(|size| size.checked_add(offset)) != Some(data.len())
    {
        return Err(Error::value("Invalid embedded font atlas."));
    }
    let records = data[offset..].to_vec();
    let mut chars = Vec::with_capacity(count);
    for i in 0..count {
        let cp = read_u32(&records, i * stride);
        let ch = char::from_u32(cp).filter(|_| cjk::supported(cp));
        chars.push(ch.ok_or_else(|| Error::value("Invalid embedded font characters."))?);
    }
    if chars.windows(2).any(|pair| pair[0] >= pair[1]) {
        return Err(Error::value("Invalid embedded font characters."));
    }
    let invalid_pixel = (0..count).any(|i| records[i * stride + 4..(i + 1) * stride].iter().any(|p| p & 15 > 2 || p >> 4 > 2));
    if invalid_pixel {
        return Err(Error::value("Invalid embedded glyph pixels."));
    }
    Ok(Some(Atlas::from_records(chars, records)?))
}

pub fn encode_row(text: &str, source: &Row, atlas: Option<&Atlas>) -> Result<(Vec<u8>, Vec<String>)> {
    let structure = source.structure.as_ref().ok_or_else(|| {
        Error::value("Keep the original dialogue for this line because it uses an unsupported game control.")
    })?;
    let parts: Vec<&str> = text.split(BOX).collect();
    if parts.len() > 3 || (parts.len() > 1 && parts.iter().any(|part| strip(part).is_empty())) {
        return Err(Error::value("Use at most two extra boxes and put text in each box."));
    }
    let mut effective = source.clone();
    if parts.len() > 1 {
        if structure.iter().any(|command| command.starts_with(&[0xFB, 0x09])) {
            return Err(Error::value("Extra boxes are unavailable for dialogue choices."));
        }
        let codes = atlas.map(|a| &a.codes);
        let chunks =
            parts.iter().map(|part| gametext::encode(part, true, codes)).collect::<Result<Vec<_>>>()?;
        validation::check(&chunks.concat(), source_structure(source)?)?;
        // Only the editor's box separators add waits. The original script controls still match.
        effective.structure = Some(validation::structure(&chunks.join(&0xF8))?);
    }
    let (raw, errors, warnings) = pack::check_row(&text.replace(BOX, "{wait}"), &effective, atlas)?;
    if !errors.is_empty() {
        return Err(Error::value(errors.join("; ")));
    }
    let raw = raw.ok_or_else(|| Error::value("Invalid or oversized translation."))?;
    Ok((raw, warnings))
}

pub type Previews = HashMap<String, (Vec<u8>, Vec<String>)>;

pub fn is_language_code(code: &str) -> bool {
    !code.is_empty() && code.split('-').all(|part| !part.is_empty() && part.bytes().all(|b| b.is_ascii_alphanumeric()))
}

#[derive(Clone)]
pub struct Project {
    pub source: &'static Source,
    pub language: String,
    pub title: String,
    pub rows: IndexMap<String, String>,
    pub baseline: IndexMap<String, String>,
    pub font: Option<FontSource>,
    pub atlas: Option<Arc<Atlas>>,
    pub needs_review: BTreeSet<String>,
}

impl Project {
    pub fn new(source: &'static Source) -> Self {
        Self {
            source,
            language: "zh-TW".to_owned(),
            title: "Opera pilot".to_owned(),
            rows: IndexMap::new(),
            baseline: IndexMap::new(),
            font: None,
            atlas: None,
            needs_review: BTreeSet::new(),
        }
    }

    pub fn apply_ai_translations(&mut self, changes: &IndexMap<String, String>) {
        for (key, value) in changes {
            self.rows.insert(key.clone(), value.clone());
            self.needs_review.insert(key.clone());
        }
    }

    fn metadata(&self) -> Map<String, Value> {
        let mut meta = Map::new();
        meta.insert("scene".to_owned(), json!(SCENE));
        meta.insert("version".to_owned(), json!(1));
        meta.insert("language".to_owned(), json!(self.language));
        meta.insert("title".to_owned(), json!(self.title));
        meta
    }

    pub fn compile(&self) -> Result<(Vec<u8>, Previews, Option<Arc<Atlas>>)> {
        if !is_language_code(&self.language) || self.language.len() > 15 {
            return Err(Error::value("Enter a language code such as fr or zh-TW, up to 15 characters."));
        }
        if strip(&self.title).is_empty() || self.title.contains('\0') || self.title.len() > 31 {
            return Err(Error::value("Enter a pack name that fits in 31 UTF-8 bytes."));
        }
        let needed: BTreeSet<char> =
            self.rows.values().flat_map(|value| value.chars()).filter(|&ch| cjk::supported(ch as u32)).collect();
        let mut atlas = self.atlas.clone();
        if let Some(font) = &self.font {
            let built = Atlas::build(self.rows.values().map(String::as_str), std::slice::from_ref(font))?;
            atlas = Some(Arc::new(built));
        } else if !needed.is_empty() && !atlas.as_ref().is_some_and(|a| needed.iter().all(|ch| a.codes.contains_key(ch))) {
            return Err(Error::value("Choose a font containing the custom characters used in this project."));
        }
        let mut bank_keys: BTreeSet<&str> = self
            .rows
            .iter()
            .filter(|(_, value)| !value.is_empty())
            .filter_map(|(key, _)| key.split('/').nth(1))
            .collect();
        if bank_keys.is_empty() {
            bank_keys.insert("M001");
        }
        let mut entries: IndexMap<String, Vec<u8>> = catalog::load()
            .banks
            .iter()
            .filter(|bank| bank_keys.contains(bank.key.as_str()))
            .map(|bank| (format!("bank/{}", bank.crc32), bank.key.as_bytes().to_vec()))
            .collect();
        let mut previews = Previews::new();
        let mut sorted: Vec<(&String, &String)> = self.rows.iter().collect();
        sorted.sort();
        for (key, value) in sorted {
            check_text(value)?;
            if value.is_empty() {
                continue;
            }
            let row = self.source.get(key).ok_or_else(|| Error::value(crate::pycompat::repr(key)))?;
            let (raw, warnings) = encode_row(value, row, atlas.as_deref()).map_err(|error| error.for_key(key))?;
            let end = row.end.as_deref().and_then(|end| u8::from_str_radix(end, 16).ok()).unwrap_or(0xFF);
            let mut entry = raw.clone();
            if end != 0xFF {
                entry.push(end);
            }
            entries.insert(key.clone(), entry);
            previews.insert(key.clone(), (raw, warnings));
        }
        let data = pack::write_pack(&self.language, &self.title, &entries, atlas.as_deref())?;
        Ok((data, previews, atlas))
    }

    /// The `project.json` member of an editable project.
    pub fn project_json(&self) -> String {
        let mut meta = self.metadata();
        let rows: Map<String, Value> = self.rows.iter().map(|(k, v)| (k.clone(), json!(v))).collect();
        meta.insert("translations".to_owned(), Value::Object(rows));
        meta.insert("needs_review".to_owned(), json!(self.needs_review.iter().collect::<Vec<_>>()));
        dumps(&Value::Object(meta), JSON_DEFAULT)
    }

    pub fn export_project(&self) -> Result<Vec<u8>> {
        let (pack, _, _) = self.compile()?;
        let now = time::OffsetDateTime::now_local().unwrap_or_else(|_| time::OffsetDateTime::now_utc());
        let mut options = SimpleFileOptions::default().compression_method(zip::CompressionMethod::Deflated);
        if let Ok(stamp) = zip::DateTime::try_from(time::PrimitiveDateTime::new(now.date(), now.time())) {
            options = options.last_modified_time(stamp);
        }
        let mut archive = zip::ZipWriter::new(Cursor::new(Vec::new()));
        archive.start_file("project.json", options)?;
        archive.write_all(self.project_json().as_bytes())?;
        archive.start_file(format!("{}.pelang", self.language), options)?;
        archive.write_all(&pack)?;
        Ok(archive.finish()?.into_inner())
    }

    pub fn import_project(source: &'static Source, data: &[u8]) -> Result<Self> {
        let mut archive = zip::ZipArchive::new(Cursor::new(data))?;
        let oversized = (0..archive.len()).any(|i| archive.by_index_raw(i).map_or(true, |f| f.size() > LIMIT as u64));
        if archive.len() != 2 || oversized {
            return Err(Error::value("Invalid or oversized translation project."));
        }
        let meta_bytes = read_member(&mut archive, "project.json")?;
        let meta: Value = parse_json(&meta_bytes)?;
        check_header(&meta)?;
        let invalid = || Error::value("Invalid project contents.");
        let language = meta.get("language").ok_or_else(|| Error::value("'language'"))?;
        let title = meta.get("title").ok_or_else(|| Error::value("'title'"))?;
        let (Some(language), Some(title)) = (language.as_str(), title.as_str()) else {
            return Err(invalid());
        };
        let pack_name = format!("{language}.pelang");
        if archive.by_name(&pack_name).is_err() {
            return Err(invalid());
        }
        let rows = meta.get("translations").ok_or_else(|| Error::value("'translations'"))?;
        let unknown = || Error::value("This project contains unknown dialogue IDs.");
        let rows = rows.as_object().ok_or_else(unknown)?;
        if rows.keys().any(|key| !source.contains_key(key)) {
            return Err(unknown());
        }
        let mut translations = IndexMap::new();
        for (key, value) in rows {
            let text = value.as_str().ok_or_else(|| Error::value("Invalid or oversized translation."))?;
            check_text(text)?;
            translations.insert(key.clone(), text.to_owned());
        }
        let pack = read_member(&mut archive, &pack_name)?;
        let review_error = || Error::value("Review status refers to a line without a translation.");
        let mut needs_review = BTreeSet::new();
        match meta.get("needs_review") {
            None => {}
            Some(Value::Array(items)) => {
                for item in items {
                    let key = item.as_str().ok_or_else(review_error)?;
                    if strip(translations.get(key).map_or("", String::as_str)).is_empty() {
                        return Err(review_error());
                    }
                    needs_review.insert(key.to_owned());
                }
            }
            Some(_) => return Err(review_error()),
        }
        let result = Self {
            source,
            language: language.to_owned(),
            title: title.to_owned(),
            baseline: translations.clone(),
            rows: translations,
            font: None,
            atlas: atlas_from_pack(&pack)?.map(Arc::new),
            needs_review,
        };
        if result.compile()?.0 != pack {
            return Err(Error::value("The editable translations and playable pack do not match."));
        }
        Ok(result)
    }

    pub fn export_fixes(&self) -> Result<Vec<u8>> {
        let empty = String::new();
        let mut changes = Vec::new();
        for key in self.source.keys() {
            let (now, was) = (self.rows.get(key).unwrap_or(&empty), self.baseline.get(key).unwrap_or(&empty));
            if now != was {
                let mut change = Map::new();
                change.insert("key".to_owned(), json!(key));
                change.insert("before".to_owned(), json!(digest(was)));
                change.insert("text".to_owned(), json!(now));
                changes.push(Value::Object(change));
            }
        }
        if changes.is_empty() {
            return Err(Error::value("No edits since this project was opened."));
        }
        let mut meta = self.metadata();
        meta.insert("changes".to_owned(), Value::Array(changes));
        Ok(dumps(&Value::Object(meta), JSON_INDENTED).into_bytes())
    }

    pub fn import_fixes(&mut self, data: &[u8]) -> Result<usize> {
        let meta = parse_json(data)?;
        check_header(&meta)?;
        let language = meta.get("language").ok_or_else(|| Error::value("'language'"))?;
        if language.as_str() != Some(self.language.as_str()) {
            let shown = language.as_str().map_or_else(|| language.to_string(), str::to_owned);
            return Err(Error::value(format!("These fixes are for {shown}. Open that language first.")));
        }
        let changes = meta.get("changes").ok_or_else(|| Error::value("'changes'"))?;
        let changes = changes.as_array().ok_or_else(|| Error::value("Invalid fixes file."))?;
        let mut pending: IndexMap<String, String> = IndexMap::new();
        let mut conflicts = Vec::new();
        for change in changes {
            let member = |name: &str| change.get(name).ok_or_else(|| Error::value(format!("'{name}'")));
            let key = member("key")?;
            let text = member("text")?.as_str().ok_or_else(|| Error::value("Invalid or oversized translation."))?;
            check_text(text)?;
            let key = key.as_str().filter(|key| self.source.contains_key(*key) && !pending.contains_key(*key));
            let key = key.ok_or_else(|| Error::value("Unknown or duplicate dialogue key in fixes."))?;
            let current = self.rows.get(key).map_or("", String::as_str);
            if current != text && member("before")?.as_str() != Some(digest(current).as_str()) {
                conflicts.push(key.to_owned());
            }
            pending.insert(key.to_owned(), text.to_owned());
        }
        if !conflicts.is_empty() {
            return Err(Error::value(format!(
                "Local edits conflict with these lines. No fixes were applied.\n{}",
                conflicts.join("\n")
            )));
        }
        for (key, value) in &pending {
            self.rows.insert(key.clone(), value.clone());
            if strip(value).is_empty() {
                self.needs_review.remove(key);
            }
        }
        Ok(pending.len())
    }
}

fn read_member<R: Read + std::io::Seek>(archive: &mut zip::ZipArchive<R>, name: &str) -> Result<Vec<u8>> {
    let member = archive
        .by_name(name)
        .map_err(|_| Error::value(format!("\"There is no item named '{name}' in the archive\"")))?;
    let mut data = Vec::new();
    member.take(LIMIT as u64 + 1).read_to_end(&mut data)?;
    if data.len() > LIMIT {
        return Err(Error::value("Invalid or oversized translation project."));
    }
    Ok(data)
}

fn parse_json(data: &[u8]) -> Result<Value> {
    let text = crate::pycompat::decode_utf8_sig(data)?;
    Ok(serde_json::from_str(&text)?)
}

fn check_header(meta: &Value) -> Result<()> {
    let version_ok = meta.get("version").is_some_and(|v| v.as_f64() == Some(1.0) || v.as_bool() == Some(true));
    let scene = meta.get("scene").and_then(Value::as_str);
    if !meta.is_object() || !version_ok || !matches!(scene, Some(SCENE | LEGACY_SCENE)) {
        return Err(Error::value("This file belongs to a different editor version or scene."));
    }
    Ok(())
}

pub fn load_source() -> &'static Source {
    &catalog::load().rows
}
