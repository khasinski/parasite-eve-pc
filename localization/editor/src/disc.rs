//! Read-only access to a Parasite Eve (USA) disc image (`petext/disc.py`): sectors, ISO 9660 root
//! files, PE.IMG, the room table in the boot executable, the resident pack, each room's data run.
use std::collections::HashMap;
use std::fs::File;
use std::io::{Read, Seek, SeekFrom};
use std::path::{Path, PathBuf};

use crate::error::{Error, Result};
use crate::pycompat::repr;

const SECTOR: usize = 2048;
const RAW_SECTOR: usize = 2352;
const SYNC: [u8; 12] = [0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0];
const ROOM_COUNT: usize = 450;
const ROOM_TABLE_ADDR: usize = 0x8009_3378;
const SECTOR_TABLE_ADDR: usize = 0x8009_30D8;
const RESIDENT_ENTRY: usize = 2;

fn tables_for(executable: &str) -> Option<(usize, usize)> {
    match executable {
        "SLUS_006.62" | "SLUS_006.68" => Some((ROOM_TABLE_ADDR, SECTOR_TABLE_ADDR)),
        "SLPS_012.30" | "SLPS_012.31" => Some((0x8009_2E40, 0x8009_2BA0)),
        _ => None,
    }
}

fn u32_le(data: &[u8], at: usize) -> Result<u32> {
    at.checked_add(4)
        .and_then(|end| data.get(at..end))
        .map(|bytes| u32::from_le_bytes(bytes.try_into().unwrap()))
        .ok_or_else(|| Error::value("the disc data is truncated"))
}

fn cue_bin(cue: &Path) -> Result<PathBuf> {
    let text = String::from_utf8_lossy(&std::fs::read(cue)?).into_owned();
    for line in text.lines() {
        let line = crate::pycompat::strip(line);
        if line.to_uppercase().starts_with("FILE") {
            let name = if line.contains('"') {
                line.split('"').nth(1)
            } else {
                line.split(crate::pycompat::is_space).filter(|p| !p.is_empty()).nth(1)
            };
            let name = name.ok_or_else(|| Error::value("list index out of range"))?;
            return Ok(cue.parent().unwrap_or(Path::new("")).join(name));
        }
    }
    Err(Error::value(format!("{}: no FILE line", cue.display())))
}

pub struct Disc {
    file: File,
    sector_size: usize,
    data_offset: usize,
    files: Option<HashMap<String, (usize, usize)>>,
}

impl Disc {
    pub fn open(path: &Path) -> Result<Self> {
        let is_cue = path.extension().is_some_and(|ext| ext.eq_ignore_ascii_case("cue"));
        let image = if is_cue { cue_bin(path)? } else { path.to_path_buf() };
        let mut file = File::open(image)?;
        file.seek(SeekFrom::Start((16 * RAW_SECTOR) as u64))?;
        let mut head = Vec::with_capacity(16);
        (&mut file).take(16).read_to_end(&mut head)?;
        let (sector_size, data_offset) = if head.starts_with(&SYNC) {
            (RAW_SECTOR, if head.get(15) == Some(&1) { 16 } else { 24 })
        } else {
            (SECTOR, 0)
        };
        Ok(Self { file, sector_size, data_offset, files: None })
    }

    fn sector(&mut self, lba: usize) -> Result<Vec<u8>> {
        self.file.seek(SeekFrom::Start((lba * self.sector_size + self.data_offset) as u64))?;
        let mut data = Vec::with_capacity(SECTOR);
        (&mut self.file).take(SECTOR as u64).read_to_end(&mut data)?;
        if data.len() != SECTOR {
            return Err(Error::value(format!("no sector {lba}")));
        }
        Ok(data)
    }

    pub fn read(&mut self, lba: usize, size: usize) -> Result<Vec<u8>> {
        let mut out = Vec::with_capacity(size.div_ceil(SECTOR) * SECTOR);
        for i in 0..size.div_ceil(SECTOR) {
            out.extend(self.sector(lba + i)?);
        }
        out.truncate(size);
        Ok(out)
    }

    /// Name -> (first sector, size) for the ISO 9660 root directory.
    pub fn root_files(&mut self) -> Result<&HashMap<String, (usize, usize)>> {
        if self.files.is_none() {
            let pvd = self.sector(16)?;
            if &pvd[1..6] != b"CD001" {
                return Err(Error::value("not a CD-ROM image"));
            }
            let (lba, size) = (u32_le(&pvd, 158)? as usize, u32_le(&pvd, 166)? as usize);
            let raw = self.read(lba, size)?;
            let mut files = HashMap::new();
            for sector in raw.chunks(SECTOR) {
                let mut at = 0;
                while at < SECTOR && sector.get(at).is_some_and(|&n| n != 0) {
                    let truncated = || Error::value("the disc directory is truncated");
                    let name_len = *sector.get(at + 32).ok_or_else(truncated)? as usize;
                    let end = (at + 33 + name_len).min(sector.len());
                    let name = sector.get(at + 33..end).ok_or_else(truncated)?;
                    let name = name.split(|&b| b == b';').next().unwrap_or_default();
                    let name: String = name.iter().map(|&b| if b.is_ascii() { b as char } else { '\u{fffd}' }).collect();
                    let entry = (u32_le(sector, at + 2)? as usize, u32_le(sector, at + 10)? as usize);
                    files.insert(name.to_uppercase(), entry);
                    at += sector[at] as usize;
                }
            }
            self.files = Some(files);
        }
        Ok(self.files.as_ref().unwrap())
    }

