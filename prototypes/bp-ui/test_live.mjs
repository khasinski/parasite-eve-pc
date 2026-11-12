// Live BP transactions in the real game: load the Day 3 save (card 2, save 1), open the BP
// screens through Aya's menu and check that confirmed purchases and a refund change the game's
// own BP / stat table by exactly the quoted amounts, and that No and refusals change nothing.
// Usage: node test_live.mjs GAME DISC OUTPUT PROFILE
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {mkdir, writeFile, copyFile, readdir, cp} from 'node:fs/promises';
import {resolve, join} from 'node:path';
import assert from 'node:assert/strict';
const [exe, disc, output, profile] = process.argv.slice(2, 6).map(x => resolve(x));
const slot = +(process.argv[6] || 1);   // save slot on card 2
const button = {up: 16, down: 64, left: 128, right: 32, confirm: 16384, back: 8192, menu: 4096};
await mkdir(output, {recursive: true});
for (const port of ['bu00', 'bu10']) {
  await mkdir(join(output, port), {recursive: true});
  for (const file of await readdir(join(profile, 'memory-cards', port)).catch(() => []))
    await copyFile(join(profile, 'memory-cards', port, file), join(output, port, file));
}
const config = join(output, 'config');
await cp(profile, config, {recursive: true, filter: s => !s.includes('memory-cards')});
const child = spawn(exe, ['--disc', disc, '--disc2', disc.replace('(Disc 1)', '(Disc 2)'), '--headless', '--frame-limit', '60000', '--control-at', '11000',
  '--pad-at', '10000:8', '--pad-at', '10010:0', '--dump-frame', join(output, 'frame.ppm')],
  {cwd: output, windowsHide: true, env: {...process.env, PE_CONFIG_HOME: config, PE_BP_UI_PAGE: 'aya',
    PE_BP_UI_TEST_JUNK: process.env.PE_BP_UI_TEST_JUNK || '',
    SDL_AUDIODRIVER: 'dummy', PSYZ_AUDIO_CLOCK: 'video'}});
