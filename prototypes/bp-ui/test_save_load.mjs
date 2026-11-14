// Verify a repo sample save (card 1, save 2 = sample B, level 14) with the real Continue/card/file UI.
// Usage: node test_save_load.mjs GAME DISC1 OUTPUT PROFILE   (PROFILE: settings only, copied)
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {mkdir,writeFile,copyFile,cp} from 'node:fs/promises';
import {resolve,join} from 'node:path';
import assert from 'node:assert/strict';
import {sampleCards,SAMPLE} from './test_saves.mjs';
const [exe,disc,output,profile]=process.argv.slice(2).map(x=>resolve(x));
await mkdir(output,{recursive:true});
// Headless intentionally uses working-directory cards. Settings come from a copy of the profile.
await sampleCards(output);
const config=join(output,'config');await cp(profile,config,{recursive:true,filter:s=>!s.includes('memory-cards')});
const child=spawn(exe,['--disc',disc,'--headless','--frame-limit','40000','--control-at','11000','--pad-at','10000:8','--pad-at','10010:0','--dump-frame',join(output,'frame.ppm')],{cwd:output,windowsHide:true,env:{...process.env,PE_CONFIG_HOME:config,SDL_AUDIODRIVER:'dummy',PSYZ_AUDIO_CLOCK:'video'}});
let log='',queue=[],waiting=[],closed=false;child.on('error',e=>log+=e);child.on('exit',()=>{closed=true;while(waiting.length)waiting.shift()(null)});
function feed(line){log+=line+'\n';if(line.startsWith('{"event":"control"')){const e=JSON.parse(line);waiting.length?waiting.shift()(e):queue.push(e);}}
createInterface({input:child.stdout}).on('line',feed);createInterface({input:child.stderr}).on('line',feed);
async function next(){if(queue.length)return queue.shift();return await new Promise((r,b)=>{const timer=setTimeout(()=>b(Error('Checkpoint timeout')),60000);waiting.push(e=>{clearTimeout(timer);e?r(e):b(Error('Game exited'));})});}
async function advance(frames,mask=0){child.stdin.write('advance '+frames+' '+mask+'\n');return await next();}
try{
 await next();await copyFile(join(output,'frame.ppm'),join(output,'title-continue.ppm'));
 await advance(8,16384);await advance(300);
 await advance(8,16384);await advance(400);
 for(let i=1;i<SAMPLE.B;i++){await advance(8,64);await advance(24);}
 await copyFile(join(output,'frame.ppm'),join(output,'card1-saves.ppm'));
 await advance(8,16384);const loaded=await advance(300);
 assert.equal(loaded.character.level,14,'sample B must load, not start a new game');
 assert.equal(loaded.character.bonus_points,578);
 await copyFile(join(output,'frame.ppm'),join(output,'sample-b-loaded.ppm'));
 await advance(8,16384);const ready=await advance(800);assert.equal(ready.character.level,14);
 await writeFile(join(output,'proof.json'),JSON.stringify({passed:true,card:1,slot:SAMPLE.B,save:'repo sample B',level:14,scene:ready.scene},null,2));
 child.stdin.write('quit\n');await new Promise(r=>child.once('exit',r));
 console.log('PASS: Continue -> card 1 -> save 2 loads repo sample B, level 14.');
}finally{await writeFile(join(output,'game.log'),log);if(!closed)child.kill();}
