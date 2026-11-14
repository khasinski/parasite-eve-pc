// Verify the classic BP button and the refund handler as separate native entry points.
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {mkdir,writeFile,copyFile,readdir,cp} from 'node:fs/promises';
import {resolve,join} from 'node:path';
import assert from 'node:assert/strict';
import {sampleCards,loadCard1,SAMPLE} from './test_saves.mjs';
// Repo sample save B (card 1, save 2: level 14). PROFILE supplies settings only.
const [exe,disc,out,profile]=process.argv.slice(2,6).map(x=>resolve(x));
const mode=process.argv[6]||'upgrade';assert(['upgrade','refund','off'].includes(mode));
await mkdir(out,{recursive:true});
await sampleCards(out);
const config=join(out,'config');await cp(profile,config,{recursive:true,filter:s=>!s.includes('memory-cards')});
await writeFile(join(config,'pe_mods.ini'),'Manage mods/All mods='+(mode==='off'?0:1)+'\nManage mods/BP Overhaul (preview)=1\n');
const child=spawn(exe,['--disc',disc,'--disc2',disc.replace('(Disc 1)','(Disc 2)'),'--headless','--frame-limit','60000','--control-at','11000','--pad-at','10000:8','--pad-at','10010:0','--dump-frame',join(out,'frame.ppm')],{cwd:out,windowsHide:true,env:{...process.env,PE_CONFIG_HOME:config,PE_BP_UI_PAGE:'',PE_BP_UI_F1_TEST:mode==='refund'?'Open Wayne preview':'',PE_BP_UI_TEST_JUNK:'',PE_BP_UI_SUITE_TEST:'1',PE_BP_UI_F1_PRESETS_TEST:'1',SDL_AUDIODRIVER:'dummy',PSYZ_AUDIO_CLOCK:'video'}});
const buttons={up:16,down:64,left:128,right:32,confirm:16384,back:8192,menu:4096};
let log='',ui=null,bpRow=-1,queue=[],waiting=[],closed=false,baseline=null,shots=0;
function feed(line){log+=line+'\n';if(line.startsWith('BP_UI_MAIN '))bpRow=+(/bp_row=(-?\d+)/.exec(line)[1]);if(line.startsWith('BP_UI_STATE ')){const m=/open=(\d+) page=(\S+) row=(\S+) selected=(\d+) top=(\d+) detail=(\d+) pages=(\d+) pending=(\d+) help=(\d+) notice=(.*)/.exec(line);if(m)ui={open:+m[1],page:m[2],row:m[3],selected:+m[4],pending:+m[8]};}if(line.startsWith('{"event":"control"')){const e=JSON.parse(line);waiting.length?waiting.shift()(e):queue.push(e);}}
createInterface({input:child.stdout}).on('line',feed);createInterface({input:child.stderr}).on('line',feed);
child.on('error',e=>{log+=e;});child.on('exit',()=>{closed=true;while(waiting.length)waiting.shift()(null);});
async function next(){if(queue.length)return queue.shift();return await new Promise((ok,bad)=>{const t=setTimeout(()=>bad(Error('Checkpoint timeout')),60000);waiting.push(e=>{clearTimeout(t);e?ok(e):bad(Error('Game exited'));});});}
async function advance(frames,mask=0){child.stdin.write('advance '+frames+' '+mask+'\n');const e=await next();if(baseline){assert.deepEqual(e.character,baseline.character);assert.deepEqual(e.inventory,baseline.inventory);}return e;}
async function press(key){await advance(8,buttons[key]);return await advance(16);}
async function shot(label){await copyFile(join(out,'frame.ppm'),join(out,String(++shots).padStart(2,'0')+'-'+label+'.ppm'));}
try{
 await next();let e=await loadCard1(advance,SAMPLE.B);assert.equal(e.character.level,14);
 for(let i=0;i<80&&!((ui&&ui.open)||e.menu?.selection===0);i++){const talking=e.dialogue?.some(d=>d.state!==0);e=talking||(e.move_lock&1)?(await advance(10,16384),await advance(120)):await press('menu');}
 baseline=await advance(24);
 if(mode==='refund'){
  assert.equal(ui?.open,1);assert.equal(ui.page,'reset.one');await shot('refund-direct');
  await press('right');assert.equal(ui.page,'reset.mastery');await press('right');assert.equal(ui.page,'reset.mastery');await shot('refund-mastery');
  for(let i=0;ui.row!=='refund.all.open'&&i<40;i++)await press('down');assert.equal(ui.row,'refund.all.open');
  await press('confirm');assert.equal(ui.page,'reset.all');await shot('refund-all');await press('back');assert.equal(ui.page,'reset.mastery');
  await press('back');assert.equal(ui.open,0);await shot('refund-returned');assert(!log.includes('page=preview'));
 }else{
  assert.equal(e.menu?.selection,0);await shot('main-menu');assert(bpRow>=0);for(let i=0;i<bpRow;i++)await press('down');const selected=await advance(2);await shot('classic-bp');
  e=await press('confirm');
  if(mode==='off'){assert(!ui?.open);assert(!log.includes('BP_UI_ENTRY source=classic_bp'));await shot('vanilla-bp');}
  else{
   assert.equal(ui?.page,'aya');assert.equal(ui.open,1);assert.match(log,/BP_UI_ENTRY source=classic_bp page=aya/);await shot('aya-direct');
   await press('left');await press('left');assert.equal(ui.page,'guns');await press('right');await press('right');await press('right');assert.equal(ui.page,'mastery');await press('right');assert.equal(ui.page,'mastery');
   await press('back');assert.equal(ui.open,0);e=await advance(2);assert.deepEqual(e.menu,selected.menu);await shot('bp-focus-restored');
   await press('confirm');assert.equal(ui.page,'aya');await press('back');assert.equal(ui.open,0);assert(!log.includes('page=preview'));assert(!log.includes('page=reset'));
  }
 }
 assert.match(log,/BP_UI_SUITE compiled=15 .*page=PE Enhancements\/Bonus Points/);
 assert(!/BP_UI_(TEXT_OVERFLOW|OPEN_FAILED|HIGHLIGHT_ERROR|PACKET_PRESSURE)/.test(log));
 await writeFile(join(out,'proof.json'),JSON.stringify({passed:true,mode,shots,unchangedCharacterAndInventory:true},null,2));child.stdin.write('quit\n');await new Promise(r=>child.once('exit',r));console.log('PASS: '+mode+' direct entry, navigation and return.');
}finally{await writeFile(join(out,'game.log'),log);if(!closed)child.kill();}
