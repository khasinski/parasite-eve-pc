// SPDX-License-Identifier: MIT
// Source-only patch to an explicitly supplied fresh PR18 prepared game tree.
const fs=require('fs'),p=require('path'),c=require('crypto'),cp=require('child_process');
const own=__dirname,manifest=JSON.parse(fs.readFileSync(p.join(own,'manifest.json'))),game=p.resolve(process.argv[2]||''),check=process.argv.includes('--check-only'),sha=s=>c.createHash('sha256').update(s).digest('hex'),norm=f=>fs.readFileSync(f,'utf8').replace(/\r\n/g,'\n');
if(!process.argv[2]||!fs.existsSync(p.join(game,'CMakeLists.txt')))throw Error('Usage: node apply-addon.cjs <prepared-game-src> [--check-only]');
if(sha(fs.readFileSync(p.join(own,'particles.patch')))!==manifest.patchSha256)throw Error('Particle patch digest mismatch');
for(const x of manifest.unchangedPsyz){const f=p.join(game,'../psyz-src',x.path);if(!fs.existsSync(f)||sha(norm(f))!==x.sha256)throw Error('Unmatched public PsyZ prerequisite: '+x.path);}
const state=manifest.files.map(x=>{const f=p.join(game,x.path),value=fs.existsSync(f)?sha(norm(f)):null;return {x,value};});
const before=state.every(({x,value})=>value===x.before),after=state.every(({x,value})=>value===x.after);
if(!before&&!after)throw Error('Fresh pinned prepared sources or exact already-applied add-on required; no mixed/unmatched patching');
const marker=p.join(game,'.PE-PS5-PARTICLE-ADDON.json');
if(check){console.log(JSON.stringify({state:after?'already-applied':'ready',verifiedFiles:state.length,sourceWrites:false}));process.exit(0);}
if(before){const patch=p.join(own,'particles.patch');for(const args of [['apply','--check','--whitespace=nowarn',patch],['apply','--whitespace=nowarn',patch]]){const q=cp.spawnSync('git',args,{cwd:game,windowsHide:true,encoding:'utf8',maxBuffer:4*1024*1024});if(q.status)throw Error('Source patch failed: '+q.stderr);}}
for(const x of manifest.files)if(sha(norm(p.join(game,x.path)))!==x.after)throw Error('Final selected source hash mismatch: '+x.path);
fs.writeFileSync(marker,JSON.stringify({patchSha256:manifest.patchSha256,verifiedFiles:state.length,sourceOnly:true},null,2)+'\n');
console.log(JSON.stringify({state:before?'applied':'already-applied',verifiedFiles:state.length,sourceOnly:true}));
