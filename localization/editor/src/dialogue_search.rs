//! Search locally decoded dialogue and route results to readable scene names.
use std::collections::HashMap;
use std::sync::OnceLock;

use indexmap::IndexMap;
use regex::Regex;
use unicode_normalization::UnicodeNormalization;

use crate::catalog::Scene;
use crate::pycompat::collapse_whitespace;

pub fn tag_pattern() -> &'static Regex {
    static PATTERN: OnceLock<Regex> = OnceLock::new();
    PATTERN.get_or_init(|| Regex::new(r"\{[^{}]+\}").unwrap())
}

/// Text continues across control markers and box breaks.
pub fn readable_dialogue(text: &str) -> String {
    let replaced = tag_pattern().replace_all(text, |found: &regex::Captures<'_>| {
        if &found[0] == "{name}" { "Aya" } else { " " }.to_owned()
    });
    collapse_whitespace(&replaced)
}

pub fn casefold(text: &str) -> String {
    caseless::default_case_fold_str(text)
}

pub fn normalize(text: &str) -> String {
    let normalized: String = text.nfkc().collect();
    let mut punctuated = String::with_capacity(normalized.len());
    for c in normalized.chars() {
        match c {
            '’' | '‘' => punctuated.push('\''),
            '“' | '”' => punctuated.push('"'),
            '…' => punctuated.push_str("..."),
            c => punctuated.push(c),
        }
    }
    collapse_whitespace(&casefold(&punctuated))
}

/// One named view per line, preferring a direct room reference.
pub fn preferred_scenes(scenes: &IndexMap<String, Scene>) -> HashMap<String, String> {
    let mut ranked: Vec<(&String, &Scene)> = scenes.iter().collect();
    ranked.sort_by_key(|(scope, scene)| {
        (
            scope.as_str() != "opera",
            scene.area.as_deref().unwrap_or("").is_empty(),
            scope.starts_with("bank-"),
            scene.shared,
        )
    });
    let mut result = HashMap::new();
    for (scope, scene) in ranked {
        for key in &scene.keys {
            result.entry(key.clone()).or_insert_with(|| scope.clone());
        }
    }
    result
}

pub type SearchIndex = IndexMap<String, (String, String)>;

pub fn build_index(original: &IndexMap<String, String>) -> SearchIndex {
    original
        .iter()
        .filter_map(|(key, raw)| {
            let text = readable_dialogue(raw);
            (!text.is_empty()).then(|| {
                let searchable = normalize(&text);
                (key.clone(), (text, searchable))
            })
        })
        .collect()
}

/// (key, excerpt) for each line containing the query.
pub fn dialogue_matches(index: &SearchIndex, query: &str) -> Vec<(String, String)> {
    let needle = normalize(query);
    if needle.is_empty() {
        return Vec::new();
    }
    let needle_len = needle.chars().count();
    let mut results = Vec::new();
    for (key, (text, searchable)) in index {
        let Some(byte_at) = searchable.find(&needle) else {
            continue;
        };
        // Positions in the normalized text select the excerpt from the readable text.
        let at = searchable[..byte_at].chars().count();
        let chars: Vec<char> = text.chars().collect();
        let start = at.saturating_sub(32);
        let end = chars.len().min((start + 105).max(at + needle_len));
        let body: String = chars.get(start..end).unwrap_or_default().iter().collect();
        let snippet = format!("{}{}{}", if start > 0 { "…" } else { "" }, body, if end < chars.len() { "…" } else { "" });
        results.push((key.clone(), snippet));
    }
    results
}
