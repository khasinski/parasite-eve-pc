"""Draw the Latin Extended set as pe_lang will compose it, from the game font on the player's disc.

  python -I preview_latin.py US_DISC1.cue [-o out/latin.png] [--words "Café Größe Żółć"]

Writes a picture (git-ignored under out/): every glyph of petext/latin.py enlarged, with the
character it stands for, and sample words set with the dialogue box's spacing.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))
from PIL import Image, ImageDraw, ImageFont  # noqa: E402

from petext import font, gametext as gt, latin  # noqa: E402
from petext.disc import Game  # noqa: E402

COLOURS = {0: (24, 26, 40), 1: (246, 246, 246), 2: (70, 70, 80)}
WORDS = "Café crème, garçon! Größe Straße. ¿Señor? ¡Olé! Żółć gęślą. Příliš žluťoučký. Ağaç şık. «Œuvre» 5€"


def glyph_cells(disc_path: str) -> dict[int, list[int]]:
    g = Game(disc_path)
    pal, w, h, px = font.page(g, font.PAGE0_ENTRY)
    cells = {c: list(font.cell(px, w, c)) for c in range(77)}
    for gl in latin.GLYPHS:
        cells[gl.code] = latin.compose(gl, cells[gt.CODE_OF[gl.base]] if gl.base else None)
    return cells


def paste(img: Image.Image, cell: list[int], x: int, y: int, scale: int) -> None:
    for i, v in enumerate(cell):
        if v:
            cx, cy = x + (i % 12) * scale, y + (i // 12) * scale
            img.paste(COLOURS[v], (cx, cy, cx + scale, cy + scale))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("disc")
    ap.add_argument("-o", "--out", default=str(ROOT / "out" / "latin.png"))
    ap.add_argument("--words", default=WORDS)
    a = ap.parse_args()
    cells = glyph_cells(a.disc)
    cols, scale, pitch = 16, 4, 64
    rows = (len(latin.GLYPHS) + cols - 1) // cols
    raw = gt.encode(a.words, extended=True)
    width = max(cols * pitch, sum(gt.lines(raw)) * 2 + 40)
    img = Image.new("RGB", (width, rows * (pitch + 20) + 120), COLOURS[0])
    d = ImageDraw.Draw(img)
    label = ImageFont.truetype(r"C:\Windows\Fonts\arial.ttf", 13) if Path(r"C:\Windows\Fonts\arial.ttf").exists() else None
    for i, gl in enumerate(latin.GLYPHS):
        x, y = (i % cols) * pitch, (i // cols) * (pitch + 20)
        paste(img, cells[gl.code], x + 6, y + 4, scale)
        d.text((x + 6, y + 50), f"{gl.char} {gl.code:02X}", fill=(240, 200, 90), font=label)
    # The words at 2x, advancing like the dialogue box.
    x, y = 20, rows * (pitch + 20) + 30
    for c in raw:
        adv = gt.BOX_ADVANCE[c] if c < len(gt.BOX_ADVANCE) else gt._latin_advance(c) or gt.WIDE
        paste(img, cells.get(c, [0] * 144), x, y, 2)
        x += adv * 2
    Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    img.save(a.out)
    print(f"wrote {a.out}: {len(latin.GLYPHS)} glyphs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
