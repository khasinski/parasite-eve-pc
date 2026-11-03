"""Reproducible local issue56 candidate. Never mutates baseline pack or refs."""
from pathlib import Path
import cv2,numpy as np,json,hashlib,os,subprocess,shutil
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parents[3]
HERE=Path(__file__).resolve().parent
BASE=ROOT/'hd-animated/pack-0.1-webp'
OUT=ROOT/'hd-animated/pack-issue-56-candidate'
# Source masters stay versioned separately. Hardlinks are replaced, never edited.
if OUT.exists():raise FileExistsError(OUT)
OUT.mkdir()
for p in BASE.iterdir():
 if p.is_file():os.link(p,OUT/p.name)
manifest=json.loads((BASE/'manifest.json').read_text())
records={t['key']:t for t in manifest['atlases']}
tasks=[];views={}
for kind in ['hd-static','hd-animated']:
 r=ROOT/kind
 for t in json.loads((r/'tasks.json').read_text())['tasks']:
  if t['room'] in ['M046','M092','M094','M095','M097','M099','M103','M239']:
   tasks.append((r,t))
 for v in json.loads((r/'views.json').read_text()):views[(v['room'],v['group'])]=(r,v)

def scene(room,g=0):
 r,v=views[(room,g)]
 return np.array(Image.open(r/'composites'/f'{room}-g{g:02}-cbase-p00-hd.png').convert('RGB'))

def project(image,quad,size):
 image=np.asarray(image);h,w=image.shape[:2]
 H=cv2.getPerspectiveTransform(np.float32([[0,0],[w-1,0],[w-1,h-1],[0,h-1]]),np.float32(quad)*4)
 return cv2.warpPerspective(image,H,size,flags=cv2.INTER_LANCZOS4)

def rectify(image,quad,size):
 w,h=size
 H=cv2.getPerspectiveTransform(np.float32(quad)*4,np.float32([[0,0],[w-1,0],[w-1,h-1],[0,h-1]]))
 return cv2.warpPerspective(image,H,size,flags=cv2.INTER_LANCZOS4)

patches={}
def add(room,g,image,polygons,reason,exclude=[],entries=None):
 r,v=views[(room,g)];w,h=v['canvas_size'];mask=Image.new('L',(w*4,h*4));d=ImageDraw.Draw(mask)
 for poly in polygons:d.polygon([(x*4,y*4) for x,y in poly],fill=255)
 for poly in exclude:d.polygon([(x*4,y*4) for x,y in poly],fill=0)
 alpha=np.clip(cv2.distanceTransform(np.array(mask),cv2.DIST_L2,5)/4,0,1)
 patches.setdefault((room,g),[]).append((np.array(image),alpha,reason,entries))

def crop_patch(room,g,name,bbox,poly):
 canvas=scene(room,g);x0,y0,x1,y1=bbox
 patch=Image.open(HERE/name).convert('RGB').resize(((x1-x0)*4,(y1-y0)*4),Image.Resampling.LANCZOS)
 canvas[y0*4:y1*4,x0*4:x1*4]=np.array(patch)
 add(room,g,canvas,[poly],name)

