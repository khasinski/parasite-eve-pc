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

Fix: a new host hook, `PE_HOOK_ROOM_TRANSFER` (the target token; nonzero
refuses), at `Script_EncodeBarcode`. The enemy-import mod refuses transfers
while a battle started in place is on. Field exits of the hidden room never
matter outside battles, because RE play holds Aya's native position still
there. Also consider refusing the hidden room's other script events (pick-ups,
messages) the same way if any turn up.

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

## Order of work

1. Transfer veto (section 1). Small, and every later step needs it.
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
- The hidden PE room (map 66, chosen for Aya's battle-ready model bank 12) is
  only safe while its exits are refused; another host room would need the same.
- Battles started by RE scripts (bosses) keep their own rules
  (`pe_trip_select_boss`); joining on sight is for ordinary encounters.
