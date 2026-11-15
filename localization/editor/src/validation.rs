//! Runtime text commands (`petext/validation.py`). Published translations must be complete,
//! supported command streams; raw tag spellings do not bypass these checks.
use crate::error::{Error, Result};

/// Complete commands (glyphs are one-byte commands); no page terminator.
pub fn commands(raw: &[u8]) -> Result<Vec<&[u8]>> {
    let mut out = Vec::new();
    let mut i = 0;
    while i < raw.len() {
        let (start, c) = (i, raw[i]);
        i += 1;
        if c < 0xF1 || matches!(c, 0xF7 | 0xF8 | 0xFA) {
        } else if c == 0xF1 {
            if i + 3 > raw.len() || raw[i] != 0xC5 || raw[i + 1] >= 240 || raw[i + 2] >= 240 {
                return Err(Error::value(format!("invalid CJK token at byte {start}")));
            }
            i += 3;
        } else if c == 0xFB {
            if i == raw.len() || raw[i] > 9 {
                return Err(Error::value(format!("invalid or incomplete FB command at byte {start}")));
            }
            let sub = raw[i];
            i += 1;
            if sub == 7 || sub == 9 {
                if i == raw.len() {
                    return Err(Error::value(format!("missing FB {sub:02X} operand at byte {start}")));
                }
                if sub == 9 && !(1..=7).contains(&raw[i]) {
                    return Err(Error::value("choice count must be between 1 and 7"));
                }
                i += 1;
            }
        } else if c == 0xFC || c == 0xFD {
            if i == raw.len() {
                return Err(Error::value(format!("missing font-page operand at byte {start}")));
            }
            i += 1;
        } else {
            return Err(Error::value(format!(
                "unsupported control/embedded terminator {c:02X} at byte {start}; the compiler supplies the page end"
            )));
        }
        out.push(&raw[start..i]);
    }
    Ok(out)
}

/// Line breaks may be moved for translation. All other commands retain their order and operands.
pub fn structure(raw: &[u8]) -> Result<Vec<Vec<u8>>> {
    Ok(commands(raw)?.into_iter().filter(|c| c[0] >= 0xF8).map(<[u8]>::to_vec).collect())
}

pub fn check(raw: &[u8], source: &[Vec<u8>]) -> Result<()> {
    if structure(raw)? != source {
        return Err(Error::value(
            "control commands must match the source in count, order and operands \
             (including waits, choices, names, numbers and pauses)",
        ));
    }
    Ok(())
}
