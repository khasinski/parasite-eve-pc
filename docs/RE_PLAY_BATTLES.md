# RE play battles: fighting across a room's cameras

Status: design, chosen 2026-11-24 (option 3 of three discussed). Not built yet.

## The goal

A battle in an imported RE room plays like Parasite Eve (Aya's turns, the time
stop while she aims or uses her menu, the Escape command) but looks and moves
like Resident Evil:

- The battle starts with the enemies in the camera's picture when one reaches
  Aya (built: `pe_trip_select_approach`, up to 6).
- Aya can run anywhere the room's floor allows; the camera follows her from
  angle to angle as it does in the field.
- An enemy that comes into the picture during the battle joins it, up to the
  game's limit (6 enemies; the engine has 7 places, one is Aya).
- Until the battle is resolved there is no door, examine, pick-up, typewriter,
  status screen or pause menu.
- The battle is resolved when every enemy in it is dead, or Aya escapes with
  PE's Escape command. Leaving the room is not a way out.
- After an escape the enemies stay where they are, and the next encounter waits
  a few seconds (a grace period). Enemies that never joined keep wandering.

## How battles work today

1. An enemy reaches Aya in the RE room (`pe_check_approach`).
2. The enemy-import mod picks the battle's enemies, arms their packs and pauses
   RE play (`pause_for_battle`); RE play's room stays drawn.
3. The mod runs PE's battle entry in place (`pe_enemy_battle_start_here`: the
   steps of opcode 85, then `Battle_StartEncounter(2)`). No PE room is visited.
4. RE play's backdrop draws the room's picture behind the battle with the RE
   camera, anchored at Aya's battle position (`backdrop_on`, `backdrop_camera`).
   Aya's steps are mapped into the room and slide along its colliders
   (`battle_walk`); the hidden PE room's floor checks are off for her.
5. The imported enemies are the mod's own actors (`pe_spawn`), started where
   they stood (`pe_enemy_battle_set_start`), driven by `pe_foe_think`, frozen
   while battle time stops.
6. The engine ends the battle (`Battle_StepPostBattle`, `Battle_StepEscapeOrDeath`,
   `Battle_StepEntityDeath`); the trip restores the movement lock, resumes RE
   play and reports the dead to the room (`pe_trip_report`).

The room's mapping is one formula both ways, PE field units against RE room
units, with k = RE play's scale (`units_per_pe_unit`, about 5.7):

    pe = anchor + (re - room_at_start) / k
    re = room_at_start + (pe - anchor) * k

## What has to change

### 1. Keep the battle in the RE room (blocker, found 2026-11-24)

Aya's native position moves during a battle. Running far enough walked it into
an exit of the hidden PE room (Central Park map 66): the room's script started
its own transfer (`Script_EncodeBarcode`, map 67) in the middle of the battle,
the imported enemies were freed with the room, and the battle went on with none.
The same happens with the PE floor checks on, so it is the room's exits, not the
new walls.

Every PE room is driven this way: map 66's exits are script polygons
(`Script_PointInPoly` on Aya's position, then `Script_SetSceneFlag2000`,
`Entity_SetGlobalFlag1` and `Script_EncodeBarcode`); its script also holds
encounter rolls (32 `Script_Random`), battle entries (opcode 85) and battle ends
(`Menu_SetModeState8`). Refusing only the transfer would leave the lock and the
scene flag set; the room's other zones would still fire.

Fix: fight off the hidden room's map. When an in-place battle starts, the trip
moves Aya's native position to a battle anchor far from every zone the host
room has (map 66's zones lie within about 6,000 units of its centre; an anchor
near 20,000 leaves the whole RE room clear), and puts her back afterwards
(`pe_place`, as now). Nothing in the room's scripts can see her there. The
floor checks are already off for her and for the imported enemies (flag 0x80),
the backdrop's camera is relative to the anchor, and targets are by distance
only, so nothing else depends on where the anchor is.

Limits: PE positions are 16-bit (`posX.parts.integer`, +-32767). The RE room
mapped into PE units must fit beside the anchor: about +-5,750 at Aya's size
100 (k = 5.7), +-11,500 at size 200. The anchor is chosen per battle from the
room's collision extent and k, outside the host room's zones (or its collision
extent, read from `g_CollisionDb`). This also makes the host room choice matter
only for Aya's model bank.

### 2. The camera follows Aya (RE play)

- Each battle frame, `battle_walk` already knows her room position. Ask the
  room's camera zones for the camera there (`pe_re_camera_at`).
- On a change, swap the backdrop's camera and picture: decode that camera's
  picture (`pe_imp_catalog_imageset`, about a millisecond since the catalog
  cache), upload it over the picture area (the saved PE pages stay as they are,
  for `backdrop_off`), rebuild the foreground masks, and set `s_camera` to that
  view. The anchor and the mapping above do not change: same room, same units.
- Room effects (fires) and placement meshes follow the camera as in the field.

### 3. Enemies join when they come into view (both mods)

