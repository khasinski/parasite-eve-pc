//! Serialization operations for PeSaveSlot.
use super::*;

impl PeSaveSlot {
    pub fn from_bytes(raw: &[u8], slot_index: usize, filename: impl Into<String>) -> Result<Self> {
        if raw.len() < SAVE_BLOCK_SIZE {
            bail!(
                "Save block is too short: expected {} bytes (0x2000), got {} bytes",
                SAVE_BLOCK_SIZE,
                raw.len()
            );
        }
        let block = &raw[..SAVE_BLOCK_SIZE];
        if block[0] != b'S' || block[1] != b'C' {
            bail!(
                "Invalid PS1 save header magic: expected 'SC' (0x53, 0x43), got {:02X} {:02X}",
                block[0],
                block[1]
            );
        }

        let filename = filename.into();
        let (_fullwidth_title, header_title) = decode_sjis_title(&block[0x04..0x44]);
        let is_clear_data =
            filename.as_bytes().get(18) == Some(&b'1') || header_title.contains("Clear");

        let rt = &block[RT_OFFSET..];
        let save_name = pe_decode(&rt[0x00..0x08]);
        let play_time_seconds = i32::from_le_bytes(rt[0x08..0x0C].try_into().unwrap());
        let game_time_seconds = i32::from_le_bytes(rt[0x0C..0x10].try_into().unwrap());
        let aya_name = pe_decode(&rt[0x10..0x1D]);
        let menu_layout = rt[0x1D];
        let draw_base_y = rt[0x1F] as i8;

        let total_exp = u32::from_le_bytes(rt[0x20..0x24].try_into().unwrap());
        let max_hp = u16::from_le_bytes(rt[0x26..0x28].try_into().unwrap());
        let current_hp = u16::from_le_bytes(rt[0x28..0x2A].try_into().unwrap());
        let level = rt[0x2A];
        let new_game_count = rt[0x2B];
        let inventory_slot_count = rt[0x2C];
        let bonus_points = u32::from_le_bytes(rt[0x30..0x34].try_into().unwrap());
        let pe_bonus_pool = u16::from_le_bytes(rt[0x3E..0x40].try_into().unwrap());
        let equipped_weapon_slot = rt[0x40] as i8;
        let equipped_armor_slot = rt[0x42] as i8;
        let parasite_spell_flags = u32::from_le_bytes(rt[0x44..0x48].try_into().unwrap());

        let mut stats_levels = [0u16; 7];
        for i in 0..7 {
            let off = 0x48 + i * 2;
            stats_levels[i] = u16::from_le_bytes(rt[off..off + 2].try_into().unwrap());
        }

        let play_time_class = i16::from_le_bytes(rt[0x5C..0x5E].try_into().unwrap());
        let current_map_number = i16::from_le_bytes(rt[0x5E..0x60].try_into().unwrap());
        let menu_clamp_value = i16::from_le_bytes(rt[0x60..0x62].try_into().unwrap());
        let draw_blend_color = u32::from_le_bytes(rt[0x64..0x68].try_into().unwrap());

        let mut inventory_items = Vec::with_capacity(50);
        for i in 0..50 {
            let off = 0x68 + i * 2;
            inventory_items.push(i16::from_le_bytes(rt[off..off + 2].try_into().unwrap()));
        }

        let mut equipment = Vec::with_capacity(128);
        for i in 0..128 {
            let off = 0xCC + i * 0x20;
            equipment.push(EquipmentRecord::parse(i, &rt[off..off + 0x20]));
        }

        let mut category_counts = [0u16; 6];
        for i in 0..6 {
            let off = 0x10CC + i * 2;
            category_counts[i] = u16::from_le_bytes(rt[off..off + 2].try_into().unwrap());
        }

        let mut storage = Vec::with_capacity(100);
        for i in 0..100 {
            let off = 0x10D8 + i * 2;
            storage.push(i16::from_le_bytes(rt[off..off + 2].try_into().unwrap()));
        }

        let mut special_storage = Vec::with_capacity(82);
        for i in 0..82 {
            let off = 0x11A0 + i * 2;
            special_storage.push(i16::from_le_bytes(rt[off..off + 2].try_into().unwrap()));
        }

        let room_type = u16::from_le_bytes(block[0x1BF4..0x1BF6].try_into().unwrap());
        let requested_entity_bank = u16::from_le_bytes(block[0x1BF6..0x1BF8].try_into().unwrap());
        let current_story_day = block[0x1BF8] as i8;
        let pending_story_day = block[0x1BF9] as i8;
        let story_day_flags = block[0x1BFA];
        let fade_mode = block[0x1BFB];

        let stored_crc = (read_stored_crc(block) & 0xFFFF) as u16;
        let calculated_crc = calc_save_crc16(block);

        Ok(Self {
            companion: None,
            slot_index,
            filename,
            is_clear_data,
            header_title,
            auto_update_header_title: true,
            save_name,
            play_time_seconds,
            game_time_seconds,
            aya_name,
            menu_layout,
            draw_base_y,
            total_exp,
            max_hp,
            current_hp,
            level,
            new_game_count,
            inventory_slot_count,
            bonus_points,
            pe_bonus_pool,
            equipped_weapon_slot,
            equipped_armor_slot,
            parasite_spell_flags,
            stats_levels,
            play_time_class,
            current_map_number,
            menu_clamp_value,
            draw_blend_color,
            inventory_items,
            equipment,
            category_counts,
            storage,
            special_storage,
            room_type,
            requested_entity_bank,
            current_story_day,
            pending_story_day,
            story_day_flags,
            fade_mode,
            stored_crc,
            calculated_crc,
            raw_bytes: block.to_vec(),
        })
    }

