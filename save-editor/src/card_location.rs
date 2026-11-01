//! Where the game keeps its memory cards, so the editor opens the cards the game actually uses.
//!
//! The location contract (`src/main/boot/main.c`: `Host_InitOptions` picks the settings
//! folder, `Host_CardPath` puts the cards in its `memory-cards` subfolder):
//!
//! | Launch                        | Settings folder                                     |
//! |-------------------------------|-----------------------------------------------------|
//! | `PE_CONFIG_HOME` set, nonempty | `$PE_CONFIG_HOME`                                   |
//! | Windows                       | `%APPDATA%\Parasite Eve` (`SDL_GetPrefPath("", "Parasite Eve")`) |
//! | macOS                         | `~/Library/Application Support/Parasite Eve` (`SDL_GetPrefPath`) |
//! | Linux and other Unix          | `$XDG_CONFIG_HOME/parasite-eve`, else `~/.config/parasite-eve`  |
//!
//! Card 1 is `<settings>/memory-cards/bu00`, card 2 `<settings>/memory-cards/bu10`.
//! Builds before 0.7 kept `bu00`/`bu10` beside the game (its working directory or the
//! executable's folder); the game copies saves from there into the current cards at start.
//! The game still creates those folders empty, so an empty legacy folder says nothing about
//! where the saves are. The tests below are the contract's cases; the game's own are in
//! `tests/original_input_rules.c`.
use std::ffi::OsString;
use std::fs;
use std::path::{Path, PathBuf};

use anyhow::{Context, Result};

/// The platform rules of the contract (a parameter so every platform's case can be tested).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Platform {
    Windows,
    MacOs,
    Unix,
}

impl Platform {
    pub fn current() -> Self {
        if cfg!(windows) {
            Platform::Windows
        } else if cfg!(target_os = "macos") {
            Platform::MacOs
        } else {
            Platform::Unix
        }
    }
}

/// The game's settings folder, from the environment `env` on `platform`.
pub fn settings_dir_with(
    env: impl Fn(&str) -> Option<OsString>,
    platform: Platform,
) -> Option<PathBuf> {
    let var = |k: &str| env(k).filter(|v| !v.is_empty()).map(PathBuf::from);
    if let Some(home) = var("PE_CONFIG_HOME") {
        return Some(home);
    }
    match platform {
        Platform::Windows => var("APPDATA").map(|d| d.join("Parasite Eve")),
        Platform::MacOs => var("HOME").map(|h| h.join("Library/Application Support/Parasite Eve")),
        Platform::Unix => var("XDG_CONFIG_HOME")
            .map(|d| d.join("parasite-eve"))
            .or_else(|| var("HOME").map(|h| h.join(".config/parasite-eve"))),
    }
}

/// The game's settings folder on this machine.
pub fn settings_dir() -> Option<PathBuf> {
    settings_dir_with(|k| std::env::var_os(k), Platform::current())
}

/// The game's two current cards (`bu00`, `bu10`) under a settings folder.
pub fn cards_in(settings: &Path) -> [PathBuf; 2] {
    let root = settings.join("memory-cards");
    [root.join("bu00"), root.join("bu10")]
}

/// The game's two current cards on this machine.
pub fn current_cards() -> Option<[PathBuf; 2]> {
    settings_dir().map(|s| cards_in(&s))
}

/// Folders that held cards before 0.7: beside the working directory, its parent, and the
/// editor's own folder (the game is usually unpacked next to it). Existing ones only.
pub fn legacy_cards() -> Vec<PathBuf> {
    let mut roots = vec![PathBuf::from("."), PathBuf::from("..")];
    if let Some(dir) = std::env::current_exe()
        .ok()
        .and_then(|p| p.parent().map(Path::to_path_buf))
    {
        roots.push(dir);
    }
    let mut out: Vec<PathBuf> = Vec::new();
    for root in roots {
        for card in ["bu00", "bu10"] {
            let dir = root.join(card);
            let same = |p: &PathBuf| fs::canonicalize(p).ok() == fs::canonicalize(&dir).ok();
            if dir.is_dir() && !out.iter().any(same) && !is_current(&dir) {
                out.push(dir);
            }
        }
    }
    out
}