let log = '', ui = null, queue = [], waiting = [], closed = false, shot = 0;
const steps = [];
child.on('exit', () => { closed = true; while (waiting.length) waiting.shift()(null); });
function feed(line) {
  log += line + '\n';
  if (line.startsWith('BP_UI_STATE ')) {
    const m = /open=(\d+) page=(\S+) row=(\S+) selected=(\d+) top=(\d+) detail=(\d+) pages=(\d+) pending=(\d+) help=(\d+) notice=(.*)/.exec(line);
    if (m) ui = {open: +m[1], page: m[2], row: m[3], selected: +m[4], pending: +m[8], notice: m[10]};
  }
  if (line.startsWith('{"event":"control"')) { const e = JSON.parse(line); waiting.length ? waiting.shift()(e) : queue.push(e); }
}
createInterface({input: child.stdout}).on('line', feed);
createInterface({input: child.stderr}).on('line', feed);
async function next() {
  if (queue.length) return queue.shift();
  return await new Promise((ok, bad) => { const t = setTimeout(() => bad(Error('Checkpoint timeout')), 90000);
    waiting.push(e => { clearTimeout(t); e ? ok(e) : bad(Error('Game exited')); }); });
}
async function advance(frames, mask = 0) { child.stdin.write(`advance ${frames} ${mask}\n`); return await next(); }
async function press(key) { await advance(8, button[key]); return await advance(16); }
async function snap(label) { await copyFile(join(output, 'frame.ppm'), join(output, `${String(++shot).padStart(2, '0')}-${label}.ppm`)); }
async function seek(row) {
  for (let guard = 0; ui.row !== row && guard < 40; ++guard) await press('down');
  assert.equal(ui.row, row, `row ${row} not reached`);
}
async function yes() { await press('left'); return await press('confirm'); }   // the native Yes/No starts on No
const bp = e => e.character.bonus_points, stat = (e, i) => e.character.stat_allocation[i];
try {
  await next();
  await advance(8, button.confirm); await advance(300);
  await advance(8, button.down); await advance(24); await advance(8, button.confirm); await advance(400);
  for (let i = 1; i < slot; ++i) { await advance(8, button.down); await advance(24); }
  await snap(`slot-${slot}`);
  await advance(8, button.confirm); await advance(300);
  let state = await advance(8, button.confirm); state = await advance(800);
  assert(state.character.level > 1, 'the save must load, not start a new game');
  console.log(`slot ${slot}: level ${state.character.level}, BP ${state.character.bonus_points}`);
  for (let i = 0; i < 60 && !(ui && ui.open); ++i) {
    const talking = state.dialogue?.some(d => d.state !== 0);
    state = talking || (state.move_lock & 1) ? (await advance(10, button.confirm), await advance(120)) : await press('menu');
  }
  assert(ui && ui.open && ui.page === 'aya', 'BP screen did not open on the Aya tab');
  assert.match(log, /live=1/, 'pe_bp service not running');
  await snap('aya-open');
  const start = await advance(4);
  steps.push({step: 'open', bp: bp(start), stats: start.character.stat_allocation});

  // 1. No leaves everything unchanged.
  await seek('aya.5');
  await press('confirm'); assert.equal(ui.pending, 1); await snap('active-confirm');
  let e = await press('confirm');
  assert.match(ui.notice, /Cancelled/); assert.equal(bp(e), bp(start)); assert.equal(stat(e, 5), stat(start, 5));

  // 2. Active Time: exactly 100 BP, entry 5 rises, nothing else in the stat table moves.
  const before5 = await advance(2);
  if (bp(before5) >= 100) {
    await press('confirm'); e = await yes(); await snap('active-bought');
    assert.equal(bp(e), bp(before5) - 100, 'Active Time must cost 100 BP');
    assert(stat(e, 5) > stat(before5, 5), 'Active Time must rise');
    for (const i of [0, 1, 2, 3, 4, 6]) assert.equal(stat(e, i), stat(before5, i), `stat ${i} must not move`);
    steps.push({step: 'active time', bp: bp(e), notice: ui.notice});
  }

  // 3. Poison rank 1: the quoted 75 BP.
  await seek('def.poison');
  const beforeP = await advance(2);
  await press('confirm'); e = await yes(); await snap('poison-bought');
  if (bp(beforeP) >= 75) {
    assert.equal(bp(e), bp(beforeP) - 75, 'Poison rank 1 must cost 75 BP');
    assert.match(ui.notice, /Poison rank 1/);
  } else assert.equal(bp(e), bp(beforeP));
  steps.push({step: 'poison', bp: bp(e), notice: ui.notice});

  // 4. A gun stat: Guns tab, first gun, Attack.
  await press('left'); await press('left'); assert.equal(ui.page, 'guns'); await snap('guns-list');
  await press('confirm'); assert.match(ui.page, /^gear\.\d+$/); await snap('gun-stats');
  const beforeG = await advance(2);
  await press('confirm'); e = await yes(); await snap('gun-bought');
  if (bp(beforeG) >= 100) assert.equal(bp(e), bp(beforeG) - 100); else assert.equal(bp(e), bp(beforeG));
  steps.push({step: 'gun attack', bp: bp(e), notice: ui.notice, page: ui.page});
  await press('back'); assert.equal(ui.page, 'guns');

  // 5. Close, then reopen at Wayne's reset (test shortcut) and refund Poison.
  await press('back'); assert.equal(ui.open, 0);
  const after = await advance(30);
  steps.push({step: 'closed', bp: bp(after)});

  // 6. Reopen through Aya's menu row, go to Reset BP and refund Poison (75 BP, 55 Junk).
  await press('up'); await snap('menu-row'); await press('confirm');
  assert(ui.open && ui.page === 'preview', 'BP Upgrades row must open the BP home page');
  await seek('home.reset'); await press('confirm'); assert.equal(ui.page, 'reset');
  await press('confirm'); assert.equal(ui.page, 'reset.one');
  await seek('refund.def.poison'); await snap('refund-poison');
  const beforeR = await advance(2);
  await press('confirm');
  if (ui.pending) {
    e = await yes(); await snap('refund-done');
    assert.equal(bp(e), bp(beforeR) + 75, 'refund must return exactly 75 BP');
    assert.match(ui.notice, /Refunded 75 BP for \d+ Junk/);
  } else {
    e = await advance(2);
    assert.match(ui.notice, /Need \d+ more Junk|Junk economy/); assert.equal(bp(e), bp(beforeR));
  }
  steps.push({step: 'refund poison', bp: bp(e), notice: ui.notice});
  await press('confirm'); e = await advance(2);   // now empty: refused, unchanged
  assert.equal(bp(e), steps.at(-1).bp);
  steps.push({step: 'refund empty', notice: ui.notice});
  await press('right'); assert.equal(ui.page, 'reset.mastery'); await snap('refund-mastery');
  await press('back'); assert.equal(ui.page, 'reset');

  // 7. Full reset: Active Time steps bought on this screen + Item Capacity's exact steps.
  await seek('reset.all.open'); await press('confirm'); assert.equal(ui.page, 'reset.all');
  await seek('refund.all.confirm'); await snap('full-reset');
  const beforeF = await advance(2);
  await press('confirm');
  if (ui.pending) {
    e = await yes(); await snap('full-reset-done');
    const m = /Refunded (\d+) BP for (\d+) Junk/.exec(ui.notice);
    assert(m, `full reset notice: ${ui.notice}`);
    assert.equal(bp(e), bp(beforeF) + +m[1]);
    assert.equal(stat(e, 5), stat(start, 5), 'Active Time must return to its value before this session');
    for (const i of [0, 1, 2, 3, 4]) assert.equal(stat(e, i), stat(beforeF, i), `stat ${i} must not move`);
    steps.push({step: 'full reset', bp: bp(e), stats: e.character.stat_allocation, notice: ui.notice});
  } else {
    e = await advance(2); assert.equal(bp(e), bp(beforeF));
    assert.deepEqual(e.character.stat_allocation, beforeF.character.stat_allocation);
    steps.push({step: 'full reset refused', notice: ui.notice});
  }
  await press('back'); await press('back'); await press('back');
  await writeFile(join(output, 'proof.json'), JSON.stringify({passed: true, save: 'Day 3, card 2 save 1', steps}, null, 2));
  child.stdin.write('quit\n'); await new Promise(r => child.once('exit', r));
  console.log('PASS: live purchases change BP and Aya stats by the quoted amounts; No changes nothing.');
  console.log(JSON.stringify(steps));
} finally { await writeFile(join(output, 'game.log'), log); if (!closed) child.kill(); }
