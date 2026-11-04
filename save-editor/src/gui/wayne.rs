//! Wayne screen.
use super::*;

impl SaveEditorApp {
    // =========================================================================
    // TAB 5: WAYNE'S STORAGE (Right Inspector SidePanel + Central Vault List)
    // =========================================================================
    pub(super) fn show_tab_wayne(&mut self, ctx: &egui::Context) {
        egui::SidePanel::right("wayne_inspector_panel")
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

                match self.wayne_subtab {
                    WayneSubTab::EquipmentVault => {
                        let idx = self.selected_wayne_special_slot.min(81);
                        render_stack_controls(ui,save,2,idx);
                        let val = save.special_storage[idx];
                        let (_, title, _, _) = slot_value_icon_and_title(save, val);

                        ui.horizontal_wrapped(|ui| {
                            ui.heading(format!("Wayne Vault #{:02} — {}", idx + 1, title));
                            if val != 0 && ui.button("🎒 Move to Aya's Bag").clicked() {
                                if let Err(e)=save.move_inventory_slot(2,idx,0) { ui.colored_label(egui::Color32::LIGHT_RED,e.to_string()); }
                            }
                            if val != 0 && ui.button("🗑 Clear").clicked() {
                                if let Err(e)=save.replace_inventory_slot(2,idx,0) { ui.colored_label(egui::Color32::LIGHT_RED,e.to_string()); }
                            }
                        });
                        ui.separator();

                        if (0x100..0x180).contains(&val) {
                            let eq_idx = (val - 0x100) as usize;
                            egui::ScrollArea::vertical()
                                .id_salt("wayne_spec_eq_editor")
                                .auto_shrink([false, false])
                                .show(ui, |ui| {
                                    render_equipment_record_editor(
                                        ui,
                                        &mut save.equipment[eq_idx],
                                        2000 + idx,
                                    );
                                });
                        } else {
                            ui.label(
                                RichText::new("Click any weapon or armor below to store it in this Wayne Vault slot:")
                                    .color(TEXT_MUTED),
                            );
                            egui::ScrollArea::vertical()
                                .id_salt("wayne_spec_picker")
                                .auto_shrink([false, false])
                                .show(ui, |ui| {
                                    for meta in crate::tables::all_item_metadata()
                                        .into_iter()
                                        .filter(|m| m.category.is_equipment())
                                    {
                                        if interactive_card(
                                            ui,
                                            ("wayne_new_eq_pick", meta.id),
                                            false,
                                            None,
                                            |ui| {
                                                ui.horizontal(|ui| {
                                                    ui.label(
                                                        RichText::new(meta.display_name).strong(),
                                                    );
                                                    badge(
                                                        ui,
                                                        meta.category.label(),
                                                        BG_DARKEST,
                                                        ACCENT_GREEN,
                                                    );
                                                    ui.label(
                                                        RichText::new(meta.description)
                                                            .color(TEXT_MUTED)
                                                            .small(),
                                                    );
                                                });
                                            },
                                        )
                                        .clicked()
                                        {
                                            if let Some(eq_val) =
                                                save.allocate_equipment_from_item(meta.id)
                                            {
                                                if let Err(e)=save.replace_inventory_slot(2,idx,eq_val) { ui.colored_label(egui::Color32::LIGHT_RED,e.to_string()); }
                                            }
                                        }
                                        ui.add_space(2.0);
                                    }
                                });
                        }
                    }
                    WayneSubTab::RegularItems => {
                        let idx = self.selected_wayne_regular_slot.min(99);
                        render_stack_controls(ui,save,1,idx);
                        let val = save.storage[idx];
                        let (_, title, _, _) = slot_value_icon_and_title(save, val);

                        ui.horizontal_wrapped(|ui| {
                            ui.heading(format!("Wayne Item #{:02} — {}", idx + 1, title));
                            if val != 0 && ui.button("🎒 Move to Aya's Bag").clicked() {
                                if let Err(e)=save.move_inventory_slot(1,idx,0) { ui.colored_label(egui::Color32::LIGHT_RED,e.to_string()); }
                            }
                            if val != 0 && ui.button("🗑 Clear").clicked() {
                                if let Err(e)=save.replace_inventory_slot(1,idx,0) { ui.colored_label(egui::Color32::LIGHT_RED,e.to_string()); }
                            }
                        });
                        ui.separator();
                        let mut dummy_replace = false;
                        render_catalog_picker_for_slot(
                            ui,
                            save,
                            idx,
                            true,
                            &mut self.picker_category,
                            &mut self.picker_search,
                            &mut dummy_replace,
                        );
                    }
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

                ui.horizontal(|ui| {
                    ui.selectable_value(
                        &mut self.wayne_subtab,
                        WayneSubTab::EquipmentVault,
                        "🔫 Wayne's Equipment Vault (82 Slots)",
                    );
                    ui.selectable_value(
                        &mut self.wayne_subtab,
                        WayneSubTab::RegularItems,
                        "💊 Wayne's Regular Item Storage (100 Slots)",
                    );
                    ui.separator();
                    if ui
                        .button("⇅ Sort Wayne's Storage")
                        .on_hover_text(SORT_HELP)
                        .clicked()
                    {
                        save.sort_wayne_storage();
                    }
                });
                ui.add_space(4.0);

                match self.wayne_subtab {
                    WayneSubTab::EquipmentVault => {
                        egui::ScrollArea::vertical()
                            .id_salt("wayne_special_cards_scroll")
                            .auto_shrink([false, false])
                            .show(ui, |ui| {
                                for i in 0..82 {
                                    let val = save.special_storage[i];
                                    let (icon, title, sub, accent) =
                                        slot_value_icon_and_title(save, val);
                                    let resp = interactive_card(
                                        ui,
                                        ("wayne_spec_card", i),
                                        self.selected_wayne_special_slot == i,
                                        None,
                                        |ui| {
                                            ui.horizontal(|ui| {
                                                ui.label(
                                                    RichText::new(format!("Vault #{:02}", i + 1))
                                                        .monospace()
                                                        .small()
                                                        .color(TEXT_MUTED),
                                                );
                                                ui.label(RichText::new(icon));
                                                ui.label(RichText::new(title).strong().color(
                                                    if val <= 0 {
                                                        TEXT_MUTED
                                                    } else {
                                                        Color32::WHITE
                                                    },
                                                ));
                                            });
                                            if val > 0 {
                                                ui.label(RichText::new(sub).color(accent).small());
                                            }
                                        },
                                    );
                                    if resp.clicked() {
                                        self.selected_wayne_special_slot = i;
                                    }
                                    ui.add_space(2.0);
                                }
                            });
                    }
                    WayneSubTab::RegularItems => {
                        egui::ScrollArea::vertical()
                            .id_salt("wayne_reg_cards_scroll")
                            .auto_shrink([false, false])
                            .show(ui, |ui| {
                                let use_two_cols = ui.available_width() >= 460.0;
                                let step = if use_two_cols { 2 } else { 1 };
                                for row_start in (0..100).step_by(step) {
                                    ui.columns(step, |scols| {
                                        for off in 0..step {
                                            let i = row_start + off;
                                            if i >= 100 {
                                                continue;
                                            }
                                            let val = save.storage[i];
                                            let (icon, title, _, _) =
                                                slot_value_icon_and_title(save, val);
                                            if interactive_card(
                                                &mut scols[off],
                                                ("wayne_reg_card", i),
                                                self.selected_wayne_regular_slot == i,
                                                None,
                                                |ui| {
                                                    ui.horizontal(|ui| {
                                                        ui.label(
                                                            RichText::new(format!("#{:02}", i + 1))
                                                                .monospace()
                                                                .small()
                                                                .color(TEXT_MUTED),
                                                        );
                                                        ui.label(RichText::new(icon));
                                                        ui.label(
                                                            RichText::new(title).strong().color(
                                                                if val <= 0 {
                                                                    TEXT_MUTED
                                                                } else {
                                                                    Color32::WHITE
                                                                },
                                                            ),
                                                        );
                                                    });
                                                },
                                            )
                                            .clicked()
                                            {
                                                self.selected_wayne_regular_slot = i;
                                            }
                                        }
                                    });
                                    ui.add_space(2.0);
                                }
                            });
                    }
                }
            });
    }
}
