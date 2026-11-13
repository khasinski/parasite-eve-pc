# Costume style prototype

Off by default. F1 > PE Enhancements > Collection > Wardrobe > Costume abilities
switches between cosmetic-only vanilla combat and the worn costume's shared style.
Only change costumes/mode outside battle. Each encounter captures one style.
The native Wardrobe also accepts Select to toggle the same saved preference.
Holding Select cannot repeatedly toggle it, and fullscreen preview cannot change it.
Style labels show on/off, later, or blocked (unsupported partial-bar policy).
The help line cycles the selected style's benefit, tradeoff and browser controls.
F1 also shows Selected style, Style benefit and Style tradeoff for the costume
highlighted in its browser, including costumes not yet purchased. The existing
Costume style row describes the worn/active style. Previewing never wears an
outfit, spends Junk, changes the mode or changes the current encounter snapshot.

## Layers

- `pe_style_catalog.c`: stable style definitions, descriptions, numeric parameters
  and explicit namespaced costume mappings. No game globals or asset mutation.
- `pe_style_engine.c`: deterministic encounter state, fractional charge
  scaling and damage rules. No native pointers, files or UI.
- `pe_style_quickstep.c`: Quickstep's rules only (breakpoints, kill trigger,
  refund, burst, Overextended, HP-pip query). Pure logic; every number and
  switch is `PeQuickstepTuning` in the catalog.
- `pe_style_runtime.c`: the only native combat adapter: snapshots the successfully
  applied appearance, receives typed hooks, applies engine results and draws cues.
- Wardrobe registry/skin loader: retains ownership and model-loading responsibility;
  no combat logic is placed in costume loaders or generated asset tables.

New styles get a stable ID and one definition; mappings reuse that definition.
Add specialized rules to the engine/event boundary with disc-free tests. Do not
copy hooks into each costume, infer abilities from display names, or alter the
existing `PeCostume` service layout. Unknown packs and deferred rigs stay Original.
All four styles now use the same catalog, engine and native adapter. Unknown
pack mappings and deferred costumes still resolve to Original.

## Revision 6.4 build status (branch `feature/costume-styles-rev6`)

| Style | Built | Tests | Playtested |
| --- | --- | --- | --- |
| Quickstep | yes | unit | no |
| Channeler | yes | unit | no |
| Striker (falloff) | no | | |
| Bulwark (Rally, Exposed, Brace) | no | | |

**TODO — one shared playtest package after all four are built.** Not one per
style. When Striker and Bulwark land, build a single package (extend local
`tools/New-CostumeStylePlaytest.ps1`) that:
- holds copied saves only, under `build-styles/` (never committed);
- starts with Costume + playstyle on, one save per style, each wearing a
  costume of that style at a spot with a regular fight and a boss nearby;
- includes the playtest sheet from the build spec section 10 (fun gate: did
  they see each con coming, could they say how to avoid it, did it feel fair);
- turns on the `costume_style` log so refunds, Overextended, charges, crack,
  break, counters and Rally can be checked after a session.

## Channeler, Revision 6.4 (current; supersedes the Channeler notes below)

Source of truth: build spec section 4.4. Rules in `pe_style_channeler.c`,
numbers in `.channeler` in `pe_style_catalog.c`.

- Charges: a completed weapon action that damaged a living enemy adds one, up
  to `max_charges` (2). Cancelled, interrupted, zero-damage and
  encounter-ending actions add none.
- Spending: the next support power (0-5, 7-12, 18) spends all charges for
  `discount[charges]` % off (50 / 100). At max charges Heal 1-3 also heal
  `overchannel_percent` (25%) more. Energy Shot, Liberate and attack powers
  neither use nor clear charges. The refund is added in `PE_HOOK_SPELL`
  before the native subtraction, from the cost after other mods and armor.
- Menu and affordability: `PE_HOOK_POWER_COST` (LATE) shows the discounted
  cost while charges are held. The game's menu cost, its can-afford check and
  the queued PE reservation all read that one value, so a charged player can
  pick what the charges pay for. Our own raw-cost query skips the discount.
- Backlash: only at max charges. An HP-loss hit (sources 1-3) cracks the
  weave; a later hit breaks one charge; casting clears the crack. Hits within
  `backlash_group_frames` (30 = 1 s, refreshed by each hit) of the last one
  count as the same enemy attack. No PE is ever taken.
- Passive regen is native (`pe_recovery_percent` 100 leaves the hook
  untouched).
- Cues: PE label pulses cyan on each charge and on a cast, glows cyan at full
  charge, flickers orange once cracked, and drops to no glow after a break.

