# RE play battles: plan and decisions

Parasite Eve battles inside imported Resident Evil rooms, for RE1, RE2 and RE3.
This is the plan we build from and the record of what was decided and why.

How to use it:

- Add a decision to the log before building something that changes the rules.
- Each phase lists its entry checks, its exit checks and its tests. A phase is
  done when its exit checks pass; mark it with the commit.
- Spikes (S#) are short experiments that settle a risk before a phase depends
  on it. Record the result here, even when it's "fine".
- Keep the module boundaries in "Architecture": a game or a host must be
  addable without editing the shared battle code.

Status (2026-11-24): phases 0-5 built for RE2 (phase 2 and the director side of
4 and 5 by Codex; the PE1 adapter hookup 953def9c6). The optional joining
sound is built, and native Escape was checked in 1-00. The wave and floor
exit checks and the rest of phase 6 are open.
Spikes done; S5 deferred (D16).

## The battle we want

A battle plays like Parasite Eve (Aya's turns, time stopping while she aims or
uses a menu, the Escape command) and looks and moves like Resident Evil:

- It starts when an enemy reaches Aya, with every enemy in the camera's picture.
- Aya can go anywhere the room's floor allows on her floor; the camera follows
  her from angle to angle, with the room's foreground and effects drawn.
- Enemies off screen keep closing in while battle time runs. One that comes
  into the picture joins, up to the cap; when one in the battle dies, the next
  in view takes its place.
- Any enemy in the battle can be targeted, on screen or not; PE's weapon range
  decides whether the shot is worth it.
- Until the battle is resolved there is no door, examine, pick-up, typewriter,
  status screen or pause menu.
- It is resolved when every enemy in it is dead, or Aya escapes with PE's
  Escape command. After an escape the enemies stay where the battle left them,
  and a grace period keeps the same enemy from pulling her straight back in.

## Decision log

Newest last. A decision changes only by adding a new entry that replaces it.

| # | Date | Decision | Why |
|---|------|----------|-----|
| D1 | 2026-11-24 | Battles start in place: the mod runs PE's battle entry (opcode 85's steps, `Battle_StartEncounter(2)`) without a PE room battle; RE2 encounters no longer travel to the Day 1 sewer. | The trip showed the sewer, its flash and its rats. Opcode 85 is room-independent; the imported enemy is the mod's own actor. |
| D2 | 2026-11-24 | The RE room stays drawn while a battle sets up and the battle plays in front of it (the backdrop). No black cover. | A seamless transition. |
| D3 | 2026-11-24 | The hidden PE room under an imported game started from the title is a Central Park path (map 66, story 0xE4). | Its Aya (model bank 12) has battle moves; Carnegie Hall's (bank 11) folded up in battle, the sewer (bank 10) drew her wrong, map 59 had a flashing police car. |
| D4 | 2026-11-24 | Enemies start a battle where they stood beside Aya in the room, and freeze whenever PE's own enemies would (mode not 0, flags 4 or 0x100). | They teleported away and kept moving through her menu. |
| D5 | 2026-11-24 | Aya's battle steps follow the RE room's colliders, not the hidden PE room's floor. | Battle borders were tighter than the field's. |
| D6 | 2026-11-24 | Fight across the room's cameras; enemies coming into view join (option 3). | Chosen over a one-camera arena and a whole-room battle. |
| D7 | 2026-11-24 | Off-screen enemies keep closing in on Aya while battle time runs. | RE's enemies pursue; they come into view by themselves. |
| D8 | 2026-11-24 | The room's foreground masks and effects are drawn in battle. | Not drawing them was unfinished work, not a choice. |
| D9 | 2026-11-24 | Boss and story fights follow the same rules, and enemies the original game brings into that fight (G's embryos) join it. | As in RE. |
| D10 | 2026-11-24 | Any enemy in the battle can be targeted, on screen or not; only enemies in Aya's room are ever in it. PE's range applies (`Battle_CheckEvasion`: distance scaled to the weapon's range, hit odds fall past 500 and 800, some actions can't hit past 1000). On-screen targets come first in the default and the cycling order, nearest first, then off-screen ones; an off-screen target gets a marker at the screen's edge. | RE lets you shoot what you can't see; PE makes distance cost. No blind spot at a camera change, and closing in still pays. Accepted by the user. |
| D11 | 2026-11-24 | At most 6 enemies in a battle (the engine has 7 places; one is Aya); joining is rolling. | 5 RE2 rooms hold more than 6 (up to 16). |
| D12 | 2026-11-24 | A battle stays on Aya's floor: only enemies on it join, and stairs and ramps act as walls until it is over. | 6 RE2 rooms have enemies on two floors. |
| D13 | 2026-11-24 | No interaction until the battle is resolved: all enemies dead, or Escape. After an escape, Aya stays where she is with a grace period. | User's rule; RE play is already paused for the battle. |
| D14 | 2026-11-24 | Battles are fought off the hidden PE room's map: Aya's native position moves to a battle anchor far from every zone the room has, and back afterwards. | The room's scripts are zones on her position: running in battle walked her into map 66's exit mid-battle. Refusing only the transfer leaves its locks set. |
| D15 | 2026-11-24 | One battle system for RE1, RE2 and RE3: per-game rules, a shared director, one adapter per host engine. | The same system across the three games, each adjusted where it differs. |
| D16 | 2026-11-24 | S5 deferred: battle distances keep following RE play's scale (Aya's size setting) for now. The mapping takes k from one place so it can become a fixed battle scale or scaled weapon ranges once play-testing shows which feels right. | The user wants to tune it from harness and real play. |
| D17 | 2026-11-24 | In a battle started in place, the imported enemies join on its first frame (no opening wait). | They vanished for about 11 frames as a battle began (the wait meant for room battles' own enemies), then came back: a visible blink. |
| D18 | 2026-11-24 | The grace period after an escape covers only that battle's survivors (other enemies can still start a fight); it ends after a time or once Aya is a distance from them, whichever is first; the survivors keep pursuing (D7) but can't start a battle meanwhile; each further escape from the same enemies halves their grace. Seconds, distance and the halving are F1 settings (Fights) to tune in play, starting at 3 seconds, 4,000 room units, halving on. Escape stays PE's own roll. Accepted by the user. | A grace is needed so the same fight doesn't re-trigger by accident; scoping it to the survivors and shrinking it on repeats keeps escaping from becoming a way to walk through a room. |
| D19 | 2026-11-24 | A joining enemy continues from where it is, in its own walk toward Aya, facing her: no pop-in or entrance anim. It joins with an empty attack timer (it can't hit on arrival), enters the target order as usual (D10), and a short cue marks it (a sound and a brief marker over it). Enemies the original game brings in with their own entrance (a Licker from the ceiling, crows) use it when the room's script has one, a per-game rule. Accepted by the user. | RE: zombies shamble into view, already pursuing (D7). PE: reinforcements are visible and fair. |
| D20 | 2026-11-24 | The detached phase 2 director keeps the lead first and ranks the other visible foes by distance from the lead, with room placement order breaking ties. Its join order is explicit and phases 3-6 retain their runtime integration work. The T1-T2 anchor geometry uses host-supplied bounds and clearance. | Preserves the current visible roster and makes it possible to test the director while the phase 1 adapter is developed independently. |
| D21 | 2026-11-24 | The director queues newly visible foes nearest Aya first, breaking ties by slot. Queued foes keep their place but can join only while visible on the battle floor. The adapter supplies visibility for the current camera and steps grace at 60 updates per second, including outside combat. Escape history is tracked per room slot until that foe dies or Aya leaves the room. Each repeat halves both grace limits when enabled. | Makes join and target order deterministic and keeps survivor grace independent of other enemies. |
| D22 | 2026-11-24 | After a battle Aya stays where it left her in the room (and in its last camera), instead of going back to where it began. | With the camera following her (D6), going back would be a jump across the room. |
| D23 | 2026-11-24 | The reinforcement sound is an opt-in Fights setting. It plays once per frame that successfully spawns a reinforcement. The yellow marker is independent of sound. | Players can keep the visual cue without adding repeated audio to busy fights. |
| D24 | 2026-11-24 | An escape records only deaths confirmed before native actor cleanup. Removed actors are not evidence of death. | The native escape path releases surviving actors before the room is restored. |

## Architecture

Three layers with one-way dependencies: adapter -> director -> rules. The
director has no PE1 symbols (the portability check enforces it), so it can be
tested headless and reused by another host (PE2).

```
 pe_enemy_import (PE1 battle adapter)      pe_re_stage (PE1 room drawing)
        |  PeRePlayApi (battle calls)              |  ReHost (camera, picture, view)
        v                                          v
             pe_re_battle  (director, shared, headless)
                       |  PeReBattleRules (per game)
                       v
        pe_re_battle_re2.c / _re1.c / _re3.c   +  pe_re_session (rooms, scripts, chase)
```

### Layer 1: game rules (`mods/pe_re_play/pe_re_battle_rules.h`, one file per game)

Data first; a callback only where data isn't enough. All take the session so
they can read room state; none touch the host.

```c
typedef struct PeReBattleRules {
    const char *game;                       /* "re1", "re2", "re3" */
    int max_foes;                           /* 6 (D11); a game may lower it */
    double floor_height;                    /* room units per floor (RE2: 1800) */
    int (*combatant)(const PeReSession *, const PeReEnemyPlacement *);   /* fights, not a person or scene actor */
    int (*pursues)(const PeReSession *, const PeReEnemyPlacement *);     /* closes in during battle time (D7) */
    double (*encounter_radius)(const PeReSession *);                     /* reaching the player starts a fight */
    int (*boss)(const PeReSession *, PeReBossFight *out);                /* a story fight ready now, and its rules (D9) */
    int (*joins_boss)(const PeReSession *, const PeReBossFight *, const PeReEnemyPlacement *);
    void (*chase_step)(PeReSession *, PeReEnemyPlacement *, int, double x, double z, int floor);  /* one battle-time step */
} PeReBattleRules;

const PeReBattleRules *pe_re_battle_rules(const char *game);   /* NULL: no battles in this game yet */
```

RE2's table wraps what exists: `pe_re2_proximity_enemy`, `pe_re2_scene_only_enemy`,
`pe_re2_ground_pursuit_kind` (`mods/host/re_encounter_rooms.h`),
`pe_trip_select_boss`, `prepare_boss`, `pe_re2_g_attached_embryo`,
`pe_re2_mr_x_fightable`, `pe_re_session_chase`. RE1 and RE3 return NULL until
their rooms are playable (see "Per-game adjustments").

### Layer 2: battle director (`mods/pe_re_play/pe_re_battle.{h,c}`)

Owns one battle in room terms. No drawing, no PE calls. Driven once per frame
by the adapter; tells the adapter what to do through returned events.

```c
typedef enum { PE_RE_BATTLE_IDLE, PE_RE_BATTLE_STARTING, PE_RE_BATTLE_RUNNING,
               PE_RE_BATTLE_ENDING, PE_RE_BATTLE_GRACE } PeReBattlePhase;

typedef struct PeReBattleFoe {
    uint8_t slot;          /* the room's enemy slot */
    uint8_t state;         /* queued (seen, over the cap), joined, dead, left (escape) */
    double x, z;           /* room position: from the room before joining, from the adapter after */
} PeReBattleFoe;

typedef struct PeReBattle {
    PeReBattlePhase phase;
    const PeReBattleRules *rules;
    uint32_t stage, room; int floor;       /* the battle's room and floor (D12) */
    double aya_x, aya_z;                   /* her room position, from the adapter each frame */
    int camera;                            /* the room camera she's in */
    PeReBattleFoe foe[PE_RE_ENEMIES_MAX];  /* every enemy the battle knows of, by slot */
    uint8_t initial_slots[PE_RE_ENEMIES_MAX];  /* first roster, in join order */
    int initial_count;
    int joined;                            /* live joined count (<= rules->max_foes) */
    int grace_frames;                      /* D13 */
} PeReBattle;

typedef enum { PE_RE_BATTLE_EV_NONE, PE_RE_BATTLE_EV_JOIN, PE_RE_BATTLE_EV_CAMERA,
               PE_RE_BATTLE_EV_END } PeReBattleEventKind;
typedef struct { PeReBattleEventKind kind; uint8_t slot; int camera; } PeReBattleEvent;

/* Start from an encounter: the lead and whoever is in view (replaces the selection in
 * pe_trip_select_approach). Fills the first joiners. 0, or -1 (nothing to fight). */
int pe_re_battle_begin(PeReBattle *, PeReSession *, const PeReBattleRules *, const PeReEnemyPlacement *lead,
                       int (*in_view)(const PeReEnemyPlacement *));
/* Once per battle frame. time_runs: PE's battle clock runs (mode 0, no stop flags).
 * Writes at most `max` events (join, camera change); returns how many. */
int pe_re_battle_step(PeReBattle *, PeReSession *, double aya_x, double aya_z, int time_runs,
                      int (*in_view)(const PeReEnemyPlacement *), PeReBattleEvent *ev, int max);
void pe_re_battle_foe_moved(PeReBattle *, uint8_t slot, double x, double z);   /* adapter: a joined foe's room position */
void pe_re_battle_foe_died(PeReBattle *, uint8_t slot);
/* PE ended the battle: won (all dead) or escaped. Writes survivors' positions back to the
 * room's placements and starts the grace period. */
void pe_re_battle_end(PeReBattle *, PeReSession *, int escaped);
int pe_re_battle_encounter_allowed(const PeReBattle *);   /* not in a battle or its grace period */
```

### Layer 3: the PE1 battle adapter

Everything that touches Parasite Eve's engine. Two places, unchanged roles:

- `mods/pe_enemy_import/hosts/pe1/`: starts the battle in place (D1, built:
  `pe_enemy_battle_start_here`), moves Aya to the battle anchor and back (D14),
  restores the movement lock (built), spawns joiners at mapped positions
  (`pe_spawn`, as `pe_summon` already does mid-battle), reports each foe's room
  position and death to the director, freezes on time stop (built), orders
  targets on screen first and draws the edge marker (D10).
- `mods/pe_re_play/hosts/pe1/pe_re_stage.c`: the backdrop (built), Aya's walk
  on the room's colliders (D5, built), and new: swap camera, picture, masks and
  effects when the director says so; draw masks and effects in battle (D8).

The host interface grows by what the stage must offer the director (append-only,
`struct_size` checked as today):

```c
/* re_host.h, appended */
int (*in_view)(double x, double y, double z);          /* built */
int (*battle_camera)(int camera);                      /* swap the backdrop to this camera: 0, or -1 */
int (*battle_room_position)(double *x, double *z);     /* Aya's room position in battle (battle_walk's) */
int (*vram_reserved)(RECT *out, int max);               /* VRAM the room's drawing uses (G2): count written */
```

### The two spaces

PE battle positions and RE room positions convert by one formula, k = RE play's
scale (`units_per_pe_unit`, 5.73 at Aya's size 100):

    pe = anchor + (re - room_at_start) / k
    re = room_at_start + (pe - anchor) * k

The anchor is chosen per battle (D14): the room's collision extent mapped at k
must fit in PE's 16-bit positions beside the host room's zones (S1). Only the
adapter and the stage use the formula; the director works in room units only.

### One frame of a running battle

1. PE runs its battle frame (`PE_HOOK_BATTLE_UPDATE`): the adapter learns
   whether battle time runs (mode 0, no flags 4 or 0x100).
2. The stage's camera hook (`on_camera`, backdrop on): Aya's step slides on the
   room's colliders (`battle_walk`); her room position is known.
3. The adapter calls `pe_re_battle_step` with that position. The director:
   picks her camera (`pe_re_camera_at`) and emits CAMERA if it changed; if time
   runs, moves pursuing enemies one step (`rules->chase_step`); finds enemies in
   view, on her floor, combatant, not in the battle; queues them, and emits JOIN
   for as many as the cap allows.
4. The adapter acts: CAMERA -> `battle_camera`; JOIN -> load the pack if
   needed, spawn at the mapped position, add to the trip's result.
5. Each joined foe's position goes back to the director (`foe_moved`); deaths
   (`foe_died`) free a place for the next in the queue.
6. PE ends the battle: the adapter calls `pe_re_battle_end`, restores Aya's
   native position, the movement lock, and resumes RE play.

### Invariants (checked by tests and logged when broken)

- I1: while a battle runs, Aya's native position is at the battle anchor's side
  of the map; outside battles it is where RE play holds it.
- I2: joined live foes <= `rules->max_foes`; every joined slot has one PE actor.
- I3: room -> PE -> room round trip moves a point by less than 1 room unit.
- I4: RE play is paused from battle start to battle end; no RE zone, door or
  menu runs in between.
- I5: no PE room transfer happens during a battle (`g_SceneDispatchToken`
  unchanged).
- I6: after the battle, the movement lock is what it was before it.

## Spikes

Settle each before the phase that needs it.

| # | Question | Result |
|---|----------|--------|
| S1 | Does every room fit PE's 16-bit positions beside the host's zones, at every Aya size? | Done 2026-11-24. Widest RE2 room 1-03 spans 64,000 room units: 5,585 PE units at size 50, 11,169 at 100, 22,339 at 200. All 252 room states fit with 8,000 units of zone clearance. |
| S2 | What does range do to a shot? | Done 2026-11-24. `Battle_CheckEvasion`: distance x 1000 / weapon range; hit odds bands at 500 and 800; some actions can't hit past 1000. Gap found: see S5. |
| S3 | Do RE1 and RE3 rooms import what battles need? | Done 2026-11-24. Collision (COLL) and camera zones are imported for all three by the shared reader (`pe_imp_rdt.c`). Missing for RE1/RE3: RE play's script runtime and collision reader are RE2's; that is RE play's port, a prerequisite, not battle work. |
| S4 | Can pursuit run while RE play is paused? | Done 2026-11-24. `pe_re_session_chase` takes the player's position, floor and collision as arguments: it can be driven with Aya's battle position. It's RE2-only today (moves into RE2's rules). |
| S5 | Battle distances scale with Aya's size setting (k). At size 200 every enemy is twice as far in PE units, so weapons lose range. Fix k for battles (size 100's), or scale ranges? | Open: a decision, before phase 1. |
| S6 | Does a battle at an off-map anchor work? | Done 2026-11-24 (experiment, code removed). Aya moved 20,000 units off map 66 at battle start: she, the zombies and the HUD drew as before; walking the route that had changed rooms mid-battle left the room unchanged; both zombies shot dead, the battle ended, she returned to her exact field position. The joiners had no PE floor there (flag 0x80): harmless. Positional sound not checked (the harness has no audio). |
| S7 | Can the stage swap camera, picture and masks while the backdrop is on, and how long does a swap take? | Done 2026-11-24 (experiment, code removed). Calling `pe_re_stage_show` for the new camera with the backdrop on, when `pe_re_camera_at` at Aya's battle position changed: camera 0 -> 1 in the street in 7.5 ms; the battle went on in the new view with Aya, the zombie and the HUD placed right. The stage needs the room logic kept (it isn't today) and a battle-safe show that leaves the backdrop's saved pages alone. |
| S8 | Two enemy kinds in one battle, the second joining mid-battle: VRAM for both packs? | Done 2026-11-24 (code reading; a live 1-06 encounter didn't trigger in the harness). `pe_place` never runs out: it overwrites the 64-wide block the battle reads least, sampled during the opening (`pe_on_background` until the first join). It knows nothing of RE play's VRAM (picture, effect pages, mesh texture and palettes), and for a late joiner its sample is stale. Gap G2. |
| S9 | Do masks and effects draw in battle? | Done 2026-11-24 (experiment, code removed). Masks: drawn cleanly from the backdrop path; occlusion of Aya behind a foreground object not yet seen (phase 3's check). Effects: garbage, because the zombie's pack texture went to VRAM 640,256, the effect pages. Gap G2. |

## Gaps found by the spikes

| # | Gap | Resolution |
|---|-----|------------|
| G1 | An imported enemy that died while the battle mode wasn't 0 froze in its death anim (D4's time stop came before the death code), and the battle could never end. | Fixed 2026-11-24 (bd61eaf51): the time stop applies to living foes; dying ones play on. A suspected Win-this-battle hang with two enemies was the results screen waiting for a button (the test pressed none); with Cross it ends normally. |
| G2 | The enemy pack loader and RE play share VRAM without knowing it: the loader overwrote the effect pages. | Phase 3 entry: RE play publishes the VRAM it uses (`ReHost` -> `vram_reserved(rects)`: picture, effect pages, mesh texture, palettes); `pe_vram_reserved` refuses those. A late joiner re-samples before placing. |
| G3 | The stage doesn't keep the room logic, and `pe_re_stage_show` isn't meant for battle use. | Phase 3: `battle_camera(camera)` in the stage: keeps the logic from the last show, swaps picture, masks, effects and view, leaves the backdrop's saved pages alone. |
| G4 | Positional sound at the off-map anchor is unchecked. | Phase 1 exit: listen in a play-test (the harness has no audio). |
| G5 | Since D17 the imported enemies join on the battle's first frame, so the pack loader places their textures with no battle frames sampled ("0 frames looked at"): it can't see what the battle itself reads, and may overwrite it. Seen once at 640,0 with no visible harm. | Phase 4, done in part (953def9c6): the loader keeps sampling the battle's frames (every fourth) and keeps out of every image the battle uploads through `Asset_LoadTimImage` (`PE_HOOK_TIM_UPLOAD`), so late joiners see what the battle uses. The first-frame packs are still placed before any battle frame; a fixed map of PE's battle VRAM would close that. |

## Phases

Each phase: entry checks, what is built, exit checks (harness, from PE's title
into the named room with the user's store), tests.

0. **Battles in place (RE2).** Built: D1-D5 (c383bf512, a7e42b88f, 1f1b56ae6,
   9d0db2f64, bdb868a05).
1. **Off-map anchor (D14).** Done 2026-11-24 (d1b30cee9).
   Entry: none (S5 deferred, D16).
   Build: anchor choice from the room's extent; native move at start, back at
   end; I1, I5, I6 logged. Exit: 1-00, run the length of the street in a
   battle; the PE room never changes; the battle ends normally.
   Tests: T1, T2.
2. **Rules tables and director skeleton (D15).** Entry: phase 1.
   Build: `PeReBattleRules` for RE2 from today's helpers, NULL for RE1/RE3;
   `pe_re_battle_begin` takes over `pe_trip_select_approach`'s selection; the
   adapter calls the director but behaviour is unchanged. Exit: the five story
   routes finish; battles start with the same enemies as before.
   Done 2026-11-24 (Codex, c5134ecb6) for the portable scope. T1-T4, portability, the disc-backed mod-re-session test, and all five story routes passed on Windows. Live roster parity is an adapter integration check.
3. **Camera follows, masks and effects (D6, D8).** Done 2026-11-24 (57fcf6aab).
   Entry: G2 and G3 built.
   Build: `battle_camera`, `battle_room_position`; CAMERA events; masks and
   effects in battle. Exit: 1-00, a battle from camera 0 into camera 1; the
   picture, foreground and fires change with her; aiming still works. Tests: T5.
4. **Join on sight, rolling cap, targeting (D6, D10, D11, D12, D19).** Entry: G2's re-sample for late joiners.
   Build: JOIN events, queue, spawn mid-battle, trip result per joiner, target
   order and edge marker. Exit: 1-00's six zombies across two cameras all end
   up in the battle; 2-05's ten come in waves of 6; 1-03 only takes enemies on
   Aya's floor; an off-screen target shows its marker. Tests: T6, T7, T8.
   The director and these headless tests are built. Adapter (953def9c6): joiners
   spawn with the battle's packs (no build mid-battle; a kind the battle didn't
   start with uses its installed pack or doesn't join); targets are reordered
   each frame (`pe_enemy_battle_order_targets`: a freshly built list starts on
   the first, a shown one keeps the chosen target); the chosen target's arrow
   and the joining cue are drawn by the stage (`battle_mark`, PE_RE_MARK_*).
   Checked in 1-00: zombies 2 and 3 walk in, join and are beaten (4 of 4);
   order "3 2 0 1" then "1 0 2 3" as they come into view; both marks draw.
   Known: the arrow sits about 30 px low of the target (left/right is right).
5. **Pursuit, escape, write-back, grace (D7, D13, D18).** Build: `chase_step` in
   battle time, `pe_re_battle_end`, the survivors' grace and its F1 settings. Exit: a zombie off screen walks into
   view and joins; after Escape the survivors stand where the battle left them
   and no battle starts during the grace period. Tests: T9, T10.
   The director and these headless tests are built. After each field frame,
   the adapter steps survivor grace and checks the lead with
   `pe_re_battle_encounter_allowed_for` before starting a fight.
   Adapter (953def9c6): `battle_begin` picks the battle, `battle_step` runs each
   battle frame with PE's clock, deaths and positions go back by room slot, and
   `battle_end(escaped)` starts the grace; F1 > Fights has "Escape grace
   (seconds)", "Escape grace distance" and "Halve grace on repeat escapes".
6. **Bosses and story fights (D9).** Build: `boss`, `joins_boss` for RE2.
   Exit: G's embryos join the G fight; the canister and launcher finishes still
   work; the five story routes finish. Tests: T11, the route tests.

RE1 and RE3: once their rooms are playable, fill in their rules tables and run
phases 3-6's exit checks in one room of each.

Phase 1 as built: `mods/host/pe_battle_space.{h,c}` (shared, no engine: anchor choice and
the two-way mapping; tests T1, T2 in `tests/pe_battle_space_test.c`, ctest
`mod-battle-space`). The trip (`pe_enemy_import.c`: `pe_anchor_battle`,
`pe_unanchor_battle`) measures the host room's floor from `g_CollisionDb`, takes the
imported room's bounds from RE play's new `room_extent` (API, appended) and Aya's room
position from the encounter (RE play can't report it once paused), moves her native
position to the anchor at battle start, puts her back on every way a trip ends, and logs
I5 if the host room changed. Exit check: from PE's title into Leon A's street, the host
floor was x 1930..7326, z -3354..3821 and the battle was fought at -2086,1711; walking the
route that had changed rooms (map 66 -> 67) left the room unchanged; both zombies shot
dead, the battle ended, Aya back at her exact field position.

Phase 3 as built (G2, G3, D6, D8, D22), `mods/pe_re_play/hosts/pe1/pe_re_stage.c`: the stage
keeps the shown room's logic; `pe_re_stage_battle_camera` swaps picture, masks and view in
battle without touching Aya, the collision or the backdrop's saved pages (the picture code
is shared with `pe_re_stage_show` as `install_picture`); `battle_follow` asks
`pe_re_camera_at` each battle frame at her battle position; masks and effects draw in front
of the battle; at the battle's end the current camera's picture goes back and Aya keeps her
battle position. G2: `pe_re_stage_vram_reserved` (ReHost and API `vram_reserved`, appended)
lists the picture, effect pages and mesh texture, and `pe_place` keeps out of them. Exit
check (from PE's title into Leon A's street, user's store): camera 0 -> 1 mid-battle; the
picture, foreground and fires follow; the zombie's texture went to 640,0, not the effect
pages, and the fires draw clean; both zombies shot dead; after the battle Aya stands in
camera 1 where it ended and walks on. Not yet seen: Aya passing behind a foreground object.

Windows follow-up. The Fights menu has a **Reinforcement sound** toggle,
off by default. The edge arrow uses the room's field visibility test
and points toward the enemy's head. The arrow position still needs a
capture with a live off-screen target.

A Claire B room check found nine EM1F placements in 1-03, including
placements reported on a different floor. Room 2-05 had four EM10
placements in this state. Room 1-1D had seven EM1F placements and is a
candidate for the native rolling-cap test. The adapter still retains
at most six native foe records for the duration of a fight, so the full
wave exit check is open.

Phase 6 preparation now reads the RE2 boss table for special battle rules.
T11 checks the Alligator choice and canister, Tyrant launcher, and G
embryo eligibility. The Leon Windows canister route won and preserved
the result after re-entering 4-0A. Live boss roster integration remains open.

Native Escape in 1-00 preserved all six zombies. With default grace, the
first 120 vblanks after return produced no encounter. With longer grace,
a zombie outside the escaped roster was still able to start a fight.
The street regression also re-entered combat, won, and saved with three
zombies remaining.

## Tests

Headless tests (`tests/pe_mod_re_battle_test.c`, new) use synthetic rooms and a
fake `in_view`; they need no disc.

| # | Test | Phase |
|---|------|-------|
| T1 | Anchor choice: a room's extent at k fits the 16-bit space beside a given zone box; refuses when it can't. | 1 |
| T2 | Round trip room -> PE -> room within 1 unit (I3), at k for sizes 50, 100, 200. | 1 |
| T3 | RE2 rules: combatant and pursuer kinds match today's helpers for every RE2 kind. | 2 |
| T4 | `begin`: lead plus in-view enemies on the floor, nearest first, cap 6; people and scene actors left out. | 2 |
| T5 | CAMERA event when Aya's position crosses a camera zone; none while she stays. | 3 |
| T6 | JOIN: an enemy entering view joins; a full battle queues it; a death lets the next in. | 4 |
| T7 | Floors: an enemy on another floor never joins. | 4 |
| T8 | Target order: in view first by distance, then out of view by distance. | 4 |
| T9 | Pursuit moves only while time runs; positions are written back after an escape. | 5 |
| T10 | Grace (D18): the survivors can't start a fight until the time or the distance runs out; other enemies can; a second escape from the same survivors halves it. | 5 |
| T11 | Boss: who joins a boss fight follows `joins_boss`. | 6 |

Disc tests: `mod-re-session` and the five story routes (`mod-re-route`) after
every phase. Harness scenes as in each phase's exit check.

Logs: one line per battle event, so a play-test report shows what happened:
`battle: start 1-00 floor 0 camera 0 foes 2`, `battle: camera 1`,
`battle: join slot 3 (queue 1)`, `battle: death slot 3`,
`battle: end won|escaped survivors 2`, and any broken invariant.

## Per-game adjustments

| | RE2 | RE1 | RE3 |
|---|---|---|---|
| Rooms playable in RE play | yes | not yet (scripts, collision reader) | not yet (scripts, collision reader) |
| Collision and camera zones imported | yes | yes (`pe_imp_rdt.c`) | yes (`pe_imp_rdt.c`) |
| Enemy and person kinds | known (`re_encounter_rooms.h`) | to map (`pe_src_re1_emd.c`) | to map (`pe_src_re3_emd.c`) |
| Pursuit | `pe_re_session_chase` | to define | to define; Nemesis follows between rooms, but joins only in Aya's room (D10) |
| Bosses and story fights | G, Alligator, Tyrant, Mr. X (partly) | to list | to list |
| Camera zones | `pe_re_camera_at` | own-area flag (`flag` 1) | to check |

Adding a game: its rules table, its collision and camera access in the shared
layer, T3-T11 with its kinds, and one room's exit checks per phase. Adding a
host engine: implement the three `ReHost` battle calls and an adapter that
turns director events into its own battle; the director and rules stay as
they are.

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
- Range: `Battle_CheckEvasion` (see D10, S2).
- Mid-battle spawns work (`pe_summon` for G's embryos, through `pe_spawn`).
- The engine has 7 enemy places (`g_BattleEnemySlots[7]`) and ends a battle when
  the last live enemy dies (`Battle_StepEntityDeath`); Escape is
  `Battle_StepEscapeOrDeath`.
- PE room scripts test zones on Aya's position (`Script_PointInPoly`), then
  lock movement and change rooms (`Script_SetSceneFlag2000`,
  `Entity_SetGlobalFlag1`, `Script_EncodeBarcode`); scripted battle ends are
  mode 8 (`Menu_SetModeState8`).
- A room battle's script locks field movement (`g_FieldMoveLock` bit 1) and
  lifts it after; in-place battles restore it themselves.
- RE play is paused for the battle: the room's scripts, zones and other enemies
  don't run until it resumes (D7 drives pursuit only).

## Risks

| Risk | Mitigation |
|------|------------|
| VRAM runs out with two packs plus effects and masks | S8 and S9 first; a refused joiner stays in the room and is logged |
| A camera swap stalls the battle | S7 measures it; decode once per change, not per frame |
| Host rooms other than map 66 have zones near the anchor | the anchor is chosen from the host's own collision extent (T1) |
| Story scripts expect enemies where they were before a battle | write-back only after an escape; won battles report deaths as today |
| Aya's size setting changes weapon range | S5 decides before phase 1 |

## Open questions

- S5 (D16): fixed battle scale or scaled weapon ranges, once play-testing shows which feels right.
