//! Inventory operations for PeSaveSlot.
use super::*;

impl PeSaveSlot {
    /// Formats any inventory or storage slot value (`0`, `1..0xFF`, `0x100..0x17F`, `0x200..0x208`)
    pub fn describe_slot_value(&self, val: i16) -> String {
        if val <= 0 {
            return "--- Empty ---".to_string();
        }
        if (0x100..0x180).contains(&val) {
            let eq_idx = (val - 0x100) as usize;
            if let Some(rec) = self.equipment.get(eq_idx) {
                if rec.is_active() {
                    let engraved = if rec.is_engraved() { " [Wayne]" } else { "" };
                    let eff = rec.effective_stats();
                    if rec.is_armor() {
                        return format!(
                            "[E#{:02X}] {}{} (Def {} / PE {} / Crit {}, Slots {}/{})",
                            eq_idx,
                            rec.display_name(),
                            engraved,
                            eff[0],
                            eff[1],
                            eff[2],
                            rec.tail_count,
                            rec.tail_capacity
                        );
                    } else {
                        return format!(
                            "[E#{:02X}] {}{} (Atk {} / Rng {} / Bul {}, Ammo {}, Slots {}/{})",
                            eq_idx,
                            rec.display_name(),
                            engraved,
                            eff[0],
                            eff[1],
                            eff[2],
                            rec.ammo,
                            rec.tail_count,
                            rec.tail_capacity
                        );
                    }
                }
                return format!("[E#{:02X}] (Inactive Equipment Record)", eq_idx);
            }
        }
        if (1..=0xFF).contains(&val) {
            let meta = item_metadata(val as u8);
            return format!("{} (0x{:02X})", meta.display_name, val);
        }
        if (0x200..=0x208).contains(&val) {
            let idx = (val - 0x200) as usize;
            let label = match idx {
                0 => format!("9mm / Standard Bullets Pool ({})", self.category_counts[0]),
                1 => format!("Rockets Pool ({})", self.category_counts[1]),
                2 => format!("DNA / Maeda Bullets Pool ({})", self.category_counts[2]),
                _ => format!("Special Pool #{}", idx),
            };
            return format!("[0x{:03X}] {}", val, label);
        }
        format!("Unknown (0x{:04X})", val as u16)
    }

    /// Allocates a free `EquipmentRecord` in `self.equipment[0..128]` and initializes it from `item_id`.
    /// Returns the inventory slot value `0x100 + eq_index`.
    pub fn allocate_equipment_from_item(&mut self, item_id: u8) -> Option<i16> {
        let free_idx = self.equipment.iter().position(|r| !r.is_active())?;
        self.equipment[free_idx] = EquipmentRecord::from_item_template(free_idx, item_id);
        Some((0x100 + free_idx) as i16)
    }

    /// Adds an item to the first empty slot in Aya's inventory.
    /// If `item_id` is a weapon or armor (`0x3F..=0xC7`), automatically allocates an `EquipmentRecord`!
    pub fn add_item_to_inventory(&mut self, item_id: u8) -> Result<usize> {
        if crate::stacks::stackable(item_id as i16) && crate::stacks::read(self)?.is_some() {
            if let Some(index) = self
                .inventory_items
                .iter()
                .position(|&v| v == item_id as i16)
            {
                let quantity = self.stack_quantity(0, index)?;
                anyhow::ensure!(quantity < 99, "Stack is full (99 copies)");
                self.set_stack_quantity(0, index, quantity + 1)?;
                return Ok(index);
            }
        }
        let empty_slot = self
            .inventory_items
            .iter()
            .position(|&v| v == 0)
            .ok_or_else(|| anyhow::anyhow!("Aya's inventory is full (all 50 slots occupied)"))?;

        let meta = item_metadata(item_id);
        if meta.category.is_equipment() {
            let eq_val = self
                .allocate_equipment_from_item(item_id)
                .ok_or_else(|| anyhow::anyhow!("All 128 equipment records are occupied"))?;
            self.inventory_items[empty_slot] = eq_val;
        } else {
            self.inventory_items[empty_slot] = item_id as i16;
        }
        Ok(empty_slot)
    }

    /// Sort order of a slot value: weapons, armor, ammo, medicine, other items, key items; then by
    /// item id, and copies of one weapon or armor by their total stats, best first.
    pub(super) fn sort_key(&self, val: i16) -> (u8, u16, i32) {
        let rank = |c: ItemCategory| match c {
            ItemCategory::Handgun => 0,
            ItemCategory::MachineGun => 1,
            ItemCategory::Rifle => 2,
            ItemCategory::Shotgun => 3,
            ItemCategory::GrenadeLauncher => 4,
            ItemCategory::RocketLauncher => 5,
            ItemCategory::MeleeWeapon => 6,
            ItemCategory::Armor => 7,
            ItemCategory::Ammo | ItemCategory::Crate => 8,
            ItemCategory::Consumable => 9,
            ItemCategory::StatUpgrade => 10,
            ItemCategory::Tool => 11,
            ItemCategory::ParasiteEnergy => 12,
            ItemCategory::TradingCard => 13,
            ItemCategory::KeyItem => 14,
            ItemCategory::Empty => 15,
        };
        match val {
            1..=0xFF => (rank(item_metadata(val as u8).category), val as u16, 0),
            0x100..=0x17F => match self.equipment.get((val - 0x100) as usize) {
                Some(rec) if rec.is_active() => {
                    let cat = item_metadata(rec.item_id).category;
                    let cat = if cat.is_equipment() {
                        cat
                    } else if rec.is_armor() {
                        ItemCategory::Armor
                    } else {
                        ItemCategory::MeleeWeapon
                    };
                    (
                        rank(cat),
                        rec.item_id as u16,
                        -rec.effective_stats().iter().sum::<i32>(),
                    )
                }
                _ => (16, val as u16, 0),
            },
            0x200..=0x208 => (rank(ItemCategory::Ammo), val as u16, 0),
            _ => (16, val as u16, 0),
        }
    }

    /// Sorts a list in place, filled slots first (empty ones at the end).
    pub(super) fn sort_values(&self, list: &mut [i16]) {
        list.sort_by_key(|&v| (v == 0, if v == 0 { (0, 0, 0) } else { self.sort_key(v) }));
    }

    /// Sorts Aya's bag. The equipped weapon and armor stay equipped (their slot numbers follow
    /// them), as the game's own Sort does.
    pub fn sort_aya_bag(&mut self) {
        let held = |slot: i8, items: &[i16]| {
            usize::try_from(slot)
                .ok()
                .and_then(|s| items.get(s).copied())
                .filter(|&v| v != 0)
        };
        let weapon = held(self.equipped_weapon_slot, &self.inventory_items);
        let armor = held(self.equipped_armor_slot, &self.inventory_items);
        let mut items = std::mem::take(&mut self.inventory_items);
        self.sort_values(&mut items);
        let find = |v: Option<i16>, old: i8| match v {
            Some(v) => items.iter().position(|&x| x == v).map_or(old, |p| p as i8),
            None => old,
        };
        self.equipped_weapon_slot = find(weapon, self.equipped_weapon_slot);
        self.equipped_armor_slot = find(armor, self.equipped_armor_slot);
        self.inventory_items = items;
    }

    /// Sorts Wayne's storage: his item shelf and his equipment vault.
    pub fn sort_wayne_storage(&mut self) {
        let mut storage = std::mem::take(&mut self.storage);
        let mut special = std::mem::take(&mut self.special_storage);
        self.sort_values(&mut storage);
        self.sort_values(&mut special);
        self.storage = storage;
        self.special_storage = special;
    }
}
