# pe_bp: BP Overhaul (preview)

New ways to spend the game's own Bonus Points: Resistance, Fortitude, PE Mastery, Wayne's
Reset BP and EX Game carryover. Earning BP is unchanged. Design:
`PE_BP_Overhaul_Revised_2026-11-12.md`. Built only into the preview executable
(`-DPE_EXTRA_MODS=pe_bp`, see `prototypes/bp-ui`).

## Files

| File | What it does | Game globals |
|---|---|---|
| `bp_defs.c/.h` | Tracks, lines, prices, battle numbers | no |
| `bp_record.c/.h` | Ranks a save holds, the three save sections, EX carryover | no |
| `bp_quote.c/.h` | Snapshots, requests, quotes, the write plan | no |
| `bp_txn.c/.h` | Transaction executor: re-read, re-quote, refusable steps, then writes | no |
| `bp_rules.h` | Includes the three rules headers | |
| `bp_api.c` | The public `bp` service, pauses, events; every commit goes through it | no (host callbacks) |
| `bp_store.c` | Save sections, the current record, load / New Game / EX carry | yes |
| `bp_game.c` | Snapshot of the game, and the writes a transaction commits | yes |
| `pe_bp_battle.c` | Battle effects (Resistance, Fortitude, Mastery) | yes |
| `pe_bp_menu.c`, `bp_config.c` | F1 page, presets and profile settings | |
| `pe_bp.c` | Mod entry: registers the parts, publishes the services | |
| `pe_bp_internal.h` | How the parts reach each other (this mod only) | |
| `pe_bp_screens.h` | `bp_screens` v1: the BP screens' full snapshot (this mod's own UI only) | |

Everything without game globals is unit tested in `prototypes/bp-ui` (`bp-live-backend`,
`bp-public-api`).

## The `bp` service, for other mods

Declared in `mods/host/pe_services.h` with the other services, so no header of this mod is
needed. Get it at any time after init and check it is there: with `pe_bp` off or missing a mod
carries on without it.

```c
#include "pe_services.h"

const PeBpApi *bp = pe_service_get("bp", 1);
if (bp && bp->struct_size >= sizeof *bp) {
    PeBpStatus st = {sizeof st};
    if (bp->status(&st)) {                      /* a game is loaded */
        int poison = bp->active_rank(PE_BP_DEFENSE, 0);   /* 0 while off or paused */
        PeBpQuote q = {sizeof q};
        if (bp->quote("buy.line.0", &q) == PE_BP_OK && q.bp_cost <= st.bp)
            bp->execute("buy.line.0", "my_mod", &q);      /* commits, or changes nothing */
    }
}
```

| Function | Use |
|---|---|
| `status` | Loaded, unspent BP, upgrade-only pool, features in effect, pauses, carryover setting |
| `rank` / `active_rank` / `carried` / `max_rank` | A track's or line's rank; the rank in effect; ranks carried from the last run |
| `key` / `name` / `price` | Stable key (`"poison"`, `"heal"`), shown name, BP per rank |
| `quote` / `execute` | The screens' requests (`"buy.def.0"`, `"reset.line.3"`, ...). `quote` changes nothing; `execute` re-checks and commits whole or not at all |
| `grant_pool` | Adds upgrade-only BP (buys Resistance, Fortitude, Mastery only). Saved with the game |
| `pause` | `pause("my_mod", PE_BP_FEATURE_MASTERY)` switches those effects off until `pause("my_mod", 0)`. Ranks stay; purchases there are refused. Not saved; each mod's pause is its own (up to 8 mods) |
| `error_text` | The player-facing reason for a `PE_BP_ERR_*` |

Kinds and indexes, requests, error numbers and struct rules are listed with the declarations.
Out-structs carry `struct_size`: set it to `sizeof` your struct and fields appended later are
never written past it.

### Events

Listen with `pe_hook_add(pe_event_id("bp.changed"), fn, NULL, PE_PRIORITY_NORMAL, "my_mod")`.

| Event | ctx | When |
|---|---|---|
| `bp.changed` | `const PeBpChanged *` | After a purchase, refund or setting (from a mod or the BP screens; `by` is the mod id or NULL), a grant, a save load, a New Game, an EX carry |
| `bp.effect` | `const PeBpEffect *` | A rank acted in battle: `resisted`, `fortitude`, `preraise`, `second_roll` |

### What it never does

Create normal BP, write Aya's stats other than entries 5-6 (Active Time, Item Capacity), refund
weapon or armor BP, or write anything after a refusal.
