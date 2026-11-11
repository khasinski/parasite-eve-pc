//! Desktop editor for the USA dialogue catalog.
use std::collections::{BTreeSet, HashMap, HashSet};
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::time::{Duration, Instant};

use eframe::egui::{
    self, text::CCursor, text::CCursorRange, Button, Color32, ComboBox, FontData, FontDefinitions, FontFamily, FontId, Id, Key,
    KeyboardShortcut, Layout, Modal, Modifiers, RichText, ScrollArea, TextEdit, Ui,
};
use indexmap::IndexMap;
use rfd::{FileDialog, MessageButtons, MessageDialog, MessageDialogResult, MessageLevel};

use crate::catalog::{self, scope_keys, Scene};
use crate::cjk::{self, Atlas, FontSource};
use crate::dialogue_csv::{ai_prompt, export_dialogue, import_translation, read_plain_csv};
use crate::dialogue_search::{build_index, casefold, dialogue_matches, preferred_scenes, SearchIndex};
use crate::error::{Error, Result};
use crate::model::{is_language_code, load_source, read_original, read_small, sample, Project, BOX, KEYS};
use crate::pycompat::{strip, thousands};
use crate::{gametext, layout, validation};

pub const TITLE: &str = "Parasite Eve Translation Editor";
const LINE_FILTERS: [&str; 3] = ["All lines", "Untranslated", "Needs review"];
const PREVIEW_BACKGROUND: Color32 = Color32::from_rgb(0x18, 0x25, 0x39);
const ORIGINAL_BACKGROUND: Color32 = Color32::from_rgb(0xec, 0xf1, 0xf6);
const GLYPH_PALETTE: [Color32; 3] = [PREVIEW_BACKGROUND, Color32::WHITE, Color32::from_rgb(72, 86, 105)];

#[derive(PartialEq, Clone, Copy)]
enum Screen {
    Welcome,
    Workspace,
}

enum PreviewItem {
    Glyph { x: i32, y: i32, pixels: Vec<u8> },
    Text { x: i32, y: i32, text: String },
}

struct SettingsDialog {
    starting: bool,
    title: String,
    language: String,
    font: Option<FontSource>,
    font_label: String,
}

struct FindDialog {
    query: String,
    index: SearchIndex,
    matches: Vec<(String, String)>,
    selected: Option<usize>,
    count: String,
    focus_field: bool,
}

enum Dialog {
    Share,
    Settings(SettingsDialog),
    FindNeedsDisc,
    Find(FindDialog),
    Ai { copied: bool },
}

/// Work that may open a file dialog or replace the project, run after the frame's widgets.
enum Action {
    Demo,
    OpenPack,
    New,
    ImportCsv,
    Share,
    ExportPack { save_as: bool },
    AiExchange,
    Settings,
    ImportFixes,
    Disc,
    LoadAndFind,
    FindDialogue,
    ExportFixes,
    ExportGame,
    ExportCopy,
    ExportDialogue(String),
    ImportTranslatedCsv,
}

pub struct Editor {
    pub project: Project,
    saved_rows: IndexMap<String, String>,
    saved_review: BTreeSet<String>,
    original: IndexMap<String, String>,
    pub key: String,
    scenes: &'static IndexMap<String, Scene>,
    line_scenes: HashMap<String, String>,
    pub scope: String,
    pub scene_keys: Vec<String>,
    scene_labels: HashMap<String, String>,
    scene_captions: IndexMap<String, String>,
    scene_values: Vec<String>,
    progress_snapshot: Option<(HashSet<String>, BTreeSet<String>)>,
    pub visible_keys: Vec<String>,
    pub line_items: Vec<String>,
    pub line_filter: String,
    pub filter_status: String,
    scene_choice: String,
    pub scene_label: String,
    scene_query: String,
    project_path: Option<PathBuf>,
    preview_due: Option<Instant>,
    preview_page: usize,
    preview_pages: Vec<Vec<Vec<u8>>>,
    preview_atlas: Option<Arc<Atlas>>,
    preview_warnings: Vec<String>,
    preview_error: String,
    preview_items: Vec<PreviewItem>,
    preview_placeholder: Option<&'static str>,
    pub language: String,
    pub title: String,
    pub status: String,
    pub project_label: String,
    pub progress: String,
    pub preview_label: String,
    original_label: String,
    original_text: String,
    pub translation: String,
    translation_generation: u64,
    focus_translation: bool,
    scroll_to_selected_line: bool,
    review_enabled: bool,
    previous_enabled: bool,
    next_enabled: bool,
    saved_identity: (String, String, Option<FontSource>),
    screen: Screen,
    dialog: Option<Dialog>,
    allow_close: bool,
    action: Option<Action>,
}

fn show_error(message: &str) {
    MessageDialog::new().set_level(MessageLevel::Error).set_title(TITLE).set_description(message).set_buttons(MessageButtons::Ok).show();
}

fn ask_yes_no(message: &str) -> bool {
    let answer = MessageDialog::new().set_title(TITLE).set_description(message).set_buttons(MessageButtons::YesNo).show();
    answer == MessageDialogResult::Yes
}

fn heading(text: impl Into<String>) -> RichText {
    RichText::new(text).size(15.0).strong()
}

/// A labelled action with its explanation underneath; returns whether it was chosen.
fn option(ui: &mut Ui, label: &str, description: &str, primary: bool) -> bool {
    let text = if primary { RichText::new(label).strong() } else { RichText::new(label) };
    let clicked = ui.add(Button::new(text).min_size(egui::vec2(0.0, if primary { 32.0 } else { 24.0 }))).clicked();
    ui.add(egui::Label::new(description).wrap());
    ui.add_space(12.0);
    clicked
}

