//! Pack-local CJK glyph dictionary (`petext/cjk.py`). Fonts remain external; packs embed 12x12
//! pixels only. Rasterization is FreeType at 11 px plus a fixed-point Lanczos resample, the same
//! steps that produced the existing packs, so the same font gives the same pack bytes.
use std::collections::{BTreeSet, HashMap};
use std::path::PathBuf;
use std::rc::Rc;

use freetype::bitmap::PixelMode;
use freetype::face::LoadFlag;
use freetype::ffi;
use freetype::Library;

use crate::error::{Error, Result};
use crate::pycompat::repr;
use crate::wrap::Characters;

pub const RECORD: usize = 76;
pub const MAX_GLYPHS: usize = 240 * 240;

pub fn supported(cp: u32) -> bool {
    matches!(cp,
        0x1100..=0x11ff | 0x2e80..=0x303f | 0x3040..=0x30ff | 0x3100..=0x318f | 0x31a0..=0x31ff
        | 0x3400..=0x4dbf | 0x4e00..=0x9fff | 0xa960..=0xa97f | 0xac00..=0xd7af | 0xd7b0..=0xd7ff
        | 0xf900..=0xfaff | 0xff00..=0xffef | 0x20000..=0x323af)
}

pub fn token(index: usize) -> Result<Vec<u8>> {
    if index >= MAX_GLYPHS {
        return Err(Error::value("too many CJK glyphs"));
    }
    Ok(vec![0xF1, 0xC5, (index / 240) as u8, (index % 240) as u8])
}

pub fn index(raw: &[u8]) -> usize {
    raw[2] as usize * 240 + raw[3] as usize
}

/// A font the translator chose, or the demo font inside the application.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum FontSource {
    Demo,
    File(PathBuf),
}

impl FontSource {
    pub const DEMO_BYTES: &'static [u8] = include_bytes!("../fonts/OperaPilotCJK-Regular.otf");

    fn read(&self) -> Result<Vec<u8>> {
        match self {
            Self::Demo => Ok(Self::DEMO_BYTES.to_vec()),
            Self::File(path) => Ok(std::fs::read(path)?),
        }
    }

    pub fn display(&self) -> String {
        match self {
            Self::Demo => "fonts/OperaPilotCJK-Regular.otf".to_owned(),
            Self::File(path) => path.display().to_string(),
        }
    }
}

fn be16(data: &[u8], at: usize) -> Result<u32> {
    let end = at.checked_add(2).filter(|&end| end <= data.len());
    end.map(|end| u16::from_be_bytes(data[at..end].try_into().unwrap()) as u32)
        .ok_or_else(|| Error::value("the font file is truncated"))
}

fn be32(data: &[u8], at: usize) -> Result<u32> {
    let end = at.checked_add(4).filter(|&end| end <= data.len());
    end.map(|end| u32::from_be_bytes(data[at..end].try_into().unwrap()))
        .ok_or_else(|| Error::value("the font file is truncated"))
}

/// Unicode coverage of face zero, from the sfnt cmap: never accept a tofu glyph.
fn coverage(data: &[u8], name: &str, wanted: &BTreeSet<u32>) -> Result<BTreeSet<u32>> {
    let u16_at = |at: usize| be16(data, at).map(|v| v as usize);
    let u32_at = |at: usize| be32(data, at).map(|v| v as usize);
    let base = if data.starts_with(b"ttcf") { u32_at(12)? } else { 0 };
    let mut cmap = None;
    for n in 0..u16_at(base + 4)? {
        let p = base + 12 + 16 * n;
        if data.get(p..p + 4) == Some(b"cmap") {
            cmap = Some(u32_at(p + 8)?);
        }
    }
    let cmap = cmap.ok_or_else(|| Error::value(format!("{name}: no cmap")))?;
    let mut found = BTreeSet::new();
    for n in 0..u16_at(cmap + 2)? {
        let p = cmap + 4 + 8 * n;
        let (platform, encoding) = (u16_at(p)?, u16_at(p + 2)?);
        if platform != 0 && !(platform == 3 && (encoding == 1 || encoding == 10)) {
            continue;
        }
        let t = cmap + u32_at(p + 4)?;
        match u16_at(t)? {
            12 => {
                let mut groups = Vec::new();
                for k in 0..u32_at(t + 12)? {
                    let g = t + 16 + 12 * k;
                    groups.push((be32(data, g)?, be32(data, g + 4)?, be32(data, g + 8)?));
                }
                for &cp in wanted {
                    let k = groups.partition_point(|group| group.0 <= cp);
                    if k > 0 {
                        let (start, end, glyph) = groups[k - 1];
                        if cp <= end && glyph as u64 + (cp - start) as u64 != 0 {
                            found.insert(cp);
                        }
                    }
                }
            }
            4 => {
                let count = u16_at(t + 6)? / 2;
                let ends = t + 14;
                let starts = ends + 2 * count + 2;
                let deltas = starts + 2 * count;
                let offsets = deltas + 2 * count;
                let mut end_codes = Vec::with_capacity(count);
                for k in 0..count {
                    end_codes.push(be16(data, ends + 2 * k)?);
                }
                for &cp in wanted {
                    if cp > 0xffff {
                        continue;
                    }
                    let k = end_codes.partition_point(|&end| end < cp);
                    if k < count && be16(data, starts + 2 * k)? <= cp {
                        let offset = u16_at(offsets + 2 * k)?;
                        let delta = be16(data, deltas + 2 * k)?;
                        let start = be16(data, starts + 2 * k)?;
                        let glyph = if offset != 0 {
                            be16(data, offsets + 2 * k + offset + 2 * (cp - start) as usize)?
                        } else {
                            cp
                        };
                        if glyph != 0 && (glyph + delta) & 0xffff != 0 {
                            found.insert(cp);
                        }
                    }
                }
            }
            _ => {}
        }
    }
    Ok(found)
}