    /// Check structural references before any save/export writes a container.
    /// Stat bonuses, ammo and legitimate custom effects are left untouched.
    pub fn validate_inventory(&self) -> Result<()> {
        self.validate_mod_inventory()?;
        if self.inventory_slot_count > 50
            || self.inventory_items.len() != 50
            || self.storage.len() != 100
            || self.special_storage.len() != 82
            || self.equipment.len() != 128
        {
            bail!("Invalid inventory dimensions");
        }
        for (index, item) in self.equipment.iter().enumerate() {
            if item.index != index {
                bail!("Equipment record {} has an invalid index", index);
            }
            if item.icon_id != 0
                && (item.item_id == 0
                    || item.kind >= 24
                    || item.tail_count > 10
                    || item.tail_capacity > 10)
            {
                bail!(
                    "Equipment record {} has invalid type or modification slots",
                    index
                );
            }
        }
        let mut seen = [false; 128];
        for list in [&self.inventory_items, &self.storage, &self.special_storage] {
            for &value in list {
                let id = value as u16;
                if id <= 255 || (0x200..=0x208).contains(&id) {
                    continue;
                }
                if !(0x100..0x180).contains(&id) {
                    bail!("Invalid inventory reference 0x{:04X}", id);
                }
                let index = (id - 0x100) as usize;
                if self.equipment[index].icon_id == 0 || seen[index] {
                    bail!(
                        "Equipment record {} is missing or referenced more than once",
                        index
                    );
                }
                seen[index] = true;
            }
        }
        for (armor, slot) in [
            (false, self.equipped_weapon_slot),
            (true, self.equipped_armor_slot),
        ] {
            if slot == -1 {
                continue;
            }
            if !(0..50).contains(&slot) {
                bail!("Invalid equipped inventory slot {}", slot);
            }
            let id = self.inventory_items[slot as usize] as u16;
            let kind = if (0x100..0x180).contains(&id) {
                self.equipment[(id - 0x100) as usize].kind
            } else if (1..=255).contains(&id) {
                item_metadata(id as u8).default_kind
            } else {
                0
            };
            if if armor {
                kind != 9
            } else {
                !(1..=8).contains(&kind)
            } {
                bail!(
                    "Equipped slot {} does not contain compatible equipment",
                    slot
                );
            }
        }
        Ok(())
    }