impl Editor {
    pub fn new() -> Self {
        let scenes = &catalog::load().scenes;
        let scope = "opera".to_owned();
        let project = Project::new(load_source());
        let scene_keys = scope_keys(&scope).expect("the catalog has the opera scene");
        let saved_identity = (project.language.clone(), project.title.clone(), project.font.clone());
        let mut editor = Self {
            language: project.language.clone(),
            title: project.title.clone(),
            project,
            saved_rows: IndexMap::new(),
            saved_review: BTreeSet::new(),
            original: IndexMap::new(),
            key: KEYS[0].to_owned(),
            scenes,
            line_scenes: preferred_scenes(scenes),
            scene_label: scenes[&scope].label.clone(),
            scope,
            scene_keys,
            scene_labels: HashMap::new(),
            scene_captions: IndexMap::new(),
            scene_values: Vec::new(),
            progress_snapshot: None,
            visible_keys: Vec::new(),
            line_items: Vec::new(),
            line_filter: LINE_FILTERS[0].to_owned(),
            filter_status: String::new(),
            scene_choice: String::new(),
            scene_query: String::new(),
            project_path: None,
            preview_due: None,
            preview_page: 0,
            preview_pages: Vec::new(),
            preview_atlas: None,
            preview_warnings: Vec::new(),
            preview_error: String::new(),
            preview_items: Vec::new(),
            preview_placeholder: None,
            status: String::new(),
            project_label: String::new(),
            progress: String::new(),
            preview_label: String::new(),
            original_label: String::new(),
            original_text: String::new(),
            translation: String::new(),
            translation_generation: 0,
            focus_translation: false,
            scroll_to_selected_line: false,
            review_enabled: false,
            previous_enabled: false,
            next_enabled: false,
            saved_identity,
            screen: Screen::Welcome,
            dialog: None,
            allow_close: false,
            action: None,
        };
        editor.refresh();
        editor
    }

    fn translation_id(&self) -> Id {
        Id::new(("translation", self.translation_generation))
    }

    fn font_caption(&self) -> String {
        match &self.project.font {
            Some(FontSource::Demo) => "Bundled opera demo font".to_owned(),
            Some(FontSource::File(path)) => path.file_name().map_or_else(|| path.display().to_string(), |n| n.to_string_lossy().into_owned()),
            None if self.project.atlas.is_some() => "Embedded glyphs".to_owned(),
            None => "Game font".to_owned(),
        }
    }

    fn identity(&self) -> (String, String, Option<FontSource>) {
        (self.project.language.clone(), self.project.title.clone(), self.project.font.clone())
    }

    fn run(&mut self, action: impl FnOnce(&mut Self) -> Result<()>) {
        if let Err(error) = action(self) {
            show_error(&error.message);
        }
    }

    pub fn sync(&mut self) {
        let value = self.translation.replace('\n', "{br}");
        if strip(&value).is_empty() {
            self.project.needs_review.remove(&self.key);
        }
        self.project.rows.insert(self.key.clone(), value);
        self.project.language = strip(&self.language).to_owned();
        self.project.title = strip(&self.title).to_owned();
    }

    fn filter_scenes(&mut self) {
        let query = casefold(strip(&self.scene_query));
        self.scene_values = self
            .scene_captions
            .iter()
            .filter(|(scope, _)| casefold(&self.scenes[scope.as_str()].label).contains(&query))
            .map(|(_, caption)| caption.clone())
            .collect();
    }

    fn first_scene(&mut self) {
        if let Some(first) = self.scene_values.first().cloned() {
            if !strip(&self.scene_query).is_empty() {
                self.sync();
                let scope = self.scene_labels[&first].clone();
                self.set_scene(&scope, None);
            }
        }
    }

    fn open_find_dialogue(&mut self) {
        if self.original.is_empty() {
            self.dialog = Some(Dialog::FindNeedsDisc);
            return;
        }
        let mut dialog = FindDialog {
            query: String::new(),
            index: build_index(&self.original),
            matches: Vec::new(),
            selected: None,
            count: String::new(),
            focus_field: true,
        };
        Self::update_find(&mut dialog);
        self.dialog = Some(Dialog::Find(dialog));
    }

    fn update_find(dialog: &mut FindDialog) {
        dialog.matches = dialogue_matches(&dialog.index, &dialog.query);
        dialog.selected = (!dialog.matches.is_empty()).then_some(0);
        dialog.count = if strip(&dialog.query).is_empty() {
            "Enter a word or phrase to begin.".to_owned()
        } else {
            format!("{} matching lines", thousands(dialog.matches.len()))
        };
    }

    fn keys_for_filter(&self) -> Vec<String> {
        let keys = self.scene_keys.iter();
        match self.line_filter.as_str() {
            "Needs review" => keys.filter(|key| self.project.needs_review.contains(*key)).cloned().collect(),
            "Untranslated" => keys
                .filter(|key| {
                    self.project.source[key.as_str()].structure.is_some()
                        && strip(self.project.rows.get(*key).map_or("", String::as_str)).is_empty()
                })
                .cloned()
                .collect(),
            _ => self.scene_keys.clone(),
        }
    }

    pub fn set_scene(&mut self, scope: &str, key: Option<&str>) {
        self.scope = scope.to_owned();
        self.scene_keys = scope_keys(scope).expect("a scene from the catalog");
        self.scene_label = self.scenes[scope].label.clone();
        let visible = self.keys_for_filter();
        let wanted = key.filter(|key| self.scene_keys.iter().any(|k| k == key));
        if wanted.is_some_and(|key| !visible.iter().any(|k| k == key)) {
            self.line_filter = LINE_FILTERS[0].to_owned();
        }
        self.key = match wanted {
            Some(key) => key.to_owned(),
            None => visible.first().or(self.scene_keys.first()).cloned().expect("every scene has lines"),
        };
        self.refresh();
        self.scroll_to_selected_line = true;
    }

    fn focus_key(&mut self, key: &str) {
        let scope = if self.scene_keys.iter().any(|k| k == key) { self.scope.clone() } else { self.line_scenes[key].clone() };
        self.set_scene(&scope, Some(key));
    }

    pub fn change_filter(&mut self) {
        self.sync();
        let keys = self.keys_for_filter();
        if !keys.is_empty() && !keys.contains(&self.key) {
            self.key = keys[0].clone();
            self.refresh();
        } else {
            self.update_summary();
        }
    }

    pub fn mark_reviewed(&mut self) {
        self.sync();
        if !self.project.needs_review.remove(&self.key) {
            return;
        }
        self.change_filter();
        self.status = "Line marked reviewed. Save the project to keep its review status.".to_owned();
    }

    fn update_scene_counts(&mut self, translated: &HashSet<String>, review: &BTreeSet<String>) {
        let unchanged = self.progress_snapshot.as_ref().is_some_and(|(t, r)| t == translated && r == review);
        if !unchanged {
            self.progress_snapshot = Some((translated.clone(), review.clone()));
            self.scene_captions.clear();
            for (scope, scene) in self.scenes {
                let total = scene.keys.iter().filter(|key| self.project.source[key.as_str()].structure.is_some()).count();
                let done = scene.keys.iter().filter(|key| translated.contains(*key)).count();
                let pending = scene.keys.iter().filter(|key| review.contains(*key)).count();
                let suffix = if pending > 0 { format!(" · {pending} review") } else { String::new() };
                self.scene_captions.insert(scope.clone(), format!("{done}/{total}{suffix} | {}", scene.label));
            }
            self.scene_labels = self.scene_captions.iter().map(|(scope, caption)| (caption.clone(), scope.clone())).collect();
            self.filter_scenes();
        }
        self.scene_choice = self.scene_captions[&self.scope].clone();
    }

