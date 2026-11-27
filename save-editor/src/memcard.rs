use std::fs;
use std::path::{Path, PathBuf};

use anyhow::{bail, Context, Result};

use crate::crc::{update_mc_frame_xor, SAVE_BLOCK_SIZE};
use crate::save::PeSaveSlot;

pub const PS1_CARD_SIZE: usize = 0x20000; // 131,072 bytes
pub const GME_HEADER_SIZE: usize = 0x0F40; // 3,904 bytes
pub const GME_CARD_SIZE: usize = PS1_CARD_SIZE + GME_HEADER_SIZE; // 134,976 bytes
pub const MCS_SAVE_SIZE: usize = SAVE_BLOCK_SIZE + 0x80; // 8,320 bytes
pub const MAX_CARD_SLOTS: usize = 15;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ContainerKind {
    /// PsyZ host directory (`bu00/` or `bu10/`) containing `BASLUS-00662000000<0|1><A..O>` files
    PsyzDirectory,
    /// Single raw `0x2000`-byte PsyZ save file
    RawSlotFile,
    /// Single `.mcs` `0x2080`-byte PS1 save file (128-byte directory header + `0x2000` block)
    McsSingleFile,
    /// Standard 128 KB PS1 memory card image (`.srm`, `.mcr`, `.mcd`)
    Ps1CardImage,
    /// DexDrive `.gme` memory card image (`0xF40` header + 128 KB card)
    DexDriveGmeImage,
}

impl ContainerKind {
    pub fn label(&self) -> &'static str {
        match self {
            Self::PsyzDirectory => "PsyZ Directory (bu00 / bu10)",
            Self::RawSlotFile => "Raw PsyZ Save Slot (8 KB)",
            Self::McsSingleFile => "PS1 Single Save (.mcs)",
            Self::Ps1CardImage => "PS1 Memory Card Image (.srm / .mcr / .mcd)",
            Self::DexDriveGmeImage => "DexDrive Memory Card (.gme)",
        }
    }
}

#[derive(Debug, Clone)]
pub struct MemoryCardContainer {
    pub path: PathBuf,
    pub kind: ContainerKind,
    /// 15 slots (`0..15` corresponding to `'A'..'O'`)
    pub slots: [Option<PeSaveSlot>; MAX_CARD_SLOTS],
    /// Underlying raw card bytes for `.srm`/`.mcr`/`.gme`/`.mcs` so non-PE slots and headers are preserved
    raw_container_bytes: Vec<u8>,
}

