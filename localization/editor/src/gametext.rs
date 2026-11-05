//! The game's text code (`petext/gametext.py`): decode to readable tags, encode back to the same
//! bytes, and split a message bank into pages. Every byte survives a decode and encode.
use std::collections::HashMap;
use std::sync::OnceLock;

use indexmap::IndexMap;
use regex::Regex;

use crate::error::{Error, Result};
use crate::pycompat::repr;
use crate::{cyrillic, latin};

/// g_TextboxGlyphSpacing: the advance of glyph codes 0..77 in a box.
pub const ADVANCE: [u32; 78] = [
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 6, 6, 8, 11, 14, 9, 8, 7, 8, 7, 7, 8, 9, 6, 7, 9, 8, 11, 9, 7, 8, 7, 9, 7, 7, 9, 9,
    11, 7, 9, 7, 9, 4, 7, 6, 4, 4, 8, 8, 7, 7, 7, 7, 8, 9, 5, 4, 8, 5, 11, 9, 7, 8, 8, 7, 6, 5, 9, 9, 11, 7, 9, 6, 4,
    4, 7, 0,
];
pub const WIDE: u32 = 12;
pub const LINE_HEIGHT: i64 = 12;

/// What the English box moves its pen: every space (code 0x0F) is pulled back 7 px.
pub fn box_advance(code: u8) -> Option<u32> {
    match code {
        0x0F => Some(ADVANCE[0x0F] - 7),
        code => ADVANCE.get(code as usize).copied(),
    }
}

pub fn char_of(code: u8) -> Option<char> {
    match code {
        0..=9 => Some((b'0' + code) as char),
        0x0A => Some('+'),
        0x0B => Some('-'),
        0x0E => Some('%'),
        0x0F => Some(' '),
        0x10..=0x29 => Some((b'A' + code - 0x10) as char),
        0x2A => Some('&'),
        0x2B => Some('!'),
        0x2C => Some('?'),
        0x2D => Some('"'),
        0x2E => Some('\''),
        0x2F => Some('.'),
        0x30..=0x49 => Some((b'a' + code - 0x30) as char),
        0x4A => Some(':'),
        0x4B => Some(','),
        0x4C => Some('/'),
        _ => None,
    }
}

pub fn code_of(c: char) -> Option<u8> {
    match c {
        '0'..='9' => Some(c as u8 - b'0'),
        '+' => Some(0x0A),
        '-' => Some(0x0B),
        '%' => Some(0x0E),
        ' ' => Some(0x0F),
        'A'..='Z' => Some(c as u8 - b'A' + 0x10),
        '&' => Some(0x2A),
        '!' => Some(0x2B),
        '?' => Some(0x2C),
        '"' => Some(0x2D),
        '\'' => Some(0x2E),
        '.' => Some(0x2F),
        'a'..='z' => Some(c as u8 - b'a' + 0x30),
        ':' => Some(0x4A),
        ',' => Some(0x4B),
        '/' => Some(0x4C),
        _ => None,
    }
}

pub fn decode(b: &[u8]) -> String {
    let mut out = String::new();
    let mut i = 0;
    while i < b.len() {
        let c = b[i];
        if let Some(ch) = char_of(c) {
            out.push(ch);
        } else if c == 0xF7 {
            out.push_str("{br}");
        } else if c == 0xF8 {
            out.push_str("{wait}");
        } else if c == 0xF9 {
            out.push_str("{end2}");
        } else if c == 0xFA {
            out.push_str("{name}");
        } else if c == 0xFB && i + 1 < b.len() {
            let sub = b[i + 1];
            match sub {
                4 => out.push_str("{red}"),
                5 => out.push_str("{/red}"),
                6 => out.push_str("{pause}"),
                8 => out.push_str("{num}"),
                9 if i + 2 < b.len() => {
                    out.push_str(&format!("{{choice {}}}", b[i + 2]));
                    i += 1;
                }
                7 if i + 2 < b.len() => {
                    out.push_str(&format!("{{fb07 {:02X}}}", b[i + 2]));
                    i += 1;
                }
                0..=3 => out.push_str(&format!("{{sym {sub}}}")),
                _ => out.push_str(&format!("{{fb {sub:02X}}}")),
            }
            i += 1;
        } else if (c == 0xFC || c == 0xFD) && i + 1 < b.len() {
            out.push_str(&format!("{{{} {:02X}}}", if c == 0xFC { "fc" } else { "fd" }, b[i + 1]));
            i += 1;
        } else if c < 0xF1 {
            out.push_str(&format!("{{g{c:02X}}}"));
        } else {
            out.push_str(&format!("{{x{c:02X}}}"));
        }
        i += 1;
    }
    out
}

fn tag_pattern() -> &'static Regex {
    static PATTERN: OnceLock<Regex> = OnceLock::new();
    PATTERN.get_or_init(|| Regex::new(r"^\{([gx][0-9A-F]{2}|[a-z/]+[0-9]*)(?: ([0-9A-Fa-f]+))?\}").unwrap())
}

fn operand(value: Option<u32>) -> Result<u8> {
    value.filter(|v| *v <= 255).map(|v| v as u8).ok_or_else(|| Error::value("bytes must be in range(0, 256)"))
}