    pub fn update_summary(&mut self) {
        let translated: HashSet<String> = self
            .project
            .rows
            .iter()
            .filter(|(key, value)| !strip(value).is_empty() && self.project.source[key.as_str()].structure.is_some())
            .map(|(key, _)| key.clone())
            .collect();
        let review = self.project.needs_review.clone();
        self.update_scene_counts(&translated, &review);
        let total = self.scene_keys.iter().filter(|key| self.project.source[key.as_str()].structure.is_some()).count();
        let done = self.scene_keys.iter().filter(|key| translated.contains(*key)).count();
        let pending = self.scene_keys.iter().filter(|key| review.contains(*key)).count();
        self.progress = format!("{done} of {total} translated · {pending} to review");
        let changed = if self.dirty() { " · Unsaved changes" } else { "" };
        self.project_label = format!("{} · {}{changed}", self.project.title, self.project.language);
        let visible = self.keys_for_filter();
        let order: HashMap<&String, usize> = self.scene_keys.iter().enumerate().map(|(index, key)| (key, index + 1)).collect();
        self.line_items = visible
            .iter()
            .map(|key| {
                let status = if self.project.source[key.as_str()].structure.is_none() {
                    "Original only"
                } else if review.contains(key) {
                    "Needs review"
                } else if translated.contains(key) {
                    "Translated"
                } else {
                    "Untranslated"
                };
                format!("Line {:02}   {status}", order[key])
            })
            .collect();
        self.filter_status = if visible.is_empty() {
            "No lines match this filter.".to_owned()
        } else if !visible.contains(&self.key) {
            "The current line is outside this filter.".to_owned()
        } else {
            String::new()
        };
        self.visible_keys = visible;
        self.review_enabled = review.contains(&self.key);
    }

    pub fn refresh(&mut self) {
        self.update_summary();
        let line = self.scene_keys.iter().position(|key| *key == self.key).map_or(0, |at| at + 1);
        self.original_label = format!("Original · Line {line:02}");
        self.original_text = self.original.get(&self.key).map_or_else(
            || "Load a USA disc to display the original dialogue here.".to_owned(),
            |text| text.replace("{br}", "\n").replace("{wait}", "\n[Next dialogue box]\n"),
        );
        self.translation = self.project.rows.get(&self.key).map_or("", String::as_str).replace("{br}", "\n");
        // A new widget identity also clears the undo history of the previous line.
        self.translation_generation += 1;
        self.schedule_preview(true);
    }

    pub fn select_line(&mut self, index: usize) {
        if self.visible_keys[index] != self.key {
            self.sync();
            self.key = self.visible_keys[index].clone();
            self.refresh();
        }
    }

    /// The translation box changed.
    pub fn edited(&mut self) {
        self.translation = self.translation.replace("\r\n", "\n").replace('\r', "\n");
        self.sync();
        self.update_summary();
        self.schedule_preview(true);
    }

    fn insert_box(&mut self, ctx: &egui::Context) {
        let id = self.translation_id();
        let mut state = TextEdit::load_state(ctx, id).unwrap_or_default();
        let count = self.translation.chars().count();
        let at = state.cursor.char_range().map_or(count, |range| range.primary.index).min(count);
        let byte = self.translation.char_indices().nth(at).map_or(self.translation.len(), |(byte, _)| byte);
        self.translation.insert_str(byte, BOX);
        state.cursor.set_char_range(Some(CCursorRange::one(CCursor::new(at + BOX.chars().count()))));
        state.store(ctx, id);
        self.focus_translation = true;
        self.edited();
    }

    fn schedule_preview(&mut self, reset: bool) {
        if reset {
            self.preview_page = 0;
        }
        self.preview_due = Some(Instant::now() + Duration::from_millis(300));
    }

    fn disc(&mut self) -> Result<()> {
        let path = FileDialog::new().add_filter("USA disc image", &["bin", "cue", "iso"]).add_filter("All files", &["*"]).pick_file();
        if let Some(path) = path {
            self.sync();
            self.original.extend(read_original(&path)?);
            self.refresh();
            self.status = "Original dialogue loaded from your disc.".to_owned();
        }
        Ok(())
    }

    /// Show `project`, starting from the opera scene or its first translated line.
    pub fn replace_project(&mut self, project: Project, path: Option<PathBuf>) {
        self.project = project;
        self.project_path = path;
        self.saved_rows = self.project.baseline.clone();
        self.saved_review = self.project.needs_review.clone();
        self.line_filter = LINE_FILTERS[0].to_owned();
        self.saved_identity = self.identity();
        self.language = self.project.language.clone();
        self.title = self.project.title.clone();
        self.screen = Screen::Workspace;
        self.set_scene("opera", None);
        let scene_empty = !self.scene_keys.iter().any(|key| self.project.rows.get(key).is_some_and(|value| !value.is_empty()));
        if !self.project.rows.is_empty() && scene_empty {
            let first = self.project.rows.iter().find(|(_, value)| !value.is_empty()).map(|(key, _)| key.clone());
            if let Some(first) = first {
                self.focus_key(&first);
            }
        }
    }

    pub fn dirty(&self) -> bool {
        let empty = String::new();
        self.identity() != self.saved_identity
            || self.project.needs_review != self.saved_review
            || self.project.rows.keys().chain(self.saved_rows.keys()).any(|key| {
                self.project.rows.get(key).unwrap_or(&empty) != self.saved_rows.get(key).unwrap_or(&empty)
            })
    }

    fn confirm_replace(&mut self, closing: bool) -> bool {
        self.sync();
        if !self.dirty() {
            return true;
        }
        let doing = if closing { "closing" } else { "switching projects" };
        let answer = MessageDialog::new()
            .set_title(TITLE)
            .set_description(format!("Save your changes before {doing}?"))
            .set_buttons(MessageButtons::YesNoCancel)
            .show();
        match answer {
            MessageDialogResult::Yes => match self.export_pack(false) {
                Ok(saved) => saved,
                Err(error) => {
                    show_error(&error.message);
                    false
                }
            },
            MessageDialogResult::No => true,
            _ => false,
        }
    }

    fn open_settings(&mut self, starting: bool) {
        self.sync();
        let dialog = SettingsDialog {
            starting,
            title: if starting { "My translation".to_owned() } else { self.title.clone() },
            language: if starting { "fr".to_owned() } else { self.language.clone() },
            font: if starting { None } else { self.project.font.clone() },
            font_label: if starting { "Game font".to_owned() } else { self.font_caption() },
        };
        self.dialog = Some(Dialog::Settings(dialog));
    }

