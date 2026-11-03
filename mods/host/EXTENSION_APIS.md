# Mod extension APIs: native-compatible v1

Implemented in `mods/host`. These APIs add cooperation points; they do not activate new presets,
change Enhanced rewards, change existing prices, or populate shops automatically. New tests and
raw evidence stay in the local `pe-native-debug` repository.

## Services and ownership

Obtain a versioned table with `pe_service_get(name, 1)` and check the returned pointer and
`struct_size`. The host publishes these before mod initialization:

| Service | Header | Purpose |
| --- | --- | --- |
| `content_registry` | `pe_content_api.h` | Declare/find native-compatible equipment; preview its full fallback record |
| `inventory_actions` | `pe_inventory_api.h` | Grant equipment, adopt existing native equipment, inspect stable identity, destroy unequipped supported equipment |
| `equipment_effects` | `pe_inventory_api.h` | Declare native-effect aliases; apply/remove tracked attachments with provenance |
| `catalog_entries` | `pe_shop_api.h` | Declare entries; record discovery/unlock; hide undiscovered descriptions |
| `economy_shop` | `pe_shop_api.h` | Declare offers; issue checked quotes; purchase equipment or a Catalog unlock |
| `economy_policy` | `pe_economy_policy.h` | Register payout contributors, completed-award observers and complete preset definitions |
| `profile_transactions` | `pe_services.h` | Register profile owners and purchase session unlocks backed by next-save receipts |
| `equipment_guard` | `pe_services.h` | Legacy frontends can ask whether a record is safe for their existing operations |

The existing `junk`, `wallet`, `catalog`, `rebuild`, `wardrobe`, `costumes`, `costume_rules`,
`bestiary` and `keys` services keep their interfaces. `wallet` v1 still owns the old battle-reward
fields; the new APIs do not reinterpret it as a general transaction service.

Definitions are copied during the declaring mod's init, in a previously declared `pe_reg_batch`.
Initialize descriptor `api` to `{1, sizeof descriptor}`. Keys must be lowercase ASCII in the
submitting mod's namespace, `<mod_id>/<name>`, up to 47 bytes. Names hold up to 63 bytes plus NUL.
The registry owns the copied definitions until restart. Never save a pointer or a registry index.

Registration returning `PE_REG_PENDING` is a receipt, not a usable definition. The host seals all
providers together after init. Use the final batch result/readiness and fresh service lookups.
Duplicate keys fail all claimants. Duplicate Catalog ordinals fail all claimants within their
owner/domain/schema. Cross-provider references include a schema, so an untyped object cannot
satisfy a typed item/effect reference. Failed dependencies propagate to a fixed point. Required
and optional batches retain the existing registry rules and quotas.

All gameplay operations run on the game thread. Worker calls, stale generations, callback
reentry, invalid layouts and unsafe save states are refused. Pure callbacks receive copied
contexts and cannot use typed mutation APIs, register hooks/services, dispatch hooks or reset the
registry. This is a cooperation contract for trusted native mods, not a security sandbox for
arbitrary C code that writes game memory directly.

## Equipment and effects

V1 supports native weapon kinds 1–8 and armor kind 9. A definition uses a real native base item
and complete native-compatible stats, bonuses and effect slots. Factory preparation checks the
actual base kind. The native item ID remains its playable fallback. Custom names are available
through this API; unextended native item-name rendering still uses the fallback name.

Unique custom consumables, ammo, key items, new weapon animations and arbitrary new combat-effect
implementations are unsupported in this version. Do not put unknown effect IDs into native mod
bytes. Effects are validated aliases of implemented native effects with their real exclusivity
groups. Economy armor effects 17–20 require the Economy provider when producing/applying them.
Bag-capacity effect installation is refused by this API until a dedicated capacity adapter exists.

`inventory_actions.adopt` takes a native equipment value `0x100..0x17f`, not a bag slot. It tracks
an existing referenced record without replacing its stats. `grant` prepares an entire new record
and checks bag space, pool space, identity capacity and save admission before publishing it.
Stable nonzero instance IDs increase monotonically within a save. Bag sorting/storage movement
does not change an instance's pool identity. No identity is silently reassigned after an editor
changes the native pool: a stamp/fingerprint mismatch becomes uncertain or orphaned.

V1's ledger holds 32 instances, 12 content/effect definition-revision dictionary entries and 16 attachments in a fixed
2,048-byte section. Exhaustion refuses the operation. Dictionary slots are not recycled in v1;
New Game clears the per-save ledger. Missing content providers retain complete native fallback
records and their identities. Destructive typed actions require an active, certain instance.
An effect may be removed only when its saved attachment is removable and its provider is present;
built-in effects and unknown provenance are protected. Apply refuses occupied slots, duplicates
and exclusivity conflicts instead of silently deleting another effect.

Legacy Tools, dismantling and Catalog edit paths refuse tracked gear because they do not yet
preserve custom identity/attachments. Tools refusal happens before salvage observers, preventing
payment for a donor that was not consumed. Normal native equipment keeps its existing rules.
Typed destruction is explicit, refuses equipped gear and removes every native reference plus the
identity/attachments together.

## Catalog and shops

Catalog discovery is separate from the existing 312-byte Catalog record. There are 20 persistent
custom discovery keys in a 1,024-byte section. `visible` returns NULL until discovery; a frontend
should show `????` and no effect details. Provider/core lookup is intentionally separate from
that player-facing accessor. Unlocking also discovers an entry.