crop_patch('M095',0,'M095-glass-v1.png',(385,110,512,205),[(406,140),(444,140),(449,177),(405,181)])
crop_patch('M239',0,'M239-keyboard-v1.png',(260,75,350,115),[(282,93),(318,89),(324,104),(286,111)])
# The reported visible cart is controller entry13. Entry12 is a uniform
# original opaque-black placeholder, not a second cart pose: leave it intact.
cart=np.array(Image.open(HERE/'M097-handtruck-state13-v1.png').convert('RGB').resize((512,800),Image.Resampling.LANCZOS))
# Register the generated object to the original controller's exact 22x32
# native bounds; imagegen shifted/expanded the crop's object slightly.
source=np.float32([[64.35-48,63.4-45],[86.4-48,63.4-45],[64.35-48,96.7-45]])*8
target=np.float32([[64-48,62-45],[86-48,62-45],[64-48,94-45]])*8
cart=cv2.warpAffine(cart,cv2.getAffineTransform(source,target),(512,800),flags=cv2.INTER_LANCZOS4,borderMode=cv2.BORDER_REFLECT_101)
canvas=scene('M097');canvas[45*4:145*4,48*4:112*4]=np.array(Image.fromarray(cart).resize((256,400),Image.Resampling.LANCZOS))
add('M097',0,canvas,[[(62,60),(89,60),(89,97),(62,97)]],'Remove baked-in hand-truck staircase; registered original bounds',entries=[13])
painting=np.array(Image.open(HERE/'M103-canonical-painting-v1.png').convert('RGB'))
for room,g,q in [('M046',0,[(90,43),(174,30),(172,59),(90,71)]),('M103',0,[(90,43),(174,30),(172,59),(90,71)]),('M103',1,[(126,-34),(350,-5),(344,82),(126,44)])]:
 canvas=scene(room,g)
 exclude=[]
 if g==1:
  # Original foreground globe crosses the painting. Preserve its complete
  # silhouette in every depth layer, rather than painting over the lamp.
  exclude=[[(298.5+11.5*np.cos(a),72+11.5*np.sin(a)) for a in np.linspace(0,2*np.pi,64)]]
 add(room,g,project(painting,q,(canvas.shape[1],canvas.shape[0])),[q],'Single source-faithful bridge painting across intact, damaged and close views',exclude=exclude)
# Reuse the already correct close-view NY [shield] PD lettering as one physical
# plaque. No independently invented replacement letters.
plaque=rectify(scene('M103',1),[(87,108),(234,128),(233,169),(87,146)],(1024,300))
for room in ['M046','M103']:
 q=[(101,109),(170,90),(170,108),(101,128)];canvas=scene(room)
 add(room,0,project(plaque,q,(canvas.shape[1],canvas.shape[0])),[q],'NYPD plaque from the canonical close view; remove extra Y')
board=rectify(scene('M042'),[(26.5,152),(79,127.7),(86.8,170),(38.4,196.5)],(1024,768))
q=[(34,172),(67,182),(57,234),(16,223)];canvas=scene('M099')
add('M099',0,project(board,q,(canvas.shape[1],canvas.shape[0])),[q],'Fallen board reuses intact M042 writing and grid',exclude=[[(0,209),(22,218),(22,250),(0,250)]])
for room,poly in [('M094',[(0,40),(110,40),(126,97),(0,117)]),('M097',[(55,365),(110,353),(312,410),(298,449),(77,399),(65,419),(36,410)])]:
 r,v=views[(room,0)];w,h=v['canvas_size'];x,y,_,_=v['active_bbox']
 canvas=Image.new('RGB',(w*4,h*4));canvas.paste(Image.open(r/'generated'/v['base_input']).convert('RGB').resize((v['active_size'][0]*4,v['active_size'][1]*4),Image.Resampling.LANCZOS),(x*4,y*4))
 add(room,0,canvas,[poly],'One full-context material shared by every intersecting depth layer')

