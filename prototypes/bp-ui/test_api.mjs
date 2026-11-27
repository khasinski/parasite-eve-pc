// The public "bp" service in the real game, as another mod uses it (battle_probe.c,
// PE_BP_TEST_API=1): status, table, a quote that changes nothing, a purchase and its "bp.changed"
// event, an upgrade-only grant spent on Heal 1, a pause the BP screens also see, and a screen
// purchase announced with no mod id. Loads a repo sample save (default B).
// Usage: node test_api.mjs GAME DISC1 OUT [card-1 slot]
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {mkdir, copyFile, readFile, writeFile} from 'node:fs/promises';
import {resolve, join} from 'node:path';
import assert from 'node:assert/strict';
import {sampleCards, loadCard1, SAMPLE} from './test_saves.mjs';
const [exe, disc, out] = process.argv.slice(2, 5).map(x => resolve(x));
const slot = +(process.argv[5] || SAMPLE.B);
const CONFIRM = 16384;

await mkdir(out, {recursive: true});
await sampleCards(out);
await writeFile(join(out, 'pe_mods.ini'), 'Manage mods/All mods=1\n');
const child = spawn(exe, ['--disc', disc, '--disc2', disc.replace('(Disc 1)', '(Disc 2)'), '--headless', '--frame-limit', '40000',
  '--control-at', '11000', '--pad-at', '10000:8', '--pad-at', '10010:0', '--dump-frame', join(out, 'frame.ppm')],
  {cwd: out, windowsHide: true, env: {...process.env, PE_CONFIG_HOME: out, PE_BP_TEST_API: '1', SDL_AUDIODRIVER: 'dummy', PSYZ_AUDIO_CLOCK: 'video'}});
const queue = [], waiting = [];
let closed = false;
createInterface({input: child.stdout}).on('line', l => { if (l.startsWith('{"event":"control"')) { const e = JSON.parse(l); waiting.length ? waiting.shift()(e) : queue.push(e); } });
child.stderr.resume();   // the game logs heavily to stderr: an unread pipe fills up and blocks it
child.on('exit', () => { closed = true; while (waiting.length) waiting.shift()(null); });
const next = () => queue.length ? Promise.resolve(queue.shift()) : closed ? Promise.resolve(null) :
  new Promise((ok, bad) => { const t = setTimeout(() => bad(Error('no answer from the game in 90 s')), 90000); waiting.push(e => { clearTimeout(t); ok(e); }); });
const advance = async (n, m = 0) => { if (closed) throw Error('game exited'); child.stdin.write(`advance ${n} ${m}\n`); const e = await next(); if (!e) throw Error('game exited'); return e; };
const results = join(out, 'bp_battle_results.txt');
try {
  await next();
  const st = await loadCard1(advance, slot);
  assert(st.character.level > 1, `save did not load (level ${st.character.level})`);
  for (let i = 0; i < 100; ++i) {   // a chapter card or dialogue: tap through until the probe ran
    if ((await readFile(results, 'utf8').catch(() => '')).includes('BP_API_DONE')) break;
    await advance(60); if (i % 4 === 3) await advance(8, CONFIRM);
  }
  await copyFile(join(out, 'frame.ppm'), join(out, 'after.ppm')).catch(() => {});
  const lines = (await readFile(results, 'utf8').catch(() => '')).split(/\r?\n/);   // text mode: CRLF on Windows
  const cases = lines.filter(l => l.startsWith('BP_BATTLE_CASE'));
  const done = lines.find(l => l.startsWith('BP_API_DONE'));
  console.log(cases.join('\n'));
  assert(done, 'the probe did not run');
  assert.match(done, /fail=0$/, done);
  await writeFile(join(out, 'proof.json'), JSON.stringify({passed: true, card: 1, slot, done, cases}, null, 2));
  console.log(`PASS: public bp service in the real game (${done})`);
} finally { if (!closed) { child.stdin.write('quit\n'); await new Promise(r => setTimeout(r, 1500)); child.kill(); } }
