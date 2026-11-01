# Mods

Mods for the native Parasite Eve port, built only with `-DPE_MODS=ON`. With it off the game is
the unchanged original: the `PE_HOOK(...)` lines in the game compile to nothing.

```
cmake -S . -B build-mods -G Ninja -DPE_MODS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-mods
```

`PE_MOD_LIST` (CMake cache) picks the mods, by folder name. Default:
`pe_qol;pe_rebalance;pe_economy;pe_collection;pe_stacks;pe_warp;pe_quicksave;pe_cheats` (add `pe_hello` for the example).

Two switches, so a modded build is safe to hand out:
- **Build**: `PE_MODS` off (the default) leaves no mod code in the game at all.
- **Runtime**: F1 > Manage mods > **All mods**, **off by default**. Until a player turns it on and
  restarts, no mod starts and nothing but the F1 panel touches the game. Below it, one toggle per
  mod; a mod turned off doesn't start, nor do the mods that need it. Changes apply at the next start.

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
  pe_economy/          Junk and loot, Mod Catalog / Dismantle / Rebuild, Requisition, Wayne and Warner
  pe_collection/       Bestiary and Wardrobe registries, skins, Wardrobe menu (packs register here)
  pe_stacks/           stacked consumables (up to 99 per bag or storage slot), from the Pathogen Eve port
  pe_warp/             warp menu to story checkpoints, from the Pathogen Eve port
  pe_quicksave/        10 quick save slots in the game's own save format (F10 / F11 / F12)
  pe_cheats/           god mode, infinite ammo and PE, instant win, all PE powers
  pe_lang/             language packs: the game's text from a .pelang pack, F1 > Language (not built
                       by default; docs/LOCALIZATION.md, tools in localization/)
  pe_hello/            example mod (not built by default)