    /// Apply the settings dialog; false keeps it open.
    fn apply_settings(&mut self, dialog: &SettingsDialog) -> bool {
        let (code, name) = (strip(&dialog.language).to_owned(), strip(&dialog.title).to_owned());
        if !is_language_code(&code) || code.len() > 15 {
            show_error("Enter a language code such as fr or es-419.");
            return false;
        }
        if name.is_empty() || name.len() > 31 {
            show_error("Enter a shorter project name.");
            return false;
        }
        if dialog.starting {
            let mut project = Project::new(load_source());
            (project.language, project.title, project.font) = (code, name, dialog.font.clone());
            self.replace_project(project, None);
            self.status = "Load your disc to see the English, then pick a line and type your translation.".to_owned();
        } else {
            self.language = code.clone();
            self.title = name.clone();
            (self.project.language, self.project.title, self.project.font) = (code, name, dialog.font.clone());
            self.update_summary();
            self.schedule_preview(true);
        }
        true
    }

    pub fn demo(&mut self) {
        if self.confirm_replace(false) {
            let mut project = Project::new(load_source());
            project.rows = sample();
            project.font = Some(FontSource::Demo);
            self.replace_project(project, None);
        }
    }

    fn open_pack(&mut self) -> Result<()> {
        if !self.confirm_replace(false) {
            return Ok(());
        }
        if let Some(path) = FileDialog::new().add_filter("Editable language pack", &["pepack"]).pick_file() {
            let project = Project::import_project(load_source(), &read_small(&path)?)?;
            self.replace_project(project, Some(path));
        }
        Ok(())
    }

    fn export_dialogue_csv(&mut self, scope: &str) -> Result<()> {
        let keys = scope_keys(scope)?;
        if keys.iter().any(|key| !self.original.contains_key(key)) {
            self.disc()?;
        }
        if keys.iter().any(|key| !self.original.contains_key(key)) {
            if !self.original.is_empty() {
                self.status = "Some lines need another USA disc. Load it before exporting this selection.".to_owned();
            }
            return Ok(());
        }
        let name = format!("{scope}-{}.csv", self.project.language);
        let scope_owned = scope.to_owned();
        let saved = self.save(
            ".csv",
            "Dialogue for translation",
            move |editor| export_dialogue(&editor.original, editor.project.source, &scope_owned),
            None,
            Some(name),
        )?;
        if saved.is_some() {
            self.status = format!("Exported {} lines. Open Translate with AI again to copy the prompt.", thousands(keys.len()));
        }
        Ok(())
    }

    fn import_translated_csv(&mut self) -> Result<()> {
        let Some(path) = FileDialog::new().add_filter("Translated dialogue CSV", &["csv"]).pick_file() else {
            return Ok(());
        };
        self.sync();
        let rows = import_translation(&read_small(&path)?, self.project.source)?;
        let changed: IndexMap<String, String> =
            rows.into_iter().filter(|(key, value)| self.project.rows.get(key) != Some(value)).collect();
        if changed.is_empty() {
            self.status = "The CSV has no new translations to apply.".to_owned();
            return Ok(());
        }
        let overwrites = changed.keys().filter(|key| self.project.rows.get(*key).is_some_and(|value| !value.is_empty())).count();
        if overwrites > 0 && !ask_yes_no(&format!("Apply {} translations and replace {overwrites} existing edits?", changed.len())) {
            return Ok(());
        }
        self.project.apply_ai_translations(&changed);
        self.line_filter = "Needs review".to_owned();
        let first = changed.keys().next().expect("changed is not empty").clone();
        self.focus_key(&first);
        self.status = format!("Applied {} translations. Check each line, then choose Mark reviewed.", changed.len());
        Ok(())
    }

    fn import_csv(&mut self) -> Result<()> {
        if !self.confirm_replace(false) {
            return Ok(());
        }
        let Some(path) = FileDialog::new().add_filter("Translation CSV", &["csv"]).pick_file() else {
            return Ok(());
        };
        let (rows, ignored) = read_plain_csv(&read_small(&path)?, self.project.source)?;
        let mut project = Project::new(load_source());
        project.language = self.project.language.clone();
        project.title = self.project.title.clone();
        project.font = self.project.font.clone();
        project.baseline = rows.clone();
        let count = rows.len();
        project.rows = rows;
        self.replace_project(project, None);
        self.status = format!("Opened {count} dialogue entries. Skipped {ignored} non-dialogue or unknown rows.");
        Ok(())
    }

    fn import_fixes(&mut self) -> Result<()> {
        if let Some(path) = FileDialog::new().add_filter("Translation fixes", &["pefix"]).pick_file() {
            self.sync();
            let count = self.project.import_fixes(&read_small(&path)?)?;
            self.refresh();
            self.status = format!("Imported {count} line(s). Preview the edits before exporting.");
        }
        Ok(())
    }

    fn save(
        &mut self,
        suffix: &str,
        label: &str,
        produce: impl FnOnce(&Self) -> Result<Vec<u8>>,
        destination: Option<PathBuf>,
        initial_name: Option<String>,
    ) -> Result<Option<PathBuf>> {
        self.sync();
        let data = produce(self)?;
        let destination = match destination {
            Some(destination) => destination,
            None => {
                let name = initial_name.unwrap_or_else(|| format!("{}{suffix}", self.project.language));
                let extension = suffix.trim_start_matches('.');
                let Some(mut path) = FileDialog::new().add_filter(label, &[extension]).set_file_name(name).save_file() else {
                    return Ok(None);
                };
                if path.extension().is_none() {
                    path.set_extension(extension);
                }
                path
            }
        };
        write_replacing(&destination, &data)?;
        let shown = destination.file_name().map_or_else(|| destination.display().to_string(), |n| n.to_string_lossy().into_owned());
        self.status = format!("Saved {shown}.");
        Ok(Some(destination))
    }

    fn export_pack(&mut self, save_as: bool) -> Result<bool> {
        let destination = if save_as { None } else { self.project_path.clone() };
        let path = self.save(".pepack", "Editable translation project", |editor| editor.project.export_project(), destination, None)?;
        let saved = path.is_some();
        if let Some(path) = path {
            self.project_path = Some(path);
            self.saved_rows = self.project.rows.clone();
            self.saved_review = self.project.needs_review.clone();
            self.saved_identity = self.identity();
            self.update_summary();
        }
        Ok(saved)
    }

