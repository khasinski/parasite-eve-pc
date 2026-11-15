// Lossless conversion of game-rendered P6 captures; no image generation.
import {readFileSync,writeFileSync,readdirSync} from 'node:fs';
import {deflateSync} from 'node:zlib';
import {join} from 'node:path';
const table=Array.from({length:256},(_,n)=>{for(let i=0;i<8;i++)n=(n&1)?0xedb88320^(n>>>1):n>>>1;return n>>>0});
function chunk(type,data){const t=Buffer.from(type),all=Buffer.concat([t,data]);let crc=0xffffffff;for(const b of all)crc=table[(crc^b)&255]^(crc>>>8);const out=Buffer.alloc(data.length+12);out.writeUInt32BE(data.length);t.copy(out,4);data.copy(out,8);out.writeUInt32BE((crc^0xffffffff)>>>0,out.length-4);return out}
function convert(path){const ppm=readFileSync(path);const match=/^P6\s+(\d+)\s+(\d+)\s+255\s/.exec(ppm.subarray(0,100).toString('ascii'));if(!match)throw Error(`Unexpected PPM header: ${path}`);const width=+match[1],height=+match[2],start=match[0].length;assertSize(ppm.length-start,width*height*3);const scan=Buffer.alloc(height*(width*3+1));for(let y=0;y<height;y++)ppm.copy(scan,y*(width*3+1)+1,start+y*width*3,start+(y+1)*width*3);const header=Buffer.alloc(13);header.writeUInt32BE(width);header.writeUInt32BE(height,4);header[8]=8;header[9]=2;const png=Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]),chunk('IHDR',header),chunk('IDAT',deflateSync(scan)),chunk('IEND',Buffer.alloc(0))]);writeFileSync(path.replace(/\.ppm$/,'.png'),png)}
function assertSize(a,b){if(a!==b)throw Error(`PPM payload ${a} != ${b}`)}
for(const directory of process.argv.slice(2))for(const file of readdirSync(directory)){if(file.endsWith('.ppm'))convert(join(directory,file))}
