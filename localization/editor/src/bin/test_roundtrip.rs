//! Verify extra boxes, bundled glyphs, and both editor exchange formats, optionally on a real disc.
//!
//!     cargo run --release --bin test_roundtrip -- OUTPUT [--disc "USA Disc 1.cue"] [--font FONT]
use std::path::PathBuf;
use std::process::ExitCode;

use pe_translation_editor::cjk::FontSource;
use pe_translation_editor::roundtrip::run;

fn main() -> ExitCode {
    let mut args = std::env::args_os().skip(1);
    let (mut output, mut disc, mut font) = (None, None, FontSource::Demo);
    while let Some(arg) = args.next() {
        match arg.to_str() {
            Some("--disc") => disc = args.next().map(PathBuf::from),
            Some("--font") => font = args.next().map_or(FontSource::Demo, |path| FontSource::File(PathBuf::from(path))),
            _ if output.is_none() => output = Some(PathBuf::from(arg)),
            _ => output = None,
        }
    }
    let Some(output) = output else {
        eprintln!("usage: test_roundtrip OUTPUT [--disc DISC] [--font FONT]");
        return ExitCode::from(2);
    };
    match run(&output, disc.as_deref(), &font) {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("{error}");
            ExitCode::FAILURE
        }
    }
}
