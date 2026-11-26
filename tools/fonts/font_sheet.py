"""Draw a TrueType/OpenType font into the F1 > Font picture layout (mods/fonts/README.txt).

  python tools/fonts/font_sheet.py FONT SIZE THRESHOLD OUT.png        (needs Pillow)

Each letter is drawn at SIZE pixels on a common baseline, kept where its coverage is at least
THRESHOLD (0-255), in white with a dark shadow one pixel right and down, in a 12 x 12 cell. The
shipped pictures were made with:
  Departure_Mono.png  DepartureMono-Regular.otf 1.500   11  115
  Plain_Sans.png      IBMPlexSans-Regular.ttf          11   80  (src/main/boot/fonts/ibm_plex_sans.ttf.inc)
"""
import sys
from PIL import Image, ImageDraw, ImageFont

CHARS = '0123456789+-=*% ABCDEFGHIJKLMNOPQRSTUVWXYZ&!?"\'.abcdefghijklmnopqrstuvwxyz:,/'


def main(path, size, threshold, out):
    assert len(CHARS) == 77
    font = ImageFont.truetype(path, size)
    bounds = [font.getbbox(c, anchor="ls") for c in CHARS if c != " "]
    top, bottom = min(b[1] for b in bounds), max(b[3] for b in bounds)
    if bottom - top > 12:
        sys.exit(f"{path} at {size} px is {bottom - top} px tall; cells hold 12")
    sheet = Image.new("RGBA", (252, 48))
    for i, c in enumerate(CHARS):
        if c == " ":
            continue
        mask = Image.new("L", (20, 16))
        ImageDraw.Draw(mask).text((0, -top), c, font=font, anchor="ls", fill=255)
        mask = mask.point(lambda p: 255 if p >= threshold else 0)
        box = mask.getbbox()
        if not box or box[2] > 11 or box[3] > 12:
            sys.exit(f"{c!r} does not fit its cell: {box}")
        cell, shadow, ink = Image.new("RGBA", (12, 12)), Image.new("L", (12, 12)), Image.new("L", (12, 12))
        shadow.paste(mask, (1, 1)); ink.paste(mask, (0, 0))
        cell.paste((35, 35, 35, 255), (0, 0), shadow); cell.paste((255, 255, 255, 255), (0, 0), ink)
        sheet.paste(cell, ((i % 21) * 12, (i // 21) * 12))
    sheet.save(out)


if __name__ == "__main__":
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    main(sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4])
