//! Regenerate catalog.json from the pinned source manifest and both US discs.
//!
//!     cargo run --release --bin build_catalog -- "USA Disc 1.bin" "USA Disc 2.bin"
use std::path::{Path, PathBuf};
use std::process::ExitCode;

use pe_translation_editor::build_catalog::generate;
use pe_translation_editor::pycompat::{dumps, JSON_COMPACT_ASCII};

fn main() -> ExitCode {
    let args: Vec<PathBuf> = std::env::args_os().skip(1).map(PathBuf::from).collect();
    let [first, second] = args.as_slice() else {
        eprintln!("usage: build_catalog DISC1 DISC2");
        return ExitCode::from(2);
    };
    let editor = Path::new(env!("CARGO_MANIFEST_DIR"));
    let root = editor.join("../..");
    let catalog = match generate(&root, &[first.clone(), second.clone()]) {
        Ok(catalog) => catalog,
        Err(error) => {
            eprintln!("{error}");
            return ExitCode::FAILURE;
        }
    };
    let text = dumps(&catalog, JSON_COMPACT_ASCII) + if cfg!(windows) { "\r\n" } else { "\n" };
    if let Err(error) = std::fs::write(editor.join("catalog.json"), text) {
        eprintln!("{error}");
        return ExitCode::FAILURE;
    }
    let (entries, scenes) = (catalog["entries"].as_object().map_or(0, |m| m.len()), catalog["scenes"].as_object().map_or(0, |m| m.len()));
    println!("{entries} dialogue entries, {scenes} scene and bank views");
    ExitCode::SUCCESS
}
