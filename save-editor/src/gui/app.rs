//! Lifecycle, file loading/saving and frame orchestration.
use super::*;

impl SaveEditorApp {
    pub fn new(cc: &eframe::CreationContext<'_>, initial_path: Option<PathBuf>) -> Self {
        configure_mito_theme(&cc.egui_ctx);

        let mut app = Self {
            container: None,
            selected_slot: 0,
            active_tab: EditorTab::InventoryAndLoadout,
            slot_textures: Default::default(),
            status_message: String::new(),
            is_error_status: false,
            selected_inv_slot: 0,
            show_locked_inv_slots: false,
            inv_replace_mode: false,
            selected_equip_index: 0,
            show_only_active_equip: true,
            wayne_subtab: WayneSubTab::EquipmentVault,
            selected_wayne_regular_slot: 0,
            selected_wayne_special_slot: 0,
            picker_category: ItemPickerFilter::All,
            picker_search: String::new(),
            show_hidden_pe_spells: false,
            show_script_hex_inspector: false,
            script_inspector_offset: 0,
        };

        crate::disc::load_configured_disc();

        if let Some(path) = initial_path {
            app.load_path(&cc.egui_ctx, path);
        } else {
            app.open_start_card(&cc.egui_ctx);
        }

        app
    }

    /// Open the game's current card (see `card_location`), or old saves beside the game when
    /// the current cards are empty. Sample cards are only opened from Quick Open.
    fn open_start_card(&mut self, ctx: &egui::Context) {
        let current = card_location::current_cards();
        match card_location::start_card(current.as_ref(), &card_location::legacy_cards()) {
            Some(StartCard::Current(card, dir)) => {
                self.load_path(ctx, dir.clone());
                if !self.is_error_status {
                    self.set_status(
                        format!(
                            "Opened the game's card {} ({} save(s)) at {}.",
                            card + 1,
                            card_location::save_count(&dir),
                            dir.display()
                        ),
                        false,
                    );
                }
            }
            Some(StartCard::Legacy(dir)) => {
                self.load_path(ctx, dir.clone());
                if !self.is_error_status {
                    let home = current
                        .map(|c| c[0].display().to_string())
                        .unwrap_or_else(|| "its settings folder".to_string());
                    self.set_status(
                        format!(
                            "Opened old saves in {}. The game now keeps its cards in {}; \
                             use Convert / Export → Import Old Saves to copy them there.",
                            dir.display(),
                            home
                        ),
                        false,
                    );
                }
            }
            None => self.set_status(
                match current {
                    Some(c) => format!(
                        "No saves yet. The game keeps its memory cards in {} (created at its first start).",
                        c[0].parent().unwrap_or(&c[0]).display()
                    ),
                    None => "Open a PsyZ folder (bu00/bu10) or a PS1 Memory Card (.srm/.mcr/.gme)."
                        .to_string(),
                },
                false,
            ),
        }
    }

    /// Copy the PsyZ saves from a folder the player picks into one of the game's current cards,
    /// after showing what will be copied; existing saves are never replaced.
    pub(super) fn import_old_saves(&mut self, ctx: &egui::Context) {
        let Some(cards) = card_location::current_cards() else {
            self.set_status(
                "The game's card folder could not be worked out on this system.",
                true,
            );
            return;
        };
        let mut dialog = rfd::FileDialog::new()
            .set_title("Select the folder with your old saves (bu00 or bu10)");
        if let Some(dir) = card_location::legacy_cards().into_iter().next() {
            dialog = dialog.set_directory(dir);
        }
        let Some(source) = dialog.pick_folder() else {
            return;
        };
        // A bu10 folder is card 2; anything else goes to card 1.
        let card = usize::from(
            source
                .file_name()
                .is_some_and(|n| n.eq_ignore_ascii_case("bu10")),
        );
        let target = cards[card].clone();
        let plan = match card_location::plan_import(&source, &target) {
            Ok(plan) => plan,
            Err(e) => return self.set_status(format!("Import error: {e:#}"), true),
        };
        let mut summary = format!(
            "From: {}\nTo the game's card {}: {}\n\nCopy {} save(s).",
            source.display(),
            card + 1,
            target.display(),
            plan.copy.len()
        );
        if !plan.existing.is_empty() {
            summary += &format!(
                "\nKeep {} save(s) already on the card (same name, not replaced): {}",
                plan.existing.len(),
                plan.existing.join(", ")
            );
        }
        for (name, why) in &plan.rejected {
            summary += &format!("\nSkip {name}: {why}.");
        }
        summary += "\n\nYour old files stay where they are.";
        if plan.copy.is_empty() {
            rfd::MessageDialog::new()
                .set_title("Nothing to import")
                .set_description(&summary)
                .set_buttons(rfd::MessageButtons::Ok)
                .show();
            return;
        }
        let go = rfd::MessageDialog::new()
            .set_title("Import old saves")
            .set_description(&summary)
            .set_buttons(rfd::MessageButtons::OkCancel)
            .show();
        if go != rfd::MessageDialogResult::Ok {
            return;
        }
        match card_location::run_import(&plan) {
            Ok(n) => {
                self.load_path(ctx, target.clone());
                self.set_status(
                    format!(
                        "Imported {} save(s) into the game's card {} ({}); kept {} existing.",
                        n,
                        card + 1,
                        target.display(),
                        plan.existing.len()
                    ),
                    false,
                );
            }
            Err(e) => self.set_status(format!("Import error: {e:#}"), true),
        }
    }

