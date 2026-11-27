// Current persistence/host closure layered AFTER the original pinned native overlay.
// All source bytes are verified before copying; no game assets or executables are read.
const fs=require('fs'),p=require('path'),c=require('crypto');
module.exports=function(source,destination,files){
 const src=p.resolve(source),dst=p.resolve(destination),allowed=new Set();
 if(!Array.isArray(files)||!files.length)throw Error('Missing compatibility source manifest');
 const checked=[];
 for(const entry of files){const rel=entry.path;
  if(typeof rel!=='string'||p.isAbsolute(rel)||rel.split(/[\\/]/).some(x=>!x||x==='.'||x==='..')||allowed.has(rel))throw Error('Invalid compatibility source');
  const from=p.resolve(src,rel),to=p.resolve(dst,rel);
  if(!from.startsWith(src+p.sep)||!to.startsWith(dst+p.sep)||fs.lstatSync(from).isSymbolicLink())throw Error('Compatibility containment');
  const data=fs.readFileSync(from,'utf8').replaceAll('\r\n','\n');
  if(c.createHash('sha256').update(data).digest('hex')!==entry.sha256)throw Error('Compatibility source mismatch '+rel);
  allowed.add(rel);checked.push({to,data});
 }
 // Remove obsolete grouped provider source files from this fresh export only.
 const modules=p.resolve(dst,'mods');if(!modules.startsWith(dst+p.sep))throw Error('Module containment');
 function prune(dir){if(!fs.existsSync(dir))return;for(const ent of fs.readdirSync(dir,{withFileTypes:true})){
  const file=p.join(dir,ent.name);if(ent.isSymbolicLink())throw Error('Export source link');
  if(ent.isDirectory())prune(file);else if(/\.(c|h|inc|cmake)$/.test(ent.name)||ent.name==='CMakeLists.txt'){
   const rel=p.relative(dst,file).replaceAll('\\','/');if(!allowed.has(rel))fs.unlinkSync(file);
  }
 }}prune(modules);
 for(const {to,data}of checked){fs.mkdirSync(p.dirname(to),{recursive:true});fs.writeFileSync(to,data);}
 return checked.length;
};