impl MemoryCardContainer {
    #[cfg(test)]
    pub(crate) fn raw_container_bytes_for_test(&mut self, bytes: Vec<u8>) {
        self.raw_container_bytes = bytes;
    }
    /// Opens a PsyZ directory (`bu00`/`bu10`), a raw PsyZ slot file, a `.mcs` single save,
    /// or a full PS1 memory card image (`.srm`, `.mcr`, `.mcd`, `.gme`).
    pub fn open(path: impl AsRef<Path>) -> Result<Self> {
        let mut card = Self::open_inner(path)?;
        let aggregate = !matches!(
            card.kind,
            ContainerKind::PsyzDirectory | ContainerKind::RawSlotFile
        );
        for slot in card.slots.iter_mut().flatten() {
            let native = if card.kind == ContainerKind::PsyzDirectory {
                card.path.join(&slot.filename)
            } else {
                card.path.clone()
            };
            let name = if card.kind == ContainerKind::RawSlotFile {
                native
                    .file_name()
                    .and_then(|s| s.to_str())
                    .context("Invalid save name")?
            } else {
                &slot.filename
            };
            slot.companion = crate::companion::load(
                &slot.raw_bytes,
                &crate::companion::directory(&native, aggregate),
                name,
            )?;
            slot.validate_mod_inventory()?;
        }
        Ok(card)
    }
    fn open_inner(path: impl AsRef<Path>) -> Result<Self> {
        let path = path.as_ref().to_path_buf();
        if path.is_dir() {
            return Self::open_psyz_dir(path);
        }
        if !path.exists() {
            bail!("Path does not exist: {}", path.display());
        }
        let bytes =
            fs::read(&path).with_context(|| format!("Failed to read file {}", path.display()))?;

        match bytes.len() {
            SAVE_BLOCK_SIZE => {
                let fname = path
                    .file_name()
                    .and_then(|s| s.to_str())
                    .unwrap_or("BASLUS-006620000000A")
                    .to_string();
                let slot_idx = slot_index_from_filename(&fname).unwrap_or(0);
                let slot = PeSaveSlot::from_bytes(&bytes, slot_idx, fname)?;
                let mut slots: [Option<PeSaveSlot>; MAX_CARD_SLOTS] = Default::default();
                slots[slot_idx] = Some(slot);
                Ok(Self {
                    path,
                    kind: ContainerKind::RawSlotFile,
                    slots,
                    raw_container_bytes: bytes,
                })
            }
            MCS_SAVE_SIZE => {
                let dir_frame = &bytes[..0x80];
                let fname = extract_ascii_filename(&dir_frame[0x0A..0x1E]);
                let slot_idx = slot_index_from_filename(&fname).unwrap_or(0);
                let slot = PeSaveSlot::from_bytes(&bytes[0x80..], slot_idx, fname)?;
                let mut slots: [Option<PeSaveSlot>; MAX_CARD_SLOTS] = Default::default();
                slots[slot_idx] = Some(slot);
                Ok(Self {
                    path,
                    kind: ContainerKind::McsSingleFile,
                    slots,
                    raw_container_bytes: bytes,
                })
            }
            PS1_CARD_SIZE => Self::from_ps1_card_bytes(path, ContainerKind::Ps1CardImage, bytes, 0),
            GME_CARD_SIZE => Self::from_ps1_card_bytes(
                path,
                ContainerKind::DexDriveGmeImage,
                bytes,
                GME_HEADER_SIZE,
            ),
            other => bail!(
                "Unsupported save file size ({} bytes) for {}. Expected 8192 (PsyZ slot), 8320 (.mcs), 131072 (.srm/.mcr/.mcd), or 134976 (.gme).",
                other,
                path.display()
            ),
        }
    }

    pub fn open_psyz_dir(dir: PathBuf) -> Result<Self> {
        if !dir.exists() {
            fs::create_dir_all(&dir)
                .with_context(|| format!("Failed to create PsyZ directory {}", dir.display()))?;
        }
        let mut slots: [Option<PeSaveSlot>; MAX_CARD_SLOTS] = Default::default();

        let mut entries: Vec<_> = fs::read_dir(&dir)
            .with_context(|| format!("Failed to read PsyZ directory {}", dir.display()))?
            .filter_map(|e| e.ok())
            .collect();
        entries.sort_by_key(|e| e.file_name());

        for entry in entries {
            let file_path = entry.path();
            if !file_path.is_file() {
                continue;
            }
            let fname = entry.file_name().to_string_lossy().to_string();
            let meta = match entry.metadata() {
                Ok(m) => m,
                Err(_) => continue,
            };
            if meta.len() < SAVE_BLOCK_SIZE as u64 {
                continue;
            }
            let bytes = match fs::read(&file_path) {
                Ok(b) => b,
                Err(_) => continue,
            };
            if bytes.len() < SAVE_BLOCK_SIZE || bytes[0] != b'S' || bytes[1] != b'C' {
                continue;
            }

            let slot_idx = slot_index_from_filename(&fname).unwrap_or_else(|| {
                // Fallback for 19-char truncated PsyZ filename (`BASLUS-006620000000`): find first free slot
                slots.iter().position(|s| s.is_none()).unwrap_or(0)
            });

            if let Ok(slot) = PeSaveSlot::from_bytes(&bytes[..SAVE_BLOCK_SIZE], slot_idx, &fname) {
                slots[slot_idx] = Some(slot);
            }
        }

        Ok(Self {
            path: dir,
            kind: ContainerKind::PsyzDirectory,
            slots,
            raw_container_bytes: Vec::new(),
        })
    }