```

## Stacked items (pe_stacks)

F1 > PE Enhancements > Inventory > Stacked items (on by default). Lists keep plain item ids; the
extra copies of a slot are counted per (list, item) in the mod. Hooks: ITEM_ADD (join a carried
stack), ITEM_REMOVE (use / give / discard takes one copy), ITEM_COUNT and INV_CMD_COUNT_ITEM
(story checks see every copy), STORAGE_TRANSFER (a bag stack goes into storage whole; storage
gives one copy), INVENTORY_REBUILD (duplicates fold into one slot), SLOT_DRAWN (the count in the
rounds column). Only base items of kinds 10 and 12..15 stack; Junk stays out while pe_economy's
Junk economy is on. Saved as the `pe_stacks.counts` record (204 bytes, up to 100 stacks). With the
mod or the option off, extra copies go back into free slots as plain items.

## Warp (pe_warp)

F1 > Warp: pick a story checkpoint and Warp. It sets script variable 74 (story progress) to that
point and runs the field script's own room transfer (Script_EncodeBarcode) on the next free field
frame, never during a battle, an event, a room change or a movie.

## Quick save (pe_quicksave)

F10 saves to the current slot, F11 loads it, F12 picks the next of 10 slots (also F1 > Quick save).
A quick save is the game's own 8 KB card file (Save_BuildCardFile, with every mod's records), made
in the free field and kept beside the preferences as quicksave_<slot>.mcd. Loading commits it the
way Save_LoadCardFileIntoRuntime does, without the card menu's dialogs, then runs the field
script's room transfer to the saved room: the room starts again as after a loaded save. It holds
what a save holds, not Aya's place in the room or a fight in progress.

## Cheats (pe_cheats)

F1 > Cheats: God mode, Infinite ammo and PE, Win this battle, and Learn all / Restore PE powers.
All off until chosen; while they are off the mod writes nothing.

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

- **Services**: `pe_service_publish("junk", 1, &api)` / `pe_service_get("junk", 1)`. A service is
  a versioned struct of functions; a mod that finds none carries on without it.
- **Events**: `pe_event_id("name")` and `pe_event_emit(id, ctx)` for announcements.

Service definitions live in `host/pe_services.h`, never in a mod's folder: `junk` v1 and
`catalog` v1 (pe_economy), `keys` v1 (pe_qol), `wardrobe`, `bestiary` and `costumes` v1
(pe_collection). Events: `play.log` (every pe_log row), `bestiary.unlock`, `wardrobe.worn`.
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
| `pe_economy.loot` | Junk, rare pity, difficulty | pe_economy/pe_loot.c |
| `pe_economy.catalog` | Mod Catalog unlocks, Rebuild list | pe_economy/pe_catalog.c |
| `pe_economy.shop` | Requisition | pe_economy/pe_requisition.c |
| `pe_qol.keys` | key item pocket | pe_qol/pe_key_items.c |
| `pe_qol.sort` | bag sort | pe_qol/pe_bag_sort.c |

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
boxes draw): Mod Permit Card and Workshop above Cancel, Junk and his count on Discard junk.
Workshop opens Requisition, Rebuild (5 a page, Left/Right pages, dimmed rows can't be bought
now) and a Wardrobe row while a mod publishes the `wardrobe` v1 service. Mod Permit Card adds
one mod slot after a Yes / No. Circle goes back. While his box is the counter the shop allows
buying though the field is busy (pe_shop_at_counter). Messages use pe_gametext.h.

host/pe_equip_sync.c (a host feature, so it works whichever mods are loaded) reruns the game's
equipment rebuild in free field whenever the equipped gun or armor record changes, so Catalog,
Tool, Rebuild and editor changes reach the next battle.

## Collection (pe_collection)

F1 > PE Enhancements > Collection. Cosmetic and information only; Junk comes from the economy's
`junk` service. The Collection is a registry: its creatures and costumes come from packs, and
Parasite Eve's own are just the first pack (`pe_pack_pe1.c`). Other mods (an import from another
game, a randomizer) add theirs through the `bestiary` and `costumes` services (host/pe_services.h)
without touching these files.

- `pe_bestiary.c`: the creature registry. A kill (enemy HP above 0 to 0 or less, watched on
  PE_HOOK_BATTLE_UPDATE) or Scan (PE_HOOK_SPELL power 7) is credited to the creature the enemy
  resolves to: mods' resolvers first (newest first), then the mapped (room, identity) table.
  5 kills or Scan show weaknesses, 15 kills the loot (bosses: defeat shows weaknesses, loot is Junk
  only); each tier can be bought. Announces "bestiary.unlock".
- `pe_bestiary_menu.c`: a Bestiary row in Aya's main menu (icon: the B of the BP icon) once a creature is
  met: the area's creatures (L1 / R1 change area; unmet ones "???"), the model turning with its idle
  animation in a game panel (host pe_model_stage; Left / Right turn it, Triangle full screen, Square
  pause, Select the next form), and what is known by tier below. Unmet creatures of a reached area
  show as a silhouette; others and bosses show nothing. Cross buys the next tier after the game's
  Yes / No (host `pe_confirm`). NEW: tiers not looked at yet (profile `seen`) list in the game's
  yellow. Debug > Collection: Junk, Give 1000 Junk, Test creature (its tiers, kills, NEW), Unlock
  next tier, Add a kill, Reset creature, Mark creature NEW, Clear / Make all NEW.
- `pe_wardrobe.c`: the costume registry. A costume is one of the game's player bundles (bank 10 /
  12 / 13, rewritten at the start of Aya's model load, PE_HOOK_PLAYER_BANK), a skin folder
  (`bundle`, loaded by pe_skin_loader.c), or a provider's own model (its load(1) / load(0) at that
  moment). The room reloads when a costume needs it. Costumes that can't be worn now (files
  missing) are hidden. Publishes `wardrobe` v1 for shops (Wayne's Workshop); announces
  "wardrobe.worn". F1 groups and Bestiary areas come from whatever packs register.
- `pe_skin_loader.c`: skins in the game's own player formats, from pe_skins/<folder>/ beside the
  preferences (texture.bin, model.bin, day1-8.bin, optional rooms.bin and meta.bin; the bridge's
  build_skin.py makes them from the player's disc). Kept in host memory: Aya's textures are swapped
  at the game's upload (PE_HOOK_PLAYER_TEXTURES, the game's own kept for changing back), and
  bank_slots[0] and her rows point at the skin before her actor takes them (PE_HOOK_PLAYER_ROWS) and
  every frame; the weapon follows the skin's hand, room animations use retargeted copies. Instant
  swap (Wardrobe, experimental, off) changes skins in place without a room reload. A game outfit
  may carry a room pack (rooms.bin alone: pe_skins/outfit_evening, outfit_tshirt, made by the
  bridge's build_outfit_rooms.py): rooms install Aya's animations for the outfit the story has her
  in (29, 31 or 23 parts), so the evening dress worn on a jacket day gets them retargeted to its own.
- `pe_wardrobe_menu.c`: a Wardrobe row in Aya's main menu (icon: the Equip Armor icon, twice) once a
  costume is owned. Uses the Bestiary's six-row list, right-hand model preview and three-row details
  panel (outfit name, wear status/action, and preview control; no cast/category detail row). L1/R1 filter owned outfits by registered group; Left/Right rotate, Triangle opens/closes
  full view, Square (default keyboard V) pauses. Previews start facing the viewer. Both Wardrobe
  and Bestiary use `pe_screen_preview_input` to pass Left/Right through the native list to rotation. Cross wears the selected outfit when the menu closes; Circle returns.
  There are no purchases, prices or unlock actions here. Missing/unowned outfits stay out of the list.
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
- `pe_profile.c`: unlocks by key in pe_collection.ini beside the preferences, kept across every
  game; records of packs that aren't loaded are kept untouched. The recomp's pe_collection.dat is
  imported once.
- `pe_pack_pe1.c`: the 75 creatures (pe_collection_data.h, generated by the bridge), the outfits
  and the 38 character skins (each hidden while its pe_skins folder is missing).
- `pe_collection.c`: the mod, Junk, messages; Debug > Collection resets the profile.

Writing a pack (another mod):
1. List "pe_collection" in your PeMod `requires`, so the services exist at your init.
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

## Mod Catalog files

- `pe_catalog.c`: effects, prices, install / remove rules, unlocks, the PECT save block, the
  `catalog` service.
- `pe_dismantle.c`: Dismantle, Rebuild and Tool salvage.
- `pe_catalog_pages.c`: the F1 pages. `pe_catalog_menu.c`: the Catalog in Aya's own menu.
- `pe_catalog.h`: their private interface; other files use the `pe_cat_*` calls in pe_economy.h.
- `pe_discard_dismantle.c`: Discard on a gun or armor in Aya's menu or Wayne's storage reads
  Dismantle and confirms with what it pays (PE_HOOK_ITEM_ACTIONS); the equipped gear and the last
  Club keep the plain Discard. F1 > Economy > Discard dismantles.

## Warner's services

pe_economy/pe_field_wayne.c: wherever Warner stands guard (map 374) he offers Store equipment,
Store item and the Workshop. His retail talk task (checked word for word, read-only) plays
untouched; when it ends, the field is locked again the way his script does it (field event 0xFE)
and our menu opens in a dialogue box of its own near the screen centre (PE_HOOK_TEXTBOXES, ahead
of pe_wayne_menu.c, which adds Mod Permit Card and Workshop). Closing it unlocks the field (0xFF);
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
