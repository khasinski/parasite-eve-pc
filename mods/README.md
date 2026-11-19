# Mods

Mods for the native Parasite Eve port, built only with `-DPE_MODS=ON`. With it off the game is
the unchanged original: the `PE_HOOK(...)` lines in the game compile to nothing.

```
cmake -S . -B build-mods -G Ninja -DPE_MODS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-mods
```

`PE_MOD_LIST` (CMake cache) picks the mods, by folder name. Default:
`pe_qol;pe_rebalance;pe_economy;pe_rewards;pe_catalog;pe_shops;pe_collection;pe_wardrobe;pe_stacks;pe_warp;pe_quicksave;pe_cheats;pe_phone;pe_src_re1;pe_src_re2;pe_src_re3;pe_src_dc1;pe_import` (desktop builds add `pe_assets;pe_src_ff7;pe_lang;pe_fonts;pe_enemy_import`; add `pe_hello` for the example).

Two switches, so a modded build is safe to hand out:
- **Build**: `PE_MODS` off (the default) leaves no mod code in the game at all.
- **Runtime**: F1 > Manage mods > **All mods**, **off by default**. Until a player turns it on and
  restarts, no mod starts and nothing but the F1 panel touches the game. Below it, one toggle per
  mod; a mod turned off doesn't start, nor do the mods that need it. Changes apply at the next start.
  The exceptions are **Language packs** and **Font**: a language or font the player picked is no
  gameplay change, so they run with All mods off too (their own toggles turn them off).

The upstream test suite (`-DBUILD_TESTING=ON`) gives the same results with `PE_MODS` on and off.

## Layout

```
include/pe_hooks.h     hook ids and context structs: the only file the game includes
mods/
  host/                the mod host, shared by every mod
    pe_mod_host.*        mod registry (start order by requirements, Manage mods), hooks, services,
                         events, pe_mods.log, pe_play.log (each row also the "play.log" event), pe_toast
    pe_mod_menu.*        rows of the Mods panel and pe_mods.ini
    pe_mod_panel.c       the F1 panel: the list of mods, pages, navigation, input
    pe_pages.*           where PE Enhancements rows live (one layout for the whole suite)
    pe_services.h        every service struct mods share (no mod includes another's headers)
    pe_game.*            game access and the game facts every mod shares (see below)
    pe_game_facts.c      those facts as tables: mod effect names and groups, power tags
    pe_main_menu.*       extra rows in Aya's main menu, screens from the game's own widgets, Yes / No
    pe_equip_sync.c      edited equipment reaches the next battle
    pe_debug_control.c   commands from a test script, off unless PE_DEBUG_CONTROL is set (docs/DEBUG_CONTROL.md)
    pe_gametext.h        text in the game's glyph code; pe_item_table.h the game's item names
    pe_image.h           the picture type the image code shares (RGBA, straight alpha)
    pe_present.*         image presenter: the only code that puts pictures into the game's frame
                         (pasted at PE_HOOK_FRAME_DRAWN; box now, strips as a test; never read back)
    pe_disc_assets.*     read-only game data from the player's disc (a room's blobs)
    pe_model.*           decoded game models: geometry, textures (private VRAM), idle animation
    pe_model_view.*      a model posed, turned, fitted and drawn into a picture (no game state)
    pe_model_stage.*     a model on show for a screen: turning, idle animation, pause, silhouette,
                         its box or full screen, through the presenter (Bestiary, test pages)
    ui/                  drawing (pe_ui_raster), the overlay quad (pe_ui_gpu), toasts
  pe_qol/              quality of life: movement, encounters, key item pocket, bag sort, Key Items menu
  pe_rebalance/        mod effects, Parasite Energy balance, half-bar turns / Quick powers, Combine
  pe_economy/          the Junk wallet (counter, Difficulty, prices), Costume unlock rules, Smoke Grenades
  pe_shops/            Requisition, Wayne's menu and Workshop, Warner, craft refunds (needs pe_economy)
  pe_catalog/          Mod Catalog / Dismantle / Rebuild (its record keeps the name pe_economy.catalog)
  pe_rewards/          battle rewards: extra Junk, rare drops and pity (needs pe_economy)
  pe_collection/       Bestiary registry and menu (creature packs register here)
  pe_wardrobe/         Wardrobe registry, skins, Wardrobe menu (costume packs register here)
  pe_stacks/           stacked consumables (up to 99 per bag or storage slot), from the Pathogen Eve port
  pe_warp/             warp menu to story checkpoints, from the Pathogen Eve port
  pe_quicksave/        10 quick save slots in the game's own save format (F10 / F11 / F12)
  pe_cheats/           god mode, infinite ammo and PE, instant win, all PE powers
  pe_enemy_import/     F1 > Fights: start a chosen random battle in its own room; imported enemy packs
  pe_phone/            Phone Calls: after saving at a phone, "Stay on the line?" and a short call
  pe_challenges/       optional harder rules per run (one file per challenge, Hardcore permadeath),
                       bundles, assisted marker, run summary (opt-in via PE_MOD_LIST)
  pe_lang/             language packs: the game's text from a .pelang pack, F1 > Language (not built
                       by default; docs/LOCALIZATION.md, tools in localization/)
  pe_hello/            example mod (not built by default)
  pe_import/           F1 > Import: models from the player's other PlayStation games, built into packs
  pe_src_re1/          import source: Resident Evil, Director's Cut (its reader, naming table and rig)
  pe_src_re2/          import source: Resident Evil 2 (its readers, naming table and rig)
  pe_src_re3/          import source: Resident Evil 3 (its reader, naming table and rig)
  pe_src_dc1/          import source: Dino Crisis (Regina's outfits; its reader, naming table and rig)
  host/import/         the importer core (disc images, store, catalog, PE formats); see Import below
```

## Stacked items (pe_stacks)

F1 > PE Enhancements > Inventory > Stacked items (on by default). Lists keep plain item ids; the
extra copies of a slot are counted per (list, item) in the mod. Hooks: ITEM_ADD (join a carried
stack), ITEM_REMOVE (use / give / discard takes one copy), ITEM_COUNT and INV_CMD_COUNT_ITEM
(story checks see every copy), STORAGE_TRANSFER (a bag stack goes into storage whole; storage
gives one copy), INVENTORY_REBUILD (duplicates fold into one slot), SLOT_DRAWN (the count in the
rounds column). Only base items of kinds 10 and 12..15 stack; Junk stays out while pe_economy's
Junk economy is on. Saved in the companion file as the `pe_stacks/counts` section (204 bytes, up to 100 stacks); an older `pe_stacks.counts` record moves there on load. With the
mod or the option off, extra copies go back into free slots as plain items.

## Font (pe_fonts)

F1 > Font: Original keeps the game's own letters; Pixel Sans (shipped in `mods/fonts/`) swaps in a
bolder set, drawn from Pixeloid Sans Bold by GGBotNet (SIL OFL 1.1, see `mods/fonts/README.txt`).
The pick shows at once and is kept in `pe_fonts_selection.txt` in the user folder.
Every `.png` in `mods/fonts/` beside the game or in the user folder is a choice, named after its
file, with no code to add; `mods/fonts/README.txt` describes the picture. The letters go into the
dialogue font page as it goes to VRAM (PE_HOOK_TIM_UPLOAD, before pe_lang adds its accents) and
their widths into the dialogue and menu tables. US and European discs only; the Japanese discs keep
their own letters.

## Warp (pe_warp)

F1 > Warp: pick a story checkpoint and Warp. It sets script variable 74 (story progress) to that
point and runs the field script's own room transfer (Script_EncodeBarcode) on the next free field
frame, never during a battle, an event, a room change or a movie.

## Fights (pe_enemy_import)

F1 > Fights: pick an area and a fight, then Start fight. A room's enemies live in that room's data,
so a picked fight is that room's own random battle: the field script's room transfer (as pe_warp,
story variable set to the area's point), Aya stood in the spot's floor zone (mods/host/pe_encounter.c
finds the zones in the loaded room script) and that spot's encounter roll made to succeed once.
After a win or an escape the script variables go back as they were and Aya returns to where she
stood. A loss is the game's own Game Over. The list holds only fights the `enemy-import-fights-all`
CTest saw start and come back. Logs `cheat_fight_picker`, so challenge runs count as assisted.

**Imported enemy** (pe_enemy_battle.c, pe_enemy_pack.c): a pack in `<prefs>/pe_enemies/<folder>/`
(formats in pe_enemy_pack.h: enemy.json, model.bin, texture.bin, anims/) is added to the picked
fight as a new enemy; nothing of the room is replaced. **How many** adds 1-3 of it; **Room's
enemies** lets the room's own foes fight too or sends them away. Each one is a new actor from the
game's actor pool (type 0xEE, no model or script) with its own enemy record (Entity_AllocSlot),
stood on the floor (Entity_FindFloor). It joins once the room's battle script has started the clock
(g_BattleEnemyEscapeFlag) plus 10 frames (900 frames at most). Its brain runs each battle frame:
turn and walk to Aya, attack when its time bar is full (curHP >= 9000) and Aya is in reach, land
the blow at the attack's "hit" frame through the game's enemy-hit path (effect record power,
coreFlags bit 31, Aya stateFlags 0x4000), stagger when shot (unless mid-attack), and at 0 HP play
its death animation, then the game's own death fade, EXP (hpMirror = exp) and free. Drawing: at
PE_HOOK_ACTOR_RENDER (Entity_FrameUpdate, before posing) the mod poses the pack's model (every
vertex on its bone, joint vertices half and half on two) and links it as textured triangles,
depth-sorted like the game's, and returns nonzero so the game skips its own posing, draw and
shadow for that actor. The texture goes to VRAM x 320..511, y 256..511 (palettes x 512, y 500..),
which no battle room uses; what was there is saved and put back after the battle. Pack fields the
brain reads: hp, defense, exp; "behavior" {speed, charge, radius, stagger}; per attack "power"
(else "damage"), "range" and "hit" (else half the animation). Tests: `mod-enemy-pack` (loader,
refusals, posing, behavior fields; a real pack with PE_TEST_ENEMY_PACK) and `enemy-import-nemesis`
(PE_TEST_DISC + PE_TEST_ENEMY_PACK: alone and beside the room's enemies, it appears, walks,
attacks, is hurt, dies, the battle is won and the VRAM block given back).