    /// Compile the selected line alone: it previews even if another line still has errors.
    pub fn preview(&mut self) {
        self.preview_due = None;
        self.sync();
        self.preview_pages.clear();
        self.preview_atlas = None;
        self.preview_warnings.clear();
        self.preview_error.clear();
        let text = self.project.rows.get(&self.key).cloned().unwrap_or_default();
        if !text.is_empty() {
            match self.compile_preview(&text) {
                Ok(()) => {}
                Err(error) => {
                    let prefix = format!("{}: ", self.key);
                    self.preview_pages.clear();
                    self.preview_error = error.message.strip_prefix(&prefix).unwrap_or(&error.message).to_owned();
                }
            }
        }
        self.draw_preview();
    }

    fn compile_preview(&mut self, text: &str) -> Result<()> {
        let mut draft = Project::new(self.project.source);
        draft.rows.insert(self.key.clone(), text.to_owned());
        draft.font = self.project.font.clone();
        draft.atlas = self.project.atlas.clone();
        let (_, previews, atlas) = draft.compile()?;
        let (raw, warnings) = previews.get(&self.key).ok_or_else(|| Error::value(self.key.clone()))?;
        self.preview_atlas = atlas;
        self.preview_warnings = warnings.clone();
        self.preview_pages = vec![Vec::new()];
        for command in validation::commands(raw)? {
            if command == [0xF8] {
                self.preview_pages.push(Vec::new());
            } else {
                self.preview_pages.last_mut().expect("at least one page").push(command.to_vec());
            }
        }
        Ok(())
    }

    pub fn step_box(&mut self, direction: i32) {
        let next = self.preview_page as i32 + direction;
        if next >= 0 && (next as usize) < self.preview_pages.len() {
            self.preview_page = next as usize;
            self.draw_preview();
        }
    }

    pub fn preview_glyph_count(&self) -> usize {
        self.preview_items.iter().filter(|item| matches!(item, PreviewItem::Glyph { .. })).count()
    }

    fn draw_preview(&mut self) {
        self.preview_items.clear();
        self.preview_placeholder = None;
        let count = self.preview_pages.len();
        self.preview_page = self.preview_page.min(count.saturating_sub(1));
        self.previous_enabled = self.preview_page > 0;
        self.next_enabled = self.preview_page + 1 < count;
        self.preview_label = if count > 0 { format!("Box {} of {count}", self.preview_page + 1) } else { String::new() };
        if count == 0 {
            self.preview_placeholder = Some(if self.preview_error.is_empty() {
                "This line uses the original dialogue."
            } else {
                "Preview unavailable until this line is fixed."
            });
            self.status = self.preview_error.clone();
            return;
        }
        let (mut x, mut y) = (14, 14);
        for command in &self.preview_pages[self.preview_page] {
            let opcode = command[0];
            if opcode == 0xF7 {
                (x, y) = (14, y + 19);
                continue;
            }
            if opcode == 0xF1 {
                let index = cjk::index(command);
                let records = self.preview_atlas.as_ref().map_or(&[][..], |atlas| &atlas.records);
                let glyph = &records[index * cjk::RECORD + 4..(index + 1) * cjk::RECORD];
                let pixels = glyph.iter().flat_map(|byte| [byte & 15, byte >> 4]).collect();
                self.preview_items.push(PreviewItem::Glyph { x, y, pixels });
            } else {
                let text = if opcode == 0xFA {
                    "Aya".to_owned()
                } else if opcode < 0xF1 {
                    gametext::decode(command)
                } else {
                    String::new()
                };
                self.preview_items.push(PreviewItem::Text { x, y, text });
            }
            x += layout::advance(command, layout::name_width()) as i32;
        }
        let notes = if self.preview_warnings.is_empty() { "Layout and control checks passed.".to_owned() } else { self.preview_warnings.join(" • ") };
        self.status = format!("{count} compiled box(es). {notes}");
    }

    fn perform(&mut self, action: Action) {
        match action {
            Action::Demo => self.demo(),
            Action::OpenPack => self.run(Self::open_pack),
            Action::New => {
                if self.confirm_replace(false) {
                    self.open_settings(true);
                }
            }
            Action::ImportCsv => self.run(Self::import_csv),
            Action::Share => self.dialog = Some(Dialog::Share),
            Action::ExportPack { save_as } => self.run(|editor| editor.export_pack(save_as).map(|_| ())),
            Action::AiExchange => {
                self.sync();
                self.dialog = Some(Dialog::Ai { copied: false });
            }
            Action::Settings => self.open_settings(false),
            Action::ImportFixes => self.run(Self::import_fixes),
            Action::Disc => self.run(Self::disc),
            Action::LoadAndFind => {
                self.run(Self::disc);
                if !self.original.is_empty() {
                    self.open_find_dialogue();
                }
            }
            Action::FindDialogue => self.open_find_dialogue(),
            Action::ExportFixes => self.run(|editor| {
                editor.save(".pefix", "Translation fixes", |e| e.project.export_fixes(), None, None).map(|_| ())
            }),
            Action::ExportGame => self.run(|editor| {
                editor.save(".pelang", "Playable language pack", |e| Ok(e.project.compile()?.0), None, None).map(|_| ())
            }),
            Action::ExportCopy => self.run(|editor| {
                editor.save(".pepack", "Editable translation project", |e| e.project.export_project(), None, None).map(|_| ())
            }),
            Action::ExportDialogue(scope) => self.run(|editor| editor.export_dialogue_csv(&scope)),
            Action::ImportTranslatedCsv => self.run(Self::import_translated_csv),
        }
    }

    fn welcome_ui(&mut self, ui: &mut Ui) {
        ui.add_space(20.0);
        egui::Frame::new().inner_margin(egui::Margin::symmetric(30, 0)).show(ui, |ui| {
            ui.label(RichText::new("Parasite Eve").size(26.0).strong());
            ui.label(heading("Translation Editor"));
            ui.add_space(8.0);
            ui.label("Browse the USA dialogue catalog or try the opening opera demo");
            ui.add_space(22.0);
            if option(ui, "Try the opera demo", "Explore a sample translation with two dialogue boxes. No disc or font setup needed.", true) {
                self.action = Some(Action::Demo);
            }
            if option(ui, "Open an existing project", "Continue editing a project someone saved or shared with you.", false) {
                self.action = Some(Action::OpenPack);
            }
            if option(ui, "Start a new translation", "Pick a language and begin with an empty translation.", false) {
                self.action = Some(Action::New);
            }
            ui.label("Have an existing translation CSV?");
            if ui.button("Open translation CSV…").clicked() {
                self.action = Some(Action::ImportCsv);
            }
        });
    }

