"""Repair M116's open passage without changing native coverage or animation.

The M056 material reference hallucinated a closed gate and too many stairs in
M116. Apply one registered clean plate to the static passage/landing in every
packet; retain the existing right-hand gate, door poses and warning-light states.
All native reference planes and authored alpha remain byte/pixel identical.
"""
from pathlib import Path
import argparse, hashlib, json, shutil
import cv2
import numpy as np
from PIL import Image

p=argparse.ArgumentParser()
p.add_argument('--baseline',type=Path,required=True)
p.add_argument('--material',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--version',default='0.5.1')
a=p.parse_args()
r=Path(__file__).resolve().parents[2]
assert a.baseline.resolve()!=a.output.resolve()
if not a.output.exists():shutil.copytree(a.baseline,a.output)
manifest=json.loads((a.baseline/'manifest.json').read_text())
assert manifest.get('version')=='0.4', 'This reviewed correction targets immutable pack 0.4'
manifest['version']=a.version
tasks=[t for t in json.loads((r/'tasks.json').read_text())['tasks'] if t['room']=='M116' and t['group']==0]
material=np.array(Image.open(a.material).convert('RGB'),dtype=np.float32)
assert material.shape==(960,1280,3)
old=np.array(Image.open(r/'gallery/M116-g00-c1-p00-hd.webp').convert('RGB'))[:960].astype(np.float32)
# Fixed exposure calibration in an unchanged floor region, never the hallucinated bars.
gain=np.median(old[560:840,580:900],axis=(0,1))/np.maximum(np.median(material[560:840,580:900],axis=(0,1)),1)
material=np.clip(material*gain,0,255)
# The generated plate uses the red-light phase 5. Recover the source's
# low-frequency illumination for each original phase, without copying the
# old hallucinated bars into the newly cleared opening.
light_entries=sorted((t for t in tasks if t['control']==1),key=lambda t:t['entry'])
def source_light(phase):
 image=np.array(Image.open(r/'inputs'/light_entries[phase]['input']).convert('RGB'),dtype=np.float32)
 image=cv2.resize(image,(1280,1024),interpolation=cv2.INTER_NEAREST)
 return cv2.GaussianBlur(image,(0,0),2)
reference_light=source_light(5)
dark_light=source_light(0)
phase_light_delta=[source_light(i)-dark_light for i in range(6)]
view=next(v for v in json.loads((r/'views.json').read_text()) if v['room']=='M116' and v['group']==0)
light_phases={s['entry']:i for c in view['controllers'] if c['id']==1 for i,s in enumerate(c['slots'])}
mask=np.zeros((1024,1280),np.uint8)
mask[0:77*4,136*4:244*4]=1
mask[75*4:133*4,138*4:178*4]=1
# The light itself and the right-hand animated door are outside the repair.
# Do not infer masks from lossy preview differences: codec noise can otherwise
# leave pieces of the hallucinated gate behind in a clean opening.
assert not mask[78*4:130*4,180*4:230*4].any()
alpha=np.clip(cv2.distanceTransform(np.pad(mask,1),cv2.DIST_L2,5)[1:-1,1:-1]/8,0,1)
plate=np.zeros((1024,1280,3),np.float32);plate[:960]=material
# Recover a valid dark material first. Adding light after this clamp retains
# the source emission even when a generated metal bar was too dark.
plate=np.clip(plate+dark_light-reference_light,0,255)
bykey={t['key']:t for t in tasks};records=[]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for entry in manifest['atlases']:
 t=bykey.get(entry['key'])
 if t is None:continue
 source=a.baseline/entry['file'];image=np.array(Image.open(source).convert('RGBA'));before=image.copy()
 lit_plate=np.clip(plate+phase_light_delta[light_phases.get(t['entry'],0)],0,255)
 allowed=np.zeros(image.shape[:2],bool);cols=image.shape[1]//68
 for i,(x,y) in enumerate(t['tiles']):
  ax=(i%cols)*68;ay=(i//cols)*68
  patch=image[ay+2:ay+66,ax+2:ax+66].copy()
  weight=alpha[y*4:y*4+64,x*4:x*4+64,None]
  patch[:,:,:3]=np.rint(patch[:,:,:3]*(1-weight)+lit_plate[y*4:y*4+64,x*4:x*4+64]*weight).astype(np.uint8)
  tile_support=np.pad(weight[:,:,0]>0,2,mode='edge')
  replacement=np.pad(patch,((2,2),(2,2),(0,0)),mode='edge')
  target_rgb=image[ay:ay+68,ax:ax+68,:3]
  target_rgb[tile_support]=replacement[:,:,:3][tile_support]
  allowed[ay:ay+68,ax:ax+68]=tile_support
 assert np.array_equal(image[:,:,3],before[:,:,3])
 assert not np.any(np.any(image[:,:,:3]!=before[:,:,:3],axis=2)&~allowed)
 target=a.output/entry['file'];temp=target.with_suffix('.tmp')
 changed=int(np.any(image[:,:,:3]!=before[:,:,:3],axis=2).sum())
 if not changed:
  shutil.copy2(source,target)
  records.append(dict(key=t['key'],entry=t['entry'],changed_rgb_pixels=0,native_reference_unchanged=True,authored_alpha_unchanged=True,outside_support_unchanged=True))
  continue
 Image.fromarray(image).save(temp,format='WEBP',lossless=True,method=6,exact=True);temp.replace(target)
 decoded=np.array(Image.open(target).convert('RGBA'));assert np.array_equal(decoded,image)
 ref=a.output/entry['reference_file'];assert sha(ref)==entry['reference_sha256']
 entry['atlas_sha256']=sha(target)
 entry['pixel_sha256']=hashlib.sha256(decoded.tobytes()+Image.open(ref).convert('RGBA').tobytes()).hexdigest()
 entry['compressed_bytes']=target.stat().st_size+ref.stat().st_size
 records.append(dict(key=t['key'],entry=t['entry'],source_light_phase=light_phases.get(t['entry'],0),changed_rgb_pixels=int(np.any(image[:,:,:3]!=before[:,:,:3],axis=2).sum()),native_reference_unchanged=True,authored_alpha_unchanged=True,outside_support_unchanged=True))
assert len(records)==13
manifest['correction_m116']='Issue #244: open passage and original landing geometry; native planes, warning light and door animation unchanged.'
temp=a.output/'manifest.tmp';temp.write_text(json.dumps(manifest,indent=2)+'\n');temp.replace(a.output/'manifest.json')
(a.output/'m116-correction.json').write_text(json.dumps(dict(method=__doc__,material_sha256=sha(a.material),gain_rgb=gain.tolist(),phase_lighting='Dark-state material plus original per-phase RGB light differences',packets=records),indent=2)+'\n')
print(len(records),'packets corrected; native references, alpha and all outside-support RGB unchanged')
