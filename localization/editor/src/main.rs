#![cfg_attr(windows, windows_subsystem = "windows")]
//! The standalone translation editor. `--smoke PROOF` runs the demo without a window and writes
//! its status to PROOF.
use std::path::Path;
use std::process::ExitCode;

use eframe::egui;
use pe_translation_editor::app::{install_fonts, smoke, Editor, TITLE};

fn main() -> ExitCode {
    let args: Vec<_> = std::env::args_os().collect();
    if args.len() == 3 && args[1] == "--smoke" {
        return match smoke(Path::new(&args[2])) {
            Ok(()) => ExitCode::SUCCESS,
            Err(error) => {
                eprintln!("{error}");
                ExitCode::FAILURE
            }
        };
    }
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default().with_title(TITLE).with_inner_size([980.0, 735.0]).with_min_inner_size([820.0, 650.0]),
        ..Default::default()
    };
    let result = eframe::run_native(
        TITLE,
        options,
        Box::new(|context| {
            context.egui_ctx.set_visuals(egui::Visuals::light());
            install_fonts(&context.egui_ctx);
            Ok(Box::new(Editor::new()))
        }),
    );
    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            rfd::MessageDialog::new()
                .set_level(rfd::MessageLevel::Error)
                .set_title(TITLE)
                .set_description(format!("The editor window could not start: {error}"))
                .show();
            ExitCode::FAILURE
        }
    }
}
