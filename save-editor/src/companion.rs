//! Bounded PEEX v1 reader/writer. Provider payloads remain opaque.
use crate::mod_save::{self, crc32, ModData};
use anyhow::{ensure, Context, Result};
use serde::{Deserialize, Serialize};
use std::{
    fs,
    io::{Read, Write},
    path::{Path, PathBuf},
};
const LINK: &str = "pe_host.extlink";
pub const LIMIT: usize = 8192;
pub(crate) fn u16at(b: &[u8], p: usize) -> usize {
    u16::from_le_bytes(b[p..p + 2].try_into().unwrap()) as usize
}
pub(crate) fn u32at(b: &[u8], p: usize) -> u32 {
    u32::from_le_bytes(b[p..p + 4].try_into().unwrap())
}
pub(crate) fn put32(b: &mut [u8], p: usize, n: u32) {
    b[p..p + 4].copy_from_slice(&n.to_le_bytes());
}
fn text(b: &[u8]) -> Result<&str> {
    let end = b
        .iter()
        .position(|&c| c == 0)
        .context("Unterminated companion name")?;
    Ok(std::str::from_utf8(&b[..end])?)
}
fn name_ok(s: &str) -> bool {
    !s.is_empty() && s.len() <= 63 && s.bytes().all(|c| c.is_ascii_alphanumeric() || c == b'-')
}
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Companion {
    pub bytes: Vec<u8>,
    #[serde(default)]
    pub equipment_snapshot: Vec<u8>,
}
#[derive(Debug, Clone)]
pub struct Section {
    pub key: String,
    pub version: usize,
    pub reader: usize,
    pub offset: usize,
    pub size: usize,
    directory: usize,
}
pub fn link(raw: &[u8]) -> Result<Option<Vec<u8>>> {
    let d = ModData::inspect(raw)?;
    let Some(r) = d.records.iter().find(|r| r.id == mod_save::id(LINK)) else {
        if &raw[mod_save::OFFSET..mod_save::OFFSET + 4] == b"PEMC" && !d.editable {
            let linear = [
                &raw[mod_save::OFFSET..mod_save::OFFSET + 0x200],
                &raw[mod_save::OFFSET + 0x288..],
            ]
            .concat();
            ensure!(
                !linear
                    .windows(4)
                    .any(|b| b == mod_save::id(LINK).to_le_bytes()),
                "Damaged or unsupported companion link"
            );
        }
        return Ok(None);
    };
    ensure!(
        d.editable && r.version == 1 && r.data.len() == 20,
        "Damaged or unsupported companion link"
    );
    let b = &r.data;
    ensure!(
        b[..4] == [1, 1, 0, 0]
            && b[17..] == [0, 0, 0]
            && u32at(b, 4) != 0
            && (96..=LIMIT).contains(&(u32at(b, 8) as usize))
            && b[16] <= 16,
        "Invalid companion link"
    );
    Ok(Some(b.clone()))
}
impl Companion {
    pub fn sections(&self) -> Result<Vec<Section>> {
        let b = &self.bytes;
        ensure!((96..=LIMIT).contains(&b.len()), "Invalid companion size");
        ensure!(
            &b[..4] == b"PEEX"
                && u16at(b, 4) == 1
                && u16at(b, 6) == 96
                && u32at(b, 12) as usize == b.len()
                && u32at(b, 8) != 0,
            "Unsupported companion header"
        );
        ensure!(name_ok(text(&b[20..84])?), "Invalid companion binding");
        ensure!(
            b[92..96].iter().all(|&v| v == 0),
            "Unsupported companion header fields"
        );
        let mut check = b.clone();
        put32(&mut check, 88, 0);
        ensure!(crc32(&check) == u32at(b, 88), "Companion checksum mismatch");
        let n = u16at(b, 16);
        ensure!(
            n <= 16 && u16at(b, 18) == 0 && 96 + n * 64 <= b.len(),
            "Invalid companion directory"
        );
        let mut next = 96 + n * 64;
        let mut result: Vec<Section> = vec![];
        for i in 0..n {
            let p = 96 + i * 64;
            let key = text(&b[p..p + 48])?;
            ensure!(
                !key.is_empty()
                    && key.bytes().all(|c| c.is_ascii_lowercase()
                        || c.is_ascii_digit()
                        || b"_-/.".contains(&c)),
                "Invalid provider key"
            );
            let size = u32at(b, p + 56) as usize;
            ensure!(
                u16at(b, p + 48) != 0
                    && u16at(b, p + 50) != 0
                    && size != 0
                    && u32at(b, p + 52) as usize == next
                    && size <= b.len() - next,
                "Invalid provider section"
            );
            ensure!(
                result.last().is_none_or(|s| s.key.as_str() < key)
                    && crc32(&b[next..next + size]) == u32at(b, p + 60),
                "Invalid provider order/checksum"
            );
            if key == "pe_host/content" {
                ensure!(
                    u16at(b, p + 48) == 1 && u16at(b, p + 50) == 1 && size == 2048,
                    "Unsupported custom-content identity ledger"
                );
            }
            result.push(Section {
                key: key.into(),
                version: u16at(b, p + 48),
                reader: u16at(b, p + 50),
                offset: next,
                size,
                directory: p,
            });
            next += size;
        }
        ensure!(next == b.len(), "Trailing companion bytes");
        Ok(result)
    }
    pub fn validate_link(&self, raw: &[u8], name: &str) -> Result<()> {
        let l = link(raw)?.context("Companion without a native link")?;
        let sections = self.sections()?;
        ensure!(
            text(&self.bytes[20..84])? == name
                && u32at(&self.bytes, 8) == u32at(&l, 4)
                && self.bytes.len() == u32at(&l, 8) as usize
                && crc32(&self.bytes) == u32at(&l, 12)
                && sections.len() == l[16] as usize,
            "Companion does not match this save"
        );
        Ok(())
    }
    pub fn section(&self, key: &str) -> Result<Option<(Section, Vec<u8>)>> {
        Ok(self
            .sections()?
            .into_iter()
            .find(|s| s.key == key)
            .map(|s| {
                let data = self.bytes[s.offset..s.offset + s.size].to_vec();
                (s, data)
            }))
    }
    pub fn replace(&mut self, section: &Section, data: &[u8]) -> Result<()> {
        self.sections()?;
        ensure!(data.len() == section.size, "Provider size cannot change");
        self.bytes[section.offset..section.offset + section.size].copy_from_slice(data);
        put32(&mut self.bytes, section.directory + 60, crc32(data));
        self.refresh();
        Ok(())
    }
    fn refresh(&mut self) {
        put32(&mut self.bytes, 88, 0);
        let c = crc32(&self.bytes);
        put32(&mut self.bytes, 88, c);
    }
    pub fn bind(&mut self, raw: &mut [u8], name: &str, generation: u32) -> Result<()> {
        let n = self.sections()?.len();
        ensure!(
            name_ok(name) && generation != 0,
            "Invalid companion destination"
        );
        let previous = u32at(&self.bytes, 8);
        put32(&mut self.bytes, 84, previous);
        put32(&mut self.bytes, 8, generation);
        self.bytes[20..84].fill(0);
        self.bytes[20..20 + name.len()].copy_from_slice(name.as_bytes());
        self.refresh();
        let mut l = vec![0; 20];
        l[0] = 1;
        l[1] = 1;
        l[16] = n as u8;
        put32(&mut l, 4, generation);
        put32(&mut l, 8, self.bytes.len() as u32);
        put32(&mut l, 12, crc32(&self.bytes));
        mod_save::replace_record(raw, LINK, 1, &l, None)
    }
}
pub fn directory(path: &Path, aggregate: bool) -> PathBuf {
    if aggregate {
        let mut p = path.as_os_str().to_os_string();
        p.push(".pe-ext");
        PathBuf::from(p)
    } else {
        path.parent().unwrap_or(Path::new(".")).join(".pe-ext")
    }
}
pub fn load(raw: &[u8], dir: &Path, name: &str) -> Result<Option<Companion>> {
    let Some(l) = link(raw)? else { return Ok(None) };
    ensure!(name_ok(name), "Invalid companion filename");
    let path = dir.join(format!("{}.{:08x}.pex", name, u32at(&l, 4)));
    let mut bytes = vec![];
    fs::File::open(&path)
        .with_context(|| format!("Required companion missing: {}", path.display()))?
        .take((LIMIT + 1) as u64)
        .read_to_end(&mut bytes)?;
    let c = Companion {
        bytes,
        equipment_snapshot: raw[0x1cc..0x11cc].to_vec(),
    };
    c.validate_link(raw, name)?;
    Ok(Some(c))
}

