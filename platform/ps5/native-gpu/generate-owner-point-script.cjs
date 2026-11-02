// SPDX-License-Identifier: MIT -- project-authored local input generation.
// No retail tails, executable offsets or original-game fingerprints distributed.
const fs=require('fs'),p=require('path'),crypto=require('crypto');
const prefix=[4194304,268566528,269680641,270663682,65541,131076,65538,1245216,1310752,1376288,65537,131076,1048704,1114240,1179776,65536,131074,65539,65540,131080,65536,-1]; // Previously published upstream prefix only.
function fail(s){throw Error('Required owner PointSprite input: '+s);}
function validate(data){
 if(data.length!==216)fail('expected exactly54 signed little-endian32-bit words (216bytes)');
 const w=Array.from({length:54},(_,i)=>data.readInt32LE(i*4));if(prefix.some((n,i)=>w[i]!==n))fail('published prefix mismatch');
 // Independently authored ownership contract: only existing handler IDs and
 // register/wait instructions in the omitted tails; no pointer/variable writes.
 for(let pc=22;pc<54;pc++){const n=w[pc],op=n>>>16,arg=(n<<16)>>16;if(n===-1){if(pc!==37&&pc!==53)fail('unexpected terminal');continue;}if(op===1){if(arg<0||arg>5)fail('unowned handler');}else if(op===2){if(arg<0||arg>8)fail('unbounded wait');}else if(op>=0x10&&op<=0x1f){}else fail('unsupported tail instruction');}
 const stops=[[6,12,17,20,21],[25,31,33,36,37],[41,47,49,52,53]];
 for(let control=0;control<3;control++){let pc=0,stage=0,steps=0,regs=Array(16).fill(0);while(stage<5){if(pc<0||pc>=54||steps++>100)fail('branch/termination bounds');const n=w[pc];let wait=0,stopped=false,halt=n===-1;if(!halt){const op=n>>>16,arg=(n<<16)>>16;if(op===1){if((arg&255)>5)fail('unowned handler');}else if(op===2){wait=arg;stopped=true;}else if(op>=0x10&&op<=0x1f)regs[op-0x10]=arg;else if(op>=0x40&&op<=0x4f){if((arg&65535)!==0)fail('unowned variable');regs[op-0x40]=control;}else if(op>=0x1000&&op<0x2000){if(regs[(op&0xf00)>>8]===arg)pc+=(op<<24)>>24;}else fail('unsupported instruction');pc++;}
   if(stopped||halt){const expected=Array(16).fill(0);expected[0]=control;if(!control){if(stage>=1)expected[3]=expected[4]=expected[5]=32;if(stage>=2)expected[0]=expected[1]=expected[2]=128;}else if(stage>=1){expected[0]=64;expected[control===1?1:2]=64;}const limit=[4,control?3:4,2,8,0][stage];if(pc!==stops[control][stage]||wait!==limit||halt!==(stage===4)||regs.some((v,i)=>v!==expected[i]))fail('typed stopped-state contract mismatch');stage++;}
 }}return w;
}
function localMetadata(file,text){let st;try{st=fs.lstatSync(file);}catch(e){if(e.code!=='ENOENT')throw e;}if(st){if(!st.isFile()||st.isSymbolicLink()||fs.readFileSync(file,'utf8')!==text)fail('existing unsafe/different local provenance; use fresh tree');}else fs.writeFileSync(file,text,{flag:'wx'});}
module.exports=function(root,input){
 root=fs.realpathSync(p.resolve(root));const own=fs.realpathSync(__dirname),rel=p.relative(own,root);if(!rel||(!rel.startsWith('..'+p.sep)&&rel!=='..'&&!p.isAbsolute(rel)))fail('generated data must stay outside this publishable package tree');
 if(!fs.existsSync(p.join(root,'SOURCE-EXPORT.json')))fail('prepare a fresh source tree first');
 if(!input||typeof input.file!=='string'||!/^[a-fA-F0-9]{64}$/.test(input.sha256||''))fail('set local tools config pointSpriteOwnerData.file and caller-pinned sha256; no input is bundled');
 const st=fs.lstatSync(input.file);if(!st.isFile()||st.isSymbolicLink()||st.size!==216)fail('regular216-byte supplied input file required');const realInput=fs.realpathSync(input.file),ir=p.relative(own,realInput);if(!ir||(!ir.startsWith('..'+p.sep)&&ir!=='..'&&!p.isAbsolute(ir)))fail('input must stay outside publishable package tree');const b=fs.readFileSync(realInput),hash=crypto.createHash('sha256').update(b).digest('hex');if(hash!==input.sha256.toLowerCase())fail('caller input SHA256 mismatch');const words=validate(b);
 const target=p.join(root,'game-src/include/pe1/point_sprite_owner.inc'),text='/* Caller-supplied local data; no license grant. Do not commit or redistribute without authority. */\n#define PE_POINT_SPRITE_SCRIPT_INITIALIZER {'+words.join(',')+'}\n';const parent=fs.realpathSync(p.dirname(target)),inside=p.relative(root,parent);if(inside==='..'||inside.startsWith('..'+p.sep)||p.isAbsolute(inside))fail('generated parent escapes prepared tree');let existing;try{existing=fs.lstatSync(target);}catch(e){if(e.code!=='ENOENT')throw e;}if(existing){if(!existing.isFile()||existing.isSymbolicLink()||fs.readFileSync(target,'utf8')!==text)fail('existing unsafe/different generated input; use a fresh prepared tree');}else fs.writeFileSync(target,text,{flag:'wx'});localMetadata(p.join(root,'POINTSPRITE-OWNER-INPUT.json'),JSON.stringify({callerSupplied:true,sha256:hash,bytes:b.length,wordCount:54,shapeAndTypedBoundaryContractValidated:true,authenticityOrLicenseCleared:false,generatedHeader:'game-src/include/pe1/point_sprite_owner.inc',keepLocalDoNotPublish:true},null,2)+'\n');return {wordCount:54,generated:true};
};
module.exports.validate=validate;
if(require.main===module){if(process.argv.length!==4)fail('usage: node generate-owner-point-script.cjs <prepared-root> <local-input-config.json>');console.log(JSON.stringify(module.exports(process.argv[2],JSON.parse(fs.readFileSync(process.argv[3])))));}
