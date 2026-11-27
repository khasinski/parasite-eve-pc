/* Source-export check: current host closure, stale provider pruning, refusal before writes. */
const fs=require('fs'),p=require('path'),assert=require('assert'),crypto=require('crypto');
const source=p.resolve(__dirname,'..'),manifest=require('../platform/ps5/native-gpu/manifest.json');
const apply=require('../platform/ps5/native-gpu/prepare-compatibility.cjs');
const root=fs.mkdtempSync(p.join(p.resolve(process.argv[2]),'ps5-export-')),dest=p.join(root,'game-src');
fs.mkdirSync(p.join(dest,'mods/old-group'),{recursive:true});
fs.writeFileSync(p.join(dest,'mods/old-group/stale.c'),'stale');
assert.equal(apply(source,dest,manifest.compatibilitySources),manifest.compatibilitySources.length);
assert(!fs.existsSync(p.join(dest,'mods/old-group/stale.c')));
assert(fs.existsSync(p.join(dest,'mods/host/pe_checkpoint_game.c')));
for(const e of manifest.compatibilitySources){const data=fs.readFileSync(p.join(dest,e.path));assert.equal(crypto.createHash('sha256').update(data).digest('hex'),e.sha256);}
const before=fs.readFileSync(p.join(dest,'mods/host/pe_checkpoint_game.c'));
const bad=manifest.compatibilitySources.map(x=>({...x}));bad[0].sha256='0'.repeat(64);
assert.throws(()=>apply(source,dest,bad),/mismatch/);
assert(fs.readFileSync(p.join(dest,'mods/host/pe_checkpoint_game.c')).equals(before));
console.log('PASS PS5 compatibility export, source hashes, stale-source removal and preflight refusal');
