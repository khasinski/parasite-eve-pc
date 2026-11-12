// Drives only an explicitly injected preview build through the real pad decoder.
import {spawn, execFileSync} from 'node:child_process';
import {mkdirSync, writeFileSync, readFileSync, copyFileSync} from 'node:fs';
import {resolve, join} from 'node:path';
import {createInterface} from 'node:readline';
import assert from 'node:assert/strict';
const [exeArg, discArg, exporterArg, outputArg, ...rootsArg] = process.argv.slice(2);
if (!outputArg) throw Error('Usage: node test_headless.mjs GAME DISC EXPORTER OUTPUT [ROOT ...]');
const executable=resolve(exeArg), disc=resolve(discArg), output=resolve(outputArg);
const catalog=JSON.parse(execFileSync(resolve(exporterArg),{encoding:'utf8'}));
const pages=new Map(catalog.pages.map(p=>[p.id,p])), visited=new Set(), findings=new Set();
const roots=rootsArg.length?rootsArg:['preview'];
const manual=process.env.PE_BP_UI_MANUAL_ENTRY==='1', f1Test=process.env.PE_BP_UI_F1_TEST||'';
const button={up:16,down:64,left:128,right:32,confirm:16384,back:8192,detailNext:2048,detailPrev:1024,help:32768,menu:4096};
const results=[];
mkdirSync(output,{recursive:true});
for(const root of roots){
 const directory=join(output,root);mkdirSync(directory,{recursive:true});
 mkdirSync(join(directory,'config'),{recursive:true});
 // Enable the host in this private config only. PE_MOD_LIST is empty.
 writeFileSync(join(directory,'config','pe_mods.ini'),`Manage mods/All mods=${manual||f1Test?0:1}\n`);
 const timeline=[[10000,8],[10010,0],[11000,16],[11010,0],[11500,16384],[11510,0]];
 for(const tick of [17000,17600,18200,18800,19400,20000,20600,21200,21800])timeline.push([tick,16384],[tick+10,0]);
 for(const [tick,mask] of [[23000,8],[23600,8],[24200,16384],[24800,16384]])timeline.push([tick,mask],[tick+10,0]);
 const args=['--disc',disc,'--headless','--frame-limit','200000','--control-at','25000','--dump-frame',join(directory,'frame.ppm')];
 for(const [tick,mask]of timeline)args.push('--pad-at',`${tick}:${mask}`);
 const child=spawn(executable,args,{cwd:directory,env:{...process.env,PE_CONFIG_HOME:join(directory,'config'),PE_BP_UI_PAGE:manual||f1Test?'':root,SDL_AUDIODRIVER:'dummy',PSYZ_AUDIO_CLOCK:'video'},stdio:['pipe','pipe','pipe'],windowsHide:true});
 let ui=null, focus=null, baseline=null, serial=0, log='', stopped=false;
 const queue=[], waiting=[];
 function feed(line){log+=line+'\n';if(line.includes('BP_UI_TEXT_OVERFLOW')||line.includes('BP_UI_OPEN_FAILED')||line.includes('BP_UI_HIGHLIGHT_ERROR')||line.includes('BP_UI_PACKET_PRESSURE'))findings.add(line);
  if(line.startsWith('BP_UI_FOCUS ')){const m=/x=(\d+) y=(\d+) w=(\d+) h=(\d+)/.exec(line);if(m)focus={x:+m[1],y:+m[2],w:+m[3],h:+m[4]}}
  if(line.startsWith('BP_UI_STATE ')){const m=/open=(\d+) page=(\S+) row=(\S+) selected=(\d+) top=(\d+) detail=(\d+) pages=(\d+) pending=(\d+) help=(\d+) notice=(.*)/.exec(line);if(m)ui={open:+m[1],page:m[2],row:m[3],selected:+m[4],top:+m[5],detail:+m[6],detailPages:+m[7],pending:+m[8],help:+m[9],notice:m[10]}}
  if(line.startsWith('{"event":"control"')){const state=JSON.parse(line);const event={state,ui:ui?{...ui}:null};if(waiting.length)waiting.shift()(event);else queue.push(event)}
 }
 createInterface({input:child.stdout}).on('line',feed);createInterface({input:child.stderr}).on('line',feed);
 child.on('exit',()=>{stopped=true;while(waiting.length)waiting.shift()(null)});
 async function next(){if(queue.length)return queue.shift();if(stopped)throw Error('Game exited before checkpoint');return await new Promise((ok,bad)=>{const timer=setTimeout(()=>bad(Error('Headless checkpoint timed out')),60000);waiting.push(v=>{clearTimeout(timer);v?ok(v):bad(Error('Game exited before checkpoint'))})})}
 function send(command){child.stdin.write(command+'\n')}
 function unchanged(state){if(baseline){assert.deepEqual(state.character,baseline.character,'UI modified character state');assert.deepEqual(state.inventory,baseline.inventory,'UI modified inventory')}}
 async function advance(frames,mask=0){send(`advance ${frames} ${mask}`);const event=await next();unchanged(event.state);return event}
 async function press(key){await advance(8,button[key]);return await advance(16)}
 function expect(page,index,pending=0){assert(ui,`No UI state for ${page}`);assert.equal(ui.open,1);assert.equal(ui.page,page);if(index!==undefined)assert.equal(ui.selected,index);assert.equal(ui.pending,pending)}
 function capture(label){
  if(ui?.open&&!ui.pending&&focus?.w){
   const ppm=readFileSync(join(directory,'frame.ppm')),head=/^P6\s+(\d+)\s+(\d+)\s+255\s/.exec(ppm.subarray(0,100).toString('ascii'));
   assert(head,'Capture must be P6');const width=+head[1],height=+head[2];
   // The game's own list cursor (Draw_AllocColorTriGradient): an opaque two-pixel light bevel
   // along its top. (Its bottom shadow is translucent, so it is not a reliable signal.)
   const edge=(y,ok)=>{let hit=0,total=0;for(let x=focus.x+2;x<focus.x+focus.w-4;x++){
    assert(x>=0&&y>=0&&x<width&&y<height,'Focus outside display');const at=head[0].length+3*(y*width+x);
    if([0,1,2].every(c=>ok(ppm[at+c])))hit++;total++}return hit/total};
   const top=edge(focus.y,v=>v>=90),second=edge(focus.y+1,v=>v>=90);
   assert(top>0.8&&second>0.8,'Selected-row cursor missing on '+label+' (bevel '+top.toFixed(2)+'/'+second.toFixed(2)+')');
  }
  copyFileSync(join(directory,'frame.ppm'),join(directory,`${String(++serial).padStart(3,'0')}-${label.replaceAll(/[^a-z0-9.-]/gi,'_')}.ppm`))}
 async function visit(id){
  expect(id);if(visited.has(id))return;visited.add(id);const page=pages.get(id);capture(id);
  for(let index=0;index<page.rows.length;index++){
   const row=page.rows[index];if(row.kind===1)continue;
   let guard=page.rows.length+1;
   while(ui.selected!==index&&guard--)await press(ui.selected>index?'up':'down');expect(id,index);
   capture(`${id}-${row.id}`);
   const detailPages=ui.detailPages;
   await press('help');assert.equal(ui.help,1);capture(`${id}-${row.id}-help`);
   for(let d=1;d<detailPages;d++){await press('detailNext');assert.equal(ui.detail,d);capture(`${id}-${row.id}-detail${d}`)}
   for(let d=detailPages-1;d>0;d--)await press('detailPrev');
   await press('help');assert.equal(ui.help,0);
   await press('confirm');
   if(row.state){expect(id,index);assert(ui.notice);}
   else if(row.action===1){expect(row.target);await visit(row.target);await press('back');expect(id,index);}
   else if(row.action===2){
    expect(id,index,1);capture(`${id}-${row.id}-confirm`);
    await press('confirm');expect(id,index);assert.match(ui.notice,/Cancelled/);
    await press('confirm');expect(id,index,1);await press('back');expect(id,index);
    await press('confirm');await press('left');await press('confirm');expect(id,index);assert.match(ui.notice,/nothing spent/);
   }else expect(id,index);
  }
  const last=ui.selected;await press('down');expect(id,last);
  const first=page.rows.findIndex(r=>r.kind!==1);
  while(ui.selected>first)await press('up');await press('up');expect(id,first);
  for(const [key,target,undo]of [['left',page.previous,'right'],['right',page.next,'left']]){
   if(!catalog.tabs.some(t=>t.page===id)||!target||visited.has(target))continue;await press(key);expect(target);await visit(target);await press(undo);expect(id);
  }
 }
 try{
  let initial=await next();console.log(`${root}: boot checkpoint scene=${initial.state.scene} menu=${JSON.stringify(initial.state.menu)}`);
  // Finish the unmodified opening dialogue before asking for Aya's menu.
  for(let attempt=0;!ui&&attempt<80;attempt++){
   if(manual&&initial.state.menu){
    capture('normal-menu-before');await press('up');capture('normal-menu-preview-entry');initial=await press('confirm');break;
   }
   const speaking=initial.state.dialogue?.some(d=>d.state!==0);
   if(!speaking&&!(initial.state.move_lock&1))initial=await press('menu');
   else {await advance(10,button.confirm);initial=await advance(120);}
   if(attempt%10===9)console.log(`${root}: opening dialogue step ${attempt+1}, lock=${initial.state.move_lock}`);
  }
  expect(root);assert.match(log,/BP_UI_ACCESS registered/);if(manual||f1Test)assert.match(log,/master=0/);if(f1Test)assert.match(log,/BP_UI_F1_ACTION tested=/);baseline=(await advance(24)).state;
  if(process.env.PE_BP_UI_ENTRY_ONLY==='1')capture(root);else await visit(root);const returned=await press('back');assert.equal(ui.open,0);
  assert.equal(returned.state.menu.mode,2);assert.equal(returned.state.menu.selection,0);
  capture(`${root}-returned`);
  send('quit');await new Promise(ok=>child.once('exit',ok));
  results.push({root,captures:serial,unchangedCharacterAndInventory:true});
  console.log(`${root}: passed (${serial} captures)`);
 }finally{if(!stopped){child.kill();await new Promise(ok=>child.once('exit',ok))}writeFileSync(join(directory,'game.log'),log)}
}
const proof={passed:findings.size===0,pages:[...visited],results,findings:[...findings]};
writeFileSync(join(output,'proof.json'),JSON.stringify(proof,null,2));
assert.equal(findings.size,0,`Native UI findings: ${[...findings].slice(0,8).join('\n')}`);
console.log(`PASS: ${visited.size} native pages; controller navigation, details, Yes/No and unchanged game state.`);
