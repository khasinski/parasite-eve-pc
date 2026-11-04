//! Read-only game data from the player's own Parasite Eve (USA) disc image.
//!
//! The game creates a weapon or armor by copying the item's 32-byte base record verbatim
//! (`Inv_FindSlotByIndex`, `active_list.c`), and recomputes Aya's bag size from the Item Capacity
//! stat (`Inv_RecalcSlotStats`, `stats.c`). Both tables live in the resident pack of `PE.IMG`
//! (entry 2, `g_PeImageSectorTable` in `scene_assets.c`), so the editor reads them from the disc
//! instead of shipping copies. Nothing is written back to the image.

use std::fs::File;
use std::io::{Read, Seek, SeekFrom};
use std::path::{Path, PathBuf};
use std::sync::{Arc, RwLock};

use anyhow::{bail, Context, Result};

const RAW_SECTOR: u64 = 2352;
const ISO_SECTOR: usize = 2048;
const SYNC: [u8; 12] = [
    0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0,
];

/// Resident pack: `PE.IMG` sectors `g_PeImageSectorTable[2]..[3]` (USA).
const RESIDENT_FIRST_SECTOR: u32 = 72;
const RESIDENT_SECTOR_COUNT: usize = 87 - 72;

/// Pack directory: byte offsets of the tables inside the resident pack.
const DIR_ITEM_BASE_DATA: usize = 0x0C;
const DIR_STAT_GROWTH: usize = 0x10;
const DIR_LEVEL_STATS: usize = 0x14;

pub const ITEM_RECORD_SIZE: usize = 0x20;
const GROWTH_ENTRIES: usize = 128;
const STAT_CATEGORIES: usize = 7;
const LEVEL_COUNT: usize = 0x63;
const LEVEL_STATS_SIZE: usize = 0x18;
const LEVEL_STATS_HP: usize = 0x00;
const LEVEL_STATS_INVENTORY_CAPACITY: usize = 0x07;
pub const HP_STAT: usize = 0;
pub const ITEM_CAPACITY_STAT: usize = 6;

/// The tables the editor needs, read from one disc.
#[derive(Debug, Clone)]
pub struct GameData {
    pub source: PathBuf,
    /// Base records for item ids 1..=255, indexed by `item_id - 1` (`Item_LookupBaseData`).
    items: Vec<[u8; ITEM_RECORD_SIZE]>,
    /// Stat growth thresholds per stat category (`Aya_GetStatGrowthTable`).
    growth: Vec<[i32; GROWTH_ENTRIES]>,
    /// `AyaLevelStats.inventoryCapacity` for levels 0..=98.
    capacity: [u8; LEVEL_COUNT],
    /// `AyaLevelStats.hp` for levels 0..=98.
    hp: [u16; LEVEL_COUNT],
}

impl GameData {
    /// The game's base record for `item_id`, or `None` when the disc has no record for it.
    pub fn item_record(&self, item_id: u8) -> Option<[u8; ITEM_RECORD_SIZE]> {
        let rec = self.items.get((item_id as usize).checked_sub(1)?)?;
        (rec[0x04] == item_id).then_some(*rec)
    }

    /// `Aya_StatQueryLevelAndSubLevel`: the level a stat value reaches (0..=98).
    pub fn stat_level(&self, category: usize, value: u16) -> usize {
        let table = &self.growth[category];
        let value = value as i16 as i32;
        let mut index = 0x40usize;
        let mut step = 0x20usize;
        loop {
            if value >= table[index] {
                index += step;
            } else if value < table[index - 1] {
                index -= step;
            } else {
                step = 0;
            }
            step >>= 1;
            if step == 0 {
                break;
            }
        }
        (index as isize - 1).clamp(0, LEVEL_COUNT as isize - 1) as usize
    }

    /// The bag size the game sets from an Item Capacity stat value (`Inv_RecalcSlotStats`).
    pub fn bag_slots_for_stat(&self, value: u16) -> u8 {
        self.capacity[self.stat_level(ITEM_CAPACITY_STAT, value)]
    }

