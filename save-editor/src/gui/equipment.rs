//! Equipment forms and shared item picker.
use super::*;

pub(super) fn render_equipment_record_editor(
    ui: &mut egui::Ui,
    rec: &mut EquipmentRecord,
    id_salt: usize,
) {
    let is_armor = rec.is_armor();
    let cur_meta = item_metadata(rec.item_id);

    ui.horizontal_wrapped(|ui| {
        ui.label(RichText::new("Base Model:").color(TEXT_MUTED));
        egui::ComboBox::from_id_salt(("eq_model_combo", id_salt, rec.index))
            .width(210.0)
            .selected_text(format!(
                "{} ({})",
                cur_meta.display_name,
                cur_meta.category.label()
            ))
            .show_ui(ui, |ui| {
                for meta in crate::tables::all_item_metadata()
                    .into_iter()
                    .filter(|m| m.category.is_equipment())
                {
                    if ui
                        .selectable_label(
                            rec.item_id == meta.id,
                            format!("{} [{}]", meta.display_name, meta.category.label()),
                        )
                        .clicked()
                    {
                        rec.item_id = meta.id;
                        (rec.icon_id, rec.kind) = EquipmentRecord::icon_and_kind_for(meta.id);
                    }
                }
            });

        if ui.button("✨ Max +999 & 10 Slots").clicked() {
            rec.bonus_stats = [999, 999, 999];
            rec.tail_count = 10;
            rec.tail_capacity = 10;
            rec.set_engraved(true);
            if !is_armor {
                rec.ammo = 999;
            }
        }
    });

    let mut engraved = rec.is_engraved();
    if ui
        .checkbox(
            &mut engraved,
            "★ Engraved with Wayne's Name (Carries over into EX Game)",
        )
        .changed()
    {
        rec.set_engraved(engraved);
    }

    ui.add_space(4.0);

    // Clean 4-column table for the 3 stat axes (never overlaps horizontally!)
    let axes = if is_armor {
        [
            ("🛡 Defense", ACCENT_CYAN),
            ("🧬 P.Energy", ACCENT_GREEN),
            ("⚡ Crit Evade", ACCENT_AMBER),
        ]
    } else {
        [
            ("💥 Attack", ACCENT_GREEN),
            ("🎯 Range", ACCENT_CYAN),
            ("弹 Bullets Cap", ACCENT_AMBER),
        ]
    };

    egui::Frame::NONE
        .fill(BG_CARD)
        .stroke(Stroke::new(1.0_f32, BORDER_SUBTLE))
        .corner_radius(CornerRadius::same(6))
        .inner_margin(Margin::symmetric(10, 8))
        .show(ui, |ui| {
            egui::Grid::new(("eq_stat_axes_grid", id_salt, rec.index))
                .num_columns(4)
                .spacing([18.0, 6.0])
                .show(ui, |ui| {
                    ui.label(
                        RichText::new("Parameter")
                            .color(TEXT_MUTED)
                            .small()
                            .strong(),
                    );
                    ui.label(
                        RichText::new("Base (0..255)")
                            .color(TEXT_MUTED)
                            .small()
                            .strong(),
                    );
                    ui.label(
                        RichText::new("Bonus (+N)")
                            .color(TEXT_MUTED)
                            .small()
                            .strong(),
                    );
                    ui.label(RichText::new("Total").color(TEXT_MUTED).small().strong());
                    ui.end_row();

                    for axis in 0..3 {
                        let (label, color) = axes[axis];
                        let total = rec.base_stats[axis] as i32 + rec.bonus_stats[axis] as i32;
                        ui.label(RichText::new(label).strong());
                        ui.add(egui::DragValue::new(&mut rec.base_stats[axis]).range(0..=255));
                        ui.add(
                            egui::DragValue::new(&mut rec.bonus_stats[axis])
                                .range(-999..=999)
                                .prefix("+"),
                        );
                        ui.label(
                            RichText::new(format!("{}", total))
                                .color(color)
                                .size(14.5)
                                .strong(),
                        );
                        ui.end_row();
                    }
                });

            if !is_armor {
                ui.separator();
                ui.horizontal_wrapped(|ui| {
                    ui.label(RichText::new("Loaded Ammo:").color(TEXT_MUTED));
                    ui.add(egui::DragValue::new(&mut rec.ammo).range(0..=999));
                    let cap =
                        (rec.base_stats[2] as i32 + rec.bonus_stats[2] as i32).clamp(1, 999) as u16;
                    if ui
                        .small_button(format!("Fill Magazine ({})", cap))
                        .clicked()
                    {
                        rec.ammo = cap;
                    }
                });
            }
        });

    ui.add_space(6.0);

    // Modifications Section
    ui.horizontal_wrapped(|ui| {
        ui.label(
            RichText::new("MODIFICATIONS (UP TO 10 SLOTS)")
                .color(TEXT_MUTED)
                .small()
                .strong(),
        );
        ui.separator();
        ui.label("Active Slots:");
        ui.add(egui::Slider::new(&mut rec.tail_count, 0..=10));
    });
    if rec.tail_capacity < rec.tail_count {
        rec.tail_capacity = rec.tail_count;
    }

    let mod_slice = crate::custom_effects::choices(is_armor);
    ui.label(crate::custom_effects::note(is_armor));

    egui::Grid::new(("eq_mods_table_grid", id_salt, rec.index))
        .striped(true)
        .num_columns(3)
        .spacing([10.0, 5.0])
        .show(ui, |ui| {
            for slot_i in 0..10 {
                let is_unlocked = (slot_i as u8) < rec.tail_count;
                let cur_mod_id = rec.mod_id_at(slot_i);
                let cur_mod_meta = mod_metadata(cur_mod_id, is_armor);

                ui.label(
                    RichText::new(format!("Mod #{:02}", slot_i + 1))
                        .color(if is_unlocked {
                            ACCENT_GREEN
                        } else {
                            TEXT_MUTED
                        })
                        .monospace()
                        .small(),
                );

                egui::ComboBox::from_id_salt(("eq_mod_slot_combo", id_salt, rec.index, slot_i))
                    .width(260.0)
                    .selected_text(if cur_mod_id == 0 {
                        RichText::new("— Empty Slot —").color(TEXT_MUTED)
                    } else if cur_mod_meta.code_name == "MOD_UNKNOWN" {
                        RichText::new(format!("Unknown Effect #{} (kept as it is)", cur_mod_id))
                            .color(ACCENT_AMBER)
                    } else {
                        RichText::new(format!(
                            "{} ({})",
                            cur_mod_meta.name, cur_mod_meta.description
                        ))
                        .color(Color32::WHITE)
                    })
                    .show_ui(ui, |ui| {
                        for m in mod_slice.iter() {
                            if ui
                                .selectable_label(
                                    cur_mod_id == m.id,
                                    format!("{} — {}", m.name, m.description),
                                )
                                .clicked()
                            {
                                rec.set_mod_at(slot_i, m.id);
                                if (slot_i as u8) >= rec.tail_count && m.id != 0 {
                                    rec.tail_count = (slot_i as u8) + 1;
                                    rec.tail_capacity = rec.tail_capacity.max(rec.tail_count);
                                }
                            }
                        }
                    });

                if cur_mod_id != 0 {
                    if ui.small_button("✖").on_hover_text("Remove mod").clicked() {
                        rec.set_mod_at(slot_i, 0);
                    }
                } else {
                    ui.label("");
                }
                ui.end_row();
            }
        });
}