/// An 8-bit image.
struct Gray {
    width: usize,
    height: usize,
    pixels: Vec<u8>,
}

const PRECISION_BITS: u32 = 32 - 8 - 2;

fn sinc(x: f64) -> f64 {
    if x == 0.0 {
        return 1.0;
    }
    let x = x * std::f64::consts::PI;
    x.sin() / x
}

fn lanczos(x: f64) -> f64 {
    if (-3.0..3.0).contains(&x) {
        sinc(x) * sinc(x / 3.0)
    } else {
        0.0
    }
}

/// For each output position, its first input and fixed-point weights.
fn coefficients(in_size: usize, out_size: usize) -> Vec<(usize, Vec<i32>)> {
    let scale = in_size as f64 / out_size as f64;
    let filter_scale = scale.max(1.0);
    let support = 3.0 * filter_scale;
    (0..out_size)
        .map(|xx| {
            let center = (xx as f64 + 0.5) * scale;
            let xmin = ((center - support + 0.5) as i32).max(0) as usize;
            let xmax = (((center + support + 0.5) as i32).min(in_size as i32) as usize).saturating_sub(xmin);
            let mut weights: Vec<f64> =
                (0..xmax).map(|x| lanczos((x as f64 + xmin as f64 - center + 0.5) / filter_scale)).collect();
            let total: f64 = weights.iter().sum();
            if total != 0.0 {
                weights.iter_mut().for_each(|w| *w /= total);
            }
            let one = (1u32 << PRECISION_BITS) as f64;
            let fixed = weights
                .iter()
                .map(|&w| if w < 0.0 { (-0.5 + w * one) as i32 } else { (0.5 + w * one) as i32 })
                .collect();
            (xmin, fixed)
        })
        .collect()
}

fn clip8(value: i32) -> u8 {
    (value >> PRECISION_BITS).clamp(0, 255) as u8
}

fn resize(image: &Gray, width: usize, height: usize) -> Gray {
    let mut horizontal = image.pixels.clone();
    if width != image.width {
        let coeffs = coefficients(image.width, width);
        horizontal = Vec::with_capacity(width * image.height);
        for y in 0..image.height {
            let row = &image.pixels[y * image.width..(y + 1) * image.width];
            for (xmin, k) in &coeffs {
                let mut sum = 1i32 << (PRECISION_BITS - 1);
                for (x, weight) in k.iter().enumerate() {
                    sum = sum.wrapping_add(row[xmin + x] as i32 * weight);
                }
                horizontal.push(clip8(sum));
            }
        }
    }
    let mut pixels = horizontal.clone();
    if height != image.height {
        let coeffs = coefficients(image.height, height);
        pixels = Vec::with_capacity(width * height);
        for (ymin, k) in &coeffs {
            for x in 0..width {
                let mut sum = 1i32 << (PRECISION_BITS - 1);
                for (y, weight) in k.iter().enumerate() {
                    sum = sum.wrapping_add(horizontal[(ymin + y) * width + x] as i32 * weight);
                }
                pixels.push(clip8(sum));
            }
        }
    }
    Gray { width, height, pixels }
}

fn pixel_round(value: i64) -> i64 {
    (value + 32) >> 6
}

struct Face {
    face: freetype::Face,
    coverage: BTreeSet<u32>,
}