    /// The smallest Item Capacity stat value that gives at least `slots`, if any does.
    pub fn stat_for_bag_slots(&self, slots: u8) -> Option<u16> {
        let level = self.capacity.iter().position(|&c| c >= slots)?;
        let threshold = self.growth[ITEM_CAPACITY_STAT][level];
        u16::try_from(threshold.max(0)).ok()
    }

    /// The largest bag the Item Capacity stat can give.
    pub fn max_bag_slots(&self) -> u8 {
        self.capacity
            .iter()
            .copied()
            .max()
            .unwrap_or(MAX_STAT_BAG_SLOTS)
    }

    /// The max HP the game sets from an HP stat value (`Inv_RecalcSlotStats`): the level's HP,
    /// times 23/20 while the equipped armor has HP Max Up (`BattleCmd_LoadWeaponModifiers`).
    pub fn max_hp_for_stat(&self, value: u16, hp_max_up: bool) -> u16 {
        hp_with_armor(self.hp[self.stat_level(HP_STAT, value)], hp_max_up)
    }

    /// The smallest HP stat value that gives at least `max_hp`, if any does.
    pub fn stat_for_max_hp(&self, max_hp: u16, hp_max_up: bool) -> Option<u16> {
        let level = self
            .hp
            .iter()
            .position(|&hp| hp_with_armor(hp, hp_max_up) >= max_hp)?;
        u16::try_from(self.growth[HP_STAT][level].max(0)).ok()
    }

    /// Builds the tables from a resident pack (`PE.IMG` entry 2) and checks they look right.
    pub fn from_resident_pack(source: PathBuf, pack: &[u8]) -> Result<Self> {
        let dir = |off: usize| -> Result<usize> {
            let v = i32::from_le_bytes(pack[off..off + 4].try_into().unwrap());
            usize::try_from(v)
                .ok()
                .filter(|&v| v < pack.len())
                .with_context(|| {
                    format!(
                        "resident pack directory entry 0x{:02X} is out of range",
                        off
                    )
                })
        };
        if pack.len() < 0x30 {
            bail!("resident pack is too short");
        }
        let item_base = dir(DIR_ITEM_BASE_DATA)?;
        let growth_base = dir(DIR_STAT_GROWTH)?;
        let level_base = dir(DIR_LEVEL_STATS)?;
        if growth_base + STAT_CATEGORIES * GROWTH_ENTRIES * 4 > pack.len()
            || level_base + LEVEL_COUNT * LEVEL_STATS_SIZE > pack.len()
        {
            bail!("resident pack tables run past its end");
        }

        let items = (0..255)
            .map_while(|i| {
                let off = item_base + i * ITEM_RECORD_SIZE;
                pack.get(off..off + ITEM_RECORD_SIZE)
                    .map(|r| r.try_into().unwrap())
            })
            .collect::<Vec<[u8; ITEM_RECORD_SIZE]>>();
        let growth = (0..STAT_CATEGORIES)
            .map(|c| {
                let mut t = [0i32; GROWTH_ENTRIES];
                for (i, v) in t.iter_mut().enumerate() {
                    let off = growth_base + c * GROWTH_ENTRIES * 4 + i * 4;
                    *v = i32::from_le_bytes(pack[off..off + 4].try_into().unwrap());
                }
                t
            })
            .collect::<Vec<_>>();
        let mut capacity = [0u8; LEVEL_COUNT];
        for (lv, c) in capacity.iter_mut().enumerate() {
            *c = pack[level_base + lv * LEVEL_STATS_SIZE + LEVEL_STATS_INVENTORY_CAPACITY];
        }
        let mut hp = [0u16; LEVEL_COUNT];
        for (lv, h) in hp.iter_mut().enumerate() {
            let off = level_base + lv * LEVEL_STATS_SIZE + LEVEL_STATS_HP;
            *h = u16::from_le_bytes([pack[off], pack[off + 1]]);
        }

        let data = Self {
            source,
            items,
            growth,
            capacity,
            hp,
        };
        // Every weapon and armor id (0x3F..=0xC7) has its own record on the USA disc; another
        // region's PE.IMG puts other data at these sectors.
        let gear = (0x3Fu8..=0xC7)
            .filter(|&id| data.item_record(id).is_some())
            .count();
        if gear < 0xC7 - 0x3F + 1
            || data.capacity[0] == 0
            || data.growth[ITEM_CAPACITY_STAT][0] != 0
        {
            bail!("this does not look like the Parasite Eve (USA) disc (item table check failed)");
        }
        Ok(data)
    }