- RE play keeps the room's other enemies in its placements during the battle.
  Each battle frame (cheap: a handful of projections) the enemy-import mod asks
  which of them are in the current picture (`in_view`), on Aya's floor, alive,
  not already in the battle.
- For each new one, under the cap: make sure its pack is loaded (same kind:
  already in VRAM; a new kind: `pe_place`, which may refuse when VRAM is full),
  add it to the roster and spawn it at its mapped position (the path the
  G-embryo summon already uses: `pe_enemy_battle_arm_summon`, `pe_spawn`), and
  add its slot to the trip's result so its death is recorded.
- Optional, later: let the off-screen enemies keep closing in while battle time
  runs (RE play's chase, `advance_enemies`, toward Aya's room position, only
  while `g_BattleModeState` is 0), so they come into view by themselves.

### 4. No interaction until it is resolved

Mostly true already: RE play is paused for the battle (no zones, doors,
examine, Start or Select), and PE's battle owns Cross. Check that its pause
also covers the Square pocket and the Start/Select handlers (`s_battle_paused`
gates them in `on_frame`), and that an RE door zone Aya stands in after the
battle needs a fresh Cross (it does: `s_cross_held`).

### 5. After the battle

- Escape: the trip already sets a 180-frame retry for automatic fights
  (`s_approach.retry`). Make it a grace period that also waits until Aya has
  moved away (or 5 seconds), so the same enemy doesn't pull her straight back.
- Survivors after an escape: move their RE placements to where they were in
  the battle (map back with the formula) so they don't jump to where the
  battle began.

## Findings across RE2 (survey, 2026-11-24)

A scratch survey entered every room of both discs from the route-test store,
let its scripts place enemies, and counted (people, kinds 0x40 and up, left out):

- 123 of 252 room states have enemies. 100 have one kind, 23 have two, none
  three: two enemy packs loaded at once covers every room.
- Five rooms place more than 6: 1-1D (7 zombies), 2-05 (10 of kind 0x2D),
  4-0B (16 of kind 0x29, a swarm), 6-0E (9, two kinds), 6-13 (7, two kinds).
  The cap stays at 6 (the engine has 7 places; one is Aya). Joining is rolling:
  when an enemy in the battle dies, the next one in view takes its place, so a
  swarm is fought in waves rather than all at once.
- Six rooms have enemies on two floors (1-03, 4-05, 4-0A, 4-10, 6-0E, 7-02). A
  battle stays on Aya's floor: only enemies on it join, and her battle walk uses
  that floor's colliders, so stairs and ramps act as walls until it is over.
- Enemy rooms have 2 to 16 cameras, most 6 or more: a battle crosses cameras in
  almost every room, so the camera must follow.

Engine facts the design leans on:

- Targets: `Battle_BuildTargetList` lists every live battle enemy with no range
  or visibility check, sorted by distance. Enemies that joined stay targetable
  when Aya runs into another camera; the nearest is the default.
- Joining mid-battle works: the G-embryo summon spawns into a running battle
  through the same `pe_spawn` path join-on-sight would use (`pe_summon`).
- The engine ends the battle when the last live enemy dies; an enemy about to
  join can't keep it open. Enemies in view after a win start the next battle as
  any encounter does.
- RE play is paused for the battle: the room's scripts, its other enemies and
  its zones don't run (RE time is stopped). The backdrop draws only the room's
  picture in battle; its foreground masks and effects (fires) are not drawn
  yet, so in battle Aya is never hidden behind a foreground object.

## Decisions to make

- Off-screen enemies: stay frozen until seen (simplest), or keep closing in on
  Aya's room position while battle time runs (RE play's chase,
  `advance_enemies`, only when `g_BattleModeState` is 0).
- Foreground masks and effects in battle: draw them (RE-like: Aya passes behind
  pillars and cars) or not (clearer for aiming).
- Bosses and story fights (`pe_trip_select_boss`: G, the Alligator, the Tyrant,
  canister and launcher finishes): same camera following and off-map anchor,
  but no joining.
- Targeting off-screen joined enemies: allowed (PE's rule) or limited to the
  ones in view.

## Order of work

1. Off-map battle anchor (section 1). Small, and every later step needs it.
2. Camera follows (section 2). RE play only.
3. Join on sight (section 3). Enemy-import mod plus RE play's placements.
4. Grace period and survivor positions (section 5).
5. Optional: off-screen enemies keep coming.

Each step is testable in the harness from PE's title into Leon A's street,
which has six zombies across two cameras: walk Aya across the camera change
during a battle, check the picture changes, the zombies of the second camera
join, and the battle ends only when they are all dead or she escapes.

## Limits and risks

- 6 imported enemies per battle (engine: 7 places). Several kinds at once may
  run out of VRAM for their packs; a refused one stays in the field.
- Camera swaps cost a picture decode and an upload; fine at a camera change,
  not every frame.
- The hidden PE room (map 66) is chosen only for Aya's battle-ready model
  (bank 12); with the off-map anchor its zones never matter in battle.
- Battles started by RE scripts (bosses) keep their own rules
  (`pe_trip_select_boss`); joining on sight is for ordinary encounters.