    pub(super) fn set_status(&mut self, msg: impl Into<String>, is_error: bool) {
        self.status_message = msg.into();
        self.is_error_status = is_error;
    }

    pub(super) fn load_path(&mut self, ctx: &egui::Context, path: PathBuf) {
        match MemoryCardContainer::open(&path) {
            Ok(container) => {
                let first_active = container.first_active_slot_index().unwrap_or(0);
                let active_count = container.active_slot_indices().len();
                let kind_label = container.kind.label();
                self.container = Some(container);
                self.selected_slot = first_active;
                self.selected_inv_slot = 0;
                self.rebuild_icon_textures(ctx);
                self.set_status(
                    format!(
                        "Loaded {} ({}, {} active slot(s)).",
                        path.display(),
                        kind_label,
                        active_count
                    ),
                    false,
                );
            }
            Err(err) => {
                self.set_status(format!("Error opening {}: {:#}", path.display(), err), true);
            }
        }
    }

    pub(super) fn rebuild_icon_textures(&mut self, ctx: &egui::Context) {
        self.slot_textures = Default::default();
        if let Some(container) = &self.container {
            for i in 0..MAX_CARD_SLOTS {
                if let Some(slot) = &container.slots[i] {
                    let rgba = slot.decode_icon_rgba();
                    let img = ColorImage::from_rgba_unmultiplied([16, 16], &rgba);
                    let tex = ctx.load_texture(
                        format!("pe_slot_icon_{}", i),
                        img,
                        TextureOptions::NEAREST,
                    );
                    self.slot_textures[i] = Some(tex);
                }
            }
        }
    }

    pub(super) fn save_current_container(&mut self) {
        if let Some(container) = &mut self.container {
            match container.save() {
                Ok(()) => {
                    let path_str = container.path.display().to_string();
                    self.set_status(
                        format!("✔ Saved all slots & recalculated CRC-16 to {}.", path_str),
                        false,
                    );
                }
                Err(err) => {
                    self.set_status(format!("Save failed: {:#}", err), true);
                }
            }
        }
    }
}

impl eframe::App for SaveEditorApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        if ctx.input_mut(|i| i.consume_key(egui::Modifiers::COMMAND, egui::Key::S)) {
            self.save_current_container();
        }

        self.show_application_bar(ctx);
        self.show_status_bar(ctx);
        self.show_slot_browser(ctx);
        // 4. CHECK FOR EMPTY CONTAINER OR EMPTY SLOT BEFORE RENDERING PANELS
        // =====================================================================
        if self.container.is_none() {
            egui::CentralPanel::default()
                .frame(egui::Frame::NONE.fill(BG_PANEL).inner_margin(Margin::same(20)))
                .show(ctx, |ui| {
                    ui.centered_and_justified(|ui| {
                        ui.vertical_centered(|ui| {
                            ui.add_space(100.0);
                            ui.heading("No Save Container Loaded");
                            ui.label(
                                RichText::new("Use the top bar to open a PsyZ directory (bu00) or PS1 Memory Card (.srm/.mcr).")
                                    .color(TEXT_MUTED),
                            );
                        });
                    });
                });
            return;
        }

        let is_empty_slot = self
            .container
            .as_ref()
            .and_then(|c| c.slots[self.selected_slot].as_ref())
            .is_none();

        if is_empty_slot {
            let first_active = self
                .container
                .as_ref()
                .and_then(|c| c.first_active_slot_index());
            let slot_letter = (b'A' + self.selected_slot as u8) as char;
            let mut clone_from: Option<usize> = None;

            egui::CentralPanel::default()
                .frame(egui::Frame::NONE.fill(BG_PANEL).inner_margin(Margin::same(20)))
                .show(ctx, |ui| {
                    ui.vertical_centered(|ui| {
                        ui.add_space(80.0);
                        ui.heading(format!(
                            "Slot {:02} ({}) is Empty",
                            self.selected_slot + 1,
                            slot_letter
                        ));
                        ui.add_space(8.0);
                        if let Some(src_idx) = first_active {
                            ui.label(
                                RichText::new(format!(
                                    "You can clone Slot {:02} into this empty slot to create a backup or experimental build.",
                                    src_idx + 1
                                ))
                                .color(TEXT_MUTED),
                            );
                            ui.add_space(12.0);
                            if ui
                                .button(
                                    RichText::new(format!(
                                        "📋 Clone Slot {:02} into Slot {:02} ({})",
                                        src_idx + 1,
                                        self.selected_slot + 1,
                                        slot_letter
                                    ))
                                    .strong(),
                                )
                                .clicked()
                            {
                                clone_from = Some(src_idx);
                            }
                        }
                    });
                });

            if let Some(src_idx) = clone_from {
                let target_idx = self.selected_slot;
                let ok = self
                    .container
                    .as_mut()
                    .map(|c| c.clone_slot(src_idx, target_idx).is_ok())
                    .unwrap_or(false);
                if ok {
                    self.rebuild_icon_textures(ctx);
                    self.set_status(
                        format!(
                            "Cloned Slot {:02} into Slot {:02}.",
                            src_idx + 1,
                            target_idx + 1
                        ),
                        false,
                    );
                }
            }
            return;
        }

        // =====================================================================
        self.show_workspace_header(ctx);
        self.show_active_tab(ctx);
    }
}