    fn top_bar_ui(&mut self, ui: &mut Ui) {
        ui.horizontal(|ui| {
            ui.vertical(|ui| {
                ui.label(heading(&self.scene_label));
                ui.label(&self.project_label);
            });
            ui.with_layout(Layout::right_to_left(egui::Align::Center), |ui| {
                if ui.add(Button::new(RichText::new("Share or export…").strong()).min_size(egui::vec2(0.0, 30.0))).clicked() {
                    self.action = Some(Action::Share);
                }
                if ui.add(Button::new(RichText::new("Save project").strong()).min_size(egui::vec2(0.0, 30.0))).clicked() {
                    self.action = Some(Action::ExportPack { save_as: false });
                }
                if ui.button("Translate with AI…").clicked() {
                    self.action = Some(Action::AiExchange);
                }
                ui.menu_button("Project ⏷", |ui| {
                    let items: [(&str, fn() -> Action); 7] = [
                        ("Project settings…", || Action::Settings),
                        ("Apply someone's fixes…", || Action::ImportFixes),
                        ("Save project as…", || Action::ExportPack { save_as: true }),
                        ("Start a new translation…", || Action::New),
                        ("Open another project…", || Action::OpenPack),
                        ("Open translation CSV…", || Action::ImportCsv),
                        ("Try the opera demo", || Action::Demo),
                    ];
                    for (label, action) in items {
                        if ui.button(label).clicked() {
                            self.action = Some(action());
                            ui.close();
                        }
                    }
                });
            });
        });
    }

    fn navigation_ui(&mut self, ui: &mut Ui) {
        ui.label(heading("Find a scene"));
        let search = ui.add(TextEdit::singleline(&mut self.scene_query).desired_width(f32::INFINITY));
        if search.changed() {
            self.filter_scenes();
        }
        if search.lost_focus() && ui.input(|i| i.key_pressed(Key::Enter)) {
            self.first_scene();
        }
        let mut chosen = None;
        ComboBox::from_id_salt("scene-picker")
            .width(ui.available_width())
            .truncate()
            .selected_text(self.scene_choice.as_str())
            .height(420.0)
            .show_ui(ui, |ui| {
                for caption in &self.scene_values {
                    if ui.selectable_label(*caption == self.scene_choice, caption).clicked() {
                        chosen = Some(caption.clone());
                    }
                }
            });
        if let Some(caption) = chosen {
            self.sync();
            let scope = self.scene_labels[&caption].clone();
            self.set_scene(&scope, None);
        }
        ui.add_space(6.0);
        if ui.add_sized([ui.available_width(), 24.0], Button::new("Find original dialogue…")).clicked() {
            self.action = Some(Action::FindDialogue);
        }
        ui.add_space(6.0);
        ui.label(heading("Scene dialogue"));
        ui.label(&self.progress);
        ui.add_space(4.0);
        let mut filter = self.line_filter.clone();
        ComboBox::from_id_salt("line-filter").width(ui.available_width()).selected_text(filter.as_str()).show_ui(ui, |ui| {
            for choice in LINE_FILTERS {
                ui.selectable_value(&mut filter, choice.to_owned(), choice);
            }
        });
        if filter != self.line_filter {
            self.line_filter = filter;
            self.change_filter();
        }
        ui.add_space(4.0);
        let reserve = if self.filter_status.is_empty() { 0.0 } else { 44.0 };
        let mut clicked = None;
        let scroll_to = std::mem::take(&mut self.scroll_to_selected_line);
        ScrollArea::vertical().id_salt("lines").auto_shrink(false).max_height(ui.available_height() - reserve).show(ui, |ui| {
            ui.with_layout(Layout::top_down_justified(egui::Align::Min), |ui| {
                for (index, text) in self.line_items.iter().enumerate() {
                    let selected = self.visible_keys[index] == self.key;
                    let response = ui.selectable_label(selected, text);
                    if response.clicked() {
                        clicked = Some(index);
                    }
                    if selected && scroll_to {
                        response.scroll_to_me(None);
                    }
                }
            });
        });
        if let Some(index) = clicked {
            self.select_line(index);
        }
        if !self.filter_status.is_empty() {
            ui.add(egui::Label::new(&self.filter_status).wrap());
        }
    }

    fn editor_ui(&mut self, ui: &mut Ui) {
        ui.horizontal(|ui| {
            ui.label(heading(&self.original_label));
            ui.with_layout(Layout::right_to_left(egui::Align::Center), |ui| {
                if ui.button("Load original dialogue…").clicked() {
                    self.action = Some(Action::Disc);
                }
            });
        });
        egui::Frame::new().fill(ORIGINAL_BACKGROUND).inner_margin(8).show(ui, |ui| {
            ScrollArea::vertical().id_salt("original").max_height(76.0).auto_shrink([false, false]).show(ui, |ui| {
                ui.add(
                    TextEdit::multiline(&mut self.original_text.as_str())
                        .frame(false)
                        .text_color(PREVIEW_BACKGROUND)
                        .desired_width(f32::INFINITY),
                );
            });
        });
        ui.add_space(8.0);
        ui.horizontal(|ui| {
            ui.label(heading("Your translation"));
            ui.with_layout(Layout::right_to_left(egui::Align::Center), |ui| {
                if ui.add_enabled(self.review_enabled, Button::new("Mark reviewed")).clicked() {
                    self.mark_reviewed();
                }
            });
        });
        let id = self.translation_id();
        ScrollArea::vertical().id_salt("translation-scroll").max_height(100.0).auto_shrink([false, false]).show(ui, |ui| {
            let response = ui.add(
                TextEdit::multiline(&mut self.translation)
                    .id(id)
                    .desired_rows(5)
                    .desired_width(f32::INFINITY),
            );
            if std::mem::take(&mut self.focus_translation) {
                response.request_focus();
            }
            if response.changed() {
                self.edited();
            }
        });
        ui.add_space(2.0);
        ui.horizontal(|ui| {
            if ui.button("Start next dialogue box").clicked() {
                self.insert_box(ui.ctx());
            }
            ui.label("Enter starts a new line. Keep tags like {name}.");
        });
        ui.add(egui::Label::new("Start next dialogue box adds a {box} marker where the player will press a button to continue.").wrap());
        ui.add_space(6.0);
        ui.horizontal(|ui| {
            ui.label(heading("In-game preview"));
            ui.with_layout(Layout::right_to_left(egui::Align::Center), |ui| {
                if ui.add_enabled(self.next_enabled, Button::new("Next box ›")).clicked() {
                    self.step_box(1);
                }
                if ui.add_enabled(self.previous_enabled, Button::new("‹ Previous box")).clicked() {
                    self.step_box(-1);
                }
                ui.label(&self.preview_label);
            });
        });
        self.preview_ui(ui);
        ui.add(egui::Label::new(&self.status).wrap());
    }