    /// Reads the tables from a disc image: `.cue`, raw `.bin` (2352-byte sectors) or `.iso`.
    pub fn from_disc_image(path: &Path) -> Result<Self> {
        let mut disc = DiscImage::open(path)?;
        let (pe_img_lba, pe_img_size) = disc
            .find_root_file("PE.IMG")?
            .context("PE.IMG is not on this disc (is it Parasite Eve?)")?;
        if ((RESIDENT_FIRST_SECTOR as usize + RESIDENT_SECTOR_COUNT) * ISO_SECTOR) as u32
            > pe_img_size
        {
            bail!("PE.IMG is smaller than expected");
        }
        let mut pack = Vec::with_capacity(RESIDENT_SECTOR_COUNT * ISO_SECTOR);
        for s in 0..RESIDENT_SECTOR_COUNT as u32 {
            pack.extend_from_slice(&disc.read_sector(pe_img_lba + RESIDENT_FIRST_SECTOR + s)?);
        }
        Self::from_resident_pack(path.to_path_buf(), &pack)
    }
}

/// Stat value of the top growth level (98); the HP and Item Capacity tables both reach it here.
pub const TOP_LEVEL_STAT: u16 = 980;

/// Max HP at the top HP level without armor help; HP Max Up armor makes it 999.
pub const MAX_STAT_MAX_HP: u16 = 869;

/// `Inv_RecalcSlotStats`: `hp * (20 + g_StatScaleBase[0]) / 20`, the scale being 3 for HP Max Up.
pub fn hp_with_armor(hp: u16, hp_max_up: bool) -> u16 {
    (hp as u32 * if hp_max_up { 23 } else { 20 } / 20) as u16
}

/// The bag size the Item Capacity stat gives at its top level (Aya's own slots; armor mods add
/// up to 50 on top while equipped, and the save stores only Aya's own count).
pub const MAX_STAT_BAG_SLOTS: u8 = 46;

struct DiscImage {
    file: File,
    /// Byte offset of the 2048 user bytes inside each stored sector, and the stored sector size.
    data_offset: u64,
    sector_size: u64,
}

impl DiscImage {
    fn open(path: &Path) -> Result<Self> {
        let is_cue = path
            .extension()
            .is_some_and(|e| e.eq_ignore_ascii_case("cue"));
        let image = if is_cue {
            cue_data_file(path)?
        } else {
            path.to_path_buf()
        };
        let mut file =
            File::open(&image).with_context(|| format!("cannot open {}", image.display()))?;

        // Raw images start every sector with the sync pattern; mode 1 keeps user data at 16,
        // mode 2 (XA form 1, as on PlayStation discs) at 24.
        let mut head = [0u8; 16];
        file.seek(SeekFrom::Start(16 * RAW_SECTOR))?;
        let raw = file.read_exact(&mut head).is_ok() && head[..12] == SYNC;
        let (data_offset, sector_size) = match (raw, head[15]) {
            (true, 1) => (16, RAW_SECTOR),
            (true, _) => (24, RAW_SECTOR),
            (false, _) => (0, ISO_SECTOR as u64),
        };
        Ok(Self {
            file,
            data_offset,
            sector_size,
        })
    }

    fn read_sector(&mut self, lba: u32) -> Result<[u8; ISO_SECTOR]> {
        let mut buf = [0u8; ISO_SECTOR];
        self.file.seek(SeekFrom::Start(
            lba as u64 * self.sector_size + self.data_offset,
        ))?;
        self.file
            .read_exact(&mut buf)
            .with_context(|| format!("disc image ends before sector {}", lba))?;
        Ok(buf)
    }

