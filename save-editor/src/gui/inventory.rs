//! Inventory screen.
use super::*;

impl SaveEditorApp {
    // =========================================================================
    // TAB 1: INVENTORY & LOADOUT (Right Inspector SidePanel + Central Bag Grid)
    // =========================================================================
    pub(super) fn show_tab_inventory(&mut self, ctx: &egui::Context) {
        egui::SidePanel::right("inventory_inspector_panel")
            .resizable(true)
            .default_width(470.0)
            .min_width(370.0)
            .max_width(640.0)
            .frame(
                egui::Frame::NONE
                    .fill(BG_DARKEST)
                    .inner_margin(Margin::symmetric(14, 12))
                    .stroke(Stroke::new(1.0_f32, BORDER_SUBTLE)),
            )
            .show(ctx, |ui| {
                let save = self.container.as_mut().unwrap().slots[self.selected_slot]
                    .as_mut()
                    .unwrap();
                let idx = self.selected_inv_slot.min(49);
                let val = save.inventory_items[idx];
                let (icon, title, sub, accent) = slot_value_icon_and_title(save, val);

                // Inspector Header
                ui.horizontal_wrapped(|ui| {
                    ui.label(RichText::new(icon).size(20.0));
                    ui.label(
                        RichText::new(format!("Slot #{:02} — {}", idx + 1, title))
                            .size(15.5)
                            .strong()
                            .color(Color32::WHITE),
                    );
                    if save.equipped_weapon_slot == idx as i8 {
                        badge(
                            ui,
                            "EQUIPPED WEAPON",
                            Color32::from_rgb(24, 65, 42),
                            ACCENT_GREEN,
                        );
                    }
                    if save.equipped_armor_slot == idx as i8 {
                        badge(
                            ui,
                            "EQUIPPED ARMOR",
                            Color32::from_rgb(24, 52, 68),
                            ACCENT_CYAN,
                        );
                    }
                });
                render_stack_controls(ui, save, 0, idx);
                ui.label(RichText::new(sub).color(accent).size(12.0));

                ui.add_space(4.0);
                ui.horizontal_wrapped(|ui| {
                    if (0x100..0x180).contains(&val) {
                        let eq_idx = (val - 0x100) as usize;
                        let is_armor = save.equipment[eq_idx].is_armor();
                        if !is_armor {
                            let is_eq = save.equipped_weapon_slot == idx as i8;
                            if ui
                                .add_enabled(!is_eq, egui::Button::new("🔫 Equip Weapon"))
                                .clicked()
                            {
                                save.equipped_weapon_slot = idx as i8;
                            }
                        } else {
                            let is_eq = save.equipped_armor_slot == idx as i8;
                            if ui
                                .add_enabled(!is_eq, egui::Button::new("🛡 Equip Armor"))
                                .clicked()
                            {
                                save.equipped_armor_slot = idx as i8;
                            }
                        }

                        if ui
                            .button(if self.inv_replace_mode {
                                "🔧 Edit Gear Stats & Mods"
                            } else {
                                "⇄ Replace Item in Slot"
                            })
                            .clicked()
                        {
                            self.inv_replace_mode = !self.inv_replace_mode;
                        }

                        if ui.button("🏛 Move to Wayne").clicked() {
                            if let Err(e) = save.move_inventory_slot(0, idx, 2) {
                                ui.colored_label(egui::Color32::LIGHT_RED, e.to_string());
                            }
                        }
                    }

                    if val != 0 && ui.button("🗑 Empty Slot").clicked() {
                        if let Err(e) = save.replace_inventory_slot(0, idx, 0) {
                            ui.colored_label(egui::Color32::LIGHT_RED, e.to_string());
                        }
                        self.inv_replace_mode = false;
                    }
                });

                ui.separator();

                if (0x100..0x180).contains(&val) && !self.inv_replace_mode {
                    let eq_idx = (val - 0x100) as usize;
                    egui::ScrollArea::vertical()
                        .id_salt("inv_embedded_gear_scroll")
                        .auto_shrink([false, false])
                        .show(ui, |ui| {
                            render_equipment_record_editor(ui, &mut save.equipment[eq_idx], idx);
                        });
                } else {
                    render_catalog_picker_for_slot(
                        ui,
                        save,
                        idx,
                        false,
                        &mut self.picker_category,
                        &mut self.picker_search,
                        &mut self.inv_replace_mode,
                    );
                }
            });

        egui::CentralPanel::default()
            .frame(
                egui::Frame::NONE
                    .fill(BG_PANEL)
                    .inner_margin(Margin::symmetric(14, 10)),
            )
            .show(ctx, |ui| {
                let save = self.container.as_mut().unwrap().slots[self.selected_slot]
                    .as_mut()
                    .unwrap();

                // Ammo Reserves Bar
                egui::Frame::NONE
                    .fill(BG_DARKEST)
                    .stroke(Stroke::new(1.0_f32, BORDER_SUBTLE))
                    .corner_radius(CornerRadius::same(7))
                    .inner_margin(Margin::symmetric(10, 7))
                    .show(ui, |ui| {
                        ui.horizontal_wrapped(|ui| {
                            ui.label(
                                RichText::new("AMMO RESERVES:")
                                    .color(TEXT_MUTED)
                                    .small()
                                    .strong(),
                            );
                            ui.label("弹 9mm:");
                            ui.add(
                                egui::DragValue::new(&mut save.category_counts[0]).range(0..=9999),
                            );
                            ui.separator();
                            ui.label("🚀 Rockets:");
                            ui.add(
                                egui::DragValue::new(&mut save.category_counts[1]).range(0..=9999),
                            );
                            ui.separator();
                            ui.label("🧬 DNA:");
                            ui.add(
                                egui::DragValue::new(&mut save.category_counts[2]).range(0..=9999),
                            );
                            if ui.small_button("Max 999").clicked() {
                                save.apply_cheat_max_ammo();
                            }
                            ui.separator();
                            ui.label("Base Bag Slots:");
                            ui.add(
                                egui::DragValue::new(&mut save.inventory_slot_count)
                                    .range(10..=crate::disc::MAX_STAT_BAG_SLOTS),
                            )
                            .on_hover_text(BAG_SIZE_HELP);
                            ui.checkbox(&mut self.show_locked_inv_slots, "Show Locked (..50)");
                            ui.separator();
                            if ui.button("⇅ Sort Bag").on_hover_text(SORT_HELP).clicked() {
                                save.sort_aya_bag();
                            }
                        });
                    });

                ui.add_space(6.0);

                let max_visible = if self.show_locked_inv_slots {
                    50
                } else {
                    (save.inventory_slot_count as usize).clamp(10, 50)
                };

                egui::ScrollArea::vertical()
                    .id_salt("inv_bag_cards_scroll")
                    .auto_shrink([false, false])
                    .show(ui, |ui| {
                        let use_two_cols = ui.available_width() >= 460.0;
                        let step = if use_two_cols { 2 } else { 1 };

                        for row_start in (0..max_visible).step_by(step) {
                            ui.columns(step, |cols| {
                                for offset in 0..step {
                                    let idx = row_start + offset;
                                    if idx >= max_visible {
                                        continue;
                                    }
                                    let val = save.inventory_items[idx];
                                    let is_sel = self.selected_inv_slot == idx;
                                    let is_eq_wpn = save.equipped_weapon_slot == idx as i8;
                                    let is_eq_arm = save.equipped_armor_slot == idx as i8;
                                    let is_locked = idx >= save.inventory_slot_count as usize;

                                    let (icon, title, sub, accent) =
                                        slot_value_icon_and_title(save, val);
                                    let border_override = if is_eq_wpn {
                                        Some(ACCENT_GREEN)
                                    } else if is_eq_arm {
                                        Some(ACCENT_CYAN)
                                    } else {
                                        None
                                    };

                                    let resp = interactive_card(
                                        &mut cols[offset],
                                        ("aya_inv_slot_card", idx),
                                        is_sel,
                                        border_override,
                                        |ui| {
                                            ui.horizontal(|ui| {
                                                ui.label(
                                                    RichText::new(format!("#{:02}", idx + 1))
                                                        .color(if is_locked {
                                                            Color32::from_rgb(100, 90, 90)
                                                        } else {
                                                            TEXT_MUTED
                                                        })
                                                        .monospace()
                                                        .small(),
                                                );
                                                if is_eq_wpn {
                                                    badge(
                                                        ui,
                                                        "EQUIPPED WPN",
                                                        Color32::from_rgb(24, 65, 42),
                                                        ACCENT_GREEN,
                                                    );
                                                } else if is_eq_arm {
                                                    badge(
                                                        ui,
                                                        "EQUIPPED ARM",
                                                        Color32::from_rgb(24, 52, 68),
                                                        ACCENT_CYAN,
                                                    );
                                                }
                                                ui.label(RichText::new(icon).size(13.5));
                                                ui.label(
                                                    RichText::new(&title)
                                                        .color(if val <= 0 {
                                                            TEXT_MUTED
                                                        } else {
                                                            Color32::WHITE
                                                        })
                                                        .strong()
                                                        .size(13.0),
                                                );
                                            });
                                            ui.label(
                                                RichText::new(sub)
                                                    .color(accent.gamma_multiply(0.85))
                                                    .size(11.0),
                                            );
                                        },
                                    );

                                    if resp.clicked() {
                                        self.selected_inv_slot = idx;
                                        self.inv_replace_mode = false;
                                    }
                                }
                            });
                            ui.add_space(2.0);
                        }
                    });
            });
    }
}
