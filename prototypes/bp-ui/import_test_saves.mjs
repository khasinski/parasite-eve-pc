// Import existing PS1 test saves into a private native profile, without overwriting slots.
import {readFile,mkdir,readdir,writeFile} from 'node:fs/promises';
import {inflateRawSync} from 'node:zlib';
import {resolve,join} from 'node:path';
import assert from 'node:assert/strict';
const [archiveArg,profileArg]=process.argv.slice(2);
assert(archiveArg&&profileArg,'Usage: node import_test_saves.mjs ARCHIVES PROFILE');
const archiveDir=resolve(archiveArg),profile=resolve(profileArg),destination=join(profile,'memory-cards','bu10');
await mkdir(destination,{recursive:true});
const selected=['ParasiteEveSave-Day03_1-Beginning.zip','ParasiteEveSave-Day04_1-Beginning.zip','ParasiteEveSave-Day05_01-Beginning.zip','ParasiteEveSave-Day02_1-Beginning.zip','ParasiteEveSave-Day01_Alligator.zip','ParasiteEveSave-Day06_Beginning.zip'];
function cardFromZip(zip){
 for(let p=0;p+46<=zip.length;p++)if(zip.readUInt32LE(p)===0x02014b50){
  const nameLength=zip.readUInt16LE(p+28),name=zip.subarray(p+46,p+46+nameLength).toString();
  if(!name.endsWith('.mcr'))continue;
  const method=zip.readUInt16LE(p+10),size=zip.readUInt32LE(p+20),offset=zip.readUInt32LE(p+42);
  assert.equal(zip.readUInt32LE(offset),0x04034b50);
  const start=offset+30+zip.readUInt16LE(offset+26)+zip.readUInt16LE(offset+28),bytes=zip.subarray(start,start+size);
  assert(method===0||method===8,'Unsupported ZIP compression');
  const card=method===8?inflateRawSync(bytes,{maxOutputLength:131072}):bytes;
  assert.equal(card.length,131072);assert.equal(card.subarray(0,2).toString(),'MC');return card;
 }
 throw Error('Memory card not found');
}
function saveFromCard(card){
 for(let block=1;block<=15;block++){
  const directory=card.subarray(block*128,(block+1)*128);
  if(directory[0]!==0x51)continue;
  const name=directory.subarray(10,30).toString('ascii').replace(/\0.*$/,'');
  if(!/^BASLUS-00662/.test(name))continue;
  assert.equal(directory.readUInt32LE(4),8192,'Expected one-block PE save');
  assert.equal(directory.readUInt16LE(8),65535,'Unexpected block chain');
  let checksum=0;for(let i=0;i<127;i++)checksum^=directory[i];assert.equal(checksum,directory[127]);
  const data=card.subarray(block*8192,(block+1)*8192);assert.equal(data.subarray(0,2).toString(),'SC');return {data,name};
 }
 throw Error('PE save not found');
}
const manifest=[];
for(const archive of selected){
 const {data,name}=saveFromCard(cardFromZip(await readFile(join(archiveDir,archive))));
 let slot=null;
 const existing=await readdir(destination);
 for(const file of existing.filter(x=>/^BASLUS-00662/.test(x))){if((await readFile(join(destination,file))).equals(data)){slot=file;break;}}
 if(!slot)for(let i=0;i<15;i++){
  const file=name.slice(0,19)+String.fromCharCode(65+i);
  if(existing.some(x=>x.startsWith('BASLUS-00662')&&x[19]===file[19]))continue;
  try{await writeFile(join(destination,file),data,{flag:'wx'});slot=file;break;}catch(e){if(e.code!=='EEXIST')throw e;}
 }
 assert(slot,'No free test-save slot on memory card 2');
 manifest.push({card:2,slot:slot[19].charCodeAt(0)-64,file:slot,source:archive,bytes:data.length});
}
await writeFile(join(profile,'test-saves.json'),JSON.stringify(manifest,null,2)+'\n');
console.log(JSON.stringify(manifest,null,2));
