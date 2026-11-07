"""Offline phase previews with source tile depth, coverage, STP and blend mode.
Does not include live actors, mirrors, scripts or camera movement.
"""
from pathlib import Path
from PIL import Image
import json,numpy as np,argparse
R=Path(__file__).resolve().parent;p=argparse.ArgumentParser();p.add_argument('--root',type=Path,default=R);p.add_argument('--capture-json',type=Path,help='Use the exact active entry keys saved with an in-game screenshot');p.add_argument('--room');p.add_argument('--synchronized',action='store_true');p.add_argument('--group',type=int,default=0);p.add_argument('--controller',type=int);p.add_argument('--pack',default='pack');p.add_argument('--output',default='composites');p.add_argument('--shards',type=int,default=1);p.add_argument('--shard',type=int,default=0);a=p.parse_args();R=a.root.resolve();capture=json.loads(a.capture_json.read_text()) if a.capture_json else None
if capture:a.room=f"M{capture['game']['room']:03}";a.group=capture['background']['active_group']
assert 0<=a.shard<a.shards;data=json.loads((R/'tasks.json').read_text());vs=json.loads((R/'views.json').read_text());out=R/a.output;out.mkdir(exist_ok=True);records=[]
for vi,view in enumerate(vs):
 if vi%a.shards!=a.shard:continue
 room,g=view['room'],view['group']
 if a.room and (a.room!=room or a.group!=g):continue
 ts=[t for t in data['tasks'] if (t['room'],t['group'])==(room,g)];desc=next(x for x in data['rooms'] if x['room']==room);controls=view['controllers'];initial={e['index'] for e in desc['entries'] if e['group']==g and e['flags']&2}
 for c in controls:
  if c['slots']:initial.add(c['slots'][0]['entry'])
 sets=[(None,0,initial.copy())]
 # Script-switched layers have no animation controller. Show each additional
 # layer in native context as an art diagnostic, without inventing script order.
 controlled={s['entry'] for c in controls for s in c['slots']}
 for t in ts:
  if t['entry'] not in initial and t['entry'] not in controlled:
   sets.append(('entry'+str(t['entry']),0,initial|{t['entry']}))
 for c in controls:
  if a.controller is not None and a.controller!=c['id']:continue
  for phase,slot in enumerate(c['slots']):
   visible=initial-{x['entry'] for x in c['slots']};visible.add(slot['entry']);sets.append((c['id'],phase,visible))
 if a.synchronized and room in ('M038','M039'):
  lengths={len(c['slots'])for c in controls};assert len(lengths)==1
  for phase in range(next(iter(lengths))):
   visible=initial-{s['entry']for c in controls for s in c['slots']}
   visible.update(c['slots'][phase]['entry']for c in controls);sets.append(('sync',phase,visible))
 if capture:
  keys=set(capture['background']['active_entry_keys']);selected={t['key'] for t in ts if t['key'] in keys};assert selected==keys,'Capture keys missing from this source manifest'
  sets=[('capture',0,{t['entry'] for t in ts if t['key'] in keys})]
 native={t['key']:np.load(R/'refs'/t['ref']) for t in ts};hd={};authored={}
 for t in ts:
  path=R/a.pack/(t['key']+'.png');path=path if path.exists() else R/a.pack/(t['key']+'.bmp')
  if not path.exists():path=R/a.pack/(t['key']+'.webp')
  if path.exists():
   hd[t['key']]=np.array(Image.open(path).convert('RGBA'))
   top=hd[t['key']] if path.suffix=='.webp' else hd[t['key']][:hd[t['key']].shape[0]//2]
   authored[t['key']]=np.any(top[:,:,3]!=255)
 w,h=view['canvas_size']
 for cid,phase,visible in sets:
  selected=[t for t in ts if t['entry'] in visible]
  if not all(t['key'] in hd for t in selected):continue
  tiles=[]
  for t in selected:
   for index,(xy,meta) in enumerate(zip(t['tiles'],t['tile_meta'])):tiles.append((meta,t,index,xy))
  tiles.sort(key=lambda x:(x[0]['depth'],x[0]['ordinal']),reverse=True)
  for enhanced in [False,True]:
   scale=4 if enhanced else 1;dest=np.zeros((h*scale,w*scale,3),np.float32)
   for meta,t,index,(x,y) in tiles:
    ref=native[t['key']][index];src=np.stack([(ref&31)*255//31,((ref>>5)&31)*255//31,((ref>>10)&31)*255//31],axis=-1).astype(np.float32)
    if enhanced:
     cols=min(16,len(native[t['key']]));ax=index%cols*68+2;ay=index//cols*68+2;material=hd[t['key']][ay:ay+64,ax:ax+64];src=material[:,:,:3].astype(np.float32)
    mask=np.repeat(np.repeat(ref!=0,scale,0),scale,1);stp=np.repeat(np.repeat((ref&0x8000)!=0,scale,0),scale,1);d=dest[y*scale:(y+16)*scale,x*scale:(x+16)*scale]
    if meta['semi']:
     mode=meta['mode'];blend=(src+d)/2 if mode==0 else np.minimum(255,src+d) if mode==1 else np.maximum(0,d-src) if mode==2 else np.minimum(255,d+src/4);src=np.where(stp[...,None],blend,src)
    if enhanced and not meta['semi'] and authored[t['key']]:
     alpha=material[:,:,3:4].astype(np.float32)/255
     d[:]=src*alpha+d*(1-alpha)
    else:d[mask]=src[mask]
   name=f'{room}-g{g:02d}-c{cid if cid is not None else "base"}-p{phase:02d}-{"hd" if enhanced else "native"}.png';Image.fromarray(np.clip(dest,0,255).astype('uint8')).save(out/name)
  records.append({'room':room,'group':g,'controller':cid,'phase':phase,'visible_entries':sorted(visible)})
if a.room and (out/'manifest.json').exists():
 old=json.loads((out/'manifest.json').read_text())['composites'];records=[x for x in old if (x['room'],x['group'])!=(a.room,a.group)]+records
(out/('manifest.json'if a.shards==1 else f'manifest-shard-{a.shard}.json')).write_text(json.dumps({'note':__doc__,'composites':records},indent=2));print(len(records),'paired source/HD phase previews')
