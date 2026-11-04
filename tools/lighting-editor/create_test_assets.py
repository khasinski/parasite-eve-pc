"""Synthetic ISO/model fixtures for packaged app CI; contains no game assets."""
from pathlib import Path
import argparse,struct,json

def record(name,lba,size,directory=False):
    name=name.encode();length=33+len(name)+(len(name)%2==0);row=bytearray(length);row[0]=length
    struct.pack_into('<I',row,2,lba);struct.pack_into('>I',row,6,lba);struct.pack_into('<I',row,10,size);struct.pack_into('>I',row,14,size)
    row[25]=2 if directory else 0;row[28:32]=b'\x01\x00\x00\x01';row[32]=len(name);row[33:33+len(name)]=name;return row

def create(out):
    out.mkdir(parents=True,exist_ok=True);base=0x80010000;offset=0x800+0x80093378-base
    exe=bytearray(offset+450*8);exe[:8]=b'PS-X EXE';struct.pack_into('<I',exe,24,base);struct.pack_into('<II',exe,offset+19*8,0,1<<20)
    room=bytearray(2048);struct.pack_into('<II',room,0,len(room),8)
    # One collision block and one geometry/camera block in the original directories.
    struct.pack_into('<I',room,8+0x18,(1<<22)|64);struct.pack_into('<I',room,8+0x1c,(1<<22)|76)
    struct.pack_into('<III',room,64,128,128,0);struct.pack_into('<III',room,76,112,256,0)
    collision=memoryview(room)[128:256];struct.pack_into('<HHH',collision,2,1,1,0);struct.pack_into('<III',collision,24,48,64,0);struct.pack_into('<I',collision,40,44)
    struct.pack_into('<6h',collision,48,-1000,-1000,1000,-1000,0,1000);struct.pack_into('<BB3H',collision,64,0,0,0,1,2)
    g=memoryview(room)[256:368];struct.pack_into('<I',g,0,112);struct.pack_into('<H',g,10,1);struct.pack_into('<II',g,20,60,0);struct.pack_into('<I',g,28,60)
    struct.pack_into('<H9h3i',g,60,256,4096,0,0,0,4096,0,0,0,4096,0,0,1000);struct.pack_into('<HHhhhh',g,100,320,224,160,160,112,112)
    iso=bytearray((25+(len(exe)+2047)//2048)*2048);pvd=16*2048;iso[pvd+1:pvd+6]=b'CD001';root=record('\0',20,2048,True);iso[pvd+156:pvd+156+len(root)]=root
    directory=record('PE.IMG;1',24,2048)+record('SLUS_006.62;1',25,len(exe));iso[20*2048:20*2048+len(directory)]=directory
    iso[24*2048:25*2048]=room;iso[25*2048:25*2048+len(exe)]=exe;(out/'disc.iso').write_bytes(iso)
    model=dict(format=1,origin=[0,0,0],vertices=[[-50,0,0,0,0,-1],[50,0,0,0,0,-1],[0,-300,0,0,0,-1]],faces=[dict(indices=[0,1,2],uv=[[0,0]]*3,page=-1,clut=0,colors=[[128,128,128]]*3)])
    (out/'model.json').write_text(json.dumps(model));(out/'model.json.vram').write_bytes(bytes(1048576))
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('output',type=Path);create(p.parse_args().output)
