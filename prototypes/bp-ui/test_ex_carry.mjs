// EX Game carryover in the real game, for each F1 setting (Off / Keep purchased skills / Convert
// skills to BP). Loads a save, buys Poison 1 and Heal 1 through the real transaction service, starts
// an EX Game with the game's own command (battle_probe.c, PE_BP_TEST_EX=1) and checks what the new
// run starts with. Uses the repo's sample saves (save-editor/samples/bu00) by default.
// Usage: node test_ex_carry.mjs GAME DISC1 OUT [card-1 slot]   (default: save 6 = sample F)
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {mkdir, copyFile, readFile, writeFile} from 'node:fs/promises';
import {resolve, join} from 'node:path';
import assert from 'node:assert/strict';
import {sampleCards, loadCard1, SAMPLE} from './test_saves.mjs';
const [exe, disc, out] = process.argv.slice(2, 5).map(x => resolve(x));
const slot = +(process.argv[5] || SAMPLE.F);   // card 1 save (repo samples)
const button = {down: 64, confirm: 16384};

async function session(mode) {
  const dir = join(out, `mode${mode}`);
  await sampleCards(dir);
  await writeFile(join(dir, 'pe_mods.ini'), `Manage mods/All mods=1\nPE Enhancements/Bonus Points/EX Game carryover=${mode}\n`);
  const child = spawn(exe, ['--disc', disc, '--disc2', disc.replace('(Disc 1)', '(Disc 2)'), '--headless', '--frame-limit', '40000',
    '--control-at', '11000', '--pad-at', '10000:8', '--pad-at', '10010:0', '--dump-frame', join(dir, 'frame.ppm')],
    {cwd: dir, windowsHide: true, env: {...process.env, PE_CONFIG_HOME: dir, PE_BP_TEST_EX: '1', SDL_AUDIODRIVER: 'dummy', PSYZ_AUDIO_CLOCK: 'video'}});
  const queue = [], waiting = [];
  let closed = false, shot = 0;
  createInterface({input: child.stdout}).on('line', l => { if (l.startsWith('{"event":"control"')) { const e = JSON.parse(l); waiting.length ? waiting.shift()(e) : queue.push(e); } });
  child.stderr.resume();   // the game logs heavily to stderr: an unread pipe fills up and blocks it
  child.on('exit', () => { closed = true; while (waiting.length) waiting.shift()(null); });
  const next = () => queue.length ? Promise.resolve(queue.shift()) : closed ? Promise.resolve(null) :
    new Promise((ok, bad) => { const t = setTimeout(() => bad(Error('no answer from the game in 90 s')), 90000); waiting.push(e => { clearTimeout(t); ok(e); }); });
  const advance = async (n, m = 0) => { if (closed) throw Error('game exited'); child.stdin.write(`advance ${n} ${m}\n`); const e = await next(); if (!e) throw Error('game exited'); return e; };
  const snap = async label => copyFile(join(dir, 'frame.ppm'), join(dir, `${String(++shot).padStart(2, '0')}-${label}.ppm`)).catch(() => {});
  try {
    await next();
    let st = await loadCard1(advance, slot); await snap('loaded');
    assert(st.character.level > 1, `save did not load (level ${st.character.level})`);
    const results = join(dir, 'bp_battle_results.txt');
    for (let i = 0; i < 200; ++i) {   // chapter card / dialogue: tap through until the probe has run
      const text = await readFile(results, 'utf8').catch(() => '');
      if (text.includes('BP_EX_AFTER')) break;
      await advance(8, button.confirm); st = await advance(60);
    }
    await snap('after');
    const text = await readFile(results, 'utf8').catch(() => '');
    const field = (line, key) => +new RegExp(`${key}=(\\d+)`).exec(line)?.[1];
    const lines = text.split(/\r?\n/);   // the probe writes text mode: CRLF on Windows
    const before = lines.find(l => l.startsWith('BP_EX_BEFORE')), after = lines.find(l => l.startsWith('BP_EX_AFTER'));
    assert(before && after, `probe output missing:\n${text}`);
    return {before, after, field};
  } finally { if (!closed) { child.stdin.write('quit\n'); await new Promise(r => setTimeout(r, 1500)); child.kill(); } }
}

await mkdir(out, {recursive: true});
const report = [];
for (const mode of [0, 1, 2]) {
  const {before, after, field} = await session(mode);
  const bp = field(before, 'bp');
  assert.equal(field(before, 'buy_poison'), 0, before); assert.equal(field(before, 'buy_heal'), 0, before);
  assert.equal(field(after, 'level'), 1, 'the EX Game starts at level 1');
  assert.equal(field(after, 'bp'), bp, 'unspent BP carries (the game keeps it)');
  assert.equal(field(after, 'full_resets'), 0, 'run data restarts');
  if (mode === 0) { assert.equal(field(after, 'poison') + field(after, 'heal') + field(after, 'pool'), 0, after); }
  if (mode === 1) {
    assert.equal(field(after, 'poison'), 1); assert.equal(field(after, 'heal'), 1);
    assert.equal(field(after, 'carried_poison'), 1); assert.equal(field(after, 'carried_heal'), 1); assert.equal(field(after, 'pool'), 0);
  }
  if (mode === 2) { assert.equal(field(after, 'poison') + field(after, 'heal'), 0); assert.equal(field(after, 'pool'), 75 + 150); }
  report.push({mode, before, after});
  console.log(`mode ${mode}: ${after}`);
}
await writeFile(join(out, 'proof.json'), JSON.stringify({passed: true, card: 1, slot, report}, null, 2));
console.log('PASS: EX Game carryover Off / Keep / Convert in the real game');
