# Mod API reference

This is the starting point for writing a mod. It covers the API version `PE_MOD_API_VERSION` 1 and
lists what each part does and where it's declared. The headers stay the authority on exact
behaviour.

For more detail, see:

- [`mods/README.md`](../mods/README.md): conventions, the mods themselves and how they fit together.
- [`mods/host/EXTENSION_APIS.md`](../mods/host/EXTENSION_APIS.md): custom equipment, shops, payouts,
  purchases and the companion file format.

Mods are compiled into the game with `-DPE_MODS=ON`. There is no plugin loading at run time. A mod
is trusted native code, so these rules are a cooperation contract, not a sandbox.

## Headers

| Header | Covers |
| --- | --- |
| `include/pe_hooks.h` | Game hooks and their context structs |
| `mods/host/pe_mod_host.h` | `PeMod`, hooks, services, events, files, logs, toasts |
| `mods/host/pe_mod_menu.h` | Rows and pages in the F1 Mods panel |
| `mods/host/pe_pages.h` | Shared PE Enhancements page paths |
| `mods/host/pe_game.h` | Shared game facts such as `pe_free_field()` |
| `mods/host/pe_save.h` | Per-save records in the card's free tail |
| `mods/host/pe_extension_store.h` | Companion file sections |
| `mods/host/pe_registry.h` | Definitions shared between mods |
| `mods/host/pe_services.h` | Service tables: Junk, Bestiary, costumes and others |
| `mods/host/pe_content_api.h`, `pe_inventory_api.h`, `pe_shop_api.h` | Custom equipment, effects, Catalog entries and shops |
| `mods/host/pe_economy_policy.h` | Payout modifiers, observers and Economy presets |

## A minimal mod

Create `mods/<folder>/` with a `.c` file that defines `const PeMod pe_mod_<folder>`, then add the
folder name to `PE_MOD_LIST` in `mods/CMakeLists.txt`.

```c
#include "pe_mod_host.h"
#include "pe_mod_menu.h"

#define MOD_ID "my_mod"

static int s_enabled = 1;

static int on_scene_start(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    if (s_enabled) pe_mod_log(MOD_ID, "a scene started");
    return 0;
}

static int my_init(void) {
    if (pe_hook_add(PE_HOOK_SCENE_START, on_scene_start, NULL, PE_PRIORITY_NORMAL, MOD_ID) < 0)
        return -1;
    pe_menu_begin("My Mod");
    pe_menu_register_toggle(0, "Log scenes", "Write a line to pe_mods.log per scene.", &s_enabled, NULL);
    pe_menu_begin(0);
    return 0;
}

const PeMod pe_mod_my_mod = {MOD_ID, "My Mod", "0.1", NULL, my_init, NULL};
```

`mods/pe_hello/pe_hello.c` is a fuller example, with a service, an event and live rows.

### `PeMod`

| Field | Meaning |
| --- | --- |
| `id` | Short and unique. It's also your registry namespace. |
| `name`, `version` | Shown in the Mods panel. |
| `requires` | NULL-terminated mod IDs to start first, or `NULL`. |
| `init` | Registers everything. A non-zero return leaves the mod off. |
| `shutdown` | Optional. |

Every mod's `init` runs, so its F1 pages exist even when the mod is off. If a mod is off in Manage
mods, is under All mods off, or needs an inactive mod, the host skips its handlers, ticks, services
and main-menu rows. Check with `pe_mod_active()`.

## Hooks

```c
typedef int (*PeHookFn)(void *ctx, void *user);
int pe_hook_add(int id, PeHookFn fn, void *user, int priority, const char *mod_id);
```

- `PeHookId` in `pe_hooks.h` lists every hook and its context type.
- Handlers run in priority order, lowest first: `PE_PRIORITY_EARLY`, `NORMAL`, `LATE`. A handler
  that changes a value sees the previous handler's result.
- Return 0 normally. Only hooks documented as replaceable act on a non-zero result, which makes the
  game skip its own code.
- Context pointers are valid only during the call. Don't keep them.
- Registering a hook while one is dispatching is refused with -1. So are recursion into the same
  event and nesting beyond eight dispatches.

