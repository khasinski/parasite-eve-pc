//! Automatic line breaks and screens for dialogue pages that overflow their box (`petext/wrap.py`).
//! A page that fits keeps the translator's layout. A screen with a choice is never split and its
//! choice rows are never rewrapped. A word wider than the box stays as it is.
use std::collections::HashMap;

use crate::error::Result;
use crate::layout::advance;
use crate::validation::commands;

const SPACE: u8 = 0x0F;
const BREAK: u8 = 0xF7;
const WAIT: u8 = 0xF8;
const OPENING: &str = "（［｛〈《「『【〔〖〘〚“‘";
const CLOSING: &str = "、。，．！？：；）］｝〉》」』】〕〗〙〛”’ー々ぁぃぅぇぉっゃゅょァィゥェォッャュョ";

type Command = Vec<u8>;
pub type Characters = HashMap<Vec<u8>, char>;

fn count(haystack: &[u8], needle: &[u8]) -> usize {
    let (mut found, mut at) = (0, 0);
    while at + needle.len() <= haystack.len() {
        if &haystack[at..at + needle.len()] == needle {
            found += 1;
            at += needle.len();
        } else {
            at += 1;
        }
    }
    found
}

fn indents(english: &[u8]) -> (bool, bool) {
    let rule = |mark: u8| {
        let total = count(english, &[mark]);
        count(english, &[mark, SPACE]) * 2 >= total && total > 0
    };
    (rule(BREAK), rule(WAIT))
}

fn is_choice(c: &[u8]) -> bool {
    c[0] == 0xFB && c[1] == 9
}

fn wrap_line(line: &[Command], width: i64, name_px: u32, indent: bool, characters: Option<&Characters>) -> Vec<Vec<Command>> {
    let (mut words, mut word): (Vec<Vec<Command>>, Vec<Command>) = (Vec::new(), Vec::new());
    let char_of = |c: &[u8]| characters.and_then(|map| map.get(c)).copied();
    for c in line {
        let prev = word.last().and_then(|w| char_of(w));
        let current = char_of(c);
        let cjk_break = !word.is_empty()
            && (prev.is_some() || current.is_some())
            && prev.is_none_or(|p| !OPENING.contains(p))
            && current.is_none_or(|n| !CLOSING.contains(n));
        if (c[0] == SPACE && word.last().is_some_and(|w| w[0] != SPACE)) || cjk_break {
            words.push(std::mem::take(&mut word));
        }
        word.push(c.clone());
    }
    if !word.is_empty() {
        words.push(word);
    }
    let pixels = |cs: &[Command]| cs.iter().map(|c| advance(c, name_px) as i64).sum::<i64>();
    let (mut out, mut cur, mut cur_w): (Vec<Vec<Command>>, Vec<Command>, i64) = (Vec::new(), Vec::new(), 0);
    for w in words {
        let w_px = pixels(&w);
        if !cur.is_empty() && cur_w + w_px > width {
            out.push(std::mem::take(&mut cur));
            // The break replaces the spaces before the word.
            let body = w.iter().skip_while(|c| c[0] == SPACE).cloned();
            cur = if indent { vec![vec![SPACE]] } else { Vec::new() };
            cur.extend(body);
            cur_w = pixels(&cur);
        } else {
            cur.extend(w);
            cur_w += w_px;
        }
    }
    out.push(cur);
    out
}

/// (the page rewrapped to the box, the number of screens added)
pub fn wrap(raw: &[u8], width: i64, lines: i64, english: &[u8], name_px: u32, characters: Option<&Characters>) -> Result<(Vec<u8>, usize)> {
    let (indent_line, indent_screen) = indents(english);
    let (mut screens, mut cur): (Vec<(Vec<Command>, Option<Command>)>, Vec<Command>) = (Vec::new(), Vec::new());
    for c in commands(raw)? {
        if c[0] == WAIT {
            screens.push((std::mem::take(&mut cur), Some(c.to_vec())));
        } else {
            cur.push(c.to_vec());
        }
    }
    screens.push((cur, None));
    let (mut out, mut added) = (Vec::new(), 0);
    for (body, wait) in screens {
        let (mut rows, mut row): (Vec<Vec<Command>>, Vec<Command>) = (Vec::new(), Vec::new());
        for c in body {
            if c[0] == BREAK {
                rows.push(std::mem::take(&mut row));
            } else {
                row.push(c);
            }
        }
        rows.push(row);
        let choice_at = rows.iter().position(|r| r.iter().any(|c| is_choice(c)));
        let mut wrapped: Vec<Vec<Command>> = Vec::new();
        for (i, r) in rows.iter().enumerate() {
            if choice_at.is_some_and(|at| i > at) {
                wrapped.push(r.clone());
            } else {
                wrapped.extend(wrap_line(r, width, name_px, indent_line, characters));
            }
        }
        let groups: Vec<Vec<Vec<Command>>> = if choice_at.is_none() && wrapped.len() as i64 > lines {
            let chunks: Vec<Vec<Vec<Command>>> = wrapped.chunks(lines.max(1) as usize).map(<[_]>::to_vec).collect();
            added += chunks.len() - 1;
            chunks
        } else {
            vec![wrapped]
        };
        for (g, mut group) in groups.into_iter().enumerate() {
            if g > 0 {
                out.push(WAIT);
                if indent_screen && !group.is_empty() && group[0].first().is_none_or(|c| c[0] != SPACE) {
                    group[0].insert(0, vec![SPACE]);
                }
            }
            for (n, r) in group.iter().enumerate() {
                if n > 0 {
                    out.push(BREAK);
                }
                for c in r {
                    out.extend_from_slice(c);
                }
            }
        }
        if let Some(wait) = wait {
            out.extend_from_slice(&wait);
        }
    }
    Ok((out, added))
}
