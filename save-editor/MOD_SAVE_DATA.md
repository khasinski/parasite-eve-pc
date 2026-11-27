# Mod save data in the editor

The **Mod Save Data** page and `inspect --verbose` understand the upstream host's
PEMC format 1 (`mods/host/pe_save.h` and `pe_save.c`). This is the merged format,
not the earlier 12-byte-header proposal.

- The 880-byte tail starts at file offset 0x1C90. The 136-byte PKEY/SORT window
  at relative 0x200..0x287 is excluded from the linear container (744 bytes).
- Containers have a 20-byte header and 12-byte record headers, whole-container
  and individual IEEE CRC-32 checksums, plus the compatibility-window checksum.
- The page lists known and unknown records, versions, sizes, writer identity and
  damage/unsupported-format warnings. Valid records from a damaged container can
  be inspected, but explicit mod editing is disabled. Normal game-data edits
  still preserve its bytes.
- Key pocket, remembered sorting and the saved Junk counter can be edited for supported schema 1 and
  the earlier fixed PKEY/SORT layout. Changes also update the compatibility copy,
  record CRC, window CRC, container CRC and the game's save CRC.
- A compatibility window changed by an older game build takes precedence exactly
  as in the host. Its effective records are retained if another record is edited.
- Ordinary editor operations (including JSON round trips) preserve the mod tail
  byte for byte. Unknown records and other economy payloads remain read-only; they are not
  reset, migrated or merged with other saves. Junk has a separate schema module
  for PELT v1-v4 and pe_economy.loot v1, preserving all non-balance fields.
- Unsupported record versions/sizes cannot be overwritten by their controls;
  unsupported container formats/flags cannot be edited. Full containers fail
  without partial writes. The page does not delete unknown records to make room.
- Legacy saves remain legacy when edited: no automatic conversion to PEMC.
- This page only edits per-save state. Global Bestiary/costume progress and F1
  preferences live in separate files and are outside the save editor.

Pocketed keys are distinct from the bag. Editing a pocket does not change story
flags, open doors or transfer an existing bag item; use care when changing story
progression. Rank/category presets retain the game's serialized IDs rather than
native pointers. Saving sorting settings does not immediately reorder the bag.

## Checks

Run the normal `cargo test` suite. `mod_save` tests include a synthetic fixture
written by the actual C host: `tests/fixtures/mod-records.bin` (no retail data).
Its 512-byte unknown record crosses the compatibility window boundary.

For an additional native-reader check, compile `../tests/pe_editor_mod_fixture.c`
and `../mods/host/pe_save.c` with the same flags/includes as
`../tests/run_pe_save_test.ps1`. Set `PE_MOD_SAVE_ORACLE` to that executable when
running `cargo test`. The test sends editor output to the real host loader,
which verifies the edited keys/sorting and unchanged unknown payload.
The oracle test returns early if that optional native executable is not supplied.

To regenerate the committed synthetic fixture:

```text
pe-editor-native-fixture.exe write tests/fixtures/mod-records.bin
```

The original native serializer regression tests remain unchanged. No game save
from a player is required to test this format.
See [CUSTOM_DEFINITIONS.md](CUSTOM_DEFINITIONS.md) for the independent item/effect
definition packs, built-in custom items/effects, and saved Junk counter controls.

## Companion files and Stacks

The editor loads the exact PEEX v1 generation referenced by `pe_host.extlink`.
Missing, corrupt, mismatched and unsupported required companions are refused;
the editor does not silently discard a link or reset provider state. Unknown
sections, Economy policy/receipts and custom-content identities travel with the
save without being migrated or interpreted as ordinary inventory items.

For native PsyZ files, companions remain in the sibling `.pe-ext` folder. For
MCS, MCR/SRM and GME packages, the editor uses `<card-file>.pe-ext` beside the
package, keyed by the slot's native filename. Keep that folder with the package.
Exporting back to a PsyZ directory restores the game's `.pe-ext` layout. A PS1
emulator does not implement PEEX and cannot use the extra mod state itself.
JSON exports include companion bytes and the custom-equipment baseline.

Save, export, folder import, cloning, slot reassignment and Clear Data filename
changes publish a fresh, checksum-verified companion generation first, then
atomically replace the native save. Existing generations are retained; a failed
native publication leaves the previous native/companion pair usable. Files from
the source folder are kept. As before, close the game while editing saves.

The bag and Wayne inspectors show stack copies and allow quantities 1–99 for
supported stackable items. Transfers move the whole selected stack, merge into
an existing destination stack, and refuse overflow. Clearing/replacing a stack
clears its extra copies; sorting keeps counts attached to the item. The older
`pe_stacks.counts` tail record is also supported; the companion section wins when
both are present, matching the game. No new stack provider is invented for saves
without stack data: enable Stacks and save in-game first.

CLI example: `pe-save-editor edit CARD --stack-quantity bag:1:20` (also `storage`
and `vault`; slot numbers start at 1). Ordinary `--add-item` joins an existing
supported stack and refuses to exceed 99 copies. Direct JSON inventory changes
that leave orphan counts are refused rather than silently losing copies.

Registered custom equipment is protected against native record edits, since
the editor cannot recreate a provider's item/effect identity. Other inventory,
character, wallet, keys and sorting edits remain available. Unknown newer stack
schemas are preserved and refused for mutation.

Run `cargo test` for the editor regressions. To additionally compile and exercise
the current game's real companion loader on editor output, run
`tests/run_companion_native.ps1`; it sets `PE_COMPANION_ORACLE` for the optional
native-loader test. The older `PE_MOD_SAVE_ORACLE` checks remain available.
