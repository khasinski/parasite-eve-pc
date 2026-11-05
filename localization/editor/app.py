"""Desktop editor for the USA dialogue catalog."""
from pathlib import Path
import csv
import re
import struct
import sys
import tempfile
import zipfile
import tkinter as tk
from tkinter import font as tkfont
from tkinter import filedialog, messagebox, ttk

# The source launch and the frozen app share the production compiler.
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from model import BOX, DEMO_FONT, KEYS, SAMPLE, Project, load_catalog, load_source, read_original, read_small, scope_keys
from petext import cjk, gametext, layout, validation
from dialogue_csv import ai_prompt, export_dialogue, import_translation
from dialogue_search import dialogue_matches, normalize, preferred_scenes, readable_dialogue

TITLE = "Parasite Eve Translation Editor"


class Editor:
    def __init__(self, root):
        self.root = root
        self.project = Project(load_source())
        self.saved_rows = {}
        self.saved_review = set()
        self.original = {}
        self.key = KEYS[0]
        self.scenes = load_catalog()[2]
        self.line_scenes = preferred_scenes(self.scenes)
        self.scope = "opera"
        self.scene_keys = scope_keys(self.scope)
        self.scene_labels = {}
        self.scene_captions = {}
        self.progress_snapshot = None
        self.visible_keys = ()
        self.line_filter = tk.StringVar(value="All lines")
        self.filter_status = tk.StringVar()
        self.scene_choice = tk.StringVar()
        self.scene_label = tk.StringVar(value=self.scenes[self.scope]["label"])
        self.scene_query = tk.StringVar()
        self.project_path = None
        self.preview_job = None
        self.preview_page = 0
        self.preview_pages = []
        self.refreshing = False
        self.language = tk.StringVar(value=self.project.language)
        self.title = tk.StringVar(value=self.project.title)
        self.status = tk.StringVar(value="")
        self.project_label = tk.StringVar()
        self.progress = tk.StringVar()
        self.preview_label = tk.StringVar()
        self.original_label = tk.StringVar()
        root.title(TITLE)
        root.geometry("980x735")
        root.minsize(820, 650)
        root.protocol("WM_DELETE_WINDOW", self.close)
        shortcut = "Command" if sys.platform == "darwin" else "Control"
        root.bind(f"<{shortcut}-s>", lambda _: self.run(self.export_pack) if self.workspace.winfo_ismapped() else None)
        default = tkfont.nametofont("TkDefaultFont")
        style = ttk.Style(root)
        style.configure("Title.TLabel", font=(default["family"], 22, "bold"))
        style.configure("Heading.TLabel", font=(default["family"], 12, "bold"))
        style.configure("Primary.TButton", padding=(14, 8))
        style.configure("Nav.TButton", padding=(8, 4))

        self.welcome = ttk.Frame(root, padding=(38, 30))
        self.welcome.columnconfigure(0, weight=1)
        ttk.Label(self.welcome, text="Parasite Eve", style="Title.TLabel").pack(anchor="w")
        ttk.Label(self.welcome, text="Translation Editor", style="Heading.TLabel").pack(anchor="w", pady=(0, 10))
        ttk.Label(self.welcome, text="Browse the USA dialogue catalog or try the opening opera demo").pack(anchor="w", pady=(0, 24))
        self.option(self.welcome, "Try the opera demo",
                    "Explore a sample translation with two dialogue boxes. No disc or font setup needed.", self.demo, True)
        self.option(self.welcome, "Open an existing project",
                    "Continue editing a project someone saved or shared with you.", self.open_pack)
        self.option(self.welcome, "Start a new translation",
                    "Pick a language and begin with an empty translation.", self.new)
        ttk.Label(self.welcome, text="Have an existing translation CSV?").pack(anchor="w", pady=(12, 2))
        ttk.Button(self.welcome, text="Open translation CSV…", command=lambda: self.run(self.import_csv)).pack(anchor="w")

        self.workspace = ttk.Frame(root, padding=(18, 14))
        top = ttk.Frame(self.workspace)
        top.pack(fill="x", pady=(0, 12))
        heading = ttk.Frame(top)
        heading.pack(side="left", fill="x", expand=True)
        ttk.Label(heading, textvariable=self.scene_label, style="Heading.TLabel").pack(anchor="w")
        ttk.Label(heading, textvariable=self.project_label).pack(anchor="w")
        ttk.Button(top, text="Share or export…", style="Primary.TButton",
                   command=self.share).pack(side="right", padx=(6, 0))
        ttk.Button(top, text="Save project", style="Primary.TButton",
                   command=lambda: self.run(self.export_pack)).pack(side="right")
        ttk.Button(top, text="Translate with AI…", command=self.ai_exchange).pack(side="right", padx=6)
        project_menu = ttk.Menubutton(top, text="Project ▾")
        project_menu.pack(side="right", padx=(0, 6))
        menu = tk.Menu(project_menu, tearoff=False)
        for label, action in (
            ("Project settings…", self.settings), ("Apply someone's fixes…", self.import_fixes),
            ("Save project as…", lambda: self.export_pack(save_as=True)),
            ("Start a new translation…", self.new), ("Open another project…", self.open_pack),
            ("Open translation CSV…", self.import_csv), ("Try the opera demo", self.demo),
        ):
            menu.add_command(label=label, command=lambda run=action: self.run(run))
        project_menu.configure(menu=menu)

        split = ttk.Panedwindow(self.workspace, orient="horizontal")
        split.pack(fill="both", expand=True)
        nav = ttk.Frame(split, padding=(0, 0, 12, 0))
        editor = ttk.Frame(split)
        split.add(nav, weight=0)
        split.add(editor, weight=1)
        ttk.Label(nav, text="Find a scene", style="Heading.TLabel").pack(anchor="w")
        search = ttk.Entry(nav, textvariable=self.scene_query, width=26)
        search.pack(fill="x", pady=(3, 4))
        search.bind("<KeyRelease>", self.filter_scenes)
        search.bind("<Return>", self.first_scene)
        self.scene_picker = ttk.Combobox(nav, textvariable=self.scene_choice, width=29, state="readonly")
        self.scene_picker.pack(fill="x", pady=(0, 10))
        self.scene_picker.bind("<<ComboboxSelected>>", self.select_scene)
        ttk.Button(nav, text="Find original dialogue…", command=self.find_dialogue).pack(fill="x", pady=(0, 10))
        ttk.Label(nav, text="Scene dialogue", style="Heading.TLabel").pack(anchor="w")
        ttk.Label(nav, textvariable=self.progress).pack(anchor="w", pady=(2, 8))
        line_filter = ttk.Combobox(nav, textvariable=self.line_filter, state="readonly",
                                   values=("All lines", "Untranslated", "Needs review"))
        line_filter.pack(fill="x", pady=(0, 6))
        line_filter.bind("<<ComboboxSelected>>", self.change_filter)
        line_frame = ttk.Frame(nav)
        line_frame.pack(fill="both", expand=True)
        self.lines = tk.Listbox(line_frame, exportselection=False, width=21, height=12, activestyle="none",
                                selectbackground="#275a86", selectforeground="white")
        self.lines.pack(side="left", fill="both", expand=True)
        line_scroll = ttk.Scrollbar(line_frame, orient="vertical", command=self.lines.yview)
        line_scroll.pack(side="right", fill="y")
        self.lines.configure(yscrollcommand=line_scroll.set)
        self.lines.bind("<<ListboxSelect>>", self.select)
        ttk.Label(nav, textvariable=self.filter_status, wraplength=190).pack(anchor="w", pady=8)

        source_header = ttk.Frame(editor)
        source_header.pack(fill="x")
        ttk.Label(source_header, textvariable=self.original_label, style="Heading.TLabel").pack(side="left")
        ttk.Button(source_header, text="Load original dialogue…", command=lambda: self.run(self.disc)).pack(side="right")
        self.original_text = tk.Text(editor, height=4, wrap="word", relief="flat", bg="#ecf1f6", fg="#182539",
                                     font=default, padx=8, pady=7, takefocus=False)
        self.original_text.pack(fill="x", pady=(4, 10))

        translation_header = ttk.Frame(editor)
        translation_header.pack(fill="x")
        ttk.Label(translation_header, text="Your translation", style="Heading.TLabel").pack(side="left")
        self.review_button = ttk.Button(translation_header, text="Mark reviewed", command=self.mark_reviewed)
        self.review_button.pack(side="right")
        self.translation = tk.Text(editor, height=5, wrap="word", undo=True, font=default, padx=8, pady=7)
        self.translation.pack(fill="x", pady=4)
        self.translation.bind("<<Modified>>", self.edited)
        controls = ttk.Frame(editor)
        controls.pack(fill="x", pady=(0, 4))
        ttk.Button(controls, text="Start next dialogue box", command=lambda: self.insert(BOX)).pack(side="left")
        ttk.Label(controls, text="Enter starts a new line. Keep tags like {name}.").pack(side="left", padx=8)
        ttk.Label(editor, text="Start next dialogue box adds a {box} marker where the player will press a button to continue.",
                  wraplength=470).pack(anchor="w", pady=(0, 8))

        preview_header = ttk.Frame(editor)
        preview_header.pack(fill="x")
        ttk.Label(preview_header, text="In-game preview", style="Heading.TLabel").pack(side="left")
        self.next_box = ttk.Button(preview_header, text="Next box ›", command=lambda: self.step_box(1))
        self.next_box.pack(side="right")
        self.previous_box = ttk.Button(preview_header, text="‹ Previous box", command=lambda: self.step_box(-1))
        self.previous_box.pack(side="right", padx=4)
        ttk.Label(preview_header, textvariable=self.preview_label).pack(side="right", padx=6)
        self.preview_canvas = tk.Canvas(editor, height=105, bg="#182539", highlightthickness=0)
        self.preview_canvas.pack(fill="x", pady=(5, 4))
        ttk.Label(editor, textvariable=self.status, wraplength=510).pack(anchor="w", pady=(0, 4))
        self.saved_identity = self.identity()
        self.welcome.pack(fill="both", expand=True)
        self.refresh()

    def option(self, parent, label, description, action, primary=False):
        card = ttk.Frame(parent, padding=(0, 0, 0, 14))
        card.pack(fill="x")
        ttk.Button(card, text=label, style="Primary.TButton" if primary else "TButton",
                   command=lambda: self.run(action)).pack(anchor="w")
        ttk.Label(card, text=description, wraplength=580).pack(anchor="w", pady=(3, 0))

    def show_workspace(self):
        self.welcome.pack_forget()
        self.workspace.pack(fill="both", expand=True)

    def dialog(self, title):
        window = tk.Toplevel(self.root)
        window.title(title)
        window.transient(self.root)
        window.resizable(False, False)
        window.bind("<Escape>", lambda _: window.destroy())
        pane = ttk.Frame(window, padding=20)
        pane.pack(fill="both", expand=True)
        window.grab_set()
        return window, pane

    def share(self):
        window, pane = self.dialog("Share or export")
        ttk.Label(pane, text="What would you like to send?", style="Heading.TLabel").pack(anchor="w", pady=(0, 12))

        def pick(action):
            window.destroy()
            self.run(action)

        self.option(pane, "Share only my fixes…",
                    "Send edited lines to someone who already has the project. They can apply the fixes from the Project menu.",
                    lambda: pick(self.export_fixes), True)
        self.option(pane, "Export for playtesting…",
                    "Create a playable .pelang file. Put it in the game's lang folder and select it under F1 → Language.",
                    lambda: pick(self.export_game))
        self.option(pane, "Save an editable copy…",
                    "Send a complete .pepack project that another translator can open and keep editing.",
                    lambda: pick(self.export_copy))
        ttk.Button(pane, text="Cancel", command=window.destroy).pack(anchor="e")

    def settings(self, starting=False):
        self.sync()
        window, pane = self.dialog("Start a translation" if starting else "Project settings")
        title = tk.StringVar(value="My translation" if starting else self.title.get())
        language = tk.StringVar(value="fr" if starting else self.language.get())
        font = None if starting else self.project.font
        font_label = tk.StringVar(value="Game font" if starting else self.font_caption())
        ttk.Label(pane, text="Name shown in the game").pack(anchor="w")
        title_entry = ttk.Entry(pane, textvariable=title, width=36)
        title_entry.pack(fill="x", pady=(2, 9))
        ttk.Label(pane, text="Language code (for example fr or es-419)").pack(anchor="w")
        ttk.Entry(pane, textvariable=language).pack(fill="x", pady=(2, 9))

        def choose_font():
            nonlocal font
            path = filedialog.askopenfilename(parent=window, filetypes=[("Font files", "*.ttf *.otf *.ttc")])
            if path:
                font = Path(path)
                font_label.set(font.name)

        ttk.Label(pane, text="Custom font for CJK characters").pack(anchor="w")
        ttk.Label(pane, textvariable=font_label, wraplength=300).pack(anchor="w", pady=(2, 4))
        ttk.Button(pane, text="Choose font…", command=choose_font).pack(anchor="w")
        buttons = ttk.Frame(pane)
        buttons.pack(fill="x", pady=(16, 0))

        def apply():
            code, name = language.get().strip(), title.get().strip()
            if not re.fullmatch(r"[A-Za-z0-9]+(?:-[A-Za-z0-9]+)*", code) or len(code) > 15:
                messagebox.showerror(TITLE, "Enter a language code such as fr or es-419.", parent=window)
                return
            if not name or len(name.encode("utf-8")) > 31:
                messagebox.showerror(TITLE, "Enter a shorter project name.", parent=window)
                return
            window.destroy()
            if starting:
                self.replace_project(Project(load_source(), code, name, font=font))
                self.status.set("Load your disc to see the English, then pick a line and type your translation.")
            else:
                self.project.language, self.project.title, self.project.font = code, name, font
                self.language.set(code)
                self.title.set(name)
                self.update_summary()
                self.schedule_preview(reset=True)

        ttk.Button(buttons, text="Start editing" if starting else "Apply settings", command=apply).pack(side="right")
        ttk.Button(buttons, text="Cancel", command=window.destroy).pack(side="right", padx=6)
        title_entry.focus_set()

    def font_caption(self):
        if self.project.font == DEMO_FONT:
            return "Bundled opera demo font"
        if self.project.font:
            return self.project.font.name
        return "Embedded glyphs" if self.project.atlas else "Game font"

    def identity(self):
        return self.project.language, self.project.title, self.project.font

    def close(self):
        if self.confirm_replace(closing=True):
            self.root.destroy()

    def run(self, action):
        try:
            action()
        except (ValueError, OSError, KeyError, TypeError, IndexError, ImportError, EOFError, RuntimeError, csv.Error, struct.error, zipfile.BadZipFile) as error:
            messagebox.showerror(TITLE, str(error), parent=self.root)

    def sync(self):
        value = self.translation.get("1.0", "end-1c").replace("\n", "{br}")
        self.project.rows[self.key] = value
        if not value.strip():
            self.project.needs_review.discard(self.key)
        self.project.language = self.language.get().strip()
        self.project.title = self.title.get().strip()

    def filter_scenes(self, _=None):
        query = self.scene_query.get().strip().casefold()
        self.scene_picker.configure(values=[caption for scope, caption in self.scene_captions.items()
                                             if query in self.scenes[scope]["label"].casefold()])

    def first_scene(self, _):
        matches = self.scene_picker.cget("values")
        if matches and self.scene_query.get().strip():
            self.sync()
            self.set_scene(self.scene_labels[matches[0]])

    def find_dialogue(self):
        window, pane = self.dialog("Find original dialogue")
        if not self.original:
            ttk.Label(pane, text="Load a USA disc to search its English dialogue.", wraplength=400).pack(anchor="w")

            def load_and_find():
                window.destroy()
                self.run(self.disc)
                if self.original:
                    self.find_dialogue()

            ttk.Button(pane, text="Load original dialogue…", command=load_and_find).pack(anchor="w", pady=(10, 0))
            return

        ttk.Label(pane, text="Search the original English", style="Heading.TLabel").pack(anchor="w")
        ttk.Label(pane, text="Type a phrase. The search ignores capitalization and dialogue box breaks.").pack(anchor="w", pady=(3, 6))
        query = tk.StringVar()
        field = ttk.Entry(pane, textvariable=query)
        field.pack(fill="x")
        count = tk.StringVar()
        ttk.Label(pane, textvariable=count).pack(anchor="w", pady=(5, 2))

        list_frame = ttk.Frame(pane)
        list_frame.pack(fill="x")
        results = tk.Listbox(list_frame, width=88, height=10, exportselection=False)
        results.pack(side="left", fill="both", expand=True)
        scroll = ttk.Scrollbar(list_frame, orient="vertical", command=results.yview)
        scroll.pack(side="right", fill="y")
        results.configure(yscrollcommand=scroll.set)
        detail = tk.Text(pane, height=4, width=70, wrap="word", padx=6, pady=5, state="disabled")
        detail.pack(fill="x", pady=(7, 0))
        matches = []
        index = {key: (text, normalize(text)) for key, raw in self.original.items()
                 if (text := readable_dialogue(raw))}

        def selected(_=None):
            choice = results.curselection()
            detail.configure(state="normal")
            detail.delete("1.0", "end")
            if choice:
                key = matches[choice[0]][0]
                detail.insert("1.0", self.scenes[self.line_scenes[key]]["label"] + "\n" + index[key][0])
            detail.configure(state="disabled")
            open_button.configure(state="normal" if choice else "disabled")

        def update(*_):
            matches[:] = dialogue_matches(index, query.get())
            results.delete(0, "end")
            for key, excerpt in matches:
                scene = self.scenes[self.line_scenes[key]]
                results.insert("end", f"{scene['area'] or 'Unlabelled scene'}  |  {excerpt}")
            if matches:
                results.selection_set(0)
            count.set(f"{len(matches):,} matching lines" if query.get().strip() else "Enter a word or phrase to begin.")
            selected()

        def open_line(_=None):
            choice = results.curselection()
            if choice:
                key = matches[choice[0]][0]
                self.sync()
                window.destroy()
                self.set_scene(self.line_scenes[key], key)
                self.translation.focus_set()

        open_button = ttk.Button(pane, text="Go to line", command=open_line)
        open_button.pack(anchor="e", pady=(8, 0))
        results.bind("<<ListboxSelect>>", selected)
        results.bind("<Double-Button-1>", open_line)
        results.bind("<Return>", open_line)
        field.bind("<Return>", open_line)
        query.trace_add("write", update)
        update()
        field.focus_set()

    def keys_for_filter(self):
        if self.line_filter.get() == "Needs review":
            return tuple(key for key in self.scene_keys if key in self.project.needs_review)
        if self.line_filter.get() == "Untranslated":
            return tuple(key for key in self.scene_keys
                         if self.project.source[key]["structure"] is not None
                         and not self.project.rows.get(key, "").strip())
        return self.scene_keys

    def set_scene(self, scope, key=None):
        self.scope = scope
        self.scene_keys = scope_keys(scope)
        self.scene_label.set(self.scenes[scope]["label"])
        visible = self.keys_for_filter()
        if key in self.scene_keys and key not in visible:
            self.line_filter.set("All lines")
        self.key = key if key in self.scene_keys else (visible or self.scene_keys)[0]
        self.refresh()
        if self.key in self.visible_keys:
            self.lines.see(self.visible_keys.index(self.key))

    def select_scene(self, _):
        self.sync()
        self.set_scene(self.scene_labels[self.scene_choice.get()])

    def focus_key(self, key):
        scope = self.scope if key in self.scene_keys else self.line_scenes[key]
        self.set_scene(scope, key)

    def change_filter(self, _=None):
        self.sync()
        keys = self.keys_for_filter()
        if keys and self.key not in keys:
            self.key = keys[0]
            self.refresh()
        else:
            self.update_summary()

    def mark_reviewed(self):
        self.sync()
        if self.key not in self.project.needs_review:
            return
        self.project.needs_review.remove(self.key)
        self.change_filter()
        self.status.set("Line marked reviewed. Save the project to keep its review status.")

    def update_scene_counts(self, translated, review):
        snapshot = (translated, review)
        if snapshot != self.progress_snapshot:
            self.progress_snapshot = snapshot
            self.scene_captions = {}
            for scope, scene in self.scenes.items():
                keys = scene["keys"]
                total = sum(self.project.source[key]["structure"] is not None for key in keys)
                done = sum(key in translated for key in keys)
                pending = sum(key in review for key in keys)
                prefix = f"{done}/{total}" + (f" · {pending} review" if pending else "")
                self.scene_captions[scope] = f"{prefix} | {scene['label']}"
            self.scene_labels = {caption: scope for scope, caption in self.scene_captions.items()}
            self.filter_scenes()
        self.scene_choice.set(self.scene_captions[self.scope])

    def update_summary(self):
        translated = {key for key, value in self.project.rows.items()
                      if value.strip() and self.project.source[key]["structure"] is not None}
        review = set(self.project.needs_review)
        self.update_scene_counts(translated, review)
        total = sum(self.project.source[key]["structure"] is not None for key in self.scene_keys)
        done = sum(key in translated for key in self.scene_keys)
        pending = sum(key in review for key in self.scene_keys)
        self.progress.set(f"{done} of {total} translated · {pending} to review")
        changed = " · Unsaved changes" if self.dirty() else ""
        self.project_label.set(f"{self.project.title} · {self.project.language}{changed}")
        visible = self.keys_for_filter()
        order = {key: index for index, key in enumerate(self.scene_keys, 1)}
        if visible != self.visible_keys:
            self.lines.delete(0, "end")
        self.visible_keys = visible
        for index, key in enumerate(visible):
            if self.project.source[key]["structure"] is None:
                status = "Original only"
            elif key in review:
                status = "Needs review"
            elif key in translated:
                status = "Translated"
            else:
                status = "Untranslated"
            text = f"Line {order[key]:02d}   {status}"
            if self.lines.size() <= index:
                self.lines.insert("end", text)
            elif self.lines.get(index) != text:
                self.lines.delete(index)
                self.lines.insert(index, text)
        self.lines.selection_clear(0, "end")
        if self.key in visible:
            self.lines.selection_set(visible.index(self.key))
        self.filter_status.set("No lines match this filter." if not visible else
                               "The current line is outside this filter." if self.key not in visible else "")
        self.review_button.configure(state="normal" if self.key in review else "disabled")

    def refresh(self):
        self.refreshing = True
        self.update_summary()
        self.original_label.set(f"Original · Line {self.scene_keys.index(self.key) + 1:02d}")
        self.original_text.config(state="normal")
        self.original_text.delete("1.0", "end")
        original = self.original.get(self.key, "Load a USA disc to display the original dialogue here.")
        self.original_text.insert("1.0", original.replace("{br}", "\n").replace("{wait}", "\n[Next dialogue box]\n"))
        self.original_text.config(state="disabled")
        self.translation.delete("1.0", "end")
        self.translation.insert("1.0", self.project.rows.get(self.key, "").replace("{br}", "\n"))
        self.translation.edit_reset()
        self.translation.edit_modified(False)
        self.refreshing = False
        self.schedule_preview(reset=True)

    def select(self, _):
        selection = self.lines.curselection()
        if selection and not self.refreshing and self.visible_keys[selection[0]] != self.key:
            self.sync()
            self.key = self.visible_keys[selection[0]]
            self.refresh()

    def edited(self, _):
        if self.translation.edit_modified():
            self.translation.edit_modified(False)
            if not self.refreshing:
                self.sync()
                self.update_summary()
                self.schedule_preview(reset=True)

    def insert(self, value):
        self.translation.insert("insert", value)
        self.translation.focus_set()

    def schedule_preview(self, reset=False):
        if self.preview_job is not None:
            self.root.after_cancel(self.preview_job)
        if reset:
            self.preview_page = 0
        self.preview_job = self.root.after(300, self.preview)

    def disc(self):
        path = filedialog.askopenfilename(parent=self.root, filetypes=[("USA disc image", "*.bin *.cue *.iso"), ("All files", "*")])
        if path:
            self.sync()
            self.original.update(read_original(path))
            self.refresh()
            self.status.set("Original dialogue loaded from your disc.")

    def replace_project(self, project, path=None):
        self.project = project
        self.project_path = path
        self.saved_rows = project.baseline.copy()
        self.saved_review = project.needs_review.copy()
        self.line_filter.set("All lines")
        self.saved_identity = self.identity()
        self.language.set(project.language)
        self.title.set(project.title)
        self.show_workspace()
        self.set_scene("opera")
        if self.project.rows and not any(self.project.rows.get(key) for key in self.scene_keys):
            first = next((key for key, value in self.project.rows.items() if value), None)
            if first:
                self.focus_key(first)

    def dirty(self):
        return self.identity() != self.saved_identity or self.project.needs_review != self.saved_review or any(
            self.project.rows.get(key, "") != self.saved_rows.get(key, "") for key in self.project.rows.keys() | self.saved_rows.keys()
        )

    def confirm_replace(self, closing=False):
        self.sync()
        if not self.dirty():
            return True
        action = "closing" if closing else "switching projects"
        save = messagebox.askyesnocancel(TITLE, f"Save your changes before {action}?", parent=self.root)
        if save is None:
            return False
        if not save:
            return True
        try:
            return self.export_pack()
        except (ValueError, OSError, RuntimeError, struct.error) as error:
            messagebox.showerror(TITLE, str(error), parent=self.root)
            return False

    def new(self):
        if self.confirm_replace():
            self.settings(starting=True)

    def demo(self):
        if self.confirm_replace():
            self.replace_project(Project(load_source(), rows=SAMPLE.copy(), font=DEMO_FONT))

    def open_pack(self):
        if not self.confirm_replace():
            return
        path = filedialog.askopenfilename(filetypes=[("Editable language pack", "*.pepack")])
        if path:
            project = Project.import_project(load_source(), read_small(path))
            self.replace_project(project, Path(path))

    def ai_exchange(self):
        self.sync()
        window, pane = self.dialog("Translate with AI")
        ttk.Label(pane, text="Export dialogue for translation", style="Heading.TLabel").pack(anchor="w", pady=(0, 12))

        def pick(action):
            window.destroy()
            self.run(action)

        self.option(pane, "1. Export this scene…",
                    f"{self.scene_label.get()} · {len(self.scene_keys)} lines",
                    lambda: pick(lambda: self.export_dialogue_csv(self.scope)), True)
        self.option(pane, "Export all dialogue…",
                    f"Save all {len(self.project.source):,} unique lines in one CSV.",
                    lambda: pick(lambda: self.export_dialogue_csv("all")))
        ttk.Label(pane, text="2. Give the CSV and this prompt to your AI tool").pack(anchor="w")
        prompt = ai_prompt(self.project.language)
        text = tk.Text(pane, height=9, width=65, wrap="word", padx=7, pady=7)
        text.insert("1.0", prompt)
        text.configure(state="disabled")
        text.pack(fill="x", pady=(4, 5))

        def copy_prompt():
            self.root.clipboard_clear()
            self.root.clipboard_append(prompt)
            copy.configure(text="Prompt copied")

        copy = ttk.Button(pane, text="Copy AI prompt", command=copy_prompt)
        copy.pack(anchor="w", pady=(0, 14))
        self.option(pane, "3. Import translated CSV…",
                    "Check the returned CSV and apply its translations to the open project.",
                    lambda: pick(self.import_translated_csv))
        ttk.Button(pane, text="Close", command=window.destroy).pack(anchor="e")

    def export_dialogue_csv(self, scope):
        keys = scope_keys(scope)
        if any(key not in self.original for key in keys):
            self.disc()
        if any(key not in self.original for key in keys):
            if self.original:
                self.status.set("Some lines need another USA disc. Load it before exporting this selection.")
            return
        if self.save(".csv", "Dialogue for translation",
                     lambda: export_dialogue(self.original, self.project.source, scope),
                     initialfile=f"{scope}-{self.project.language}.csv"):
            self.status.set(f"Exported {len(keys):,} lines. Open Translate with AI again to copy the prompt.")

    def import_translated_csv(self):
        path = filedialog.askopenfilename(parent=self.root, filetypes=[("Translated dialogue CSV", "*.csv")])
        if not path:
            return
        self.sync()
        rows = import_translation(read_small(path), self.project.source)
        changed = {key: value for key, value in rows.items() if self.project.rows.get(key) != value}
        if not changed:
            self.status.set("The CSV has no new translations to apply.")
            return
        overwrites = sum(bool(self.project.rows.get(key)) for key in changed)
        if overwrites and not messagebox.askyesno(
            TITLE, f"Apply {len(changed)} translations and replace {overwrites} existing edits?",
            parent=self.root,
        ):
            return
        self.project.apply_ai_translations(changed)
        self.line_filter.set("Needs review")
        self.focus_key(next(iter(changed)))
        self.status.set(f"Applied {len(changed)} translations. Check each line, then choose Mark reviewed.")

    def import_csv(self):
        if not self.confirm_replace():
            return
        path = filedialog.askopenfilename(filetypes=[("Translation CSV", "*.csv")])
        if not path:
            return
        reader = csv.DictReader(read_small(path).decode("utf-8-sig").splitlines(keepends=True))
        if not {"key", "translation"} <= set(reader.fieldnames or []):
            raise ValueError("The CSV needs key and translation columns.")
        rows = {}
        ignored = 0
        for row in reader:
            key, value = row["key"], row["translation"]
            if key not in self.project.source:
                ignored += 1
            elif key in rows or value is None:
                raise ValueError("Duplicate or incomplete scene entry in the CSV.")
            else:
                rows[key] = value
        if not rows:
            raise ValueError("This CSV has no entries in the dialogue catalog.")
        project = Project(load_source(), self.project.language, self.project.title, rows, rows.copy(), self.project.font)
        self.replace_project(project)
        self.status.set(f"Opened {len(rows)} dialogue entries. Skipped {ignored} non-dialogue or unknown rows.")

    def import_fixes(self):
        path = filedialog.askopenfilename(filetypes=[("Translation fixes", "*.pefix")])
        if path:
            self.sync()
            count = self.project.import_fixes(read_small(path))
            self.refresh()
            self.status.set(f"Imported {count} line(s). Preview the edits before exporting.")

    def save(self, suffix, label, produce, destination=None, initialfile=None):
        self.sync()
        data = produce()
        if destination is None:
            path = filedialog.asksaveasfilename(parent=self.root, defaultextension=suffix,
                                                filetypes=[(label, "*" + suffix)],
                                                initialfile=initialfile or self.project.language + suffix)
            if not path:
                return None
            destination = Path(path)
        with tempfile.NamedTemporaryFile(dir=destination.parent, delete=False) as file:
            temporary = Path(file.name)
            file.write(data)
        try:
            temporary.replace(destination)
        finally:
            temporary.unlink(missing_ok=True)
        self.status.set(f"Saved {destination.name}.")
        return destination

    def export_fixes(self):
        self.save(".pefix", "Translation fixes", self.project.export_fixes)

    def export_pack(self, save_as=False):
        path = self.save(".pepack", "Editable translation project", self.project.export_project,
                         None if save_as else self.project_path)
        if path:
            self.project_path = path
            self.saved_rows = self.project.rows.copy()
            self.saved_review = self.project.needs_review.copy()
            self.saved_identity = self.identity()
            self.update_summary()
        return bool(path)

    def export_copy(self):
        self.save(".pepack", "Editable translation project", self.project.export_project)

    def export_game(self):
        self.save(".pelang", "Playable language pack", lambda: self.project.compile()[0])

    def preview(self):
        if self.preview_job is not None:
            self.root.after_cancel(self.preview_job)
            self.preview_job = None
        self.sync()
        self.preview_pages = []
        self.preview_atlas = None
        self.preview_warnings = []
        self.preview_error = ""
        text = self.project.rows.get(self.key, "")
        if text:
            # Preview the selected line even if another line still has errors.
            try:
                draft = Project(self.project.source, rows={self.key: text},
                                font=self.project.font, atlas=self.project.atlas)
                _, previews, self.preview_atlas = draft.compile()
                raw, self.preview_warnings = previews[self.key]
                self.preview_pages = [[]]
                for command in validation.commands(raw):
                    if command == b"\xf8":
                        self.preview_pages.append([])
                    else:
                        self.preview_pages[-1].append(command)
            except (ValueError, OSError, TypeError, RuntimeError, ImportError, struct.error) as error:
                self.preview_error = str(error).removeprefix(f"{self.key}: ")
        self.draw_preview()

    def step_box(self, direction):
        next_page = self.preview_page + direction
        if 0 <= next_page < len(self.preview_pages):
            self.preview_page = next_page
            self.draw_preview()

    def draw_preview(self):
        from PIL import Image, ImageTk

        canvas = self.preview_canvas
        canvas.delete("all")
        self.images = []
        count = len(self.preview_pages)
        self.preview_page = min(self.preview_page, max(0, count - 1))
        self.previous_box.configure(state="normal" if self.preview_page > 0 else "disabled")
        self.next_box.configure(state="normal" if self.preview_page + 1 < count else "disabled")
        self.preview_label.set(f"Box {self.preview_page + 1} of {count}" if count else "")
        if not count:
            placeholder = "This line uses the original dialogue." if not self.preview_error else "Preview unavailable until this line is fixed."
            canvas.create_text(14, 15, text=placeholder, fill="white", anchor="nw")
            self.status.set(self.preview_error)
            return

        x, y = 14, 14
        for command in self.preview_pages[self.preview_page]:
            opcode = command[0]
            if opcode == 0xF7:
                x, y = 14, y + 19
                continue
            if opcode == 0xF1:
                index = cjk.index(command)
                pixels = self.preview_atlas.records[index * 76 + 4:(index + 1) * 76]
                glyph = Image.new("RGB", (12, 12))
                palette = [(24, 37, 57), (255, 255, 255), (72, 86, 105)]
                glyph.putdata([palette[min(value, 2)] for byte in pixels for value in (byte & 15, byte >> 4)])
                image = ImageTk.PhotoImage(glyph)
                self.images.append(image)
                canvas.create_image(x, y, image=image, anchor="nw")
            else:
                text = "Aya" if opcode == 0xFA else gametext.decode(command) if opcode < 0xF1 else ""
                canvas.create_text(x, y, text=text, fill="white", anchor="nw")
            x += layout.advance(command)
        self.status.set(f"{count} compiled box(es). " + (" • ".join(self.preview_warnings) or "Layout and control checks passed."))


if __name__ == "__main__":
    root = tk.Tk()
    app = Editor(root)
    if len(sys.argv) == 3 and sys.argv[1] == "--smoke":
        root.withdraw()
        app.demo()
        app.preview()
        assert app.preview_label.get() == "Box 1 of 2"
        app.step_box(1)
        assert app.preview_label.get() == "Box 2 of 2" and app.images
        root.update_idletasks()
        Path(sys.argv[2]).write_text(app.status.get(), encoding="utf-8")
        root.destroy()
    else:
        root.mainloop()
