from pathlib import Path
import argparse,json,os,hashlib,subprocess,concurrent.futures
from PIL import Image
r=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description='Encode corrected materials without changing native references')
parser.add_argument('--source',type=Path,default=r/'pack-issue-56-candidate')
parser.add_argument('--out',type=Path,default=r/'pack-issue-56-webp')
parser.add_argument('--lossless',action='store_true')
parser.add_argument('--version',help='Set the output pack version without modifying the source')
a=parser.parse_args();source=a.source;dest=a.out
if a.version is not None and not a.version.strip():parser.error('--version must not be empty')
report=dest.parent/(dest.name+'-encoding.json')
if report.exists():raise FileExistsError(report)
if dest.exists():raise FileExistsError(dest)
dest.mkdir(parents=True);m=json.loads((source/'manifest.json').read_text())
keys={record['key'] for record in m['atlases'] if Path(record['file']).suffix=='.png'}
selected={record['file'] for record in m['atlases'] if record['key'] in keys}
audit=[]
for p in source.iterdir():
 if p.is_file() and p.name!='manifest.json' and p.name not in selected:os.link(p,dest/p.name)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def encode(record):
 if record['key'] not in keys:return
 key=record['key'];png=source/record['file'];temp=dest/(key+'.source.png');target=dest/(key+'.webp')
 with Image.open(png) as image:
  w,h=image.size;top=image.crop((0,0,w,h//2)).convert('RGBA');top.save(temp)
  # Break a baseline material hardlink before encoding the candidate.
  target.unlink(missing_ok=True)
  subprocess.run(['cwebp','-quiet',*(['-lossless'] if a.lossless else ['-q','95']),'-m','6','-exact',str(temp),'-o',str(target)],check=True)
  with Image.open(target) as decoded,Image.open(dest/record['reference_file']) as ref:
   rgba=decoded.convert('RGBA');assert rgba.getchannel('A').tobytes()==top.getchannel('A').tobytes()
   if a.lossless:assert rgba.tobytes()==top.tobytes(),key+' RGBA drift'
   assert sha(dest/record['reference_file'])==record['reference_sha256']
   assert ref.size==top.size,key+' reference dimensions'
   audit.append(dict(key=key,source_sha256=sha(png),webp_sha256=sha(target),rgba_exact=a.lossless,alpha_exact=True,reference_exact=True))
   record.update(file=target.name,atlas_sha256=sha(target),pixel_sha256=hashlib.sha256(rgba.tobytes()+ref.convert('RGBA').tobytes()).hexdigest(),compressed_bytes=target.stat().st_size+(dest/record['reference_file']).stat().st_size)
 temp.unlink()
with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:list(pool.map(encode,m['atlases']))
m['image_format']='webp';m['development_candidate']='issue-56';m['image_bytes']=sum(p.stat().st_size for p in dest.iterdir() if p.is_file())
if a.version is not None:m['version']=a.version
# Material changes can leave the source pack's byte count stale. Include the
# final manifest itself; its byte-count field can change its serialized size.
m['distribution_bytes']=m['image_bytes']
while True:
 data=json.dumps(m,separators=(',',':')).encode()
 total=m['image_bytes']+len(data)
 if m['distribution_bytes']==total:break
 m['distribution_bytes']=total
(dest/'manifest.json').write_bytes(data)
report.write_text(json.dumps(dict(lossless=a.lossless,materials=sorted(audit,key=lambda x:x['key'])),indent=2))
print(dest,m['image_bytes'],len(keys),'replaced materials; native refs and alpha lossless; RGBA exact:',a.lossless)