**Changing it.** `max_charges` (up to 4) with matching `discount[]`;
`overchannel_percent` 0 turns Overchannel off; `backlash` 0 turns the con
off; `backlash_group_frames` for how close hits count as one attack;
`pe_recovery_percent` below 100 brings back slower regen. Crack color and
flicker speed: `CUE_CRACK` and `CRACK_FLICKER_FRAMES` in `pe_style_runtime.c`.

**Not done yet.** Barrier's menu cost and can-afford check stay native (its
cost is a third of max PE, not a table cost, so the cost hook never sees
it); the cast discount still applies. "One enemy attack" is a 1 s window, not
the enemy's real attack sequence. The PE-menu discount display is unit-tested
only; check it in game. Not playtested.

## Quickstep, Revision 6.4 (current; supersedes the Quickstep notes below)

Source of truth: `Parasite Eve Costume Combat Styles Build Spec.md` section 4.1.

- Trigger: a weapon hit (gun or club, not PE) that takes an enemy or part from
  above to at-or-below 50% of its current max HP for the first time, or a kill
  while other enemies remain. One refund per action. Breakpoints are tracked
  per enemy combatant (parts with their own HP count separately); healing never
  re-arms one; a changed max HP (new boss phase) does.
- Refund: 20% of a full bar, added in `PE_HOOK_PLAYER_ACTION_END` (LATE, after
  the native reset), clamped to full. None on the encounter-ending action.
- Burst: x1.25 movement for 90 controllable battle frames (3 s at 30 a
  second), ticked in `PE_HOOK_BATTLE_UPDATE` only while `controllable()`; ends
  early when Aya's next action begins.
- Overextended: eligible incoming losses (HP-loss sources 1-3) x1.20 during
  the burst, through the same `pe_style_damage` call as Bulwark's guard.
- Cues: AT marker cyan for 12 frames when the refund lands, then red while the
  burst lasts. HP pip: the target marker turns dark grey when the targeted
  enemy is at or below half (its breakpoint is spent), for guns and clubs, in
  or out of range.

**Changing it.** Edit `.quickstep` in `pe_style_catalog.c`:
`triggers` (drop `PE_QS_TRIGGER_KILL` or `_BREAKPOINT` to test either alone),
`breakpoints` (`{50, 25}` adds a second threshold, up to 4), `refund_percent`,
`burst_frames` (0 turns off the burst and Overextended), `move_percent`,
`exposed_percent` (100 turns off Overextended), `exposed_grace_frames`
(Overextended starts this many burst frames in). Cue colors are the `CUE_*`
constants at the top of `pe_style_runtime.c`.

**Not done yet.** Scripted-HP enemies (the Actress) are not excluded: their
identities still need to be read from the Bestiary. The pip tints the native
target marker rather than drawing a separate pip. Not playtested.

## Revision 5

The design doc's Revision 5 replaces two passive taxes with build-and-release
mechanics. Where this section conflicts with "Implemented first pass" below,
this section wins.

- Quickstep momentum (superseded by Revision 6.4 above).
- Bulwark / Retaliate: `charge_percent` is 100 (native). Guard is unchanged.
  `pe_style_absorb` stores the guarded loss actually taken (capped at Aya's
  current HP). During the next ordinary weapon action, `pe_style_counter` adds
  `min(store, ceil(hit * 50%))` to each ordinary hit on a live candidate in
  `PE_HOOK_ENEMY_DAMAGE`. At action end the rest of the store is dropped if any
  bonus landed; a whiffed weapon action and non-weapon actions keep it. AT
  marker is orange while a counter is loaded and the guard is off.
- Channeler / Spellweave: the 5% max-PE refund is gone. The same completion
  rules (`pending_damage`, cancel/interrupt, final kill) now add one charge, up
  to three. `PE_HOOK_SPELL` (LATE, in battle, Aya acting) spends all charges
  on support powers 0-5, 7-12 and 18: refund 34% / 67% / 100% of the raw cost
  the native code is about to subtract (table cost after `PE_HOOK_POWER_COST`
  and the 2/3 PE armor; Barrier uses max PE / 3). Three charges also add 25%
  of the native Heal 1-3 amount (30/60/280). Energy Shot (6), Liberate (19)
  and attack powers (13-17) keep charges untouched. The refund is applied
  before the native subtraction, as Rebalance's Detox/Scan refund already is.
  Limits: the menu still shows and checks the full cost; casts are unit-tested
  only (no scripted native cast yet). PE label pulses per charge and stays cyan
  at three. Passive recovery is still halved.