fn is_current(dir: &Path) -> bool {
    current_cards().is_some_and(|cards| {
        cards.iter().any(|c| {
            fs::canonicalize(c)
                .ok()
                .is_some_and(|c| fs::canonicalize(dir).ok() == Some(c))
        })
    })
}

/// A PsyZ save file as the game reads it: `BASLUS-00662` + 8 characters, exactly 8192 bytes.
pub fn is_save_file(path: &Path) -> bool {
    let named = path
        .file_name()
        .and_then(|n| n.to_str())
        .is_some_and(|n| n.len() == 20 && n.starts_with("BASLUS-00662"));
    named && fs::metadata(path).is_ok_and(|m| m.is_file() && m.len() == 8192)
}

/// How many PsyZ saves a card folder holds (0 when it is missing or unreadable).
pub fn save_count(dir: &Path) -> usize {
    fs::read_dir(dir)
        .map(|entries| {
            entries
                .flatten()
                .filter(|e| is_save_file(&e.path()))
                .count()
        })
        .unwrap_or(0)
}

/// What the editor opens at start when no path was given.
#[derive(Debug, PartialEq, Eq)]
pub enum StartCard {
    /// A current game card (0 = card 1, 1 = card 2).
    Current(usize, PathBuf),
    /// A legacy folder with saves, while both current cards are empty.
    Legacy(PathBuf),
}

/// The first current card with saves; else a legacy folder with saves; else current card 1
/// if the game has created it (an empty card is still the right place to look); else nothing.
/// An empty legacy folder never wins, and sample cards are never picked.
pub fn start_card(current: Option<&[PathBuf; 2]>, legacy: &[PathBuf]) -> Option<StartCard> {
    if let Some(cards) = current {
        if let Some(i) = cards.iter().position(|c| save_count(c) > 0) {
            return Some(StartCard::Current(i, cards[i].clone()));
        }
    }
    if let Some(dir) = legacy.iter().find(|d| save_count(d) > 0) {
        return Some(StartCard::Legacy(dir.clone()));
    }
    current
        .filter(|cards| cards[0].is_dir())
        .map(|cards| StartCard::Current(0, cards[0].clone()))
}