    /// Looks `name` up in the ISO 9660 root directory; returns its first sector and size.
    fn find_root_file(&mut self, name: &str) -> Result<Option<(u32, u32)>> {
        let pvd = self.read_sector(16)?;
        if &pvd[1..6] != b"CD001" {
            bail!("not a CD-ROM image (no ISO 9660 volume descriptor)");
        }
        let root = &pvd[156..156 + 34];
        let root_lba = u32::from_le_bytes(root[2..6].try_into().unwrap());
        let root_size = u32::from_le_bytes(root[10..14].try_into().unwrap());

        let sectors = root_size.div_ceil(ISO_SECTOR as u32);
        for s in 0..sectors {
            let sector = self.read_sector(root_lba + s)?;
            let mut at = 0usize;
            while at + 33 < ISO_SECTOR {
                let len = sector[at] as usize;
                if len == 0 {
                    break;
                }
                let name_len = sector[at + 32] as usize;
                let id = sector.get(at + 33..at + 33 + name_len).unwrap_or(&[]);
                let id = id.split(|&b| b == b';').next().unwrap_or(&[]);
                if id.eq_ignore_ascii_case(name.as_bytes()) {
                    let lba = u32::from_le_bytes(sector[at + 2..at + 6].try_into().unwrap());
                    let size = u32::from_le_bytes(sector[at + 10..at + 14].try_into().unwrap());
                    return Ok(Some((lba, size)));
                }
                at += len;
            }
        }
        Ok(None)
    }
}

/// The first `FILE` of a cue sheet (the data track), relative to the sheet.
fn cue_data_file(cue: &Path) -> Result<PathBuf> {
    let text =
        std::fs::read_to_string(cue).with_context(|| format!("cannot read {}", cue.display()))?;
    for line in text.lines() {
        let line = line.trim();
        let Some(rest) = line
            .strip_prefix("FILE")
            .or_else(|| line.strip_prefix("file"))
        else {
            continue;
        };
        let rest = rest.trim();
        let name = if let Some(quoted) = rest.strip_prefix('"') {
            quoted.split('"').next().unwrap_or("")
        } else {
            rest.split_whitespace().next().unwrap_or("")
        };
        if !name.is_empty() {
            return Ok(cue.parent().unwrap_or(Path::new(".")).join(name));
        }
    }
    bail!("{} has no FILE line", cue.display())
}

/// Disc images named in the game's own settings (`[discs]` in `parasite-eve.toml`).
pub fn configured_disc_images() -> Vec<PathBuf> {
    let mut out = Vec::new();
    for toml in game_config_files() {
        let Ok(text) = std::fs::read_to_string(&toml) else {
            continue;
        };
        for disc in toml_discs(&text) {
            if !out.contains(&disc) {
                out.push(disc);
            }
        }
    }
    out
}

/// Where the game keeps `parasite-eve.toml` (mirrors `main.c`'s options lookup): the user
/// settings folder (see `card_location`), then the files beside the game.
fn game_config_files() -> Vec<PathBuf> {
    let mut dirs = Vec::new();
    if let Some(settings) = crate::card_location::settings_dir() {
        dirs.push(settings);
    }
    if let Some(exe_dir) = std::env::current_exe()
        .ok()
        .and_then(|p| p.parent().map(Path::to_path_buf))
    {
        dirs.push(exe_dir);
    }
    dirs.push(PathBuf::from("."));
    dirs.into_iter()
        .map(|d| d.join("parasite-eve.toml"))
        .collect()
}

/// `disc1`/`disc2` strings from the `[discs]` table of a TOML file.
fn toml_discs(text: &str) -> Vec<PathBuf> {
    let mut in_discs = false;
    let mut out = Vec::new();
    for line in text.lines() {
        let line = line.trim();
        if line.starts_with('[') {
            in_discs = line == "[discs]";
            continue;
        }
        if !in_discs {
            continue;
        }
        let Some((key, value)) = line.split_once('=') else {
            continue;
        };
        if !key.trim().starts_with("disc") {
            continue;
        }
        if let Some(s) = toml_string(value.trim()) {
            if !s.is_empty() {
                out.push(PathBuf::from(s));
            }
        }
    }
    out
}

