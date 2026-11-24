# RE play battles: plan and decisions

Parasite Eve battles inside imported Resident Evil rooms, for RE1, RE2 and RE3.
This file is the plan we build from and the record of what was decided and why.
Add a decision to the log before building something that changes the rules, and
mark steps done as they land (with the commit).

Status: designed 2026-11-24. Phase 0 (battles start in place) is built for RE2;
phases 1-6 are not.

## The battle we want

A battle plays like Parasite Eve (Aya's turns, time stopping while she aims or
uses a menu, the Escape command) and looks and moves like Resident Evil:

- It starts when an enemy reaches Aya, with every enemy in the camera's picture.
- Aya can go anywhere the room's floor allows on her floor; the camera follows
  her from angle to angle, with the room's foreground and effects drawn.
- Enemies off screen keep closing in while battle time runs. One that comes
  into the picture joins, up to the cap; when one in the battle dies, the next
  in view takes its place.
- Until the battle is resolved there is no door, examine, pick-up, typewriter,
  status screen or pause menu.
- It is resolved when every enemy in it is dead, or Aya escapes with PE's
  Escape command. Leaving the room is not a way out.
- After an escape the enemies stay where the battle left them, and a grace
  period keeps the same enemy from pulling her straight back in.

## Decision log

Newest last. Each entry: what, why, and where it lives.