    fn from_ps1_card_bytes(
        path: PathBuf,
        kind: ContainerKind,
        bytes: Vec<u8>,
        card_offset: usize,
    ) -> Result<Self> {
        let card = &bytes[card_offset..card_offset + PS1_CARD_SIZE];
        if &card[0..2] != b"MC" {
            bail!(
                "Invalid PS1 Memory Card header in {}: expected 'MC' at offset 0x{:X}",
                path.display(),
                card_offset
            );
        }

        let mut slots: [Option<PeSaveSlot>; MAX_CARD_SLOTS] = Default::default();
        for i in 0..MAX_CARD_SLOTS {
            let dir_offset = (i + 1) * 0x80;
            let dir_frame = &card[dir_offset..dir_offset + 0x80];
            let alloc_state = dir_frame[0];
            // 0x51 = active first block of a save
            if alloc_state != 0x51 {
                continue;
            }
            let fname = extract_ascii_filename(&dir_frame[0x0A..0x1E]);
            let data_offset = (i + 1) * SAVE_BLOCK_SIZE;
            let block = &card[data_offset..data_offset + SAVE_BLOCK_SIZE];
            if block[0] != b'S' || block[1] != b'C' {
                continue;
            }
            // Check if it is a Parasite Eve save (or parse any valid PE-compatible block)
            if fname.starts_with("BASLUS-00662")
                || fname.starts_with("BISLPS-0123")
                || fname.is_empty()
            {
                let slot_idx = slot_index_from_filename(&fname).unwrap_or(i);
                let target_idx = if slots[slot_idx].is_none() {
                    slot_idx
                } else {
                    i
                };
                let normalized_name = if fname.is_empty() {
                    format!("BASLUS-006620000000{}", (b'A' + target_idx as u8) as char)
                } else {
                    fname
                };
                if let Ok(mut parsed) = PeSaveSlot::from_bytes(block, target_idx, normalized_name) {
                    parsed.slot_index = target_idx;
                    slots[target_idx] = Some(parsed);
                }
            }
        }

        Ok(Self {
            path,
            kind,
            slots,
            raw_container_bytes: bytes,
        })
    }

    /// Saves all modified slots back to the current container on disk (`bu00` directory, `.srm`, `.mcr`, `.gme`, `.mcs`, or raw file).
    pub fn save(&mut self) -> Result<()> {
        let mut staged = self.clone();
        staged.validate_slots()?;
        match staged.kind {
            ContainerKind::PsyzDirectory => {
                for i in 0..MAX_CARD_SLOTS {
                    if let Some(slot) = &mut staged.slots[i] {
                        slot.slot_index = i;
                        slot.to_bytes();
                        let target = staged.path.join(&slot.filename);
                        write_slot(slot, &target, true)?;
                        // Retire the alternate only after successful publication.
                        for style in ['0', '1'] {
                            let other = staged.path.join(format!(
                                "BASLUS-00662000000{}{}",
                                style,
                                (b'A' + i as u8) as char
                            ));
                            if other != target && other.exists() {
                                fs::remove_file(&other).with_context(|| {
                                    format!(
                                        "Saved {}, but could not retire {}",
                                        target.display(),
                                        other.display()
                                    )
                                })?;
                            }
                        }
                    }
                }
            }
            ContainerKind::RawSlotFile => {
                let slot = staged
                    .slots
                    .iter_mut()
                    .flatten()
                    .next()
                    .context("No active save")?;
                write_slot(slot, &staged.path, true)?;
            }
            _ => {
                staged.write_aggregate(&staged.path.clone())?;
            }
        }
        *self = staged;
        Ok(())
    }

