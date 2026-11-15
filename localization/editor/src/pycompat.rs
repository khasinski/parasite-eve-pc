//! Text and JSON behaviour the file formats and messages rely on: Python's definition of
//! whitespace, `repr`, UTF-8 decode messages, and `json.dumps` spacing.
use serde_json::Value;

use crate::error::{Error, Result};

pub fn is_space(c: char) -> bool {
    matches!(c,
        '\t'..='\r' | '\x1c'..='\x1f' | ' ' | '\u{85}' | '\u{a0}' | '\u{1680}'
        | '\u{2000}'..='\u{200a}' | '\u{2028}' | '\u{2029}' | '\u{202f}' | '\u{205f}' | '\u{3000}')
}

pub fn strip(text: &str) -> &str {
    text.trim_matches(is_space)
}

/// `" ".join(text.split())`
pub fn collapse_whitespace(text: &str) -> String {
    text.split(is_space).filter(|part| !part.is_empty()).collect::<Vec<_>>().join(" ")
}

fn is_printable(c: char) -> bool {
    let cp = c as u32;
    !matches!(cp,
        0..=0x1f | 0x7f..=0xa0 | 0xad | 0x600..=0x605 | 0x61c | 0x6dd | 0x70f | 0x1680 | 0x180e
        | 0x2000..=0x200f | 0x2028..=0x202f | 0x205f..=0x206f | 0x3000 | 0xd800..=0xf8ff
        | 0xfeff | 0xfff0..=0xfffb | 0xe0000..=0x10ffff)
}

/// `repr(text)` for a string.
pub fn repr(text: &str) -> String {
    let quote = if text.contains('\'') && !text.contains('"') { '"' } else { '\'' };
    let mut out = String::with_capacity(text.len() + 2);
    out.push(quote);
    for c in text.chars() {
        match c {
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if c == quote => {
                out.push('\\');
                out.push(c);
            }
            c if is_printable(c) => out.push(c),
            c => {
                let cp = c as u32;
                if cp <= 0xff {
                    out.push_str(&format!("\\x{cp:02x}"));
                } else if cp <= 0xffff {
                    out.push_str(&format!("\\u{cp:04x}"));
                } else {
                    out.push_str(&format!("\\U{cp:08x}"));
                }
            }
        }
    }
    out.push(quote);
    out
}

/// Decode UTF-8, dropping one leading byte order mark.
pub fn decode_utf8_sig(data: &[u8]) -> Result<String> {
    let body = data.strip_prefix(b"\xef\xbb\xbf").unwrap_or(data);
    std::str::from_utf8(body).map(str::to_owned).map_err(|error| {
        let at = error.valid_up_to();
        let first = body[at];
        match error.error_len() {
            None if body.len() - at > 1 => Error::value(format!(
                "'utf-8' codec can't decode bytes in position {at}-{}: unexpected end of data",
                body.len() - 1
            )),
            None => Error::value(format!(
                "'utf-8' codec can't decode byte 0x{first:02x} in position {at}: unexpected end of data"
            )),
            Some(_) => {
                let reason = if (0xc2..=0xf4).contains(&first) { "invalid continuation byte" } else { "invalid start byte" };
                Error::value(format!("'utf-8' codec can't decode byte 0x{first:02x} in position {at}: {reason}"))
            }
        }
    })
}

fn write_string(out: &mut String, text: &str, ensure_ascii: bool) {
    out.push('"');
    for c in text.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            '\x08' => out.push_str("\\b"),
            '\x0c' => out.push_str("\\f"),
            c if (c as u32) < 0x20 => out.push_str(&format!("\\u{:04x}", c as u32)),
            c if ensure_ascii && !c.is_ascii() => {
                let mut units = [0u16; 2];
                for unit in c.encode_utf16(&mut units) {
                    out.push_str(&format!("\\u{unit:04x}"));
                }
            }
            c => out.push(c),
        }
    }
    out.push('"');
}

#[derive(Clone, Copy)]
pub struct JsonStyle<'a> {
    pub item: &'a str,
    pub key: &'a str,
    pub indent: Option<usize>,
    pub ensure_ascii: bool,
}

/// `json.dumps(value, ensure_ascii=False)`
pub const JSON_DEFAULT: JsonStyle = JsonStyle { item: ", ", key: ": ", indent: None, ensure_ascii: false };
/// `json.dumps(value, ensure_ascii=False, indent=2)`
pub const JSON_INDENTED: JsonStyle = JsonStyle { item: ",", key: ": ", indent: Some(2), ensure_ascii: false };
/// `json.dumps(value, separators=(",", ":"))`
pub const JSON_COMPACT_ASCII: JsonStyle = JsonStyle { item: ",", key: ":", indent: None, ensure_ascii: true };

fn newline(out: &mut String, style: JsonStyle, level: usize) {
    if let Some(indent) = style.indent {
        out.push('\n');
        out.push_str(&" ".repeat(indent * level));
    }
}

fn write_value(out: &mut String, value: &Value, style: JsonStyle, level: usize) {
    match value {
        Value::Null => out.push_str("null"),
        Value::Bool(flag) => out.push_str(if *flag { "true" } else { "false" }),
        Value::Number(number) => out.push_str(&number.to_string()),
        Value::String(text) => write_string(out, text, style.ensure_ascii),
        Value::Array(items) if items.is_empty() => out.push_str("[]"),
        Value::Array(items) => {
            out.push('[');
            for (n, item) in items.iter().enumerate() {
                if n > 0 {
                    out.push_str(style.item);
                }
                newline(out, style, level + 1);
                write_value(out, item, style, level + 1);
            }
            newline(out, style, level);
            out.push(']');
        }
        Value::Object(map) if map.is_empty() => out.push_str("{}"),
        Value::Object(map) => {
            out.push('{');
            for (n, (key, item)) in map.iter().enumerate() {
                if n > 0 {
                    out.push_str(style.item);
                }
                newline(out, style, level + 1);
                write_string(out, key, style.ensure_ascii);
                out.push_str(style.key);
                write_value(out, item, style, level + 1);
            }
            newline(out, style, level);
            out.push('}');
        }
    }
}

pub fn dumps(value: &Value, style: JsonStyle) -> String {
    let mut out = String::new();
    write_value(&mut out, value, style, 0);
    out
}

/// `{value:,}`
pub fn thousands(value: usize) -> String {
    let digits = value.to_string();
    let mut out = String::new();
    for (n, c) in digits.chars().enumerate() {
        if n > 0 && (digits.len() - n) % 3 == 0 {
            out.push(',');
        }
        out.push(c);
    }
    out
}
