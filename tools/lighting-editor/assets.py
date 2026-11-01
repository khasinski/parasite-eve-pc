"""Read-only USA disc geometry for the room-lighting workbench.

Layouts mirror scene_room.h, geom_state.h and collision_database.h. Game assets
are decoded locally from the user's disc; none are shipped with this tool.
"""
from pathlib import Path
from functools import lru_cache
import io,mmap,struct,unicodedata,re
import numpy as np
from PIL import Image

def u16(b,o):return struct.unpack_from('<H',b,o)[0]
def u32(b,o):return struct.unpack_from('<I',b,o)[0]
def checked(b,o,size):
    if o<0 or size<0 or o+size>len(b):raise ValueError('Disc data is out of bounds')
    return b[o:o+size]

class Disc:
    def __init__(self,path):
        path=Path(path)
        if path.suffix.lower()=='.cue':
            match=re.search(r'(?im)^\s*FILE\s+"([^"]+)"\s+BINARY',path.read_text(encoding='utf-8-sig'))
            if not match:raise ValueError('CUE must reference a BINARY disc image')
            path=path.parent/match[1]
        self.file=open(path,'rb');self.data=mmap.mmap(self.file.fileno(),0,access=mmap.ACCESS_READ)
        self.stride=2352 if self.data[1:11]==b'\xff'*10 else 2048
        self.skip=24 if self.stride==2352 else 0
        pvd=self.sectors(16,1)
        if pvd[1:6]!=b'CD001':raise ValueError('Expected an ISO9660 USA BIN image')
        self.root=(u32(pvd,158),u32(pvd,166))
    def sectors(self,lba,count):
        if lba<0 or count<0 or (lba+count)*self.stride>len(self.data):raise ValueError('Disc sector out of bounds')
        return b''.join(self.data[(lba+i)*self.stride+self.skip:(lba+i)*self.stride+self.skip+2048] for i in range(count))
    def find(self,name):
        lba,size=self.root;raw=self.sectors(lba,(size+2047)//2048);offset=0
        while offset<len(raw):
            n=raw[offset]
            if not n:offset=(offset//2048+1)*2048;continue
            row=checked(raw,offset,n);key=row[33:33+row[32]].decode('ascii','replace').split(';')[0]
            if key==name:return u32(row,2),u32(row,10)
            offset+=n
        raise ValueError('Disc file not found: '+name)
    def read(self,extent,offset,size):
        if offset<0 or size<0 or offset+size>extent[1]:raise ValueError('Disc file range out of bounds')
        return self.sectors(extent[0]+offset//2048,(offset%2048+size+2047)//2048)[offset%2048:offset%2048+size]

def payload(blob,directory,field):
    packed=u32(blob,directory+field);table=packed&0x3fffff
    if not packed>>22:raise ValueError('Missing room table')
    row=checked(blob,table,12);offset=u32(row,4)&0xffffff;size=u32(row,0)&0xffffff
    return checked(blob,offset,size)

def collision(blob):
    regions,triangles,planes=struct.unpack_from('<HHH',blob,2)
    vp,tp,pp=struct.unpack_from('<III',blob,24)
    out=[]
    for t in range(triangles):
        row=checked(blob,tp+t*(28 if pp else 22),28 if pp else 22)
        indices=struct.unpack_from('<3H',row,8 if pp else 2)
        if row[1]>=regions:raise ValueError('Invalid collision region')
        points=[]
        for index in indices:
            if pp:
                x,y,z=struct.unpack('<hhh',checked(blob,vp+index*6,6))
                plane=u16(row,2)
                if plane>=planes:raise ValueError('Invalid collision plane')
                a,inverse_b,c=struct.unpack('<iii',checked(blob,pp+plane*12,12))
                distance=struct.unpack_from('<i',row,4)[0]
                # Use the same plane equation as Entity_ResolveCurrentPosition;
                # stored vertex Y differs slightly from the gameplay surface.
                y=(distance/65536-a/65536*x-c/65536*z)*inverse_b/65536
            else:
                x,z=struct.unpack('<hh',checked(blob,vp+index*4,4))
                height=u32(blob,40+row[1]*4);y=struct.unpack('<h',checked(blob,height,2))[0]
            points.append([x,y,z])
        out.append(dict(points=points,kind=row[0],region=row[1],walkable=not bool(row[0]&128)))
    return out

def upload(vram,blob):
    if not blob:return
    directory=u32(blob,4);packed=u32(blob,directory+0x28);table=packed&0x3fffff
    for i in range(packed>>22):
        row=checked(blob,table+i*20,20);source,rect=struct.unpack_from('<II',row,4)
        primary=source&0xffffff
        for second in (False,True):
            src,r=struct.unpack_from('<II',row,12) if second else (source,rect)
            offset=src&0xffffff
            if second and not offset:continue
            if second:offset+=primary
            height=src>>24
            if not second and not height:height=256
            width=r&1023;x=(r>>10)&2047;y=r>>21
            if not width or not height:continue
            if x+width>1024 or y+height>512:raise ValueError('Invalid texture upload')
            values=np.frombuffer(checked(blob,offset,width*height*2),dtype='<u2').reshape(height,width)
            vram[y:y+height,x:x+width]=values

def background(geometry,group,vram,width,height):
    """Native default static layers; animation is deliberately frozen."""
    entry=u32(geometry,20);tiles=[];vb=vram.view(np.uint8).reshape(512,2048)
    for i in range(u16(geometry,6)):
        at=entry+56*i
        if not geometry[at]&2 or geometry[at+36]!=group:continue
        pos=at+u32(geometry,at+40);uv=at+u32(geometry,at+44)
        sx,sy=struct.unpack_from('<hh',geometry,at+12);depth=(u32(geometry,at)>>8)&4095
        for t in range(u16(geometry,at+38)):
            a=u32(geometry,pos+4*t);word=u32(geometry,uv+8*t);u=geometry[uv+8*t+4];v=geometry[uv+8*t+3]
            x=sx+(a>>22);y=sy+((a>>12)&1023);tx=(word>>10)&0x3c0;ty=(word>>7)&256;cx=(word>>5)&0x3f0;cy=word&511
            indices=vb[(ty+np.arange(v,v+16)%256)%512][:,(tx*2+np.arange(u,u+16)%256)%2048]
            colors=vram[cy,(cx+indices.astype(np.uint16))%1024]
            tiles.append((depth+(a&4095),i*100000+t,x,y,colors,bool(u32(geometry,uv+8*t+4)&0x10000000),(word>>22)&3))
    out=np.zeros((height,width),dtype=np.uint16)
    for _,_,x,y,pixels,semi,mode in sorted(tiles,key=lambda t:(t[0],t[1]),reverse=True):
        x0=max(0,x);y0=max(0,y);x1=min(width,x+16);y1=min(height,y+16)
        if x0>=x1 or y0>=y1:continue
        p=pixels[y0-y:y1-y,x0-x:x1-x].copy();mask=p!=0;dest=out[y0:y1,x0:x1]
        if semi:
            stp=(p&0x8000)!=0
            for shift in (0,5,10):
                fg=(p>>shift)&31;bg=(dest>>shift)&31
                if mode==0:c=(fg.astype(int)+bg)//2
                elif mode==1:c=np.minimum(31,fg.astype(int)+bg)
                elif mode==2:c=np.maximum(0,bg.astype(int)-fg)
                else:c=np.minimum(31,bg.astype(int)+fg//4)
                p=(p&~np.uint16(31<<shift))|np.where(stp,c,fg).astype(np.uint16)<<shift
        dest[mask]=p[mask]
    return rgb_image(out)

def rgb_image(colors):
    rgb=np.stack([((colors>>shift)&31)*255//31 for shift in (0,5,10)],axis=-1).astype(np.uint8)
    return Image.fromarray(rgb)

def png(image):
    out=io.BytesIO();image.save(out,format='PNG');return out.getvalue()

class Rooms:
    def __init__(self,path):
        self.disc=Disc(path);self.archive=self.disc.find('PE.IMG')
        try:extent=self.disc.find('SLUS_006.62')
        except ValueError:extent=self.disc.find('SLUS_006.68')
        exe=self.disc.read(extent,0,extent[1]);base=u32(exe,24)
        self.ranges=[];self.catalog=[]
        for i in range(450):
            start,bits=struct.unpack_from('<II',exe,0x800+0x80093378-base+i*8)
            self.ranges.append((start,bits&255,(bits>>8)&4095,(bits>>20)&4095))
            if not bits>>20:continue
            try:
                blob=self.raw(i+1);directory=u32(blob,4);g=payload(blob,directory,0x1c)
                vp=u32(g,28);views=[]
                for group in range(u16(g,10)):
                    row=checked(g,vp+group*52,52);width,height=struct.unpack_from('<HH',row,40)
                    if not (0<width<=2048 and 0<height<=2048):continue
                    views.append(dict(group=group,width=width,height=height))
                if views:self.catalog.append(dict(map=i+1,id=f'M{i+1:03}' if i<409 else f'E{i-409:02}',views=views))
            except (ValueError,struct.error,IndexError):continue
    def raw(self,room):
        start,scratch,tex,size=self.ranges[room-1]
        return self.disc.read(self.archive,(start+scratch+tex)*2048,size*2048)
    @lru_cache(maxsize=4)
    def load(self,room,group):
        if room<1 or room>450:raise ValueError('Invalid room')
        blob=self.raw(room);directory=u32(blob,4);g=payload(blob,directory,0x1c);views=u16(g,10)
        if group<0 or group>=views:raise ValueError('Invalid camera')
        camera=checked(g,u32(g,28)+group*52,52);width,height=struct.unpack_from('<HH',camera,40)
        if not (0<width<=2048 and 0<height<=2048):raise ValueError('Invalid background size')
        try:floor=collision(payload(blob,directory,0x18))
        except (ValueError,struct.error,IndexError):floor=[]
        data=dict(map=room,group=group,width=width,height=height,projection=u16(camera,0),
                  rotation=list(struct.unpack_from('<9h',camera,2)),translation=list(struct.unpack_from('<3i',camera,20)),
                  camera_bounds=list(struct.unpack_from('<4h',camera,44)),floor=floor,background_animated=bool(u16(g,4)))
        start,scratch,tex,_=self.ranges[room-1];vram=np.zeros((512,1024),dtype='<u2')
        for offset,size in ((start,scratch),(start+scratch,tex)):
            if size:upload(vram,self.disc.read(self.archive,offset*2048,size*2048))
        return data,png(background(g,group,vram,width,height))

def model_texture(vram,page,clut):
    mode=(page>>7)&3
    x=np.arange(256);y=np.arange(256)
    bx=(page&15)*64;by=((page>>4)&1)*256
    if mode<2:
        words=vram[by+y[:,None],(bx+(x>>(1 if mode else 2)))%1024]
        indices=(words>>((x&(1 if mode else 3))*(8 if mode else 4)))&(255 if mode else 15)
        colors=vram[(clut>>6)&511,((clut&63)*16+indices)%1024]
    elif mode==2:colors=vram[by+y[:,None],(bx+x)%1024]
    else:raise ValueError('Unsupported texture page mode')
    image=rgb_image(colors).convert('RGBA');image.putalpha(Image.fromarray(np.where(colors!=0,255,0).astype(np.uint8)))
    return png(image)
