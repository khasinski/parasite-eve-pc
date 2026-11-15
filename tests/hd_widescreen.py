"""Optional real-disc regression for HD side tiles (no visible game windows).
PE_TEST_DISC and PE_TEST_HD_BACKGROUNDS must point to the reviewed local assets.
Usage: python tests/hd_widescreen.py /path/to/parasite-eve-pc
"""
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import struct
import sys
import tempfile
import zlib


def read_png_rgb(path):
    """Decode a non-interlaced 8-bit RGB PNG (the packs' reference planes)."""
    data=Path(path).read_bytes();at=8;idat=b'';header=None
    while at<len(data):
        size,kind=struct.unpack_from('>I4s',data,at);body=data[at+8:at+8+size];at+=12+size
        if kind==b'IHDR':header=struct.unpack('>IIBBBBB',body)
        elif kind==b'IDAT':idat+=body
    width,height,depth,colour,_,_,interlace=header
    assert depth==8 and colour==2 and not interlace,path
    raw=zlib.decompress(idat);stride=width*3;rows=[];previous=bytearray(stride)
    for y in range(height):
        kind=raw[y*(stride+1)];line=bytearray(raw[y*(stride+1)+1:(y+1)*(stride+1)])
        for x in range(stride):
            a=line[x-3] if x>=3 else 0;b=previous[x];c=previous[x-3] if x>=3 else 0
            if kind==1:line[x]=(line[x]+a)&255
            elif kind==2:line[x]=(line[x]+b)&255
            elif kind==3:line[x]=(line[x]+(a+b)//2)&255
            elif kind==4:
                p=a+b-c;pa,pb,pc=abs(p-a),abs(p-b),abs(p-c)
                line[x]=(line[x]+(a if pa<=pb and pa<=pc else b if pb<=pc else c))&255
        rows.append(bytes(line));previous=line
    return width,height,rows


def write_png_rgba(path,width,rows):
    chunk=lambda kind,body:struct.pack('>I',len(body))+kind+body+struct.pack('>I',zlib.crc32(kind+body))
    raw=b''.join(b'\0'+row for row in rows)
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,len(rows),8,6,0,0,0))+
                           chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b''))


