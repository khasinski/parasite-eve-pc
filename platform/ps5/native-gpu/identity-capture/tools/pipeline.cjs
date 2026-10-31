'use strict';
const crypto=require('crypto'),{validateCapture}=require('./preflight.cjs'),{prepareIndex,selectWindow}=require('./candidate-window.cjs'),{annotateCoverage}=require('./annotate-coverage.cjs');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
function runWindow({bundle,index,drawIndex=0,limit=256,cursor=null,indexDigest=null,mapPage,hash}){
 if(!bundle||!bundle.ok||!bundle.names||!bundle.manifest||!bundle.fileSha256)throw Error('PREFLIGHT_BUNDLE_REQUIRED');
 const check=validateCapture(bundle.manifest,{manifestBasename:bundle.names.manifest});if(!check.ok)throw Error('PREFLIGHT_MANIFEST_CHANGED');
 if(!Buffer.isBuffer(bundle.words)||bundle.words.length!==1048576||!Buffer.isBuffer(bundle.known)||bundle.known.length!==524288||sha(bundle.words)!==bundle.fileSha256[check.names.raw]||sha(bundle.known)!==bundle.fileSha256[check.names.known])throw Error('PREFLIGHT_BUFFERS_CHANGED');
 if(!Number.isInteger(drawIndex)||drawIndex<0||drawIndex>=bundle.manifest.draws.length||typeof mapPage!=='function'||typeof hash!=='function')throw Error('DRAW_OR_TRUSTED_CALLBACK_REQUIRED');
 const draw=bundle.manifest.draws[drawIndex],prepared=prepareIndex(index),window=selectWindow(prepared,{mode:draw.mode,limit,cursor,indexDigest});
 const mapped=mapPage({entries:window.entries},bundle.words,bundle.known,draw,hash,limit);
 if(!mapped||!Array.isArray(mapped.matches))throw Error('MAPPER_RESULT_CONTRACT');
 const evidence=annotateCoverage(draw,mapped.matches,bundle.manifest);
 const {entries,...selection}=window;
 return {schema:'cpe.identity-window-evidence.v1',selection,mapper:{examined:mapped.examined??null,eligible:mapped.eligible??null,quarantine:mapped.quarantine||{}},evidence,knownWords:bundle.knownWords,captureFileSha256:{...bundle.fileSha256},realCaptureVerified:false,overallMatchingComplete:false,characterClassificationEstablished:false,pixelReplacementEstablished:false,caveat:'One bounded window only; preserve earlier cursor-chain results and unknown/collision quarantine. UV bounds are not visible-triangle or character proof.'};
}
module.exports={runWindow};