    fn preview_ui(&self, ui: &mut Ui) {
        let (rect, _) = ui.allocate_exact_size(egui::vec2(ui.available_width(), 105.0), egui::Sense::hover());
        let painter = ui.painter_at(rect);
        painter.rect_filled(rect, 0.0, PREVIEW_BACKGROUND);
        let font = FontId::proportional(12.0);
        if let Some(placeholder) = self.preview_placeholder {
            painter.text(rect.min + egui::vec2(14.0, 15.0), egui::Align2::LEFT_TOP, placeholder, font.clone(), Color32::WHITE);
        }
        // Exact quads: anti-aliased rectangles would fade one-point glyph pixels away.
        let mut glyphs = egui::Mesh::default();
        for item in &self.preview_items {
            match item {
                PreviewItem::Glyph { x, y, pixels } => {
                    for (at, &value) in pixels.iter().enumerate() {
                        if value == 0 {
                            continue;
                        }
                        let pixel = rect.min + egui::vec2((x + at as i32 % 12) as f32, (y + at as i32 / 12) as f32);
                        let cell = egui::Rect::from_min_size(pixel, egui::vec2(1.0, 1.0));
                        glyphs.add_colored_rect(cell, GLYPH_PALETTE[value.min(2) as usize]);
                    }
                }
                PreviewItem::Text { x, y, text } => {
                    painter.text(rect.min + egui::vec2(*x as f32, *y as f32), egui::Align2::LEFT_TOP, text, font.clone(), Color32::WHITE);
                }
            }
        }
        painter.add(glyphs);
    }

    /// Returns false when the dialog should close.
    fn dialog_ui(&mut self, ctx: &egui::Context, dialog: &mut Dialog) -> bool {
        let mut keep = !ctx.input(|i| i.key_pressed(Key::Escape));
        Modal::new(Id::new("dialog")).show(ctx, |ui| {
            ui.set_max_width(560.0);
            match dialog {
                Dialog::Share => {
                    ui.label(heading("What would you like to send?"));
                    ui.add_space(10.0);
                    let choices: [(&str, &str, bool, fn() -> Action); 3] = [
                        ("Share only my fixes…", "Send edited lines to someone who already has the project. They can apply the fixes from the Project menu.", true, || Action::ExportFixes),
                        ("Export for playtesting…", "Create a playable .pelang file. Put it in the game's lang folder and select it under F1 → Language.", false, || Action::ExportGame),
                        ("Save an editable copy…", "Send a complete .pepack project that another translator can open and keep editing.", false, || Action::ExportCopy),
                    ];
                    for (label, description, primary, action) in choices {
                        if option(ui, label, description, primary) {
                            self.action = Some(action());
                            keep = false;
                        }
                    }
                    ui.with_layout(Layout::right_to_left(egui::Align::Min), |ui| {
                        keep &= !ui.button("Cancel").clicked();
                    });
                }
                Dialog::Settings(settings) => {
                    ui.label(heading(if settings.starting { "Start a translation" } else { "Project settings" }));
                    ui.add_space(6.0);
                    ui.label("Name shown in the game");
                    ui.add(TextEdit::singleline(&mut settings.title).desired_width(320.0));
                    ui.add_space(6.0);
                    ui.label("Language code (for example fr or es-419)");
                    ui.add(TextEdit::singleline(&mut settings.language).desired_width(320.0));
                    ui.add_space(6.0);
                    ui.label("Custom font for CJK characters");
                    ui.label(&settings.font_label);
                    if ui.button("Choose font…").clicked() {
                        if let Some(path) = FileDialog::new().add_filter("Font files", &["ttf", "otf", "ttc"]).pick_file() {
                            settings.font_label = path.file_name().map_or_else(String::new, |n| n.to_string_lossy().into_owned());
                            settings.font = Some(FontSource::File(path));
                        }
                    }
                    ui.add_space(12.0);
                    ui.with_layout(Layout::right_to_left(egui::Align::Min), |ui| {
                        if ui.button(if settings.starting { "Start editing" } else { "Apply settings" }).clicked() && self.apply_settings(settings) {
                            keep = false;
                        }
                        keep &= !ui.button("Cancel").clicked();
                    });
                }
                Dialog::FindNeedsDisc => {
                    ui.label("Load a USA disc to search its English dialogue.");
                    ui.add_space(8.0);
                    if ui.button("Load original dialogue…").clicked() {
                        self.action = Some(Action::LoadAndFind);
                        keep = false;
                    }
                }
                Dialog::Find(find) => keep &= self.find_ui(ui, find),
                Dialog::Ai { copied } => {
                    ui.label(heading("Export dialogue for translation"));
                    ui.add_space(10.0);
                    if option(ui, "1. Export this scene…", &format!("{} · {} lines", self.scene_label, self.scene_keys.len()), true) {
                        self.action = Some(Action::ExportDialogue(self.scope.clone()));
                        keep = false;
                    }
                    let total = format!("Save all {} unique lines in one CSV.", thousands(self.project.source.len()));
                    if option(ui, "Export all dialogue…", &total, false) {
                        self.action = Some(Action::ExportDialogue("all".to_owned()));
                        keep = false;
                    }
                    ui.label("2. Give the CSV and this prompt to your AI tool");
                    let prompt = ai_prompt(&self.project.language);
                    ScrollArea::vertical().id_salt("prompt").max_height(150.0).show(ui, |ui| {
                        ui.add(TextEdit::multiline(&mut prompt.as_str()).desired_width(520.0));
                    });
                    if ui.button(if *copied { "Prompt copied" } else { "Copy AI prompt" }).clicked() {
                        ui.ctx().copy_text(prompt.clone());
                        *copied = true;
                    }
                    ui.add_space(12.0);
                    if option(ui, "3. Import translated CSV…", "Check the returned CSV and apply its translations to the open project.", false) {
                        self.action = Some(Action::ImportTranslatedCsv);
                        keep = false;
                    }
                    ui.with_layout(Layout::right_to_left(egui::Align::Min), |ui| {
                        keep &= !ui.button("Close").clicked();
                    });
                }
            }
        });
        keep
    }