**Built in the game (F1 > Import)**, the same way as costumes: a source game's mod registers
readers, a naming table and an **enemy profile** (`host/import/pe_imp_enemy.h`: moves for the
roles and attacks, HP, power, reach, height, sounds) for an asset; the `enemies` builder target
(pe_enemy_build.c, pe_enemy_target.c, "Build as an enemy") turns the catalogued Model, its
Animation and its Sounds into a pack in `<prefs>/pe_enemies/<game>-<name>/` (targets may now name
their own place and which assets they take: `accepts`, `place`, `current` in PeImpTarget). Pack
additions: anims version 2 carry a u16 event per frame (sound number, hit frame); "sounds" lists
WAV files. Sound frames play through the `audio` service's new `play_effect` (several voices at
once). Importer core additions: Model v2 (optional per-corner skin: bone + second bone, written
only for skinned models), the Animation type (moves, frames, per-frame events), and a PC game
folder of RE3 `rofs*.dat` archives opened like a disc image (pe_imp_rofs.c). pe_src_re3 reads RE3
PC enemies (`ROOM/EMD/EM??.EMD` + TIM: skinned model and all moves; step flag 0x800 = hit frame,
top 4 bits = sound) and enemy sound banks (`DATA/SOUND/E_??.VH` + VB, as tones at their pitch);
Nemesis = EM34 with his profile and an automatic build once the RE3 PC folder is in the import
folders. The PS1 disc keeps its enemies in each room's bundle, `CD_DATA/STAGEs/Rsrr.BIN` (u32 size,
u32 count, per entry u32 length and tag, entries on 2 KB boundaries; tag byte 3 = the enemy number,
byte 1 = 1 for its TIM, byte 2 a version): the same EMD and TIM formats, read by `g_re3_emd_ps1`
into `enemy/em<nn>-<version>` and its moves (85 from 64 enemies; 21 come in two versions whose later
move sets differ). The Nemesis profile still names the PC file's `enemy/em34`. Tests: `mod-re3-enemy` (archives, EMD, tones and a scan on made-up data;
PE_TEST_RE3_PC = the RE3 PC folder adds EM34 and E_34) and `mod-enemy-build` (builder on made-up
data; with PE_TEST_RE3_PC Nemesis built from the store into `enemy-build-test/nemesis`).

## Quick save (pe_quicksave)

F10 saves to the current slot, F11 loads it, F12 picks the next of 10 slots (also F1 > Quick save).
A quick save is the game's own 8 KB card file (Save_BuildCardFile, with every mod's records), made
in the free field and kept beside the preferences as quicksave_<slot>.mcd. Loading commits it the
way Save_LoadCardFileIntoRuntime does, without the card menu's dialogs, then runs the field
script's room transfer to the saved room: the room starts again as after a loaded save. It holds
what a save holds, not Aya's place in the room or a fight in progress.

## Phone Calls (pe_phone)