    pub fn export_to_psyz_dir(&mut self, target_dir: impl AsRef<Path>) -> Result<Vec<PathBuf>> {
        self.validate_slots()?;
        let mut staged = self.clone();
        let mut written = vec![];
        for (i, slot) in staged.slots.iter_mut().enumerate() {
            if let Some(slot) = slot {
                slot.slot_index = i;
                slot.to_bytes();
                let path = target_dir.as_ref().join(&slot.filename);
                write_slot(slot, &path, true)?;
                written.push(path);
            }
        }
        Ok(written)
    }

    pub fn export_to_ps1_card_image(&mut self, target_file: impl AsRef<Path>) -> Result<()> {
        self.validate_slots()?;
        let mut staged = self.clone();
        staged.kind = ContainerKind::Ps1CardImage;
        staged.raw_container_bytes = create_blank_ps1_card();
        staged.write_aggregate(target_file.as_ref())
    }

    // Whole-card companions live in <card-file>.pe-ext, keyed by logical slot name.
    // This keeps multiple card images in one folder independent.
    fn write_aggregate(&mut self, path: &Path) -> Result<()> {
        let single = self.kind == ContainerKind::McsSingleFile;
        let offset = if self.kind == ContainerKind::DexDriveGmeImage {
            GME_HEADER_SIZE
        } else {
            0
        };
        let mut bytes = self.raw_container_bytes.clone();
        let required = if single {
            MCS_SAVE_SIZE
        } else {
            offset + PS1_CARD_SIZE
        };
        if bytes.len() != required {
            bail!("Invalid card container size")
        }
        for (i, slot) in self.slots.iter_mut().enumerate() {
            if let Some(slot) = slot {
                slot.slot_index = i;
                slot.to_bytes();
                slot.prepare_companion_inventory()?;
                if let Some(c) = &mut slot.companion {
                    crate::companion::publish(
                        c,
                        &mut slot.raw_bytes,
                        &crate::companion::directory(path, true),
                        &slot.filename,
                    )?;
                }
                let block = slot.to_bytes();
                if single {
                    write_ps1_dir_frame(&mut bytes[..0x80], &slot.filename);
                    bytes[0x80..].copy_from_slice(&block);
                } else {
                    let dir = offset + (i + 1) * 0x80;
                    write_ps1_dir_frame(&mut bytes[dir..dir + 0x80], &slot.filename);
                    let data = offset + (i + 1) * SAVE_BLOCK_SIZE;
                    bytes[data..data + SAVE_BLOCK_SIZE].copy_from_slice(&block);
                }
            }
        }
        crate::companion::atomic_write(path, &bytes, true)?;
        self.raw_container_bytes = bytes;
        Ok(())
    }
    fn validate_slots(&self) -> Result<()> {
        for (i, slot) in self.slots.iter().enumerate() {
            if let Some(slot) = slot {
                slot.validate_inventory()
                    .with_context(|| format!("Save slot {}", i + 1))?;
                slot.clone()
                    .prepare_companion_inventory()
                    .with_context(|| format!("Save slot {}", i + 1))?;
            }
        }
        Ok(())
    }

    pub fn active_slot_indices(&self) -> Vec<usize> {
        self.slots
            .iter()
            .enumerate()
            .filter_map(|(i, s)| s.as_ref().map(|_| i))
            .collect()
    }

    pub fn first_active_slot_index(&self) -> Option<usize> {
        self.slots.iter().position(|s| s.is_some())
    }

    pub fn clone_slot(&mut self, from_idx: usize, to_idx: usize) -> Result<()> {
        if from_idx >= MAX_CARD_SLOTS || to_idx >= MAX_CARD_SLOTS {
            bail!("Slot index out of range (0..14)");
        }
        let mut cloned = self.slots[from_idx]
            .clone()
            .ok_or_else(|| anyhow::anyhow!("Source slot {} is empty", from_idx + 1))?;
        cloned.slot_index = to_idx;
        let _ = cloned.to_bytes();
        self.slots[to_idx] = Some(cloned);
        Ok(())
    }