fn toml_string(v: &str) -> Option<String> {
    if let Some(lit) = v.strip_prefix('\'') {
        return lit.split('\'').next().map(str::to_string);
    }
    let body = v.strip_prefix('"')?;
    let mut out = String::new();
    let mut chars = body.chars();
    while let Some(c) = chars.next() {
        match c {
            '"' => return Some(out),
            '\\' => match chars.next()? {
                'n' => out.push('\n'),
                't' => out.push('\t'),
                other => out.push(other),
            },
            c => out.push(c),
        }
    }
    None
}

static GAME_DATA: RwLock<Option<Arc<GameData>>> = RwLock::new(None);

/// The loaded game data, if a disc has been read.
pub fn game_data() -> Option<Arc<GameData>> {
    GAME_DATA.read().ok()?.clone()
}

pub fn set_game_data(data: Option<GameData>) {
    if let Ok(mut slot) = GAME_DATA.write() {
        *slot = data.map(Arc::new);
    }
}

/// Reads a disc image and makes its tables the ones the editor uses.
pub fn load_disc(path: &Path) -> Result<Arc<GameData>> {
    let data = GameData::from_disc_image(path)
        .with_context(|| format!("cannot read game data from {}", path.display()))?;
    set_game_data(Some(data));
    Ok(game_data().expect("just set"))
}