F1 > Phone Calls. After Aya saves at a phone (the game's own question and save screen, unchanged),
a box asks "Stay on the line?" with No first, so one Cross goes straight back to the game. Yes
plays a short call in dialogue boxes: `*dial... ring... ring...*`, the call (Aya's lines as
`{name}: ...` in the game's dialogue band, the caller's in a window higher up), then `*click*`.
Cross goes on, Circle hangs up. Aya is held by the field lock (field event 0xFE / 0xFF, as a
talking actor's script sends) until the last box closes. Backing out of the save screen, or
answering No to the save, asks too while F1 > Ask without saving too is on (the default; off: only
a real save asks). The question then comes once Aya has put the phone down (only a real save
leaves her put-down waiting to be held; holding her earlier would stall a Yes before its save
screen). Nothing in the game's scripts is patched.

- Spotting: the save question page ("Will you save your progress?" with two choices) is
  recognised at PE_HOOK_TEXT_PAGE ahead of every page handler; each sighting logs
  `phone room=<map> story=0x<var 74> day=<n>` to pe_play.log (the room list phase 2 needs). The
  story save near the end, which starts with Aya's name, gets no call.
- A real save: the host's `save.committed` event (`PE_EVENT_SAVE_COMMITTED`, from
  `pe_mod_save_committed` once the card file is published). Quick saves and room checkpoints
  never commit, so they never ask.
- **Calls are text files anyone can read and change.** The game's own are
  `pe_phone/calls/pe1.txt` (its top explains the format), built into the game by CMake. The
  first start writes a copy to `pe_calls/pe1.txt` beside the preferences; that copy, when present,
  replaces the built-in calls (delete it to go back), and every other `.txt` in `pe_calls` adds
  its calls. The copy's first line is a stamp (the hash of the calls it was written from): while
  the rest still matches it, nobody edited the file and a newer game rewrites it with its calls;
  an edited copy is never overwritten, and the game's calls go beside it as `pe1.txt.new` (not
  loaded). A copy without a stamp (older builds) is adopted when it equals the built-in calls,
  otherwise treated as edited. A call looks like:

  ```
  [mine/pizza2]            key: pack/name (saves remember it: don't rename)
  caller = Tony's Pizza
  tone = funny             serious or funny
  day = 1                  1 to 6, 2-5, or any
  rooms = 87               optional: only at these phones
  priority = 10            optional: plays first there until heard
  them: Tony's again. You hungry?
  aya: Not anymore.
  ```

  Mistakes go to `pe_mods.log` with the file and line; a call with a mistake is left out, the
  rest load. F1 > Phone Calls > Writing calls: Open calls folder, Reload calls (no restart), Try
  call (pick any call by key), Play it now / Ask now (no save needed), Check calls (the game's font).
- Mods can still add calls in code: the `phonecalls` v1 service (host/pe_services.h): `add`,
  `find`, `pick`, `heard`, `reset`, `add_condition`; events `phone.call_start` / `phone.call_end`
  (ctx: the call). A call has a key, caller, tone (serious / funny), lines, weight, `once`, story
  days (the save screen's Day) or a variable 74 range, phone rooms, priority, `after` and `needs`.
- Set calls: rooms + priority + a single day, chained with `after`; the header of `calls/pe1.txt`
  lists every phone's room, place and day. One day each keeps the record at 48 bytes: its once list
  (16) forgets the oldest, and a forgotten set call can't fit its day again.
- Conditions: `needs = warner.supplies` plays a call only while another mod's feature is on. The
  mod that owns the feature offers the name with `add_condition(name, holds)`; a name nobody offers
  never holds, so neither mod needs the other. Offered now: `warner.supplies` (pe_shops: Warner
  helps out and the Junk economy on), used by `pe1/park_wayne`, Wayne's call at Central Park;
  `phone.crossovers` (Phone Calls itself: F1 > Phone Calls > Crossover calls, on by default),
  used by the Raccoon City and Final Fantasy calls in `pe1.txt`.
- Picking: a set call (priority > 0) not heard yet first; else weighted random among the calls
  that fit, leaving out the last five played.
- Text: ASCII in the game's glyphs (letters, digits, space, `. , : ! ? ' - + * % /`, `{name}`).
  Lines are wrapped with the game's glyph widths and the player's name, at most three lines a
  box; a longer line runs on into another box. Debug > Check calls measures every call.
- Per save: record `pe_phone.heard` v1 (48 bytes): the last five calls and up to 16 one-time
  calls heard, as 16-bit key hashes. New Game resets it; F1 > Forget heard calls clears it.
- Aya stays on the phone (pe_phone_hold.c): when the save is written, the phone's task already
  waits (opcode 48) for her put-down animation (0x26 from frame 0x3C, receiver at her ear). While
  the question and the call are up her animation step is 0 (PE_HOOK_ACTOR_STEP, a local value), so
  she holds the receiver, the task waits and the phone's own field lock holds the field. On
  `*click*` (or No) she is let go: the game's own put-down and hang-up sound play, then its unlock.
  A phone that lets her go before the save ends (the sewer's emergency phone) has its own pose
  replayed for the call (animation 30 to frame 14, opcodes 46/78/47 as its script does), then idle.
- Sounds: the phone's own. Every save phone's script plays four sounds from its area's bank,
  from a base id A (Akao command 0x15F): A and A+1 after Yes (pick up, dial tone), A+3 and A+2
  after the save (put it down). A call plays the pick-up and the tone twice ("ring... ring...") on
  its ring page, stopped when the other side picks up, and the put-down pair on `*click*`. The ids
  come from a table of the 23 phone rooms (pe-native-debug `tools/phone_rooms.py`); elsewhere no
  sound. F1 > Phone sounds turns them off.
- Only save phones: the same question also comes when a new day starts (M351, M352), in the debug
  room (M360) and as the story save (M271); those get no call. The Day 1 sewer's emergency phone
  (M032) saves through "Use the phone" with no question of its own: that choice is spotted instead
  (`pe_phone_use_option`), and its set call `pe1/sewer_emergency` plays first there. Its call has
  no sounds (the sewer's bank has no phone sounds).
- Settings: Phone calls (Ask after saving / Always call / Off), Ask without saving too (on),
  Call tone (Mixed / Serious only / Funny only), Phone sounds.

Files (one job each; they talk only through `pe_phone.h`):

| File | Job |
|---|---|
| `pe_phone_mod.c` | the mod: descriptor, F1 rows, settings |
| `pe_phone_service.c` | the `phonecalls` registry and this save's `pe_phone.heard` record |
| `pe_phone_spot.c` | spotting a phone save: the question page, then `save.committed` |
| `pe_phone_flow.c` | after the save: "Stay on the line?", then the call page by page |
| `pe_phone_box.c` | the game side: our dialogue box, field lock, taking the pad, the font |
| `pe_phone_sound.c` | the phone's own sounds: pick up and ring, put down |
| `pe_phone_hold.c` | keeping Aya at the phone, receiver at her ear, until the call ends |
| `calls/pe1.txt` | Parasite Eve's own calls, as text |
| `pe_phone_packs.c` | call files: the built-in pe1.txt and the `pe_calls` folder, reload, open folder |
| `pe_phone_file.c`, `pe_phone_text.c`, `pe_phone_record.c`, `pe_phone_pick.c` | the pure core (`pe_phone_core.h`): reading call files, pages and wrapping, the heard record, picking |

`tests/pe_mod_phone_test.c` (CTest `mod-phone`) checks the pure core and `calls/pe1.txt` without the game;
pe-native-debug's `scenarios/phone_calls.py` checks the game side through real phone saves.

## Cheats (pe_cheats)

F1 > Cheats: God mode, Infinite ammo and PE, Win this battle, and Learn all / Restore PE powers.
All off until chosen; while they are off the mod writes nothing.

## Challenges (pe_challenges)

Not in the default `PE_MOD_LIST` yet (the PS5 port pins `mods/CMakeLists.txt`): add `pe_challenges`
to build it.

New Game opens a title-style challenge picker when this mod is enabled. Each challenge shows
its current Off / On / level value and description. Hardcore is the first row, followed by the
other challenges; Clear all challenges and Start game are at the bottom. Choose a row to edit it; Back keeps its
previous value. Combine challenges, Clear all challenges, then Start game to commit the draft.
Long lists scroll through six rows, with a range indicator. Hardcore's Start game summary warns
that death ends the run permanently and challenge levels cannot be lowered after starting.

The picker is independent of Combat styles: with both mods enabled, style selection comes first,
then challenges; with only one enabled, only its picker appears; with neither, New Game proceeds
without either question. A build may omit either mod. Continue/load and EX Game do not open the
challenge picker. Headless or unavailable UI preserves the F1 choices. Settings are saved only
when Start game confirms the draft; the existing run record, locks and F1 controls stay in use.

F1 > Challenges, all off until chosen (while every one is off the mod writes nothing). The top
page starts with Hardcore (featured: `CH_DEF_FEATURED` puts a challenge's row there), then an
overview (Run, On now, Cleared, Warnings); every other challenge sits on its category's
sub-page (a category left with no rows gets no page), then Presets, This run and Debug:

| Category | Challenge | Rule | File |
|---|---|---|---|
| Restrictions | No Parasite Energy | no PE battle tab; every power dimmed, field too | pe_ch_no_pe.c |
| Restrictions | No items in battle | no Item battle tab | pe_ch_menu_rules.c |
| Restrictions | No escape | no Escape battle tab | pe_ch_menu_rules.c |
| Restrictions | Locked loadout | no gun / armor battle tabs | pe_ch_menu_rules.c |
| Restrictions | Club only (Club 2x / Club 1.5x) | weapon hits with a Club (items 0x3F-0x43) deal x2 / x1.5; with a gun, no damage, except Maeda's gun (0x61, 0x93: the last fight needs it), which keeps normal damage; PE untouched | pe_ch_club_only.c |
| Enemies | Tough crowd (1.5x / 2x HP) | enemies take /1.5 or /2 damage; HP, EXP, drops untouched | pe_ch_damage.c |
| Enemies | Fast enemies (1.5x / 2x) | enemies' turn gauges gain 50% / 100% more than the game added each frame (pauses, Slow, menus unchanged) | pe_ch_fast_enemies.c |
| Aya | Glass cannon | Aya takes x2 from hits and contact, deals x1.5 | pe_ch_damage.c |
| Aya | Half health (1/2 / 1/4 HP) | current HP capped at half or a quarter of max, field and battle; max HP itself never changed | pe_ch_half_health.c |
| Aya | One-hit KO | any hit, contact or HP-reduction attack takes all of Aya's HP | pe_ch_one_hit.c |
| Aya | Blood price (1 / 2 HP per PE) | each power also costs HP (its real PE cost), once per cast, never the last HP | pe_ch_blood_price.c |
| Aya | No EXP | the battle's EXP total is zeroed just before it is paid | pe_ch_no_exp.c |
| Run rules | Hardcore | permadeath; New Game only; locks the run | pe_ch_hardcore.c |

Each definition names its category (`PeChallengeDef.category`; names and descriptions in
`g_ch_categories`, pe_challenges.c), so F1 groups by it with no list to
update. Rows moved from the top page to their category page keep their saved picks
(`pe_menu_alias_setting` from `Challenges/<label>` to `Challenges/<category>/<label>`).

Presets: Purist (No PE, No items, No escape), Nightmare (Purist plus Locked loadout, Glass cannon,
Tough crowd 2x) only turn challenges on; Clear all is press-twice.

- **Modular:** each challenge is a `PeChallengeDef` in the file of its rule: rows (label, levels),
  the battle tabs it removes, flags (`CH_DEF_NEW_GAME_ONLY`, `CH_DEF_LOCKS_RUN`) and its hooks. The
  core (`pe_ch_run.c` rows and record, `pe_ch_tabs.c`, assisted detection, run log, history,
  milestones, summary, bundles, the screen and the notice box) only reads the definitions. A new challenge = an id (append only: ids are save bits), a
  definition in its own file, a line in `g_ch_defs` (`pe_challenges.c`). `pe_challenges.h` maps it all.
- **Per run:** the set and levels are saved with the game (record `pe_challenges.run` v2, 48 bytes),
  so a loaded save shows its own set. At the title the rows are the pick for the next New Game.
- **Assisted, never blocked:** a challenge turned off or lowered after the run's first minute or
  first won battle, or a `pe_cheats` / `pe_assists` row switched on (or Win this battle) while a
  challenge is on, marks the run Assisted. Other mods that work around a challenge (shops under No
  items, Smoke Grenades under No escape) get a line on the Warnings row.
- **Tabs:** the battle tabs are `g_MenuItemContextFlag` bits 0-4, which the game saves; they are
  cleared only in battle (after Half-bar ATB's own tab handling) and the game's bits go back on the
  first frame after. If every tab would go, Weapon stays (an empty battle menu is untested).
- **Damage:** PE_HOOK_HP_LOSS for Aya (Glass cannon sources 1-2, then One-hit KO sources 1, 2, 4);
  PE_HOOK_ENEMY_HIT / ENEMY_HIT_END give each hit's damage on an enemy, scaled after Rebalance and
  before the Bestiary's kill check. Challenges that change dealt damage register a modifier
  (`pe_ch_damage_modifier`) that multiplies the hit's num / den; a num of 0 means no damage.
  Club only can't block equipping a gun (the game has no equip-refusal hook), so gun hits do
  nothing and a message says so; the Weapon tab stays for switching to the Club in battle. No EXP warns that battle Junk (pe_rewards) scales with EXP.
- **Other mods** (checked against every mod in the default list; nothing in them changed):
  - Half-bar ATB edits the same tab word: the tabs code runs after it and follows its writes.
  - Rebalance mod effects adjust each hit at the same hooks: the scaling runs after them, so it
    scales their final damage. Glass cannon also goes past Rebalance's Attack All crowd cap (x1.5).
  - No PE has the last word on power availability over PE balance / Quick powers.
  - The Bestiary's kill check runs after the scaling, so kills still count.
  - Assisted: pe_cheats / pe_assists rows switched on, and any play log tag starting `cheat_`
    (Win this battle, Warp).
  - Hardcore: quick load works only in free field, and the death is written on the game over's
    first frame, so neither a quick load nor closing the game saves the run.
  - Warnings (`kWarnings` in pe_ch_summary.c, one line each, a mod or one of its rows): shops
    under No items, Smoke Grenades under No escape, battle Junk under No EXP, No random battles.
- **Events for other mods** (`pe_ch_events.c`; listen with `pe_hook_add(pe_event_id("..."), ...)`,
  no header of this mod needed):

  | Event | ctx | When |
  |---|---|---|
  | `challenges.changed` | `const uint32_t *` active mask, bit = challenge id | after a load, a New Game, every change |
  | `challenges.assisted` | `const char *` what did it ("Warp", "God mode", "lowered: No PE") | once per run |
  | `challenges.run_ended` | `const uint32_t *` run id | a Hardcore run's game over |
  | `challenges.run_cleared` | `const uint32_t *` run id | a challenge run's Clear Data save, once |

  Challenge ids (append only): 0 No PE, 1 No items, 2 No escape, 3 Glass cannon, 4 Tough crowd,
  5 Hardcore, 6 Locked loadout, 7 One-hit KO, 8 No EXP, 9 Blood price, 10 Fast enemies, 11 Club only, 12 Half health.
  A `challenges` service (is a challenge on, its level, report an assist) belongs in
  `host/pe_services.h`, which the PS5 port pins by checksum, so it waits for upstream. Until then
  cheat mods are detected through their rows and the `play.log` `cheat_*` convention.
- **Debug** (F1 > Challenges > Debug, nothing saved or changed): Preview the Hardcore notice (the
  fallen-run window in the field, closing to nothing) and Preview fallen saves (Hardcore saves grey
  in the save / load lists for the session).
- **Not yet:** Scarcity, no bonus-point spending.
- **Challenges in Aya's menu** (`pe_ch_screen.c`): no row of its own in Aya's menu. The game's
  System list (the SYS icon, "Set User Preferences": Sound, Cursor, Window, Position) gets a fifth
  row, **Challenges**, outside battle with a game loaded; the panel widens to fit it. It opens a
  read-only screen from the game's own widgets: two framed lists side by side, **Active** (the
  challenges on in this run) and **Completed** (every challenge with a record, below; cleared
  clean in the game's yellow), each under a framed title with its count ("Completed 5/13"). Left /
  Right moves between them; the list not in use is dimmed. The info panel below gives the
  highlighted challenge: on Active its level (a run-locking one in yellow) and what it does, on
  Completed its clears, best level, fastest time, Hardcore and first clear. The title bar gives the
  run (Clean / Assisted, battles won, knockouts, time), then the controls; Circle or Cross goes
  back to the System list. The game's handler reads System rows 4 and 5 as a jump to bonus points,
  so Cross on our row is taken before it. Run history stays in `pe_challenges.ini` and F1's This
  run page. Game text uses only what the game's font has (letters, digits, `. , : ! ? ' / + - %`).
  Challenges are still chosen in F1.
- **Run history** (`pe_ch_history.c`): `pe_challenges.ini` beside the preferences, one line per run
  (`<id> ko= st=playing|cleared|fallen on= ever= drop= fl= b= t= lv= at=`; the older
  `<id> <knockouts> [dead]` lines still read). It is per user, like `pe_collection.ini`: knockouts
  and deaths survive the reload that follows them, and nothing goes into the card, so the save
  editor, save states and companion files are unchanged. Every real save (card menu or quick save:
  the block is `g_MemCardFileBuffer`; the crash reporter's room snapshot is skipped) refreshes the
  run's line; the save after the credits (Clear Data: `g_SaveTitleStyleFlag` with the card menu's
  clear prompt) marks it cleared (`pe_ch_milestones.c`). 128 runs; playing runs are dropped first,
  fallen ones last.
- **Hardcore saves in the save / load lists** (`pe_ch_card_marks.c`; the save phone's card screen and
  the title's Load screen are the same menu): a Hardcore save's slot is drawn in a dark red window,
  a fallen one in grey (only the menu's copy of the slot's window colour, `slot->blendColor`, is
  changed; the card never is). With one highlighted, the title bar says "Hardcore run" and its other
  challenges, or "Fallen Hardcore run: it can't be played"; when saving, that the slot holds a
  Hardcore run. Each slot's file is read once (the game's `Psyz_AdjustPath` name) and its
  `pe_challenges.run` record peeked read-only (`pe_save_peek`), again only when the slot changes.
- **Records** (`pe_ch_records.c`): a challenge is completed when a run is cleared (the Clear Data
  save after the credits) with it on at the end and never lowered. Per challenge: clean and
  assisted clears, the hardest level cleared, the fastest time, whether it was cleared in a
  Hardcore run, the first clear's date. Kept per user in `pe_challenges_records.ini` (one line per
  challenge id). It is fed by listening to `challenges.run_cleared`, like any other mod could be;
  the first time the file is missing, cleared runs already in the history are counted once. Shown
  in the Completed list of Aya's Challenges screen (cleared clean in yellow) and as
  "Challenges cleared N of M" in F1.
- **This run (F1):** status, challenges on / used / lowered, cheats, battles won, knockouts and time.
- **Hardcore (permadeath):** chosen at the title for a New Game only, and its run locks every
  challenge on (more can be added, none lowered; Clear all refuses). The game's own game over (phase
  3 with no Preraise and no Revive, `Battle_StepPostBattle`) marks the run fallen in the history.
  Loading any save of a fallen run (card slot, quick save, copy) opens the game's own dialogue
  window ("This Hardcore run has ended. It fell after N battles, time." / Return to title;
  `pe_ch_notice.c`), then the title through the game's own `Boot_Shutdown` on the first frame a soft
  reset is allowed. Saves are never deleted or moved. The load isn't refused up front because the
  load admission code lives in host files the PS5 port pins; a separate Hardcore card would need the
  pinned save flow too.

## A mod

A folder with `.c` files, one of which defines its descriptor:

```c
const PeMod pe_mod_<folder> = {"<folder>", "Readable name", "0.1", requires, init, shutdown};
```

Versions: the mod API is `PE_MOD_API_VERSION` (pe_mod_host.h, now 1), raised only when a change
would break existing mods; the PE Enhancements suite is `PE_ENHANCEMENTS_VERSION` (pe_pages.h,
now 0.10.0), one version for all of its mods.

