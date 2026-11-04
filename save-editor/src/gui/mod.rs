//! Application state and stable GUI entry points. Screen code lives in sibling modules.
use std::path::{Path, PathBuf};

use eframe::egui::{
    self, Color32, ColorImage, CornerRadius, CursorIcon, Margin, RichText, Sense, Stroke,
    TextWrapMode, TextureHandle, TextureOptions, Vec2,
};

use crate::card_location::{self, StartCard};
use crate::memcard::{MemoryCardContainer, MAX_CARD_SLOTS};
use crate::save::{EquipmentRecord, PeSaveSlot, SCRIPT_VAR_SIZE};
use crate::tables::{
    item_metadata, map_location_descriptive, mod_metadata, ItemCategory, MAP_TELEPORT_PRESETS,
    PARASITE_SPELLS, STAT_GROWTH_NAMES,
};

mod app;
mod armory;
mod character;
mod chrome;
mod equipment;
mod inventory;
mod mod_data;
mod parasite_energy;
mod slot_browser;
mod story;
mod theme;
mod wayne;
mod widgets;
mod workspace;

use character::render_tab_character_and_stats;
use equipment::*;
use parasite_energy::render_tab_parasite_energy_cards;
use theme::*;
use widgets::*;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum EditorTab {
    InventoryAndLoadout,
    CharacterAndStats,
    ParasiteEnergy,
    ArmoryRecords,
    WayneVault,
    StoryAndTeleport,
    ModSaveData,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum ItemPickerFilter {
    All,
    Consumables,
    Ammo,
    UpgradesAndTools,
    Weapons,
    Armor,
    KeysAndCards,
}

impl ItemPickerFilter {
    fn label(self) -> &'static str {
        match self {
            Self::All => "All",
            Self::Consumables => "💊 Meds",
            Self::Ammo => "弹 Ammo",
            Self::UpgradesAndTools => "🔧 Tools / +N",
            Self::Weapons => "🔫 Weapons",
            Self::Armor => "🛡 Armor",
            Self::KeysAndCards => "🔑 Keys & Cards",
        }
    }

    fn matches(self, cat: ItemCategory) -> bool {
        match self {
            Self::All => cat != ItemCategory::Empty,
            Self::Consumables => cat == ItemCategory::Consumable,
            Self::Ammo => matches!(cat, ItemCategory::Ammo | ItemCategory::Crate),
            Self::UpgradesAndTools => matches!(cat, ItemCategory::StatUpgrade | ItemCategory::Tool),
            Self::Weapons => cat.is_weapon(),
            Self::Armor => cat.is_armor(),
            Self::KeysAndCards => matches!(
                cat,
                ItemCategory::KeyItem | ItemCategory::TradingCard | ItemCategory::ParasiteEnergy
            ),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum WayneSubTab {
    EquipmentVault,
    RegularItems,
}

pub struct SaveEditorApp {
    container: Option<MemoryCardContainer>,
    selected_slot: usize,
    active_tab: EditorTab,
    slot_textures: [Option<TextureHandle>; MAX_CARD_SLOTS],
    status_message: String,
    is_error_status: bool,

    // Master-detail selections
    selected_inv_slot: usize,
    show_locked_inv_slots: bool,
    inv_replace_mode: bool,

    selected_equip_index: usize,
    show_only_active_equip: bool,

    wayne_subtab: WayneSubTab,
    selected_wayne_regular_slot: usize,
    selected_wayne_special_slot: usize,

    // Shared item picker filter state
    picker_category: ItemPickerFilter,
    picker_search: String,

    // Diagnostics / Advanced
    show_hidden_pe_spells: bool,
    show_script_hex_inspector: bool,
    script_inspector_offset: usize,
}

const BAG_SIZE_HELP: &str = "The game sets the bag size from the Item Capacity stat (up to 46), \
    so saving raises or lowers that stat to match. Armor with \
    Item Capacity mods adds up to 50 in game.";

const MAX_HP_HELP: &str = "The game recomputes max HP from the HP stat at every battle, so saving \
    raises or lowers that stat to match (exactly with a game disc loaded). The top is 869, or 999 \
    while the equipped armor has HP Max Up.";

const SORT_HELP: &str = "Weapons, armor, ammo, medicine, other items, then key items; empty slots \
    last. Copies of one weapon or armor go best first. The equipped gun and armor stay equipped. \
    Takes effect when you save.";

pub fn run_gui(initial_path: Option<PathBuf>) -> anyhow::Result<()> {
    let native_options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_title("Parasite Eve (SLUS-006.62 / PsyZ PC) Save Editor")
            .with_inner_size([1340.0, 860.0])
            .with_min_inner_size([1024.0, 680.0]),
        ..Default::default()
    };

    eframe::run_native(
        "Parasite Eve Save Editor",
        native_options,
        Box::new(move |cc| Ok(Box::new(SaveEditorApp::new(cc, initial_path)))),
    )
    .map_err(|e| anyhow::anyhow!("GUI error: {}", e))
}

pub fn default_psyz_bu00_dir(workspace_root: &Path) -> PathBuf {
    workspace_root.join("bu00")
}

#[cfg(test)]
mod tests;
