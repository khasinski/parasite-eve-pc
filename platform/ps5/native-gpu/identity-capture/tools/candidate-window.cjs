'use strict';
// Independent metadata selection; no images, VRAM, hashing helper or model IO.
const crypto=require('crypto');
const POLICY='texpage-min0-window-v1',owned=new WeakSet(),sha=s=>crypto.createHash('sha256').update(s).digest('hex');
const int=(n,a,b)=>Number.isSafeInteger(n)&&n>=a&&n<=b;
const object=x=>x!==null&&typeof x==='object'&&!Array.isArray(x);
function fail(code){throw Error(code);}
function metadataValue(v){if(v===undefined)return {missing:true};if(v===null||typeof v==='boolean')return v;if(typeof v==='number'&&Number.isFinite(v))return v;if(typeof v==='string'&&v.length<=4096)return v;if(Array.isArray(v)&&v.length<=4)return v.map(x=>{if(x&&typeof x==='object')fail('INDEX_METADATA_SHAPE');return metadataValue(x);});fail('INDEX_METADATA_SHAPE_OR_STRING_BOUND');}
function safeRelative(s){return typeof s==='string'&&s.length<=1024&&s.length>0&&!/[\\:\x00-\x1f<>"|?*]/.test(s)&&s.split('/').every(q=>q&&q!=='.'&&q!=='..'&&!/[. ]$/.test(q)&&!/^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)/i.test(q));}
function normalizeIdentity(id){
 if(!object(id)||typeof id.key!=='string'||id.key.length>4096)return {reason:'INVALID_IDENTITY_KEY'};
 if(id.kind!=='texpage')return {reason:'UPLOAD_LINEAGE_UNAVAILABLE'};
 if(!['P4','P8','C16'].includes(id.mode))return {reason:'UNSUPPORTED_MODE'};const factor={P4:4,P8:2,C16:1}[id.mode];
 if(typeof id.textureHash!=='string'||!/^[A-F0-9]{16}$/.test(id.textureHash))return {reason:'INVALID_TEXTURE_HASH'};
 if(!Array.isArray(id.sourceWords)||id.sourceWords.length!==2||id.sourceWords[0]!==256/factor||id.sourceWords[1]!==256)return {reason:'UNSUPPORTED_PAGE_DIMENSIONS'};
 const r=id.rectangleTexels;if(!Array.isArray(r)||r.length!==4||!r.every(n=>int(n,0,256))||!r[2]||!r[3]||r[0]+r[2]>256||r[1]+r[3]>256||r[0]%factor||r[2]%factor)return {reason:'UNSUPPORTED_OR_UNALIGNED_RECTANGLE'};
 if(factor>1){if(!Array.isArray(id.paletteRangeTokens)||id.paletteRangeTokens[0]!==0)return {reason:'NONZERO_PALETTE_MIN_UNESTABLISHED'};if(id.paletteRangeTokens.length!==2||!int(id.paletteRangeTokens[1],0,factor===4?15:255)||typeof id.paletteHash!=='string'||!/^[A-F0-9]{16}$/.test(id.paletteHash))return {reason:'INVALID_PALETTE_FIELDS'};}
 else if(id.paletteHash!==null||id.paletteRangeTokens!==null)return {reason:'UNEXPECTED_DIRECT_PALETTE'};
 const fields={kind:id.kind,mode:id.mode,textureHash:id.textureHash,paletteHash:id.paletteHash,sourceWords:[...id.sourceWords],rectangleTexels:[...r],paletteRangeTokens:id.paletteRangeTokens&&[...id.paletteRangeTokens]};
 if(id.key!==JSON.stringify(fields))return {reason:'IDENTITY_KEY_MISMATCH'};
 return {identity:{...fields,key:id.key}};
}
function freeze(x){if(x&&typeof x==='object'){Object.values(x).forEach(freeze);Object.freeze(x);}return x;}
function prepareIndex(index){
 if(!object(index)||!Array.isArray(index.entries)||index.entries.length>100000)fail('INDEX_ENTRY_BOUND');
 const groups=new Map(),blockedKeys=new Set(),quarantine={},records=[];let bytes=0;
 for(const e of index.entries){const n=normalizeIdentity(e&&e.identity);let reason=n.reason||null;
  const disc=typeof(e&&e.disc)==='string'&&/^[A-Za-z0-9_-]{1,64}$/.test(e.disc)?e.disc:null;
  const relative=safeRelative(e&&e.relative)?e.relative:null;
  const digest=typeof(e&&e.png&&e.png.sha256)==='string'&&/^[a-f0-9]{64}$/i.test(e.png.sha256)?e.png.sha256.toLowerCase():null;
  const key=n.identity?n.identity.key:(typeof(e&&e.identity&&e.identity.key)==='string'?e.identity.key.slice(0,4096):null);
  const raw=object(e&&e.identity)?Object.fromEntries(['key','kind','mode','textureHash','paletteHash','sourceWords','rectangleTexels','paletteRangeTokens'].map(k=>[k,metadataValue(e.identity[k])])):metadataValue(e&&e.identity);
  const record=JSON.stringify({identity:raw,disc:metadataValue(e&&e.disc),relative:metadataValue(e&&e.relative),sha256:metadataValue(e&&e.png&&e.png.sha256),reason});bytes+=Buffer.byteLength(record);if(bytes>64*1024*1024)fail('INDEX_METADATA_BYTE_BOUND');records.push(record);
  if(reason){if(key)blockedKeys.add(key);quarantine[reason]=(quarantine[reason]||0)+1;continue;}
  if(!groups.has(key))groups.set(key,{key,mode:n.identity.mode,identity:n.identity,aliases:[],invalid:false});const group=groups.get(key);
  if(!disc||!relative)group.invalid=true;
  group.aliases.push({disc,relative,png:{sha256:digest},identity:n.identity});
 }
 const ready=[];for(const group of [...groups.values()].sort((a,b)=>a.key<b.key?-1:a.key>b.key?1:0)){
  let reason=blockedKeys.has(group.key)?'QUARANTINED_ALIAS_IDENTITY':group.invalid?'UNSAFE_ALIAS_METADATA':group.aliases.some(x=>!x.png.sha256)?'MISSING_FILE_DIGEST':new Set(group.aliases.map(x=>x.png.sha256)).size>1?'IDENTITY_FILE_COLLISION':null;
  if(reason){quarantine[reason]=(quarantine[reason]||0)+1;continue;}
  group.aliases.sort((a,b)=>{const x=JSON.stringify([a.disc,a.relative,a.png.sha256]),y=JSON.stringify([b.disc,b.relative,b.png.sha256]);return x<y?-1:x>y?1:0;});ready.push(group);
 }
 records.sort();const prepared=freeze({policy:POLICY,indexDigest:sha(POLICY+'\n'+records.join('\n')),inputEntries:index.entries.length,metadataBytes:bytes,groups:ready,quarantine});owned.add(prepared);return prepared;
}
function selectWindow(prepared,{mode,limit=256,cursor=null,indexDigest=null}={}){
 if(!owned.has(prepared))fail('PREPARED_INDEX_REQUIRED');if(!['P4','P8','C16'].includes(mode))fail('MODE_REQUIRED');if(!int(limit,1,4096))fail('WINDOW_LIMIT');if(indexDigest!==null&&indexDigest!==prepared.indexDigest)fail('INDEX_DIGEST_MISMATCH');
 const groups=prepared.groups.filter(x=>x.mode===mode);let offset=0;
 if(cursor!==null){if(!object(cursor)||cursor.version!==1||cursor.policy!==POLICY||cursor.indexDigest!==prepared.indexDigest||cursor.mode!==mode||!int(cursor.nextOffset,0,groups.length))fail('STALE_OR_INVALID_CURSOR');offset=cursor.nextOffset;}
 const chosen=groups.slice(offset,offset+limit),next=offset+chosen.length,complete=next===groups.length;
 return {indexDigest:prepared.indexDigest,policy:POLICY,mode,offset,selectedGroups:chosen.length,totalEligibleGroups:groups.length,remainingGroups:groups.length-next,selectionComplete:complete,budgetExhausted:!complete,matchingComplete:false,status:complete?'ALL_ELIGIBLE_GROUPS_SELECTED_NOT_HASH_PROVEN':'WINDOW_BUDGET_EXHAUSTED_NOT_NO_MATCH',nextCursor:complete?null:{version:1,policy:POLICY,indexDigest:prepared.indexDigest,mode,nextOffset:next},quarantine:{...prepared.quarantine},entries:JSON.parse(JSON.stringify(chosen.flatMap(x=>x.aliases)))};
}
module.exports={prepareIndex,selectWindow,normalizeIdentity,safeRelative};
