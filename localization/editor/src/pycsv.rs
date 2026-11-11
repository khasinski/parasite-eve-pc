//! The CSV dialect of the exchange files: quoting exactly as the exported files have always been
//! written, and a line-fed reader with strict and lenient modes.
use crate::error::{Error, Result};

const FIELD_LIMIT: usize = 131_072;

pub fn write_row(out: &mut String, fields: &[&str]) {
    for (n, field) in fields.iter().enumerate() {
        if n > 0 {
            out.push(',');
        }
        if field.contains([',', '"', '\r', '\n']) {
            out.push('"');
            out.push_str(&field.replace('"', "\"\""));
            out.push('"');
        } else {
            out.push_str(field);
        }
    }
    out.push_str("\r\n");
}

/// Lines with their endings, split at `\n`, `\r` and `\r\n`.
pub fn universal_lines(text: &str) -> Vec<&str> {
    let bytes = text.as_bytes();
    let (mut lines, mut start, mut at) = (Vec::new(), 0, 0);
    while at < bytes.len() {
        let end = match bytes[at] {
            b'\n' => at + 1,
            b'\r' if bytes.get(at + 1) == Some(&b'\n') => at + 2,
            b'\r' => at + 1,
            _ => {
                at += 1;
                continue;
            }
        };
        lines.push(&text[start..end]);
        start = end;
        at = end;
    }
    if start < text.len() {
        lines.push(&text[start..]);
    }
    lines
}

/// `str.splitlines(keepends=True)`
pub fn splitlines_keepends(text: &str) -> Vec<&str> {
    let (mut lines, mut start) = (Vec::new(), 0);
    let mut chars = text.char_indices().peekable();
    while let Some((at, c)) = chars.next() {
        let is_break = matches!(c, '\n' | '\r' | '\x0b' | '\x0c' | '\x1c' | '\x1d' | '\x1e' | '\u{85}' | '\u{2028}' | '\u{2029}');
        if !is_break {
            continue;
        }
        let mut end = at + c.len_utf8();
        if c == '\r' && matches!(chars.peek(), Some((_, '\n'))) {
            chars.next();
            end += 1;
        }
        lines.push(&text[start..end]);
        start = end;
    }
    if start < text.len() {
        lines.push(&text[start..]);
    }
    lines
}

#[derive(Clone, Copy, PartialEq)]
enum State {
    StartRecord,
    StartField,
    InField,
    InQuotedField,
    QuoteInQuotedField,
    EatCrNl,
}

pub struct Reader<'a> {
    lines: std::vec::IntoIter<&'a str>,
    strict: bool,
    pub line_num: usize,
}

impl<'a> Reader<'a> {
    pub fn new(lines: Vec<&'a str>, strict: bool) -> Self {
        Self { lines: lines.into_iter(), strict, line_num: 0 }
    }

    /// The next record, `Vec::new()` for a blank line, `None` at the end of the input.
    pub fn next_row(&mut self) -> Result<Option<Vec<String>>> {
        let mut parser = Parser { state: State::StartRecord, fields: Vec::new(), field: String::new(), strict: self.strict };
        loop {
            let Some(line) = self.lines.next() else {
                if !parser.field.is_empty() || parser.state == State::InQuotedField {
                    if self.strict {
                        return Err(Error::value("unexpected end of data"));
                    }
                    parser.save_field();
                    return Ok(Some(parser.fields));
                }
                return Ok(None);
            };
            self.line_num += 1;
            for c in line.chars() {
                parser.process(Some(c))?;
            }
            parser.process(None)?;
            if parser.state == State::StartRecord {
                return Ok(Some(parser.fields));
            }
        }
    }
}

struct Parser {
    state: State,
    fields: Vec<String>,
    field: String,
    strict: bool,
}

impl Parser {
    fn save_field(&mut self) {
        self.fields.push(std::mem::take(&mut self.field));
    }

    fn add(&mut self, c: char) -> Result<()> {
        if self.field.chars().count() >= FIELD_LIMIT {
            return Err(Error::value(format!("field larger than field limit ({FIELD_LIMIT})")));
        }
        self.field.push(c);
        Ok(())
    }

    fn end_record(&mut self, c: Option<char>) {
        self.save_field();
        self.state = if c.is_none() { State::StartRecord } else { State::EatCrNl };
    }

    fn process(&mut self, c: Option<char>) -> Result<()> {
        let line_end = matches!(c, None | Some('\n') | Some('\r'));
        if self.state == State::StartRecord {
            match c {
                None => return Ok(()),
                Some('\n') | Some('\r') => {
                    self.state = State::EatCrNl;
                    return Ok(());
                }
                _ => self.state = State::StartField,
            }
        }
        match self.state {
            State::StartRecord => unreachable!(),
            State::StartField => {
                if line_end {
                    self.end_record(c);
                } else if c == Some('"') {
                    self.state = State::InQuotedField;
                } else if c == Some(',') {
                    self.save_field();
                } else {
                    self.add(c.unwrap_or_default())?;
                    self.state = State::InField;
                }
            }
            State::InField => {
                if line_end {
                    self.end_record(c);
                } else if c == Some(',') {
                    self.save_field();
                    self.state = State::StartField;
                } else {
                    self.add(c.unwrap_or_default())?;
                }
            }
            State::InQuotedField => match c {
                None => {}
                Some('"') => self.state = State::QuoteInQuotedField,
                Some(c) => self.add(c)?,
            },
            State::QuoteInQuotedField => {
                if c == Some('"') {
                    self.add('"')?;
                    self.state = State::InQuotedField;
                } else if c == Some(',') {
                    self.save_field();
                    self.state = State::StartField;
                } else if line_end {
                    self.end_record(c);
                } else if !self.strict {
                    self.add(c.unwrap_or_default())?;
                    self.state = State::InField;
                } else {
                    return Err(Error::value("',' expected after '\"'"));
                }
            }
            State::EatCrNl => {
                if c.is_none() {
                    self.state = State::StartRecord;
                } else if !line_end {
                    return Err(Error::value(
                        "new-line character seen in unquoted field - do you need to open the file with newline=''?",
                    ));
                }
            }
        }
        Ok(())
    }
}

/// One row keyed by the header. Short rows hold `None`; `extra` records fields past the header.
pub struct Row {
    pub fields: Vec<(String, Option<String>)>,
    pub extra: bool,
}

impl Row {
    pub fn get(&self, name: &str) -> Option<&Option<String>> {
        self.fields.iter().rev().find(|(field, _)| field == name).map(|(_, value)| value)
    }
}

pub struct DictReader<'a> {
    reader: Reader<'a>,
    pub fieldnames: Option<Vec<String>>,
}

impl<'a> DictReader<'a> {
    pub fn new(lines: Vec<&'a str>, strict: bool) -> Result<Self> {
        let mut reader = Reader::new(lines, strict);
        let fieldnames = reader.next_row()?;
        Ok(Self { reader, fieldnames })
    }

    pub fn line_num(&self) -> usize {
        self.reader.line_num
    }

    pub fn next_row(&mut self) -> Result<Option<Row>> {
        let names = self.fieldnames.as_deref().unwrap_or_default();
        loop {
            let Some(row) = self.reader.next_row()? else {
                return Ok(None);
            };
            if row.is_empty() {
                continue;
            }
            let extra = row.len() > names.len();
            let mut values = row.into_iter();
            let fields = names.iter().map(|name| (name.clone(), values.next())).collect();
            return Ok(Some(Row { fields, extra }));
        }
    }
}
