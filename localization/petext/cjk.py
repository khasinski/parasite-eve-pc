"""Pack-local CJK glyph dictionary. Fonts remain external; packs embed 12x12 pixels only.

Uses Pillow for rasterization and sfnt cmap 4/12 for coverage (never accept a tofu glyph).
No installed-font discovery: translators explicitly choose fonts, in locale priority order.
"""
import struct
from bisect import bisect_left, bisect_right
from pathlib import Path

TOKEN = b'\xf1\xc5'
RECORD = 76
MAX_GLYPHS = 240 * 240

def supported(cp):
    return any(a <= cp <= b for a,b in ((0x1100,0x11ff),(0x2e80,0x303f),
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

class Atlas:
    def __init__(self,texts,fonts):
        self.chars=sorted({ch for text in texts for ch in text if supported(ord(ch))},key=ord)
        self.codes={ch:token(i) for i,ch in enumerate(self.chars)}
        self.characters={token(i):ch for i,ch in enumerate(self.chars)}
        self.records=bytearray()
        if not self.chars: return
        if not fonts: raise ValueError('CJK text needs --cjk-font PATH (repeat for fallback fonts)')
        if len(self.chars)>MAX_GLYPHS: raise ValueError('CJK glyph dictionary exceeds 57600 entries')
        from PIL import Image,ImageDraw,ImageFont
        wanted={ord(c) for c in self.chars}
        faces=[(_coverage(p,wanted),ImageFont.truetype(str(p),11,index=0)) for p in fonts]
        for ch in self.chars:
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
            self.records+=struct.pack('<I',ord(ch))+bytes(pixels[i]|pixels[i+1]<<4 for i in range(0,144,2))
