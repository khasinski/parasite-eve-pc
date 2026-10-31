'use strict';
// Independently authored offline tooling. No DuckStation code or model output.
const fs=require('fs'),path=require('path'),crypto=require('crypto');
const RAW_BYTES=1048576,KNOWN_BYTES=524288,MAX_MANIFEST=24576;
const issue=(code,at,message)=>({code,path:at,message});
const plain=x=>x!==null&&typeof x==='object'&&!Array.isArray(x);
const integer=(x,min,max)=>Number.isSafeInteger(x)&&x>=min&&x<=max;
function basenameSafe(x){return typeof x==='string'&&x.length>0&&x.length<=200&&!/[\\/:\x00-\x1f<>"|?*]/.test(x)&&x!=='.'&&x!=='..'&&!/[. ]$/.test(x)&&!/^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)/i.test(x);}
function validateCapture(value,context={}){
 if(!plain(context))context={};
 const errors=[],warnings=[],bad=(code,at,msg)=>errors.push(issue(code,at,msg));
 if(!plain(value))return {ok:false,errors:[issue('MANIFEST_OBJECT','', 'Expected a JSON object')],warnings};
 const allowed=['schema','build','bankAtUpload','loadedBankAtPlayerSetup','attribution','rawSuffix','knownSuffix','rawBytes','knownBytes','draws','drawLimit','drawDescriptorsTruncated','maximumOutputBytes','runtimeReplacement','authority','knownPolicy','fullImageComplete'];
 for(const k of Object.keys(value))if(!allowed.includes(k))bad('UNKNOWN_FIELD',k,'Unsupported schema field; do not use embedded paths');
 const exact={schema:'cpe.native-texture-capture.v1',rawSuffix:'.vram.bin',knownSuffix:'.known.bin',rawBytes:RAW_BYTES,knownBytes:KNOWN_BYTES,drawLimit:64,maximumOutputBytes:1600000,runtimeReplacement:false,authority:'native-gpu-rgb5551-readback',knownPolicy:'original-upload-known-and-readback-matches-shadow',fullImageComplete:false};
 for(const [k,v]of Object.entries(exact))if(value[k]!==v)bad(k==='authority'?'UNKNOWN_AUTHORITY':k==='knownPolicy'?'UNKNOWN_KNOWN_POLICY':k==='fullImageComplete'?'COMPLETE_IMAGE_UNSUPPORTED':'CONTRACT_VALUE',k,'Expected '+JSON.stringify(v));
 if(typeof value.build!=='string'||!/^[A-Za-z0-9_-]{1,81}$/.test(value.build))bad('BUILD_LABEL','build','Expected 1..81 safe ASCII build characters');
 for(const k of ['bankAtUpload','loadedBankAtPlayerSetup'])if(!integer(value[k],-2147483648,2147483647))bad('BANK_INTEGER',k,'Expected a signed32-bit recorded bank, not a guessed identity');
 if(typeof value.attribution!=='string'||value.attribution.length<1||value.attribution.length>1024||/[\x00-\x1f]/.test(value.attribution))bad('ATTRIBUTION','attribution','Missing or invalid source-attribution text');
 if(typeof value.drawDescriptorsTruncated!=='boolean')bad('TRUNCATION_FLAG','drawDescriptorsTruncated','Expected boolean');
 else if(value.drawDescriptorsTruncated)warnings.push(issue('PARTIAL_DRAW_LIST','drawDescriptorsTruncated','Producer capped descriptors; this is not full coverage'));
 let names=null;
 if(!basenameSafe(context.manifestBasename)||!context.manifestBasename.endsWith('.json'))bad('MANIFEST_BASENAME','context.manifestBasename','Supply a single safe .json basename inside the bundle');
 else if(typeof value.build==='string'&&/^[A-Za-z0-9_-]{1,81}$/.test(value.build)){
  const prefix=context.manifestBasename.slice(0,-5),start=value.build+'-',tick=prefix.startsWith(start)?prefix.slice(start.length):'';
  if(!/^(0|[1-9][0-9]{0,19})$/.test(tick)||BigInt(tick||'0')>18446744073709551615n)bad('BUILD_FILENAME','context.manifestBasename','Expected <same-build>-<uint64ticks>.json');
  else names={manifest:context.manifestBasename,raw:prefix+'.vram.bin',known:prefix+'.known.bin'};
 }
 const seen=new Set();
 if(!Array.isArray(value.draws)||value.draws.length<1||value.draws.length>64)bad('DRAW_COUNT','draws','Expected 1..64 descriptors');
 else value.draws.forEach((d,i)=>{
  const at='draws['+i+']';if(!plain(d)){bad('DRAW_OBJECT',at,'Expected descriptor object');return;}
  const fields=['mode','pageXWords','pageY','paletteXWords','paletteY','rawTpage','rawClut','uvBoundsInclusive'];
  for(const k of Object.keys(d))if(!fields.includes(k))bad(k==='paletteRangeTokens'?'PALETTE_RANGE_UNESTABLISHED':'UNKNOWN_DRAW_FIELD',at+'.'+k,'No unsupported palette-range/path extension is accepted');
  if(!['P4','P8','C16'].includes(d.mode))bad('UNSUPPORTED_MODE',at+'.mode','Only established P4/P8/C16 modes; no guessing');
  const tp=integer(d.rawTpage,0,65535),cl=integer(d.rawClut,0,32767);
  if(!tp)bad('TPAGE_REGISTER',at+'.rawTpage','Expected unsigned16-bit register');
  if(!cl)bad('CLUT_REGISTER',at+'.rawClut','Expected valid512-row CLUT register');
  if(tp){const mode=['P4','P8','C16','unsupported'][(d.rawTpage>>7)&3],x=(d.rawTpage&15)*64,y=((d.rawTpage>>4)&1)*256;
   if(d.mode!==mode||d.pageXWords!==x||d.pageY!==y)bad('TPAGE_DECODE',at,'Mode/page coordinates disagree with rawTpage');
   const width={P4:64,P8:128,C16:256}[d.mode];if(width&&x+width>1024)bad('PAGE_WRAP_UNSUPPORTED',at,'Page crosses native VRAM row; wrapping is not inferred');}
  if(cl&&(d.paletteXWords!==(d.rawClut&63)*16||d.paletteY!==(d.rawClut>>6)))bad('CLUT_DECODE',at,'Palette coordinates disagree with rawClut');
  const uv=d.uvBoundsInclusive;
  if(!Array.isArray(uv)||uv.length!==4||!uv.every(x=>integer(x,0,255))||uv[0]>uv[2]||uv[1]>uv[3])bad('UV_BOUNDS',at+'.uvBoundsInclusive','Expected ordered inclusive texel bounds0..255');
  if(tp&&cl){const key=d.rawTpage+':'+d.rawClut;if(seen.has(key))bad('DUPLICATE_DESCRIPTOR',at,'Repeated page/CLUT pair');seen.add(key);}
 });
 return {ok:errors.length===0,errors,warnings,names,authorityClaim:'metadata-only',realCaptureVerified:false};
}
// JSON.parse alone silently accepts duplicate keys. This bounded lexical walk
// detects duplicates after decoding escaped keys; JSON.parse checks grammar.
function parseCapture(text,context={}){
 const errors=[];if(typeof text!=='string'||Buffer.byteLength(text,'utf8')>MAX_MANIFEST)return {ok:false,errors:[issue('MANIFEST_SIZE','','Manifest exceeds24576 bytes or is not text')],warnings:[]};
 let value;try{value=JSON.parse(text);}catch{return {ok:false,errors:[issue('JSON_SYNTAX','','Malformed JSON')],warnings:[]};}
 let i=0;const ws=()=>{while(/\s/.test(text[i]||'')&&i<text.length)i++;};
 const string=()=>{const start=i++;while(i<text.length){if(text[i]==='\\'){i+=2;continue;}if(text[i++]==='"')return JSON.parse(text.slice(start,i));}throw Error('string');};
 function visit(depth,at){if(depth>32)throw Error('depth');ws();if(text[i]==='{'){i++;ws();const keys=new Set();if(text[i]==='}'){i++;return;}for(;;){ws();const key=string();if(keys.has(key))errors.push(issue('DUPLICATE_JSON_KEY',at+'.'+key,'Duplicate JSON key is not a trustworthy contract'));keys.add(key);ws();i++;visit(depth+1,at+'.'+key);ws();if(text[i++]==='}')return;}}
  else if(text[i]==='['){i++;ws();if(text[i]===']'){i++;return;}let n=0;for(;;){visit(depth+1,at+'['+n+++']');ws();if(text[i++]===']')return;}}
  else if(text[i]==='"')string();else{while(i<text.length&&!/[\s,}\]]/.test(text[i]))i++;}}
 try{visit(0,'');}catch{return {ok:false,errors:[issue('JSON_DEPTH','','JSON nesting exceeds supported32 levels')],warnings:[]};}
 const result=validateCapture(value,context);result.errors.unshift(...errors);result.ok=!result.errors.length;if(result.ok)result.manifest=value;return result;
}
function inspectIdentity(id){if(!plain(id)||id.kind!=='texpage'||!['P4','P8','C16'].includes(id.mode))return {ok:false,reason:'UNSUPPORTED_IDENTITY'};if(id.mode!=='C16'){
 if(!Array.isArray(id.paletteRangeTokens)||id.paletteRangeTokens[0]!==0)return {ok:false,reason:'NONZERO_PALETTE_MIN_UNESTABLISHED'};
 if(id.paletteRangeTokens.length!==2||!integer(id.paletteRangeTokens[1],0,id.mode==='P4'?15:255))return {ok:false,reason:'INVALID_PALETTE_RANGE'};
 }else if(id.paletteRangeTokens!=null||id.paletteHash!=null)return {ok:false,reason:'UNEXPECTED_DIRECT_PALETTE'};
 return {ok:true,classification:'identity metadata only; no character or decoded-pixel proof'};}
