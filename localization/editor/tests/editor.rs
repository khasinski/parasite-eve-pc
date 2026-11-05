//! The exchange round trip and the interface's project flow, without a window.
use eframe::egui;
use indexmap::IndexMap;
use pe_translation_editor::app::{install_fonts, smoke, Editor};
use pe_translation_editor::cjk::FontSource;
use pe_translation_editor::model::{load_source, sample, Project, KEYS};
use pe_translation_editor::roundtrip::run;

#[test]
fn round_trip_writes_the_demo_files() {
    let folder = tempfile::tempdir().unwrap();
    run(folder.path(), None, &FontSource::Demo).unwrap();
    for name in ["opera-demo.pepack", "opera-demo.pefix", "opera-demo.pelang"] {
        assert!(folder.path().join(name).metadata().unwrap().len() > 0, "{name}");
    }
}

#[test]
fn smoke_passes_the_checks_the_packager_requires() {
    let folder = tempfile::tempdir().unwrap();
    let proof = folder.path().join("proof.txt");
    smoke(&proof).unwrap();
    let text = std::fs::read_to_string(proof).unwrap();
    assert!(text.starts_with("2 compiled box(es). Layout and control checks passed."), "{text}");
}

#[test]
fn demo_opens_with_unsaved_rows_and_both_boxes() {
    let mut editor = Editor::new();
    editor.demo();
    assert!(editor.dirty());
    assert_eq!(editor.scope, "opera");
    assert_eq!(editor.key, KEYS[0]);
    assert_eq!(editor.translation, sample()[KEYS[0]]);
    assert_eq!(editor.progress, "2 of 8 translated · 0 to review");
    assert_eq!(editor.line_items[0], "Line 01   Translated");
    assert_eq!(editor.line_items[1], "Line 02   Untranslated");
}

#[test]
fn line_filters_and_review_status_follow_the_project() {
    let mut editor = Editor::new();
    editor.demo();
    editor.project.apply_ai_translations(&IndexMap::from([(KEYS[3].to_owned(), "Hola".to_owned())]));
    editor.line_filter = "Needs review".to_owned();
    editor.change_filter();
    assert_eq!(editor.key, KEYS[3]);
    assert_eq!(editor.visible_keys, [KEYS[3]]);
    assert_eq!(editor.line_items, ["Line 04   Needs review"]);
    editor.mark_reviewed();
    assert!(editor.project.needs_review.is_empty());
    assert_eq!(editor.filter_status, "No lines match this filter.");
    assert_eq!(editor.status, "Line marked reviewed. Save the project to keep its review status.");
    editor.line_filter = "Untranslated".to_owned();
    editor.change_filter();
    assert_eq!(editor.visible_keys.len(), 6);
}

#[test]
fn editing_reaches_the_project_and_clearing_a_line_clears_its_review() {
    let mut editor = Editor::new();
    editor.demo();
    editor.project.needs_review.insert(KEYS[0].to_owned());
    editor.translation = "One\r\nTwo".to_owned();
    editor.edited();
    assert_eq!(editor.project.rows[KEYS[0]], "One{br}Two");
    assert!(editor.project.needs_review.contains(KEYS[0]));
    editor.translation = "   ".to_owned();
    editor.edited();
    assert!(editor.project.needs_review.is_empty());
    assert_eq!(editor.line_items[0], "Line 01   Untranslated");
}

#[test]
fn preview_reports_invalid_text_without_the_key_prefix() {
    let mut editor = Editor::new();
    editor.demo();
    editor.translation = "Bad {gZZ}".to_owned();
    editor.edited();
    editor.preview();
    assert_eq!(editor.preview_label, "");
    assert!(editor.status.starts_with("invalid text/tag: bad tag at 4"), "{}", editor.status);
}

#[test]
fn opening_a_saved_project_shows_its_first_translated_line() {
    let source = load_source();
    let second = source.iter().find(|(key, row)| !key.starts_with("dlg/M001/") && row.structure.as_ref().is_some_and(Vec::is_empty)).unwrap().0;
    let mut project = Project::new(source);
    project.rows.insert(second.clone(), "Test".to_owned());
    let reopened = Project::import_project(source, &project.export_project().unwrap()).unwrap();
    let mut editor = Editor::new();
    editor.replace_project(reopened, None);
    assert_eq!(&editor.key, second);
    assert_ne!(editor.scope, "opera");
    assert!(!editor.dirty());
    assert_eq!(editor.project_label, "Opera pilot · zh-TW");
}

#[test]
fn interface_lays_out_every_screen() {
    let ctx = egui::Context::default();
    install_fonts(&ctx);
    let mut editor = Editor::new();
    let _ = ctx.run(egui::RawInput::default(), |ctx| editor.ui(ctx));
    editor.demo();
    for _ in 0..3 {
        let _ = ctx.run(egui::RawInput::default(), |ctx| editor.ui(ctx));
    }
}