/// An operand too large for the digits to fit stays out of the byte range.
fn parse_digits(text: &str, radix: u32) -> Option<u32> {
    let trimmed = text.trim_start_matches('0');
    if trimmed.len() > 6 {
        return None;
    }
    if trimmed.is_empty() {
        return Some(0);
    }
    u32::from_str_radix(trimmed, radix).ok()
}

fn decimal(arg: Option<&str>) -> Result<u8> {
    let arg = arg.ok_or_else(|| {
        Error::type_error("int() argument must be a string, a bytes-like object or a real number, not 'NoneType'")
    })?;
    if !arg.bytes().all(|b| b.is_ascii_digit()) {
        return Err(Error::value(format!("invalid literal for int() with base 10: {}", repr(arg))));
    }
    operand(parse_digits(arg, 10))
}

fn hexadecimal(arg: Option<&str>) -> Result<u8> {
    let arg = arg.ok_or_else(|| Error::type_error("int() can't convert non-string with explicit base"))?;
    if !arg.bytes().all(|b| b.is_ascii_hexdigit()) {
        return Err(Error::value(format!("invalid literal for int() with base 16: {}", repr(arg))));
    }
    operand(parse_digits(arg, 16))
}

/// `text` in the game's glyph code. `extended` allows the Latin Extended and Cyrillic glyphs.
pub fn encode(text: &str, extended: bool, cjk_codes: Option<&HashMap<char, Vec<u8>>>) -> Result<Vec<u8>> {
    let mut out = Vec::new();
    let mut chars = text.char_indices().enumerate().peekable();
    while let Some(&(i, (at, c))) = chars.peek() {
        if c == '{' {
            let Some(found) = tag_pattern().captures(&text[at..]) else {
                let preview: String = text[at..].chars().take(12).collect();
                return Err(Error::value(format!("bad tag at {i}: {}", repr(&preview))));
            };
            let tag = found.get(1).unwrap().as_str();
            let arg = found.get(2).map(|m| m.as_str());
            match tag {
                "br" => out.push(0xF7),
                "wait" => out.push(0xF8),
                "end2" => out.push(0xF9),
                "name" => out.push(0xFA),
                "red" => out.extend([0xFB, 4]),
                "/red" => out.extend([0xFB, 5]),
                "pause" => out.extend([0xFB, 6]),
                "num" => out.extend([0xFB, 8]),
                "choice" => out.extend([0xFB, 9, decimal(arg)?]),
                "sym" => out.extend([0xFB, decimal(arg)?]),
                "fb07" => out.extend([0xFB, 7, hexadecimal(arg)?]),
                "fb" => out.extend([0xFB, hexadecimal(arg)?]),
                "fc" => out.extend([0xFC, hexadecimal(arg)?]),
                "fd" => out.extend([0xFD, hexadecimal(arg)?]),
                tag if tag.len() == 3 && (tag.starts_with('g') || tag.starts_with('x')) => {
                    out.push(hexadecimal(Some(&tag[1..]))?);
                }
                tag => return Err(Error::value(format!("unknown tag {{{tag}}}"))),
            }
            let end = at + found.get(0).unwrap().end();
            while chars.peek().is_some_and(|&(_, (next, _))| next < end) {
                chars.next();
            }
        } else {
            if let Some(code) = code_of(c) {
                out.push(code);
            } else if let Some(code) = extended.then(|| extended_code(c)).flatten() {
                out.push(code);
            } else if let Some(token) = cjk_codes.and_then(|codes| codes.get(&c)) {
                out.extend_from_slice(token);
            } else {
                return Err(Error::value(format!("no glyph for {}", repr(&c.to_string()))));
            }
            chars.next();
        }
    }
    Ok(out)
}

fn extended_code(c: char) -> Option<u8> {
    cyrillic::table().code_of.get(&c).or_else(|| latin::table().code_of.get(&c)).copied()
}

pub fn latin_advance(code: u8) -> Option<u32> {
    if let Some(&advance) = cyrillic::table().advance_of.get(&code) {
        return Some(advance);
    }
    let table = latin::table();
    let glyph = &table.glyphs[*table.by_code.get(&code)?];
    Some(match glyph.base {
        Some(base) => match ADVANCE[code_of(base)? as usize] {
            0 => 7,
            advance => advance,
        },
        None => glyph.art_advance,
    })
}

/// A message bank's pages: `FE <id> text FF|F9 ...`. (id -> (page without its end, end byte), bytes used)
pub fn pages(bank: &[u8]) -> (IndexMap<u8, (Vec<u8>, u8)>, usize) {
    let mut out = IndexMap::new();
    if bank.len() < 2 || bank[0] != 0xFE {
        return (out, 0);
    }
    let mut i = 0;
    while i + 1 < bank.len() && bank[i] == 0xFE {
        let id = bank[i + 1];
        let mut j = i + 2;
        while j < bank.len() && bank[j] != 0xFF && bank[j] != 0xF9 {
            let mut step = 1;
            if bank[j] == 0xFC || bank[j] == 0xFD {
                step = 2;
            } else if bank[j] == 0xFB && j + 1 < bank.len() {
                step = if bank[j + 1] == 7 || bank[j + 1] == 9 { 3 } else { 2 };
            }
            j += step;
        }
        if j >= bank.len() {
            break;
        }
        out.insert(id, (bank[i + 2..j].to_vec(), bank[j]));
        i = j + 1;
    }
    (out, i)
}
