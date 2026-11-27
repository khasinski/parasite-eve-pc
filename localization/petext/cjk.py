"""Pack-local CJK glyph dictionary. Fonts remain external; packs embed 12x12 pixels only.

Uses Pillow for rasterization and sfnt cmap 4/12 for coverage (never accept a tofu glyph).
No installed-font discovery: translators explicitly choose fonts, in locale priority order.

A fan patch's own 12x12 letter pictures can stand in for a font (`patch_pictures`): each picture
named with its character (localization/name_ja_patch_letters.py) goes into the dictionary as the
patch drew it, so the pack draws the translator's letters and needs no font file.
"""
import csv
import json
import struct
from bisect import bisect_left, bisect_right
from pathlib import Path

TOKEN = b'\xf1\xc5'
RECORD = 76
MAX_GLYPHS = 240 * 240

def supported(cp):
    return any(a <= cp <= b for a,b in ((0x1100,0x11ff),(0x2010,0x2027),(0x2e80,0x303f),
        (0x3040,0x30ff),(0x3100,0x318f),(0x31a0,0x31ff),(0x3400,0x4dbf),
        (0x4e00,0x9fff),(0xa960,0xa97f),(0xac00,0xd7af),(0xd7b0,0xd7ff),
        (0xf900,0xfaff),(0xff00,0xffef),(0x20000,0x323af)))

def token(index):
    if not 0 <= index < MAX_GLYPHS: raise ValueError('too many CJK glyphs')
    return TOKEN + bytes(divmod(index,240))

def index(raw):
    return raw[2]*240+raw[3]

def _coverage(path, wanted):
    """Unicode coverage of face zero of a TTF/OTF/TTC. Bounds use struct's checked reads."""
    data=Path(path).read_bytes()
    u16=lambda at: struct.unpack_from('>H',data,at)[0]
    u32=lambda at: struct.unpack_from('>I',data,at)[0]
    base=u32(12) if data[:4]==b'ttcf' else 0
    cmap=None
    for n in range(u16(base+4)):
        p=base+12+16*n
        if data[p:p+4]==b'cmap': cmap=u32(p+8)
    if cmap is None: raise ValueError(f'{path}: no cmap')
    found=set()
    for n in range(u16(cmap+2)):
        p=cmap+4+8*n; platform,encoding=u16(p),u16(p+2)
        if platform!=0 and not (platform==3 and encoding in (1,10)): continue
        t=cmap+u32(p+4); fmt=u16(t)
        if fmt==12:
            groups=[struct.unpack_from('>III',data,t+16+12*k) for k in range(u32(t+12))]
            starts=[g[0] for g in groups]
            for cp in wanted:
                k=bisect_right(starts,cp)-1
                if k>=0:
                    start,end,glyph=groups[k]
                    if cp<=end and glyph+cp-start: found.add(cp)
        elif fmt==4:
            count=u16(t+6)//2; ends=t+14; starts=ends+2*count+2
            deltas=starts+2*count; offsets=deltas+2*count
            end_codes=[u16(ends+2*k) for k in range(count)]
            for cp in wanted:
                if cp>0xffff: continue
                k=bisect_left(end_codes,cp)
                if k<count and u16(starts+2*k)<=cp:
                    off=u16(offsets+2*k); delta=u16(deltas+2*k)
                    glyph=u16(offsets+2*k+off+2*(cp-u16(starts+2*k))) if off else cp
                    if glyph and (glyph+delta)&0xffff: found.add(cp)
    return found

SPACE = '　'
PIXEL = {' ': 0, '#': 1, '+': 2}    # a picture row: empty, stroke, shadow (the record's values)


def picture_pixels(rows):
    """A 12x12 picture (12 strings of ' ', '#', '+') as the record's 144 values."""
    if len(rows) != 12 or any(len(r) != 12 for r in rows):
        raise ValueError('a letter picture is 12 rows of 12')
    try:
        return [PIXEL[c] for r in rows for c in r]
    except KeyError as e:
        raise ValueError(f'a letter picture holds {e.args[0]!r}; only " ", "#" and "+"') from None


