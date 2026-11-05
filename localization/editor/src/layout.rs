//! Conservative dialogue layout checks for the native textbox renderer (`petext/layout.py`).
//! No automatic reflow here; unknown layouts remain explicit warnings.
use std::sync::OnceLock;

use regex::Regex;

use crate::catalog::Row;
use crate::error::Result;
use crate::gametext::{self as gt, WIDE};
use crate::validation::commands;

const DEFAULT_WIDTH: i64 = 297;
const DEFAULT_LINES: i64 = 4;
pub const NUMBER_WIDTH: u32 = 5 * WIDE;

/// {name} is laid out as "Aya", the game's default name.
pub fn name_width() -> u32 {
    [0x10u8, 0x48, 0x30].iter().map(|&g| gt::box_advance(g).unwrap()).sum()
}

/// How far one command moves the box's pen; 0 for breaks and waits.
pub fn advance(c: &[u8], name_px: u32) -> u32 {
    let op = c[0];
    match op {
        0xFA => name_px,
        0xFB if c[1] <= 3 => WIDE,
        0xFB if c[1] == 8 => NUMBER_WIDTH,
        0xFB => 0,
        0xF1 | 0xFC | 0xFD => WIDE,
        op if op < 0xF1 => gt::box_advance(op).unwrap_or_else(|| gt::latin_advance(op).filter(|&a| a != 0).unwrap_or(WIDE)),
        _ => 0,
    }
}

/// Lines' pixel widths per screen.
pub fn screens(raw: &[u8], name_px: u32) -> Result<Vec<Vec<u32>>> {
    let (mut pages, mut lines, mut width) = (Vec::new(), Vec::new(), 0);
    for c in commands(raw)? {
        if c[0] == 0xF7 || c[0] == 0xF8 {
            lines.push(width);
            width = 0;
            if c[0] == 0xF8 {
                pages.push(std::mem::take(&mut lines));
            }
        } else {
            width += advance(c, name_px);
        }
    }
    lines.push(width);
    pages.push(lines);
    Ok(pages)
}

fn box_pattern() -> &'static Regex {
    static PATTERN: OnceLock<Regex> = OnceLock::new();
    PATTERN.get_or_init(|| Regex::new(r"^(?:styled )?(-?[0-9]+),(-?[0-9]+),([0-9]+),([0-9]+)$").unwrap())
}

/// (width px, lines) a box holds. A styled box's rectangle is what looks right and the screen edge
/// (`screen`) is where text is lost.
pub fn bounds(rect: &str, screen: bool) -> Option<(i64, i64)> {
    if rect == "default" {
        return Some((DEFAULT_WIDTH, DEFAULT_LINES));
    }
    let found = box_pattern().captures(rect)?;
    let number = |n: usize| found[n].parse::<i64>().unwrap_or(i64::MAX);
    let (x, y, mut w, mut h) = (number(1), number(2), number(3), number(4));
    if x < 0 || y < 0 || x >= 320 || y >= 240 || w == 0 || h == 0 {
        return None;
    }
    if screen {
        (w, h) = (320, 240);
    }
    // Styled boxes normally begin six pixels below y. Reserve that inset even when a script might
    // suppress it, and clip to the visible screen.
    Some((w.min(320 - x).max(0), (h - 6).min(240 - y - 6).div_euclid(gt::LINE_HEIGHT).max(0)))
}

/// (width, lines) to wrap a page to: the narrowest of the boxes scripts open it in, else of the
/// boxes its bank opens when that set is complete.
pub fn box_limit(source: &Row) -> Option<(i64, i64)> {
    let boxes = if !source.boxes.is_empty() {
        &source.boxes
    } else if source.boxes_complete {
        &source.boxes_possible
    } else {
        return None;
    };
    let limits: Vec<_> = boxes.iter().filter_map(|b| bounds(b, false)).collect();
    if limits.is_empty() {
        return None;
    }
    Some((limits.iter().map(|l| l.0).min()?, limits.iter().map(|l| l.1).min()?))
}

fn widest(page: &[u32]) -> i64 {
    page.iter().copied().max().unwrap_or(0) as i64
}

fn fits(pages: &[Vec<u32>], rect: &str) -> Option<bool> {
    let (width, count) = bounds(rect, false)?;
    Some(pages.iter().all(|p| widest(p) <= width && p.len() as i64 <= count))
}

/// A page no script ties to a box. It can only appear in a box its bank's rooms open.
fn unproven(pages: &[Vec<u32>], source: &Row) -> Vec<String> {
    if source.boxes_possible.is_empty() {
        return vec!["dialogue layout unknown: fit needs in-game review".to_owned()];
    }
    let mut results: Vec<(&str, Option<bool>)> = Vec::new();
    for rect in &source.boxes_possible {
        if !results.iter().any(|(seen, _)| seen == rect) {
            results.push((rect, fits(pages, rect)));
        }
    }
    let unsure = if source.boxes_complete { "" } else { "; some of its rooms did not decode, so the set may be larger" };
    if results.iter().all(|(_, ok)| *ok == Some(true)) {
        return if unsure.is_empty() {
            Vec::new()
        } else {
            vec![format!("no script ties this page to a box; fits every box its bank opens{unsure}")]
        };
    }
    let too_big: Vec<&str> = results.iter().filter(|(_, ok)| *ok == Some(false)).map(|(b, _)| *b).collect();
    let unknown = results.iter().filter(|(_, ok)| ok.is_none()).count();
    if too_big.len() == results.len() {
        return vec![format!(
            "no script ties this page to a box, and it fits none its bank opens ({}){unsure}",
            too_big.join(", ")
        )];
    }
    let mut note = if too_big.is_empty() { String::new() } else { format!("too big for {}", too_big.join(", ")) };
    if unknown > 0 {
        if !note.is_empty() {
            note.push_str("; ");
        }
        note.push_str(&format!("{unknown} box(es) with a computed rectangle"));
    }
    vec![format!("no script ties this page to a box: {note}; fit needs in-game review{unsure}")]
}

/// Dialogue pages against their boxes: (errors, warnings).
pub fn check(raw: &[u8], source: &Row, name_px: u32) -> Result<(Vec<String>, Vec<String>)> {
    let pages = screens(raw, name_px)?;
    let (mut errors, mut warnings) = (Vec::new(), Vec::new());
    if source.boxes.is_empty() {
        return Ok((errors, unproven(&pages, source)));
    }
    for rect in &source.boxes {
        let Some((width, count)) = bounds(rect, false) else {
            warnings.push(format!("dialogue layout unknown ({rect}): fit needs in-game review"));
            continue;
        };
        let (edge, rows) = bounds(rect, true).expect("a box with bounds has screen bounds");
        let suffix = if rect == "default" { "" } else { " before the screen edge" };
        for (n, page) in pages.iter().enumerate() {
            let (n, wide, lines) = (n + 1, widest(page), page.len() as i64);
            if wide > edge {
                errors.push(format!("screen {n}: a line is {wide} px; dialogue box {rect} fits {edge} px{suffix}"));
            } else if wide > width {
                warnings.push(format!(
                    "screen {n}: a line is {wide} px, past the {width} px of box {rect} (the English does this in places); check it in game"
                ));
            }
            if lines > rows {
                errors.push(format!("screen {n}: {lines} lines; dialogue box {rect} fits {rows} lines{suffix}"));
            } else if lines > count {
                warnings.push(format!("screen {n}: {lines} lines, past the {count} lines of box {rect}; check it in game"));
            }
        }
    }
    Ok((errors, warnings))
}
