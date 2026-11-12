// Real native card+companion cycle: invest -> QoL-only -> module off -> re-enable.
import {spawn} from 'node:child_process';import {createInterface} from 'node:readline';
import {mkdir,readFile,writeFile,readdir,cp,copyFile} from 'node:fs/promises';import {resolve,join} from 'node:path';import assert from 'node:assert/strict';
const [exe,disc,output,profile]=process.argv.slice(2).map(x=>resolve(x));await mkdir(output,{recursive:true});
const phases=[['invest',15,1],['qol',8,1],['disabled',8,0],['restored',15,1]],proof=[];
let previous=null,expectedBP=0;
for(const [phase,flags,moduleOn] of phases){
 const folder=join(output,phase),config=join(folder,'config');await mkdir(folder,{recursive:true});
 await cp(profile,config,{recursive:true,filter:s=>!s.includes('memory-cards')});
 let ini=await readFile(join(config,'pe_mods.ini'),'utf8');ini=ini.replace(/^Manage mods\/BP Overhaul \(preview\)=.*\r?\n/gm,'').replace(/^PE Enhancements\/Bonus Points\/Applied feature flags=.*\r?\n/gm,'');
 ini+='\nManage mods/BP Overhaul (preview)='+moduleOn+'\nPE Enhancements/Bonus Points/Applied feature flags='+flags+'\n';await writeFile(join(config,'pe_mods.ini'),ini);
 for(const port of ['bu00','bu10']){await mkdir(join(folder,port),{recursive:true});const src=previous?join(previous,port):join(profile,'memory-cards',port);await cp(src,join(folder,port),{recursive:true});}
 const args=['--disc',disc,'--disc2',disc.replace('(Disc 1)','(Disc 2)'),'--headless','--frame-limit','65000','--control-at','11000','--pad-at','10000:8','--pad-at','10010:0','--dump-frame',join(folder,'frame.ppm')];if(phase!=='restored')args.push('--test-save-after','30000');
 const child=spawn(exe,args,{cwd:folder,windowsHide:true,env:{...process.env,PE_CONFIG_HOME:config,PE_BP_UI_PAGE:'',PE_BP_UI_F1_TEST:'',PE_BP_SETTINGS_TEST:'1',SDL_AUDIODRIVER:'dummy',PSYZ_AUDIO_CLOCK:'video'}});
 let log='',queue=[],waiting=[],closed=false,ui=null,bpRow=-1;child.on('error',e=>log+=e);child.on('exit',()=>{closed=true;while(waiting.length)waiting.shift()(null)});
 function feed(line){log+=line+'\n';if(line.startsWith('BP_UI_MAIN '))bpRow=+(/bp_row=(-?\d+)/.exec(line)[1]);if(line.startsWith('BP_UI_STATE ')){const m=/open=(\d+) page=(\S+) row=(\S+).*pending=(\d+) help=.*notice=(.*)/.exec(line);if(m)ui={open:+m[1],page:m[2],row:m[3],pending:+m[4],notice:m[5]};}if(line.startsWith('{"event":"control"')){const e=JSON.parse(line);waiting.length?waiting.shift()(e):queue.push(e);}}
 createInterface({input:child.stdout}).on('line',feed);createInterface({input:child.stderr}).on('line',feed);
 async function next(){if(queue.length)return queue.shift();return await new Promise((ok,bad)=>{const t=setTimeout(()=>bad(Error('Checkpoint timeout')),60000);waiting.push(e=>{clearTimeout(t);e?ok(e):bad(Error('Game exited'));});});}
 async function advance(n,mask=0){child.stdin.write('advance '+n+' '+mask+'\n');return await next();}
 const buttons={up:16,down:64,left:128,right:32,confirm:16384,back:8192,menu:4096};
 async function press(key){await advance(8,buttons[key]);return await advance(16);}
 async function seek(row){for(let i=0;ui?.row!==row&&i<40;i++)await press('down');assert.equal(ui?.row,row);}
 async function buy(row){await seek(row);await press('confirm');assert.equal(ui.pending,1);await press('left');return await press('confirm');}
 async function openBP(state){for(let i=0;i<100&&!ui?.open;i++){if(state.menu?.selection===0){assert(bpRow>=0);for(let r=0;r<bpRow;r++)await press('down');state=await press('confirm');}else if(state.dialogue?.some(d=>d.state!==0)||(state.move_lock&1)){await advance(10,16384);state=await advance(120);}else state=await press('menu');}assert(ui?.open);return state;}
 try{
  await next();await advance(8,16384);await advance(300);if(!previous){await advance(8,64);await advance(24);}await advance(8,16384);await advance(400);
  if(!previous)for(let i=1;i<6;i++){await advance(8,64);await advance(24);}
  await advance(8,16384);await advance(300);await advance(8,16384);let state=await advance(800);assert.equal(state.character.level,34);
  if(phase==='invest'){
   state=await openBP(state);const before=state.character.bonus_points;state=await buy('def.poison');assert.equal(state.character.bonus_points,before-75);
   await press('right');assert.equal(ui.page,'mastery');state=await buy('line.heal');assert.equal(state.character.bonus_points,before-225);expectedBP=state.character.bonus_points;
   await press('back');state=await press('back');
  }else{
   assert.equal(state.character.bonus_points,expectedBP,'toggle/load must not move BP');
   if(moduleOn){assert.match(log,new RegExp('BP_SETTINGS_LOAD flags='+flags+' .*poison=1 heal=1 .*damaged=0'));
    state=await openBP(state);await seek('def.poison');state=await press('confirm');
    if(phase==='qol'){assert.equal(ui.pending,0);assert.match(ui.notice,/Inactive/);}else{assert.equal(ui.pending,1);state=await press('confirm');assert.match(ui.notice,/Cancelled/);}
    await copyFile(join(folder,'frame.ppm'),join(folder,'retained-build.ppm'));await press('back');state=await press('back');
   }
  }
  if(phase!=='restored'){
   for(let i=0;!log.includes('Test save: wrote')&&i<100;i++){
    if(state.dialogue?.some(d=>d.state!==0)||(state.move_lock&1)){await advance(10,16384);state=await advance(240);}else state=await advance(600);
   }
   assert.match(log,/Test save: wrote/,'native save+companion must publish');
   assert((await readdir(join(folder,'bu00'))).includes('BASLUS-006620000000A'));
  }
  assert.equal(state.character.bonus_points,expectedBP);proof.push({phase,flags,moduleOn,bp:expectedBP,ranksObserved:phase==='invest'||!!moduleOn});
  await writeFile(join(output,'proof.json'),JSON.stringify({passed:phase==='restored',phases:proof},null,2));
  child.stdin.write('quit\n');await new Promise(r=>child.once('exit',r));console.log('PASS: '+phase+' BP='+expectedBP);previous=folder;
 }finally{await writeFile(join(folder,'game.log'),log);if(!closed)child.kill();}
}
console.log('PASS: native saves preserve investments and BP through QoL-only, module-off saves, and re-enable.');
