// Offline helper: the C export is the only source of fixture rows and values.
import { readFileSync, writeFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { resolve, dirname } from 'node:path';
const here=dirname(fileURLToPath(import.meta.url));
const exporter=process.argv[2];
if(!exporter)throw new Error('Usage: node build_preview.mjs /absolute/path/to/bp-ui-export.exe');
const json=execFileSync(resolve(exporter),{encoding:'utf8'}).trim();
const data=JSON.parse(json);
if(!Array.isArray(data.pages)||!data.pages.length)throw new Error('Empty preview catalog');
const template=readFileSync(resolve(here,'preview.template.html'),'utf8');
if(!template.includes('__BP_UI_DATA__'))throw new Error('Missing data slot');
const output=resolve(here,'preview.html');
writeFileSync(output,template.replace('__BP_UI_DATA__',json));
console.log(`Built ${output} (${data.pages.length} fixture screens). No game or network access.`);
