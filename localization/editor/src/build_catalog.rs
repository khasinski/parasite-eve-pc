//! Generate editor scene metadata from the USA discs without including their text.
use std::collections::{BTreeSet, HashMap};
use std::path::{Path, PathBuf};

use indexmap::IndexMap;
use serde_json::{json, Map, Value};

use crate::disc::{bank_slots, Game};
use crate::error::{Error, Result};
use crate::{gametext, script};

/// Location IDs and long forms are documented in docs/ROOMS.md.
fn area_name(id: u8) -> Option<&'static str> {
    Some(match id {
        1 => "Carnegie Hall",
        2 => "Theater sewers",
        3 => "NYPD 28th Precinct",
        4 => "Central Park",
        5 => "Soho",
        6 => "St. Francis Hospital",
        7 => "Chinatown",
        8 => "Chinatown sewers",
        9 => "Subway",
        10 => "Museum of Natural History",
        11 => "Pier No. 4 Warehouse",
        12 => "Aircraft carrier",
        13 => "Statue of Liberty",
        14 => "Cruiser",
        _ => return None,
    })
}

/// These checkpoints are named in mods/pe_warp/pe_warp.c.
fn checkpoint(room: usize) -> Option<&'static str> {
    Some(match room {
        1 => "Opera arrival",
        2 => "Stage",
        23 => "Eve’s first fight",
        26 => "Sewer entrance",
        34 => "Alligator fight",
        47 => "Press conference",
        52 => "Wayne’s first visit",
        68 => "Zoo",
        _ => return None,
    })
}

fn room_area(pack: &[u8], map_number: usize) -> Result<Option<&'static str>> {
    // Resident header word 9 points to Text_GetMapLocationName's map-to-place table.
    let truncated = || Error::value("the resident pack is truncated");
    let offset = pack.get(36..40).ok_or_else(truncated)?;
    let offset = u32::from_le_bytes(offset.try_into().unwrap()) as usize;
    let id = *pack.get(offset + map_number).ok_or_else(truncated)?;
    if let Some(area) = area_name(id) {
        return Ok(Some(area));
    }
    // The elevator bank is identified in docs/ROOMS.md.
    let chrysler = (288..=318).contains(&map_number) || (382..=424).contains(&map_number) || (434..=437).contains(&map_number);
    Ok(chrysler.then_some("Chrysler Building"))
}