Upstream game files only get single-line `PE_HOOK` calls, which compile to nothing without
`PE_MODS`. Add a new hook only where a mod needs one.

## Services and events

```c
int pe_service_publish(const char *name, int version, const void *api, const char *mod_id);
const void *pe_service_get(const char *name, int version);
int pe_event_id(const char *name);
int pe_event_emit(int id, void *ctx);
```

A service is a versioned table of functions. Always handle a `NULL` result and carry on without the
service. Put new service definitions in `pe_services.h`, so that no mod includes another mod's
headers.

| Service | Provider | Purpose |
| --- | --- | --- |
| `junk` | `pe_economy` | The Junk counter: read, spend, give, prices |
| `wallet` | `pe_economy` | Battle reward fields in the Junk record |
| `catalog` | `pe_catalog` | Armor effects unlocked in the Mod Catalog |
| `rebuild` | `pe_catalog` | Dismantled gear to buy back |
| `keys` | `pe_qol` | The key item pocket |
| `stacks` | `pe_stacks` | Stack counts, and stack-aware add and take |
| `wardrobe` | `pe_wardrobe` | Costumes that can be worn now |
| `costumes` | `pe_wardrobe` | Register costumes |
| `costume_rules` | `pe_economy` | Whether costumes must be owned before they're worn |
| `bestiary` | `pe_collection` | Register creatures, map enemies, unlock entries |

The host also publishes `content_registry`, `inventory_actions`, `equipment_effects`,
`catalog_entries`, `economy_shop`, `economy_policy`, `profile_transactions` and `equipment_guard`,
all described in `EXTENSION_APIS.md`.

Events let a mod announce something without knowing who listens. Existing events include
`bestiary.unlock`, `wardrobe.worn` and `play.log`.

## F1 menu rows

Call `pe_menu_begin("Page")` first. Pass 0 to return to `PE Enhancements`. `sub` is a sub-page, which
may contain `/`, or 0 for the page itself. Suite features use the paths in `pe_pages.h`.

| Function | Row |
| --- | --- |
| `pe_menu_register_toggle` | Saved on/off, with an optional per-frame function |
| `pe_menu_register_choice` | Saved choice from a fixed list |
| `pe_menu_register_int` | Saved number with a range and step |
| `pe_menu_register_action` | Runs once with the game paused |
| `pe_menu_register_info` | Read-only text |
| `pe_menu_register_live_int`, `pe_menu_register_live_choice` | Reads and writes through `get` and `set`; not saved |
| `pe_menu_register_dynamic_choice`, `pe_menu_register_live_dynamic_choice` | A list whose length can change |
| `pe_menu_register_tick` | Runs a function every field frame |
| `pe_menu_page`, `pe_menu_top_page` | A page's description and order |

Saved rows live in `pe_mods.ini`. All of these return 0, or -1 if the registry is full or an argument
is bad.

## Files and logs

| Function | Use |
| --- | --- |
| `pe_mod_path` | Builds a path beside the user's preferences |
| `pe_mod_log` | A line in `pe_mods.log` |
| `pe_log` | A `tag key=value` row in `pe_play.log`, also announced as `play.log` |
| `pe_toast` | A short on-screen message in interactive runs |

## Keeping data with a save

Pick one of the two stores:

| | Records (`pe_save.h`) | Companion sections (`pe_extension_store.h`) |
| --- | --- | --- |
| Where | Inside the 8 KB card block | `.pe-ext/` beside the card |
| Space | 744 bytes shared by all mods, including headers | 8,192 bytes in total, 16 sections, each up to 2,048 bytes |
| When a save uses it | Always, once registered | Only after the section is enabled |
| Use for | Small state every save needs | Larger or opt-in state |

### Records

```c
static struct { unsigned counter; } s_state;
static const PeSaveRecord s_record = {
    .name = "my_mod.state", .version = 1, .data = &s_state, .size = sizeof s_state,
};
/* In init: */
if (pe_save_register(&s_record) < 0) return -1;
```

The host saves and loads the memory at `data` for you. It resets the record on New Game, and it
carries the record unchanged when your mod is off.

- Never rename a record, because its name is its ID.
- Raise `version` when the layout changes, and provide `upgrade()` to read older layouts.
- Optional callbacks are `check`, `upgrade`, `reset` and `release`.
- A record must stay valid while the game runs.

