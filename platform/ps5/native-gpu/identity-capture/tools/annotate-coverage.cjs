'use strict';
const {normalizeIdentity,safeRelative}=require('./candidate-window.cjs');
function annotateCoverage(draw,matches,attribution={}){
 const errors=[],annotations=[],valid=n=>Number.isSafeInteger(n)&&n>=0&&n<=255;
 const uv=draw&&draw.uvBoundsInclusive,factor=draw&&{P4:4,P8:2,C16:1}[draw.mode];
 if(!draw||!['P4','P8','C16'].includes(draw.mode)||!Array.isArray(uv)||uv.length!==4||!uv.every(valid)||uv[0]>uv[2]||uv[1]>uv[3])return {ok:false,errors:[{code:'INVALID_OR_WRAPPING_DRAW_UV'}],annotations};
 if(!Number.isInteger(draw.rawTpage)||draw.rawTpage<0||draw.rawTpage>65535||!Number.isInteger(draw.rawClut)||draw.rawClut<0||draw.rawClut>32767||draw.pageXWords!==(draw.rawTpage&15)*64||draw.pageY!==((draw.rawTpage>>4)&1)*256||draw.paletteXWords!==(draw.rawClut&63)*16||draw.paletteY!==(draw.rawClut>>6)||['P4','P8','C16','unsupported'][(draw.rawTpage>>7)&3]!==draw.mode||draw.pageXWords+256/factor>1024)return {ok:false,errors:[{code:'INVALID_DRAW_REGISTER_OR_PAGE'}],annotations};
 if(!Array.isArray(matches)||matches.length>4096)return {ok:false,errors:[{code:'MATCH_COUNT_BOUND'}],annotations};
 const seen=new Set();for(const match of matches){let decoded;try{decoded=JSON.parse(match.key);}catch{errors.push({code:'INVALID_MATCH_KEY'});continue;}
  const n=normalizeIdentity({...decoded,key:match.key});if(n.reason){errors.push({code:n.reason,key:match.key});continue;}
  if(seen.has(match.key)){errors.push({code:'DUPLICATE_MATCH',key:match.key});continue;}seen.add(match.key);
  if(match.mode!==draw.mode||JSON.stringify(match.rectangleTexels)!==JSON.stringify(decoded.rectangleTexels)){errors.push({code:'MATCH_DESCRIPTOR_DISAGREES',key:match.key});continue;}
  if(!Array.isArray(match.files)||!match.files.length||match.files.length>100000||match.files.some(f=>!f||typeof f.disc!=='string'||!/^[A-Za-z0-9_-]{1,64}$/.test(f.disc)||!safeRelative(f.relative)||typeof f.sha256!=='string'||!/^[a-f0-9]{64}$/i.test(f.sha256))||new Set(match.files.map(f=>f.sha256.toLowerCase())).size!==1){errors.push({code:'AMBIGUOUS_MATCH_FILES',key:match.key});continue;}
  const [x,y,w,h]=decoded.rectangleTexels,left=Math.max(x,uv[0]),top=Math.max(y,uv[1]),right=Math.min(x+w,uv[2]+1),bottom=Math.min(y+h,uv[3]+1),iw=Math.max(0,right-left),ih=Math.max(0,bottom-top);
  annotations.push({key:match.key,page:{rawTpage:draw.rawTpage,rawClut:draw.rawClut,pageXWords:draw.pageXWords,pageY:draw.pageY,paletteXWords:draw.paletteXWords,paletteY:draw.paletteY},rectangleTexels:[x,y,w,h],drawUvInclusive:[...uv],intersectionTexels:iw&&ih?[left,top,iw,ih]:null,coverage:!iw||!ih?'no-overlap':iw===w&&ih===h?'full-rectangle-overlap':'partial',files:JSON.parse(JSON.stringify(match.files)),bankAtUpload:attribution.bankAtUpload??null,loadedBankAtPlayerSetup:attribution.loadedBankAtPlayerSetup??null,attribution:typeof attribution.attribution==='string'?attribution.attribution:'unprovided; no ownership attribution',caveat:'UV bounding rectangles only; not filled-triangle visibility, character identity, pixel decode, replacement or hash proof',characterClassificationEstablished:false,pixelReplacementEstablished:false,drawUseEstablished:false});
 }
 return {ok:errors.length===0,errors,annotations,identityHashVerifiedByThisAnnotation:false};
}
module.exports={annotateCoverage};
