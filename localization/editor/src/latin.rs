//! The Latin Extended glyph codes (`petext/latin.py`). The editor only needs each glyph's code and
//! pen advance; the mod composes the pixels at run time.
use std::collections::HashMap;
use std::sync::OnceLock;

pub const FIRST_CODE: u8 = 0x4D;

/// Whole signs drawn from these strokes instead of a game letter.
const ART: &[(char, &[&str])] = &[
    ('ß', &["", " ##", "#  #", "#  #", "# #", "#  #", "#   #", "#   #", "# ##"]),
    ('æ', &["", "", "", " ## ##", "   #  #", " ######", "#  #", "#  #  #", " ## ##"]),
    ('Æ', &["", "  #####", " # #", " # #", "#  ####", "####", "#  #", "#  #", "#  ####"]),
    ('œ', &["", "", "", " ## ##", "#  #  #", "#  ####", "#  #", "#  #  #", " ## ##"]),
    ('Œ', &["", " ######", "#  #", "#  #", "#  ####", "#  #", "#  #", "#  #", " ######"]),
    ('«', &["", "", "", "", " # #", "# #", " # #"]),
    ('»', &["", "", "", "", "# #", " # #", "# #"]),
    ('€', &["", "", "  ###", " #", "####", " #", "####", " #", "  ###"]),
    ('°', &["", " #", "# #", " #"]),
];

/// Accented letters and their base letters, lowercase; capitals follow in the same order.
const LETTERS: &[(&str, &str)] = &[
    ("àèìòù", "aeiou"),
    ("áéíóúý", "aeiouy"),
    ("âêîôû", "aeiou"),
    ("äëïöü", "aeiou"),
    ("ãõñ", "aon"),
    ("åů", "au"),
    ("çş", "cs"),
    ("ąę", "ae"),
    ("ćńśź", "cnsz"),
    ("ż", "z"),
    ("čěňřšž", "cenrsz"),
    ("ğ", "g"),
    ("őű", "ou"),
    ("ł", "l"),
    ("ø", "o"),
];

const SPECIAL: &[(char, char)] = &[
    ('ÿ', 'y'),
    ('Ÿ', 'Y'),
    ('ď', 'd'),
    ('ť', 't'),
    ('Ď', 'D'),
    ('Ť', 'T'),
    ('ı', 'i'),
    ('İ', 'I'),
    ('¿', '?'),
    ('¡', '!'),
];

pub struct Glyph {
    pub ch: char,
    pub code: u8,
    pub base: Option<char>,
    /// Ink width + 2 for a drawn sign; lettered glyphs use their base letter's advance.
    pub art_advance: u32,
}

pub struct Table {
    pub glyphs: Vec<Glyph>,
    pub code_of: HashMap<char, u8>,
    pub by_code: HashMap<u8, usize>,
}

fn build() -> Table {
    let mut spec: Vec<(char, char)> = Vec::new();
    for upper in [false, true] {
        for (marked, bases) in LETTERS {
            for (c, b) in marked.chars().zip(bases.chars()) {
                if upper {
                    spec.push((c.to_uppercase().next().unwrap(), b.to_ascii_uppercase()));
                } else {
                    spec.push((c, b));
                }
            }
        }
    }
    spec.extend_from_slice(SPECIAL);
    let mut glyphs: Vec<Glyph> = spec
        .iter()
        .enumerate()
        .map(|(i, &(ch, base))| Glyph { ch, code: FIRST_CODE + i as u8, base: Some(base), art_advance: 0 })
        .collect();
    let first_art = glyphs.len();
    for (i, (ch, rows)) in ART.iter().enumerate() {
        // Each row is drawn from column 1.
        let right = rows.iter().filter_map(|row| row.rfind('#')).max().unwrap_or(0) as u32 + 1;
        glyphs.push(Glyph { ch: *ch, code: FIRST_CODE + (first_art + i) as u8, base: None, art_advance: right + 2 });
    }
    let code_of = glyphs.iter().map(|g| (g.ch, g.code)).collect();
    let by_code = glyphs.iter().enumerate().map(|(i, g)| (g.code, i)).collect();
    Table { glyphs, code_of, by_code }
}

pub fn table() -> &'static Table {
    static TABLE: OnceLock<Table> = OnceLock::new();
    TABLE.get_or_init(build)
}