/// Loads the first disc named in the game's settings that reads cleanly.
pub fn load_configured_disc() -> Option<Arc<GameData>> {
    configured_disc_images()
        .into_iter()
        .find_map(|p| load_disc(&p).ok())
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A synthetic resident pack: every gear id gets a record, Item Capacity grows by 10 a level
    /// and the bag grows by one slot a level up to 46.
    fn synthetic_pack() -> Vec<u8> {
        let item_base = 0x40usize;
        let growth_base = item_base + 255 * ITEM_RECORD_SIZE;
        let level_base = growth_base + STAT_CATEGORIES * GROWTH_ENTRIES * 4;
        let mut pack = vec![0u8; level_base + LEVEL_COUNT * LEVEL_STATS_SIZE];
        pack[DIR_ITEM_BASE_DATA..DIR_ITEM_BASE_DATA + 4]
            .copy_from_slice(&(item_base as i32).to_le_bytes());
        pack[DIR_STAT_GROWTH..DIR_STAT_GROWTH + 4]
            .copy_from_slice(&(growth_base as i32).to_le_bytes());
        pack[DIR_LEVEL_STATS..DIR_LEVEL_STATS + 4]
            .copy_from_slice(&(level_base as i32).to_le_bytes());
        for id in 1..=255usize {
            let r = item_base + (id - 1) * ITEM_RECORD_SIZE;
            pack[r] = (id % 7) as u8 + 1;
            pack[r + 0x04] = id as u8;
            pack[r + 0x06] = 3;
        }
        for c in 0..STAT_CATEGORIES {
            for i in 0..GROWTH_ENTRIES {
                let v: i32 = if i < LEVEL_COUNT {
                    i as i32 * 10
                } else {
                    32767
                };
                let off = growth_base + (c * GROWTH_ENTRIES + i) * 4;
                pack[off..off + 4].copy_from_slice(&v.to_le_bytes());
            }
        }
        for lv in 0..LEVEL_COUNT {
            pack[level_base + lv * LEVEL_STATS_SIZE + LEVEL_STATS_INVENTORY_CAPACITY] =
                (10 + lv).min(46) as u8;
            let off = level_base + lv * LEVEL_STATS_SIZE + LEVEL_STATS_HP;
            pack[off..off + 2].copy_from_slice(&(45 + lv as u16 * 8).to_le_bytes());
        }
        pack
    }

    #[test]
    fn reads_records_and_bag_table_from_a_resident_pack() {
        let data =
            GameData::from_resident_pack(PathBuf::from("synthetic"), &synthetic_pack()).unwrap();
        let p90 = data.item_record(0x78).unwrap();
        assert_eq!(p90[0x04], 0x78);
        assert_eq!(p90[0x06], 3);
        assert_eq!(data.stat_level(ITEM_CAPACITY_STAT, 0), 0);
        assert_eq!(data.stat_level(ITEM_CAPACITY_STAT, 15), 1);
        assert_eq!(data.stat_level(ITEM_CAPACITY_STAT, 1000), 98);
        assert_eq!(data.bag_slots_for_stat(0), 10);
        assert_eq!(data.bag_slots_for_stat(1000), 46);
        assert_eq!(data.stat_for_bag_slots(46), Some(360));
        assert_eq!(data.stat_for_bag_slots(50), None);
        assert_eq!(data.max_bag_slots(), 46);
        assert_eq!(data.max_hp_for_stat(0, false), 45);
        assert_eq!(data.max_hp_for_stat(1000, false), 829);
        assert_eq!(data.max_hp_for_stat(1000, true), 953);
        assert_eq!(data.stat_for_max_hp(100, false), Some(70));
        assert_eq!(data.stat_for_max_hp(900, true), Some(930));
        assert_eq!(data.stat_for_max_hp(999, false), None);
    }

    #[test]
    fn rejects_a_pack_without_gear_records() {
        let mut pack = synthetic_pack();
        let r = 0x40 + (0x78 - 1) * ITEM_RECORD_SIZE;
        pack[r + 0x04] = 0;
        assert!(GameData::from_resident_pack(PathBuf::from("synthetic"), &pack).is_err());
    }

    #[test]
    fn reads_disc_paths_from_game_settings() {
        let text = "[video]\nscale = 2\n[discs]\ndisc1 = \"D:\\\\Games\\\\PE (Disc 1).cue\"\ndisc2 = 'E:\\PE2.cue'\n[audio]\ndisc3 = \"no\"\n";
        assert_eq!(
            toml_discs(text),
            vec![
                PathBuf::from("D:\\Games\\PE (Disc 1).cue"),
                PathBuf::from("E:\\PE2.cue")
            ]
        );
    }

    /// Set `PE_DISC` to a Parasite Eve (USA) `.cue`/`.bin` to check the real tables.
    #[test]
    fn real_disc_when_available() {
        let Some(path) = std::env::var_os("PE_DISC") else {
            return;
        };
        let data = GameData::from_disc_image(Path::new(&path)).expect("read PE_DISC");
        let p90 = data.item_record(0x78).unwrap();
        assert_eq!((p90[0x00], p90[0x06]), (4, 3), "P90 icon and kind");
        assert_eq!(data.max_bag_slots(), MAX_STAT_BAG_SLOTS);
        assert_eq!(data.bag_slots_for_stat(1000), MAX_STAT_BAG_SLOTS);
        assert_eq!(data.bag_slots_for_stat(0), 10);
        assert_eq!(data.max_hp_for_stat(0, false), 45);
        assert_eq!(data.max_hp_for_stat(TOP_LEVEL_STAT, false), MAX_STAT_MAX_HP);
        assert_eq!(data.max_hp_for_stat(TOP_LEVEL_STAT, true), 999);
        assert_eq!(data.stat_for_max_hp(999, true), Some(TOP_LEVEL_STAT));

        // The editor writes mod bytes as the game's own records hold them. One record keeps a
        // 3 Commands without its group (a quirk of the original data), which is allowed.
        for id in 0x3Fu8..=0xC7 {
            let rec = data.item_record(id).unwrap();
            let armor = rec[0x06] == 9;
            for &b in &rec[0x15..0x15 + (rec[0x14] as usize).min(11)] {
                if b & 0x1F == 0 || (b == 0x14 && !armor) {
                    continue;
                }
                assert_eq!(
                    crate::tables::encode_mod_byte(b & 0x1F, armor),
                    b,
                    "item 0x{:02X}",
                    id
                );
            }
        }
    }
}