/// Publish an immutable generation first. Never replace a companion an older native save uses.
pub fn publish(c: &mut Companion, raw: &mut [u8], dir: &Path, name: &str) -> Result<()> {
    c.sections()?;
    ensure!(name_ok(name), "Invalid companion destination");
    fs::create_dir_all(dir)?;
    let start = u32at(&c.bytes, 8)
        .checked_add(1)
        .context("Companion generation exhausted")?;
    for generation in start..=u32::MAX {
        let file = dir.join(format!("{}.{:08x}.pex", name, generation));
        let mut out = match fs::OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&file)
        {
            Ok(f) => f,
            Err(e) if e.kind() == std::io::ErrorKind::AlreadyExists => continue,
            Err(e) => return Err(e.into()),
        };
        c.bind(raw, name, generation)?;
        out.write_all(&c.bytes)?;
        out.sync_all()?;
        ensure!(fs::read(&file)? == c.bytes, "Companion read-back failed");
        c.validate_link(raw, name)?;
        return Ok(());
    }
    anyhow::bail!("Companion generation exhausted")
}

/// Same-directory staging plus OS atomic replacement; the previous native file stays live on failure.
pub fn atomic_write(path: &Path, bytes: &[u8], replace: bool) -> Result<()> {
    use std::sync::atomic::{AtomicU64, Ordering};
    static NEXT: AtomicU64 = AtomicU64::new(0);
    let parent = path.parent().unwrap_or(Path::new("."));
    fs::create_dir_all(parent)?;
    let tmp = parent.join(format!(
        ".pe-editor-{}-{}.tmp",
        std::process::id(),
        NEXT.fetch_add(1, Ordering::Relaxed)
    ));
    let mut out = fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&tmp)?;
    let result = (|| -> Result<()> {
        out.write_all(bytes)?;
        out.sync_all()?;
        drop(out);
        ensure!(fs::read(&tmp)? == bytes, "Native save read-back failed");
        if !replace {
            fs::hard_link(&tmp, path)?;
            fs::remove_file(&tmp)?;
        } else {
            replace_file(&tmp, path)?;
        }
        Ok(())
    })();
    if result.is_err() {
        let _ = fs::remove_file(&tmp);
    }
    result
}
/// Import without normalizing native equipment or title bytes.
pub fn copy_native(src: &Path, dst: &Path) -> Result<()> {
    let mut raw = fs::read(src)?;
    ensure!(raw.len() == 8192, "Expected an 8 KiB native save");
    let name = src
        .file_name()
        .and_then(|s| s.to_str())
        .context("Invalid source name")?;
    if let Some(mut c) = load(&raw, &directory(src, false), name)? {
        let target = dst
            .file_name()
            .and_then(|s| s.to_str())
            .context("Invalid target name")?;
        publish(&mut c, &mut raw, &directory(dst, false), target)?;
        crate::crc::update_save_crc16(&mut raw);
    }
    atomic_write(dst, &raw, false)
}
#[cfg(windows)]
fn replace_file(src: &Path, dst: &Path) -> Result<()> {
    use std::os::windows::ffi::OsStrExt;
    #[link(name = "kernel32")]
    extern "system" {
        fn MoveFileExW(src: *const u16, dst: *const u16, flags: u32) -> i32;
    }
    let a: Vec<u16> = src.as_os_str().encode_wide().chain(Some(0)).collect();
    let b: Vec<u16> = dst.as_os_str().encode_wide().chain(Some(0)).collect();
    ensure!(
        unsafe { MoveFileExW(a.as_ptr(), b.as_ptr(), 9) } != 0,
        "Could not replace save: {}",
        std::io::Error::last_os_error()
    );
    Ok(())
}
#[cfg(not(windows))]
fn replace_file(src: &Path, dst: &Path) -> Result<()> {
    fs::rename(src, dst)?;
    Ok(())
}