/// The catalog from the source manifest and both discs. `root` is the port's checkout.
pub fn generate(root: &Path, discs: &[PathBuf; 2]) -> Result<Value> {
    let manifest = std::fs::read_to_string(root.join("localization/data/manifest.json"))?;
    let mut source: Map<String, Value> = serde_json::from_str(&manifest)?;
    let entries: Map<String, Value> = source["entries"]
        .as_object()
        .ok_or_else(|| Error::value("manifest.json has no entries"))?
        .iter()
        .filter(|(key, _)| key.starts_with("dlg/"))
        .map(|(key, value)| (key.clone(), value.clone()))
        .collect();
    let bank_key: HashMap<String, String> = source["banks"]
        .as_array()
        .ok_or_else(|| Error::value("manifest.json has no banks"))?
        .iter()
        .filter_map(|bank| Some((bank["crc32"].as_str()?.to_owned(), bank["key"].as_str()?.to_owned())))
        .collect();
    let mut locations: IndexMap<String, Map<String, Value>> = IndexMap::new();
    let mut scenes = Map::new();
    let mut bank_areas: HashMap<String, IndexMap<&'static str, usize>> = HashMap::new();
    let valid = script::opcodes(root)?;
    for (index, path) in discs.iter().enumerate() {
        let disc = index + 1;
        let mut game = Game::open(path)?;
        let serial = ["SLUS_006.62", "SLUS_006.68"][index];
        if !game.has_root_file(serial)? {
            return Err(Error::value(format!("Expected USA Disc {disc}.")));
        }
        let pack = game.resident_pack()?;
        for number in 1..=450usize {
            let Some(data) = game.room_data(number)? else {
                continue;
            };
            let Some(&slot) = bank_slots(&data)?.get(&1) else {
                continue;
            };
            let raw = data.get(slot..).unwrap_or_default();
            let (pages, size) = gametext::pages(raw);
            if size == 0 {
                continue;
            }
            let checksum = format!("{:08x}", crc32fast::hash(&raw[..size]));
            let bank = bank_key.get(&checksum).ok_or_else(|| Error::value(crate::pycompat::repr(&checksum)))?.clone();
            locations.entry(checksum).or_default().entry(disc.to_string()).or_insert(json!(number));
            let (code, complete) = script::walk(script::task_block(&data)?, &valid)?;
            let constants = script::constant_writes(&code);
            let page_ids: BTreeSet<u32> = pages.keys().map(|&id| id as u32).collect();
            let mut selected = BTreeSet::new();
            let mut dynamic = !complete;
            for (page, register) in script::box_pages(&code) {
                match page {
                    None => {
                        let mut any = false;
                        if let Some(values) = constants.get(&register) {
                            for value in values.intersection(&page_ids) {
                                selected.insert(*value);
                                any = true;
                            }
                        }
                        dynamic |= !any;
                    }
                    Some(page) if page_ids.contains(&page) => {
                        selected.insert(page);
                    }
                    Some(_) => {}
                }
            }
            let shared = dynamic || selected.is_empty();
            // A computed selector or a native caller can use the shared bank.
            let chosen = if shared { &page_ids } else { &selected };
            let keys: Vec<String> = chosen.iter().map(|page| format!("dlg/{bank}/{page:03}")).collect();
            if keys.iter().any(|key| !entries.contains_key(key)) {
                return Err(Error::value(format!("Scene M{number:03} disagrees with the dialogue manifest.")));
            }
            let area = room_area(&pack, number)?;
            if let Some(area) = area {
                *bank_areas.entry(bank).or_default().entry(area).or_insert(0) += 1;
            }
            let mut name = area.unwrap_or("Unlabelled scene").to_owned();
            if let Some(checkpoint) = checkpoint(number) {
                name.push_str(&format!(" · {checkpoint}"));
            }
            let suffix = if shared { " (shared dialogue)" } else { "" };
            let mut scene = Map::new();
            scene.insert("label".to_owned(), json!(format!("{name} · M{number:03} · Disc {disc}{suffix}")));
            scene.insert("keys".to_owned(), json!(keys));
            scene.insert("area".to_owned(), json!(area));
            scene.insert("shared".to_owned(), json!(shared));
            scenes.insert(format!("d{disc}-M{number:03}"), Value::Object(scene));
        }
    }
    let covered = locations.len() == bank_key.len() && bank_key.keys().all(|crc| locations.contains_key(crc));
    if !covered {
        return Err(Error::value("Both discs must cover every dialogue bank in the manifest."));
    }
    for bank in source["banks"].as_array_mut().unwrap() {
        let crc = bank["crc32"].as_str().unwrap_or_default().to_owned();
        let key = bank["key"].as_str().unwrap_or_default().to_owned();
        bank["locations"] = Value::Object(locations[&crc].clone());
        let prefix = format!("dlg/{key}/");
        let mut area: Option<(&str, usize)> = None;
        if let Some(areas) = bank_areas.get(&key) {
            for (&name, &count) in areas {
                if area.is_none_or(|(_, best)| count > best) {
                    area = Some((name, count));
                }
            }
        }
        let area = area.map(|(name, _)| name);
        let keys: Vec<&String> = entries.keys().filter(|entry| entry.starts_with(&prefix)).collect();
        let mut scene = Map::new();
        scene.insert("label".to_owned(), json!(format!("{} · Shared bank {key}", area.unwrap_or("Unlabelled dialogue"))));
        scene.insert("area".to_owned(), json!(area));
        scene.insert("shared".to_owned(), json!(true));
        scene.insert("keys".to_owned(), json!(keys));
        scenes.insert(format!("bank-{key}"), Value::Object(scene));
    }
    source["entries"] = Value::Object(entries);
    source["menu_metrics"] = Value::Null;
    let mut all = Map::new();
    let mut opera = Map::new();
    let demo: Vec<String> = [9, 10, 13, 14, 15, 16, 17, 18].iter().map(|n| format!("dlg/M001/{n:03}")).collect();
    opera.insert("label".to_owned(), json!("Carnegie Hall · Opening opera conversation"));
    opera.insert("keys".to_owned(), json!(demo));
    opera.insert("area".to_owned(), json!("Carnegie Hall"));
    opera.insert("shared".to_owned(), json!(false));
    all.insert("opera".to_owned(), Value::Object(opera));
    all.extend(scenes);
    source.insert("scenes".to_owned(), Value::Object(all));
    Ok(Value::Object(source))
}
