'use strict';
const fs=require('node:fs'),p=require('node:path'),crypto=require('node:crypto'),cp=require('node:child_process');
const here=__dirname,m=require('./manifest.json');
const root=p.resolve(process.argv[2]||'');
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const sourceHash=f=>hash(fs.readFileSync(f,'utf8').replace(/\r\n/g,'\n'));
function requireThat(ok,msg){if(!ok)throw Error(msg);}
requireThat(process.argv.length===3,'Usage: node prepare-addon.cjs FRESH_PREPARED_EXPORT');
const proof=p.join(root,'IDENTITY-ADDON.json');
requireThat(!fs.existsSync(proof),'Addon already prepared; use fresh source export');
const base=JSON.parse(fs.readFileSync(p.join(root,'SOURCE-EXPORT.json')));
for(const [name,pin] of Object.entries(m.basePins))requireThat(base.inputs.some(x=>x.name===name&&x.revision===pin),'Wrong source pin: '+name);
for(const d of [root,...['game-src','psyz-src','sdl3-src'].map(x=>p.join(root,x))])requireThat(!fs.existsSync(p.join(d,'.git')),'Refusing Git checkout: '+d);
for(const f of m.files){const file=p.join(root,f.component+'-src',f.path);requireThat(f.before===null?!fs.existsSync(file):sourceHash(file)===f.before,'Wrong base source: '+f.path);}
for(const patch of m.patches){const file=p.join(here,patch.path);requireThat(hash(fs.readFileSync(file))===patch.sha256,'Patch digest mismatch');cp.execFileSync('git',['apply','--check',file],{cwd:p.join(root,patch.component+'-src'),stdio:'inherit'});}
for(const patch of m.patches)cp.execFileSync('git',['apply',p.join(here,patch.path)],{cwd:p.join(root,patch.component+'-src'),stdio:'inherit'});
for(const f of m.files)requireThat(sourceHash(p.join(root,f.component+'-src',f.path))===f.after,'Post-apply source mismatch: '+f.path);
fs.copyFileSync(p.join(here,'identity-capture.cmake'),p.join(root,'game-support/identity-capture.cmake'));
fs.writeFileSync(proof,JSON.stringify({schema:m.schema,base:m.publicBaseCommit,selectedEbootSource:m.selectedEbootSource,files:m.files},null,2)+'\n',{flag:'wx'});
console.log('PASS: exact four 0AAE source deltas applied; capture remains OFF');
