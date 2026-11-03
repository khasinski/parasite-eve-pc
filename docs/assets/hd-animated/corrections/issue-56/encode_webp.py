from pathlib import Path
import json,os,hashlib,subprocess,concurrent.futures
from PIL import Image
r=Path(__file__).resolve().parents[2];source=r/'pack-issue-56-candidate';dest=r/'pack-issue-56-webp'
if dest.exists():raise FileExistsError(dest)
dest.mkdir();m=json.loads((source/'manifest.json').read_text());keys=set(m['issue_56_keys'])
for p in source.iterdir():
 if p.is_file() and p.suffix!='.png' and p.name!='manifest.json':os.link(p,dest/p.name)
 # Native reference PNG files stay exactly lossless, linked without rewriting.
 elif p.name.endswith('.ref.png'):os.link(p,dest/p.name)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def encode(record):
 if record['key'] not in keys:return
 key=record['key'];png=source/(key+'.png');temp=dest/(key+'.source.png');target=dest/(key+'.webp')
 with Image.open(png) as image:
  w,h=image.size;top=image.crop((0,0,w,h//2));top.save(temp)
  # Break a baseline material hardlink before encoding the candidate.
  target.unlink(missing_ok=True)
  subprocess.run(['cwebp','-quiet','-q','95','-m','6','-exact',str(temp),'-o',str(target)],check=True)
  with Image.open(target) as decoded,Image.open(dest/record['reference_file']) as ref:
   rgba=decoded.convert('RGBA');assert rgba.getchannel('A').tobytes()==top.getchannel('A').tobytes()
   assert sha(dest/record['reference_file'])==record['reference_sha256']
   record.update(file=target.name,atlas_sha256=sha(target),pixel_sha256=hashlib.sha256(rgba.tobytes()+ref.convert('RGBA').tobytes()).hexdigest(),compressed_bytes=target.stat().st_size+(dest/record['reference_file']).stat().st_size)
 temp.unlink()
with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:list(pool.map(encode,m['atlases']))
m['image_format']='webp';m['development_candidate']='issue-56';m['image_bytes']=sum(p.stat().st_size for p in dest.iterdir() if p.is_file());(dest/'manifest.json').write_text(json.dumps(m,separators=(',',':')))
print(dest,m['image_bytes'],len(keys),'replaced materials; native refs and alpha lossless')