/// What importing a folder of old saves into a current card would do.
#[derive(Debug, Default, PartialEq, Eq)]
pub struct ImportPlan {
    /// Saves to copy: (source, destination).
    pub copy: Vec<(PathBuf, PathBuf)>,
    /// Names the current card already has; those saves are left alone.
    pub existing: Vec<String>,
    /// Files that are not PsyZ saves (card images, wrong size), with why.
    pub rejected: Vec<(String, &'static str)>,
}

const CARD_IMAGES: [&str; 8] = ["srm", "mcr", "mcd", "gme", "mcs", "mc", "psv", "vmp"];

/// Plan copying the PsyZ saves in `source` into the card folder `target`. Nothing is written.
pub fn plan_import(source: &Path, target: &Path) -> Result<ImportPlan> {
    let mut plan = ImportPlan::default();
    let mut entries: Vec<_> = fs::read_dir(source)
        .with_context(|| format!("cannot read {}", source.display()))?
        .flatten()
        .map(|e| e.path())
        .filter(|p| p.is_file())
        .collect();
    entries.sort();
    for path in entries {
        let name = path
            .file_name()
            .unwrap_or_default()
            .to_string_lossy()
            .into_owned();
        if is_save_file(&path) {
            let dest = target.join(&name);
            if dest.exists() {
                plan.existing.push(name);
            } else {
                plan.copy.push((path, dest));
            }
        } else if name.starts_with("BASLUS-00662") {
            plan.rejected.push((name, "not an 8192-byte PsyZ save"));
        } else if path
            .extension()
            .and_then(|e| e.to_str())
            .is_some_and(|e| CARD_IMAGES.contains(&e.to_ascii_lowercase().as_str()))
        {
            plan.rejected.push((
                name,
                "a whole memory card image: open it, then export to the card",
            ));
        }
    }
    Ok(plan)
}

/// Copy the planned saves. Each lands under a temporary name and is renamed into place, never
/// over an existing file; sources are kept. Returns how many were copied.
pub fn run_import(plan: &ImportPlan) -> Result<usize> {
    let mut done = 0;
    for (src, dest) in &plan.copy {
        if dest.exists() {
            continue;
        }
        if let Some(dir) = dest.parent() {
            fs::create_dir_all(dir).with_context(|| format!("cannot create {}", dir.display()))?;
        }
        let tmp = dest.with_extension("pe-editor-import.tmp");
        fs::copy(src, &tmp).with_context(|| format!("cannot copy {}", src.display()))?;
        if dest.exists() {
            let _ = fs::remove_file(&tmp);
            continue;
        }
        if let Err(e) = fs::rename(&tmp, dest) {
            let _ = fs::remove_file(&tmp);
            return Err(e).with_context(|| format!("cannot write {}", dest.display()));
        }
        done += 1;
    }
    Ok(done)
}

/// Open a folder in the system's file manager.
pub fn reveal(dir: &Path) -> Result<()> {
    let program = if cfg!(windows) {
        "explorer"
    } else if cfg!(target_os = "macos") {
        "open"
    } else {
        "xdg-open"
    };
    std::process::Command::new(program)
        .arg(dir)
        .spawn()
        .with_context(|| format!("cannot open {}", dir.display()))?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn env<'a>(vars: &'a [(&'a str, &'a str)]) -> impl Fn(&str) -> Option<OsString> + 'a {
        move |k| {
            vars.iter()
                .find(|(n, _)| *n == k)
                .map(|(_, v)| OsString::from(v))
        }
    }

    // The contract's cases: the same launches the game resolves in main.c.
    #[test]
    fn settings_folder_follows_the_game() {
        let cases: [(&[(&str, &str)], Platform, Option<&str>); 9] = [
            (
                &[("PE_CONFIG_HOME", "/custom"), ("APPDATA", "C:/AppData")],
                Platform::Windows,
                Some("/custom"),
            ),
            (
                &[("PE_CONFIG_HOME", "/custom"), ("XDG_CONFIG_HOME", "/xdg")],
                Platform::Unix,
                Some("/custom"),
            ),
            (
                &[("PE_CONFIG_HOME", ""), ("APPDATA", "C:/AppData")],
                Platform::Windows,
                Some("C:/AppData/Parasite Eve"),
            ),
            (
                &[("APPDATA", "C:/AppData")],
                Platform::Windows,
                Some("C:/AppData/Parasite Eve"),
            ),
            (
                &[("HOME", "/Users/aya")],
                Platform::MacOs,
                Some("/Users/aya/Library/Application Support/Parasite Eve"),
            ),
            (
                &[("XDG_CONFIG_HOME", "/xdg"), ("HOME", "/home/aya")],
                Platform::Unix,
                Some("/xdg/parasite-eve"),
            ),
            (
                &[("XDG_CONFIG_HOME", ""), ("HOME", "/home/aya")],
                Platform::Unix,
                Some("/home/aya/.config/parasite-eve"),
            ),
            (
                &[("HOME", "/home/aya")],
                Platform::Unix,
                Some("/home/aya/.config/parasite-eve"),
            ),
            (&[], Platform::Unix, None),
        ];
        for (vars, platform, want) in cases {
            assert_eq!(
                settings_dir_with(env(vars), platform),
                want.map(PathBuf::from),
                "{vars:?} {platform:?}"
            );
        }
        let cards = cards_in(Path::new("/home/aya/.config/parasite-eve"));
        assert_eq!(
            cards[0],
            PathBuf::from("/home/aya/.config/parasite-eve/memory-cards/bu00")
        );
        assert_eq!(
            cards[1],
            PathBuf::from("/home/aya/.config/parasite-eve/memory-cards/bu10")
        );
    }

    fn scratch(tag: &str) -> PathBuf {
        let dir =
            std::env::temp_dir().join(format!("pe-card-location-{tag}-{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    fn save(path: &Path, fill: u8) {
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        fs::write(path, vec![fill; 8192]).unwrap();
    }

    #[test]
    fn start_card_prefers_current_saves_over_an_empty_legacy_folder() {
        let dir = scratch("start");
        let cards = cards_in(&dir.join("settings"));
        let legacy = dir.join("game/bu00");
        fs::create_dir_all(&legacy).unwrap();
        // Nothing anywhere: no card yet, and the empty legacy folder is not picked.
        assert_eq!(start_card(Some(&cards), &[legacy.clone()]), None);
        // The game created an empty card 1: that is where to look.
        fs::create_dir_all(&cards[0]).unwrap();
        assert_eq!(
            start_card(Some(&cards), &[legacy.clone()]),
            Some(StartCard::Current(0, cards[0].clone()))
        );
        // Old saves only beside the game: open them, labelled legacy.
        save(&legacy.join("BASLUS-006620000000A"), 1);
        assert_eq!(
            start_card(Some(&cards), &[legacy.clone()]),
            Some(StartCard::Legacy(legacy.clone()))
        );
        // Saves on card 2 win over the legacy folder.
        save(&cards[1].join("BASLUS-006620000000A"), 2);
        assert_eq!(
            start_card(Some(&cards), &[legacy.clone()]),
            Some(StartCard::Current(1, cards[1].clone()))
        );
        // A wrong-sized file is not a save.
        fs::write(cards[0].join("BASLUS-006620000001B"), b"short").unwrap();
        assert_eq!(save_count(&cards[0]), 0);
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn import_copies_new_saves_and_never_replaces_one() {
        let dir = scratch("import");
        let (source, target) = (dir.join("old/bu00"), dir.join("settings/memory-cards/bu00"));
        save(&source.join("BASLUS-006620000000A"), 0x41);
        save(&source.join("BASLUS-006620000001B"), 0x42);
        save(&target.join("BASLUS-006620000001B"), 0x62);
        fs::write(source.join("BASLUS-006620000002C"), b"short").unwrap();
        fs::write(source.join("card.SRM"), b"image").unwrap();
        fs::write(source.join("notes.txt"), b"ignored").unwrap();
        let plan = plan_import(&source, &target).unwrap();
        assert_eq!(
            plan.copy,
            vec![(
                source.join("BASLUS-006620000000A"),
                target.join("BASLUS-006620000000A")
            )]
        );
        assert_eq!(plan.existing, vec!["BASLUS-006620000001B".to_string()]);
        assert_eq!(
            plan.rejected
                .iter()
                .map(|(n, _)| n.as_str())
                .collect::<Vec<_>>(),
            ["BASLUS-006620000002C", "card.SRM"]
        );
        assert_eq!(run_import(&plan).unwrap(), 1);
        assert_eq!(
            fs::read(target.join("BASLUS-006620000000A")).unwrap(),
            vec![0x41; 8192]
        );
        assert_eq!(
            fs::read(target.join("BASLUS-006620000001B")).unwrap(),
            vec![0x62; 8192]
        );
        assert!(
            source.join("BASLUS-006620000000A").exists(),
            "sources are kept"
        );
        // Running the same plan again copies nothing.
        assert_eq!(run_import(&plan).unwrap(), 0);
        let _ = fs::remove_dir_all(&dir);
    }
}
