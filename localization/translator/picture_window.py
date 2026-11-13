"""The Translator's Pictures window: the pictures with words, the game's and the translation's.

Save one as a PNG, redraw it in any paint program, load it back. The pictures come from the
translator's own disc (Disc 1 or Disc 2: their pictures are the same); a redrawn one goes into the
translation's pictures.bin when the translation is saved. All the work is in artwork.py.
"""
from __future__ import annotations

import base64
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

from . import artwork as A

ZOOM = 3                                     # the game's pixels up to three times the size
ROOM_AROUND = (420, 300)                     # the window's room taken by the list, labels, tip and buttons
DISC_TYPES = [("Disc image", "*.cue *.bin *.iso *.img"), ("All files", "*.*")]
PNG_TYPES = [("PNG picture", "*.png"), ("All files", "*.*")]


def _colour_name(tint: tuple[int, int, int]) -> str:
    on = tuple(t > 0 for t in tint)
    return {(True, True, True): "as drawn", (True, False, False): "red", (False, True, False): "green",
            (True, True, False): "yellow", (True, False, True): "pink"}.get(on, "tinted")


def _tint_list(tints) -> str:
    """"as drawn (words and marks); red (Miss); green, yellow, pink (damage numbers)": neighbours drawn
    for the same thing together."""
    groups: list[tuple[str, list[str]]] = []
    for what, tint in tints:
        if groups and groups[-1][0] == what:
            groups[-1][1].append(_colour_name(tint))
        else:
            groups.append((what, [_colour_name(tint)]))
    return "; ".join(f"{', '.join(names)} ({what})" for what, names in groups)


