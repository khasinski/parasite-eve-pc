//! pe_stacks/counts v1 and its pre-companion tail record. Counts are per list/item,
//! with extra copies belonging to the first occurrence, exactly as in the game.
use crate::{
    mod_save::{self, ModData},
    save::PeSaveSlot,
};
use anyhow::{ensure, Result};
pub type Counts = [[u8; 256]; 3];
pub fn stackable(id: i16) -> bool {
    if !(1..256).contains(&id) {
        return false;
    }
    let record = crate::definitions::template(id as u8)
        .copied()
        .or_else(|| crate::disc::game_data().and_then(|g| g.item_record(id as u8)));
    if let Some(r) = record {
        return r[6] == 10 || (12..=15).contains(&r[6]);
    }
    // Verified from the USA resident Item_LookupBaseData table. The editor's
    // display metadata has kind zero for ordinary items and cannot be used here.
    matches!(id,0x06..=0x19 | 0x1d..=0x3e | 0xc8..=0xeb)
}
fn decode(b: &[u8]) -> Result<Counts> {
    ensure!(
        b.len() == 204 && b[3] == 0 && b[..3].iter().map(|&v| v as usize).sum::<usize>() <= 100,
        "Unsupported or malformed stack counts"
    );
    let mut counts = [[0; 256]; 3];
    let mut k = 4;
    for l in 0..3 {
        for _ in 0..b[l] {
            let id = b[k] as usize;
            let n = b[k + 1];
            ensure!(
                id != 0 && (1..99).contains(&n) && counts[l][id] == 0,
                "Invalid or duplicate stack pair"
            );
            counts[l][id] = n;
            k += 2;
        }
    }
    Ok(counts)
}
fn encode(c: &Counts) -> Result<Vec<u8>> {
    let mut b = vec![0; 204];
    let mut k = 4;
    for l in 0..3 {
        for id in 1..256 {
            let n = c[l][id];
            if n == 0 {
                continue;
            }
            ensure!(n < 99 && k < 204, "Too many stacks or copies");
            b[l] += 1;
            b[k] = id as u8;
            b[k + 1] = n;
            k += 2;
        }
    }
    Ok(b)
}
pub fn read(s: &PeSaveSlot) -> Result<Option<Counts>> {
    let mut section_counts = None;
    if let Some(c) = &s.companion {
        if let Some((section, b)) = c.section("pe_stacks/counts")? {
            ensure!(
                section.version == 1 && section.reader == 1,
                "Unsupported stack section"
            );
            let counts = decode(&b)?;
            if counts.iter().flatten().any(|&v| v != 0) {
                return Ok(Some(counts));
            }
            section_counts = Some(counts);
        }
    }
    let d = ModData::inspect(&s.raw_bytes)?;
    if let Some(r) = d
        .records
        .iter()
        .find(|r| r.id == mod_save::id("pe_stacks.counts"))
    {
        if section_counts.is_some() && (r.version != 1 || decode(&r.data).is_err()) {
            return Ok(section_counts);
        }
        ensure!(
            d.editable && r.version == 1,
            "Unsupported legacy stack record"
        );
        return decode(&r.data).map(Some);
    }
    Ok(section_counts)
}
pub fn write(s: &mut PeSaveSlot, c: &Counts) -> Result<()> {
    let b = encode(c)?;
    if let Some(companion) = &mut s.companion {
        if let Some((section, _)) = companion.section("pe_stacks/counts")? {
            let mut raw = s.raw_bytes.clone();
            // The game imports a surviving old record when the new section is empty.
            // Keep both copies coherent so clearing the last stack cannot resurrect it.
            if ModData::inspect(&raw)?.records.iter().any(|r| {
                r.id == mod_save::id("pe_stacks.counts") && r.version == 1 && r.data.len() == 204
            }) {
                mod_save::replace_record(&mut raw, "pe_stacks.counts", 1, &b, None)?;
            }
            companion.replace(&section, &b)?;
            s.raw_bytes = raw;
            return Ok(());
        }
    }
    mod_save::replace_record(&mut s.raw_bytes, "pe_stacks.counts", 1, &b, None)
}
impl PeSaveSlot {
    pub(crate) fn prepare_companion_inventory(&mut self) -> Result<()> {
        self.validate_mod_inventory()?;
        let Some(companion) = &mut self.companion else {
            return Ok(());
        };
        let Some((section, mut ledger)) = companion.section("pe_host/content")? else {
            return Ok(());
        };
        if ledger.iter().all(|&v| v == 0) {
            return Ok(());
        }
        ensure!(
            &ledger[..4] == b"PEC1"
                && ledger[12] <= 12
                && ledger[13] <= 32
                && ledger[14] <= 16
                && ledger[15] == 0,
            "Invalid custom-content ledger"
        );
        let mut current = vec![0; 4096];
        for (i, r) in self.equipment.iter().enumerate() {
            r.write(&mut current[i * 32..i * 32 + 32]);
        }
        ensure!(
            companion.equipment_snapshot.len() == 4096,
            "Custom-item identity baseline is missing; reopen the original paired save"
        );
        let baseline = &companion.equipment_snapshot;
        for i in 0..ledger[12] as usize {
            let p = 16 + i * 52;
            ensure!(
                ledger[p] != 0
                    && ledger[p..p + 48].contains(&0)
                    && (1..=2).contains(&ledger[p + 50]),
                "Invalid content dictionary"
            );
        }
        let mut ids = vec![];
        let mut pools = vec![];
        for i in 0..ledger[13] as usize {
            let p = 640 + i * 16;
            let id = crate::companion::u32at(&ledger, p);
            let pool = ledger[p + 5] as usize;
            ensure!(
                id != 0
                    && id < crate::companion::u32at(&ledger, 4)
                    && pool < 128
                    && ledger[p + 4] < ledger[12]
                    && (1..=4).contains(&ledger[p + 6])
                    && ledger[p + 7] <= 1
                    && ledger[16 + ledger[p + 4] as usize * 52 + 50] == 1
                    && !ids.contains(&id)
                    && !pools.contains(&pool),
                "Invalid content instance"
            );
            ids.push(id);
            pools.push(pool);
            // The editor cannot recreate registered content/effect identity. Preserve its native
            // record instead of silently making the game mark it uncertain or orphaned.
            ensure!(
                baseline[pool * 32..pool * 32 + 32] == current[pool * 32..pool * 32 + 32],
                "Registered custom equipment #{} is protected; edit it in the game",
                pool
            );
        }
        let mut attachments = vec![];
        for i in 0..ledger[14] as usize {
            let p = 1152 + i * 32;
            let id = crate::companion::u32at(&ledger, p);
            let dict = ledger[p + 4] as usize;
            let slot = ledger[p + 5];
            ensure!(
                ids.contains(&id)
                    && dict < (ledger[12] as usize)
                    && ledger[16 + dict * 52 + 50] == 2
                    && slot < 10
                    && ledger[p + 7] <= 1
                    && !attachments.contains(&(id, slot)),
                "Invalid content attachment"
            );
            attachments.push((id, slot));
        }
        fn stamp(pool: &[u8]) -> u32 {
            let mut h = 2166136261u32;
            for r in pool.chunks_exact(32) {
                let f = [r[2], r[3], r[4], r[6], r[1], r[7], r[8], r[9]]
                    .iter()
                    .fold(2166136261u32, |h, &v| (h ^ v as u32).wrapping_mul(16777619));
                h = (h ^ r[0] as u32).wrapping_mul(16777619);
                h = (h ^ f).wrapping_mul(16777619);
            }
            h
        }
        if crate::companion::u32at(&ledger, 8) == stamp(baseline) {
            crate::companion::put32(&mut ledger, 8, stamp(&current));
            companion.replace(&section, &ledger)?;
        }
        companion.equipment_snapshot = current;
        Ok(())
    }
    pub fn item_list(&self, list: usize) -> &[i16] {
        match list {
            0 => &self.inventory_items,
            1 => &self.storage,
            2 => &self.special_storage,
            _ => &[],
        }
    }
    fn item_list_mut(&mut self, list: usize) -> &mut [i16] {
        match list {
            0 => &mut self.inventory_items,
            1 => &mut self.storage,
            2 => &mut self.special_storage,
            _ => &mut [],
        }
    }
    pub fn stack_quantity(&self, list: usize, index: usize) -> Result<u8> {
        let items = self.item_list(list);
        let id = *items
            .get(index)
            .ok_or_else(|| anyhow::anyhow!("Invalid inventory slot"))?;
        if id == 0 {
            return Ok(0);
        }
        let extras = if (1..256).contains(&id) && items.iter().position(|&v| v == id) == Some(index)
        {
            read(self)?.map_or(0, |c| c[list][id as usize])
        } else {
            0
        };
        Ok(1 + extras)
    }
    pub fn set_stack_quantity(&mut self, list: usize, index: usize, quantity: u8) -> Result<()> {
        ensure!(
            (1..=99).contains(&quantity),
            "Quantity must be 1 through 99"
        );
        let items = self.item_list(list);
        let id = *items
            .get(index)
            .ok_or_else(|| anyhow::anyhow!("Invalid inventory slot"))?;
        ensure!(
            (1..256).contains(&id) && items.iter().position(|&v| v == id) == Some(index),
            "Choose the first slot containing this item"
        );
        ensure!(stackable(id), "This item cannot stack");
        ensure!(
            items.iter().filter(|&&v| v == id).count() + quantity as usize - 1 <= 99,
            "Total copies of this item would exceed 99"
        );
        // Do not create new provider state without a supported existing stack store.
        let mut c = read(self)?.ok_or_else(|| {
            anyhow::anyhow!("This save has no stack data; enable Stacks and save in the game first")
        })?;
        c[list][id as usize] = quantity - 1;
        write(self, &c)
    }
    pub fn replace_inventory_slot(&mut self, list: usize, index: usize, value: i16) -> Result<()> {
        let items = self.item_list(list);
        let old = *items
            .get(index)
            .ok_or_else(|| anyhow::anyhow!("Invalid inventory slot"))?;
        let mut c = read(self)?;
        if old != value
            && (1..256).contains(&old)
            && items.iter().position(|&v| v == old) == Some(index)
        {
            if let Some(c) = &mut c {
                c[list][old as usize] = 0;
            }
        }
        if let Some(c) = c {
            write(self, &c)?;
        }
        self.item_list_mut(list)[index] = value;
        if list == 0 && value != old {
            if self.equipped_weapon_slot == index as i8 {
                self.equipped_weapon_slot = -1
            }
            if self.equipped_armor_slot == index as i8 {
                self.equipped_armor_slot = -1
            }
        }
        Ok(())
    }
    /// Move the selected slot and all its copies. Refuse capacity overflow without changing either list.
    pub fn move_inventory_slot(&mut self, from: usize, index: usize, to: usize) -> Result<()> {
        ensure!(from < 3 && to < 3 && from != to, "Invalid inventory move");
        let id = *self
            .item_list(from)
            .get(index)
            .ok_or_else(|| anyhow::anyhow!("Invalid inventory slot"))?;
        ensure!(id != 0, "Empty slot");
        let mut c = read(self)?;
        let existing = if c.is_some() && stackable(id) {
            self.item_list(to).iter().position(|&v| v == id)
        } else {
            None
        };
        let target = existing
            .or_else(|| self.item_list(to).iter().position(|&v| v == 0))
            .ok_or_else(|| anyhow::anyhow!("Destination is full"))?;
        if let Some(counts) = &mut c {
            if stackable(id) {
                let owns = self.item_list(from).iter().position(|&v| v == id) == Some(index);
                let extra = if owns { counts[from][id as usize] } else { 0 };
                let transferred = extra as u16 + 1;
                let held = self.item_list(to).iter().filter(|&&v| v == id).count() as u16
                    + counts[to][id as usize] as u16;
                ensure!(
                    held + transferred <= 99,
                    "Destination stack would exceed 99 copies"
                );
                if owns {
                    counts[from][id as usize] = 0;
                }
                counts[to][id as usize] += extra + u8::from(existing.is_some());
            }
        }
        if let Some(c) = c {
            write(self, &c)?;
        }
        self.item_list_mut(to)[target] = id;
        self.item_list_mut(from)[index] = 0;
        if from == 0 {
            if self.equipped_weapon_slot == index as i8 {
                self.equipped_weapon_slot = -1
            }
            if self.equipped_armor_slot == index as i8 {
                self.equipped_armor_slot = -1
            }
        }
        Ok(())
    }
    pub(crate) fn validate_mod_inventory(&self) -> Result<()> {
        let linked = crate::companion::link(&self.raw_bytes)?.is_some();
        ensure!(
            linked == self.companion.is_some(),
            "Save requires its matching companion file"
        );
        if let Some(c) = &self.companion {
            c.sections()?;
        }
        if let Some(c) = read(self)? {
            for l in 0..3 {
                for id in 1..256 {
                    ensure!(
                        c[l][id] == 0 || self.item_list(l).contains(&(id as i16)),
                        "Orphan stack count for item 0x{:02X} in list {}",
                        id,
                        l
                    );
                }
            }
        }
        Ok(())
    }
}