Native evidence: `build-styles/live-rev5/functional-results.json` (Quickstep one
2250 refund; Bulwark native 114 charge steps, 9->7 / 4->3 storing 10; vanilla
clean) and `build-styles/live-rev5-channeler/functional-results.json` (club and
gun charges 1,2,3 capped, no refunds, passive halved; vanilla clean). The
scripted fight landed no counter hit and cast no PE power. Playtest package:
`build-styles/costume-playtest-rev5/Play.cmd`, starting in Guard.

## Implemented first pass

Quickstep: direct normal weapon kills refund 25% of a full AT bar once when
the native action queue completes; multi-hit notifications cannot multiply it. Final kills,
PE attack forms, misses and repeated dead-target notifications do not qualify.
No damage or speed penalty. The AT marker briefly turns cyan on a refund.

Bulwark: eligible ordinary combat losses are multiplied by 0.75 while below full
AT and in a controllable idle movement phase. No guard during menus, actions,
reactions, forced control or full AT. Eligible AT increments use 0.90 with a
fractional remainder, leaving native derived stats intact. The AT marker is cyan
only while protected. No persistent guard lockout.

The original game's `hpAlive` member is AT for Aya, but HP for enemies. All
writes in this module are restricted to Aya's AT or local admitted HP loss;
no character/equipment base stats or model data are rewritten.

Striker: a committed native weapon-action queue snapshots each currently live,
native-targetable enemy entity and core separately (up to 64; unrecorded targets
receive no bonus). Ordinary gun hits on qualifying recipients get a single 1.25
multiplier after native resistance/minimum calculations and before HP commit.
Nearest-integer rounding uses ties upward and saturates at INT_MAX. Nonpositive
losses remain nonpositive. Clubs (kind 8), PE, pending native instant-kill status,
non-Aya sources and hits outside a captured gun action are excluded. Moving after
the snapshot cannot change qualification. Action completion and lifecycle resets
clear its recipient mask and identities.

Initial distance convention: horizontal gap between Aya's native 63-unit footprint
and the enemy's scaled native hit cylinder; qualify at gap <= 96 world units.
The cylinder center/radius are read only, with native moveSpeed/4096 scaling.
Missing/zero bounds or scale fail closed. This handles different sizes without
using the worn costume's radius. The cutoff is experimental and needs visual
calibration against more enemies, bosses and linked body parts. Vertical hit
eligibility, target selection and ordinary weapon range remain native rules.

Target preview recomputes that same geometry before each confirmation frame.
Its native pointer/target highlights turn cyan when the selected target qualifies
and is within native gun range. Prior colors are restored before native panel
preparation, and on resets; no new targeting input or meter is introduced.

### Channeler recovery and completion rules

Channeler: completed damaging ordinary gun/club queues restore 5% of max PE,
once for the entire native queue, including multi-hit/target and melee follow-ups.
The adapter marks actual positive enemy HP loss, rather than counting attack
attempts or assuming a positive proposed loss was applied. PE, instant effects,
zero damage, already-dead targets, canceled/interrupted queues and encounters
that have ended do not pay. Native action-mode and queue-cancel events invalidate
pending credit. Mixed command queues and unusual scripted poses still need the
broader acceptance pass; the prototype deliberately pays at full queue completion.

Native PE recovery was verified in Menu_MainUpdate: when native update/pause,
status and below-max conditions admit recovery, its divisor decreases and its
step decays toward a native minimum. The new typed hook sees only that admitted
increment. Channeler scales it by 50%, with hundredths of a raw PE unit carried
between admitted updates. Rates, divisors, costs and ability strength are not
rewritten. No hook is added to field/menu healing or other PE sources.

PE is native 16.16 fixed point. The 5% refund uses floor-with-fractional-carry
across eligible actions, with separate recovery/refund remainders. Excess and
fractions are discarded on hitting the cap; scene/load/battle cleanup clears
transient fractions. There is no saved credit bank. Reaching full PE through a
refund activates the native 240-update full-PE protection timer; already-full
actions do not refresh it. The PE label pulses cyan only when PE actually rises.
This native full-PE interaction and the healing economy require balance testing.

## Prototype limits

- Half-bar action policy is queried through `battle_action_policy` v1. Bulwark
  visibly falls back to Original when Rebalance half-bar/Quick features are active.
  Disable the Rebalance half-bar master before testing Bulwark. It never changes
  those settings itself.
- Native checkpoint capture/restore is refused while prototype abilities are on,
  rather than silently omitting the new transient encounter state. Normal card
  saves remain unchanged. Full checkpoint serialization is later work.
