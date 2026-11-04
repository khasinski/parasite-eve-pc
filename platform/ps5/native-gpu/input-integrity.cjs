// SPDX-License-Identifier: MIT
// Pinned retained target inputs plus explicitly caller-owned host tool identity.
const fs=require('fs'),p=require('path'),c=require('crypto'),cp=require('child_process');
const sha=file=>c.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
function treeInfo(root){
 const entries=[];let bytes=0;
 function walk(dir,rel=''){if(fs.lstatSync(dir).isSymbolicLink())throw Error('Symlinked input tree rejected: '+rel);for(const name of fs.readdirSync(dir).sort()){const file=p.join(dir,name),key=rel?rel+'/'+name:name,stat=fs.lstatSync(file);if(stat.isSymbolicLink())throw Error('Symlinked input rejected: '+key);if(stat.isDirectory())walk(file,key);else if(stat.isFile()){const digest=sha(file);entries.push(key+'\0'+digest+'\n');bytes+=stat.size;}else throw Error('Non-regular input rejected: '+key);}}
 walk(root);return {files:entries.length,bytes,treeSha256:c.createHash('sha256').update(entries.join('')).digest('hex')};
}
function verifyTree(root,expected,label){const actual=treeInfo(root);if(actual.files!==expected.files||actual.bytes!==expected.bytes||actual.treeSha256!==expected.treeSha256)throw Error('Pinned input tree mismatch: '+label);return actual;}
function verifyInputs(base,webpSource,manifestDir){
 const selected=JSON.parse(fs.readFileSync(p.join(manifestDir,'build-inputs.json')));for(const x of selected.files)if(sha(p.join(base,x.path))!==x.sha256)throw Error('Retained input mismatch: '+x.path);
 const webp=JSON.parse(fs.readFileSync(p.join(manifestDir,'webp-inputs.json')));for(const x of webp.files)if(sha(p.join(webpSource,x.path))!==x.sha256)throw Error('WebP source mismatch: '+x.path);
 const trees=JSON.parse(fs.readFileSync(p.join(manifestDir,'target-inputs.json')));if(trees.schema!==1)throw Error('Unsupported input tree manifest');const verified=[];for(const x of trees.trees){const root=x.scope==='webpSource'?webpSource:p.join(base,x.path);verified.push({path:x.path||'<WEBP_SOURCE>',...verifyTree(root,x,x.path||'WebP source')});}return verified;
}
function resolveTool(value){if(typeof value!=='string'||!value)throw Error('Caller tool path required');if(fs.existsSync(value))return p.resolve(value);const q=cp.spawnSync('where.exe',[value],{encoding:'utf8',windowsHide:true,timeout:10000});if(q.status)throw Error('Caller tool not found: '+value);const file=q.stdout.trim().split(/\r?\n/)[0];if(!fs.existsSync(file))throw Error('Caller tool resolution failed');return p.resolve(file);}
function inspectCallerTools(config){
 if(config.toolHashes!==undefined&&(config.toolHashes===null||typeof config.toolHashes!=='object'||Array.isArray(config.toolHashes)||Object.keys(config.toolHashes).some(k=>!['cmake','ninja'].includes(k))))throw Error('Caller toolHashes must be an object with cmake/ninja keys');
 const result={};for(const name of ['cmake','ninja']){const file=resolveTool(config[name]),digest=sha(file),expected=config.toolHashes&&config.toolHashes[name];if(expected!==undefined&&(typeof expected!=='string'||!/^[a-f0-9]{64}$/i.test(expected)||digest!==expected.toLowerCase()))throw Error('Caller tool digest mismatch: '+name);
  const q=cp.spawnSync(file,['--version'],{encoding:'utf8',windowsHide:true,timeout:10000,maxBuffer:1024*1024});if(q.status)throw Error('Caller tool version query failed: '+name);result[name]={path:file,sha256:digest,version:q.stdout.trim(),policy:expected?'caller-pinned':'caller-supplied identity recorded (not recipe-pinned)'};
 }return result;
}
module.exports={sha,treeInfo,verifyTree,verifyInputs,inspectCallerTools};
