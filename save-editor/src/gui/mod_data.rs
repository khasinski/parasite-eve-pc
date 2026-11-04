//! The mod page uses the format module; no offsets or checksums live in the UI.
use crate::{
    mod_save::{self, ModData},
    save::PeSaveSlot,
    tables::item_metadata,
};
use eframe::egui;

pub(super) fn show(ui: &mut egui::Ui, save: &mut PeSaveSlot) {
    ui.heading("Mod Save Data");
    if let Some(companion) = &save.companion {
        ui.label("Companion data is loaded and will travel with this save.");
        match companion.sections() {
            Ok(sections) => {
                for section in sections {
                    ui.label(format!(
                        "{} (version {}, {} bytes)",
                        section.key, section.version, section.size
                    ));
                }
            }
            Err(e) => {
                ui.colored_label(egui::Color32::YELLOW, e.to_string());
            }
        }
    }
    ui.label(
        "Unknown records are preserved. Normal save edits leave the entire mod tail unchanged.",
    );
    ui.label("Pocketed keys are separate from the bag. Changing them does not update story flags.");
    let parsed = match ModData::inspect(&save.raw_bytes) {
        Ok(v) => v,
        Err(e) => {
            ui.label(e.to_string());
            return;
        }
    };
    ui.label(&parsed.format);
    for warning in &parsed.warnings {
        ui.colored_label(egui::Color32::YELLOW, warning);
    }
    egui::Grid::new("mod_records").striped(true).show(ui, |ui| {
        ui.strong("Record");
        ui.strong("Schema");
        ui.strong("Bytes");
        ui.end_row();
        for r in &parsed.records {
            ui.label(r.name());
            ui.label(r.version.to_string());
            ui.label(r.data.len().to_string());
            ui.end_row();
        }
    });
    if parsed.records.is_empty() {
        ui.label("No recognized records. Opaque tail bytes are retained.");
    }
    ui.separator();
    ui.heading("Junk counter");
    ui.label("The saved economy balance, separate from Junk items in the bag. Changing this does not consume or create bag items.");
    match crate::junk_counter::read(&save.raw_bytes) {
        Err(e) => {
            ui.colored_label(egui::Color32::YELLOW, e.to_string());
        }
        Ok(value) => {
            let mut junk = value.unwrap_or(0);
            if value.is_none() {
                ui.label("No saved economy counter yet. Changing the value initializes it with Normal difficulty.");
            }
            let before = junk;
            ui.add_enabled_ui(parsed.editable, |ui| {
                ui.horizontal(|ui| {
                    ui.label("Junk");
                    ui.add(egui::DragValue::new(&mut junk).range(0..=crate::junk_counter::MAX));
                });
            });
            if before != junk {
                result(ui, crate::junk_counter::write(&mut save.raw_bytes, junk));
            }
        }
    }
    ui.separator();
    ui.heading("Key item pocket");
    match parsed.keys() {
        Err(e) => {
            ui.colored_label(egui::Color32::YELLOW, e.to_string());
        }
        Ok(keys) => {
            let mut keys = keys.unwrap_or_default();
            let before = keys.clone();
            ui.add_enabled_ui(parsed.editable, |ui| {
                egui::CollapsingHeader::new(format!(
                    "Pocketed keys ({}/32) — expand to edit",
                    keys.len()
                ))
                .show(ui, |ui| {
                    for k in (0x38..=0x3e).chain(0xc8..=0xdc) {
                        let mut checked = keys.contains(&k);
                        let label =
                            format!("{} (0x{:02X})", item_metadata(k as u8).display_name, k);
                        if ui.checkbox(&mut checked, label).changed() {
                            if checked && keys.len() < 32 {
                                keys.push(k);
                            }
                            if !checked {
                                keys.retain(|v| *v != k);
                            }
                        }
                    }
                });
            });
            if before != keys {
                result(ui, mod_save::set_keys(&mut save.raw_bytes, &keys));
            }
        }
    }
    ui.separator();
    ui.heading("Remembered bag sorting");
    match parsed.sort() {
        Err(e) => {
            ui.colored_label(egui::Color32::YELLOW, e.to_string());
        }
        Ok(sort) => {
            let mut s = sort.unwrap_or_default();
            let before = s.clone();
            ui.add_enabled_ui(parsed.editable, |ui| {
                let mut auto = s.auto == 1;
                ui.checkbox(&mut auto, "Auto sort");
                s.auto = auto as u32;
                egui::ComboBox::from_id_salt("mod_rank")
                    .selected_text(format!(
                        "Rank preset {}",
                        [0x800923f8, 0x80092410, 0x80092428, 0x80092440]
                            .iter()
                            .position(|v| *v == s.rank)
                            .unwrap_or(0)
                            + 1
                    ))
                    .show_ui(ui, |ui| {
                        for (i, v) in [0x800923f8, 0x80092410, 0x80092428, 0x80092440]
                            .into_iter()
                            .enumerate()
                        {
                            ui.selectable_value(&mut s.rank, v, format!("Rank preset {}", i + 1));
                        }
                    });
                egui::ComboBox::from_id_salt("mod_type")
                    .selected_text(format!(
                        "Category preset {}",
                        if s.category == 0x80092458 { 1 } else { 2 }
                    ))
                    .show_ui(ui, |ui| {
                        for (i, v) in [0x80092458, 0x80092468].into_iter().enumerate() {
                            ui.selectable_value(
                                &mut s.category,
                                v,
                                format!("Category preset {}", i + 1),
                            );
                        }
                    });
                egui::ComboBox::from_id_salt("mod_weapon_stat")
                    .selected_text(["Attack", "Range", "Capacity"][s.weapon as usize])
                    .show_ui(ui, |ui| {
                        for (i, label) in ["Attack", "Range", "Capacity"].iter().enumerate() {
                            ui.selectable_value(&mut s.weapon, i as u32, *label);
                        }
                    });
                egui::ComboBox::from_id_salt("mod_armor_stat")
                    .selected_text(["Defense", "PE energy", "Critical"][s.armor as usize])
                    .show_ui(ui, |ui| {
                        for (i, label) in ["Defense", "PE energy", "Critical"].iter().enumerate() {
                            ui.selectable_value(&mut s.armor, i as u32, *label);
                        }
                    });
            });
            if before != s {
                result(ui, mod_save::set_sort(&mut save.raw_bytes, &s));
            }
        }
    }
    ui.separator();
    ui.label("Changes take effect when you save the card. PKEY/SORT compatibility blocks are updated with their records. Other economy records and unknown payloads are read-only in this page.");
}
fn result(ui: &mut egui::Ui, r: anyhow::Result<()>) {
    if let Err(e) = r {
        ui.colored_label(egui::Color32::RED, e.to_string());
    }
}
