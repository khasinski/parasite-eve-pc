// BP battle effects in a real battle (battle_probe.c, PE_BP_TEST_BATTLE=1): loads a repo sample save,
// walks until a random encounter and lets the probe run its cases. Needs a preview build that also
// has pe_qol, whose "Battle frequency: Every check" makes the encounter come quickly:
//   -DPE_MOD_LIST="pe_economy;pe_shops;pe_bp;pe_qol" with headless_inject.cmake
// Usage: node test_battle.mjs GAME DISC1 OUT SAVE
//   SAVE: a card-1 slot of the repo samples, or a path to one 8 KB card file (BASLUS-00662...)
//   that is loaded alone from card 1. Every repo sample starts in a save room with no random
//   encounters (checked 2026-11-12), so this needs a save near enemies.
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {mkdir, copyFile, readFile, writeFile, stat} from 'node:fs/promises';
import {resolve, join, basename} from 'node:path';
import assert from 'node:assert/strict';
import {sampleCards, loadCard1} from './test_saves.mjs';
const [exe, disc, out] = process.argv.slice(2, 5).map(x => resolve(x));
const saveArg = process.argv[5];
assert(saveArg, 'SAVE is required: a repo sample slot or a card file near enemies');
const saveFile = isNaN(+saveArg) ? resolve(saveArg) : null;
const slot = saveFile ? 1 : +saveArg;
const UP = 16, RIGHT = 32, DOWN = 64, LEFT = 128, CONFIRM = 16384;
const DIRECTIONS = [UP, UP | RIGHT, RIGHT, DOWN | RIGHT, DOWN, DOWN | LEFT, LEFT, UP | LEFT];

await mkdir(out, {recursive: true});
if (saveFile) {
  assert.equal((await stat(saveFile)).size, 8192, 'a card file is 8192 bytes');
  await mkdir(join(out, 'bu00'), {recursive: true}); await mkdir(join(out, 'bu10'), {recursive: true});
  await copyFile(saveFile, join(out, 'bu00', 'BASLUS-006620000000A'));   // the load list shows files at their slot letter
} else await sampleCards(out);
await writeFile(join(out, 'pe_mods.ini'), 'Manage mods/All mods=1\nPE Enhancements/Combat/Battle frequency=3\nCombat/Battle frequency=3\n');
const child = spawn(exe, ['--disc', disc, '--disc2', disc.replace('(Disc 1)', '(Disc 2)'), '--headless', '--frame-limit', '200000',
  '--control-at', '11000', '--pad-at', '10000:8', '--pad-at', '10010:0', '--dump-frame', join(out, 'frame.ppm')],
  {cwd: out, windowsHide: true, env: {...process.env, PE_CONFIG_HOME: out, PE_BP_TEST_BATTLE: '1', SDL_AUDIODRIVER: 'dummy', PSYZ_AUDIO_CLOCK: 'video'}});
const queue = [], waiting = [];
let closed = false, shot = 0;
createInterface({input: child.stdout}).on('line', l => { if (l.startsWith('{"event":"control"')) { const e = JSON.parse(l); waiting.length ? waiting.shift()(e) : queue.push(e); } });
child.stderr.resume();
child.on('exit', () => { closed = true; while (waiting.length) waiting.shift()(null); });
const next = () => queue.length ? Promise.resolve(queue.shift()) : closed ? Promise.resolve(null) :
  new Promise((ok, bad) => { const t = setTimeout(() => bad(Error('no answer from the game in 90 s')), 90000); waiting.push(e => { clearTimeout(t); ok(e); }); });
const advance = async (n, m = 0) => { if (closed) throw Error('game exited'); child.stdin.write(`advance ${n} ${m}\n`); const e = await next(); if (!e) throw Error('game exited'); return e; };
const snap = async label => copyFile(join(out, 'frame.ppm'), join(out, `${String(++shot).padStart(2, '0')}-${label}.ppm`)).catch(() => {});
// Aya is the actor marked "player" in the control state (pedebug's State.player does the same).
const player = st => (st.actors || []).find(a => a.player);
const inBattle = st => (st.flags & 2) && player(st);
const freeField = st => player(st) && !st.menu && !(st.flags & 0x2006) && !(st.game_flags & 0xB600);
const results = join(out, 'bp_battle_results.txt');
try {
  await next();
  let st = await loadCard1(advance, slot);
  assert(st.character && st.character.level >= 1, 'the save must load');
  for (let i = 0; i < 80 && !freeField(st) && !inBattle(st); ++i) { await advance(8, CONFIRM); st = await advance(60); }   // chapter card, dialogue
  await snap('field');
  for (let walked = 0, d = 0; !inBattle(st) && walked < 12000; d += 3)
    for (let k = 0; k < 12 && !inBattle(st); ++k) { st = await advance(10, DIRECTIONS[d % 8]); walked += 10; }
  assert(inBattle(st), 'no encounter while walking');
  await snap('battle');
  for (let i = 0; i < 450; ++i) {
    if ((await readFile(results, 'utf8').catch(() => '')).includes('BP_BATTLE_DONE')) break;
    await advance(10);
  }
  const text = await readFile(results, 'utf8').catch(() => '');
  const lines = text.split(/\r?\n/);   // the probe writes text mode: CRLF on Windows
  const cases = lines.filter(l => l.startsWith('BP_BATTLE_CASE'));
  const done = lines.find(l => l.startsWith('BP_BATTLE_DONE'));
  console.log(cases.join('\n'));
  assert(done, 'the probe did not finish');
  assert.match(done, /fail=0$/, done);
  await writeFile(join(out, 'proof.json'), JSON.stringify({passed: true, save: saveFile ? basename(saveFile) : `repo sample slot ${slot}`, done, cases}, null, 2));
  console.log(`PASS: ${done}`);
} finally { if (!closed) { child.stdin.write('quit\n'); await new Promise(r => setTimeout(r, 1500)); child.kill(); } }
