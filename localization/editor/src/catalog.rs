//! The bundled dialogue catalog: each line's control structure and layout rules without its text,
//! the message banks, and the named scene views.
use std::collections::HashMap;
use std::sync::OnceLock;

use indexmap::IndexMap;
use serde_json::Value;

use crate::error::{Error, Result};

pub const CATALOG_JSON: &str = include_str!("../catalog.json");

#[derive(Debug, Clone)]
pub struct Row {
    /// `None`: unsupported source controls, so the line keeps the game's text.
    pub structure: Option<Vec<Vec<u8>>>,
    pub indent: (bool, bool),
    pub english_hash: String,
    pub end: Option<String>,
    pub boxes: Vec<String>,
    pub boxes_possible: Vec<String>,
    pub boxes_complete: bool,
}

#[derive(Debug, Clone)]
pub struct Bank {
    pub key: String,
    pub crc32: String,
    pub locations: HashMap<String, usize>,
}

#[derive(Debug, Clone)]
pub struct Scene {
    pub label: String,
    pub keys: Vec<String>,
    pub area: Option<String>,
    pub shared: bool,
}

pub type Source = IndexMap<String, Row>;

pub struct Catalog {
    pub rows: Source,
    pub banks: Vec<Bank>,
    pub scenes: IndexMap<String, Scene>,
}

fn strings(value: Option<&Value>) -> Vec<String> {
    value
        .and_then(Value::as_array)
        .map(|items| items.iter().filter_map(Value::as_str).map(str::to_owned).collect())
        .unwrap_or_default()
}

pub fn from_hex(text: &str) -> Option<Vec<u8>> {
    if text.len() % 2 != 0 {
        return None;
    }
    (0..text.len()).step_by(2).map(|at| u8::from_str_radix(text.get(at..at + 2)?, 16).ok()).collect()
}

fn parse(text: &str) -> Result<Catalog> {
    let data: Value = serde_json::from_str(text)?;
    let invalid = |what: &str| Error::value(format!("catalog.json: invalid {what}"));
    if data["format"] != "pelang-manifest" || data["version"] != 1 {
        return Err(Error::value("catalog.json: not a version 1 pelang manifest"));
    }
    let mut rows = Source::new();
    for (key, entry) in data["entries"].as_object().ok_or_else(|| invalid("entries"))? {
        let structure = match entry.get("structure") {
            Some(Value::Array(items)) => Some(
                items
                    .iter()
                    .map(|c| c.as_str().and_then(from_hex))
                    .collect::<Option<Vec<_>>>()
                    .ok_or_else(|| invalid("structure"))?,
            ),
            _ => None,
        };
        let flag = |n: usize| entry["indent"][n].as_bool().unwrap_or(false);
        rows.insert(key.clone(), Row {
            structure,
            indent: (flag(0), flag(1)),
            english_hash: entry["english_hash"].as_str().ok_or_else(|| invalid("english_hash"))?.to_owned(),
            end: entry.get("end").and_then(Value::as_str).map(str::to_owned),
            boxes: strings(entry.get("boxes")),
            boxes_possible: strings(entry.get("boxes_possible")),
            boxes_complete: entry.get("boxes_complete").and_then(Value::as_bool).unwrap_or(false),
        });
    }
    let mut banks = Vec::new();
    for bank in data["banks"].as_array().ok_or_else(|| invalid("banks"))? {
        let locations = bank["locations"]
            .as_object()
            .ok_or_else(|| invalid("bank locations"))?
            .iter()
            .filter_map(|(disc, number)| Some((disc.clone(), number.as_u64()? as usize)))
            .collect();
        banks.push(Bank {
            key: bank["key"].as_str().ok_or_else(|| invalid("bank key"))?.to_owned(),
            crc32: bank["crc32"].as_str().ok_or_else(|| invalid("bank crc32"))?.to_owned(),
            locations,
        });
    }
    let mut scenes = IndexMap::new();
    for (scope, scene) in data["scenes"].as_object().ok_or_else(|| invalid("scenes"))? {
        scenes.insert(scope.clone(), Scene {
            label: scene["label"].as_str().ok_or_else(|| invalid("scene label"))?.to_owned(),
            keys: strings(scene.get("keys")),
            area: scene["area"].as_str().map(str::to_owned),
            shared: scene["shared"].as_bool().unwrap_or(false),
        });
    }
    Ok(Catalog { rows, banks, scenes })
}

pub fn load() -> &'static Catalog {
    static CATALOG: OnceLock<Catalog> = OnceLock::new();
    CATALOG.get_or_init(|| parse(CATALOG_JSON).expect("the bundled catalog is valid"))
}

pub fn scope_keys(scope: &str) -> Result<Vec<String>> {
    let catalog = load();
    if scope == "all" {
        return Ok(catalog.rows.keys().cloned().collect());
    }
    let scene = catalog.scenes.get(scope).ok_or_else(|| Error::value("The CSV has an unknown dialogue scope."))?;
    Ok(scene.keys.clone())
}

pub fn english_hash(text: &str) -> String {
    use sha1::{Digest, Sha1};
    let hash = Sha1::digest(text.as_bytes());
    hash.iter().take(6).map(|b| format!("{b:02x}")).collect()
}

pub fn source_structure(source: &Row) -> Result<&[Vec<u8>]> {
    source
        .structure
        .as_deref()
        .ok_or_else(|| Error::value("the source has unsupported control bytes: keep the game's text"))
}

/// A stand-in for the English bytes with the same indent style: what the wrapper reads the
/// indent convention from.
pub fn english_shape(source: &Row) -> Vec<u8> {
    let mut shape = Vec::new();
    if source.indent.0 {
        shape.extend([0xF7, 0x0F]);
    }
    if source.indent.1 {
        shape.extend([0xF8, 0x0F]);
    }
    shape
}
