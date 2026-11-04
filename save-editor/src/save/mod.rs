//! Save data types and stable public API; editing and wire-format code are separate.
use anyhow::{bail, Result};
use serde::{Deserialize, Serialize};

use crate::charset::{decode_sjis_title, format_sjis_title_bytes, pe_decode, pe_encode_fixed};
use crate::crc::{
    calc_save_crc16, read_stored_crc, update_save_crc16, SAVE_BLOCK_SIZE, SAVE_CRC_OFFSET,
};
use crate::disc::{
    game_data, hp_with_armor, HP_STAT, ITEM_CAPACITY_STAT, MAX_STAT_BAG_SLOTS, MAX_STAT_MAX_HP,
    TOP_LEVEL_STAT,
};
use crate::tables::{
    encode_mod_byte, game_icon_and_kind, item_metadata, map_location_descriptive, mod_metadata,
    save_header_location_name, ItemCategory, BAG_CAPACITY_BY_LEVEL, EQUIPMENT_TEMPLATES,
    PARASITE_SPELLS,
};

pub const RT_OFFSET: usize = 0x0100;
pub const TAIL_OFFSET: usize = 0x13E4;
pub const SCRIPT_VAR_OFFSET: usize = 0x13E4;
pub const SCRIPT_VAR_SIZE: usize = 0x0800;
pub const BATTLE_STATE_OFFSET: usize = 0x1BFC;
/// Item Capacity stat value of the top level (98), which gives `MAX_STAT_BAG_SLOTS`.
pub const MAX_BAG_ITEM_CAPACITY_STAT: u16 = TOP_LEVEL_STAT;
/// Armor mod id of HP Max Up (`AMOD_HP_MAX_UP`).
const ARMOR_MOD_HP_MAX_UP: u8 = 0x0B;

mod cheats;
mod equipment;
mod hex_bytes_serde;
mod inventory;
mod presentation;
mod progression;
mod serialization;
mod stats;

pub use equipment::EquipmentRecord;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PeSaveSlot {
    /// Complete companion, included in JSON exports.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub companion: Option<crate::companion::Companion>,
    /// Optional slot number on memory card (0..15)
    pub slot_index: usize,
    /// Optional PsyZ / PS1 20-char filename (`BASLUS-006620000000A`)
    pub filename: String,
    /// Whether this save is marked as Clear Data / EX Game (`titleStyleFlag != 0`)
    pub is_clear_data: bool,
    /// Decoded Shift-JIS header title (`MemCardFileHeader.title`)
    pub header_title: String,
    /// Automatically re-format the Shift-JIS header title when saving
    pub auto_update_header_title: bool,

    // SaveRuntimeState top-level fields (`0x0100..0x0120`)
    pub save_name: String,
    pub play_time_seconds: i32,
    pub game_time_seconds: i32,
    pub aya_name: String,
    pub menu_layout: u8,
    pub draw_base_y: i8,

    // AyaSaveState (`0x0120..0x01CC`)
    pub total_exp: u32,
    pub max_hp: u16,
    pub current_hp: u16,
    /// 0-based internal level (`0..=98` -> display Lv `1..=99`)
    pub level: u8,
    /// EX Game round counter (`0` = 1st Playthrough, `1` = 2nd Round, etc.)
    pub new_game_count: u8,
    /// Aya's own bag size (`10..=46`), set by the game from the Item Capacity stat; equipped
    /// armor mods add up to 50 at run time
    pub inventory_slot_count: u8,
    /// Bonus Points (`0..=99999`)
    pub bonus_points: u32,
    /// PE energy pool bonus
    pub pe_bonus_pool: u16,
    /// Equipped weapon slot index in `inventory_items` (`0..49` or `-1`)
    pub equipped_weapon_slot: i8,
    /// Equipped armor slot index in `inventory_items` (`0..49` or `-1`)
    pub equipped_armor_slot: i8,
    /// 20-bit bitmask of unlocked Parasite Energy spells
    pub parasite_spell_flags: u32,
    /// 7 stat growth levels (HP, Offense, Defense, P.Energy, Status Rec, ATB, Item Cap; base = level * 10, max = 1000)
    pub stats_levels: [u16; 7],
    /// Story day shown on save card (`1..=6`)
    pub play_time_class: i16,
    /// Current room / map number (`1..=437`, or `< 0` for Clear Data)
    pub current_map_number: i16,
    /// Menu clamp value (`rt + 0x60`)
    pub menu_clamp_value: i16,
    /// UI window RGB color (`0x00BBGGRR`)
    pub draw_blend_color: u32,

    /// Aya's 50 inventory slots (`0` = empty, `1..=255` = base item ID, `0x100..=0x17F` = equipment index, `0x200..=0x208` = special ammo/crate)
    pub inventory_items: Vec<i16>,
    /// 128 equipment records (`rt + 0xCC..0x10CC`)
    pub equipment: Vec<EquipmentRecord>,
    /// 6 ammo category counts (`[0]` 9mm/Bullets, `[1]` Rockets, `[2]` DNA Bullets)
    pub category_counts: [u16; 6],
    /// Wayne's 100 regular item storage slots (`rt + 0x10D8..0x11A0`)
    pub storage: Vec<i16>,
    /// Wayne's 82 equipment storage slots (`rt + 0x11A0..0x1244`)
    pub special_storage: Vec<i16>,

    // Tail fields (`0x13E4..0x1C8C`)
    pub room_type: u16,
    pub requested_entity_bank: u16,
    pub current_story_day: i8,
    pub pending_story_day: i8,
    pub story_day_flags: u8,
    pub fade_mode: u8,

    // Checksum diagnostics
    pub stored_crc: u16,
    pub calculated_crc: u16,

    /// Full raw `0x2000` bytes preserved so untouched script flags, TIM icon, and tail data are 100% lossless
    #[serde(with = "hex_bytes_serde")]
    pub raw_bytes: Vec<u8>,
}
