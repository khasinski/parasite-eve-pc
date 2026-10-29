"""Extract original UI materials and build a faithful 4x lossless WebP pack.

Requires a user's USA disc BIN, a raw 1024x512 RGB555 VRAM capture, Pillow and
NumPy. No game data is included. Source pixels, dimensions, palette and PS1 STP
class determine keys identically to PsyZ; unrelated VRAM never affects a key.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import re
import shutil
import subprocess
import tempfile

import numpy as np
from PIL import Image

FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211


def material(vram, page, clut, x, y, width, height):
    mode = (page >> 7) & 3
    if mode > 1 or not 0 < width <= 256 or not 0 < height <= 256:
        raise ValueError('Invalid indexed UI region')
    px = (np.arange(width) + x) & 255
    py = (np.arange(height) + y) & 255
    words = vram[((page >> 4) & 1)*256 + py[:, None],
                 (((page & 15)*64 + (px >> (1 if mode else 2))) & 1023)[None, :]]
    indices = (words >> ((px & (1 if mode else 3))*(8 if mode else 4))) & (255 if mode else 15)
    colors = vram[(clut >> 6) & 511, ((clut & 63)*16 + indices) & 1023]
    h = FNV_OFFSET
    for word in [mode, width, height, *colors.flat]:
        n = int(word)
        h = ((h ^ (n & 255))*FNV_PRIME) & ((1 << 64)-1)
        h = ((h ^ (n >> 8))*FNV_PRIME) & ((1 << 64)-1)
    rgba = np.empty((height, width, 4), dtype=np.uint8)
    for channel in range(3):
        rgba[:, :, channel] = ((((colors >> (channel*5)) & 31).astype(np.uint32)*255+15)//31).astype(np.uint8)
    rgba[:, :, 3] = np.where(colors != 0, 255, 0)
    return f'{h:016x}', Image.fromarray(rgba)


def scale2x(image):
    """Edge-directed pixel reconstruction; never invent letters or labels."""
    a = np.asarray(image.convert('RGBA'))
    packed = a.copy().view(np.uint32).reshape(a.shape[:2])
    p = np.pad(packed, 1, mode='edge')
    b, d, e, f, h = p[:-2, 1:-1], p[1:-1, :-2], p[1:-1, 1:-1], p[1:-1, 2:], p[2:, 1:-1]
    eligible = (b != h) & (d != f)
    out = np.empty((a.shape[0]*2, a.shape[1]*2), dtype=np.uint32)
    out[0::2, 0::2] = np.where(eligible & (d == b), d, e)
    out[0::2, 1::2] = np.where(eligible & (b == f), f, e)
    out[1::2, 0::2] = np.where(eligible & (d == h), d, e)
    out[1::2, 1::2] = np.where(eligible & (h == f), f, e)
    return Image.fromarray(out.view(np.uint8).reshape(out.shape+(4,)))


def restore_font(image):
    """Trace monochrome original glyph outlines, retaining their cell metrics."""
    rgba=np.asarray(image.convert('RGBA'))
    opaque=rgba[:,:,3]>0
    colors=np.unique(rgba[:,:,:3][opaque],axis=0)
    if image.height not in (12,13) or image.width>16 or len(colors)>4 or len(colors)==0 or not np.all(colors[:,0:1]==colors):
        return None
    if not shutil.which('potrace') or not shutil.which('rsvg-convert'):
        raise RuntimeError('Install potrace and librsvg to trace the original fonts')
    with tempfile.TemporaryDirectory(prefix='pe-font-') as directory:
        root=Path(directory)
        result=Image.new('RGBA',(image.width*4,image.height*4))
        for color in sorted(colors,key=lambda c:int(c[0])):
            layer=opaque & (rgba[:,:,0]>=color[0])
            Image.fromarray(np.where(layer,0,255).astype(np.uint8)).convert('1').resize((image.width*2,image.height*2),Image.Resampling.NEAREST).save(root/'glyph.pbm')
            subprocess.run(['potrace',str(root/'glyph.pbm'),'-s','-t','0','-a','0.8','-O','0.1','-o',str(root/'glyph.svg')],check=True)
            subprocess.run(['rsvg-convert','-w',str(image.width*4),'-h',str(image.height*4),'-o',str(root/'glyph.png'),str(root/'glyph.svg')],check=True)
            traced=np.array(Image.open(root/'glyph.png').convert('RGBA'))
            traced[:,:,:3]=color
            result.alpha_composite(Image.fromarray(traced))
        return result



def restore_panel_label(image):
    """Trace the original gold lettering; retain its authored panel and layout."""
    import cv2
    rgba=np.array(image.convert('RGBA'))
    mask=(rgba[:,:,0]>90)&(rgba[:,:,1]>90)&(rgba[:,:,2]<np.minimum(rgba[:,:,0],rgba[:,:,1])*0.6)&(rgba[:,:,3]>0)
    if np.count_nonzero(mask)<4:return None
    background=cv2.inpaint(rgba[:,:,:3],(mask*255).astype(np.uint8),2,cv2.INPAINT_TELEA)
    result=Image.fromarray(background).resize((image.width*4,image.height*4),Image.Resampling.BICUBIC).convert('RGBA')
    result.putalpha(image.getchannel('A').resize(result.size,Image.Resampling.NEAREST))
    with tempfile.TemporaryDirectory(prefix='pe-label-') as directory:
        root=Path(directory)
        Image.fromarray(np.where(mask,0,255).astype(np.uint8)).convert('1').resize((image.width*2,image.height*2),Image.Resampling.NEAREST).save(root/'label.pbm')
        subprocess.run(['potrace',str(root/'label.pbm'),'-s','-t','0','-a','0.8','-O','0.1','-o',str(root/'label.svg')],check=True)
        subprocess.run(['rsvg-convert','-w',str(image.width*4),'-h',str(image.height*4),'-o',str(root/'label.png'),str(root/'label.svg')],check=True)
        traced=np.array(Image.open(root/'label.png').convert('RGBA'))
        traced[:,:,:3]=np.median(rgba[:,:,:3][mask],axis=0)
        result.alpha_composite(Image.fromarray(traced))
    return result


class Disc:
    def __init__(self, path):
        self.file = open(path, 'rb')
        root = self.read(16, 2048)[156:]
        self.root = struct.unpack_from('<I', root, 2)[0], struct.unpack_from('<I', root, 10)[0]

    def read(self, sector, size):
        out = bytearray()
        while len(out) < size:
            self.file.seek(sector*2352+24)
            chunk = self.file.read(2048)
            if len(chunk) != 2048:
                raise ValueError('Truncated BIN')
            out.extend(chunk)
            sector += 1
        return bytes(out[:size])

    def find(self, name):
        data = self.read(*self.root)
        offset = 0
        while offset < len(data):
            length = data[offset]
            if not length:
                offset = (offset//2048+1)*2048
                continue
            record = data[offset:offset+length]
            if record[33:33+record[32]].split(b';')[0].decode('ascii', 'replace') == name:
                return struct.unpack_from('<I', record, 2)[0], struct.unpack_from('<I', record, 10)[0]
            offset += length
        raise ValueError(name+' missing from disc')


def extract(disc_path, vram_path, output):
    output.mkdir(parents=True, exist_ok=True)
    disc = Disc(disc_path)
    exe = disc.read(*disc.find('SLUS_006.62'))
    base = struct.unpack_from('<I', exe, 0x18)[0]
    sectors = struct.unpack_from('<81H', exe, 0x800+0x800930d8-base)
    start, _ = disc.find('PE.IMG')
    resident = disc.read(start+sectors[2], (sectors[3]-sectors[2])*2048)
    directory = struct.unpack_from('<12I', resident)
    descriptors = directory[2]
    descriptor_end = min(x for x in directory if x > descriptors)
    raw = vram_path.read_bytes()
    if len(raw) != 1024*512*2:
        raise ValueError('VRAM capture must contain 1024x512 16-bit words')
    vram = np.frombuffer(raw, dtype='<u2').reshape(512, 1024)
    records = []

    def emit(kind, identifier, page, clut, x, y, w, h):
        if not 0 < w <= 256 or not 0 < h <= 256:
            return
        key, image = material(vram, page, clut, x, y, w, h)
        if not image.getchannel('A').getbbox():
            return
        image.save(output/(key+'.png'))
        records.append(dict(kind=kind, id=identifier, key=key, page=page, clut=clut,
                            rect=[x,y,w,h], source_sha256=hashlib.sha256(image.tobytes()).hexdigest()))

    for index, at in enumerate(range(descriptors, descriptor_end, 8)):
        u,v,clut,w,h,mode,_ = struct.unpack_from('<BBHBBBB', resident, at)
        if mode > 1:
            continue
        emit('menu-sprite', index, (mode << 7)|7, clut, u,v,w,h)
        # Textured polygon paths include their final authored UV coordinate.
        emit('menu-quad', index, 7, clut, u,v,w+1,h+1)
    for glyph in range(441):
        metric = resident[directory[10]+glyph]
        emit('menu-font',glyph,5,0x89c,(glyph%21)*12+(metric&15),(glyph//21)*12,(metric>>4)+1,13)
    for page,clut in [(5,(252<<6)|20),(6,(252<<6)|21)]:
        for glyph in range(78):
            emit('dialogue-font',glyph,page,clut,(glyph%21)*12,(glyph//21)*12,12,12)
    for clut,base_u in [(0x395d,0x84),(0x3a1c,0xcc)]:
        for digit in range(10):
            emit('digits',digit,7,clut,base_u+digit*5,0xa4,6,8)
    # The always-visible HP display and battle HUD share the original 4-bit bank.
    hud_page=20;hud_clut=(504<<6)|19
    for digit in range(11):
        for v,w,h in [(0xe0,8,8),(0xe8,6,10),(0xf2,6,6)]:
            emit('hud-digit',digit,hud_page,hud_clut,digit*8,v,w,h)
    symbols=[(0x50,0xf4,8,4),(0x58,0xf4,8,4),(0x60,0xf4,8,4),
        (0xc8,0xe0,4,8),(0x58,0xef,36,5),(0x7c,0xef,36,5),(0x50,0xe0,24,8),
        (0x68,0xf4,24,4),(0,0xf8,40,7),(0xd4,0xf0,20,7),(0x90,0xe8,64,7),
        (0x28,0xf8,40,7),(0xb4,0xef,28,7),(0x50,0xf8,48,7),(0x58,0xe8,56,7)]
    for index,rect in enumerate(symbols):emit('hud-symbol',index,hud_page,hud_clut,*rect)
    for i,(u,w,palette_y) in enumerate([(0xd4,4,506),(0xd8,4,507),(0xdc,4,508),(0xe0,8,509)]):
        emit('hud-action',i,hud_page,(palette_y<<6)|19,u,0xe0,w,16)
    # The title menu composites its original RGB24 resources on the CPU.
    # Header width is in 16-bit VRAM words, not RGB pixels.
    bank=disc.read(start+sectors[70],(sectors[71]-sectors[70])*2048)[0x4264:]
    for index in range(1,8):
        at=struct.unpack_from('<I',bank,4+index*4)[0]
        words,height=struct.unpack_from('<HH',bank,at+16)
        width=words*2//3
        pixels=bank[at+20:at+20+width*height*3]
        if not 0<width<=320 or not 0<height<=240 or len(pixels)!=width*height*3:
            raise ValueError('Invalid title UI resource')
        hash_value=FNV_OFFSET
        for word in [width,height]:
            for byte in (word&255,word>>8):hash_value=((hash_value^byte)*FNV_PRIME)&((1<<64)-1)
        for byte in pixels:hash_value=((hash_value^byte)*FNV_PRIME)&((1<<64)-1)
        key=f'rgb-{hash_value:016x}'
        rgb=np.frombuffer(pixels,dtype=np.uint8).reshape(height,width,3)
        rgba=np.concatenate((rgb,np.where(np.any(rgb,axis=2),255,0).astype(np.uint8)[:,:,None]),axis=2)
        image=Image.fromarray(rgba);image.save(output/(key+'.png'))
        records.append(dict(kind='title-rgb',id=index,key=key,source_sha256=hashlib.sha256(image.tobytes()).hexdigest()))
    manifest = dict(format=1, vram_sha256=hashlib.sha256(raw).hexdigest(),
                    disc_executable_sha256=hashlib.sha256(exe).hexdigest(), materials=records)
    (output/'catalog.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(f'{len(records)} UI descriptors, {len(set(r["key"] for r in records))} unique materials')


def build(source, destination):
    if destination.exists():
        raise FileExistsError(destination)
    destination.mkdir(parents=True)
    records=[]
    catalog=json.loads((source/'catalog.json').read_text()) if (source/'catalog.json').exists() else {}
    panel_keys={r['key'] for r in catalog.get('materials',[]) if r['kind'] in ('menu-sprite','menu-quad') and r['id'] in (*range(141,149),151,152,153)}
    for file in sorted(source.glob('*.png')):
        key=file.stem[4:] if file.stem.startswith('rgb-') else file.stem
        if len(key)!=16 or any(c not in '0123456789abcdef' for c in key):
            continue
        image=Image.open(file).convert('RGBA')
        label=file.stem in panel_keys
        hd=restore_panel_label(image) if label else restore_font(image)
        traced=hd is not None
        if hd is None:hd=scale2x(scale2x(image))
        target=destination/('ui-'+file.stem+'.webp')
        hd.save(target,lossless=True,exact=True,method=6)
        with Image.open(target) as check:
            if check.convert('RGBA').tobytes()!=hd.tobytes():
                raise ValueError('Lossless roundtrip failed: '+file.name)
        records.append(dict(file=target.name,font_trace=traced and not label,panel_label_trace=traced and label,size=list(hd.size),sha256=hashlib.sha256(target.read_bytes()).hexdigest(),
                            original_sha256=hashlib.sha256(image.tobytes()).hexdigest()))
    (destination/'manifest.json').write_text(json.dumps(dict(format=1,method='original glyph outline tracing; edge-directed icon reconstruction',materials=records),indent=2)+'\n')
    print(f'{len(records)} materials, {sum(p.stat().st_size for p in destination.glob("*.webp"))} bytes')


def combine(base, interface, destination, version):
    """Build a separate verified artwork pack, keeping the working pack intact."""
    if destination.exists():raise FileExistsError(destination)
    manifest=json.loads((base/'manifest.json').read_text())
    ui=json.loads((interface/'manifest.json').read_text())
    files={}
    for row in manifest.get('atlases',[]):
        files[row['file']]=row['atlas_sha256']
        if row.get('reference_file'):files[row['reference_file']]=row['reference_sha256']
    for row in manifest.get('extras',[]):files[row['file']]=row['sha256']
    additions=[]
    for row in ui['materials']:
        name=row['file']
        if not name.startswith('ui-') or Path(name).name!=name or name in files:
            raise ValueError('Invalid/duplicate interface filename')
        files[name]=row['sha256'];additions.append(dict(file=name,kind='interface',sha256=row['sha256']))
    destination.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=destination.name+'.partial-',dir=destination.parent) as temporary:
        stage=Path(temporary)/'pack';stage.mkdir()
        ui_names={r['file'] for r in ui['materials']}
        for name,digest in files.items():
            if not (name in ('title.webp','title.png') or re.fullmatch(r'(?:ui-(?:rgb-)?)?[0-9a-f]{16}\.(?:webp|png)|[0-9a-f]{16}(?:\.ref\.png|-wide\.bin)',name)):
                raise ValueError('Unsafe pack filename')
            source=(interface if name in ui_names else base)/name
            if hashlib.sha256(source.read_bytes()).hexdigest()!=digest:raise ValueError('Bad checksum: '+name)
            # Independent copies: editing a preview must never mutate the old pack.
            shutil.copy2(source,stage/name)
        manifest.setdefault('extras',[]).extend(additions)
        manifest['version']=version
        manifest['interface']=dict(format=1,materials=len(additions),source_manifest_sha256=hashlib.sha256((interface/'manifest.json').read_bytes()).hexdigest())
        manifest['distribution_bytes']=sum(f.stat().st_size for f in stage.iterdir())
        (stage/'manifest.json').write_text(json.dumps(manifest,separators=(',',':'))+'\n')
        stage.rename(destination)
    print(f'Combined {len(additions)} UI materials into {destination}')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    commands=parser.add_subparsers(dest='command',required=True)
    e=commands.add_parser('extract');e.add_argument('--disc-bin',type=Path,required=True);e.add_argument('--vram',type=Path,required=True);e.add_argument('--output',type=Path,required=True)
    b=commands.add_parser('build');b.add_argument('source',type=Path);b.add_argument('destination',type=Path)
    c=commands.add_parser('combine');c.add_argument('base',type=Path);c.add_argument('interface',type=Path);c.add_argument('destination',type=Path);c.add_argument('--version',default='0.2-preview')
    args=parser.parse_args()
    if args.command=='extract':extract(args.disc_bin,args.vram,args.output)
    elif args.command=='build':build(args.source,args.destination)
    else:combine(args.base,args.interface,args.destination,args.version)