pub(super) fn render_catalog_picker_for_slot(
    ui: &mut egui::Ui,
    save: &mut PeSaveSlot,
    target_slot_idx: usize,
    is_wayne_regular_storage: bool,
    picker_cat: &mut ItemPickerFilter,
    picker_search: &mut String,
    replace_mode: &mut bool,
) {
    ui.label(
        RichText::new("CLICK ANY ITEM BELOW TO PLACE IN THIS SLOT")
            .color(TEXT_MUTED)
            .small()
            .strong(),
    );

    ui.horizontal_wrapped(|ui| {
        for cat in [
            ItemPickerFilter::All,
            ItemPickerFilter::Consumables,
            ItemPickerFilter::Ammo,
            ItemPickerFilter::UpgradesAndTools,
            ItemPickerFilter::Weapons,
            ItemPickerFilter::Armor,
            ItemPickerFilter::KeysAndCards,
        ] {
            if ui
                .selectable_label(*picker_cat == cat, cat.label())
                .clicked()
            {
                *picker_cat = cat;
            }
        }
    });

    ui.horizontal(|ui| {
        ui.label("🔍 Search:");
        ui.text_edit_singleline(picker_search);
        if !picker_search.is_empty() && ui.small_button("Clear").clicked() {
            picker_search.clear();
        }
    });
    ui.add_space(4.0);

    let needle = picker_search.to_lowercase();

    egui::ScrollArea::vertical()
        .id_salt(("catalog_picker_scroll", is_wayne_regular_storage))
        .auto_shrink([false, false])
        .show(ui, |ui| {
            for meta in crate::tables::all_item_metadata().into_iter().skip(1) {
                if !picker_cat.matches(meta.category) {
                    continue;
                }
                if !needle.is_empty()
                    && !meta.display_name.to_lowercase().contains(&needle)
                    && !meta.description.to_lowercase().contains(&needle)
                    && !meta.category.label().to_lowercase().contains(&needle)
                {
                    continue;
                }

                let resp = interactive_card(
                    ui,
                    ("catalog_item_row", meta.id, is_wayne_regular_storage),
                    false,
                    None,
                    |ui| {
                        ui.horizontal(|ui| {
                            ui.label(
                                RichText::new(meta.display_name)
                                    .strong()
                                    .color(Color32::WHITE),
                            );
                            badge(ui, meta.category.label(), BG_DARKEST, ACCENT_GREEN);
                        });
                        ui.label(RichText::new(meta.description).color(TEXT_MUTED).small());
                    },
                );

                if resp.clicked() {
                    let assigned_val = if meta.category.is_equipment() {
                        save.allocate_equipment_from_item(meta.id)
                            .unwrap_or(meta.id as i16)
                    } else {
                        meta.id as i16
                    };

                    if is_wayne_regular_storage {
                        if target_slot_idx < save.storage.len() {
                            if let Err(e) =
                                save.replace_inventory_slot(1, target_slot_idx, assigned_val)
                            {
                                ui.colored_label(egui::Color32::LIGHT_RED, e.to_string());
                            }
                        }
                    } else if target_slot_idx < save.inventory_items.len() {
                        if let Err(e) =
                            save.replace_inventory_slot(0, target_slot_idx, assigned_val)
                        {
                            ui.colored_label(egui::Color32::LIGHT_RED, e.to_string());
                        }
                    }
                    *replace_mode = false;
                }
                ui.add_space(2.0);
            }
        });
}