    pub fn file(&mut self, name: &str) -> Result<Vec<u8>> {
        let upper = name.to_uppercase();
        let (lba, size) = *self.root_files()?.get(&upper).ok_or_else(|| Error::value(repr(&upper)))?;
        self.read(lba, size)
    }
}

/// The tables the port reads at mount, and rooms and the resident pack from PE.IMG.
pub struct Game {
    pub disc: Disc,
    pe_img_start: usize,
    rooms: Vec<(usize, usize, usize, usize)>,
    sectors: Vec<usize>,
}

impl Game {
    pub fn open(path: &Path) -> Result<Self> {
        let mut disc = Disc::open(path)?;
        let pe_img_start = disc.root_files()?.get("PE.IMG").ok_or_else(|| Error::value("'PE.IMG'"))?.0;
        let config: String =
            disc.file("SYSTEM.CNF")?.iter().map(|&b| if b.is_ascii() { b as char } else { '\u{fffd}' }).collect();
        let boot = config
            .split_once("cdrom:")
            .or_else(|| config.split_once("CDROM:"))
            .ok_or_else(|| Error::value("list index out of range"))?
            .1;
        let exe_name = crate::pycompat::strip(boot.split(';').next().unwrap_or_default());
        let exe_name = exe_name.trim_start_matches('\\').rsplit('\\').next().unwrap_or_default().to_uppercase();
        let exe = disc.file(&exe_name)?;
        if !exe.starts_with(b"PS-X EXE") {
            return Err(Error::value("the boot file is not a PS-X EXE"));
        }
        let (room_table, sector_table) =
            tables_for(&exe_name).ok_or_else(|| Error::value(format!("unknown boot executable {exe_name}")))?;
        let base = u32_le(&exe, 24)? as usize;
        let at = |addr: usize| addr.checked_add(0x800).and_then(|v| v.checked_sub(base)).ok_or_else(|| Error::value("the disc data is truncated"));
        let mut rooms = Vec::with_capacity(ROOM_COUNT);
        for i in 0..ROOM_COUNT {
            let entry = at(room_table)? + i * 8;
            let (start, counts) = (u32_le(&exe, entry)? as usize, u32_le(&exe, entry + 4)? as usize);
            rooms.push((start, counts & 0xFF, (counts >> 8) & 0xFFF, counts >> 20));
        }
        let table = at(sector_table)?;
        let bytes = exe.get(table..table + 162).ok_or_else(|| Error::value("the disc data is truncated"))?;
        let sectors = bytes.chunks(2).map(|pair| u16::from_le_bytes([pair[0], pair[1]]) as usize).collect();
        Ok(Self { disc, pe_img_start, rooms, sectors })
    }

    pub fn has_root_file(&mut self, name: &str) -> Result<bool> {
        Ok(self.disc.root_files()?.contains_key(name))
    }

    fn pe(&mut self, sector: usize, count: usize) -> Result<Vec<u8>> {
        self.disc.read(self.pe_img_start + sector, count * SECTOR)
    }

    pub fn resident_pack(&mut self) -> Result<Vec<u8>> {
        let (first, next) = (self.sectors[RESIDENT_ENTRY], self.sectors[RESIDENT_ENTRY + 1]);
        let count = next.checked_sub(first).ok_or_else(|| Error::value("the disc data is truncated"))?;
        self.pe(first, count)
    }

    /// A room's data run: models, rows, scripts and messages.
    pub fn room_data(&mut self, map_no: usize) -> Result<Option<Vec<u8>>> {
        let (start, scratch, texture, data) = self.rooms[map_no - 1];
        if data == 0 {
            return Ok(None);
        }
        Ok(Some(self.pe(start + scratch + texture, data)?))
    }
}

/// A room's message banks by task slot (0 Japanese, 1 English): offset in the data run.
pub fn bank_slots(data: &[u8]) -> Result<HashMap<u32, usize>> {
    let d = u32_le(data, 4)? as usize;
    let ts = u32_le(data, d + 0x20)? as usize;
    let mut banks = HashMap::new();
    for i in 0..(ts >> 22) {
        let word = u32_le(data, (ts & 0x3F_FFFF) + i * 8 + 4)?;
        banks.insert(word >> 24, (word & 0xFF_FFFF) as usize);
    }
    Ok(banks)
}