function loadBundleChecked(directory,manifestBasename){
 const root=path.resolve(directory),rootStat=fs.lstatSync(root),same=(a,b)=>process.platform==='win32'?a.toLowerCase()===b.toLowerCase():a===b;
 if(!rootStat.isDirectory()||rootStat.isSymbolicLink()||!same(fs.realpathSync.native(root),root))throw Error('BUNDLE_ROOT_LINK_OR_NONCANONICAL');
 if(!basenameSafe(manifestBasename)||!manifestBasename.endsWith('.json'))throw Error('MANIFEST_BASENAME');
 const read=(name,expected,max)=>{
  if(!basenameSafe(name))throw Error('UNSAFE_BASENAME');const f=path.join(root,name),st=fs.lstatSync(f);if(!st.isFile()||st.isSymbolicLink())throw Error('NONREGULAR_OR_LINK:'+name);if((expected!==null&&st.size!==expected)||st.size>max)throw Error('FILE_SIZE:'+name);
  const fd=fs.openSync(f,fs.constants.O_RDONLY|(fs.constants.O_NOFOLLOW||0));try{const pinned=fs.fstatSync(fd),current=fs.statSync(f),nowRoot=fs.lstatSync(root);
   if(pinned.dev!==st.dev||pinned.ino!==st.ino||pinned.size!==st.size||!pinned.isFile()||current.dev!==pinned.dev||current.ino!==pinned.ino||nowRoot.dev!==rootStat.dev||nowRoot.ino!==rootStat.ino||!same(fs.realpathSync.native(f),f))throw Error('BUNDLE_CHANGED:'+name);
   const data=Buffer.alloc(st.size);let offset=0;while(offset<data.length){const n=fs.readSync(fd,data,offset,data.length-offset,offset);if(!n)throw Error('BUNDLE_CHANGED:'+name);offset+=n;}
   const after=fs.fstatSync(fd);if(after.size!==st.size||after.mtimeMs!==pinned.mtimeMs)throw Error('BUNDLE_CHANGED:'+name);return data;}finally{fs.closeSync(fd);}
 };
 const text=read(manifestBasename,null,MAX_MANIFEST),decoded=text.toString('utf8');
 if(!Buffer.from(decoded,'utf8').equals(text))return {ok:false,errors:[issue('MANIFEST_UTF8',manifestBasename,'Manifest contains invalid UTF8 bytes')],warnings:[]};
 const result=parseCapture(decoded,{manifestBasename});if(!result.ok)return result;
 const words=read(result.names.raw,RAW_BYTES,RAW_BYTES),known=read(result.names.known,KNOWN_BYTES,KNOWN_BYTES);let count=0;
 for(let i=0;i<known.length;i++){if(known[i]>1)return {ok:false,errors:[issue('KNOWN_MASK_VALUE','known['+i+']','Only0/1 mask bytes allowed')],warnings:[]};if(known[i]===1)count++;else if(words[i*2]||words[i*2+1])return {ok:false,errors:[issue('UNKNOWN_WORD_NOT_ZERO','raw['+i+']','Unknown GPU words must be zero')],warnings:[]};}
 if(!count)return {ok:false,errors:[issue('NO_KNOWN_WORDS','known','No usable original-upload-known words')],warnings:[]};
 result.knownWords=count;result.fileSha256=Object.fromEntries([[result.names.manifest,text],[result.names.raw,words],[result.names.known,known]].map(([n,b])=>[n,crypto.createHash('sha256').update(b).digest('hex')]));result.words=words;result.known=known;return result;
}
function loadBundle(directory,manifestBasename){try{return loadBundleChecked(directory,manifestBasename);}catch(e){const code=e.code==='ENOENT'?'MISSING_BUNDLE_FILE':/^[A-Z_]+(?::|$)/.test(e.message)?e.message.split(':')[0]:'BUNDLE_IO';return {ok:false,errors:[issue(code,e.path?path.basename(e.path):'',e.message)],warnings:[]};}}
// Existing independent mapper is supplied by the caller, never dynamically
// loaded/executed from capture contents. No helper process or image IO here.
function mapValidatedBundle(directory,manifestBasename,index,mapCapture,hash){
 const bundle=loadBundle(directory,manifestBasename);if(!bundle.ok)return bundle;
 if(!plain(index)||!Array.isArray(index.entries)||index.entries.length>100000||typeof mapCapture!=='function'||typeof hash!=='function')return {ok:false,errors:[issue('MAPPER_INPUT','','Provide bounded index metadata and trusted existing mapper/hash callbacks')],warnings:[]};
 const safe=[],quarantine={};for(const e of index.entries){const check=inspectIdentity(e&&e.identity);if(check.ok)safe.push(e);else quarantine[check.reason]=(quarantine[check.reason]||0)+1;}
 const mapped=mapCapture({...index,entries:safe},bundle.manifest,bundle.words,bundle.known,hash);
 return {ok:true,warnings:bundle.warnings,fileSha256:bundle.fileSha256,knownWords:bundle.knownWords,quarantinedIdentities:quarantine,mapped,realCaptureVerified:false};
}
module.exports={validateCapture,parseCapture,inspectIdentity,loadBundle,mapValidatedBundle};