def main(executable):
    disc, pack = os.getenv('PE_TEST_DISC'), os.getenv('PE_TEST_HD_BACKGROUNDS')
    if not disc or not pack:
        return 77
    pack = Path(pack).resolve()
    manifest = json.loads((pack/'manifest.json').read_text())
    if not any(r['room']=='M002' and r['group']==0 for r in manifest.get('widescreen', [])):
        return 77
    # Real fresh-game route to the first theatre exterior, held in a dialogue.
    # The stationary camera catches extensions incorrectly gated on camera updates.
    timeline = [(10000,8),(10010,0),(11000,16),(11010,0),(11500,16384),(11510,0)]
    for tick in (17000,17600,18200,18800,19400,20000,20600,21200,21800):
        timeline.extend(((tick,16384),(tick+10,0)))
    for tick, mask in ((23000,8),(23600,8),(24200,16384),(24800,16384)):
        timeline.extend(((tick,mask),(tick+10,0)))
    output = Path(tempfile.mkdtemp(prefix='pe-hd-wide-',dir=Path(executable).resolve().parent))
    baseline = output/'original-width-pack';baseline.mkdir()
    for source in pack.iterdir():
        if source.is_file() and not source.name.endswith('-wide.bin'):
            try: os.link(source,baseline/source.name)
            except OSError: shutil.copyfile(source,baseline/source.name)

    broken=output/'incomplete-width-pack';broken.mkdir()
    for source in baseline.iterdir():
        try: os.link(source,broken/source.name)
        except OSError: shutil.copyfile(source,broken/source.name)
    geometry=next(r['geometry_key'] for r in manifest['widescreen'] if r['room']=='M002' and r['group']==0)
    (broken/(geometry+'-wide.bin')).write_bytes(b'PEW1\x01\x00\x00\x00')

    # A diagnostic solid underlay tests the actual room-sidecar path without
    # shipping reconstructed artwork. It must never affect native state/VRAM.
    under=output/'underlay-pack';under.mkdir()
    for source in baseline.iterdir():
        try: os.link(source,under/source.name)
        except OSError: shutil.copyfile(source,under/source.name)
    under_key=0xa55156a55156a551
    cells=[(x,y) for y in range(0,240,16) for x in range(0,320,16)]
    width=16*68;half=((len(cells)+15)//16)*68;height=half*2
    pixels=b'\0'*(width*half*3)+bytes((137,11,241))*(width*half)
    header=struct.pack('<2sIHHI',b'BM',54+len(pixels),0,0,54)
    header+=struct.pack('<IiiHHIIiiII',40,width,height,1,24,0,len(pixels),0,0,0,0)
    (under/f'{under_key:016x}.bmp').write_bytes(header+pixels)
    descriptor=struct.pack('<4sI',b'PEW1',1)+struct.pack('<IIQiiii',0,len(cells),under_key,0,320,0,240)
    descriptor+=b''.join(struct.pack('<hh',x,y) for x,y in cells)
    (under/(geometry+'-under.bin')).write_bytes(descriptor)

    # The original room covers every 4:3 pixel, so the underlay must stay
    # hidden behind it (issue #208: edge feathering exposed what lies under a
    # foreground's hole). It is revealed only through authored coverage: give
    # the room's base layer a half-transparent material over its unchanged
    # reference plane, with and without the underlay beneath it.
    base_key=int.from_bytes(bytes.fromhex(geometry),'big')
    for byte in struct.pack('<I',1):base_key=((base_key^byte)*1099511628211)&(2**64-1)
    base_name=f'{base_key:016x}'
    reference_width,reference_height,reference_rows=read_png_rgb(baseline/(base_name+'.ref.png'))
    material_rows=[bytes((90,90,90,128))*reference_width]*reference_height
    reference_rows=[bytes(b for i in range(0,len(row),3) for b in row[i:i+3]+b'\xff') for row in reference_rows]
    mattes=[]
    for name,underlay in (('matte-pack',False),('matte-underlay-pack',True)):
        directory=output/name;directory.mkdir();mattes.append(directory)
        for source in (under if underlay else baseline).iterdir():
            if source.name.startswith(base_name):continue
            try: os.link(source,directory/source.name)
            except OSError: shutil.copyfile(source,directory/source.name)
        write_png_rgba(directory/(base_name+'.png'),reference_width,material_rows+reference_rows)

    broken_under=output/'incomplete-underlay-pack';broken_under.mkdir()
    for source in baseline.iterdir():
        try: os.link(source,broken_under/source.name)
        except OSError: shutil.copyfile(source,broken_under/source.name)
    (broken_under/(geometry+'-under.bin')).write_bytes(b'PEW1\x01\x00\x00\x00')

    # PEW2: the reviewed sidecar rewritten with kind-0 records must draw exactly
    # as PEW1; a depth-sorted (kind 1) layer must show over the side art without
    # touching state, native VRAM or PCM; a kind-1 depth of 0 rejects the file.
    def pew2(extra=b'',extra_count=0):
        data=(pack/(geometry+'-wide.bin')).read_bytes();assert data[:4]==b'PEW1'
        groups=struct.unpack_from('<I',data,4)[0];at=8;out=bytearray(b'PEW2'+struct.pack('<I',groups+extra_count))
        for _ in range(groups):
            count=struct.unpack_from('<I',data,at+4)[0]
            out+=data[at:at+32]+struct.pack('<I',0)+data[at+32:at+32+count*4];at+=32+count*4
        assert at==len(data)
        return bytes(out+extra)
    def layered_pack(name,descriptor):
        directory=output/name;directory.mkdir()
        for source in pack.iterdir():
            if source.is_file() and not source.name.endswith('-wide.bin'):
                try: os.link(source,directory/source.name)
                except OSError: shutil.copyfile(source,directory/source.name)
        (directory/(geometry+'-wide.bin')).write_bytes(descriptor)
        return directory
    kind0=layered_pack('pew2-backdrop-pack',pew2())
    front_key=0xf00df00df00df00d
    front_cells=[(x,y) for y in range(0,240,16) for x in (320,336,352,368)]
    width=16*68;half=((len(front_cells)+15)//16)*68
    pixels=b'\0'*(width*half*3)+bytes((20,250,20))*(width*half)
    header=struct.pack('<2sIHHI',b'BM',54+len(pixels),0,0,54)
    header+=struct.pack('<IiiHHIIiiII',40,width,half*2,1,24,0,len(pixels),0,0,0,0)
    def front(depth):
        layer=struct.pack('<IIQiiiiI',0,len(front_cells),front_key,320,384,0,240,1)
        layer+=b''.join(struct.pack('<hh',x,y) for x,y in front_cells)
        return layer+b''.join(struct.pack('<H',depth) for _ in front_cells)
    layered=layered_pack('pew2-front-pack',pew2(front(8),1))
    (layered/f'{front_key:016x}.bmp').write_bytes(header+pixels)
    bad_depth=layered_pack('pew2-bad-depth-pack',pew2(front(0),1))
    (bad_depth/f'{front_key:016x}.bmp').write_bytes(header+pixels)

    def run(case):
        wide, extended = case
        directory=output/f'{wide}-{extended}';directory.mkdir()
        env=dict(os.environ,SDL_AUDIODRIVER='dummy',PSYZ_AUDIO_CLOCK='video',
                 PE_CONFIG_HOME=str(directory/'config'),PE_HD_BACKGROUNDS=str((baseline,pack,broken,under,broken_under,kind0,layered,bad_depth,*mattes)[extended]))
        args=[str(Path(executable).resolve()),'--disc',str(Path(disc).resolve()),'--headless-gpu',
              '--internal-resolution','4','--widescreen',str(wide),'--frame-limit','25100',
              '--control-at','25000','--dump-frame',str(directory/'frame.ppm'),
              '--dump-vram',str(directory/'vram.bin'),'--dump-audio',str(directory/'audio.pcm')]
        for tick, mask in timeline: args.extend(('--pad-at',f'{tick}:{mask}'))
        child=subprocess.Popen(args,cwd=directory,env=env,stdin=subprocess.PIPE,stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT,text=True)
        states=[];frames=[]
        try:
            with (directory/'game.log').open('w') as log:
                for line in child.stdout:
                    log.write(line)
                    if 'runtime error:' in line or 'ERROR: AddressSanitizer' in line:
                        child.kill();raise AssertionError(line)
                    if not line.startswith('{"event":"control"'):continue
                    state=json.loads(line)
                    assert state['scene']==0xa8000148 and state['display']['rgb24']==0
                    states.append((state,hashlib.sha256((directory/'vram.bin').read_bytes()).hexdigest(),
                                   hashlib.sha256((directory/'audio.pcm').read_bytes()).hexdigest()))
                    frames.append((directory/'frame.ppm').read_bytes())
                    child.stdin.write(('advance 8 0' if len(states)==1 else 'quit')+'\n');child.stdin.flush()
        except BaseException:
            if child.poll() is None: child.kill()
            child.wait()
            raise
        assert child.wait()==0 and len(states)==2
        return case,(states,frames)

    with ThreadPoolExecutor(max_workers=2) as pool:
        results=dict(pool.map(run,[(0,0),(0,1),(1,0),(1,1),(1,2),(0,3),(1,3),(0,4),(1,5),(1,6),(1,7),(0,8),(0,9)]))
    for wide in (0,1):
        native,backdrop=results[wide,0],results[wide,3]
        assert native[0]==backdrop[0],f'Underlay changed game state, native VRAM or PCM: {output}'
        assert native[1]==backdrop[1],f'Underlay showed through opaque original room coverage in aspect {wide}: {output}'
        a,b=results[wide,0],results[wide,1]
        assert a[0]==b[0],f'HD side tiles changed game state, native VRAM or PCM: {output}'
        for original,enhanced in zip(a[1],b[1]):
            if not wide:
                assert original==enhanced,'4:3 changed'
                continue
            header,pixels=enhanced.split(b'\n255\n',1)
            width,height=map(int,header.splitlines()[-1].split())
            margin=(width-320*4)//2
            lit=0
            for y in range(40,height-40):
                for x in list(range(8,margin-8))+list(range(width-margin+8,width-8)):
                    at=(y*width+x)*3
                    lit+=max(pixels[at:at+3])>12
            assert lit>(height-80)*margin//2,f'Side material is absent/stale: {output}'
    matte,revealed=results[0,8],results[0,9]
    assert matte[0]==revealed[0]==results[0,0][0],f'Authored coverage or underlay changed game state, native VRAM or PCM: {output}'
    assert matte[1]!=results[0,0][1],f'Authored base coverage had no presentation effect: {output}'
    assert matte[1]!=revealed[1],f'Underlay was not visible through authored coverage: {output}'
    assert results[1,0]==results[1,2],f'Incomplete sidecar did not fall back exactly: {output}'
    assert results[0,0]==results[0,4],f'Incomplete underlay did not fall back exactly: {output}'
    assert results[1,5]==results[1,1],f'PEW2 backdrop records did not draw exactly as PEW1: {output}'
    assert results[1,6][0]==results[1,1][0],f'Depth-sorted side layer changed state, native VRAM or PCM: {output}'
    for plain,front in zip(results[1,1][1],results[1,6][1]):
        header,pixels=front.split(b'\n255\n',1)
        width,height=map(int,header.splitlines()[-1].split())
        margin=(width-320*4)//2;green=0
        for y in range(40,height-40,4):
            for x in range(width-margin+8,width-8,4):
                r,g,b=pixels[(y*width+x)*3:(y*width+x)*3+3];green+=g>200 and r<80 and b<80
        assert plain!=front and green>((height-80)//4)*((margin-16)//4)//2,f'Depth-sorted side layer is not drawn: {output}'
    assert results[1,7]==results[1,0],f'Invalid PEW2 depth did not fall back exactly: {output}'
    (output/'proof.json').write_text(json.dumps(dict(passed=True,checkpoints=2,
        exact_4_3=True,native_vram_pcm_and_state_equal=True,visible_side_material=True,underlay_hidden_by_opaque_room=True,underlay_visible_through_authored_coverage=True,incomplete_sidecar_exact_fallback=True,pew2_backdrop_exact=True,pew2_front_layer_visible=True,pew2_invalid_depth_fallback=True),indent=2)+'\n')
    print(f'HD widescreen: legacy 4:3 exact; underlay hidden by opaque rooms, visible through authored coverage; PEW2 backdrop exact, front layer drawn, bad depth rejected; state/VRAM/PCM exact: {output}')
    return 0


if __name__=='__main__':sys.exit(main(sys.argv[1]))
