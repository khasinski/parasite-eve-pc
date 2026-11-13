# BP UI prototype

## Real BP presets (r13)

F1 > PE Enhancements > Bonus Points starts with Current BP preset, Choose BP
preset, Changes, Other changes, Inactive investments and Apply BP preset.
Presets: Vanilla + QoL, Enhanced, Resistance Only, Mastery Only and Custom.
Advanced edits Resistance, Fortitude, Mastery and paid respec independently
in the same draft. Guide selection is independent and persists separately.

Apply is synchronous, free-field-only and confirmed by the existing F1
question renderer with No selected. Back/Escape cancels. Drafts are live
rows, never persisted. Only confirmed feature flags are bound to the host's
atomic profile settings writer. Existing profiles default to the previous
all-enabled behavior; loading a preset page never auto-applies anything.

Feature flags are profile-owned. Ranks and BP investment remain save-owned
in the unchanged pe_bp/ranks and pe_bp/run encodings. Disabling features
pauses their battle effects and purchases, retains ranks, and never moves
BP. Re-enabling restores effects without another charge. An explicit refund
removes eligible ranks and returns BP once. Equipment bonuses never enter
this suspension/refund process. Vanilla + QoL keeps vanilla purchases and
paid respec; the daily free economy-off respec remains unfinished.

Settings tests cover draft isolation, No/unavailable confirmation, busy
contexts, idempotency, Custom matching, guide independence and repeated
toggles. Rule tests cover disabled purchases, inactive rank displays, stale
request refusal and refunds of retained investments. The native lifecycle
test uses real card files and companion saves across separate processes.

The existing --test-save-after helper now uses the native companion-safe
publish/commit boundary, as the real save menu does, so native save tests
can actually verify persistent extension records.


## Full PE Enhancements suite (r11)

The explicit BP build now uses the current upstream default mod list plus
pe_bp through PE_EXTRA_MODS. Stale three-module caches are cleared by the
explicit injection file; upstream defaults stay in mods/CMakeLists.txt. The
build includes QoL, Rebalance, Economy, Rewards, Catalog, Shops, Collection,
Wardrobe, Stacks, Warp, Quicksave, Cheats, Phone, Language and BP.

The BP module registers its own F1 page at PE Enhancements > Bonus Points
through PE_PAGE_BP. It describes the real upgrade/refund entry points and
owns persistent, advice-only Playstyle guide selection. Independent rule
presets remain unimplemented and are labelled unavailable on the real page.
The unfinished preset previews and screen-opening tools live at
PE Enhancements > Debug > Bonus Points; the old standalone BP UI Preview
top-level page is removed. No category's balance settings are auto-applied.

The upstream fetch/fast-forward check found origin/main already included
at cb73f9aeb. The previous missing features came from the minimal build's
PE_MOD_LIST, not missing source commits.

Native suite tests audit all 15 running modules, existing Movement/Combat/
Economy/Inventory/Collection rows, 7 BP-owned canonical rows, and 6 debug
rows. Controller tests derive the classic BP icon's row from the live native
menu mask rather than assume it is the last item when other mods add rows.


## Direct entry points (r10)

Aya's existing BP icon now opens the Aya upgrade tab directly. No extra
BP Upgrades row is registered. The four upgrade tabs share one screen; the
live catalog has no combined upgrade/refund home page. Closing restores
focus to the same classic BP icon. When the live BP module is disabled, the
classic BP icon retains vanilla behavior.

Wayne > Workshop > Reset BP opens the Aya refund tab directly. The refund
tabs contain Reset all eligible, which opens the full-reset breakdown and
returns to the selected refund tab. There is no intermediate combined menu.
The refund handler is also used by the explicit F1 debug action. F1 sample
previews and test-only fixture catalogs remain separate from normal entry.

Validation: test_entry_routes.mjs (upgrade/refund/off) loads the Day 3 save,
checks direct entry, tab boundaries, full-reset Back, focus restoration and
unchanged character/inventory. Its refund case invokes the shared Wayne
handler through the F1 test adapter, not physical dialogue navigation.
Graph checks in tests_live.c prevent upgrade roots reaching refund pages
and refund roots reaching purchase pages. test_live.mjs now tests purchases
through the classic BP icon; refund transaction rules remain covered by
the live backend unit suite.


The approved upgrade layout, now connected to live game data (r5). Without the
pe_bp mod (or with All mods Off) it falls back to the sample catalog below,
whose confirmations never spend anything. Respec staging remains a layout
example.

## EX Game carryover and repo test saves

- F1 > PE Enhancements > Bonus Points > "EX Game carryover": Off / Keep purchased skills (default)
  / Convert skills to BP. Unspent BP always carries (the game keeps it). Keep tags ranks as carried
  (`pe_bp/carry`); their refunds pay into the upgrade-only pool. Convert puts the ranks' BP value in
  that pool. `bp_ex_carry()` (bp_record.c) is the rule; `on_new_game` in bp_store.c applies it.