| # | Date | Decision | Why |
|---|------|----------|-----|
| D1 | 2026-11-24 | Battles start in place: the mod runs PE's battle entry (opcode 85's steps, `Battle_StartEncounter(2)`) without a PE room battle; RE2 encounters no longer travel to the Day 1 sewer. | The trip showed the sewer, its flash and its rats. Opcode 85 is room-independent; the imported enemy is the mod's own actor. |
| D2 | 2026-11-24 | The RE room stays drawn while a battle sets up and the battle plays in front of it (the backdrop). No black cover. | The user wants a seamless transition. |
| D3 | 2026-11-24 | The hidden PE room under an imported game started from the title is a Central Park path (map 66, story 0xE4). | Its Aya (model bank 12) has battle moves; Carnegie Hall's (bank 11) folded up in battle, the sewer (bank 10) drew her wrong, map 59 had a flashing police car. |
| D4 | 2026-11-24 | Enemies start a battle where they stood beside Aya in the room, and freeze whenever PE's own enemies would (mode not 0, flags 4 or 0x100). | They teleported away and kept moving through her menu. |
| D5 | 2026-11-24 | Aya's battle steps follow the RE room's colliders, not the hidden PE room's floor. | Battle borders were tighter than the field's. |
| D6 | 2026-11-24 | Option 3: fight across the room's cameras; enemies coming into view join. | The ideal way to play, chosen over a one-camera arena and a whole-room battle. |
| D7 | 2026-11-24 | Off-screen enemies keep closing in on Aya while battle time runs. | RE's enemies pursue; they come into view by themselves. |
| D8 | 2026-11-24 | The room's foreground masks and effects are drawn in battle. | Not drawing them was unfinished work, not a choice. |
| D9 | 2026-11-24 | Boss and story fights follow the same rules, and enemies the original game brings into that fight (G's embryos, for one) join it. | As in RE. |
| D10 | 2026-11-24 | Proposed, to confirm: any enemy in the battle can be targeted, on screen or not (all are in Aya's room; one in another room never joins). PE's range still applies: a target past the weapon's `range` is marked out of range (`Battle_CycleTarget`) and gets PE's out-of-range treatment. Targets on screen come first in the default and the cycling order, nearest first, then off-screen ones by distance; an off-screen target gets a marker at the screen's edge. | RE lets you shoot what you can't see; PE makes distance cost. Together: no blind spot when the camera changes, but closing in still pays, which suits enemies that keep coming (D7). |
| D11 | 2026-11-24 | At most 6 enemies in a battle (the engine has 7 places; one is Aya); joining is rolling. | 5 RE2 rooms hold more than 6 (up to 16). |
| D12 | 2026-11-24 | A battle stays on Aya's floor: only enemies on it join, and stairs and ramps act as walls until it is over. | 6 RE2 rooms have enemies on two floors. |
| D13 | 2026-11-24 | No interaction until the battle is resolved: all enemies dead, or Escape. After an escape, Aya stays where she is with a grace period. | User's rule; RE play is already paused for the battle. |
| D14 | 2026-11-24 | Battles are fought off the hidden PE room's map: Aya's native position moves to a battle anchor far from every zone the room has, and back afterwards. | The room's scripts are zones on her position: running in battle walked her into map 66's exit and changed rooms mid-battle. Refusing only the transfer leaves its locks set. |
| D15 | 2026-11-24 | One battle system for RE1, RE2 and RE3: game rules are data and small callbacks per game; the battle logic is shared; the PE side is one adapter. | The user wants the same system across the three games, each adjusted where it differs. |

## Architecture

Three layers, so a game or a host can change without touching the others.

### Layer 1: game rules (per game, `mods/pe_re_play/`)

What a game says about its own enemies and rooms. Data first, small callbacks
where data isn't enough. One table per game, `PeReBattleRules`:

- `game`: "re1", "re2", "re3".
- `combatant(placement)`: an enemy that fights (not a person, not a
  scene-only actor). RE2 today: `pe_re2_proximity_enemy` and
  `pe_re2_scene_only_enemy` (`mods/host/re_encounter_rooms.h`).
- `pursues(placement)`: closes in on the player during battle time (D7). RE2
  today: `pe_re2_ground_pursuit_kind`.
- `encounter_radius(room)`, `group_radius(room)`: when an enemy reaching the
  player starts a fight (RE2's street is wider).
- `boss(room, placement)`: a story fight and what joins it (D9). RE2 today:
  `pe_trip_select_boss`, `prepare_boss`, G's embryos (`pe_re2_g_attached_embryo`),
  Mr. X (`pe_re2_mr_x_fightable`), canister and launcher finishes.
- `floor_of(y)`, floor height (RE2: 1800 per floor, `PE_RE2_FLOOR_HEIGHT`).
- Room access the shared layer already has per game: camera zones
  (`pe_re_camera_at`; RE1's zones carry an own-area flag), collision
  (`PeRe2RoomCollision`, `pe_imp_re2_slide_on`; RE1 and RE3 need their readers'
  equivalents), enemy placements (`pe_re_session_enemies`).

RE2's helpers move from `re_encounter_rooms.h` into its table unchanged; RE1 and
RE3 start with tables that say "no battles" until their rooms are playable.

### Layer 2: battle director (shared, `mods/pe_re_play/`, headless-testable)

Game-neutral and engine-neutral: no PE1 symbols (the portability check holds).
It owns one battle's state in RE room terms:

- the roster: room slots in the battle, their order, the cap and the rolling
  queue (D11);
- each frame of battle time: the player's room position (from the adapter),
  which camera that is in (`pe_re_camera_at`), which room enemies are on her
  floor, alive, combatants and in the picture (`in_view`), and which of those
  join (D6, D12);
- off-screen pursuit (D7): RE play's chase (`advance_enemies`,
  `pe_re_session_chase`) toward her room position, only while battle time runs;
- the result: who died (slots), where survivors stand (written back to the
  room's placements after an escape), the grace period (D13).

It talks to the host through `ReHost` (camera, picture, in_view) and to the
battle adapter through `PeRePlayApi` (join, positions, result).

### Layer 3: PE battle adapter (`mods/pe_enemy_import/hosts/pe1/`, `mods/pe_re_play/hosts/pe1/`)

Everything that touches Parasite Eve's engine:

- start in place (D1, built: `pe_enemy_battle_start_here`), the off-map anchor
  (D14), the movement lock and native position restored afterwards (built);
- the backdrop (D2, built): the room's picture behind the battle; to add, the
  camera swap (picture, masks, effects, view) when the director says so, and
  masks and effects in battle (D8);
- Aya's battle walk on the room's colliders (D5, built);
- enemies: spawn at a room position mapped into the battle (D4, built for the
  start; `pe_summon` already spawns mid-battle), freeze on time stop (built),
  despawn and report on death;
- targeting (D10): PE's `Battle_BuildTargetList` lists every live battle enemy
  by distance; reorder it on screen first (the director's `in_view`), and draw
  an edge marker for an off-screen target. Range and its out-of-range marking
  are PE's own (`action->range` in `Battle_CycleTarget`).

The mapping between the two spaces is one formula, k = RE play's scale
(`units_per_pe_unit`, about 5.7 at Aya's size 100):

    pe = anchor + (re - room_at_start) / k
    re = room_at_start + (pe - anchor) * k

PE positions are 16-bit (`posX.parts.integer`, +-32767): the anchor is chosen per
battle so the whole room fits (about +-5,750 PE units at size 100, +-11,500 at 200)
outside the host room's zones (map 66's are within about 6,000 of its centre).

## Per-game adjustments

| | RE2 | RE1 | RE3 |
|---|---|---|---|
| Rooms playable in RE play | yes | not yet | not yet |
| Enemy and person kinds | known (`re_encounter_rooms.h`) | to map (`pe_src_re1_emd.c`) | to map (`pe_src_re3_emd.c`) |
| Bosses and story fights | partly (G, Alligator, Tyrant, Mr. X) | to list | to list (Nemesis pursuit is its own rule) |
| Camera zones | `pe_re_camera_at` | own-area flag (`flag` 1) | to check |
| Collision and floors | `PeRe2RoomCollision`, 1800 per floor | to read | to read |
| Pursuit during battle | chase kinds known | to define | Nemesis follows between rooms: D10 keeps him out until he enters Aya's room |

## Phases

Each phase ends with its harness check and the tests below passing.

0. **Battles in place (RE2).** Built: D1-D5 (commits c383bf512, a7e42b88f,
   1f1b56ae6, 9d0db2f64, bdb868a05).
1. **Off-map anchor.** D14. The trip moves Aya's native position to the anchor
   at battle start and back at the end. Check: run the length of the street in
   a battle; the hidden room never changes and the battle carries on.
2. **Rules table and director skeleton.** D15. Move RE2's encounter helpers into
   its `PeReBattleRules`; the director owns the roster and the in-view check
   that today lives in `pe_trip_select_approach`. Behaviour unchanged. Check:
   headless director tests with synthetic rooms; the route tests still finish.
3. **Camera follows, masks and effects.** D6, D8. The director picks the camera
   from Aya's room position; the stage swaps picture, masks, effects and view in
   battle. Check: a street battle crossing from camera 0 to 1; the picture,
   foreground and fires change with her.
4. **Join on sight, rolling cap.** D6, D11, D12. Check: the street's six
   zombies across two cameras all end up in the battle; 2-05's ten crows come in
   waves of 6; 1-03 only takes enemies on Aya's floor.
5. **Pursuit, escape and write-back.** D7, D13. Check: a zombie off screen
   walks into view and joins; after an Escape the survivors stand where the
   battle left them and no battle starts for the grace period.
6. **Bosses and story fights.** D9. Check: G's embryos join the G fight; the
   canister and launcher finishes still work; the five story routes finish.

RE1 and RE3 follow once their rooms are playable: fill in their rules tables
and run the same checks in one room of each.

## Tests

- Headless (`tests/`): the director with synthetic rooms per game: who starts a
  fight, who joins and when, the cap's queue, floors, the grace period.
- Session (`mod-re-session`) and the five story routes (`mod-re-route`): the
  game still finishes, with battles reported the same way.
- Harness scenes (from PE's title, the user's store): 1-00 street (two
  cameras, six zombies), 2-05 (ten), 4-0B (sixteen, a swarm), 1-03 (two
  floors), a boss room.

## RE2 survey (2026-11-24)

Every room state on both discs, entered with the route-test store, scripts
run, people (kinds 0x40 and up) left out:

- 123 of 252 room states have enemies; 100 one kind, 23 two, none three: two
  enemy packs in VRAM at once covers every room.
- More than 6: 1-1D (7 zombies), 2-05 (10 of kind 0x2D), 4-0B (16 of kind 0x29),
  6-0E (9, two kinds), 6-13 (7, two kinds).
- Enemies on two floors: 1-03, 4-05, 4-0A, 4-10, 6-0E, 7-02.
- Enemy rooms have 2 to 16 cameras, most 6 or more.

## Engine facts

- Targets: `Battle_BuildTargetList` takes every live battle enemy, no range or
  visibility check, sorted by distance.
- Mid-battle spawns work (`pe_summon` for G's embryos, through `pe_spawn`).
- The engine ends a battle when the last live enemy dies
  (`Battle_StepEntityDeath`); Escape is `Battle_StepEscapeOrDeath`.
- PE room scripts test zones on Aya's position (`Script_PointInPoly`), then
  lock movement and change rooms (`Script_SetSceneFlag2000`,
  `Entity_SetGlobalFlag1`, `Script_EncodeBarcode`); battle ends from scripts
  are mode 8 (`Menu_SetModeState8`).
- A room battle's script locks field movement (`g_FieldMoveLock` bit 1) and
  lifts it after; in-place battles restore it themselves.
- RE play is paused for the battle: the room's scripts, zones and other enemies
  don't run until it resumes (D7 changes that for pursuit only).

## Open questions

- D10: confirm the proposed targeting rule (off-screen targets allowed, range applies, on-screen first, edge marker).
- How long the grace period is, and whether it ends early once Aya has walked a
  set distance from the survivors.
- Whether a joining enemy gets an entrance (RE's own walk-in) or simply
  appears where it stands.