impl Face {
    /// The glyph on an 11x11 grid: a common em box keeps punctuation at the baseline, rather than
    /// centering each ink rectangle (which makes Japanese full stops float mid-line).
    fn raster(&self, ch: char) -> Result<Gray> {
        let glyph_error = |error: freetype::Error| Error::value(format!("no outline for {}: {error}", repr(&ch.to_string())));
        let index = self.face.get_char_index(ch as usize).unwrap_or(0);
        self.face.load_glyph(index, LoadFlag::DEFAULT).map_err(glyph_error)?;
        let slot = self.face.glyph();
        let cbox = slot.get_glyph().map_err(glyph_error)?.get_cbox(ffi::FT_GLYPH_BBOX_PIXELS);
        let advance = pixel_round(slot.metrics().horiAdvance as i64);
        let (x_min, x_max) = ((cbox.xMin as i64).min(0), (cbox.xMax as i64).max(advance).max(0));
        let (y_min, y_max) = ((cbox.yMin as i64).min(0), (cbox.yMax as i64).max(0));
        let (left, top) = (x_min.min(0), (-y_max).min(-10));
        let (right, bottom) = (x_max.max(11), (-y_min).max(2));
        let (width, height) = ((right - left) as usize, (bottom - top) as usize);
        let mut full = vec![0u8; width * height];

        self.face.load_glyph(index, LoadFlag::DEFAULT | LoadFlag::RENDER).map_err(glyph_error)?;
        let slot = self.face.glyph();
        let bitmap = slot.bitmap();
        let (bw, bh, pitch) = (bitmap.width() as i64, bitmap.rows() as i64, bitmap.pitch() as i64);
        let buffer = bitmap.buffer();
        let mode = bitmap.pixel_mode().map_err(glyph_error)?;
        let (ox, oy) = (slot.bitmap_left() as i64 - left, -(slot.bitmap_top() as i64) - top);
        for y in 0..bh {
            let row = (y * pitch.abs()) as usize;
            for x in 0..bw {
                let value = match mode {
                    PixelMode::Mono => {
                        if buffer[row + (x / 8) as usize] & (0x80 >> (x % 8)) != 0 {
                            255
                        } else {
                            0
                        }
                    }
                    _ => buffer[row + x as usize],
                };
                let (tx, ty) = (ox + x, oy + y);
                if tx >= 0 && ty >= 0 && tx < width as i64 && ty < height as i64 {
                    let target = &mut full[ty as usize * width + tx as usize];
                    *target = (*target).max(value);
                }
            }
        }
        let full = Gray { width, height, pixels: full };
        if width == 11 && height == 11 {
            return Ok(full);
        }
        Ok(resize(&full, 11, 11))
    }
}

pub struct Atlas {
    pub chars: Vec<char>,
    pub records: Vec<u8>,
    pub codes: HashMap<char, Vec<u8>>,
    pub characters: Characters,
}

impl Atlas {
    pub fn from_records(chars: Vec<char>, records: Vec<u8>) -> Result<Self> {
        let codes: HashMap<char, Vec<u8>> =
            chars.iter().enumerate().map(|(i, &ch)| Ok((ch, token(i)?))).collect::<Result<_>>()?;
        let characters = codes.iter().map(|(&ch, code)| (code.clone(), ch)).collect();
        Ok(Self { chars, records, codes, characters })
    }

    pub fn build<'a>(texts: impl IntoIterator<Item = &'a str>, fonts: &[FontSource]) -> Result<Self> {
        let unique: BTreeSet<char> =
            texts.into_iter().flat_map(str::chars).filter(|&ch| supported(ch as u32)).collect();
        let chars: Vec<char> = unique.into_iter().collect();
        if chars.len() > MAX_GLYPHS {
            return Err(Error::value("CJK glyph dictionary exceeds 57600 entries"));
        }
        if chars.is_empty() {
            return Self::from_records(chars, Vec::new());
        }
        if fonts.is_empty() {
            return Err(Error::value("CJK text needs --cjk-font PATH (repeat for fallback fonts)"));
        }
        let wanted: BTreeSet<u32> = chars.iter().map(|&ch| ch as u32).collect();
        let library = Library::init().map_err(|error| Error::value(format!("FreeType: {error}")))?;
        let mut faces = Vec::new();
        for font in fonts {
            let data = font.read()?;
            let coverage = coverage(&data, &font.display(), &wanted)?;
            let face = library
                .new_memory_face(Rc::new(data), 0)
                .map_err(|error| Error::value(format!("{}: {error}", font.display())))?;
            face.set_char_size(0, 11 * 64, 0, 0).map_err(|error| Error::value(format!("{}: {error}", font.display())))?;
            faces.push(Face { face, coverage });
        }
        let mut records = Vec::with_capacity(chars.len() * RECORD);
        for &ch in &chars {
            let face = faces.iter().find(|face| face.coverage.contains(&(ch as u32))).ok_or_else(|| {
                Error::value(format!("no supplied font contains {} U+{:04X}", repr(&ch.to_string()), ch as u32))
            })?;
            let image = if ch == '\u{3000}' { Gray { width: 11, height: 11, pixels: vec![0; 121] } } else { face.raster(ch)? };
            let brightest = image.pixels.iter().copied().max().unwrap_or(0) as i32;
            let threshold = (brightest / 2).min(80).max(1) as u8;
            let mut pixels = [0u8; 144];
            for y in 0..11 {
                for x in 0..11 {
                    if image.pixels[y * 11 + x] >= threshold {
                        pixels[(y + 1) * 12 + x + 1] = 2;
                    }
                }
            }
            for y in 0..11 {
                for x in 0..11 {
                    if image.pixels[y * 11 + x] >= threshold {
                        pixels[y * 12 + x] = 1;
                    }
                }
            }
            if ch != '\u{3000}' && !pixels.contains(&1) {
                return Err(Error::value(format!("empty raster for {}", repr(&ch.to_string()))));
            }
            records.extend_from_slice(&(ch as u32).to_le_bytes());
            records.extend(pixels.chunks(2).map(|pair| pair[0] | pair[1] << 4));
        }
        Self::from_records(chars, records)
    }
}