def patch_pictures(names_path, picture_paths):
    """The letters of a fan patch, from its named pictures: ({character: 144 values}, notes).

    `names_path` is the letters table name_ja_patch_letters.py writes (TSV: picture, char, how,
    ..., uses); `picture_paths` the pictures files import_ja_patch.py writes ({"pictures": {id:
    {"rows", "uses"}}}). Pictures without a name are left out. Only characters the dictionary holds
    are kept: the Latin letters, digits and signs come from the game's own font, as in any pack.
    When a character has more than one picture (the patch drew it twice, or once in the Japanese
    font) the most used one is taken, and the notes say which. The full-width space is drawn empty.
    """
    pictures = {}
    for path in picture_paths:
        for pid, p in json.loads(Path(path).read_text(encoding='utf-8'))['pictures'].items():
            if pid in pictures:
                pictures[pid]['uses'] = pictures[pid].get('uses', 0) + p.get('uses', 0)
            else:
                pictures[pid] = dict(p)
    by_char = {}
    with open(names_path, encoding='utf-8', newline='') as f:
        for r in csv.DictReader(f, delimiter='\t'):
            ch = (r.get('char') or '').replace('\\u3000', SPACE)
            if len(ch) != 1 or r.get('how') == 'review' or not supported(ord(ch)):
                continue
            if r['picture'] not in pictures:
                raise ValueError(f'{names_path}: picture {r["picture"]} is in no pictures file')
            by_char.setdefault(ch, []).append(r['picture'])
    letters, notes = {}, []
    for ch, pids in by_char.items():
        pids.sort(key=lambda pid: (-pictures[pid].get('uses', 0), pid))
        letters[ch] = [0] * 144 if ch == SPACE else picture_pixels(pictures[pids[0]]['rows'])
        if len(pids) > 1:
            notes.append(f'{ch} (U+{ord(ch):04X}) has {len(pids)} pictures: took {pids[0]} '
                         f'({pictures[pids[0]].get("uses", 0)} uses), not '
                         + ', '.join(f'{pid} ({pictures[pid].get("uses", 0)})' for pid in pids[1:]))
    return letters, notes


def _record(ch, pixels):
    return struct.pack('<I', ord(ch)) + bytes(pixels[i] | pixels[i+1] << 4 for i in range(0, 144, 2))


class Atlas:
    def __init__(self,texts,fonts,skip=(),pictures=None):
        """`pictures` ({character: 144 values}, patch_pictures) are taken as they are; the other
        characters are drawn from `fonts`."""
        self.chars=sorted({ch for text in texts for ch in text if supported(ord(ch)) and ch not in skip},key=ord)
        self.letters=None   # the pack's letter table, petext/letters.py
        self.codes={ch:token(i) for i,ch in enumerate(self.chars)}
        self.characters={token(i):ch for i,ch in enumerate(self.chars)}
        self.records=bytearray()
        if not self.chars: return
        if len(self.chars)>MAX_GLYPHS: raise ValueError('CJK glyph dictionary exceeds 57600 entries')
        pictures=pictures or {}
        drawn=[ch for ch in self.chars if ch not in pictures]
        if drawn and not fonts:
            if pictures:
                raise ValueError(f'{len(drawn)} CJK character(s) have no letter picture and no --cjk-font '
                                 f'was given: {"".join(drawn[:20])}')
            raise ValueError('CJK text needs --cjk-font PATH (repeat for fallback fonts)')
        if drawn:
            from PIL import Image,ImageDraw,ImageFont
            wanted={ord(c) for c in drawn}
            faces=[(_coverage(p,wanted),ImageFont.truetype(str(p),11,index=0)) for p in fonts]
        for ch in self.chars:
            if ch in pictures:
                self.records+=_record(ch,pictures[ch])
                continue
            font=next((font for coverage,font in faces if ord(ch) in coverage),None)
            if font is None: raise ValueError(f'no supplied font contains {ch!r} U+{ord(ch):04X}')
            image=Image.new('L',(11,11))
            if ch!='\u3000':
                # A common em box keeps punctuation at the baseline, rather than centering
                # each ink rectangle (which makes Japanese full stops float mid-line).
                box=font.getbbox(ch,anchor='ls')
                if box is None: raise ValueError(f'no outline for {ch!r}')
                left,top=min(0,box[0]),min(-10,box[1])
                right,bottom=max(11,box[2]),max(2,box[3])
                full=Image.new('L',(right-left,bottom-top))
                ImageDraw.Draw(full).text((-left,-top),ch,font=font,fill=255,anchor='ls')
                image=full.resize((11,11),Image.Resampling.LANCZOS)
            threshold = max(1, min(80, max(image.getdata()) // 2))
            pixels=[0]*144
            for y in range(11):
                for x in range(11):
                    if image.getpixel((x,y))>=threshold:
                        pixels[(y+1)*12+x+1]=2
            for y in range(11):
                for x in range(11):
                    if image.getpixel((x,y))>=threshold: pixels[y*12+x]=1
            if ch!='\u3000' and 1 not in pixels: raise ValueError(f'empty raster for {ch!r}')
            self.records+=_record(ch,pixels)