- The in-game tests load the repo's sample saves (`save-editor/samples/bu00`) from card 1 through
  `test_saves.mjs` (slot list there). `test_ex_carry.mjs` fires the game's own EX start command
  (`PE_BP_TEST_EX=1`); `test_battle.mjs` needs a build with pe_qol for fast encounters.
- Test scripts must drain the game's stderr: an unread pipe fills and stalls the game.

## Public `bp` service and module split

- `mods/pe_bp` is split by job (table in `mods/pe_bp/README.md`): pure rules in `bp_defs.c`,
  `bp_record.c`, `bp_quote.c`, `bp_txn.c`; the service in `bp_api.c`; the game side in
  `bp_store.c` (sections, load, New Game, EX carry) and `bp_game.c` (snapshot, writes).
- Other mods use the public `bp` v1 service, declared in `mods/host/pe_services.h`: status, ranks
  and ranks in effect, keys/names/prices, quote, execute, upgrade-only grants, per-mod pauses,
  and the `bp.changed` / `bp.effect` events. These screens use `bp_screens` v1
  (`pe_bp_screens.h`, the full snapshot); both commit through `bp_api_execute`, so screen
  purchases are announced too.
- Tests: `tests_api.c` (ctest `bp-public-api`, fake game) and `test_api.mjs` (`PE_BP_TEST_API=1`,
  sample B: the service as another mod sees it in the real game).

## Vanilla look (r7)

`BpUiRow.visual` (`BpUiVisual`, provider-filled numbers) lets gear rows draw
with the game's own pieces: `Menu_DrawSlotRow` (bag row), `Draw_DropShadow`
(equipped band), stat banner sprites 0x7C/0x7F+stat, BASE/PLUS 0x87/0x88,
BONUS POINT 0x93, `Draw_PrintNumberWidth4/4Unk/SignedWidth4/5` and
`Draw_SetStatCompareColor`. r8: `BP_UI_VISUAL_AYA_STAT` draws Aya's entries 5-6
with banner 0x8C+stat, `Draw_PrintNumberWidth2` level and
`Draw_AllocTexturedRectAlt(distance, sub)`. The cursor is `Draw_AllocColorTriGradient(w,h,0,0)`
with default colours and undimmed: steady (no bevel blink, no fill pulse) per
the approved one-steady-highlight rule. `test_headless.mjs` now checks the
cursor's opaque two-pixel top bevel instead of the old grey fill.

## Live backend (r5, 2026-11-12)

- `mods/pe_bp/` is a real mod (preview build only; production mod list is
  unchanged). Pure rules (now `bp_defs.c`, `bp_record.c`, `bp_quote.c`):
  prices, quotes, fees, save encoding. `bp_txn.c`: executor - fresh snapshot,
  re-quote, then refusable steps (store enable, Junk spend) before infallible
  writes; any refusal changes nothing. Game side (now `bp_game.c`,
  `bp_store.c`): snapshot, the writes, companion sections `pe_bp/ranks`,
  `pe_bp/run` and `pe_bp/carry`; services `bp` (public) and `bp_screens`.
- `bp_catalog.c` builds this UI's catalog from a snapshot + quotes (pure; IDs
  interned so focus survives rebuilds). `bp_live_ui.c` wires it to the
  service; `bp_ui_answer`/settings commit through `BpUiBackend`, then the
  catalog is rebuilt and focus restored by ID (`bp_ui_reload`).
- Writes: `g_AyaBonusPoints`, one gear record's bonus, Aya stat entries 5-6
  only, the pe_bp sections.
