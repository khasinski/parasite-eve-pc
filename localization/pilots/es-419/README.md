# Español latino: menús, objetos e interfaces de mods

Expanded draft for Latin American Spanish (`es-419`), October 30, 2026. It needs a fluent
Spanish-speaking player to proofread the wording and choices before a public translation release.
The original `../es.csv` is retained as the historical 174-entry pilot.

## Scope

**1,349 translated entries**, split into eleven CSV modules containing only keys and translations:

| Module | Entries | Coverage |
| --- | ---: | --- |
| `menus.csv` | 99 | Actions, save/load prompts, preferences and resident menu labels |
| `items.csv` | 155 | Medicines, supplies, bonuses, armor names, keys and PE powers |
| `descriptions.csv` | 239 | Item, weapon, armor, key and PE descriptions |
| `effects-and-help.csv` | 93 | Equipment-effect descriptions, menu help, sorting and locations |
| `wayne.csv` | 19 | Existing explicit Wayne/Workshop fragments |
| `interfaces/host.csv` | 121 | Shared menu/navigation labels and interface text |
| `interfaces/economy.csv` | 247 | Catalog, Dismantle, Rebuild, Suministros, prices and confirmations |
| `interfaces/inventory.csv` | 29 | Inventory actions, counters, combining, Smoke Grenade and Panacea |
| `interfaces/collection.csv` | 63 | Bestiary/Wardrobe display and outfit names |
| `interfaces/settings.csv` | 254 | Mod setting labels, creature/loot descriptions and collection text |
| `dialogue-pilot.csv` | 30 | Existing pilot pages only: opening, phone and Wayne choices |

All 255 native item records were reviewed. Gun model codes, proper names, symbols, internal
placeholders and text already identical in Spanish intentionally retain native fallback; these
are not counted as new translations. `coverage.json` records the unchanged keys and exclusions.
Fixed rename grids remain native, and Aya's saved name is unchanged.

Remaining English includes labels drawn into vanilla images, unkeyed native HUD text, some
advanced/debug descriptions and unregistered custom-pack labels. Most story dialogue is outside
this milestone. External costume/creature pack names are preserved unless explicitly keyed.
The two inherited phone-page layout warnings (`dlg/M026/006`, `dlg/M290/006`) still require review.

## Build and play

The committed source manifest allows compilation without extracting/distributing the game's text:

```sh
python -I localization/pelang.py build localization/pilots/es-419 \
  --strings localization/data/manifest.json \
  --mod-strings localization/data/mod_strings.json \
  --lang es-419 --name "Español latino" -o localization/out/es-419.pelang
```

This uses strict checks: no overflow override. CSV directories are read recursively in sorted
order; duplicate translated keys fail compilation. Add a row in its owning module, not a second
copy elsewhere. Blank/missing translations keep the original game text.

Put `es-419.pelang` in `lang/` beside the rebuilt executable (or in the user's language folder).
Enable All mods and Language packs, then choose **Español latino (es-419)** in F1 → Language.
The translated navigation may read F1 → Idioma. The pack is not automatically selected.
Its `lang` and `name` identity should remain stable across updates.

## Vocabulary and review decisions

| English term | Spanish |
| --- | --- |
| Junk | Chatarra |
| Dismantle | Desmantelar |
| Rebuild | Reconstruir |
| Requisition | Suministros |
| Wardrobe | Vestuario |
| Bestiary | Bestiario |
| Storage | Depósito |
| Slot | Espacio for effects; ranura for a memory-card slot |
| HP / PE / AT | PV / PE / AT; image-based HP labels remain unchanged |
| Darkness / Stiffness | Ceguera / Parálisis |
| Tool / Tool Kit | Herram. / Kit herram. in narrow rows |

No prices, recipes, balance values, unlock conditions, hidden-information rules or saved IDs are
changed. Dismantle/Rebuild descriptions retain the distinction between learning an effect and
buying equipment back with empty effect slots. Native prompts retain their control tags, choice
counts and order. Confirm these meanings in-game during proofreading, not just spelling.

## Adding mod interface text

The existing explicit-key `pe_tr` service is unchanged. Mod strings are organized under
`localization/data/mod_strings/`, whose JSON include indexes compose a single checked manifest.
Plain UTF-8 interfaces use optional `interface_text` v1 through `mods/host/pe_localize.h`.
`mod/ui/<FNV1a(source)>` is a stable display key derived from the exact English source string.
Its manifest records the source, native glyph fallback, ownership contexts and text limits.
The loader checks that each key matches its source and rejects duplicate/cyclic includes.

`pe_l10n_ui` localizes a display label or printf template. Registration paths, settings keys,
collection keys, save fields and action identifiers remain English/stable; F1 labels translate
at draw time. `pe_tr_ascii` and `pe_t_ui` encode localized UTF-8 into native glyphs at presentation
boundaries. Data-driven equipment names use their existing `item/NNN` keys. Mod-created Panacea
and Smoke Grenade refresh their translated name/description at their late text lookup hook.

Printf directives must match exactly in count, order, flags and type. The compiler and runtime
both check them before a translation is used as a format. Missing/inactive providers, absent
keys, unsupported command-bearing labels or invalid format signatures fall back to English.
Decoded UTF-8 labels are cached per pack and freed with it, so multiple translated arguments can
coexist without a reused scratch buffer. The Unicode bridge is generated with:

```sh
python -I localization/pelang.py interface-header
```

## Verification

All new test sources and test-only shims stay in local `pe-native-debug`, which has no remote.
Existing repository tests remain in place. Reports and screenshots are under `runs/es-419`.

- Strict pack build: 1,349 entries, zero errors and the two inherited phone-layout warnings.
- Existing compiler suite: 35 passed.
- Local Spanish suite: eight checks for module uniqueness, reviewed-key coverage, interface
  keys/layout limits, printf injection/type changes, manifest cycles and generated-code parity.
- Local native interface checks: real pack lookup, stable UTF-8 cache, accent encoding, item-name
  lookup, bounded output, formatting, malicious-format rejection and inactive-provider fallback.
- Local headless suite: 11/11 passed. Missing/unselected/disabled packs remain pixel-identical to
  baseline. Inventory, action lists, save screens and all six Wayne menu rows were captured.
- Catalog, Key Items, Bestiary and Wardrobe were additionally opened and visually reviewed using
  disposable local cards/profile data. No purchases or equipment changes were needed.
- Ten affected existing native checks passed. The isolated Catalog-rules CTest target has missing
  host registration stubs; the same rule assertions pass with test-only host shims kept locally.
  No production host/game stubs are included in the mod.

This is a tested draft, not a claim of full-game translation or human proofreading.
