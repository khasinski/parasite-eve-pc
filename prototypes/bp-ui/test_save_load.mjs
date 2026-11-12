// Verify the imported Day 3 save with the real Continue/card/file UI.
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {mkdir,writeFile,copyFile,readdir} from 'node:fs/promises';
import {resolve,join} from 'node:path';
import assert from 'node:assert/strict';
const [exe,disc,output,profile]=process.argv.slice(2).map(x=>resolve(x));
await mkdir(output,{recursive:true});
// Headless intentionally uses working-directory cards. Interactive uses profile paths.
for(const port of ['bu00','bu10']){await mkdir(join(output,port),{recursive:true});
 for(const file of await readdir(join(profile,'memory-cards',port)).catch(()=>[]))
  await copyFile(join(profile,'memory-cards',port,file),join(output,port,file));}
const child=spawn(exe,['--disc',disc,'--headless','--frame-limit','40000','--control-at','11000','--pad-at','10000:8','--pad-at','10010:0','--dump-frame',join(output,'frame.ppm')],{cwd:output,windowsHide:true,env:{...process.env,PE_CONFIG_HOME:profile,SDL_AUDIODRIVER:'dummy',PSYZ_AUDIO_CLOCK:'video'}});
let log='',queue=[],waiting=[],closed=false;child.on('error',e=>log+=e);child.on('exit',()=>{closed=true;while(waiting.length)waiting.shift()(null)});
function feed(line){log+=line+'\n';if(line.startsWith('{"event":"control"')){const e=JSON.parse(line);waiting.length?waiting.shift()(e):queue.push(e);}}
createInterface({input:child.stdout}).on('line',feed);createInterface({input:child.stderr}).on('line',feed);
async function next(){if(queue.length)return queue.shift();return await new Promise((r,b)=>{const timer=setTimeout(()=>b(Error('Checkpoint timeout')),60000);waiting.push(e=>{clearTimeout(timer);e?r(e):b(Error('Game exited'));})});}
async function advance(frames,mask=0){child.stdin.write('advance '+frames+' '+mask+'\n');return await next();}
try{
 await next();await copyFile(join(output,'frame.ppm'),join(output,'title-continue.ppm'));
 await advance(8,16384);await advance(300);
 await advance(8,64);await advance(24);await advance(8,16384);await advance(400);
 await copyFile(join(output,'frame.ppm'),join(output,'card2-saves.ppm'));
 await advance(8,16384);const loaded=await advance(300);
 assert.equal(loaded.character.level,14,'Day 3 save must load, not start a new game');
 await copyFile(join(output,'frame.ppm'),join(output,'day3-loaded.ppm'));
 await advance(8,16384);const ready=await advance(800);assert.equal(ready.character.level,14);
 await writeFile(join(output,'proof.json'),JSON.stringify({passed:true,card:2,slot:1,day:3,level:14,scene:ready.scene},null,2));
 child.stdin.write('quit\n');await new Promise(r=>child.once('exit',r));
 console.log('PASS: Continue -> card 2 -> save 1 loads Day 3, level 14.');
}finally{await writeFile(join(output,'game.log'),log);if(!closed)child.kill();}
