"""Local original-asset lighting editor. Requires Pillow, NumPy and local Three.js."""
import argparse,hashlib,json,math,mimetypes,os,re,tempfile
from pathlib import Path
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from functools import lru_cache
from urllib.parse import urlsplit
import numpy as np
from assets import Rooms,model_texture

ROOT=Path(__file__).resolve().parent

def profile_valid(data,room):
    if not isinstance(data,dict) or data.get('format')!=1 or data.get('map')!=room:raise ValueError('Wrong profile format or room')
    def number(v,lo,hi):
        if type(v) not in (int,float) or not math.isfinite(v) or not lo<=v<=hi:raise ValueError('Invalid numeric setting')
    def vector(v):
        if not isinstance(v,list) or len(v)!=3:raise ValueError('Expected XYZ')
        for n in v:number(n,-1000000,1000000)
    def color(v):
        if not isinstance(v,str) or not re.fullmatch('#[0-9a-fA-F]{6}',v):raise ValueError('Invalid light color')
    model=data.get('aya',{})
    if not isinstance(model,dict) or not isinstance(data.get('ambient'),dict):raise ValueError('Expected Aya and ambient settings')
    vector(model.get('position'));number(model.get('yaw'),-360,360)
    color(data.get('ambient',{}).get('color'));number(data['ambient'].get('intensity'),0,4)
    number(data.get('exposure'),0.05,4);number(data.get('roughness'),0.05,1)
    lights=data.get('lights')
    if not isinstance(lights,list) or len(lights)>16:raise ValueError('At most 16 lights')
    names=set()
    for light in lights:
        if not isinstance(light,dict) or light.get('type') not in ('point','spot','directional'):raise ValueError('Invalid light type')
        key=light.get('id')
        if not isinstance(key,str) or not re.fullmatch('[a-zA-Z0-9_-]{1,64}',key) or key in names:raise ValueError('Invalid or duplicate light ID')
        names.add(key);vector(light.get('position'));vector(light.get('target'));color(light.get('color'))
        number(light.get('intensity'),0,1000);number(light.get('range'),0,100000)
        number(light.get('angle'),1,89)
    return data

def save_profile(directory,room,data,identity):
    profile_valid(data,room)
    content=json.dumps(dict(profile=data,source=identity),ensure_ascii=False,indent=2)+'\n'
    directory.mkdir(parents=True,exist_ok=True)
    descriptor,path=tempfile.mkstemp(prefix=f'.M{room:03}-',dir=directory)
    try:
        with os.fdopen(descriptor,'w') as file:file.write(content);file.flush();os.fsync(file.fileno())
        os.replace(path,directory/f'M{room:03}.json')
    finally:
        if os.path.exists(path):os.unlink(path)

class Workbench:
    def __init__(self,disc,model,profiles,vendor):
        self.rooms=Rooms(disc);self.model=json.loads(model.read_text());self.vendor=vendor.resolve();self.profiles=profiles.resolve()
        self.identity=dict(model_sha256=hashlib.sha256(model.read_bytes()).hexdigest(),disc_name=disc.name)
        self.model['source']=self.identity
        raw=Path(str(model)+'.vram').read_bytes()
        if len(raw)!=1048576:raise ValueError('Invalid captured VRAM')
        self.vram=np.frombuffer(raw,dtype='<u2').reshape(512,1024)
    @lru_cache(maxsize=48)
    def texture(self,page,clut):return model_texture(self.vram,page,clut)

def handler(app):
    class Handler(BaseHTTPRequestHandler):
        def reply(self,content,status=200,mime='application/json'):
            if not isinstance(content,bytes):content=json.dumps(content).encode()
            self.send_response(status);self.send_header('Content-Type',mime);self.send_header('Content-Length',str(len(content)))
            self.send_header('Cache-Control','no-store');self.send_header('X-Content-Type-Options','nosniff');self.end_headers();self.wfile.write(content)
        def do_GET(self):
            path=urlsplit(self.path).path
            try:
                if path=='/api/rooms':return self.reply(app.rooms.catalog)
                if path=='/api/model':return self.reply(app.model)
                m=re.fullmatch(r'/api/room/(\d+)/(\d+)(\.png)?',path)
                if m:
                    data,image=app.rooms.load(int(m[1]),int(m[2]));return self.reply(image,mime='image/png') if m[3] else self.reply(data)
                m=re.fullmatch(r'/api/texture/(\d+)/(\d+)\.png',path)
                if m:
                    page,clut=int(m[1]),int(m[2])
                    if page>511 or clut>32767:raise ValueError('Invalid material')
                    return self.reply(app.texture(page,clut),mime='image/png')
                m=re.fullmatch(r'/api/profile/(\d+)',path)
                if m:
                    room=int(m[1]);p=app.profiles/f'M{room:03}.json'
                    if not p.exists():return self.reply(dict(profile=None))
                    data=json.loads(p.read_text());profile_valid(data['profile'],room);return self.reply(data)
                if path.startswith('/vendor/'):
                    file=(app.vendor/path.removeprefix('/vendor/')).resolve()
                    if not file.is_relative_to(app.vendor):raise ValueError('Invalid vendor path')
                else:
                    file=(ROOT/('index.html' if path=='/' else path.lstrip('/'))).resolve()
                    if file.parent!=ROOT or file.suffix not in ('.html','.js','.css'):return self.reply(dict(error='Not found'),404)
                return self.reply(file.read_bytes(),mime=mimetypes.guess_type(file.name)[0] or 'application/octet-stream')
            except (ValueError,KeyError,IndexError,FileNotFoundError) as e:self.reply(dict(error=str(e)),400)
        def do_POST(self):
            m=re.fullmatch(r'/api/profile/(\d+)',urlsplit(self.path).path)
            if not m:return self.reply(dict(error='Not found'),404)
            try:
                room=int(m[1]);length=int(self.headers.get('Content-Length','0'))
                if room<1 or room>450 or not 0<length<=65536:raise ValueError('Invalid save request')
                origin=self.headers.get('Origin')
                if origin and urlsplit(origin).netloc!=self.headers.get('Host'):raise ValueError('Wrong origin')
                data=json.loads(self.rfile.read(length));save_profile(app.profiles,room,data,app.identity)
                self.reply(dict(saved=f'M{room:03}.json'))
            except (ValueError,KeyError,TypeError) as e:self.reply(dict(error=str(e)),400)
        def log_message(self,fmt,*args):
            if args and str(args[1] if len(args)>1 else '') not in ('200','304'):super().log_message(fmt,*args)
    return Handler

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--disc',type=Path,required=True,help='USA Disc 1 or Disc 2 BIN')
    p.add_argument('--model',type=Path,required=True);p.add_argument('--profiles',type=Path,default=Path('build/lighting-editor/profiles'))
    p.add_argument('--vendor',type=Path,default=ROOT/'node_modules/three');p.add_argument('--port',type=int,default=8766)
    args=p.parse_args();app=Workbench(args.disc,args.model,args.profiles,args.vendor)
    if not (args.vendor/'build/three.module.js').exists():p.error('Install pinned Three.js first; see README')
    server=ThreadingHTTPServer(('127.0.0.1',args.port),handler(app))
    print(f'Lighting editor: http://127.0.0.1:{args.port} — {len(app.rooms.catalog)} rooms; profiles: {app.profiles}',flush=True)
    server.serve_forever()
