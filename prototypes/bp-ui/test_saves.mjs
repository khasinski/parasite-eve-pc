// The repo's own sample saves (save-editor/samples/bu00), so anyone can run the in-game tests.
// They go on memory card 1; card 2 stays empty. Slots on card 1, as the load screen lists them:
//   1 A: level 7,  132 BP (+31 pending), Heal 1, Scan, Slow
//   2 B: level 14, 578 BP, Heal 1-2, Detox, Barrier, Scan, Slow
//   3 C: level 18, 136 BP, adds Energy Shot, Confuse
//   4 D: level 24, 271 BP, Day 5, adds Heal 3, Haste
//   5 E: level 32, 4935 BP, Day 5 Museum, 14 powers incl. Gene Heal, Preraise, Full Recover
//   6 F: level 34, 5348 BP, Day 6 (Disc 2, opens on the chapter card), every power
//   7 G: level 1,  8348 BP, an EX Game start
import {mkdir, copyFile, readdir} from 'node:fs/promises';
import {join, resolve, dirname} from 'node:path';
import {fileURLToPath} from 'node:url';

export const SAMPLES = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..', 'save-editor', 'samples', 'bu00');
export const SAMPLE = {A: 1, B: 2, C: 3, D: 4, E: 5, F: 6, G: 7};

/* Card folders for a headless run in `dir` (headless reads bu00 / bu10 from the working directory). */
export async function sampleCards(dir) {
  await mkdir(join(dir, 'bu00'), {recursive: true});
  await mkdir(join(dir, 'bu10'), {recursive: true});
  for (const f of await readdir(SAMPLES)) await copyFile(join(SAMPLES, f), join(dir, 'bu00', f));
}

/* Title > Continue > card 1 > save `slot`, with the game's own pad input. `advance(frames, mask)`
 * is the test's control call. Returns the state after the load. */
export async function loadCard1(advance, slot) {
  await advance(8, 16384); await advance(300);            // Continue
  await advance(8, 16384); await advance(400);            // memory card 1
  for (let i = 1; i < slot; ++i) { await advance(8, 64); await advance(24); }
  await advance(8, 16384); await advance(300);
  await advance(8, 16384);
  return await advance(800);
}
