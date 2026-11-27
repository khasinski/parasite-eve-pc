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
p.add_argument('--coherent-static',action='store_true',help='Reviewed M097 only: use one registered HD material for static layers, base and underlay; preserve controlled regions')
a=p.parse_args()
if a.coherent_static and set(a.rooms.split(','))!={'M097'}:p.error('--coherent-static requires --rooms M097')
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
def coherent_static_room():
 # A cutout mask cannot repair a different material baked into its underlay.
 # Reuse one registered image at identical room coordinates, and retain the
 # original controller neighbourhoods in every plane. This is art-only and
 # explicitly limited to the reviewed M097; it is not an automatic remaster.
 root=a.assets;base=a.baseline;out=a.out
 def extra(path):
  entry=dict(file=path.name,sha256=sha(path))
  for old in manifest.setdefault('extras',[]):
   if old['file']==path.name:old.update(entry);return
  manifest['extras'].append(entry)
 ts=[t for t in json.loads((root/'tasks.json').read_text())['tasks'] if t['room']=='M097' and t['group']==0];w,h=ts[0]['size'];selected=[t for t in ts if t['entry'] in [0,1,2,3,4,5,6,11]]
 if len(selected)!=8 or any(t.get('control') is not None or t.get('controls') or any(m.get('semi',False) for m in t['tile_meta']) for t in selected):raise ValueError('Expected eight opaque static M097 layers')
 def read(key):
  p=base/records[key]['file'];image=np.array(Image.open(p).convert('RGBA'));return image[:image.shape[0]//2] if p.suffix.lower()!='.webp' else image

 def unpack(t):
  mat=read(t['key']);cols=mat.shape[1]//68;world=np.zeros((h*4,w*4,4),np.uint8);native=np.zeros((h,w),np.uint16)
  for i,((x,y),ref) in enumerate(zip(t['tiles'],np.load(root/'refs'/t['ref']))):
   ax=i%cols*68+2;ay=i//cols*68+2;world[y*4:y*4+64,x*4:x*4+64]=mat[ay:ay+64,ax:ax+64];native[y:y+16,x:x+16]=ref
  return world,native

 def save(t,world):
  mat=read(t['key']).copy();cols=mat.shape[1]//68
  for i,(x,y) in enumerate(t['tiles']):
   ax=i%cols*68;ay=i//cols*68;mat[ay:ay+68,ax:ax+68]=np.pad(world[y*4:y*4+64,x*4:x*4+64],((2,2),(2,2),(0,0)),mode='edge')
  ref=np.array(Image.open(base/(t['key']+'.ref.png')).convert('RGBA'));(out/(t['key']+'.webp')).unlink(missing_ok=True);p=out/(t['key']+'.png');p.unlink(missing_ok=True);Image.fromarray(np.concatenate((mat,ref),axis=0)).save(p)
  records[t['key']].update(file=p.name,atlas_sha256=sha(p),pixel_sha256=hashlib.sha256(np.concatenate((mat,ref),axis=0).tobytes()).hexdigest(),compressed_bytes=p.stat().st_size+(out/(t['key']+'.ref.png')).stat().st_size)

 anchor=next(v for v in json.loads((root/'anchor-views.json').read_text()) if v['room']=='M097' and v['group']==0);aid=anchor['id'];v=next(v for v in json.loads((root/'views.json').read_text()) if v['room']=='M097' and v['group']==0);aw,ah=v['active_size'];x0,y0,x1,y1=v['active_bbox']
 alignment=json.loads((root/'anchor-alignment.json').read_text())['anchors'];registration=next(x for x in alignment if x['id']==aid)
 if registration['status']!='global affine registered':raise ValueError('M097 requires a reviewed registered anchor')
 ai=np.array(Image.open(root/'registered-anchors'/(aid+'.png')).convert('RGB').resize((aw*4,ah*4),Image.Resampling.LANCZOS));sr=np.array(Image.open(root/'source-sr-full'/(aid+'.png')).convert('RGB'));illum=np.clip((cv2.GaussianBlur(sr.astype(np.float32),(0,0),16)+8)/(cv2.GaussianBlur(ai.astype(np.float32),(0,0),16)+8),.4,2.5);ai=np.uint8(np.clip(np.rint(ai*illum),0,255));canvas=np.zeros((h*4,w*4,3),np.uint8);canvas[y0*4:y1*4,x0*4:x1*4]=ai
 dynamic=np.zeros((h,w),np.uint8)
 for t in ts:
  if t['control'] is None and not t['controls']:continue
  for (x,y),ref in zip(t['tiles'],np.load(root/'refs'/t['ref'])):dynamic[y:y+16,x:x+16]|=(ref!=0).astype(np.uint8)
 dynamic=cv2.dilate(dynamic,np.ones((7,7),np.uint8));dynamic=np.repeat(np.repeat(dynamic,4,0),4,1)!=0
 fg=np.zeros((h*4,w*4),np.uint8)
 for t in selected:
  world,native=unpack(t);mask=np.repeat(np.repeat((native!=0).astype(np.uint8),4,0),4,1);fg|=mask;world[~dynamic,:3]=canvas[~dynamic];save(t,world)
  p=out/(t['key']+'-matte.bin');p.unlink(missing_ok=True);p.write_bytes(b'PEM1');extra(p)
 back=next(t for t in ts if t['entry']==16);background,bg=unpack(back);background[~dynamic,:3]=canvas[~dynamic];save(back,background)
 geometry='b1c7435335ee7113';descriptor=(base/(geometry+'-under.bin')).read_bytes();group,count,key,left,right,top,bottom=struct.unpack_from('<IIQiiii',descriptor,8);cells=[struct.unpack_from('<hh',descriptor,40+i*4) for i in range(count)];old=read(f'{key:016x}');cols=old.shape[1]//68;under=np.zeros((h*4,w*4,4),np.uint8)
 for i,(x,y) in enumerate(cells):
  ax=i%cols*68+2;ay=i//cols*68+2;under[y*4:y*4+64,x*4:x*4+64]=old[ay:ay+64,ax:ax+64]
 fill=(cv2.dilate(fg,np.ones((17,17),np.uint8))!=0)&~dynamic;under[fill,:3]=canvas[fill];under[fill,3]=255
 cells=[(x,y) for y in range(0,h,16) for x in range(0,w,16) if under[y*4:y*4+64,x*4:x*4+64,3].any()];cols=min(16,len(cells));rows=(len(cells)+cols-1)//cols;atlas=np.zeros((rows*68,cols*68,4),np.uint8)
 for i,(x,y) in enumerate(cells):atlas[i//cols*68:i//cols*68+68,i%cols*68:i%cols*68+68]=np.pad(under[y*4:y*4+64,x*4:x*4+64],((2,2),(2,2),(0,0)),mode='edge')
 p=out/(f'{key:016x}'+'.png');p.unlink(missing_ok=True);(out/(f'{key:016x}'+'.webp')).unlink(missing_ok=True);Image.fromarray(np.concatenate((atlas,np.zeros_like(atlas)),axis=0)).save(p)
 p=out/(geometry+'-under.bin');p.unlink();p.write_bytes(struct.pack('<4sI',b'PEW1',1)+struct.pack('<IIQiiii',group,len(cells),key,left,right,top,bottom)+b''.join(struct.pack('<hh',x,y) for x,y in cells))

 refpath=out/(f'{key:016x}'+'.ref.png');refpath.unlink(missing_ok=True);Image.fromarray(np.zeros_like(atlas)).save(refpath)
 image=out/(f'{key:016x}'+'.png');records[f'{key:016x}'].update(file=image.name,atlas_sha256=sha(image),reference_sha256=sha(refpath),pixel_sha256=hashlib.sha256(np.concatenate((atlas,np.zeros_like(atlas)),axis=0).tobytes()).hexdigest(),compressed_bytes=image.stat().st_size+refpath.stat().st_size)
 extra(out/(geometry+'-under.bin'))
 manifest['development_candidate']='M097-coherent-static-v1';manifest['image_format']='mixed-png-webp'
 path=out/'manifest.json';path.unlink()
 manifest['image_bytes']=sum(p.stat().st_size for p in out.iterdir() if p.is_file())
 manifest['distribution_bytes']=manifest['image_bytes']
 for _ in range(16):
  data=json.dumps(manifest,separators=(',',':')).encode();total=manifest['image_bytes']+len(data)
  if total==manifest['distribution_bytes']:break
  manifest['distribution_bytes']=total
 else:raise ValueError('Manifest byte count did not converge')
 path.write_bytes(data)
 audit_path.write_text(json.dumps(dict(room='M097',method='One registered HD source for all opaque static layers, base and underlay',anchor=aid,registered_sha256=sha(root/'registered-anchors'/(aid+'.png')),lighting_source_sha256=sha(root/'source-sr-full'/(aid+'.png')),source_manifest_sha256=sha(base/'manifest.json'),static_entries=[t['entry'] for t in selected],dynamic_context_protected_hd_pixels=int(dynamic.sum()),native_room_references_unchanged=True),indent=2))

if a.coherent_static:
 coherent_static_room()
 print(audit_path)
 raise SystemExit(0)

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
  details=dict(method='nearest original HD background in a two-native-pixel band',source_background=str(master),source_background_sha256=sha(master),max_sampling_distance=float(sample_distance[support].max(initial=0)))
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
 # Other static foregrounds have the same one-native-pixel inter-layer
 # gaps. Reconstruct a bounded gutter for every opaque static silhouette,
 # not just the previously reported counter/microscope. Never sample a
 # transparent/opaque-black placeholder, alter actors or bake a moving layer.
 static_fg=np.zeros((h,w),np.uint8)
 for layer in tasks:
  if layer['room']!=room or layer['group']!=0 or layer['entry']==base_entry:continue
  if layer.get('control') is not None or layer.get('controls'):continue
  for i,((x,y),ref) in enumerate(zip(layer['tiles'],np.load(a.assets/'refs'/layer['ref']))):
   if not layer.get('tile_meta',[{}]*len(layer['tiles']))[i].get('semi',False):
    static_fg[y:y+16,x:x+16]|=(ref!=0).astype(np.uint8)
 background_words=np.zeros((h,w),np.uint16)
 for (x,y),ref in zip(back['tiles'],np.load(a.assets/'refs'/back['ref'])):
  background_words[y:y+16,x:x+16]=ref
 edge_valid=np.repeat(np.repeat((background_words!=0)&(background_words!=0x8000),4,0),4,1)
 extra_support=np.zeros_like(support)
 if np.any(edge_valid):
  edge_distance,edge_labels=cv2.distanceTransformWithLabels((~edge_valid).astype(np.uint8),cv2.DIST_L2,5,labelType=cv2.DIST_LABEL_PIXEL)
  near_static=cv2.dilate(np.repeat(np.repeat(static_fg,4,0),4,1),cv2.getStructuringElement(cv2.MORPH_ELLIPSE,(9,9)))!=0
  extra_support=near_static&np.repeat(np.repeat(bg==0,4,0),4,1)&(edge_distance<=8)&~support
  generated[extra_support]=background[edge_valid][np.maximum(edge_labels[extra_support]-1,0)]
  support|=extra_support
  # An original opaque-black outline in the base would cover the gutter.
  # Continue its adjacent HD floor colour only within the same bounded strip.
  black_edge=near_static&np.repeat(np.repeat(background_words==0x8000,4,0),4,1)&(edge_distance<=8)&(background.max(axis=2)<24)
  if np.any(black_edge):
   fixed_background=background.copy()
   fixed_background[black_edge]=background[edge_valid][np.maximum(edge_labels[black_edge]-1,0)]
   base_reference=np.array(Image.open(a.baseline/base_record['reference_file']).convert('RGBA'))
   base_rgba=np.array(Image.open(a.baseline/base_record['file']).convert('RGBA'))[:base_reference.shape[0]].copy()
   for i,(x,y) in enumerate(back['tiles']):
    if not np.any(black_edge[y*4:y*4+64,x*4:x*4+64]):continue
    ax=i%base_columns*68;ay=i//base_columns*68
    tile=base_rgba[ay+2:ay+66,ax+2:ax+66].copy()
    tile[:,:,:3]=fixed_background[y*4:y*4+64,x*4:x*4+64]
    base_rgba[ay:ay+68,ax:ax+68]=np.pad(tile,((2,2),(2,2),(0,0)),mode='edge')
   path=a.out/(back['key']+'.png');path.unlink(missing_ok=True)
   combined_base=np.concatenate((base_rgba,base_reference),axis=0);Image.fromarray(combined_base).save(path)
   old=a.out/base_record['file']
   if old!=path:old.unlink(missing_ok=True)
   base_record.update(file=path.name,atlas_sha256=sha(path),pixel_sha256=hashlib.sha256(combined_base.tobytes()).hexdigest())
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
 audit.append(dict(room=room,geometry=geometry,foreground=t['key'],underlay=key,cells=len(cells),pixels=int(support.sum()),matte="static-authored-long-edges",**details,original_foreground_rgb_exact=True,expanded_pixels=int(expansion.sum()),native_reference_exact=True,static_edge_gutter_pixels=int(extra_support.sum()),static_edge_gutter_max_hd=8))
manifest['development_candidate']='issue-56-underlays-v2' if 'M092' in selected else 'issue-56-underlays-v1';manifest['image_format']='mixed-png-webp'
(a.out/'manifest.json').unlink();(a.out/'manifest.json').write_text(json.dumps(manifest,separators=(',',':')))
audit_path.write_text(json.dumps(audit,indent=2))
print(json.dumps(audit,indent=2))
