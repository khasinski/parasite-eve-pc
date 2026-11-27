# Save editor architecture

The editor is organized by responsibility. Keep UI layout, editing operations,
wire-format handling, and optional content definitions separate. Refactoring
retained the public `pe_save_editor::gui`, `cli` and `save` APIs, JSON schema,
command names, IDs and binary save layouts.

## Application entry points

- `src/main.rs`: command selection and the one-time optional definition-pack load.
- `src/lib.rs`: public module list. Library regressions live in `src/tests.rs`.
- `src/gui/mod.rs`: shared application state, tab/filter enums and GUI entry point.
- `src/cli/mod.rs`: CLI dispatcher and shared slot selection.
- `src/save/mod.rs`: `PeSaveSlot`, layout constants and the `EquipmentRecord` re-export.

The small module roots deliberately expose shared types/imports to their private
children through `use super::*`. Helpers needed by sibling modules are scoped
`pub(super)` rather than added to the public library API. They do not form a
plugin system or a new abstraction over the game.

## GUI: src/gui/

| File | Responsibility |
| --- | --- |
| `app.rs` | Initialization, loading/saving, icon refresh and frame orchestration |
| `chrome.rs` | Application menus and status bar |
| `slot_browser.rs` | Memory card slot sidebar |
| `workspace.rs` | Current-save summary, tab navigation and page dispatch |
| `theme.rs` | Colors and egui theme setup |
| `widgets.rs` | Shared cards, badges and saved-slot value presentation |
| `equipment.rs` | Shared equipment/stat/effect form and item picker |
| `inventory.rs` | Aya's bag and loadout screen |
| `armory.rs` | All equipment records screen |
| `wayne.rs` | Wayne storage screen |
| `story.rs` | Story flags, teleport and script inspector screen |
| `character.rs` | Character/stat form |
| `parasite_energy.rs` | Spell controls |
| `mod_data.rs` | Mod records, Junk, key pocket and sorting controls |
| `tests.rs` | Headless screen-rendering smoke test at two supported window sizes |

Keep panel order in `app.rs`: application bar, status bar, slot sidebar, empty
container/slot handling, workspace summary, then the active page. egui panel
ordering affects available space; rearranging it can change layout even when
the individual screens are untouched.

A page renders controls and calls model/schema operations. It must not invent
save offsets, compute checksums, load game mods, or duplicate content definitions.
Rendering without user input must preserve the raw mod tail.

## Save model: src/save/

| File | Responsibility |
| --- | --- |
| `mod.rs` | Save type and stable layout constants |
| `equipment.rs` | Equipment type, templates and exact 32-byte read/write logic |
| `serialization.rs` | Save parsing, inventory validation and block serialization |
| `hex_bytes_serde.rs` | Lossless raw block representation in JSON |
| `stats.rs` | Bag/HP synchronization and armor stat effects |
| `inventory.rs` | Item descriptions, equipment allocation, insertion and sorting |
| `progression.rs` | Spell flags and bounded script-byte access |
| `presentation.rs` | CRC status, display level/time, location, icons and effect text |
| `cheats.rs` | Explicit repair and cheat operations |

`to_bytes()` remains the single normal save-block serializer. Its write order,
stat synchronization, raw-byte preservation and CRC update are unchanged.
`memcard.rs` owns containers, slot placement and filesystem writes/backups;
`crc.rs` and `charset.rs` own their respective low-level encodings.

`companion.rs` validates bounded PEEX v1 files and the native `pe_host.extlink`,
preserves provider payloads, rebinds filenames/generations and publishes the new
companion before atomically replacing the native file. `PeSaveSlot.companion`
travels through cloning and JSON; older JSON without it is accepted only when
the native tail does not require a companion. `stacks.rs` owns stack schemas and
inventory operations shared by GUI and CLI. Provider-owned custom equipment
records are protected; unrelated equipment changes update the identity stamp
only when the original baseline matches.

Do not add filesystem or GUI dependencies to save operations. New editing
operations should act on `PeSaveSlot`/`EquipmentRecord`, and both GUI and CLI
should call those operations rather than reimplement them.

## CLI: src/cli/

| File | Responsibility |
| --- | --- |
| `args.rs` | clap argument schema; keep option names/backward compatibility here |
| `mod.rs` | Dispatch and shared slot resolution |
| `inspect.rs` | Save summaries and verbose inspection |
| `edit.rs` | Edit command and scalar ID parsing |
| `transfer.rs` | Card import/export and JSON transfer |
| `catalog.rs` | Item, effect and spell definition listings |

For a new command, define arguments in `args.rs`, implement the command in the
appropriate module and add one dispatch arm. Save mutation belongs in the model
or a mod-state schema module; command handlers orchestrate it.

## Custom content and mod state

These remain separate, focused modules:

- `tables.rs`: built-in game metadata and ID/class facts.
- `disc.rs`: optional verified disc data and retail templates.
- `custom_effects.rs`: PE Enhancements item/effect definitions and picker choices.
- `definitions.rs`: validated, immutable, data-only JSON definition packs.
- `mod_save.rs`: PEMC/legacy-tail inspection, preservation and transactional packing.
- `junk_counter.rs`: PELT/loot-record schema and Junk balance editing.
- `mod_save/tests.rs`, `mod_save/boundary_tests.rs`: container regression cases.

New custom items/effects normally require a definition pack, not a game-mod
installation or UI code change. See `CUSTOM_DEFINITIONS.md`. New editable record
payloads require a separate schema module that calls the shared transactional
record writer; do not place payload-specific offsets in `mod_save.rs` or the UI.

Unknown data must remain opaque and preserved. Unsupported formats/versions and
corrupt records must not be silently rewritten. Keep all size/version checks and
capacity failure paths when adding a schema. See `MOD_SAVE_DATA.md`.

## Verification

Run the full editor suite with `cargo test` and rebuild with `cargo build --release`.
The native-reader oracle can be enabled with `PE_MOD_SAVE_ORACLE` as described in
`MOD_SAVE_DATA.md`. The integration test under `tests/definitions_cli.rs` exercises
custom equipment creation through the real CLI in a separate process.

The GUI smoke test renders every page at 1024x680 and 1340x860 for two frames,
checking for rendering failures and changes to the saved mod tail. It is not a
replacement for manual pixel-level layout checks or in-game validation.

Synthetic fixtures in `tests/fixtures/` contain no player save data. Keep real
saves, build output and temporary reports out of source-control changes.