### Companion sections

```c
static unsigned char s_bytes[256];
static const PeExtensionSection s_section = {
    .key = "my_mod/state", .version = 1, .min_reader = 1, .size = sizeof s_bytes, .data = s_bytes,
};
/* In init: */
if (pe_extension_register(&s_section) < 0) return -1;
/* Before first changing the data: */
if (pe_extension_enable(&s_section) != PE_REG_OK) { /* refuse the change */ }
```

- A section stays dormant until it's enabled, so a save that never needs it doesn't get one.
- A refused enable changes nothing, so enable before you change the data.
- Sections from newer builds or absent mods keep their bytes.
- Fixed layouts should be encoded into bytes, not stored as raw C structs.
- A missing or corrupt required companion refuses the load.
- Save backups must include the matching `.pe-ext` files.

## Registry: definitions shared between mods

Use the registry for custom content, effects, Catalog entries and shop offers. It decides once per
start which definitions are ready, with the same result whatever order mods start in.

1. **Collect:** during `init`, call `pe_reg_batch()` and then submit entries. `PE_REG_PENDING` is a
   receipt, not a usable definition.
2. **Seal:** the host checks limits, duplicates and references once every mod has started.
3. **Run:** look up ready entries with `pe_reg_find()` or `pe_reg_find_typed()`. Later submissions get
   `PE_REG_WRONG_PHASE`, so changes need a restart.

- Keys are `<mod id>/<name>`, lowercase ASCII, at most 47 bytes.
- A key claimed twice fails every claimant. Nothing wins.
- Every entry in a batch becomes ready, or none does.
- Check how a batch came out with `pe_reg_batch_result()`.

## Economy presets and payouts

A provider can register a complete preset, a payout modifier or an observer through
`economy_policy`. Registering never applies anything. A preset takes effect only after an explicit
Apply, such as from the F1 playstyle picker, and Enhanced doesn't call contributors at all.

```c
#include "pe_economy_policy.h"

static int my_init(void) {
    const PeEconomyPolicyApi *policy = pe_service_get("economy_policy", 1);
    if (!policy) return 0;
    if (pe_reg_batch("presets", 0, NULL) != PE_REG_OK) return 0;
    PeEconomyPolicyDef def = {
        .api = {1, sizeof def},
        .key = "my_mod/generous",
        .name = "Generous",
        .kind = PE_POLICY_PRESET,
        .preset = {
            .api = {1, sizeof(PeEconomyPreset)},
            .battle_junk = 3, .boss_junk = 10, .payment_chance_10000 = 10000,
            .extra_junk = 1, .native_to_counter = 1, .algorithm = PE_ECON_ALGORITHM_1,
        },
    };
    policy->add("presets", &def);
    return 0;
}
```

- Presets are self-contained, not partial overlays. Extra income requires `native_to_counter`.
- Modifiers add 0 to 65,535 whole Junk and a factor of 0 to 40,000 basis points. Factors combine by
  additive deviation from 10,000, so two 1.5x contributors give 2x, in any order.
- Modifiers and observers can't change game state or start another typed operation from the
  callback.
- Observers hear about a committed award once. Repeated or out-of-order operation IDs are refused.
- Inside `pe_economy`, `pe_econ_preview()` is pure, and `pe_econ_store_apply()` works only in free
  field. A refused Apply leaves settings and progress as they were.

Balance numbers are provisional, so don't rely on the built-in presets' exact values.

## Threads and timing

- Gameplay APIs run on the game thread. Worker calls are refused.
- Registration belongs in `init`. Hooks, sections, records and registry entries can't be added later.
- Check `pe_free_field()` before changing game state outside a hook made for it.
- Use the game's globals and structs, or `pe_game.h`, and never PS1 addresses.

## Compatibility

`PE_MOD_API_VERSION` rises only when a change in `pe_mod_host.h`, `pe_hooks.h`, `pe_mod_menu.h` or
`pe_services.h` would break existing mods. Tables can gain fields at the end within a version, and a
field added that way may be `NULL` on older builds, so check before calling it.

Keep these stable once released: record names, section keys, registry keys and profile keys such as
`pe1/rat`.
