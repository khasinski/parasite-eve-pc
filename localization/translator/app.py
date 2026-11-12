"""The Parasite Eve Translator's window.

  python -I localization/translate.py [translation.pelang] [--english "English pack.pelang"]

Open the English pack (made from your own discs with `pelang.py english`), start a new translation
or open one, and translate line by line. Each line is checked by the port's own pack compiler as
you type, and drawn as the game will break it. Save writes one .pelang: the file the game loads,
which is also the file you open again to go on. All the work is in project.py; this file is only
the window.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import queue
import sys
import threading
import time
import traceback
import tkinter as tk
import tkinter.font as tkfont
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

from . import project as P

SETTINGS = Path.home() / ".pe-translator.json"
SCALE = 2                                    # the preview: the game's pixels, twice the size,
SMALLEST = 1.0                               # or smaller, down to their own size, when the window is narrow
SIDE_PAD = 6                                 # the preview's side: its padding left and right
VIEW = 136                                   # game rows the preview shows: the box and what runs past it
BOX_AT = 34                                  # where the box's first line sits in it, under the notes
FILTERS = ("All lines", "Not translated", "Translated", "With problems")
MARK = {"": "", "ok": "✓", "warning": "!", "error": "✖"}
PACK_TYPES = [("Parasite Eve language pack", "*.pelang"), ("All files", "*.*")]


def load_settings() -> dict:
    try:
        data = json.loads(SETTINGS.read_text(encoding="utf-8"))
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


def save_settings(data: dict) -> None:
    try:
        SETTINGS.write_text(json.dumps(data, indent=1), encoding="utf-8")
    except OSError:
        pass                                 # the settings only save a click next time


def one_line(text: str, limit: int = 90) -> str:
    text = text.replace("\n", " ")
    return text if len(text) <= limit else text[:limit - 1] + "…"


class Worker:
    """One thread for the compiler, so typing never waits for it. Results come back on the
    window's own thread (poll)."""

    def __init__(self) -> None:
        self.jobs: queue.Queue = queue.Queue()
        self.done: queue.Queue = queue.Queue()
        threading.Thread(target=self._run, daemon=True).start()

    def _run(self) -> None:
        while True:
            work, then = self.jobs.get()
            try:
                self.done.put((then, work(), None))
            except Exception as e:           # shown to the person, never swallowed
                self.done.put((then, None, e))

    def ask(self, work, then) -> None:
        self.jobs.put((work, then))

    def poll(self) -> None:
        while True:
            try:
                then, result, error = self.done.get_nowait()
            except queue.Empty:
                return
            then(result, error)


