"""Build a separate, review-only M092/M097/M239 underlay candidate; no baseline writes."""
import argparse, hashlib, json, os, struct
from pathlib import Path
import cv2
import numpy as np
from PIL import Image

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--assets',type=Path,required=True)
p.add_argument('--baseline',type=Path,required=True)
p.add_argument('--out',type=Path,required=True)
p.add_argument('--rooms',default='M092,M097,M239',help='Comma-separated subset (default: M092,M097,M239); select unmarked rooms')
a=p.parse_args()
selected=set(a.rooms.split(','))
if not selected or not selected<={'M092','M097','M239'}:p.error('Expected --rooms M092,M097,M239 or a subset')
audit_path=a.out.parent/(a.out.name+'-audit.json')
if a.out.exists() or audit_path.exists():raise FileExistsError(a.out)
a.out.mkdir(parents=True)
for source in a.baseline.iterdir():
 if source.is_file():os.link(source,a.out/source.name)
manifest=json.loads((a.baseline/'manifest.json').read_text())
tasks=json.loads((a.assets/'tasks.json').read_text())['tasks']
records={r['key']:r for r in manifest['atlases']}
audit=[]
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
for room,base_entry,geometry in [('M092',12,'2c48651307e4dc15'),('M097',16,'b1c7435335ee7113'),('M239',6,'60de4207704ba290')]:
 if room not in selected:continue
 t=next(t for t in tasks if t['room']==room and t['group']==0 and t['entry']==0)
 if t.get('control') is not None or t.get('controls'):raise ValueError('Static HD coverage cannot be used for animated foregrounds')
 if any(r['file']==t['key']+'-matte.bin' for r in manifest.get('extras',[])):raise ValueError(room+': baseline already has a static matte; select an unmarked room')
 back=next(t for t in tasks if t['room']==room and t['group']==0 and t['entry']==base_entry)
 w,h=t['size'];refs=np.load(a.assets/'refs'/t['ref'])
 def coverage(task):
  mask=np.zeros((h,w),np.uint8)
  for (x,y),ref in zip(task['tiles'],np.load(a.assets/'refs'/task['ref'])):
   mask[y:y+16,x:x+16]|=(ref!=0).astype(np.uint8)
  return mask
 fg=coverage(t);bg=coverage(back)
 native=np.repeat(np.repeat(fg,4,0),4,1)*255
 # Straighten long silhouette runs. Do not erode thin microscope parts;
 # the broad M097 counter also receives a conservative quarter-pixel AA band.
 alpha=cv2.GaussianBlur(native,(0,0),1.0) if room=='M097' else native.copy();yy,xx=np.mgrid[:h*4,:w*4].astype(np.float32)
 points=np.stack(((xx+0.5)/4,(yy+0.5)/4),axis=-1)
 contours,_=cv2.findContours(fg,cv2.RETR_EXTERNAL,cv2.CHAIN_APPROX_SIMPLE)
 for contour in contours:
  vertices=cv2.approxPolyDP(contour,1.0,True)[:,0,:].astype(float)+0.5
  for start,end in zip(vertices,np.roll(vertices,-1,axis=0)):
   delta=end-start;length=float(np.linalg.norm(delta))
   if length<24:continue
   tangent=delta/length;normal=np.array([-tangent[1],tangent[0]])
   def sample(sign):
    pts=start[None]+np.array([0.25,0.5,0.75])[:,None]*delta+sign*normal*2
    return sum(int(fg[int(np.clip(y,0,h-1)),int(np.clip(x,0,w-1))]) for x,y in pts)
   plus,minus=sample(1),sample(-1)
   if plus==minus:continue
   normal*=1 if plus>minus else -1
   relative=points-start;along=relative@tangent;distance=relative@normal
   region=(along>2)&(along<length-2)&(np.abs(distance)<3)
   fitted=np.uint8(np.rint(np.clip((distance+0.25)*4+0.5,0,1)*255))
   alpha[region]=fitted[region]
 queued=np.zeros_like(native)
 for x,y in t['tiles']:queued[y*4:y*4+64,x*4:x*4+64]=1
 alpha[queued==0]=0
 expansion=(native==0)&(alpha>0)
 distance=cv2.distanceTransform((native==0).astype(np.uint8),cv2.DIST_L2,5)
 if np.any(expansion) and float(distance[expansion].max())>6:raise ValueError('Authored coverage expands beyond 1.5 native pixels')
 # Original layers can leave a one-pixel inter-layer gap outside the
 # foreground's own mask. Fill only this narrow static boundary neighbourhood.
 vicinity=cv2.dilate((native>0).astype(np.uint8),cv2.getStructuringElement(cv2.MORPH_ELLIPSE,(9,9)))!=0
 support=vicinity&(alpha<255)&(np.repeat(np.repeat(bg,4,0),4,1)==0)
 base_record=records[back['key']]
 base_material=np.array(Image.open(a.baseline/base_record['file']).convert('RGB'))
 background=np.zeros((h*4,w*4,3),np.uint8);base_columns=base_material.shape[1]//68
 for i,(x,y) in enumerate(back['tiles']):
  ax=i%base_columns*68+2;ay=i//base_columns*68+2
  background[y*4:y*4+64,x*4:x*4+64]=base_material[ay:ay+64,ax:ax+64]
 valid=np.repeat(np.repeat(bg!=0,4,0),4,1)
 sample_distance,labels=cv2.distanceTransformWithLabels((~valid).astype(np.uint8),cv2.DIST_L2,5,labelType=cv2.DIST_LABEL_PIXEL)
 index=np.maximum(labels-1,0)
 visible=background[valid][index].astype(np.int16)
 if room=='M092':
  # Reconstruct only the narrow cutout boundary from actual adjacent HD pixels.
  # Do not extend into voids or alter the independently animated door layers.
  safe=sample_distance<=8
  alpha[~safe]=native[~safe]
  expansion=(native==0)&(alpha>0)
  support=(alpha<255)&((native!=0)|(alpha>0))&~valid&safe
  generated=visible.astype(np.uint8)
  master=a.baseline/base_record['file']
  details=dict(method='nearest original HD background in a two-native-pixel band',source_background=str(master),source_background_sha256=sha(master),max_sampling_distance=float(sample_distance[support].max()))
 else:
  master=a.assets/'corrections/issue-56'/f'{room}-floor-underlay-v1.png'
  original=np.array(Image.open(a.assets/'composites'/f'{room}-g00-cbase-p00-hd.png').convert('RGB'))
  generated=np.array(Image.open(master).convert('RGB').resize((w*4,h*4),Image.Resampling.LANCZOS))
  # Align only untouched room features. The removed foreground is excluded.
  match_mask=cv2.resize((1-cv2.dilate(fg,np.ones((7,7),np.uint8)))*255,(w*2,h*2),interpolation=cv2.INTER_NEAREST)
  sift=cv2.SIFT_create(nfeatures=6000)
  kp1,d1=sift.detectAndCompute(cv2.cvtColor(cv2.resize(generated,(w*2,h*2)),cv2.COLOR_RGB2GRAY),match_mask)
  kp2,d2=sift.detectAndCompute(cv2.cvtColor(cv2.resize(original,(w*2,h*2)),cv2.COLOR_RGB2GRAY),match_mask)
  matches=[m for m,n in cv2.BFMatcher().knnMatch(d1,d2,k=2) if m.distance<0.7*n.distance]
  src=np.float32([kp1[m.queryIdx].pt for m in matches])*2
  dst=np.float32([kp2[m.trainIdx].pt for m in matches])*2
  matrix,inliers=cv2.estimateAffinePartial2D(src,dst,method=cv2.RANSAC,ransacReprojThreshold=3)
  if matrix is None or int(inliers.sum())<40:raise ValueError(f'{room}: insufficient registration')
  scale=float(np.hypot(matrix[0,0],matrix[0,1]));assert 0.95<scale<1.05
  residual=np.linalg.norm(cv2.transform(src[None],matrix)[0]-dst,axis=1)[inliers[:,0]!=0]
  generated=cv2.warpAffine(generated,matrix,(w*4,h*4),flags=cv2.INTER_LANCZOS4)
  adjacent=(cv2.dilate(fg,np.ones((7,7),np.uint8))!=0)&(fg==0)&(bg!=0)
  adjacent=np.repeat(np.repeat(adjacent,4,0),4,1)
  gain=np.median((original[adjacent].astype(float)+8)/(generated[adjacent].astype(float)+8),axis=0)
  gain=np.clip(gain,0.8,1.2);generated=np.uint8(np.clip(np.rint(generated*gain),0,255))
  # Preserve the *actual pack's* floor/shadow continuity at the old cutout.
  # Transfer its local boundary colour into the generated hidden floor; a
  # global image gain cannot preserve the baked contact shadow at this edge.
  prior=generated[valid][index].astype(np.int16)
  void=np.all(generated==0,axis=2)
  generated=np.uint8(np.clip(generated.astype(np.int16)+visible-prior,0,255));generated[void]=0
  details=dict(registration=matrix.tolist(),inliers=int(inliers.sum()),residual_p95=float(np.percentile(residual,95)),gain=gain.tolist(),generated_master=str(master),generated_sha256=sha(master),local_boundary_colour_transfer=True)
 r=records[t['key']]
 material=np.array(Image.open(a.baseline/r['file']).convert('RGBA'))
 # WebP material is one plane; a combined PNG contains both.
 reference=np.array(Image.open(a.baseline/r['reference_file']).convert('RGBA'))
 material=material[:reference.shape[0]].copy();before_rgb=material[:,:,:3].copy();columns=material.shape[1]//68
 foreground=np.zeros((h*4,w*4,3),np.uint8)
 for i,(x,y) in enumerate(t['tiles']):
  ax=i%columns*68+2;ay=i//columns*68+2
  foreground[y*4:y*4+64,x*4:x*4+64]=material[ay:ay+64,ax:ax+64,:3]
 _,labels=cv2.distanceTransformWithLabels((native==0).astype(np.uint8),cv2.DIST_L2,5,labelType=cv2.DIST_LABEL_PIXEL)
 nearest=foreground[native!=0][np.maximum(labels-1,0)]
 allowed=np.zeros(material.shape[:2],bool)
 for i,(x,y) in enumerate(t['tiles']):
  ax=i%columns*68;ay=i//columns*68
  tile=material[ay+2:ay+66,ax+2:ax+66].copy();tile[:,:,3]=alpha[y*4:y*4+64,x*4:x*4+64]
  extra=expansion[y*4:y*4+64,x*4:x*4+64]
  tile[extra,:3]=nearest[y*4:y*4+64,x*4:x*4+64][extra]
  padded=np.pad(tile,((2,2),(2,2),(0,0)),mode='edge');allowed[ay:ay+68,ax:ax+68]=np.pad(extra,2,mode='edge')
  cell=material[ay:ay+68,ax:ax+68];cell[:,:,3]=padded[:,:,3]
  cell[:,:,:3]=np.where(allowed[ay:ay+68,ax:ax+68,None],padded[:,:,:3],cell[:,:,:3])
 assert not np.any(np.any(before_rgb!=material[:,:,:3],axis=2)&~allowed)
 combined=np.concatenate((material,reference),axis=0)
 path=a.out/(t['key']+'.png');path.unlink(missing_ok=True);Image.fromarray(combined).save(path)
 old=a.out/r['file']
 if old!=path:old.unlink(missing_ok=True)
 r.update(file=path.name,atlas_sha256=sha(path),pixel_sha256=hashlib.sha256(combined.tobytes()).hexdigest())
 marker=a.out/(t['key']+'-matte.bin');marker.unlink(missing_ok=True);marker.write_bytes(b'PEM1')
 manifest.setdefault('extras',[]).append(dict(file=marker.name,sha256=sha(marker)))
 cells=[(x,y) for y in range(0,h,16) for x in range(0,w,16) if np.any(support[y*4:y*4+64,x*4:x*4+64])]
 assert cells
 key=(hashlib.sha256(room.encode()+generated.tobytes()+support.tobytes()).hexdigest() if room=='M092' else hashlib.sha256((room+sha(master)+'underlay-v1').encode()).hexdigest())[:16];assert key not in records
 cols=min(16,len(cells));rows=(len(cells)+cols-1)//cols
 under=np.zeros((rows*68,cols*68,4),np.uint8)
 for i,(x,y) in enumerate(cells):
  tile=np.zeros((64,64,4),np.uint8);tile[:,:,:3]=generated[y*4:y*4+64,x*4:x*4+64]
  tile[:,:,3]=support[y*4:y*4+64,x*4:x*4+64]*255
  ax=i%cols*68;ay=i//cols*68;under[ay:ay+68,ax:ax+68]=np.pad(tile,((2,2),(2,2),(0,0)),mode='edge')
 ref=np.zeros_like(under);image=a.out/(key+'.png');refpath=a.out/(key+'.ref.png')
 image.unlink(missing_ok=True);refpath.unlink(missing_ok=True)
 Image.fromarray(np.concatenate((under,ref),axis=0)).save(image);Image.fromarray(ref).save(refpath)
 manifest['atlases'].append(dict(key=key,file=image.name,atlas_sha256=sha(image),reference_file=refpath.name,reference_sha256=sha(refpath),room=room,group=0,presentation='underlay'))
 descriptor=struct.pack('<4sI',b'PEW1',1)+struct.pack('<IIQiiii',0,len(cells),int(key,16),0,w,0,h)
 descriptor+=b''.join(struct.pack('<hh',x,y) for x,y in cells)
 path=a.out/(geometry+'-under.bin');path.unlink(missing_ok=True);path.write_bytes(descriptor)
 manifest.setdefault('extras',[]).append(dict(file=path.name,sha256=sha(path)))
 audit.append(dict(room=room,geometry=geometry,foreground=t['key'],underlay=key,cells=len(cells),pixels=int(support.sum()),matte="static-authored-long-edges",**details,original_foreground_rgb_exact=True,expanded_pixels=int(expansion.sum()),native_reference_exact=True))
manifest['development_candidate']='issue-56-underlays-v2' if 'M092' in selected else 'issue-56-underlays-v1';manifest['image_format']='mixed-png-webp'
(a.out/'manifest.json').unlink();(a.out/'manifest.json').write_text(json.dumps(manifest,separators=(',',':')))
audit_path.write_text(json.dumps(audit,indent=2))
print(json.dumps(audit,indent=2))