    pub fn delete_slot(&mut self, idx: usize) -> Result<()> {
        if idx >= MAX_CARD_SLOTS {
            bail!("Slot index out of range (0..14)");
        }
        if self.kind == ContainerKind::PsyzDirectory {
            if let Some(slot) = &self.slots[idx] {
                let p = self.path.join(&slot.filename);
                if p.exists() {
                    let _ = fs::remove_file(p);
                }
            }
        }
        self.slots[idx] = None;
        Ok(())
    }
}

pub fn slot_index_from_filename(filename: &str) -> Option<usize> {
    let bytes = filename.as_bytes();
    if bytes.len() >= 20 && filename.starts_with("BASLUS-00662") {
        let last = bytes[19];
        if (b'A'..=b'O').contains(&last) {
            return Some((last - b'A') as usize);
        }
    }
    None
}

fn extract_ascii_filename(raw: &[u8]) -> String {
    let end = raw.iter().position(|&b| b == 0).unwrap_or(raw.len());
    String::from_utf8_lossy(&raw[..end]).trim().to_string()
}

fn write_ps1_dir_frame(frame: &mut [u8], filename: &str) {
    frame.fill(0);
    frame[0x00] = 0x51; // Active single/first block
    frame[0x04..0x08].copy_from_slice(&(SAVE_BLOCK_SIZE as u32).to_le_bytes());
    frame[0x08..0x0A].copy_from_slice(&0xFFFFu16.to_le_bytes()); // Next block pointer = end of chain
    let name_bytes = filename.as_bytes();
    let copy_len = name_bytes.len().min(20);
    frame[0x0A..0x0A + copy_len].copy_from_slice(&name_bytes[..copy_len]);
    update_mc_frame_xor(frame);
}

pub fn create_blank_ps1_card() -> Vec<u8> {
    let mut card = vec![0u8; PS1_CARD_SIZE];
    // Frame 0: "MC" header
    card[0] = b'M';
    card[1] = b'C';
    card[0x7F] = b'M' ^ b'C';

    // Frames 1..=15: Free directory frames (0xA0)
    for i in 1..=MAX_CARD_SLOTS {
        let off = i * 0x80;
        card[off] = 0xA0;
        card[off + 0x08] = 0xFF;
        card[off + 0x09] = 0xFF;
        update_mc_frame_xor(&mut card[off..off + 0x80]);
    }

    // Frames 16..=35: Broken sector list (0xFFFFFFFF)
    for i in 16..=35 {
        let off = i * 0x80;
        card[off..off + 4].copy_from_slice(&0xFFFF_FFFFu32.to_le_bytes());
        card[off + 0x08] = 0xFF;
        card[off + 0x09] = 0xFF;
        update_mc_frame_xor(&mut card[off..off + 0x80]);
    }

    // Frame 63: Write test frame (copy of Frame 0)
    let f63 = 63 * 0x80;
    card[f63] = b'M';
    card[f63 + 1] = b'C';
    card[f63 + 0x7F] = b'M' ^ b'C';

    card
}

/// Shared native-save publication for the editor and old-save importer.
pub(crate) fn write_slot(slot: &mut PeSaveSlot, path: &Path, replace: bool) -> Result<()> {
    slot.validate_inventory()?;
    slot.prepare_companion_inventory()?;
    let mut block = slot.to_bytes();
    if let Some(c) = &mut slot.companion {
        let name = path
            .file_name()
            .and_then(|s| s.to_str())
            .context("Invalid save name")?;
        crate::companion::publish(
            c,
            &mut block,
            &crate::companion::directory(path, false),
            name,
        )?;
        slot.raw_bytes = block;
    }
    let block = slot.to_bytes();
    crate::companion::atomic_write(path, &block, replace)
}