`init` registers the mod's rows, hooks and services. Each feature lives in its own file with
one `pe_<feature>_register(mod_id)` function that the mod's main file calls, so a feature can be
moved, disabled or updated without touching the others. Add the folder name to `PE_MOD_LIST`;
`pe_hello` is the smallest complete example (rows, a hook, a service, an event, its own top page).

## Talking to the game

- **Hooks** (`pe_hook_add`): one line in the game at a function entry or a fixed moment, with a
  context struct (`include/pe_hooks.h` lists them). Handlers run in priority order; a handler
  that changes a value sees the previous one's result. Add a hook only where a mod needs one,
  and keep it a single line.
- **Game state**: use the game's own globals and structs (`pe_game.h` has the common ones),
  never PS1 addresses: native structs hold host pointers and their own layouts.
- **Shared game facts live once, in the host** (`pe_game.h`): `pe_free_field()` (no battle, menu,
  transition or event), `pe_save_loaded()` (also mid room load, when Aya's actor is rebuilt),
  `pe_room_number()`, `pe_wayne_count()` / `pe_set_wayne_count()`, `pe_mod_effect_name()` /
  `pe_mod_effect_group()`, `pe_power_tag()`. Never copy one of these into a mod.

## Talking to other mods

- **Services**: `pe_service_publish("junk", 1, &api, MOD_ID)` / `pe_service_get("junk", 1)`. A service is
  a versioned struct of functions; a mod that finds none carries on without it.
- **Events**: `pe_event_id("name")` and `pe_event_emit(id, ctx)` for announcements.

Service definitions live in `host/pe_services.h`, never in a mod's folder: `junk` v1 and
`catalog` and `rebuild` v1 (pe_catalog), `keys` v1 (pe_qol), `bestiary` v1 (pe_collection), `wardrobe` and
`costumes` v1 (pe_wardrobe).

The shared registry (host/pe_registry.h; bridge findings D6 Â§11) is for mods' definitions that
other mods build on: custom content, equipment effects, Catalog entries and shop offers. During
its init a mod declares batches (`pe_reg_batch`) and submits entries (`pe_reg_submit`) under its
own namespace, "<mod id>/<name>"; it gets a receipt, not a definition. Once every mod has started
the host seals the registry: limits, keys declared twice (every claimant fails, nothing wins),
references and batch requirements, to a fixed point, the same whatever order mods start in. Then
`pe_reg_find`, `pe_reg_count` / `pe_reg_key` (canonical order) and `pe_reg_batch_result` (with the
reason) answer; nothing new is accepted until the next start. No mod uses it yet.
`tests/pe_mod_registry_test.c` runs the D6 Â§11.6 examples. Events: `play.log` (every pe_log row), `save.committed` (the host: a card save was published), `bestiary.unlock`, `wardrobe.worn`, `phone.call_start` / `phone.call_end`.
A mod that registers into another mod's service at init lists that mod in its `requires`.

## The Mods panel

- F1 always opens on the list of mods: each mod's top page (`pe_menu_begin("My Mod")`, described
  and ordered with `pe_menu_top_page`), then the host's Manage mods. The PE Enhancements mods share
  one top page, laid out by topic.
- PE Enhancements rows go on the pages in `host/pe_pages.h` (Movement, Combat, Economy, Inventory,
  Collection, Debug); tuning and test rows always go under Debug.
- Values the player sets: `toggle`, `choice`, `int` rows (saved in `pe_mods.ini` by page and
  label: renaming a row resets it). Values read from the game: `live_*` rows (not saved).
- Read-only values (Junk, prices, results) are `info` rows, never live rows that refuse changes.
- Every row has a one-line description; actions that cost or destroy something are press-twice.

Movement speed uses the local `PLAYER_MOVE` hook in the original input movement
handlers. It never writes the actor's `moveFactor`: cutscenes and battle-entry
scripts own that value, including values that match a mod speed option.
Always Run changes a local input mask, never the game's mapping table. Battle-start
speed scales the local script speed before its destination clamp, and the local
animation increment; it never writes the actor's velocity or `animStep`.
These options keep no per-actor ownership state to restore after a scene or save load.
## Saves

Per-save data goes through the host, the same way for every mod (`host/pe_save.h`): register each
piece of state once in your init, and the host saves, loads and carries it. Never write into the card
block yourself.

```c
static struct { uint16_t count, pad, ids[32]; } s_keys;
static const PeSaveRecord s_record = { "pe_qol.keys", 1, &s_keys, sizeof s_keys, check, upgrade, reset };
pe_save_register(&s_record);
```

