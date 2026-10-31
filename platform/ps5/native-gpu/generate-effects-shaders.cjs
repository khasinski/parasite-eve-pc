// SPDX-License-Identifier: MIT
// Regenerate original project effects headers from original GLSL + pinned AMD MIT
// source; never copies compiled shader arrays from a private build.
const fs=require('fs'),p=require('path'),cp=require('child_process'),c=require('crypto');
module.exports=function(root,compiler,expected){const base=p.join(root,'psyz-src/psyz/src/platform'),out=p.join(root,'build-shaders');fs.mkdirSync(out,{recursive:true});const sha=s=>c.createHash('sha256').update(s).digest('hex'),records=[];
function compile(file,name,include){const src=p.join(base,file),dst=p.join(out,name+'.spv');let args=['-V','--target-env','vulkan1.0'];if(include)args.push('-I'+p.join(base,include));args.push(src,'-o',dst);const q=cp.spawnSync(compiler,args,{encoding:'utf8',windowsHide:true});if(q.status!==0)throw Error(q.stdout+'\n'+q.stderr);const bytes=fs.readFileSync(dst);records.push({source:file,sourceSha256:sha(fs.readFileSync(src)),spirvSha256:sha(bytes),bytes:bytes.length});return bytes;}
let header='/* Generated from source by pinned glslang. No hand-edited shader blobs. */\n';
for(const file of ['fullscreen.vert','fsr1_easu.frag','fsr1_rcas.frag']){const bytes=compile('fsr1/shaders/'+file,'fsr1-'+file,'fsr1/third_party/fsr1');header+='_Alignas(4) static const unsigned char cpe_'+file.replace(/\./g,'_')+'[] = {\n'+[...bytes].map(x=>'0x'+x.toString(16).padStart(2,'0')).join(',')+'\n};\n';}
fs.mkdirSync(p.join(base,'fsr1/generated'),{recursive:true});fs.writeFileSync(p.join(base,'fsr1/generated/fsr1_shaders.h'),header);
header='/* Generated from project original GLSL, no hand-edited blobs. */\n';for(const [file,name]of [['fsr1/shaders/fullscreen.vert','cpe_fullscreen_vert'],['movie_blur.frag','cpe_movie_blur_frag']]){const bytes=compile(file,'movie-'+name);header+='_Alignas(4) static const unsigned char '+name+'[]={'+[...bytes].join(',')+'};\n';}fs.writeFileSync(p.join(base,'movie_blur_shaders.h'),header);
for(const x of expected){const actual=sha(fs.readFileSync(p.join(root,'psyz-src',x.path),'utf8').replaceAll('\r\n','\n'));if(actual!==x.after)throw Error('Generated selected-production header mismatch '+x.path);}
fs.writeFileSync(p.join(root,'EFFECTS-SHADER-PROVENANCE.json'),JSON.stringify({records,selectedProductionHeadersMatch:true,shaderABIGateClaim:false,deviceExecuted:false},null,2));return records;};