class PictureWindow:
    def __init__(self, app) -> None:
        self.app = app
        self.win = tk.Toplevel(app.root)
        self.win.title("Pictures with words — Parasite Eve Translator")
        self.win.transient(app.root)
        self.size = (min(1060, app.root.winfo_screenwidth() - 60), min(900, app.root.winfo_screenheight() - 100))
        self.win.geometry(f"{self.size[0]}x{self.size[1]}")
        self.zoom = ZOOM
        self.win.protocol("WM_DELETE_WINDOW", self.close)
        self.photos: list[tk.PhotoImage] = []         # Tk shows a picture only while Python keeps it
        self.notes: list[str] = []

        bar = ttk.Frame(self.win, padding=6)
        bar.pack(fill="x")
        ttk.Button(bar, text="Choose disc…", command=self.ask_disc).pack(side="left")
        self.disc_label = ttk.Label(bar, text="")
        self.disc_label.pack(side="left", padx=(8, 0))

        panes = ttk.PanedWindow(self.win, orient="horizontal")
        panes.pack(fill="both", expand=True)
        left = ttk.Frame(panes, padding=(6, 0, 0, 6))
        self.list = ttk.Treeview(left, columns=("mark", "picture"), show="headings", selectmode="browse")
        self.list.heading("mark", text="")
        self.list.heading("picture", text="Picture", anchor="w")
        self.list.column("mark", width=80, stretch=False)
        self.list.column("picture", width=300, stretch=True)
        self.list.pack(fill="both", expand=True)
        for pic in A.PICTURES:
            self.list.insert("", "end", iid=pic.id, values=("", pic.label))
        self.list.bind("<<TreeviewSelect>>", lambda e: self.show())
        self.win.bind("<Configure>", self._resized)
        panes.add(left, weight=1)

        right = ttk.Frame(panes, padding=(10, 0, 10, 6))
        ttk.Label(right, text="The game's picture").pack(anchor="w")
        self.english_view = ttk.Label(right, background="#000000")
        self.english_view.pack(anchor="w", pady=(2, 8))
        ttk.Label(right, text="Your picture").pack(anchor="w")
        self.your_view = ttk.Label(right, background="#000000")
        self.your_view.pack(anchor="w", pady=(2, 8))
        # Only for a picture the game tints as it draws it (the battle words): yours in those colours.
        self.fight_label = ttk.Label(right, text="", wraplength=560, justify="left")
        self.fight_view = ttk.Label(right, background="#000000")
        self.tip = ttk.Label(right, text="", wraplength=560, justify="left")
        self.tip.pack(anchor="w")
        buttons = ttk.Frame(right)
        buttons.pack(anchor="w", pady=(8, 0))
        self.save_png = ttk.Button(buttons, text="Save as PNG…", command=self.ask_save_png)
        self.save_png.pack(side="left")
        self.load_png = ttk.Button(buttons, text="Load PNG…", command=self.ask_load_png)
        self.load_png.pack(side="left", padx=(6, 0))
        self.undo = ttk.Button(buttons, text="Use the game's picture", command=self.use_english)
        self.undo.pack(side="left", padx=(6, 0))
        self.message = ttk.Label(right, text="", wraplength=560, justify="left", foreground="#8a5a00")
        self.message.pack(anchor="w", pady=(8, 0))
        panes.add(right, weight=2)

        self.list.selection_set(A.PICTURES[0].id)
        self.refresh()

    # ---- what is shown ----

    def picked(self) -> A.Picture:
        sel = self.list.selection()
        return A.BY_ID[sel[0]] if sel else A.PICTURES[0]

    def refresh(self) -> None:
        disc, tr = self.app.picture_disc, self.app.tr
        if disc is None:
            self.disc_label.configure(text=self.app.disc_error or "Choose your Parasite Eve (USA) disc: Disc 1 or "
                                      "Disc 2. The pictures are read from it; nothing of it is put anywhere else.")
        else:
            self.disc_label.configure(text=f"Disc: {disc.path.name}")
        for pic in A.PICTURES:
            mark = ""
            if disc is not None and tr is not None:
                mark = "redrawn" if A.redrawn(disc, pic, A.read_pieces(tr.parts)) else "the game's"
            self.list.set(pic.id, "mark", mark)
        self.show()

    def zoom_for(self, w: int, h: int, copies: int = 2) -> int:
        """The largest zoom (1 to ZOOM) at which the game's and the translation's pictures (and its tinted
        copies), one above the other, fit the window beside the list (the map's names are 256×143: 2x in
        the usual window)."""
        shown = self.win.winfo_width() > 1             # before it is first drawn, the size it asked for
        width = (self.win.winfo_width() if shown else self.size[0]) - ROOM_AROUND[0]
        height = (self.win.winfo_height() if shown else self.size[1]) - ROOM_AROUND[1]
        return max(1, min(ZOOM, width // w, height // (copies * h)))

    def layout(self, pic):
        """Where a card's tiles are (read from the disc); None for a picture that is one TIM."""
        return getattr(self.app.picture_disc, "layout", {}).get(pic.id)

    @staticmethod
    def copies(pic: A.Picture) -> int:
        return 2 + len(pic.tints)

    def _resized(self, event) -> None:
        """The window made larger or smaller: the pictures at the zoom that fits it now."""
        disc = self.app.picture_disc
        if event.widget is self.win and disc is not None:
            pic = self.picked()
            if self.zoom_for(*A.size(pic, disc.english[pic.id]), self.copies(pic)) != self.zoom:
                self.show()

    def _photo(self, png: bytes) -> tk.PhotoImage:
        photo = tk.PhotoImage(master=self.win, data=base64.b64encode(png).decode("ascii"), format="png")
        photo = photo.zoom(self.zoom)
        self.photos.append(photo)
        return photo

    def show(self) -> None:
        pic, disc, tr = self.picked(), self.app.picture_disc, self.app.tr
        self.photos = []
        english = disc.english[pic.id] if disc else None
        ready = disc is not None and tr is not None
        self.fight_label.pack_forget()
        self.fight_view.pack_forget()
        if english is None:
            self.english_view.configure(image="", text="(no disc)")
            self.your_view.configure(image="", text="")
        else:
            w, h = A.size(pic, english)
            layout = self.layout(pic)
            self.zoom = self.zoom_for(w, h, self.copies(pic))
            self.tip.configure(text=pic.tip.format(w=w, h=h))
            self.english_view.configure(image=self._photo(A.png(pic, english, layout)), text="")
            yours = english
            if tr is not None:
                yours = A.shown(disc, pic, A.read_pieces(tr.parts))
                self.your_view.configure(image=self._photo(A.png(pic, yours, layout)), text="")
            if pic.tints:
                self.fight_label.configure(text=("Your picture" if tr is not None else "The game's picture")
                                           + " in a fight, in the colours the game draws it with: "
                                           + _tint_list(pic.tints))
                self.fight_label.pack(anchor="w", before=self.tip)
                self.fight_view.configure(image=self._photo(A.tinted_png(pic, yours)), text="")
                self.fight_view.pack(anchor="w", pady=(2, 8), before=self.tip)
        locked = tr is None or bool(tr.locked)
        self.save_png.configure(state="normal" if ready else "disabled")
        self.load_png.configure(state="normal" if ready and not locked else "disabled")
        self.undo.configure(state="normal" if ready and not locked and A.redrawn(disc, pic, A.read_pieces(tr.parts))
                            else "disabled")
        self.message.configure(text=" ".join(self.notes))

    # ---- actions ----

    def ask_disc(self) -> None:
        path = filedialog.askopenfilename(parent=self.win, title="Choose your Parasite Eve (USA) disc",
                                          filetypes=DISC_TYPES)
        if path:
            error = self.app.use_disc(path)
            if error:
                messagebox.showerror("Not the disc", error, parent=self.win)
            self.notes = []
            self.refresh()

    def ask_save_png(self) -> None:
        pic, disc, tr = self.picked(), self.app.picture_disc, self.app.tr
        path = filedialog.asksaveasfilename(parent=self.win, title="Save the picture as", defaultextension=".png",
                                            filetypes=PNG_TYPES, initialfile=pic.id.replace("/", " - ") + ".png")
        if not path:
            return
        try:
            Path(path).write_bytes(A.png(pic, A.shown(disc, pic, A.read_pieces(tr.parts)), self.layout(pic)))
        except OSError as e:
            messagebox.showerror("Could not save the picture", str(e), parent=self.win)
            return
        self.notes = [f"Saved {Path(path).name}. Redraw it in your paint program, then Load PNG…"]
        self.show()

    def ask_load_png(self) -> None:
        path = filedialog.askopenfilename(parent=self.win, title="Load your picture", filetypes=PNG_TYPES)
        if not path:
            return
        try:
            loaded = self.app.load_picture(self.picked(), Path(path).read_bytes(), Path(path).name)
        except (OSError, ValueError) as e:
            messagebox.showerror("Could not use that picture", str(e), parent=self.win)
            return
        self.notes = [f"Loaded {Path(path).name}; it goes into the translation when you save it."] + loaded.notes
        self.refresh()

    def use_english(self) -> None:
        self.app.load_picture(self.picked(), None)
        self.notes = ["Back to the game's picture."]
        self.refresh()

    def close(self) -> None:
        self.app.pictures_window = None
        self.win.destroy()