    fn find_ui(&mut self, ui: &mut Ui, find: &mut FindDialog) -> bool {
        ui.label(heading("Search the original English"));
        ui.label("Type a phrase. The search ignores capitalization and dialogue box breaks.");
        ui.add_space(4.0);
        let field_id = Id::new("find-query");
        let field = ui.add(TextEdit::singleline(&mut find.query).id(field_id).desired_width(f32::INFINITY));
        if std::mem::take(&mut find.focus_field) {
            field.request_focus();
        }
        if field.changed() {
            Self::update_find(find);
        }
        let enter = ui.input(|i| i.key_pressed(Key::Enter));
        let mut open = field.lost_focus() && enter;
        // A search with nothing to open keeps the cursor in the field.
        find.focus_field = open && find.selected.is_none();
        ui.label(&find.count);
        ScrollArea::vertical().id_salt("find-results").max_height(170.0).auto_shrink([false, false]).show(ui, |ui| {
            ui.with_layout(Layout::top_down_justified(egui::Align::Min), |ui| {
                for (row, (key, excerpt)) in find.matches.iter().enumerate() {
                    let scene = &self.scenes[&self.line_scenes[key]];
                    let area = scene.area.as_deref().filter(|area| !area.is_empty()).unwrap_or("Unlabelled scene");
                    let response = ui.selectable_label(find.selected == Some(row), format!("{area}  |  {excerpt}"));
                    if response.clicked() {
                        find.selected = Some(row);
                    }
                    if response.double_clicked() || (response.has_focus() && enter) {
                        open = true;
                    }
                }
            });
        });
        ui.add_space(6.0);
        let detail = find.selected.map_or_else(String::new, |row| {
            let key = &find.matches[row].0;
            format!("{}\n{}", self.scenes[&self.line_scenes[key]].label, find.index[key].0)
        });
        ScrollArea::vertical().id_salt("find-detail").max_height(70.0).auto_shrink([false, true]).show(ui, |ui| {
            ui.add(TextEdit::multiline(&mut detail.as_str()).desired_rows(4).desired_width(f32::INFINITY));
        });
        ui.add_space(6.0);
        ui.with_layout(Layout::right_to_left(egui::Align::Min), |ui| {
            open |= ui.add_enabled(find.selected.is_some(), Button::new("Go to line")).clicked();
        });
        if let Some(row) = find.selected.filter(|_| open) {
            let key = find.matches[row].0.clone();
            self.sync();
            let scope = self.line_scenes[&key].clone();
            self.set_scene(&scope, Some(&key));
            self.focus_translation = true;
            return false;
        }
        true
    }

    /// One frame of the whole interface.
    pub fn ui(&mut self, ctx: &egui::Context) {
        if ctx.input(|i| i.viewport().close_requested()) && !self.allow_close {
            if self.confirm_replace(true) {
                self.allow_close = true;
            } else {
                ctx.send_viewport_cmd(egui::ViewportCommand::CancelClose);
            }
        }
        if let Some(due) = self.preview_due {
            let now = Instant::now();
            if due <= now {
                self.preview();
            } else {
                ctx.request_repaint_after(due - now);
            }
        }
        if self.screen == Screen::Workspace
            && self.dialog.is_none()
            && ctx.input_mut(|i| i.consume_shortcut(&KeyboardShortcut::new(Modifiers::COMMAND, Key::S)))
        {
            self.action = Some(Action::ExportPack { save_as: false });
        }
        match self.screen {
            Screen::Welcome => {
                egui::CentralPanel::default().show(ctx, |ui| self.welcome_ui(ui));
            }
            Screen::Workspace => {
                egui::TopBottomPanel::top("top").show(ctx, |ui| {
                    ui.add_space(8.0);
                    self.top_bar_ui(ui);
                    ui.add_space(8.0);
                });
                egui::SidePanel::left("navigation").default_width(210.0).width_range(170.0..=420.0).show(ctx, |ui| {
                    ui.add_space(6.0);
                    self.navigation_ui(ui);
                });
                egui::CentralPanel::default().show(ctx, |ui| self.editor_ui(ui));
            }
        }
        if let Some(mut dialog) = self.dialog.take() {
            let keep = self.dialog_ui(ctx, &mut dialog);
            if keep && self.dialog.is_none() {
                self.dialog = Some(dialog);
            }
        }
        if let Some(action) = self.action.take() {
            self.perform(action);
            ctx.request_repaint();
        }
    }
}

impl Default for Editor {
    fn default() -> Self {
        Self::new()
    }
}

impl eframe::App for Editor {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        self.ui(ctx);
    }
}

/// Write beside the destination, then replace it, so a failed save keeps the old file.
fn write_replacing(destination: &Path, data: &[u8]) -> Result<()> {
    use std::io::Write;
    let folder = destination.parent().filter(|p| !p.as_os_str().is_empty()).unwrap_or(Path::new("."));
    let mut file = tempfile::NamedTempFile::new_in(folder)?;
    file.write_all(data)?;
    file.persist(destination).map_err(|error| Error::value(error.error.to_string()))?;
    Ok(())
}

/// Text boxes show the system's CJK faces; the demo's glyphs always display.
pub fn install_fonts(ctx: &egui::Context) {
    const CANDIDATES: &[&str] = &[
        r"C:\Windows\Fonts\msjh.ttc",
        r"C:\Windows\Fonts\msyh.ttc",
        r"C:\Windows\Fonts\meiryo.ttc",
        r"C:\Windows\Fonts\YuGothM.ttc",
        r"C:\Windows\Fonts\malgun.ttf",
        "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/Hiragino Sans GB.ttc",
        "/System/Library/Fonts/AppleSDGothicNeo.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
    ];
    let mut fonts = FontDefinitions::default();
    let mut add = |name: String, bytes: Vec<u8>| {
        fonts.font_data.insert(name.clone(), Arc::new(FontData::from_owned(bytes)));
        for family in [FontFamily::Proportional, FontFamily::Monospace] {
            fonts.families.entry(family).or_default().push(name.clone());
        }
    };
    add("opera-demo".to_owned(), FontSource::DEMO_BYTES.to_vec());
    for path in CANDIDATES {
        if let Ok(bytes) = std::fs::read(path) {
            add((*path).to_owned(), bytes);
        }
    }
    ctx.set_fonts(fonts);
}

/// Exercise the demo without a window: both preview boxes, then a full interface frame.
pub fn smoke(proof: &Path) -> Result<()> {
    let mut app = Editor::new();
    let ctx = egui::Context::default();
    install_fonts(&ctx);
    app.demo();
    app.preview();
    assert_eq!(app.preview_label, "Box 1 of 2");
    app.step_box(1);
    assert_eq!(app.preview_label, "Box 2 of 2");
    assert!(app.preview_glyph_count() > 0);
    for _ in 0..2 {
        let _ = ctx.run(egui::RawInput::default(), |ctx| app.ui(ctx));
    }
    std::fs::write(proof, &app.status)?;
    Ok(())
}
