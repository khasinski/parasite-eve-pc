"""Keep recurring rooms/materials consistent and preserve text-heavy source art."""
from pathlib import Path
import json
R=Path(__file__).resolve().parent;jobs=json.loads((R/'anchor-jobs.json').read_text());byview={tuple(v):j for j in jobs for v in j['views']}
source_only={('M047',1),('M054',1),('M109',1),('M110',1),('M276',1),('M048',0),('M048',1),('M108',0),('M108',1)}
links={98:41,99:42,103:46,112:41,102:46,104:47,108:48,116:56,196:195,226:225,238:236,379:195,331:125,320:131,323:133,324:137,325:138,326:139,327:140,328:141,329:146,380:156,381:156}
for j in jobs:
 if any(tuple(v)in source_only for v in j['views']):
  j['source_only']=True
  if any(room in ('M048','M052','M108')for room,g in j['views']):j['source_only_reason']='Generation or required material reference declined; retain original source with non-generative restoration.'
 for room,g in j['views']:
  number=int(room[1:]);parent=None
  if number in links:parent=(f'M{links[number]:03d}',0)
  if number in (51,100,101,107):parent=('M043',min(g,1))
  if number==109 and g==0:parent=('M054',0)
  if number==110 and g==0:parent=('M053',0)
  if number==192:parent=('M191',g)
  if number==241:parent=('M239',g)
  if number==269 and g==1:parent=('M269',0)
  if number==271 and g==1:parent=('M271',0)
  if number==285:parent=('M284',0)
  if parent in byview and byview[parent]['id']!=j['id']:
   j['reference']=byview[parent]['id'];j['reference_kind']='same place or building material reference; preserve target geometry and lighting'
  if number in (161,163):
   j.pop('reference_file',None)
   j['material_note']='Preserve the original irregular herringbone road layout exactly. Do not replace it with straight rows or use the rejected M162 road-v2 material reference. Source-material regions restore original road geometry consistently in every native layer.'
  if number in (254,371):j['reference_file']=str(R.parent/'hd-static/composites/M255-g00-cbase-p00-hd.png');j['material_note']='Same museum rotunda as the reference: keep coherent blue floor tile material and architecture finish. Target original camera, floor joints, bones, lighting and silhouettes remain absolute authority.'
  if number in (290,292,293,294):j['reference_file']=str(R.parent/'hd-static/composites/M291-g00-cbase-p00-hd.png');j['material_note']='Match the existing Chrysler marble/stone material family without changing the target source floor joints, medallion geometry, elevator details or light.'
  if number==195:
   j['literal_text']='Nov 1st, Jan 15th';j['material_note']='The tiny dates at the bottom of the red exhibition banner read Nov 1st, on the first line and Jan 15th on the second line. Keep all other banner text and geometry unchanged.'
  if number==94:j['literal_text']="SAM'S"
  if number==96:j['literal_text']='PHARMACY'
 # Explicit files have no generation dependency. A source-only board never
 # serves as a guessed generative material reference for another room.
 if j.get('reference_file'):j['reference']=None
 if j.get('reference') and next(x for x in jobs if x['id']==j['reference']).get('source_only'):j['reference']=None
# Detect reference cycles before launching any work.
lookup={j['id']:j for j in jobs}
for j in jobs:
 seen=set();q=j
 while q.get('reference'):
  assert q['id']not in seen,('Material reference cycle',q['id']);seen.add(q['id']);q=lookup[q['reference']]
(R/'anchor-jobs.json').write_text(json.dumps(jobs,indent=2));print(sum(not j.get('source_only')and not(R/'generated'/(j['id']+'.png')).exists()for j in jobs),'new OpenAI anchors;',sum(j.get('source_only',False)for j in jobs),'source-preserved boards/screens')