- **Save:** the host copies every running mod's registered state into the card block's free tail
  (0x1C90..0x1FFF, inside the game's CRC) as named, versioned records, after every mod's own
  `PE_HOOK_SAVE_WRITE` handler.
- **Load:** the records go back into each mod's memory before any mod's `PE_HOOK_SAVE_READ` handler
  runs. `check()` can fix up or refuse what was loaded; a refused or missing record gets `reset()`
  (zeros without one).
- **Layouts:** raise the version when the layout changes and read older ones in `upgrade()`. A record
  newer than the build can read is kept as it is, so playing on an older build doesn't destroy it.
  The same goes for the whole tail when its container format is newer: every save writes it back
  unchanged and the mods start from their defaults. Write the state as a struct with a
  `_Static_assert` on its size, so a changed layout can't ship without a new version.
- **Mods off:** a record whose mod is off, failed to start or isn't in the build carries from the
  loaded save into every later save unchanged.
- **New game:** `PE_HOOK_NEW_GAME` (`PeHookNewGame`: the title's New Game or Tutorial, or the EX Game).
  The host resets every record and drops the carried ones before any mod hears it, so a record
  needs no handler of its own; reset anything else per game there. Don't test for script command
  1117: only the EX Game sends it, and a New Game from the title after a loaded save sends nothing.
- **Crash reports:** `pe_save_crash_tail()` copies the same records from memory without calling any
  mod code, so a save rebuilt from a report holds every mod's data. The room snapshot the crash
  report takes when a room starts uses it, and the report lists its records.
- **Older builds:** tail bytes 0x200..0x287 stay the old PKEY (key pocket) and SORT blocks, which
  pe_qol still writes itself, because the official releases read and write them there. The records
  run around those bytes and their CRC leaves them out, so a save made in an official release keeps
  every mod's data and its pocket, and the pocket it saved wins when the save comes back here.
- **Damage:** each record carries its own CRC. If another build writes over part of the records,
  only the records it hit reset; the rest load. The header also records who wrote the save (mod API
  version and the host's save writer revision), so later builds can tell older saves apart.
- **Room:** 744 bytes (the tail's 880 less those 136; 20 for the header, 12 per record). A record
  that doesn't fit is refused when it registers, with a line in `pe_mods.log`.

Records today:

| Record | Holds | Owner |
|---|---|---|
| `pe_economy.loot` | Junk, rare pity, difficulty (the rewards' fields through the `wallet` service, D10) | pe_economy/pe_loot.c |
| `pe_economy.catalog` | Mod Catalog unlocks, Rebuild list (the name from before the split) | pe_catalog/pe_catalog.c |
| `pe_economy.shop` | Requisition (the name from before the split) | pe_shops/pe_requisition.c |
| `pe_qol.keys` | key item pocket | pe_qol/pe_key_items.c |
| `pe_qol.sort` | bag sort | pe_qol/pe_bag_sort.c |
| `pe_phone.heard` | the last five calls and one-time calls heard | pe_phone/pe_phone_calls.c |
| `pe_challenges.run` | challenges and levels on / used / lowered, assisted, battles won, time | pe_challenges/pe_ch_run.c |

Saves from earlier builds kept these as fixed blocks (`PELT` +0x000, `PECT` +0x040, `PESH` +0x1C0, `PKEY` +0x200, `SORT`
+0x260 from 0x1C90).
They still load: each block reaches its owner's `upgrade()` as version 0, and the next save writes
records. pe_qol still writes both blocks in the window too, so builds without records keep reading
them. Checks: tests/run_pe_save_test.ps1.

## Optional encounter controls

No random battles (Combat) reads the game's own saved switch. Turning it off
clears only that bit; loading another save or starting a new game clears the
session override. A save made with the switch on retains it.

Each feature has its own file and registration function in pe_qol.

## Half-bar turns

Combat / Parasite Energy includes the recomp's half-bar ATB rules. Rebalanced
allows Items, Quick PE powers and weapon changes at half a bar; armor changes
and escape remain full-bar by default. Quick powers default to Heal 1/2,
Detox, Medic, Scan, Slow and Confuse. On a half bar every other power shows
dimmed in the PE menu, like a power Aya can't afford (PE_HOOK_POWER_AVAILABLE),
so Energy Shot and the rest need a full bar. Cancelling refunds the borrowed
gauge; attacks still pay a full bar through refill debt. Vanilla disables
half-bar actions. The gold gauge indicator uses native HUD quads.

Runtime turn accounting lives in pe_rebalance/pe_atb_items.c; settings remain
on the PE rules page. No card-save blocks are required.

Regression coverage is retained in tests/pe_mod_atb_test.c. It links only the
half-bar and PE-balance feature sources against native headers, with test-owned
game state and mod-host callbacks, to exercise turn accounting and rule presets.

## Battle frequency

Combat / Battle frequency ports the recomp's Normal, More (2x), Lots (4x),
and Every check choices. Normal is the default. It changes the random-roll
range only for the encounter script sequence; unrelated RNG is unchanged.
Every check still respects story gates, room encounter rules and rooms with
no encounters. No random battles overrides it. This increases fight frequency,
not the number of enemies in a fight.

pe_qol/pe_battle_frequency.c owns matching and settings. A single SCRIPT_RANDOM
hook supplies local RNG bounds; loaded scripts and save data are not patched.

## Panacea and medicine combining

Panacea (item 0x19) restores 400 HP and cures ailments in battle or the field,
while preserving ailment immunities. It uses Medicine 4's icon and healing
effect. The Parasite Energy rules page enables it in Rebalanced and disables
it in Vanilla; names remain readable for already-carried Panacea.

Combat / Combine items provides the recomp recipes through the shared F1
panel: two Medicine 1/2/3 become the next tier, or Medicine 4 plus Full Cure
become Panacea. Junk economy must be enabled, both ingredients must be in
Aya's bag, and combining is outside battle and game menus. Press Combine
twice; the bag is checked again before consuming anything.

In Aya's item menu a medicine whose partner is in the bag gets a fourth
action, Combine, under Use / Move / Discard; it combines at once and closes
the list (Combine items / Combine in item menu). Items with an available Combine
recipe remain white in Aya's bag even at full health; Use keeps its native
availability and dimming. The Combine module owns this presentation through
PE_HOOK_INVENTORY_ROW without changing selection bits. Checks:
tests/run_pe_combine_colour_test.ps1. It uses PE_HOOK_ITEM_ACTIONS:
the action list just opened, whose row count, row drawer and panel input step
a mod may replace. Text helpers for the game's glyph code are in
host/pe_gametext.h.

pe_rebalance/pe_panacea.c owns effects, labels and use flags; pe_combine.c
owns recipes and confirmation. They share only their feature header and
use the versioned Junk service for the optional economy dependency. Existing
item IDs use the normal game save; no new save block is needed.

## Smoke Grenades

Combat / Smoke Grenades sells the recomp's escape item for 15 Junk before
difficulty/Bargain pricing. Purchase in the field with a free bag slot; press
Buy twice. Junk economy must be enabled to buy, but carried grenades remain
usable when it is off. The item is battle-only, guarantees an ordinary escape,
and is disabled when any living active enemy forbids escape. A blocked item
that reaches resolution is returned to the bag slot freed by its consumption.

pe_economy/pe_smoke.c owns the effects, labels, availability and purchase rows.
It uses the existing ITEM_USE and TEXT_LOOKUP hooks. Normal inventory saves
carry item 0x18; no new hook or save block is needed.

## Inventory convenience

Inventory / Key item pocket moves story keys and Chrysler elevator keys out
of Aya's bag. Inventory / Key items browses them. Story count/removal hooks
query the pocket directly, including with a full bag; no equipment or items
are parked elsewhere. Turning it off returns keys as slots become free, while
remaining pocketed keys still work for story checks. Key pickups and full-bag
found-list rewards also use the pocket. The keys v1 service remains compatible
with pe_economy's loot handling.

Bag sort starts Manual for a new game. Use the game's Sort screen to choose
an order and enable Auto, or choose Auto in F1 / Inventory. Bag changes are
sorted only in free field; manually rearranging the same items in the game
menu switches back to Manual. The native sorter preserves equipped slots.
Square on Aya's Use Item list opens the native Sort screen. Sort is hidden
from the main menu; Wayne's storage is untouched by automatic sorting.
pe_qol/pe_inventory_menu.c owns the shortcut and transient row hiding, separate
from sorting policy. It restores the original menu mask on save and menu exit.
Inventory tips (on by default) say so: the title bar on the main menu's Items
row and on an empty bag slot, and a message the first time the bag opens each
session, including whether Auto sort is on.

Key Items in Aya's main menu (pe_qol/pe_key_menu.c, while the pocket is on): a row with the
Dressing Room Key's icon opens a view-only list of the pocket, each key with its icon and name and
the highlighted key's game description in the title bar. Circle or Cross goes back.

Extra main-menu rows and screens are a host service (host/pe_main_menu.h): a mod adds a row
(title, icon item or its own icon drawer, shown test, open callback, order) and builds its screen
with pe_screen_open_game (flush where the game's own screens sit) / pe_screen_rows /
pe_screen_close, which also hide Aya's status panels and keep scrolling inside the list. The host
owns the main-menu hooks (LIST_ROWS, MAIN_MENU_ICON, HELP_TEXT for the row names,
MAIN_MENU_ACTION). Order: Mod Catalog 10, Key Items 20, Bestiary 25, Wardrobe 30. An icon drawer
may draw the icon itself (pe_icon_sprite, or pe_icon_part for part of a sprite at any size) and
set the hook's sprite to -1 so the game draws nothing more; pe_confirm asks Yes / No on the game's
own dialog.

pe_qol/pe_key_items.c and pe_bag_sort.c each own their state, hooks and rows.
pe_inventory.h holds private save/field helpers. PKEY and SORT use the exact
recomp v1 card layouts and checksums. SORT stores original table identifiers,
which map explicitly to native tables; no host pointers are serialized.

Run the inventory regression checks on Windows with
`tests/run_pe_inventory_test.ps1` after configuring build-mods. The test
links both feature files and the real native inventory sorter; unrelated
game paths are isolated by explicit test stubs.

Inventory Junk counter (pe_economy/pe_junk_counter.c): enabled by default;
F1 / PE Enhancements / Inventory / Junk counter saves its visibility in pe_mods.ini.
Draws the live economy balance in the free box left of Quantity, following the
native header when bag height or scrolling changes (on an odd bag's last page the
header moves up beside the last item; the counter keeps the row it left). Hidden when Junk economy
is off. The native header provides one draw hook; all presentation stays in
the feature file. It never changes inventory capacity, balances or save data.

Custom armor effect display (pe_economy/pe_armor_effect_display.c) replaces
retail sprite lookup only for armor effects 17-20: BRG (Bargain), SCV
(Scavenger), LCK (Lucky Find), SLV (Salvager). Badges fit the 36-pixel
effect cells; selecting a badge shows the full name in the help bar.
Original armor and all weapon effects retain native icons. One replaceable
equipment-effect draw hook keeps presentation in the economy feature file.
Checks: tests/run_pe_armor_effect_display_test.ps1.

## Requisition

F1 / PE Enhancements / Economy / Requisition offers Medicine 1-4, Cure-P/D/C/M,
Full Cure, Bullets x15/x30, Rockets, Smoke Grenades, Revive and Tools. The four
economy effects are sold as direct Catalog unlocks and leave the list once
unlocked; they require no bag slot. Tools disappear if either Tool Kit is in
Aya's bag or Wayne's storage. The same shop is in Wayne's (and Warner's) own
dialogue menu, under Workshop.

Goods use recomp prices (Medicine 3 = 35, Medicine 4 = 80). Tools start at 40,
increase by 10 per purchase and cap at 100 after difficulty/Bargain pricing.
Revive costs 300 on the first playthrough or 500 in EX Game, before those
adjustments. Tuning lives under Debug / Loot tuning. The Tool purchase count
uses the exact recomp PESH v1 save block, checksum and offset; loading an old
save without that block or starting a new game resets the count.

Buy requires two presses with the same stable goods identity, price and bag.
Returning to gameplay or loading/changing scenes cancels confirmation.
Purchase rechecks the balance, bag space, current unlocks and ammo capacity.
Ammo merges into native shared pools; it needs a new bag slot only when its
pool is not already carried. Purchases are restricted to the free field with
Junk economy enabled, outside battles, cutscenes and native game menus.

Rescue matches the recomp: if no weapon in Aya's bag or Wayne's storage can
attack (melee, loaded rounds, or ammunition in its matching pool), Bullets x15
are free and Spare Club can issue a free Club 1 after confirmation. Spare Club
requires both a bag slot and a free equipment record. A field reminder points
to this page once per episode; no usable weapons are replaced.

pe_requisition.c owns rules, native inventory access, tuning and persistence;
pe_requisition_menu.c owns the shared F1 page, confirmation and rescue reminder.
pe_requisition.h exposes stable goods IDs for future menu clients. No native
engine hooks were added. Deferred custom-content ideas remain unimplemented.

Run tests/run_pe_requisition_test.ps1 for focused purchase, ammo, unlock,
Tool-price, rescue/storage, confirmation, guard and save-layout checks.

## Wayne's Workshop

pe_economy/pe_wayne_menu.c rewrites Wayne's own choice box (PE_HOOK_TEXTBOXES, before the
boxes draw): Workshop above Cancel, Junk and his count on Discard junk.
Workshop opens a menu screen of its own (pe_shops/pe_workshop_screen.c), his box waiting below
the screen: Requisition, Rebuild (the rebuilt item's stats as the equip screens show them), a
Wardrobe row while a mod publishes the `wardrobe` v1 service (outfits beside their model), and
Mod Permit Card (pe_permit.c: the bag's guns and armor; one permit adds one mod slot). Lists
scroll; gray rows can't be had now and say why; every purchase asks Yes / No. Circle goes back.
While his box is the counter the shop allows buying though the field is busy
(pe_shop_at_counter). Messages use pe_gametext.h.

host/pe_equip_sync.c (a host feature, so it works whichever mods are loaded) reruns the game's
equipment rebuild in free field whenever the equipped gun or armor record changes, so Catalog,
Tool, Rebuild and editor changes reach the next battle.

## Import (pe_import, pe_src_<game>, host/import)

F1 > Import brings models from the player's other PlayStation games into Parasite Eve, in the game
itself (PC and PS5, no outside tools). The player only provides the discs: disc images (.cue with
its .bin, or .iso) in `<preferences>/import/discs/`, loose or one folder deep, or in folders of
their own added with F1 > Import > Add a folder (the system's folder picker; kept one per line in
`<preferences>/import/folders.txt`, which is edited by hand where there is no picker, as on PS5).

**Automatic.** When the game starts, a worker thread (`pe_import_job.c`) reads new or changed disc
images and builds everything the source games mark automatic (`pe_imp_add_auto`; RE1: Chris and Jill;
RE2: Claire from disc 2, Leon from disc 1; RE3: Jill and Carlos; Dino Crisis: Regina) unless its pack already holds a build of the same source file by the
same builder. Back on the main thread the Wardrobe adds the new costumes at once (the
`wardrobe_packs` service, `pe_packs_add_new`: only new packs and characters; registered costumes
stay put), with a message. F1 > Import > Automatic import says what's ready and which discs to add;
Scan again reruns it.

**By hand** (F1 > Import > Build by hand), for anything else:

- **Scanning**: each image a source game recognises is read once into the store,
  `<preferences>/import/store/<game>/<sha256 of the file>/`, and its catalog
  (`<game>/catalog.json`: stable IDs such as `re2:player/claire`). Files already read by the same
  reader version are skipped.
- **Model** lists what a builder target can make (today: costumes, from models a rig fits), and
  **Build as a costume** writes it as a pack in `<preferences>/packs/<game>-local/` (format 1, the
  same pack id and folder names as tools/re2-skin, so keys such as `re2-local/claire.re2` match).
  The Wardrobe loads it at the next start.

**Sounds (PC).** F1 > Import > Sounds lists waveforms from scanned copies of Resident Evil
Director's Cut, Resident Evil 2, Resident Evil 3, Dino Crisis, and Final Fantasy VII. Choose
a source game, browse the pages, select a waveform, and choose **Preview sound**. These are
raw sample auditions at a fixed reference pitch. Dino Crisis (US) includes Gian and VAB
sample banks embedded in DAT archives. The shared asset and audio services also
work with the Import UI disabled. See [the mod API](../docs/MOD_API.md#imported-sound-previews).

**Music (PC).** F1 > Import > Music lists source songs after scanning a supported disc:
RE2 (`COMMON/SOUND/*.BGM`: a sequence or two, the bank and samples), RE1 Director's Cut
(`SOUND/SEPxx.HSB`: a bank and a package of sequences, one Song each, 94 on the US disc) and
Dino Crisis (`PSX/DATA_XA/STAGE00.XAS`: seven streamed CD-XA tracks; the other XA files are
speech) and RE3 (`SOUND/MAINxx.BGM` with its `.VB`, and the room archives' tracks: 46, the 169
rooms sharing 42, one catalog entry each; its compact banks are rebuilt as standard VABs) and FF7
(the AKAO songs inside the LZS-compressed `FIELD/*.DAT` files and `ENEMY6/*.SND`, 92 across the
three discs, one entry per song number; they play on the game's own instrument bank, `INSTR.DAT`
and `INSTR.ALL`, cataloged once as `ff7:bank/instr`). Square's AKAO sequences play through
`host/import/pe_imp_akao.c` (first-generation AKAO: notes, ties, rests, octaves and the
instruments' pitch tables, envelopes, loops and jumps, tempo, drum kits; reverb, LFOs and
overlay voices aren't modelled). `mod-akao` tests it on made-up songs and on FF7 CD1 with
`PE_TEST_FF7_DISC` (`PE_TEST_AKAO_WAV`). Choose a track and **Play track**: it plays from the game's own
sequence and instruments, looping until **Stop**. `host/import/pe_imp_seq.c` renders a PS1
sequence (SEQ) on its bank (VAB) to 44.1 kHz stereo: programs and key-split tones, the SPU's
ADSR envelope, pitch bend, volume, pan and expression, and the sequence's loop marks; reverb
isn't modelled. Plain C, so the same renderer can prepare tracks offline for in-game use later;
on PC the audio service (`play_song`, `stop_song`, `song_playing`) pulls it from an SDL stream.
`mod-seq` tests it on made-up songs, on RE2's MAIN00 with `PE_TEST_RE2_DISC`
(`PE_TEST_SEQ_WAV` writes what it rendered to a WAV file) and on RE1's 94 with
`PE_TEST_RE1_DISC`. Streamed tracks play through `host/import/pe_imp_xa.c` (CD-XA ADPCM,
resampled to 44.1 kHz); readers set `xa` to get a file's audio sectors (`pe_imp_disc_xa`,
raw `.bin` images only). `mod-xa` tests it on made-up sectors and on Dino Crisis's tracks with
`PE_TEST_DC1_DISC` (`PE_TEST_XA_WAV`).

**Rooms.** F1 > Import > Rooms lists the room backgrounds after scanning a Resident Evil
(Director's Cut, RE2, or RE3) PS1 disc. Each ImageSet holds 320 × 240 MDEC frames in their
original BSS slot order, plus the matching camera and any foreground data. The importer pairs
RE1 and RE2 backgrounds with RDT files and extracts the RE3 RDT from the room's ARD.
RE1 supplies the foreground atlas as a TIM in the RDT. RE2 and RE3 store it compressed in
the BSS slot. The importer decodes both backgrounds and atlases on demand.

**Export pictures** writes the selected room's background BMPs, transparent `*_mask.bmp`
foreground layers, and one `*_cameras.json` file to `<preferences>/import/rooms/<game>/`.
**Export every room** writes these for the whole game. Camera JSON preserves the source slot,
eye and target (source units, Y down), raw projection word, and per-tile atlas position,
screen position, dimensions, and raw depth. Some discs have extra BSS pictures without a matching
camera record. Those backgrounds still export. Discs already scanned are picked up by the next
import scan. Collision and in-game room placement are separate from this export.

`mod-mdec` tests the legacy store and checks backgrounds, camera pairing, and foreground pixels
with `PE_TEST_RE1_DISC`, `PE_TEST_RE2_DISC`, and `PE_TEST_RE3_DISC`.
Set `PE_TEST_ROOMS_DIR` to write background previews during that test.

**Room logic (RE1, RE2, RE3).** `host/import/pe_imp_rdt.c` reads what happens in a room from its RDT:
the doors (zone, where each leads, starting camera, door animation and sound, lock), the trigger
zones, item pickups and enemy placements declared by the room's scripts, and the camera switch
zones. It walks the scripts with the games' instruction sizes (from IntelOrca's biohazard-utils,
MIT) and leaves the bytecode as it is for a later interpreter. `mod-rdt` checks it on a made-up
rooms and damaged copies, and with `PE_TEST_RE1_DISC` / `PE_TEST_RE2_DISC` / `PE_TEST_RE3_DISC`
reads every room: RE1 Director's Cut has 335 room files (Chris's and Jill's), 551 procedures (none
unreadable) and 782 doors; RE2 disc 1 1,940 procedures (none unreadable) and 283 doors; RE3 4,238
procedures and 453 doors. RE1 has its own script format (length-prefixed procedures, its own
instruction set; its event scripts aren't walked yet). `PE_TEST_RDT_ROOM=ROOM1060` prints a room's
doors. Not used by the game yet.

**RoomLogic: the contract for the room runtime.** A scan now also stores each RE room file as a
**RoomLogic** record (`host/import/pe_imp_logic.h`), cataloged as `<game>:logic/<stage><room>`
plus `-<player>` for RE1 and RE2, where Chris/Jill and Leon/Claire have their own scripts (for
example `re2:logic/105-0`). It holds game-neutral records (doors, trigger zones, item pickups,
camera zones, enemy placements as their source instruction) and keeps the room's collision,
messages and scripts as they are, for that game's interpreter. The future room runtime reads only
these records through `pe_imp_catalog_roomlogic`, never a disc. The file is sectioned, so sections
can be added later without breaking a reader. The readers are `pe_src_re{1,2,3}_logic.c`.
Each record also keeps the whole room file (`RDT0`, offsets intact: sound bank, lights, floors,
objects and their textures, effects, animations), so the runtime never needs the disc again; RE3's
also keeps its room archive's sound entries (`ARDS`). RE1 and RE2 rooms' sound banks are cataloged
as Sounds too (bank `room<stage><room>`; 1,285 on RE2 disc 1). RE3's are too (1,609 in 153 rooms):
its room file holds a compact bank header (after 0xFF padding) and the archive entry of the size it
names holds the samples; the header is rebuilt as a standard VAB with the same code as RE3's music
(`re3_bank_size`, `re3_bank_rebuild`). The kept files take about what they
take on the disc: about 123 MB for RE1 (Chris's and Jill's rooms are separate files), 22 MB for RE2,
21 MB for RE3.

**Room animations (RE1, RE2, RE3).** Each room's RBJ (RDT section 22: per animation a flags word, an EMR of
key frames, an EDD of moves; layout from biohazard-utils `Rbj.cs`, `Edd1.cs`, `Emr.cs`) becomes an
Animation `roomanim/<stage><room>-<n>` at 30 frames a second (`pe_src_re2/pe_src_re2_rbj.c`; 114 on
disc 1, mostly 15-bone people). RE1 keeps one set per room file in sections 9 (EMR) and 10 (EDD),
the same layout with plain s16 angles (6 bytes a bone): one Animation `roomanim/<stage><room>-<player>`
per file (`pe_src_re1/pe_src_re1_anims.c`; 334 of 335, Chris's and Jill's separate). RE3's room files
have no animation section; its room moves are in the room's enemy bundle, `CD_DATA/STAGEs/Rsrr.BIN`
(see the enemies above), in the entries tagged 0x80 and up: u32 directory offset, u32 count, the
directory's u32 section offsets. A section holding a flags word and an EMR (15 bones for a player, 7
or 9 for a prop) followed by a section holding its EDD (8-byte moves, as in RE3's EMD) becomes an
Animation `roomanim/<stage><room>-<entry>-<n>` (`g_re3_room_anims_ps1`; 114 on the disc, 1,478
moves). EMRs with no EDD after them (key frames only, likely played straight through by the room
script; 83) aren't read.

**Room objects (RE2).** The 3D props an RE2 room places (typewriters, boxes, floor items, swinging
doors) become one-bone Models, `object/<stage><room>-<n>` (`pe_src_re2/pe_src_re2_objects.c`; 522 on
disc 1, in 106 rooms). A room file's section 10 lists them as (texture, mesh) offsets; each mesh is
a one-part MD1 with an 8-bit TIM, read by the same code as RE2's player models (`re2_object_parse`,
sharing `mesh_and_texture` with `re2_pld_parse`; the costume tests are unchanged). Where an object
stands is set by the room's script. RE3's room objects are too (`pe_src_re3/pe_src_re3_objects.c`;
695 of 712, in 143 rooms): the same section-10 table; each mesh starts with a small header whose
first u16 is the offset of an MD2 block (the player models' mesh format), read as one part per MD2
object. The rest of that header is an EMR skeleton (tree, bone offsets); when it names one bone per
MD2 object (663 of 712) the model gets those bones, piece i on bone i, so a prop whose pieces move
can be animated; otherwise it is one bone. RE1's room props and floor items are
too (`pe_src_re1/pe_src_re1_objects.c`, `object/...` and `item/...`; 589, Chris's and Jill's rooms
merged): sections 2 and 3 list (TMD, TIM) pairs; the TMD's textured triangles and quads are read
(modes 0x34/0x36, 0x24, 0x3C), the rare untextured and unlit ones passed over.
A scan now offers each file to every reader that matches it, not just the first; a second reader
of a file keeps its own store folder with `path_key`. `mod-rdt` saves and loads a record, refuses a
damaged one, builds one for every room on the discs (about 0.8 MB for RE1, 1.3 MB for RE2, 1.7 MB
for RE3), and scans RE2 into a store and reads room 105's doors back through the catalog.

**Door transitions.** Each RE door file becomes a **DoorScene** record (`host/import/pe_imp_door.c`,
same sectioned container as RoomLogic, `doorscene.bin`), cataloged as `door/<name>` (RE3:
`door/<stage>-<name>`, as RE3 keeps doors per stage). RE2's `COMMON/DOOR/DOORnn.DO2` holds a sound
bank (`VAB0`; its samples start 8 bytes after the bank header) and the 3D door scene (`SCN0`:
animation, model, texture); its door sounds are also cataloged as Sounds (bank `door<nn>`, 151 on
disc 1). RE1's `ITEM_M1/DOOR*.DOR` (21) and RE3's `STAGEs/DOOR*.DO1` (76) are kept whole as `SCN0`
until their layouts are worked out. Playing the 3D door scene is the room runtime's job. RE1's door
model is also a Model, `doormodel/<name>` (21): a `.DOR` starts with three u32 offsets (animation,
TMD model of the door and its frame, 8-bit TIM); the TMD's objects become parts (the TMD reader now
takes several objects). Its "animation" block is a door script too (36 u32 offsets to short command
records, the same bytes in every `.DOR`), kept as stored with the rest of the file. RE2's door models are too (`doormodel/<nn>`, 55): the scene block starts
with u32 offsets of the door's MD1 mesh and its TIM, read like a room object. Door animations aren't
keyframes: RE2's scene holds a small door script (a table of offsets, then bytecode that moves the
camera and turns the door), kept as stored for the runtime to interpret. RE3's door models are Models
too (`doormodel/<stage>-<nn>`, 76): after the compact sound bank, a scene at a 2 KB boundary starts
with u32 offsets of the door's MD2 mesh and its 8-bit TIM, read like an RE3 room object. RE1's event scripts are kept as stored
(`SCD2`): they're state machines with nested actor streams, decoded by the runtime's interpreter.

**Items.** Each RE game's item table is an **ItemTable** record (`host/import/pe_imp_items.c`,
`itemtable.bin`, same container), cataloged as `<game>:items/all` and built when the game's icon
file is scanned (RE1 `DATA/ITEM_ALL.PIX`, RE2 `COMMON/DATA/ITEMALL.PIX`, RE3 `CD_DATA/ETC/ITEMI.PIX`).
`NAME` holds each item number with its name (from biohazard-utils' lists, MIT; RE1 75, RE2 124, RE3
127) and its icon where known. `ICN8` keeps the 40 x 30 icons as stored, 8 bits per pixel (RE1 76,
RE2 106, RE3 228). Not yet known: the icons' palette (not stored plainly on the discs), and which
icon each RE2 and RE3 item uses (RE1's icon k is item k + 1; item 0 is "nothing"). The key pocket
names RE keys from this table.

**Item pictures in colour.** Pictures records, picture i for item i: RE2 `COMMON/DATA/ITPS.ITP` (100)
and RE3 `CD_DATA/ETC/ITEMG.PIX` (134) hold the inventory's 112 x 72 item pictures, a TIM every 12 KB /
10 KB, each with its own palette (`items/pictures`; 1 is the knife, 2 the handgun, 7 the shotgun).
RE1's 40 x 30 icons take their colours from row 2 of `DATA/STATUS.TIM`'s palette, the status screen's
(found by eye; rows 0 and 1 are wrong): `items/icons`, icon k for item k + 1 (`pe_imp_emit_pix8`).
RE2's map pages are still grey: no fixed palette was found, and the map screen recolours rooms by
state (visited, current), so the colours are probably set by the game's code.

**Movies.** Each RE STR movie (RE1 `ZMOVIE`, RE2 `ZMOVIE` and the player's `PL0`/`PL1` `ZMOVIE`, RE3 `CD_DATA/ZMOVIE`) is
copied once into the store as a **Movie** (`host/import/pe_imp_movie.c`, `movie.bin`): a frame
index, its MDEC frames (320 x 240 in RE1, 320 x 160 letterboxed in RE2 and RE3; decoded by
`pe_imp_mdec.c` like the room backgrounds) and its CD-XA sound sectors as stored. That is about
1 GB for all the discs (RE1 296 MB, RE2 330 MB a disc, RE3 120 MB). The sound needs a raw `.bin`
image; an `.iso` has none. A reader can ask for a file and its XA sectors together
(`xa = PE_IMP_XA_BOTH`), and one file may now be up to 128 MB (RE2's longest movie is 92 MB).
`mod-rdt` reads a short movie from each disc, saves and loads it, and decodes a frame.

**Files (readable documents).** The file pictures are kept as stored in **Pictures** records
(`host/import/pe_imp_pictures.c`, `pictures.bin`, the sectioned container; one or more PS1 TIMs, with
a TIM decoder to RGBA for 4-, 8- and 15-bit TIMs): RE1 `ITEM_M2/FILE*.TIM` as `files/<name>` (19),
RE2 `COMMON/DATA/FILES.TIM` as `files/all` (216 TIMs), RE3 `CD_DATA/ETC/FILEGU.PIX` as `files/all`
(183). Each document is an 8-bit background followed by its 4-bit text pages (a page may follow the
background directly, off the 2 KB grid), so the record also has a DOCS section: per document its
first picture and its picture count (RE2 25 documents, in the game's file order, the 25 file names
in biohazard-utils' `Bio2ConstantTable.cs`; RE3 31). RE1's file text, `ITEM_M2/FILEM*.PIX` (a 64-byte palette, then
4-bit pixels 512 wide), is cut into 512 x 192 pages and kept as 4-bit TIMs with that palette
(`files/filem_00` and so on; 92 pages in 46 files). The
container's cap is now 16 MB. `mod-rdt` scans each disc's files and decodes every picture.

**Maps.** Also Pictures, as `maps/all`. RE2's `COMMON/DATA/MAPS.PIX` is 20 map pages of 256 x 256 at
4 bits a pixel with no palette on the disc: each is wrapped as a 4-bit TIM with a placeholder palette
of 16 greys, and the record is marked `GREY`. RE3's `CD_DATA/ETC/MAP_U.MAP` holds a section table,
the room and label tables and 18 TIMs (9 maps and their legends, in their own colours): the TIMs,
and the whole file as `FILE` for the tables. RE1's are `ITEM_M2/MAP00.TIM` to `MAP0E.TIM`, one 8-bit
TIM each in its own colours, one record each as `maps/<name>` (`maps/map01` and so on; 15): MAP01 to
MAP0B the floor maps (256 x 112), MAP00 and MAP0C to MAP0E the area overviews (256 x 160) the map
screen shows as more of the game opens up. `mod-rdt` decodes every map picture.

Other games' files are read only then, when the player asks, never during play. Aya's data comes
from the PE discs the game has open (`g_CdImageVolumes`, CUE/BIN or CHD).

- `host/import/`: the core. `pe_imp.h` (disc images, the Model type, readers, rigs, targets, store,
  catalog, packs), `pe_imp_pe.h` (PE's model and row formats, retargeting, the rows rooms install),
  `pe_imp_rig.h` (a rig profile). Plain C with stdio; its own JSON reader (no cJSON on PS5).
- `pe_src_<game>/`: one mod per source game registers its readers, naming table and rigs. Nothing
  in the core names a game. RE1's people are rigid like RE2's, in another bone order; its reader
  puts them in RE2's order (`pe_src_re1_emd.c`). RE3's people are skinned (an arm is one mesh in the root's frame, a
  table says which bone moves each vertex); its reader splits them back into RE2's 15 bone-local
  parts, so the fit is RE2's (`pe_src_re3_pld.c` has the layout). Dino Crisis packs Regina into
  compressed archives (`PSX/DATA/CORE0x.DAT`, one per outfit) with a skinned vertex pool and a
  tiled texture; `pe_src_dc1_core.c` documents what was worked out from the disc. A game whose
  disc shares another's file layout names a file that tells them apart (`pe_imp_add_disc_id`:
  Dino Crisis 2 has the same CORE names).
- `pe_wardrobe/pe_skin_build.c` + `pe_skin_target.c`: the skins target, a port of tools/re2-skin
  (PR #4). Every file is byte for byte the Python's except texture.bin (our colour reduction) and
  angles at gimbal lock, where the Python read rounding noise (`pe_imp_to_yxz`).
- Tests: `mod-import` (made-up disc images and models, broken files, the store; real disc with
  `PE_TEST_RE2_DISC`), `mod-re1`, `mod-re3` and `mod-dc1` (made-up data each; the real people with
  `PE_TEST_RE1_DISC`, `PE_TEST_RE3_DISC`, `PE_TEST_DC1_DISC`),
  `mod-skin-build` and `mod-import-flow` (need `PE_TEST_DISC` and
  `PE_TEST_RE2_DISC`; `PE_TEST_SKIN_REFERENCE` compares with tools/re2-skin's output).

## Collection (pe_collection) and Wardrobe (pe_wardrobe)

F1 > PE Enhancements > Collection and Wardrobe. Cosmetic and information only; Junk comes from the
economy's `junk` service. Two mods since November 2026 (bridge findings D8 Â§12, D9): the
Collection is the Bestiary, the Wardrobe the costumes; neither needs the other. Each is a
registry: its creatures or costumes come from packs, and Parasite Eve's own are just the first
pack (`pe_collection/pe_pack_pe1.c`, `pe_wardrobe/pe_pack_pe1_costumes.c`). Other mods (an import
from another game, a randomizer) add theirs through the `bestiary` and `costumes` services
(host/pe_services.h) without touching these files.

In pe_collection/:

- `pe_bestiary.c`: the creature registry. A kill (enemy HP above 0 to 0 or less, watched on
  PE_HOOK_BATTLE_UPDATE) or Scan (PE_HOOK_SPELL power 7) is credited to the creature the enemy
  resolves to: mods' resolvers first (newest first), then the mapped (room, identity) table.
  5 kills or Scan show weaknesses, 15 kills the loot (bosses: defeat shows weaknesses, loot is Junk
  only); each tier can be bought. Announces "bestiary.unlock".
  Scripted bosses can end their fight at the original million-HP offset rather
  than HP zero. Their defeat is deferred until Aya leaves battle alive, with
  phase refills cancelling the pending defeat and attached parts deduplicated.
  Completed hits are observed before scripts can release their actor. The stage
  boss is displayed as **Eve (stage)**; its legacy `pe1/actress` profile key stays valid.
  The native PE pack resolves `(room, rank/kind, model bank slot)` through its resolver:
  rank/kind alone is shared by different species in the same room. Its 371 actor keys retain
  all 331 previous room/identity/species associations, add the stage Eve encounter and retain all 75 stable profile keys. HP,
  defense and other mutable combat stats are not used as species identifiers. If EX/progression
  changes rank, a unique room/model association still resolves the same species. A unique
  legacy mapping remains available when the actor is detached; ambiguous identities never
  guess the last registered species. Later import/randomizer resolvers still take precedence.
- `pe_bestiary_menu.c`: a Bestiary row in Aya's main menu (icon: the B of the BP icon) once a creature is
  met: the area's creatures (L1 / R1 change area; unmet ones "???"), the model turning with its idle
  animation in a game panel (host pe_model_stage; Left / Right turn it, Triangle full screen, Square
  pause, Select the next form), and what is known by tier below. The browser sits in the game's
  screen area (right of the icon rail, under the title bar), widened to the status screen's right
  edge. Details wrap to the panel width and native drawing budget;
  L2 / R2 scroll the information independently of the creature list. Changing creatures or
  unlocking a tier returns to the first detail line. Unmet creatures of a reached area
  show as a silhouette; others and bosses show nothing. Cross buys the next tier after the game's
  Yes / No (host `pe_confirm`). NEW: tiers not looked at yet (profile `seen`) list in the game's
  yellow. Debug > Collection: Junk, Give 1000 Junk, Test creature (its tiers, kills, NEW), Unlock
  next tier, Add a kill, Reset creature, Mark creature NEW, Clear / Make all NEW.
  `tests/run_pe_bestiary_menu_test.ps1` checks the layout, wrapping without lost text (also when the
  overflowing glyph is a space), scrolling, the title bar's Info page only when it scrolls, translated
  glyph boundaries, dense text drawing limits, viewer controls and teardown.
In pe_wardrobe/:
- `pe_wardrobe.c`: the costume registry. A costume is one of the game's player bundles (bank 10 /
  12 / 13, rewritten at the start of Aya's model load, PE_HOOK_PLAYER_BANK), a skin folder
  (`bundle`, loaded by pe_skin_loader.c), or a provider's own model (its load(1) / load(0) at that
  moment). The room reloads when a costume needs it. Costumes that can't be worn now (files
  missing) are hidden. Publishes `wardrobe` v1 for shops (Wayne's Workshop); announces
  "wardrobe.worn". F1 groups and Bestiary areas come from whatever packs register.
- `pe_skin_loader.c`: skins in the game's own player formats, from pe_skins/<folder>/ beside the
  preferences (texture.bin, model.bin, day1-8.bin, optional rooms.bin and meta.bin; the bridge's
  build_skin.py makes them from the player's disc). Loaded character models use native jacket
  Aya's movement-cylinder radius (63 for the supported US assets), including character packs.
  Only the host-owned model copy is normalized: installed assets, appearance, animations and
  per-part combat hit bounds remain unchanged. This prevents NPC body widths from changing
  movement clearance, as with Baker getting stuck near the police save-room area. Existing rig
  defects are separate. Restart to reload any cached models after updating the mod.
  Kept in host memory: Aya's textures are swapped
  at the game's upload (PE_HOOK_PLAYER_TEXTURES, the game's own kept for changing back), and
  bank_slots[0] and her rows point at the skin before her actor takes them (PE_HOOK_PLAYER_ROWS) and
  every frame; the weapon follows the skin's hand, room animations use retargeted copies. A row
  the skin can't play (another bone count, no retargeted copy) gets the skin's idle pose held for
  its length ("idle pose held" in the log), and the mirror rooms' reflection actor draws the worn
  skin instead of the room's own Aya mesh. Instant
  swap (Wardrobe, experimental, off) changes skins in place without a room reload. A game outfit
  may carry a room pack (rooms.bin alone: pe_skins/outfit_evening, outfit_tshirt, made by the
  bridge's build_outfit_rooms.py): rooms install Aya's animations for the outfit the story has her
  in (29, 31 or 23 parts), so the evening dress worn on a jacket day gets them retargeted to its own.
  A room row with no copy (no pack installed, or none for that row) is fitted to the outfit's part
  count instead: missing bones hold the outfit's idle pose, extra bones are dropped. Otherwise the
  undecoded gown parts compound into a flickering, stretched shape in cutscenes (issue #220).
  When rebuilding packs for Day 1 long-dress scenes, the generator must accept the shortest
  23-part row (300 bytes, not the jacket-only minimum of 372). In the bridge generator, check
  `FP_BYTES` and invalidate `tools/pe_assets/skins/room_rows.pkl` before rebuilding; changing the
  threshold alone keeps the old cached set. The generator must also collect the bank 11 bundle
  rows 28â€“34 (image entry 19), not only rows installed by room archives: none of those seven
  fingerprints occurs in the room-only scan of either US disc. Updated `rooms.bin` copies are required as well as
  the loader fix: updating the game executable does not repair existing skin assets.
- `pe_wardrobe_menu.c`: a Wardrobe row in Aya's main menu (icon: the Equip Armor icon, twice) once a
  costume is owned. Uses the Bestiary's six-row list, right-hand model preview and three-row details
  panel (outfit name, wear status/action, and preview control; no cast/category detail row). L1/R1 filter owned outfits by registered group; Left/Right rotate, Triangle opens/closes
  full view, Square (default keyboard V) pauses. Previews start facing the viewer. Both Wardrobe
  and Bestiary use `pe_screen_preview_input` to pass Left/Right through the native list to rotation. Cross wears the selected outfit when the menu closes; Circle returns.
  There are no purchases, prices or unlock actions here. Missing outfits stay out of the list, and
  so do unowned ones while unlock rules apply (below).
  Names are fitted to the native font and capped to leave room for the title and side rail in the
  16 KB menu drawing buffer. Groups remember their scroll position while the screen is open.
  Preview loading is separate from equipment: `host/pe_model_skin.c` reads standalone skin blobs
  or Aya's original disc bundles into private model/texture memory, using player idle row 0x15.
  It never calls a costume's gameplay `load` callback. Callback-only costumes without standalone
  assets remain wearable with a Preview unavailable message. Presenter slot 3 is reserved for this
  screen (Bestiary uses 2). `tests/run_pe_wardrobe_menu_test.ps1` covers controls, scrolling, long
  names, the drawing budget, owned-only choices and cleanup; the model fixture covers player idle,
  skin source caching and missing files. Set `PE_MODEL_SKIN_ROOT` to the preference directory when
  running `tests/run_pe_model_test.ps1` to also decode installed Harlequin, Baker and Maya assets.
- Unlock rules: a mod publishing `costume_rules` v1 (pe_economy's F1 > Economy > Costume unlock
  rules, on by default) restricts wearing to owned or free costumes, bought with Junk. With the
  rule off, or no such mod running, every installed costume can be worn and nothing is bought or
  written as owned. What Aya wears follows a rule change at the next room load only; a costume the
  rule locks comes off there with its key kept (bridge findings D8 Â§12).
- `pe_packs.c`: character packs (bridge findings D8 Â§3, format v1) dropped in
  `<preferences>/packs/` as a plain zip or the same layout unzipped: `pack.json` (`format` 1, `id`
  of a-z / 0-9 / -, at most 31, `kind` "assets"), `LICENSE`, and `characters/<folder>/` with
  `character.json` (`name`, `group`, `tier`: extra, outfit, supporting, main or special) and the
  skin loader's files. Read at start-up only. Keys are `<pack id>/<folder>` (at most 63 bytes,
  never renamed); the bundle `@<pack id>/<folder>` sends the skin loader to the pack. Refused, with
  the reason on F1 > Collection > Wardrobe > Packs: an id installed twice (every copy), a bad or
  reserved id (`pe1`, `local`), no LICENSE, another format or kind (recipe packs need the
  builder), and zips with unsafe or duplicate names, encryption, links, files over 4 MiB, over
  64 MiB unpacked or suspicious compression. A character missing a file, or not shaped like Aya
  (host `pe_model_check_skin`, the bridge's check_skins_struct.py: at least 29 parts, draw commands
  visiting each part once in order, every animation row one channel set per part, meta.bin's gun
  part a part), is refused alone. Tiers price 25 / 50 / 100 / 250 / 400 Junk while unlock rules
  apply. Previews in Aya's Wardrobe menu read the pack through `pe_model_skin_set_reader`, set by
  the Wardrobe (the host never calls it by name). Recipe packs (Parasite Eve's own characters,
  shared as instructions) are listed as needing a build: the bridge's
  tools/pe_assets/pe_pack_build.py builds them from the player's disc into an assets pack here.
  Models from other games are converted on the player's computer: `tools/re2-skin/` builds an
  assets pack from a Resident Evil 2 PC player model (BioRand's `claire.re2/PL01.PLD`).
  Not yet: default-appearance replacement. `tests/pe_mod_packs_test.c` writes real zips and folders; the model fixture covers
  the preview reader and the shape check's refusals.
- `pe_wardrobe_profile.c`: costumes owned and the one worn, by key, in pe_wardrobe.ini beside the
  preferences, kept across every game; records of packs that aren't loaded are kept untouched. The
  first time the file is missing, it imports the costume lines of pe_collection.ini (or, with none,
  the recomp's pe_collection.dat) and records the source on an `imported` line. The Collection
  drops its copies once it sees that line, so a lost pe_wardrobe.ini can't bring back costumes
  cleared since.
- `pe_pack_pe1_costumes.c`: the outfits and the 38 character skins (pe_wardrobe_data.h, generated
  by the bridge; each hidden while its pe_skins folder is missing).
- `pe_wardrobe_mod.c`: the mod, Junk, messages; Debug > Collection > Reset wardrobe resets its
  profile. A player who had turned the Collection off in Manage mods starts with the Wardrobe off
  too (host `pe_menu_alias_setting`).

Back in pe_collection/:
- `pe_profile.c`: Bestiary unlocks by key in pe_collection.ini beside the preferences, kept across
  every game; records of packs that aren't loaded are kept untouched. The recomp's
  pe_collection.dat is imported once. Costume lines from before the split are carried until the
  Wardrobe has imported them; Reset collection never clears them.
- `pe_pack_pe1.c`: the 75 creatures (pe_collection_data.h, generated by the bridge).
- `pe_collection.c`: the mod, Junk, messages; Debug > Collection resets the Bestiary.

Writing a pack (another mod):
1. List "pe_collection" (creatures) and / or "pe_wardrobe" (costumes) in your PeMod `requires`,
   so the services exist at your init.
2. `const PeBestiaryApi *b = pe_service_get("bestiary", 1);` then `b->add(&info)` for each creature
   (static `PeCreatureInfo`, a key under your own prefix: "re2/zombie") and `b->map(room, identity,
   handle)` for where it appears, or `b->add_resolver(fn)` to decide per enemy (a randomizer that
   shuffles enemies). `b->replace(handle, &info)` swaps in new info under the same key (randomized
   drops or weaknesses). `b->set_model(handle, form, &src)` says where its 3D model comes from for
   the Bestiary viewer (form 0 the main body, then parts or phases): a room and bank slot on the
   Parasite Eve disc, a `pe_skins` folder, or your own decoder (`PE_MODEL_CALLBACK`). The PE pack
   gives all 75 creatures their forms from the generated table (`g_pe_creature_models`).
3. `const PeCostumesApi *c = pe_service_get("costumes", 1);` then `c->add(&costume)`: `bank` for one
   of the game's outfits, `bank = -1` with `bundle` for a skin folder in the game's player formats
   (the Wardrobe loads it), or `bank = -1` with `load` (and `available`) for a model your mod puts
   in itself; add the hooks your loader needs.
4. Keys are saved in players' profiles: never rename one once released.
`tests/run_pe_collection_test.ps1` drives the registries the way such a pack would.

## Mod Catalog files (mods/pe_catalog)

Its own mod since November 2026, "PE Enhancements: Mod Catalog". It reaches the Junk wallet only
through the `junk` service (`pe_catalog_wallet.c`): without the economy mod running, paid actions
are refused, never free. It publishes `catalog` (unlocks) and `rebuild` (the Rebuild list, for
Wayne's Workshop). A player who had the economy off starts with it off too.


- `pe_catalog.c`: effects, prices, install / remove rules, unlocks, the PECT save block, the
  `catalog` service.
- `pe_dismantle.c`: Dismantle, Rebuild and Tool salvage.
- `pe_catalog_pages.c`: the F1 pages. `pe_catalog_menu.c`: the Catalog in Aya's own menu.
- `pe_catalog.h`: their private interface; the mod's other files use the `pe_cat_*` calls in pe_catalog_mod.h.
- `pe_discard_dismantle.c`: Discard on a gun or armor in Aya's menu or Wayne's storage reads
  Dismantle and confirms with what it pays (PE_HOOK_ITEM_ACTIONS); the equipped gear and the last
  Club keep the plain Discard. F1 > Economy > Discard dismantles.

## Warner's services

pe_economy/pe_field_wayne.c: wherever Warner stands guard (map 374) he offers Store equipment,
Store item and the Workshop. His retail talk task (checked word for word, read-only) plays
untouched; when it ends, the field is locked again the way his script does it (field event 0xFE)
and our menu opens in a dialogue box of its own near the screen centre (PE_HOOK_TEXTBOXES, ahead
of pe_wayne_menu.c, which adds Workshop). Closing it unlocks the field (0xFF);
a storage choice then opens the game's storage screen. Nothing in his script is patched. F1 >
Economy > Warner helps out.

## Tests

`tests/run_pe_*_test.ps1` (Windows, after configuring build-mods) build and run each mod's checks
against the production files, with the game and host stubbed where they don't matter.

Debug / Collection has Unlock all costumes and Lock all costumes testing
actions. They save ownership changes immediately for all registered costume
keys, including costumes with missing assets; asset availability is still
required to wear them. Lock returns a registered worn costume to story
clothes. Free outfits remain available, and Bestiary progress, Junk and
records from unloaded packs are preserved. Ownership handling stays in
pe_wardrobe.c and pe_profile.c; Collection regression checks cover both actions.

Debug / Collection also provides Unlock all Bestiary and Reset Bestiary.
Unlock reveals basics, weaknesses and loot for every registered creature
without inventing kills or spending Junk. Reset clears those discoveries
and kill counts and discards old enemy HP tracking. Both save immediately;
costume ownership, the worn outfit, Junk and unloaded packs' records remain
unchanged. The actions live in pe_bestiary.c and use the existing profile
API. Collection checks cover persistence, repeat use and post-reset kills.

## Extension APIs

The native-compatible v1 services, ownership rules, next-save purchase behavior and supported limits are documented in [the extension API guide](host/EXTENSION_APIS.md). For the whole mod API, start with [the API reference](../docs/MOD_API.md). The existing gameplay defaults are unchanged.