class App:
    def __init__(self, root: tk.Tk, english: str | None = None, translation: str | None = None,
                 select: str | None = None, type_text: str | None = None,
                 new: tuple[str, str] | None = None, report: str | None = None,
                 screenshot: str | None = None) -> None:
        """`new` (language code, name) starts a new translation saved as `translation`, with no
        dialog. `report`: save once the typing is in, quit, and write what happened to that file
        (a JSON object) — the check a test or the download's build runs, since a program with no
        console can't print. `screenshot`: with `report`, a PNG of the window just before saving."""
        self.root = root
        self.report = report
        self.screenshot = screenshot if report else None
        self.shot_error: str | None = "not taken"
        self.started = time.monotonic()
        self.english_busy = False
        self.english_error: str | None = None
        self.exit_code = 0
        if report:
            root.report_callback_exception = lambda *exc: self.finish(
                "".join(traceback.format_exception(*exc)).strip())
        self.game = P.Game()
        self.settings = load_settings()
        self.worker = Worker()
        self.english: dict[str, str] = {}
        self.tr: P.Translation | None = None
        self.state: dict[str, str] = {}      # key -> "", "ok", "warning", "error"
        self.said: dict[str, tuple[list[str], list[str]]] = {}
        self.key: str | None = None
        self.group = "sys"
        self.dirty = False
        self.loading = False                 # the editor is being filled: not the person typing
        self.pending = None                  # the timer of the next check
        self.asked = 0                       # the newest check asked for; older answers are dropped
        self.preview: P.Preview | None = None
        self.screen = 0
        self._select_first, self._type_first = select, type_text
        self._typed_by_script = (select, type_text) if select and type_text else None
        if select in self.game.rows:
            self.group = self.game.group(select)
        self._build_window()
        root.protocol("WM_DELETE_WINDOW", self.close)
        self._tick_id = root.after(50, self._tick)
        root.bind("<Destroy>", self._closing, add="+")
        english = english or self.settings.get("english")
        if english and Path(english).is_file():
            self.load_english(english, quiet=True)
        elif english and report:
            self.english_error = f"no such file: {english}"
        if new and translation:
            tr = P.Translation(self.game, *new)
            try:
                tr.save(translation)
            except (OSError, RuntimeError, SystemExit) as e:
                if report:
                    self.finish(f"could not start the translation: {e}")
                    return
                raise
            self.use(tr)
        elif translation:
            self.load_translation(translation)
        if report:
            root.after(100, self._save_when_typed)

    # ---- the window ----

    def _build_window(self) -> None:
        root = self.root
        root.title("Parasite Eve Translator")
        # The size it opens at, made smaller to fit a smaller screen (laptops, a 1280 wide screen).
        window_width = min(1360, root.winfo_screenwidth() - 40)
        height = min(820, root.winfo_screenheight() - 80)
        root.geometry(f"{window_width}x{height}")
        root.minsize(min(1100, window_width), min(700, height))
        self.font = tkfont.nametofont("TkDefaultFont")
        self.text_font = tkfont.Font(family=self.font.cget("family"), size=11)
        self.scale = SCALE
        self.game_font = tkfont.Font(family=self.font.cget("family"), size=-11 * SCALE)

        bar = ttk.Frame(root, padding=6)
        bar.pack(fill="x")
        ttk.Button(bar, text="Open English pack…", command=self.ask_english).pack(side="left")
        ttk.Button(bar, text="New translation…", command=self.new_translation).pack(side="left", padx=(6, 0))
        ttk.Button(bar, text="Open translation…", command=self.ask_translation).pack(side="left", padx=(6, 0))
        self.save_button = ttk.Button(bar, text="Save", command=self.save, state="disabled")
        self.save_button.pack(side="left", padx=(6, 0))
        self.title = ttk.Label(bar, text="Open the English pack to begin.")
        self.title.pack(side="left", padx=12)
        self.progress = ttk.Label(bar, text="")
        self.progress.pack(side="right")

        self.status = ttk.Label(root, text="", padding=(6, 3), relief="sunken", anchor="w")
        self.status.pack(fill="x", side="bottom")

        panes = ttk.PanedWindow(root, orient="horizontal")
        panes.pack(fill="both", expand=True)

        left = ttk.Frame(panes, padding=(6, 0, 0, 6))
        ttk.Label(left, text="Parts of the game").pack(anchor="w")
        self.groups = tk.Listbox(left, exportselection=False, width=26, activestyle="none")
        self.groups.pack(fill="both", expand=True)
        self.groups.bind("<<ListboxSelect>>", self._group_picked)
        panes.add(left, weight=0)

        middle = ttk.Frame(panes, padding=(6, 0, 0, 6))
        tools = ttk.Frame(middle)
        tools.pack(fill="x")
        ttk.Label(tools, text="Show").pack(side="left")
        self.filter = ttk.Combobox(tools, values=FILTERS, state="readonly", width=16)
        self.filter.set(FILTERS[0])
        self.filter.pack(side="left", padx=(4, 10))
        self.filter.bind("<<ComboboxSelected>>", lambda e: self.fill_rows())
        ttk.Label(tools, text="Find").pack(side="left")
        self.find = tk.StringVar()
        entry = ttk.Entry(tools, textvariable=self.find, width=10)     # it grows into the room there is
        entry.pack(side="left", fill="x", expand=True, padx=(4, 0))
        self.find.trace_add("write", lambda *a: self.fill_rows())
        self.everywhere = tk.BooleanVar(value=False)
        ttk.Checkbutton(tools, text="in every part", variable=self.everywhere,
                        command=self.fill_rows).pack(side="left", padx=(6, 0))
        table = ttk.Frame(middle)
        table.pack(fill="both", expand=True, pady=(4, 0))
        self.rows = ttk.Treeview(table, columns=("state", "line", "english", "translation"), show="headings",
                                 selectmode="browse")
        # The Line column fits the longest line name in the list's own font (macOS and Linux fonts
        # are wider than Windows', so a fixed width cut "M001/004" to "M001/00" there).
        list_font = tkfont.Font(root=root, font=str(ttk.Style(root).lookup("Treeview", "font") or "TkDefaultFont"))
        line_width = max(list_font.measure(key.split("/", 1)[1]) for key in self.game.keys) + 16
        for column, title, width, stretch in (("state", "", 26, False), ("line", "Line", line_width, False),
                                               ("english", "English", 160, True),
                                               ("translation", "Translation", 160, True)):
            self.rows.heading(column, text=title, anchor="w")
            self.rows.column(column, width=width, minwidth=width if not stretch else 80, stretch=stretch,
                             anchor="center" if column == "state" else "w")
        scroll = self.rows_scroll = ttk.Scrollbar(table, orient="vertical", command=self.rows.yview)
        self.rows.configure(yscrollcommand=scroll.set)
        scroll.pack(side="right", fill="y")     # packed first, so a narrow list never loses it
        self.rows.pack(side="left", fill="both", expand=True)
        self.rows.tag_configure("error", foreground="#b00020")
        self.rows.tag_configure("warning", foreground="#8a5a00")
        self.rows.tag_configure("empty", foreground="#707070")
        self.rows.bind("<<TreeviewSelect>>", self._row_picked)
        panes.add(middle, weight=3)

        right = ttk.Frame(panes, padding=(SIDE_PAD, 0, SIDE_PAD, 6))
        self.where = ttk.Label(right, text="", wraplength=int(320 * SMALLEST), justify="left")
        self.where.pack(anchor="w")
        ttk.Label(right, text="English (the game's own)").pack(anchor="w", pady=(6, 0))
        # The text boxes ask for no width of their own (width=1) and fill the side: a tk.Text asks
        # for 80 letters by default, which pushed this side wider than the preview with the wider
        # fonts of macOS and Linux and squeezed the list of lines.
        self.english_box = tk.Text(right, width=1, height=5, wrap="word", font=self.text_font, state="disabled",
                                   background="#f2f2f2", relief="solid", borderwidth=1)
        self.english_box.pack(fill="x")
        row = ttk.Frame(right)
        row.pack(fill="x", pady=(6, 0))
        ttk.Label(row, text="Translation").pack(side="left")
        self.tags = ttk.Frame(row)
        self.tags.pack(side="left", padx=(10, 0))
        self.edit = tk.Text(right, width=1, height=6, wrap="word", font=self.text_font, undo=True, relief="solid",
                            borderwidth=1, state="disabled")
        self.edit.pack(fill="x")
        self.edit.bind("<<Modified>>", self._typed)
        note = ttk.Frame(right)
        note.pack(fill="x", pady=(4, 0))
        ttk.Label(note, text="Your note").pack(side="left")
        self.note = tk.StringVar()
        self.note_entry = ttk.Entry(note, textvariable=self.note, state="disabled")
        self.note_entry.pack(side="left", fill="x", expand=True, padx=(4, 0))
        self.note.trace_add("write", self._note_typed)
        self.messages = tk.Text(right, width=1, height=5, wrap="word", state="disabled", relief="flat",
                                background=root.cget("background"), font=self.font)
        self.messages.pack(fill="x", pady=(6, 0))
        self.messages.tag_configure("error", foreground="#b00020")
        self.messages.tag_configure("warning", foreground="#8a5a00")
        self.messages.tag_configure("fine", foreground="#1b6e2d")
        head = ttk.Frame(right)
        head.pack(fill="x", pady=(4, 0))
        self.preview_title = ttk.Label(head, text="How the game draws it")
        self.preview_title.pack(side="left")
        self.next_screen = ttk.Button(head, text="Next screen ▶", command=lambda: self.turn(1), state="disabled")
        self.next_screen.pack(side="right")
        self.prev_screen = ttk.Button(head, text="◀ Screen before", command=lambda: self.turn(-1), state="disabled")
        self.prev_screen.pack(side="right", padx=(0, 4))
        # The preview asks for its smallest size and fills its side; _fit_panes sizes the side.
        self.canvas = tk.Canvas(right, width=int(320 * SMALLEST), height=VIEW * SCALE, background="#101418",
                                highlightthickness=1, highlightbackground="#808080")
        self.canvas.pack(fill="x", pady=(2, 0))
        panes.add(right, weight=0)
        self.panes, self.left_side, self.middle_side, self.right_side = panes, left, middle, right
        self._fit_id = None
        panes.bind("<Configure>", self._fit_panes_soon)
        self.canvas.bind("<Configure>", self._preview_resized)

        root.bind("<Control-s>", lambda e: self.save())
        root.bind("<Control-Down>", lambda e: self.step(1))
        root.bind("<Control-Up>", lambda e: self.step(-1))
        self.fill_groups()

    def say(self, text: str) -> None:
        self.status.configure(text=text)

    def _tick(self) -> None:
        self.worker.poll()
        self._tick_id = self.root.after(50, self._tick)

    def _closing(self, event) -> None:
        if event.widget is self.root:        # the window itself, not one of its parts
            self.root.after_cancel(self._tick_id)
            for waiting in ("pending", "_fit_id"):    # a line check, a layout, still waiting
                if getattr(self, waiting) is not None:
                    self.root.after_cancel(getattr(self, waiting))
                    setattr(self, waiting, None)

    # ---- the English pack ----

    def ask_english(self) -> None:
        path = filedialog.askopenfilename(title="Open the English pack", filetypes=PACK_TYPES)
        if path:
            self.load_english(path)

    def load_english(self, path: str, quiet: bool = False) -> None:
        self.say("Reading the English pack…")
        self.english_busy = True

        def done(result, error):
            self.english_busy = False
            if error or result[1]:
                self.english_error = str(error) if error else f"{result[1]} lines are not the game's own English"
                self.say("")
                if not quiet:
                    why = str(error) if error else (f"{result[1]} of its lines are not the game's own English. "
                                                    "Use the pack `pelang.py english` made from your discs.")
                    messagebox.showerror("Not the English pack", f"{Path(path).name}\n\n{why}")
                return
            self.english, self.english_error = result[0], None
            self.settings["english"] = str(path)
            save_settings(self.settings)
            self.say(f"English pack: {len(self.english):,} lines, each one the game's own.")
            if self.tr is None:
                self.title.configure(text="Now start a new translation, or open one.")
            self.fill_rows()
            self.show_row()

        self.worker.ask(lambda: P.read_english(path, self.game), done)

    # ---- a translation ----

    def new_translation(self) -> None:
        if not self.may_leave():
            return
        dialog = tk.Toplevel(self.root)
        dialog.title("New translation")
        dialog.transient(self.root)
        dialog.grab_set()
        frame = ttk.Frame(dialog, padding=12)
        frame.pack(fill="both", expand=True)
        choices = P.languages()
        ttk.Label(frame, text="Language").grid(row=0, column=0, sticky="w")
        picked = tk.StringVar()
        box = ttk.Combobox(frame, textvariable=picked, state="readonly", width=44,
                           values=[f"{c.name}  ({c.code})" + ("" if c.state == "ready" and not c.note else f"  — {c.state}")
                                   for c in choices])
        box.grid(row=0, column=1, sticky="we", padx=(8, 0))
        ttk.Label(frame, text="Name the game shows").grid(row=1, column=0, sticky="w", pady=(8, 0))
        shown = tk.StringVar()
        ttk.Entry(frame, textvariable=shown, width=46).grid(row=1, column=1, sticky="we", padx=(8, 0), pady=(8, 0))
        about = ttk.Label(frame, text="", wraplength=420, justify="left")
        about.grid(row=2, column=0, columnspan=2, sticky="w", pady=(8, 0))
        go = ttk.Button(frame, text="Choose where to save it…", state="disabled")
        go.grid(row=3, column=1, sticky="e", pady=(12, 0))

        def choice():
            return choices[box.current()] if box.current() >= 0 else None

        def changed(*_):
            c = choice()
            if not c:
                return
            shown.set(c.name)
            about.configure(text=c.note[:300] if c.note else "The game can draw every letter of this language.")
            go.configure(state="disabled" if c.state == "not yet" else "normal")

        def start():
            c, name = choice(), shown.get().strip()
            if not c or not name:
                return
            path = filedialog.asksaveasfilename(parent=dialog, title="Save the new translation as",
                                                defaultextension=".pelang", filetypes=PACK_TYPES,
                                                initialfile=f"Parasite Eve - {name}.pelang")
            if not path:
                return
            tr = P.Translation(self.game, c.code, name)
            try:
                tr.save(path)
            except (OSError, RuntimeError, SystemExit) as e:
                messagebox.showerror("Could not start the translation", str(e), parent=dialog)
                return
            dialog.destroy()
            self.use(tr)

        box.bind("<<ComboboxSelected>>", changed)
        go.configure(command=start)

    def ask_translation(self) -> None:
        if not self.may_leave():
            return
        path = filedialog.askopenfilename(title="Open a translation", filetypes=PACK_TYPES)
        if path:
            self.load_translation(path)

    def load_translation(self, path: str) -> None:
        self.say(f"Opening {Path(path).name}…")

        def done(tr, error):
            if error:
                self.say("")
                messagebox.showerror("Could not open it", f"{Path(path).name}\n\n{error}")
                return
            self.use(tr)
            if tr.locked:
                whole = tr.locked.get("")
                messagebox.showinfo(
                    "Open for reading only",
                    f"{Path(path).name} was not made with this tool, and saving it from here would change "
                    + (f"it: {whole}" if whole else f"{len(tr.locked):,} of its lines that nobody edited "
                       "(they were written under a fan patch's own rules).")
                    + "\n\nYou can read it and search it. Saving is switched off for this file, so it can't be damaged.")

        self.worker.ask(lambda: P.Translation.open(path, self.game), done)

    def use(self, tr: P.Translation) -> None:
        self.tr, self.dirty = tr, False
        self.state, self.said = {}, {}
        self.settings["last"] = str(tr.path)
        save_settings(self.settings)
        self.save_button.configure(state="disabled" if tr.locked else "normal")
        self.title.configure(text=f"{tr.name} ({tr.lang})  —  {tr.path.name}"
                             + ("  —  reading only" if tr.locked else ""))
        self.fill_groups()
        self.fill_rows()
        self.show_row()
        self.check_all()

    def check_all(self) -> None:
        tr = self.tr
        if tr is None or tr.locked:
            self.count()
            return
        self.say("Checking every line…")

        def done(built, error):
            if error or tr is not self.tr:
                self.say(f"The check failed: {error}" if error else "")
                return
            self.take(built)
            self.say(f"Checked: {len(built.keys_with('error')):,} lines with errors, "
                     f"{len(built.keys_with('warning') - built.keys_with('error')):,} with something to look at.")

        self.worker.ask(tr.check, done)

    def take(self, built: P.Built) -> None:
        """The marks of every row from a whole check."""
        self.said = {}
        for m in built.messages:
            if m.key:
                self.said.setdefault(m.key, ([], []))[0 if m.level == "error" else 1].append(m.text)
        self.state = {}
        for key, text in self.tr.texts.items():
            if text:
                errors, warnings = self.said.get(key, ([], []))
                self.state[key] = "error" if errors else "warning" if warnings else "ok"
        self.fill_groups()
        self.fill_rows(keep=True)
        self.count()

    def count(self) -> None:
        if self.tr is None:
            self.progress.configure(text="")
            return
        self.progress.configure(text=f"{self.tr.translated():,} of {len(self.game.keys):,} lines translated"
                                + ("  •  not saved" if self.dirty else ""))

    def save(self) -> None:
        tr = self.tr
        if tr is None or tr.locked:
            return
        self.say("Saving…")

        def done(report, error):
            if error:
                self.say("Not saved.")
                messagebox.showerror("Could not save", str(error))
                return
            self.dirty = False
            self.count()
            self.say(f"Saved {report.path.name}: the game shows {report.in_pack:,} lines translated"
                     + (f"; {len(report.left_out):,} lines don't pass yet and stay in English" if report.left_out else "")
                     + ".")
            self.check_all()

        self.worker.ask(tr.save, done)

    def may_leave(self) -> bool:
        if not self.dirty or self.tr is None:
            return True
        answer = messagebox.askyesnocancel("Save your work?", f"{self.tr.name} has changes that are not saved.")
        if answer is None:
            return False
        if answer:
            try:
                self.tr.save()
            except (OSError, RuntimeError) as e:
                messagebox.showerror("Could not save", str(e))
                return False
        return True

    def close(self) -> None:
        if self.may_leave():
            self.root.destroy()

    # ---- the scripted run (--report) ----

    def _save_when_typed(self) -> None:
        """Wait for the English pack and the scripted typing to land in the translation through
        the editor, then save through the same path as the Save button, write the report, quit."""
        if self.exit_code or not self.root.winfo_exists():
            return
        if time.monotonic() - self.started > 120:
            self.finish("timed out waiting for the English pack or the typing")
            return
        tr, want = self.tr, self._typed_by_script
        # The row's own check has come back too (its mark is set), so the report can say it.
        ready = tr is not None and not self.english_busy and self.pending is None \
            and (want is None or (tr.text(want[0]) == want[1].replace("\n", "") and want[0] in self.state))
        if not ready:
            self.root.after(100, self._save_when_typed)
            return
        if self.screenshot:
            self.shot_error = self.take_screenshot(self.screenshot)

        def done(report, error):
            if error:
                self.finish(f"could not save: {error}")
                return
            self.dirty = False
            self.finish(None, report)

        self.worker.ask(tr.save, done)

    def take_screenshot(self, path: str) -> str | None:
        """The window as it is on screen, as a PNG (Pillow's screen grab). Returns why it failed."""
        try:
            from PIL import ImageGrab
            root = self.root
            root.attributes("-topmost", True)
            root.lift()
            root.update()
            time.sleep(0.3)
            root.update()
            x, y = root.winfo_rootx(), root.winfo_rooty()
            ImageGrab.grab((x, y, x + root.winfo_width(), y + root.winfo_height()), all_screens=True).save(path)
            root.attributes("-topmost", False)
            return None
        except Exception as e:               # a picture for people to look at; never fails the run
            return f"{type(e).__name__}: {e}"

    def finish(self, error: str | None, saved: P.SaveReport | None = None) -> None:
        """Write the report of a scripted run and close the window."""
        tr = self.tr
        out = {"ok": error is None, "error": error, "frozen": bool(getattr(sys, "frozen", False)),
               "seconds": round(time.monotonic() - self.started, 2), "window_title": self.root.title(),
               "english_rows": len(self.english), "english_error": self.english_error}
        if self.screenshot:
            out.update(screenshot=self.screenshot, screenshot_error=self.shot_error)
        if tr is not None:
            out.update(lang=tr.lang, name=tr.name, translated=tr.translated())
            if self._typed_by_script:
                out["row"], out["text_in_row"] = self._typed_by_script[0], tr.text(self._typed_by_script[0])
                out["row_state"] = self.state.get(self._typed_by_script[0], "")
        if saved is not None:
            data = saved.path.read_bytes()
            out.update(file=str(saved.path), file_sha256=hashlib.sha256(data).hexdigest(), in_pack=saved.in_pack,
                       left_out=saved.left_out, warnings=saved.warnings, whole=saved.whole)
        if error is None and self.english_error:
            out.update(ok=False, error=f"the English pack did not load: {self.english_error}")
        self.exit_code = 0 if out["ok"] else 1
        try:
            Path(self.report).write_text(json.dumps(out, ensure_ascii=False, indent=1), encoding="utf-8")
        except OSError:
            self.exit_code = 2
        self.root.after(0, self.root.destroy)

    # ---- the lists ----

    def fill_groups(self) -> None:
        at = self.groups.curselection()
        self.group_ids = []
        self.groups.delete(0, "end")
        done: dict[str, int] = {}
        if self.tr is not None:
            for key, text in self.tr.texts.items():
                if text:
                    done[self.game.group(key)] = done.get(self.game.group(key), 0) + 1
        for group, name, total in self.game.groups():
            self.group_ids.append(group)
            self.groups.insert("end", f"{name}   {done.get(group, 0)}/{total}" if self.tr else f"{name}   {total}")
        index = at[0] if at else self.group_ids.index(self.group)
        self.groups.selection_set(index)
        self.groups.see(index)

    def _group_picked(self, _event=None) -> None:
        at = self.groups.curselection()
        if at and self.group_ids[at[0]] != self.group:
            self.group = self.group_ids[at[0]]
            self.fill_rows()

    def shown_keys(self) -> list[str]:
        want, find = self.filter.get(), self.find.get().strip().lower()
        everywhere = bool(find) and self.everywhere.get()
        out = []
        for key in self.game.keys:
            if not everywhere and self.game.group(key) != self.group:
                continue
            text = self.tr.text(key) if self.tr else ""
            if want == FILTERS[1] and text or want == FILTERS[2] and not text:
                continue
            if want == FILTERS[3] and self.state.get(key) not in ("error", "warning"):
                continue
            if find and find not in key.lower() and find not in self.english.get(key, "").lower() \
                    and find not in text.lower():
                continue
            out.append(key)
        return out

    def fill_rows(self, keep: bool = False) -> None:
        picked = self.key
        self.rows.delete(*self.rows.get_children())
        keys = self.shown_keys()
        for key in keys:
            self.rows.insert("", "end", iid=key, values=self.row_values(key), tags=(self.row_tag(key),))
        if keep and picked in keys:
            self.loading = True
            self.rows.selection_set(picked)
            self.rows.see(picked)
            self.loading = False
        elif self._select_first in keys and (self.tr is not None or not self._type_first):
            first, self._select_first = self._select_first, None
            self.rows.selection_set(first)
            self.rows.see(first)
            if self._type_first:             # a test's typing, once the row is on screen
                typed, self._type_first = self._type_first, None
                self.root.after(400, lambda: self._type_into(first, typed))
        elif keys and not keep:              # the row being worked on stays, if it is still listed
            self.rows.selection_set(picked if picked in keys else keys[0])
            self.rows.see(picked if picked in keys else keys[0])

    def _type_into(self, key: str, typed: str) -> None:
        """The scripted typing (--select, --type), into its own row whatever was shown since."""
        if self.key != key:
            self.rows.selection_set(key)
            self.rows.see(key)
            self.key = key
            self.show_row()
        self.edit.insert("end", typed)

    def row_values(self, key: str) -> tuple:
        text = self.tr.text(key) if self.tr else ""
        return (MARK.get(self.state.get(key, ""), ""), key.split("/", 1)[1],
                one_line(self.english.get(key, "")), one_line(text))

    def row_tag(self, key: str) -> str:
        state = self.state.get(key, "")
        return state if state in ("error", "warning") else "" if (self.tr and self.tr.text(key)) else "empty"

    def refresh_row(self, key: str) -> None:
        if self.rows.exists(key):
            self.rows.item(key, values=self.row_values(key), tags=(self.row_tag(key),))

    def _row_picked(self, _event=None) -> None:
        if self.loading:
            return
        at = self.rows.selection()
        if at and at[0] != self.key:
            self.key = at[0]
            self.show_row()

    def step(self, by: int) -> str:
        keys = list(self.rows.get_children())
        if self.key in keys and 0 <= keys.index(self.key) + by < len(keys):
            nxt = keys[keys.index(self.key) + by]
            self.rows.selection_set(nxt)
            self.rows.see(nxt)
        return "break"

    # ---- one line ----

    def show_row(self) -> None:
        key = self.key
        self.loading = True
        for child in self.tags.winfo_children():
            child.destroy()
        self.english_box.configure(state="normal")
        self.english_box.delete("1.0", "end")
        self.edit.configure(state="normal")
        self.edit.delete("1.0", "end")
        self.note_entry.configure(state="normal")
        self.note.set("")
        if key is None:
            self.where.configure(text="")
        else:
            english = self.english.get(key)
            self.english_box.insert("1.0", english if english is not None else
                                    ("(open the English pack to see the game's text)" if not self.english else
                                     "(the English pack doesn't hold this line: the game draws it its own way)"))
            keep = self.game.must_keep(key)
            self.where.configure(text=f"{key}   •   {self.game.room(key)}"
                                 + (f"\nKeep these, in this order: {' '.join(keep)}" if keep else ""))
            if self.tr is not None:
                self.edit.insert("1.0", self.tr.text(key))
                self.note.set(self.tr.notes.get(key, ""))
                for tag in dict.fromkeys(["{br}"] + keep):
                    ttk.Button(self.tags, text=tag, width=len(tag) + 1,
                               command=lambda t=tag: self.edit.insert("insert", t)).pack(side="left", padx=1)
        self.english_box.configure(state="disabled")
        editable = key is not None and self.tr is not None and not self.tr.locked and self.game.writable(key)
        self.edit.configure(state="normal" if editable else "disabled",
                            background="white" if editable else "#f2f2f2")
        self.note_entry.configure(state="normal" if editable else "disabled")
        self.edit.edit_reset()
        self.edit.edit_modified(False)
        self.loading = False
        self.show_messages(*self.said.get(key, ([], [])), checked=key in self.state)
        self.preview = None
        self.draw()
        self.check_soon(0)

    def _typed(self, _event=None) -> None:
        if not self.edit.edit_modified():
            return
        self.edit.edit_modified(False)
        if self.loading or self.tr is None or self.key is None:
            return
        text = self.edit.get("1.0", "end-1c").replace("\n", "")
        if text != self.tr.text(self.key):
            self.tr.set_text(self.key, text)
            self.dirty = True
            self.state.pop(self.key, None)
            self.refresh_row(self.key)
            self.count()
            self.check_soon(300)

    def _note_typed(self, *_args) -> None:
        if self.loading or self.tr is None or self.key is None:
            return
        note = self.note.get()
        if note != self.tr.notes.get(self.key, ""):
            if note:
                self.tr.notes[self.key] = note
            else:
                self.tr.notes.pop(self.key, None)
            self.dirty = True
            self.count()

    def check_soon(self, wait: int) -> None:
        if self.pending is not None:
            self.root.after_cancel(self.pending)
        self.pending = self.root.after(wait, self.check_now)

    def check_now(self) -> None:
        self.pending = None
        key, tr = self.key, self.tr
        if key is None:
            return
        text = tr.text(key) if tr else ""
        mine = bool(text)
        if not text:                         # nothing typed: show the game's own line
            text = self.english.get(key, "")
        if not text:
            return
        self.asked += 1
        asked = self.asked
        lang, name = (tr.lang, tr.name) if tr and mine else ("en", "English")
        default = tr.default_name if tr and mine else ""
        fonts = tr.cjk_fonts if tr else ()

        def done(result, error):
            if asked != self.asked or key != self.key:
                return                       # an older answer: the line has changed since
            if error:
                self.show_messages([f"The check itself failed: {error}"], [], checked=True)
                return
            self.preview, self.screen = result.preview, 0
            self.preview_title.configure(text="How the game draws it" if mine else
                                         "How the game draws the English (nothing translated yet)")
            if mine:
                self.said[key] = (result.errors, result.warnings)
                self.state[key] = "error" if result.errors else "warning" if result.warnings else "ok"
                self.refresh_row(key)
                self.show_messages(result.errors, result.warnings, checked=True)
            else:
                self.show_messages([], [], checked=False)
            self.draw()

        self.worker.ask(lambda: P.check_row(self.game, key, text, lang, name, default_name=default,
                                            cjk_fonts=fonts), done)

    def show_messages(self, errors, warnings, checked: bool) -> None:
        box = self.messages
        box.configure(state="normal")
        box.delete("1.0", "end")
        for e in errors:
            box.insert("end", f"✖ {e}\n", "error")
        for w in warnings:
            box.insert("end", f"! {w}\n", "warning")
        if checked and not errors and not warnings:
            box.insert("end", "✓ This line passes every check.\n", "fine")
        if errors:
            box.insert("end", "Until this is fixed the game shows its own English for this line.\n", "error")
        box.configure(state="disabled")

    # ---- the preview ----

    def turn(self, by: int) -> None:
        if self.preview and self.preview.screens:
            self.screen = max(0, min(len(self.preview.screens) - 1, self.screen + by))
            self.draw()

    def _fit_panes_soon(self, event=None) -> None:
        if self._fit_id is not None:
            self.root.after_cancel(self._fit_id)
        self._fit_id = self.root.after_idle(self._fit_panes)

    def _fit_panes(self) -> None:
        """Place the divider before the preview: the list of lines gets the room it asks for, the
        preview the rest, between the game's pixels at their own size and twice it. Done whenever
        the window changes size, since a divider, once placed, doesn't follow what its sides ask
        for (on Linux and macOS it is placed at the first layout and stays)."""
        self._fit_id = None
        panes, right, canvas = self.panes, self.right_side, self.canvas
        total = panes.winfo_width()
        if total <= 1 or not self.right_side.winfo_ismapped():
            return
        # Only fixed sizes here: on macOS the sides' own widths can still be the ones from before
        # the last layout (once the preview's side measured 650 px of "padding", from a canvas 1 px
        # wide).
        divider = right.winfo_x() - panes.sashpos(1)                  # the divider's own width
        divider = divider if 0 < divider <= 20 else 5
        around = 2 * SIDE_PAD + 2 * int(canvas.cget("highlightthickness"))
        smallest, largest = int(320 * SMALLEST) + around, 320 * SCALE + around
        room = total - panes.sashpos(0) - divider - self.middle_side.winfo_reqwidth() - divider
        side = max(smallest, min(largest, room))
        panes.sashpos(1, max(panes.sashpos(0) + divider, total - side - divider))

    def _preview_resized(self, event) -> None:
        seen = event.width - 2 * int(self.canvas.cget("highlightthickness"))
        scale = max(SMALLEST, min(SCALE, seen / 320))
        self.where.configure(wraplength=max(200, event.width))
        if abs(scale - self.scale) >= 0.01:
            self.scale = scale
            self.game_font.configure(size=-round(11 * scale))
        self.draw()                          # the notes wrap to the new width too

    def draw(self) -> None:
        c, pv, s = self.canvas, self.preview, self.scale
        c.delete("all")
        self.prev_screen.configure(state="disabled")
        self.next_screen.configure(state="disabled")
        if pv is None:
            if self.key and self.said.get(self.key, ([], []))[0]:
                c.create_text(10 * s, 10 * s, anchor="nw", fill="#d0d0d0", width=300 * s, font=self.font,
                              text="This line can't be drawn until the problem above is fixed.")
            return
        if not pv.dialogue:
            c.create_text(10 * s, 10 * s, anchor="nw", fill="#d0d0d0", width=300 * s, font=self.font,
                          text="A menu line. The menus draw it in their own places:\n\n"
                               + (self.game.room(self.key) if self.key else "")
                               + (f"\n\nThis text is {pv.menu_px} px wide." if pv.menu_px is not None else ""))
            return
        screens = pv.screens
        self.screen = max(0, min(len(screens) - 1, self.screen))
        screen = screens[self.screen]
        box = pv.boxes[0] if pv.boxes else P.Box("default", 20, 174, None, None, None, False)
        top = BOX_AT                         # the screen is shown from just above the box down
        # The notes are in this computer's font, so they keep their own line heights whatever the
        # scale; the game's screen starts under them.
        note = f"Screen {self.screen + 1} of {len(screens)}"
        if box.width is not None:
            note += f"   •   box: {box.width} px wide, {box.lines} lines" + ("" if box.known else " (most likely box)")
        if pv.wrapped:
            note += "   •   line breaks added by the tool"
        shown = c.winfo_width() if c.winfo_ismapped() else int(c.cget("width"))
        wrap = max(100, shown - 16)
        first = c.create_text(12, 8, anchor="nw", fill="#b8c4d0", font=self.font, text=note, width=wrap)
        second = c.create_text(12, c.bbox(first)[3] + 2, anchor="nw", fill="#8090a0", font=self.font, width=wrap,
                               text="Widths and line breaks are the game's. Letter shapes are your computer's.")
        y0 = max((top - 6) * s, c.bbox(second)[3] + 6)

        def Y(game_y):                       # a game row to a place on the preview
            return y0 + (game_y - (top - 6)) * s

        if box.name == "default":
            c.create_rectangle(0, Y(top - 6), 320 * s, Y(top + 50), fill="#1d2733", outline="")
        elif box.width is not None and box.lines is not None:
            c.create_rectangle((box.x - 4) * s, Y(top - 6), (box.x + box.width) * s,
                               Y(top + box.lines * 12 + 2), fill="#1d2733", outline="#5f7890")
        if box.width is not None:
            edge = (box.x + box.width) * s
            c.create_line(edge, Y(top - 6), edge, Y(top + (box.lines or 4) * 12 + 2), fill="#c06060", dash=(3, 3))
        for n, (line, width) in enumerate(zip(screen.lines, screen.widths)):
            y = Y(top + n * 12)
            late = box.lines is not None and n >= box.lines
            wide = box.width is not None and width > box.width
            for piece in line:
                right_to_left = pv.right_to_left and box.width is not None
                x = (box.x + (box.width - piece.x - piece.width if right_to_left else piece.x)) * s
                colour = "#ff5a5a" if piece.red else "#ffb080" if (late or wide) else "#f4f4f4"
                if piece.kind in ("name", "number"):
                    c.create_text(x, y, anchor="nw", text=piece.text, fill="#9fd0ff" if not piece.red else colour,
                                  font=self.game_font)
                else:
                    c.create_text(x + piece.width * s / 2, y, anchor="n", text=piece.text, fill=colour,
                                  font=self.game_font)
            if wide or late:
                c.create_text(318 * s, y, anchor="ne", fill="#ffb080", font=self.font,
                              text=f"{width} px" if wide else "past the box")
        c.configure(height=round(Y(VIEW)))
        self.prev_screen.configure(state="normal" if self.screen > 0 else "disabled")
        self.next_screen.configure(state="normal" if self.screen + 1 < len(screens) else "disabled")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="The Parasite Eve Translator")
    ap.add_argument("translation", nargs="?", help="a .pelang translation to open")
    ap.add_argument("--english", help="the English pack (remembered after the first time)")
    ap.add_argument("--select", help=argparse.SUPPRESS)       # for tests: the row to show first,
    ap.add_argument("--type", help=argparse.SUPPRESS)         # and text to type into it
    # For the checks (tests, the download's build): start a new translation with no dialog, keep the
    # settings in another file than the person's own, and save, quit and report (see App).
    ap.add_argument("--new", nargs=2, metavar=("LANG", "NAME"), help=argparse.SUPPRESS)
    ap.add_argument("--settings", help=argparse.SUPPRESS)
    ap.add_argument("--report", help=argparse.SUPPRESS)
    ap.add_argument("--screenshot", help=argparse.SUPPRESS)
    a = ap.parse_args(argv)
    if a.new and not a.translation:
        ap.error("--new needs the file to save the new translation as")
    if a.settings:
        global SETTINGS
        SETTINGS = Path(a.settings)
    root = tk.Tk()
    app = App(root, a.english, a.translation, a.select, a.type, tuple(a.new) if a.new else None, a.report,
              a.screenshot)
    root.mainloop()                          # a scripted run closes the window itself (App.finish)
    return app.exit_code