    /// Serializes all modified fields back into `self.raw_bytes`, syncs `g_AyaBattleState`,
    /// updates the Shift-JIS header title if relevant fields changed, and recalculates the CRC-16 at `0x1C8C`.
    pub fn to_bytes(&mut self) -> Vec<u8> {
        if self.raw_bytes.len() < SAVE_BLOCK_SIZE {
            self.raw_bytes.resize(SAVE_BLOCK_SIZE, 0);
        }
        self.raw_bytes[0] = b'S';
        self.raw_bytes[1] = b'C';
        if self.raw_bytes[2] == 0 {
            self.raw_bytes[2] = 0x11;
        }
        if self.raw_bytes[3] == 0 {
            self.raw_bytes[3] = 0x01;
        }

        let orig_slot_idx =
            crate::memcard::slot_index_from_filename(&self.filename).unwrap_or(self.slot_index);
        let orig_clear = self.filename.as_bytes().get(18) == Some(&b'1');

        // Update filename style character and slot letter
        let slot_letter = (b'A' + (self.slot_index.min(14) as u8)) as char;
        let style_char = if self.is_clear_data { '1' } else { '0' };
        self.filename = format!("BASLUS-00662000000{}{}", style_char, slot_letter);

        // Check which core fields changed relative to `raw_bytes`
        let orig_play_time = i32::from_le_bytes(
            self.raw_bytes[RT_OFFSET + 0x08..RT_OFFSET + 0x0C]
                .try_into()
                .unwrap(),
        );
        let orig_max_hp = u16::from_le_bytes(
            self.raw_bytes[RT_OFFSET + 0x26..RT_OFFSET + 0x28]
                .try_into()
                .unwrap(),
        );
        let orig_cur_hp = u16::from_le_bytes(
            self.raw_bytes[RT_OFFSET + 0x28..RT_OFFSET + 0x2A]
                .try_into()
                .unwrap(),
        );
        let orig_level = self.raw_bytes[RT_OFFSET + 0x2A];
        let orig_slot_count = self.raw_bytes[RT_OFFSET + 0x2C];
        let orig_day = i16::from_le_bytes(
            self.raw_bytes[RT_OFFSET + 0x5C..RT_OFFSET + 0x5E]
                .try_into()
                .unwrap(),
        );
        let orig_map = i16::from_le_bytes(
            self.raw_bytes[RT_OFFSET + 0x5E..RT_OFFSET + 0x60]
                .try_into()
                .unwrap(),
        );

        let header_needs_update = self.auto_update_header_title
            && (self.slot_index != orig_slot_idx
                || self.is_clear_data != orig_clear
                || self.play_time_seconds != orig_play_time
                || self.play_time_class != orig_day
                || self.current_map_number != orig_map);

        if header_needs_update {
            let loc = save_header_location_name(self.current_map_number, self.is_clear_data);
            let title_bytes = format_sjis_title_bytes(
                self.slot_index + 1,
                self.play_time_seconds,
                self.play_time_class,
                Some(loc),
                self.is_clear_data,
            );
            self.raw_bytes[0x04..0x44].copy_from_slice(&title_bytes);
            self.header_title = decode_sjis_title(&title_bytes).1;
        }

        let stat_off = RT_OFFSET + 0x48 + ITEM_CAPACITY_STAT * 2;
        let orig_item_capacity =
            u16::from_le_bytes(self.raw_bytes[stat_off..stat_off + 2].try_into().unwrap());
        self.sync_bag_with_item_capacity(orig_slot_count, orig_item_capacity);
        let hp_off = RT_OFFSET + 0x48 + HP_STAT * 2;
        let orig_hp_stat =
            u16::from_le_bytes(self.raw_bytes[hp_off..hp_off + 2].try_into().unwrap());
        self.sync_max_hp_with_hp_stat(orig_max_hp, orig_hp_stat);

        let battle_state_needs_sync = self.max_hp != orig_max_hp
            || self.current_hp != orig_cur_hp
            || self.level != orig_level
            || self.inventory_slot_count != orig_slot_count;

        {
            let rt = &mut self.raw_bytes[RT_OFFSET..];
            if pe_decode(&rt[0x00..0x08]) != self.save_name {
                pe_encode_fixed(&self.save_name, &mut rt[0x00..0x08], 7);
            }
            rt[0x08..0x0C].copy_from_slice(&self.play_time_seconds.to_le_bytes());
            rt[0x0C..0x10].copy_from_slice(&self.game_time_seconds.to_le_bytes());
            if pe_decode(&rt[0x10..0x1D]) != self.aya_name {
                pe_encode_fixed(&self.aya_name, &mut rt[0x10..0x1D], 12);
            }
            rt[0x1D] = self.menu_layout;
            rt[0x1F] = self.draw_base_y as u8;

            rt[0x20..0x24].copy_from_slice(&self.total_exp.to_le_bytes());
            rt[0x26..0x28].copy_from_slice(&self.max_hp.to_le_bytes());
            rt[0x28..0x2A].copy_from_slice(&self.current_hp.to_le_bytes());
            rt[0x2A] = self.level;
            rt[0x2B] = self.new_game_count;
            rt[0x2C] = self.inventory_slot_count;
            rt[0x30..0x34].copy_from_slice(&self.bonus_points.to_le_bytes());
            rt[0x3E..0x40].copy_from_slice(&self.pe_bonus_pool.to_le_bytes());
            rt[0x40] = self.equipped_weapon_slot as u8;
            rt[0x42] = self.equipped_armor_slot as u8;
            rt[0x44..0x48].copy_from_slice(&self.parasite_spell_flags.to_le_bytes());

            for (i, val) in self.stats_levels.iter().enumerate() {
                let off = 0x48 + i * 2;
                rt[off..off + 2].copy_from_slice(&val.to_le_bytes());
            }

            rt[0x5C..0x5E].copy_from_slice(&self.play_time_class.to_le_bytes());
            rt[0x5E..0x60].copy_from_slice(&self.current_map_number.to_le_bytes());
            rt[0x60..0x62].copy_from_slice(&self.menu_clamp_value.to_le_bytes());
            rt[0x64..0x68].copy_from_slice(&self.draw_blend_color.to_le_bytes());

            for i in 0..50 {
                let val = self.inventory_items.get(i).copied().unwrap_or(0);
                let off = 0x68 + i * 2;
                rt[off..off + 2].copy_from_slice(&val.to_le_bytes());
            }

            for i in 0..128 {
                let off = 0xCC + i * 0x20;
                if let Some(rec) = self.equipment.get(i) {
                    rec.write(&mut rt[off..off + 0x20]);
                }
            }

            for (i, val) in self.category_counts.iter().enumerate() {
                let off = 0x10CC + i * 2;
                rt[off..off + 2].copy_from_slice(&val.to_le_bytes());
            }

            for i in 0..100 {
                let val = self.storage.get(i).copied().unwrap_or(0);
                let off = 0x10D8 + i * 2;
                rt[off..off + 2].copy_from_slice(&val.to_le_bytes());
            }

            for i in 0..82 {
                let val = self.special_storage.get(i).copied().unwrap_or(0);
                let off = 0x11A0 + i * 2;
                rt[off..off + 2].copy_from_slice(&val.to_le_bytes());
            }
        }

        // Update Tail & g_AyaBattleState (`0x1BFC..0x1C6C`)
        self.raw_bytes[0x1BF4..0x1BF6].copy_from_slice(&self.room_type.to_le_bytes());
        self.raw_bytes[0x1BF6..0x1BF8].copy_from_slice(&self.requested_entity_bank.to_le_bytes());
        self.raw_bytes[0x1BF8] = self.current_story_day as u8;
        self.raw_bytes[0x1BF9] = self.pending_story_day as u8;
        self.raw_bytes[0x1BFA] = self.story_day_flags;
        self.raw_bytes[0x1BFB] = self.fade_mode;

        if battle_state_needs_sync {
            let display_lvl = (self.level as i16).saturating_add(1);
            self.raw_bytes[BATTLE_STATE_OFFSET + 0x04..BATTLE_STATE_OFFSET + 0x06]
                .copy_from_slice(&display_lvl.to_le_bytes());
            self.raw_bytes[BATTLE_STATE_OFFSET + 0x0C..BATTLE_STATE_OFFSET + 0x0E]
                .copy_from_slice(&self.current_hp.to_le_bytes());
            self.raw_bytes[BATTLE_STATE_OFFSET + 0x0E..BATTLE_STATE_OFFSET + 0x10]
                .copy_from_slice(&self.current_hp.to_le_bytes());
            self.raw_bytes[BATTLE_STATE_OFFSET + 0x1C..BATTLE_STATE_OFFSET + 0x1E]
                .copy_from_slice(&self.max_hp.to_le_bytes());
            let slot_cap = self.inventory_slot_count as i16;
            self.raw_bytes[BATTLE_STATE_OFFSET + 0x26..BATTLE_STATE_OFFSET + 0x28]
                .copy_from_slice(&slot_cap.to_le_bytes());
        }

        // Compute and store CRC-16 at 0x1C8C (32-bit word with upper 16 bits zero)
        let _ = SAVE_CRC_OFFSET;
        let crc = update_save_crc16(&mut self.raw_bytes);
        self.stored_crc = crc;
        self.calculated_crc = crc;

        self.raw_bytes.clone()
    }
}
