//! Stable CLI entry point and command dispatch.
use std::fs;
use std::path::PathBuf;

use anyhow::{bail, Context, Result};
use clap::{Args, Parser, Subcommand};

use crate::memcard::MemoryCardContainer;
use crate::save::PeSaveSlot;
use crate::tables::{PARASITE_SPELLS, STAT_GROWTH_NAMES};

mod args;
mod catalog;
mod edit;
mod inspect;
mod transfer;

pub use args::{Cli, Command, EditArgs};
use catalog::cmd_list_items;
use edit::cmd_edit;
use inspect::{cmd_inspect, print_slot_summary};
use transfer::{cmd_export_card, cmd_export_json, cmd_import_json, cmd_import_psyz};

pub fn run_cli_command(cmd: Command) -> Result<()> {
    match cmd {
        Command::Gui { .. } => unreachable!("Handled in main"),
        Command::Inspect {
            path,
            slot,
            verbose,
        } => cmd_inspect(&path, slot, verbose),
        Command::Edit(args) => cmd_edit(args),
        Command::ImportPsyz { source, target_dir } => cmd_import_psyz(&source, &target_dir),
        Command::ExportCard {
            source,
            target_file,
        } => cmd_export_card(&source, &target_file),
        Command::ExportJson {
            source,
            output,
            slot,
        } => cmd_export_json(&source, &output, slot),
        Command::ImportJson {
            input,
            target,
            slot,
        } => cmd_import_json(&input, &target, slot),
        Command::ListItems { filter } => cmd_list_items(filter.as_deref()),
        Command::Cards => cmd_cards(),
    }
}

fn cmd_cards() -> Result<()> {
    use crate::card_location as cards;
    match cards::settings_dir() {
        Some(settings) => {
            println!("Game settings folder: {}", settings.display());
            for (i, dir) in cards::cards_in(&settings).iter().enumerate() {
                let state = if dir.is_dir() {
                    format!("{} save(s)", cards::save_count(dir))
                } else {
                    "not created yet".to_string()
                };
                println!("  card {}: {} ({})", i + 1, dir.display(), state);
            }
        }
        None => println!("Game settings folder: unknown (no PE_CONFIG_HOME, APPDATA or HOME)"),
    }
    let legacy = cards::legacy_cards();
    if !legacy.is_empty() {
        println!("Old card folders (before 0.7; the game imports new saves from them at start):");
        for dir in legacy {
            println!("  {} ({} save(s))", dir.display(), cards::save_count(&dir));
        }
    }
    Ok(())
}

fn resolve_slot_index(
    container: &MemoryCardContainer,
    user_slot_1based: Option<usize>,
) -> Result<usize> {
    if let Some(s) = user_slot_1based {
        if !(1..=15).contains(&s) {
            bail!("Slot number must be between 1 and 15 (got {})", s);
        }
        let idx = s - 1;
        if container.slots[idx].is_none() {
            bail!(
                "Slot {} ({}) is empty in {}",
                s,
                (b'A' + idx as u8) as char,
                container.path.display()
            );
        }
        Ok(idx)
    } else {
        container.first_active_slot_index().ok_or_else(|| {
            anyhow::anyhow!(
                "No active Parasite Eve save slots found in {}",
                container.path.display()
            )
        })
    }
}