- Native Wardrobe includes the mode switch, style status, and rotating benefit
  and tradeoff text. F1 includes additional guard/policy details. Dedicated
  icons and richer native costume cards are later UI work.
- A real headless encounter confirmed refund timing, guard damage admission and
  charge scaling. Broad action/rig coverage, visual cue review and fun/balance
  playtests remain required. Do not present this as release-ready.
- No installation into the user's active game or PR publication is performed by
  this implementation task.

## Checks

`mod-costume-styles` runs the real native adapter against a simulated game/host,
with the production catalog and engine. It checks attribution, duplicate/final
kills, fractional charge, guard state, mode locking, failed appearances, future
style fallbacks, cleanup and marker restoration. Existing Wardrobe, Collection
and half-bar regression tests must also pass.
Native-menu coverage includes mode changes without wearing another outfit,
held-button suppression and fullscreen preview isolation. Adapter coverage checks
saved mode changes, battle rejection and preview status for unsupported policies.

Local build: `cmake --build build-styles --target parasite-eve-pc test-mod-costume-styles test-mod-wardrobe-menu test-mod-collection test-mod-atb test-mod-rebalance test-mod-skin-bounds`.
Run `ctest --test-dir build-styles -R "^mod-(costume-styles|wardrobe-menu|collection|atb|rebalance-.*|skin-bounds)$" --output-on-failure`.
All nine checks pass in the Debug build, including four Rebalance scenarios and
skin bounds. The game links. F1 preview coverage also checks that browsing a
different style leaves the worn outfit and captured encounter style unchanged.

The two prior linking failures were fixed by separating production boundaries,
not by suppressing missing symbols or adding dummy gameplay implementations:
`enemy_entity_anim.c` owns the unrelated animation/reward phase, leaving the
real damage/facing code directly testable. `pe_skin_rows.h` is the private
row-binding core included by both the loader and its original bounds fixture;
it contains no file I/O, pack providers, VRAM uploads or hooks. All original
bounds assertions remain. Fourteen row-core functions and the moved enemy
animation body were checked against their prior source and are unchanged.

Striker adapter tests cover equal surface gaps on different enemy sizes, the
cutoff edge, per-recipient snapshots, moving targets, repeated hits, missing
recipients, gun/club/PE/instant exclusions, rounding/overflow, cue restoration
and action cleanup. The Rebalance native fixture also checks the new final-loss
hook receives the original native result, commits an admitted replacement once,
and marks PE/instant-kill paths ineligible.

Channeler checks include gun/club credit, repeated hits, payment only on
completion, duplicate completion, zero/PE exclusions, cancellation, interruption,
final kills, caps/full-PE timer, fractional recovery/refunds and PE cue cleanup.
All nine related checks pass. The broader `original-game-actor-lifecycle` fixture
now links (MinGW Clang only) by reusing the game's full production sources plus a
host object whose `main` is renamed; no stubs or ignored undefined symbols. It
passes. Native on/off encounter checks exercise the new actor-mode hook in the
built game.

Local toolchain caveats (unrelated to styles, pre-existing on this clang/lld
setup): 22 other test programs (e.g. `test-original-gte`, `test-mod-loot`) fail
to link for the same unresolved-reference reason; `original-game-actor-animation`
and `-player-input-assets` fail only the movie-abort check because UCRT `abort()`
exits 0xC0000409 rather than 3; `mod-ps5-compatibility-export` fails a source hash
because `core.autocrlf=true` checked headers out with CRLF.

## Native functional evidence (November 11, 2026)

Isolated copied-card amphitheater runs confirmed Quickstep selects T-shirt's
profile and awards exactly one 2250 refund after a non-final weapon kill. Normal
charge steps remained 114. Guard's Bulwark run produced charge steps of 102/103
(90% with fractional carry), with guarded damage examples 9 -> 7 and 21 -> 16.
The same Guard appearance in vanilla mode retained steps of 114 and no style
effects. The action completion event was moved to actual queue completion after
the first real run exposed that the old idle helper was not the correct boundary.

These are bounded scripted functional traces, not completed-battle or balance
claims. Bulwark's trace ended at 29 HP versus 68 for the vanilla control under
the fixed input script; do not conclude either balance or lack of balance from
one non-adaptive run. No mechanics were retuned from these traces.