proof=[]
for r,t in tasks:
 if t['room']=='M097' and t['entry']==12:continue # source-only opaque-black placeholder
 key=t['key'];record=records[key]
 with Image.open(BASE/record['file']) as im:material=np.array(im.convert('RGBA'))
 before=material.copy();refs=np.load(r/'refs'/t['ref']);cols=min(16,len(refs))
 # Exact native coverage in room coordinates, then smooth only the authored HD
 # contour. Native RGB555/STP reference stays byte-identical in its own file.
 rr,v=views[(t['room'],t['group'])];w,h=v['canvas_size'];mask=np.zeros((h,w),np.uint8)
 for (x,y),ref in zip(t['tiles'],refs):mask[y:y+16,x:x+16]|=(ref!=0).astype(np.uint8)
 native4=np.repeat(np.repeat(mask,4,0),4,1)*255
 contours,_=cv2.findContours(native4,cv2.RETR_LIST,cv2.CHAIN_APPROX_SIMPLE)
 contours=[cv2.approxPolyDP(c,2.0,True) if cv2.arcLength(c,True)>=64 else c for c in contours]
 alpha=np.zeros_like(native4);cv2.drawContours(alpha,contours,-1,255,cv2.FILLED,lineType=cv2.LINE_AA)
 allowed=np.zeros(material.shape[:2],bool)
 for i,(x,y) in enumerate(t['tiles']):
  ax=i%cols*68+2;ay=i//cols*68+2;tile=material[ay:ay+64,ax:ax+64].copy()
  for image,a,reason,entries in patches.get((t['room'],t['group']),[]):
   if entries is not None and t['entry'] not in entries:continue
   blend=a[y*4:y*4+64,x*4:x*4+64,None];candidate=image[y*4:y*4+64,x*4:x*4+64]
   tile[:,:,:3]=np.uint8(np.rint(tile[:,:,:3]*(1-blend)+candidate*blend))
   allowed[ay-2:ay+66,ax-2:ax+66]|=np.pad(blend[:,:,0]>0,2,mode='edge')
  tile[:,:,3]=alpha[y*4:y*4+64,x*4:x*4+64]
  original_coverage=np.repeat(np.repeat(refs[i]!=0,4,0),4,1)
  expansion=(~original_coverage)&(tile[:,:,3]>0)
  if np.any(expansion) and np.any(original_coverage):
   # New edge texels must inherit real foreground colour, never black or the
   # unrelated background carried by the full-context material outside it.
   _,labels=cv2.distanceTransformWithLabels((~original_coverage).astype(np.uint8),cv2.DIST_L2,5,labelType=cv2.DIST_LABEL_PIXEL)
   colours=tile[:,:,:3][original_coverage];nearest=colours[np.maximum(labels-1,0)]
   tile[:,:,:3][expansion]=nearest[expansion]
   allowed[ay-2:ay+66,ax-2:ax+66]|=np.pad(expansion,2,mode='edge')
  padded=np.pad(tile,((2,2),(2,2),(0,0)),mode='edge')
  cell=material[ay-2:ay+66,ax-2:ax+66];support=allowed[ay-2:ay+66,ax-2:ax+66]
  cell[:,:,:3]=np.where(support[:,:,None],padded[:,:,:3],cell[:,:,:3]);cell[:,:,3]=padded[:,:,3]
 changed=np.any(material[:,:,:3]!=before[:,:,:3],axis=2)
 assert not np.any(changed&~allowed),key
 # Lossless PNG candidate avoids any unrelated re-encoding drift during review.
 target=OUT/(key+'.png');Image.fromarray(material).save(target,optimize=True)
 reference_sha=hashlib.sha256((OUT/record['reference_file']).read_bytes()).hexdigest();assert reference_sha==record['reference_sha256']
 # Runtime PNG needs a combined reference half; keep the WebP sidecar linked as
 # the independent integrity proof and material for final WebP packaging.
 ref=np.array(Image.open(BASE/record['reference_file']).convert('RGBA'));assert ref.shape==material.shape
 combined=np.concatenate((material,ref),axis=0);Image.fromarray(combined).save(target,optimize=True)
 record.update(file=target.name,atlas_sha256=hashlib.sha256(target.read_bytes()).hexdigest(),pixel_sha256=hashlib.sha256(combined.tobytes()).hexdigest())
 proof.append(dict(room=t['room'],group=t['group'],entry=t['entry'],key=key,rgb_changed=int(changed.sum()),coverage_changed=int(np.sum(material[:,:,3]!=before[:,:,3])),reference_byte_identical=True,unselected_material_exact=True,corrections=[x[2] for x in patches.get((t['room'],t['group']),[]) if x[3] is None or t['entry'] in x[3]]))
manifest['image_format']='mixed-png-webp';manifest['development_candidate']='issue-56';manifest['issue_56_keys']=[r['key'] for r in proof]
# Break the baseline manifest hardlink before writing.
(OUT/'manifest.json').unlink();(OUT/'manifest.json').write_text(json.dumps(manifest,separators=(',',':')))
(HERE/'application-audit.json').write_text(json.dumps(dict(candidate=str(OUT),atlases=proof,baseline_preserved=True),indent=2))
print(OUT,len(proof),'atlases; all refs and unselected material exact')
