//! Headless rendering checks for screen/module boundaries. No player data is written.
use super::*;
fn loaded_app() -> SaveEditorApp {
    let path =
        Path::new(env!("CARGO_MANIFEST_DIR")).join("samples/Parasite_Eve_PS1_Sample_Card.srm");
    let card = MemoryCardContainer::open(&path).unwrap();
    SaveEditorApp {
        container: Some(card),
        selected_slot: 0,
        active_tab: EditorTab::InventoryAndLoadout,
        slot_textures: Default::default(),
        status_message:
            "Ready. Open a PsyZ folder (bu00/bu10) or a PS1 Memory Card (.srm/.mcr/.gme)."
                .to_string(),
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
    }
}

#[test]
fn all_screens_render_without_mutating_saved_mod_data() {
    for tab in [
        EditorTab::InventoryAndLoadout,
        EditorTab::CharacterAndStats,
        EditorTab::ParasiteEnergy,
        EditorTab::ArmoryRecords,
        EditorTab::WayneVault,
        EditorTab::StoryAndTeleport,
        EditorTab::ModSaveData,
    ] {
        for (width, height) in [(1024.0, 680.0), (1340.0, 860.0)] {
            let ctx = egui::Context::default();
            configure_mito_theme(&ctx);
            let mut app = loaded_app();
            app.active_tab = tab;
            let tail = include_bytes!("../../tests/fixtures/mod-records-junk.bin");
            let slot = app.container.as_mut().unwrap().slots[0].as_mut().unwrap();
            slot.raw_bytes[crate::mod_save::OFFSET..]
                .copy_from_slice(&tail[crate::mod_save::OFFSET..]);
            let before = slot.raw_bytes[crate::mod_save::OFFSET..].to_vec();
            for _ in 0..2 {
                let input = egui::RawInput {
                    screen_rect: Some(egui::Rect::from_min_size(
                        egui::Pos2::ZERO,
                        egui::vec2(width, height),
                    )),
                    ..Default::default()
                };
                let output = ctx.run(input, |ctx| {
                    app.show_application_bar(ctx);
                    app.show_status_bar(ctx);
                    app.show_slot_browser(ctx);
                    app.show_workspace_header(ctx);
                    app.show_active_tab(ctx);
                });
                assert!(!output.shapes.is_empty(), "{:?}", tab);
            }
            let slot = app.container.as_ref().unwrap().slots[0].as_ref().unwrap();
            assert_eq!(
                &slot.raw_bytes[crate::mod_save::OFFSET..],
                &before,
                "{:?}",
                tab
            );
        }
    }
}

#[test]
fn companion_screens_render_without_changing_stacks_or_provider_data() {
    for tab in [
        EditorTab::InventoryAndLoadout,
        EditorTab::WayneVault,
        EditorTab::ModSaveData,
        EditorTab::ArmoryRecords,
    ] {
        let ctx = egui::Context::default();
        let mut app = loaded_app();
        app.active_tab = tab;
        app.container.as_mut().unwrap().slots[0] = Some(crate::companion_tests::fixture());
        let before =
            serde_json::to_string(app.container.as_ref().unwrap().slots[0].as_ref().unwrap())
                .unwrap();
        for (width, height) in [(1024.0, 680.0), (1340.0, 860.0)] {
            let output = ctx.run(
                egui::RawInput {
                    screen_rect: Some(egui::Rect::from_min_size(
                        egui::Pos2::ZERO,
                        egui::vec2(width, height),
                    )),
                    ..Default::default()
                },
                |ctx| {
                    app.show_application_bar(ctx);
                    app.show_status_bar(ctx);
                    app.show_slot_browser(ctx);
                    app.show_workspace_header(ctx);
                    app.show_active_tab(ctx);
                },
            );
            assert!(!output.shapes.is_empty());
        }
        assert_eq!(
            before,
            serde_json::to_string(app.container.as_ref().unwrap().slots[0].as_ref().unwrap())
                .unwrap()
        );
    }
}
