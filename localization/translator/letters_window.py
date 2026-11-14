"""The Translator's Letters window: the letters the game can't draw yet for this language.

Save them as one PNG, paint them in any paint program, load it back (no drawing in this program).
A loaded letter goes into the translation when it is saved; the game then draws it instead of a
question mark. All the work is in letterart.py; the window only shows it.
"""
from __future__ import annotations

import base64
import io
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

from . import letterart as LA

ZOOM = 12                                    # a game dot as a square this many pixels wide
PNG_TYPES = [("PNG picture", "*.png"), ("All files", "*.*")]
DOT, SHADOW, BEHIND = "#f7f7f7", "#080808", "#5a5a5a"   # the game's letter colours over a mid grey
SAMPLE_ZOOM = 2


class LettersWindow:
    def __init__(self, app) -> None:
        self.app = app
        self.win = tk.Toplevel(app.root)
        self.win.title("Letters — Parasite Eve Translator")
        self.win.transient(app.root)
        self.win.geometry(f"{min(980, app.root.winfo_screenwidth() - 60)}x{min(720, app.root.winfo_screenheight() - 100)}")
        self.win.protocol("WM_DELETE_WINDOW", self.close)
        self.notes: list[str] = []
        self.photo = None
        self.shapes: list[LA.Shape] = []

        self.intro = ttk.Label(self.win, padding=8, wraplength=900, justify="left")
        self.intro.pack(fill="x")
        panes = ttk.PanedWindow(self.win, orient="horizontal")
        panes.pack(fill="both", expand=True)
        left = ttk.Frame(panes, padding=(6, 0, 0, 6))
        self.list = ttk.Treeview(left, columns=("letter", "state", "name"), show="headings", selectmode="browse")
        for col, text, width in (("letter", "Letter", 60), ("state", "", 110), ("name", "Name", 260)):
            self.list.heading(col, text=text, anchor="w")
            self.list.column(col, width=width, stretch=col == "name")
        bar = ttk.Scrollbar(left, orient="vertical", command=self.list.yview)
        self.list.configure(yscrollcommand=bar.set)
        bar.pack(side="right", fill="y")
        self.list.pack(fill="both", expand=True)
        self.list.bind("<<TreeviewSelect>>", lambda e: self.show())
        panes.add(left, weight=1)

        right = ttk.Frame(panes, padding=(10, 0, 10, 6))
        ttk.Label(right, text="As the game draws it (its own shadow added), and in a computer font").pack(anchor="w")
        self.canvas = tk.Canvas(right, width=LA.CELL * ZOOM * 2 + 40, height=LA.CELL * ZOOM + 4,
                                highlightthickness=0, background="#2c2c2c")
        self.canvas.pack(anchor="w", pady=(2, 4))
        self.about = ttk.Label(right, text="", wraplength=540, justify="left")
        self.about.pack(anchor="w")
        buttons = ttk.Frame(right)
        buttons.pack(anchor="w", pady=(8, 0))
        self.save_png = ttk.Button(buttons, text="Save as PNG…", command=self.ask_save_png)
        self.save_png.pack(side="left")
        self.load_png = ttk.Button(buttons, text="Load PNG…", command=self.ask_load_png)
        self.load_png.pack(side="left", padx=(6, 0))
        self.undo = ttk.Button(buttons, text="Back to the question mark", command=self.clear_letter)
        self.undo.pack(side="left", padx=(6, 0))
        self.message = ttk.Label(right, text="", wraplength=540, justify="left", foreground="#8a5a00")
        self.message.pack(anchor="w", pady=(8, 0))
        self.sample_label = ttk.Label(right, text="", wraplength=540, justify="left")
        self.sample_label.pack(anchor="w", pady=(10, 0))
        self.sample = ttk.Label(right, background="#000000")
        self.sample.pack(anchor="w", pady=(2, 0))
        panes.add(right, weight=2)
        self.refresh()

    # ---- what is shown ----

    def language(self) -> str:
        tr = self.app.tr
        if tr is None:
            return ""
        known = LA.alphabets.BY_CODE.get(tr.lang)
        return known.name if known else tr.name

    def refresh(self) -> None:
        tr = self.app.tr
        picked = self.list.selection()
        self.list.delete(*self.list.get_children())
        self.shapes = LA.every_shape(tr.letters_to_draw()) if tr is not None else []
        if tr is None:
            self.intro.configure(text="Open a translation first.")
        elif not self.shapes:
            self.intro.configure(text=f"The game can draw every letter of {self.language()}: there is nothing to draw.")
        else:
            arabic = any(s.joins for s in self.shapes)
            self.intro.configure(text=(
                f"{len(self.shapes)} {'shapes' if arabic else 'letters'} of {self.language()} that the game can't draw "
                "yet: it shows a question mark for each. Save them as one PNG, paint each letter white, dot by dot, "
                "in its 12 x 12 square in your own paint program (zoom in: one dot of the game is one pixel), and "
                "load the PNG back. The tool adds the shadow. A square left empty stays a question mark."
                + (" Arabic: each letter has a row of its shapes; the orange marks show the side a shape joins on."
                   if arabic else "")))
        for k, shape in enumerate(self.shapes):
            self.list.insert("", "end", iid=str(k), values=(shape.char, "", LA.describe(shape)
                                                            + (f" ({shape.what})" if shape.what else "")))
        self.marks()
        if self.shapes:
            self.list.selection_set(picked[0] if picked and int(picked[0]) < len(self.shapes) else "0")
        self.show()

    def marks(self) -> None:
        tr = self.app.tr
        for k, shape in enumerate(self.shapes):
            self.list.set(str(k), "state", "drawn" if tr is not None and tr.letter(shape.char) else "a ? in the game")

    def picked(self) -> LA.Shape | None:
        sel = self.list.selection()
        return self.shapes[int(sel[0])] if sel and int(sel[0]) < len(self.shapes) else None

    def show(self) -> None:
        tr, shape = self.app.tr, self.picked()
        c = self.canvas
        c.delete("all")
        rows = tr.letter(shape.char) if tr is not None and shape is not None else None
        if shape is not None:
            c.create_rectangle(0, 0, LA.CELL * ZOOM + 1, LA.CELL * ZOOM + 1, fill=BEHIND, outline="")
            if rows:
                for i, v in enumerate("".join(LA.picture(rows))):
                    if v != " ":
                        x, y = i % LA.CELL * ZOOM, i // LA.CELL * ZOOM
                        c.create_rectangle(x, y, x + ZOOM, y + ZOOM, fill=DOT if v == "#" else SHADOW, outline="")
            else:
                c.create_text(LA.CELL * ZOOM // 2, LA.CELL * ZOOM // 2, text="not drawn yet\n(the game shows ?)",
                              fill="#dddddd", justify="center")
            c.create_text(LA.CELL * ZOOM * 3 // 2 + 30, LA.CELL * ZOOM // 2, text=shape.char, fill="#eeeeee",
                          font=("TkDefaultFont", 72))
            joins = {"r": "joins on its right side", "l": "joins on its left side",
                     "rl": "joins on both sides"}.get(shape.joins, "")
            width = LA.advance(rows) if rows else 0
            self.about.configure(text=f"{LA.describe(shape)}" + (f", {shape.what} shape" if shape.what else "")
                                 + (f"; {joins}" if joins else "")
                                 + (f". The pen moves on {width} dots after it." if width else "."))
        else:
            self.about.configure(text="")
        ready = tr is not None and bool(self.shapes)
        locked = tr is None or bool(tr.locked)
        self.save_png.configure(state="normal" if ready else "disabled")
        self.load_png.configure(state="normal" if ready and not locked else "disabled")
        self.undo.configure(state="normal" if ready and not locked and rows else "disabled")
        self.message.configure(text="\n".join(self.notes))
        self.show_sample()

    def show_sample(self) -> None:
        """A line of the translator's own text that uses the drawn letters, as the game draws it (with
        a disc chosen: the game's font is read from it)."""
        tr, disc = self.app.tr, self.app.look_disc
        self.photo = None
        self.sample.configure(image="")
        drawn = {s.char for s in self.shapes if tr is not None and tr.letter(s.char)}
        if not drawn:
            self.sample_label.configure(text="")
            return
        wanted = drawn | {chr(c) for c in LA.rtl.FORMS if any(chr(f) in drawn for f in LA.rtl.forms_of(c))}
        key = next((k for k in sorted(tr.texts) if k.startswith("dlg/") and set(tr.texts[k]) & wanted), None)
        if key is None:
            self.sample_label.configure(text="No dialogue line uses these letters yet: type one to see it here.")
            return
        if disc is None or not self.app_can_draw():
            self.sample_label.configure(text=f"Choose your disc (Pictures…) to see {key} here as the game draws it.")
            return
        try:
            from PIL import Image
            from . import gamelook as GL, project as P
            checked = tr.check_row(key)
            pv = checked.preview
            if pv is None or not pv.pack:
                self.sample_label.configure(text=f"{key} can't be drawn yet: " + "; ".join(checked.errors)[:200])
                return
            pack = GL.Pack.read(pv.pack)
            raw = pack.text(key) or b"\xff"
            look = GL.lay_out(GL.Font(disc, pack), raw, GL.name_bytes(pack, pv.shown_name),
                              right_to_left=P.rtl.is_rtl(pack.lang))
            rgb = GL.draw(GL.Font(disc, pack), look.screens[0])
            top = GL.LINE_Y - 2
            picture = Image.frombytes("RGB", (GL.W, GL.H), rgb).crop((0, top, GL.W, GL.H))
            picture = picture.resize((picture.width * SAMPLE_ZOOM, picture.height * SAMPLE_ZOOM), Image.NEAREST)
            data = io.BytesIO()
            picture.save(data, "PNG")
            self.photo = tk.PhotoImage(master=self.win, data=base64.b64encode(data.getvalue()).decode("ascii"),
                                       format="png")
            self.sample.configure(image=self.photo)
            self.sample_label.configure(text=f"{key} as the game draws it (first screen):")
        except Exception as e:               # a picture to look at; never in the way of the work
            self.sample_label.configure(text=f"{key} could not be drawn: {type(e).__name__}: {e}")

    def app_can_draw(self) -> bool:
        from . import gamelook as GL
        return GL.can_draw_here()

    # ---- actions ----

    def ask_save_png(self) -> None:
        tr = self.app.tr
        name = f"{self.language() or tr.name} letters.png"
        path = filedialog.asksaveasfilename(parent=self.win, title="Save the letters as", defaultextension=".png",
                                            filetypes=PNG_TYPES, initialfile=name)
        if not path:
            return
        try:
            Path(path).write_bytes(self.app.letters_png())
        except (OSError, ValueError, ImportError) as e:
            messagebox.showerror("Could not save the letters", str(e), parent=self.win)
            return
        self.notes = [f"Saved {Path(path).name}. Paint the letters in your paint program, then Load PNG…"]
        self.show()

    def ask_load_png(self) -> None:
        path = filedialog.askopenfilename(parent=self.win, title="Load your letters", filetypes=PNG_TYPES)
        if not path:
            return
        try:
            loaded = self.app.load_letters(Path(path).read_bytes())
        except (OSError, ValueError, ImportError) as e:
            messagebox.showerror("Could not use that picture", str(e), parent=self.win)
            return
        drawn = sum(1 for rows in loaded.drawn.values() if rows)
        self.notes = [f"Loaded {Path(path).name}: {drawn} letter(s) drawn; they go into the translation when you "
                      "save it."] + loaded.notes
        self.marks()
        self.show()

    def clear_letter(self) -> None:
        shape, tr = self.picked(), self.app.tr
        if shape is None or tr is None:
            return
        tr.set_letter(shape.char, None)
        self.app.dirty = True
        self.app.fonts = {}
        if self.app.key is not None:
            self.app.check_now()
        self.notes = [f"{shape.char} is a question mark again."]
        self.marks()
        self.show()

    def close(self) -> None:
        self.app.letters_window = None
        self.win.destroy()