- r6 battle effects (`mods/pe_bp/pe_bp_battle.c`) use two new hooks in
  `include/pe_hooks.h`: `PE_HOOK_ENEMY_EFFECT` (`enemy_actions.c`: stage 0 at
  entry, stage 1 after each ailment's armor roll) and `PE_HOOK_SPELL_DONE`
  (`attack.c`, before the HP clamp and PE drain). Also PE_HOOK_HP_LOSS (Fortitude
  soften), PE_HOOK_SPELL, BATTLE_UPDATE (Gene Heal, Preraise), ENEMY_HIT/_END
  (Energy Shot). Not mirrored into platform/ps5 patches. Not yet verified in a
  real battle (no headless battle driver in this build).
- Active Time refunds only steps bought on this screen (ledger in `pe_bp/run`):
  replaying Active Time from the save is wrong in ~60% of buy/level orders
  (irregular early thresholds). Item Capacity replays exactly (10-point
  steps, checked at runtime). Full reset refuses if the smaller bag would
  drop items.
- Not built: Junk-economy-off daily respec, EX Game carry, independent F1 rules presets.
- Tests: `tests_live.c` (ctest `bp-live-backend`); `test_live.mjs` drives
  the real game (`node test_live.mjs EXE DISC1 OUT PROFILE [slot]`,
  `PE_BP_UI_TEST_JUNK=n` grants test Junk).

## Approved composition

Four tabs: **Guns, Armor, Aya, PE Mastery**. Equipment pickers lead to stat
choices and retain the selected item on return. Aya contains non-selectable
**Character** and **Resistance** headings. Navigation skips headings and the
list scrolls when needed. Presets and guides remain **F1-only**.

One shared two-column shell puts choices on the left and comparisons on the
right. Cost and remaining currency stay pinned, including in full details.
Square toggles details, L1/R1 pages them, and Cancel closes details first.
Left/Right changes category and clears stale equipment navigation in that tab.
Each nonempty list has one steady highlight. Read-only panels have no cursors.

Wayne's original conversation and built-in Workshop services are preserved.
The explicit preview build adds Reset BP preview through a bounded service
registry. Its shared refund layout has Aya and PE Mastery tabs only. Weapons
and armor are excluded; Aya's two stats require the full reset. Refund amounts
remain sample values and confirmation is still a no-op.

## Modules

- bp_ui.h / bp_ui.c: stable IDs, tabs, sections, focus memory, modal gating,
  help and measured text pagination.
- BpUiPreview: supplied before/after totals, benefit, context, price and
  remaining currency. The renderer never calculates purchase effects.
- BpUiRow: selectable item or non-selectable section heading.
- BpUiPage: category identity and equipment/context subject, separate from rows.
- BpUiTab: ribbon label and category root. Larger ribbons scroll with selection.
- bp_ui_demo.c: all sample data and separate native/F1 catalogs.
- bp_ui_native.c: shared panels, real font metrics, adaptive viewport, pinned
  comparisons/costs and bounded per-panel drawing buffers.
- preview.html: offline browser approximation generated from the same data.

Tab groups keep upgrade and refund navigation separate while sharing the shell.
New equipment, powers, ranks and services are definitions rather than new
renderers. Definition strings must outlive the controller. Validation rejects
missing routes, duplicate IDs, unknown tabs, interactive headings and capacity
overflows. The current navigation/cache budget is 32 pages. A future read-only
provider must supply final effective totals and quotes, not renderer formulas.

## Viewing the compiled game

Run the packaged Launch-UI-Preview.cmd. Aya's classic BP icon opens the
upgrade screen directly. F1 > BP UI Preview provides BP/Workshop opening actions and direct
rules/guide choices. For opening actions, close F1 and open Aya's menu.
Access works with All mods Off. The launcher uses a private test profile.

## Standalone checks

~~~powershell
cmake -S prototypes/bp-ui -B build/bp-ui-verified -G Ninja -DCMAKE_C_COMPILER=clang
cmake --build build/bp-ui-verified
ctest --test-dir build/bp-ui-verified --output-on-failure
node prototypes/bp-ui/build_preview.mjs build/bp-ui-verified/bp-ui-export.exe
~~~

Tests cover section skipping, merged Aya, equipment focus, modal release gating,
every sample action, empty/long lists, fourth-rank growth, complete measured
wrapping, F1-only settings and no-op transactions.

## Real-game checks

headless_inject.cmake explicitly adds the preview to the test executable. The
production root build and mod list remain unchanged. Clang/MinGW is the tested
Windows toolchain; linker wrappers need a GNU-compatible linker.

~~~powershell
./prototypes/bp-ui/build_headless.ps1
node prototypes/bp-ui/test_headless.mjs build/bp-ui-headless/parasite-eve-ui-preview.exe "PATH/TO/DISC1.cue" build/bp-ui-verified/bp-ui-export.exe build/bp-ui-evidence preview
node prototypes/bp-ui/capture_png.mjs build/bp-ui-evidence/preview
~~~

For an empty submodule, pass -PsyzCheckout with an existing PsyZ checkout.
-DependencyCache points to existing CMake _deps sources for offline builds.
The helper removes only its temporary dependency link after building.

The driver boots the original game and uses its real pad decoder to visit every
native screen and selectable row, including scrolling, help and confirmations.
It checks highlight counts and actual background pixels, font widths and unchanged character/inventory
state. test_entry_routes.mjs tests normal entry and vanilla behavior with All mods Off.
PE_BP_UI_F1_PRESETS_TEST=1 checks the direct F1 rules/guide choices.
Rendered PPMs, logs and proof.json are saved; PNG conversion is lossless.

Checks establish preview behavior with sample data, not live rules, persistence,
live refunds or every language/graphics/input configuration.

## Test saves

import_test_saves.mjs converts the existing PS1 save archives into 8192-byte
native files on memory card 2 of the private preview profile. It validates the
card directory/checksum and save header, selects free slots and never overwrites
an existing save. Original ZIPs remain unchanged. The packaged profile includes
Day 3, Day 4, Day 5, Day 2, Day 1 (Alligator), and Day 6 in slots 1-6.

The preview build includes pe_economy and pe_shops to preserve the real Workshop
route. All mods must be On to see Workshop in Wayne's conversation. F1 debug previews still work with All mods Off; the classic BP icon uses vanilla behavior.

The r4 private profile starts with All mods On for the real Wayne route; the
main-menu shortcut was also tested with All mods Off. Native UI regression:
19 pages, 286 captures, no text/highlight findings. Workshop registry fixture
checks original ordering, duplicate IDs, dynamic availability and routing.

test_save_load.mjs verifies Continue -> card 2 -> save 1 using the original
card/file UI and checks the loaded Day 3 level-14 character.