`tests/run_pe_costume_styles_live.py` reproduces these checks with explicit
`--exe`, `--harness` (the local pedebug project), `--card` (amphitheater Club 1
fixture, slot 1), `--guard` (generated Guard bundle folder), `--disc1`, `--disc2`
and `--output` paths. It uses fresh isolated folders, hashes the original card,
and writes a machine-readable report. The original card and active preferences
are never modified. Local evidence is in `build-styles/live/functional-results.json`.

## Isolated manual playtest build

`tools/New-CostumeStylePlaytest.ps1` accepts explicit `-Executable`, `-Disc1`,
`-Disc2`, `-Card`, `-Guard` bundle folder and an optional new `-OutputDirectory`.
Pass `-Daniel` to include Daniel's assets and `-StartingStyle Striker` to start
with that costume. This does not change the supplied save's equipped weapon.
It copies the executable, Guard assets and one card into a fresh folder and
creates `Play.cmd`. It refuses an existing output directory. Disc locations are
stored as data; no images are copied or downloaded. A SHA256 manifest records
the copied inputs, and preparation verifies the original save hash is unchanged.

The generated launcher sets `PE_CONFIG_HOME` only for its process, restoring the
previous value afterwards. The test profile owns T-shirt and Guard and disables
Rebalance for clean comparisons. Normal purchases/ownership code is unchanged.
Each manual session keeps progress in that folder's own memory card. Default
starting outfit is T-shirt; `-StartingStyle Bulwark` selects Guard instead.

Locally prepared build: `build-styles/costume-playtest-channeler/Play.cmd`. Load Continue,
card 1, save 1; open Wardrobe outside battle to switch outfits or mode. This is
an experimental local playtest build, not a published release.
This package includes T-shirt, Guard, Daniel and Aya's evening dress, starts in
the evening dress (Channeler), and uses the original copied save. Spend PE in
battle if the gauge is full to see Channeler's refunds. Equip a gun through the
normal menu to test Striker; costume switches never change equipment or resources.

Native screen verification used `build-styles/ui-check-18e7ce86`: T-shirt
displayed Quickstep on, holding Select disabled abilities once, a second press
enabled them, Guard preview displayed Bulwark on, and wearing Guard returned
successfully to the field. Screenshots confirm the three info rows fit the
native screen. This verifies browser controls and an initial outfit transition;
combat cue review and the broad rig/action acceptance pass remain outstanding.

After the linking fix and F1 previews, the native runner passed all
three cases again against the rebuilt game. Results are in
`build-styles/live-link-fix/functional-results.json`: one 2250 Quickstep refund,
Bulwark 102/103 charge steps and 9 -> 7 / 21 -> 16 guarded losses, and vanilla
114 charge steps without ability effects. Source save hashes remained unchanged.

## Striker native evidence

`tests/run_pe_striker_live.py` accepts `--exe`, `--harness`, `--card`, `--daniel`,
`--disc1`, `--disc2` and `--output`. It equips an existing gun only in a copied
fixture, fixes that copy's CRC through the harness, and hashes the original save.
It runs the same costume with abilities enabled and disabled in isolated folders.

`build-styles/live-striker-range/functional-results.json` records a successful
native run: a qualifying 27-point shot became 34 (nearest integer to 33.75).
Other committed snapshots were outside point-blank range and received no bonus.
The vanilla control produced no Striker effects. Earlier scripted runs made no
shots within the committed cutoff; the diagnostic confirmed their snapshots
were far, so the input script was adjusted to approach closer. The multiplier
and distance cutoff were not retuned. These traces do not establish balance,
all-weapon compatibility, or visual acceptance of the targeting cue.

## Channeler native evidence

`tests/run_pe_channeler_live.py` uses explicit `--exe`, `--harness`, `--card`,
`--disc1`, `--disc2`, `--output` paths. It runs club, gun and vanilla cases by
default; `--cases` selects a subset. Only fresh test copies have their starting
PE lowered and an existing weapon selected. The source save is hashed before
and after, and each copy's native CRC is regenerated by the harness.

`build-styles/live-channeler-final/functional-results.json` confirms both guns
and clubs earned refunds of 481689/481690 native raw units against max 9633792
(about 7.35 PE from 147 max). The alternating unit reflects fractional carry.
Admitted passive increments of 15720 became 7860; vanilla retained 15720 and
earned no refunds. Traces are bounded functional checks, not balance results.

Visual inspection exposed that the native PE label uses raw texturing and
ignores RGB tint. During Channeler's pulse only, the adapter enables texture
color modulation, then restores the original command byte and color. Unit
coverage checks both restoration paths. The rebuilt game's visible cyan PE cue
was confirmed in `build-styles/live-channeler-cue`; that run also passed its
club refund and half-speed passive recovery checks again.