Shop offers reference a ready content definition or Catalog entry. Prices are exact registered
Junk prices in v1; they do not silently apply a second discount. Existing shops retain their own
pricing pipeline. Finite stock uses up to 10 stable offer counters in a 512-byte section;
unlimited offers need no stock counter. A changed limit below a saved sold count is a conflict.
Unknown provider keys are retained instead of being evicted.

A quote is short-lived and binds its generation, one-use token, price, stock, wallet balance and
native inventory snapshot. The last 32 quotes are bounded in memory; request a fresh quote after
any relevant state changes. Buying rechecks every precondition and prepares delivery/save
admission before spending. The synchronous native wallet subtraction is followed only by bounded,
infallible memory updates, with no provider callback, allocation or file I/O after debit. Wallet
callbacks run under the reentry guard. Providers of the old `junk` interface must honor its
existing contract: a failed spend changes nothing; a successful spend subtracts exactly the price.

These are shop/Catalog APIs, not a new shop screen. Providers can build their own frontends.
The existing native Catalog/Workshop lists are not automatically expanded with every registered
custom definition by this change.

## Payout cooperation

The policy service is opt-in for a configurable payout owner. Legacy Enhanced calculation does
not invoke contributors and remains unchanged. Registering a preset never automatically applies
it. New preset menus and automatic integration of a configurable battle pipeline are separate work.

Modifiers add 0–65,535 whole Junk and contribute a factor of 0–40,000 basis points. Factors combine
by additive deviations from 10,000, not registration-order multiplication. For example, two 1.5×
contributors produce 2×. Calculations use checked 64-bit arithmetic, cap an award at 99,999 and
return the fractional remainder for the payout owner to persist with its own state. Invalid
aggregate factors/overflow refuse the uncommitted award. No RNG or wallet is changed by preview.
The owner notifies observers only after committing an award; repeated/out-of-order operation IDs
are refused. Observers cannot replay a payment. Reset the notice sequence on load/New Game.

## Next-save profile purchases

Purchases become usable in the current session immediately. They become permanent only after a
successful normal game save. Loading an earlier save or quitting without saving discards both the
unsaved payment and pending unlock. There is no automatic save-file update at purchase time.

A profile owner registers an adapter during init in its own namespace. `admit` and `owned` are
pure; `promote` must be idempotent and report whether its atomic profile publication succeeded.
Other mods purchase through that owner; they do not edit another mod's profile. The host has eight
adapter slots and twelve bounded receipts in a 1,408-byte section. Unknown/inactive adapters keep
their receipts. If no slot or save capacity is available, the purchase is refused before payment.

Wardrobe and Bestiary use this service. Pending purchased facts are presentation overlays, not
profile-owned data. Earned Bestiary kills/Scan unlocks merge committed/earned bits, so saving a
profile independently cannot accidentally make an unsaved paid fact permanent. Profile writes
retain dirty state on failure and retry with a bounded delay.

The host writes, flushes and publishes a new companion generation first, packs its link under the
native card checksum, then flushes/publishes the native card. Only the successful native commit
promotes profile unlocks. If a later profile write fails or the process stops at that boundary,
the committed card's receipt replays on load. A failed native flush, close or rename never promotes
the profile. Failure tests use controlled Windows file operations; they are not certification
against every filesystem/power-loss behavior.

## Save layout and compatibility

Ordinary saves do not create companion data. The existing PEMC container format/writer revision
and native 8 KiB card layout stay unchanged. A conditional `pe_host.extlink` v1 record uses a
20-byte payload plus its existing 12-byte record header only when extension state needs saving.
Checked admission refuses a save rather than silently dropping carried mod data.

Companions are host-derived paths beside the native card:
`.pe-ext/<native-card-name>.<eight-hex-generation>.pex`. Providers never supply filesystem paths.
V1 has an 8,192-byte total file budget and 16 sections, each up to 2,048 bytes. It uses a 96-byte
header and 64-byte directory entries with full keys, versions, offsets, sizes and CRCs. Header
offset 84 stores the preceding committed generation; offset 88 stores the header/file CRC with
that field zeroed for calculation. The tail link also checks the complete file, size, generation
and section count. Fixed component layouts are encoded into byte buffers, not raw C structs.

This deliberately replaces the draft's inconsistent 32-byte-header/5,120-byte arithmetic:
the complete keys, directory and twelve full-key receipts must be counted. With all four core
sections enabled, payloads use 4,992 bytes and framing uses 352 bytes, totaling 5,344, within 8 KiB.
Unknown/newer provider sections retain their bytes/version; they cannot be enabled or rewritten
by an older reader. A missing/corrupt required companion or unreadable core identity-ledger
layout refuses the outer load before native runtime fields change. New Game clears per-save
state. Save backups must include the matching `.pe-ext` files.

Current/previous committed generations are retained; superseded known generations and owned
cancelled files are cleaned. A process crash can leave an orphan generation. Existing generation
files are never overwritten; excessive collisions refuse saving instead of deleting unknown data.
There is no per-frame registry rebuild, unbounded transaction history or runtime heap allocation
in the new equipment/shop/policy loops. Registry staging retains its checked 1 MiB envelope.

## Validation

Local runners cover provider orders/conflicts, schemas, disabled providers, thread/reentry guards,
stale/duplicate purchases, capacities, unknown/newer preservation, native commit failure points,
real Wardrobe/Bestiary adapters and save/writer compatibility. The 10,000-cycle equipment soak is
a short regression test, not a long-running gameplay or low-powered-hardware benchmark.
See `pe-native-debug/tools/run_api_interactions.py`, `run_api_extensions.py`,
`run_api_integrations.py` and `run_api_save_regressions.py`; no new test suite is added to this repo.
