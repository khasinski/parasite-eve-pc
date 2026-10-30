# Localization

The `pe_lang` mod shows the game's text from a **language pack**: translations by key, already in
the game's glyph code, loaded at start and swapped in when the game looks the text up. Nothing on
the disc changes and nothing is saved, so packs can be switched at any time and saves stay the
same in every language. Anything a pack doesn't translate keeps the game's own text.

The full plan, with the font work, the languages and the Japanese-to-English re-localization, is
the [Parasite Eve Localization Mod Plan](https://claude.ai/artifact/2NpmecRhhgMB1Zx7vxQojM).

## Status

| Piece | State |
| --- | --- |
| Text extraction, English and Japanese (`localization`) | Done |
| Translator template, test language, pack compiler (`pelang.py`) | Done |
| `pe_lang` mod: F1 > Language, item names and descriptions | Done |
| System messages, misc table, room dialogue | Done: `PE_HOOK_TEXT_LOOKUP` from every table, `PE_HOOK_TEXT_PAGE` |
| Spanish pilot | Latin American Spanish: 1,349 entries across eleven modules for menus, items and mod interfaces; draft, needs Spanish-speaker review |
| Mod-owned text (`pe_tr`) | Explicit-key fragments plus optional UTF-8 interface translation, checked printf templates, Catalog/shops/inventory/collection integration |
| Text drawn into images and text compiled into the port | Not keyed yet |
| Accented letters (Latin Extended: French, German, Spanish, Italian, Portuguese, Polish, Czech, Turkish) | Done |
| Japanese semantic proofreading | Pending human review; extraction/alignment does not establish translation accuracy |
| Cyrillic | Russian, Ukrainian and Belarusian alphabets supported; modular authored glyphs and per-pack activation |
| Chinese, Japanese, Korean | Optional external-font compilation, v2 glyph dictionaries and a native cache; see CJK scope and limits below |

A pack can use the US font's glyphs (A-Z, a-z, 0-9, space and `+ - % & ! ? " ' . : , /`) and the
Latin Extended set below and the Russian/Ukrainian/Belarusian Cyrillic alphabets. The compiler
names unsupported characters. CJK requires an explicit font input and uses the separate encoding
described below; other extended Cyrillic alphabets are not implied.

## The Latin Extended letters

111 glyphs, defined in `localization/petext/latin.py`:

- **Letters with marks**, lowercase and capital: à á â ã ä å ç è é ê ë ì í î ï ñ ò ó ô õ ö ø ù ú
  û ü ý ÿ, ą ć ę ł ń ś ź ż, č ď ě ň ř š ť ů ž, ğ ı İ ş, ő ű.
- **Signs**: ß æ Æ œ Œ « » € ° ¿ ¡.

They are made from the game's own letters at run time, so no game art is in the repo or in a pack:
- When the dialogue font goes to VRAM (`PE_HOOK_TIM_UPLOAD`), `pe_lang` composes each glyph from its
  base letter plus a mark drawn in `latin.py` (é is the font's own `e` plus an acute), into the
  font's empty cells, codes 0x4D-0xBB.
- Over a capital it first drops one repeated stem row, so the mark fits without moving the baseline.
- The few signs that aren't letter + mark (ß, æ, «, €) are drawn in `latin.py` in the font's style.
  ¿ and ¡ are the font's ? and ! turned over.
- Widths come from the base letter: the dialogue box reads them through `g_TextboxGlyphWidths` (the
  original table unless a mod points it at a longer one), the menus from the resident glyph
  metrics, for those codes only.

`python -I preview_latin.py US1.cue` draws the set from your disc to `out/latin.png`, with sample
words. After changing `latin.py`, run `python -I pelang.py latin-header` to regenerate
`mods/pe_lang/pe_lang_latin.h` and the test vectors; the tests check both are current, and
`mod-lang-font` checks the C composer against the Python one pixel for pixel.

## Making a pack

Translators: [TRANSLATING.md](TRANSLATING.md) is the contributor's guide (extract from your own
discs, translate, `export` the translations-only file, open a pull request). Submitted languages
live in `localization/translations/`; CI (`.github/workflows/localization.yml`) checks and builds
them from `localization/data/manifest.json`, the source's shape without its text, so it needs no
discs, and refuses a submission with source text in it.

Every step reads the translator's own discs; no game text goes into git or into a pack.

```
cd localization
python -I extract_text.py US1.cue US2.cue [--ja JP1.cue JP2.cue]
python -I pelang.py template out/text/strings.json -o fr.csv
python -I pelang.py build fr.csv --strings out/text/strings.json --lang fr --name French -o fr.pelang
```

1. **Extract.** `extract_text.py` writes `out/text/` (git-ignored): `strings.json` and
   `strings.csv` with every entry's key, English, Japanese (with `--ja`), line widths in pixels and
   the boxes it shows in, `banks.json` (each message bank's CRC32), `menu_glyphs.json` (the menus'
   glyph widths), plus the item reports. Keys: `sys/NNN`, `item/NNN`, `desc/NNN`, `misc/NNN`
   for the resident text tables, `dlg/MNNN/PPP` for room dialogue (the message bank first used by
   room MNNN, page PPP). The 148 table slots that hold filler the game never shows are left out.
2. **Template.** `pelang.py template` writes the translator CSV: key, English, Japanese (for
   reference), translation, box, widest line, rooms, notes. `--merge old.csv` carries the
   translations of an earlier template over, by key, and notes rows whose English changed.
3. **Translate.** In any spreadsheet. Keep the tags: `{br}` line break, `{wait}` next screen,
   `{name}` Aya's name, `{choice N}` a choice of N options, `{red}`/`{/red}` highlight, `{num}` a
   number (see `petext/gametext.py`). An empty translation keeps the game's text.
4. **Build.** `pelang.py build` checks every row and writes the pack.
   - Errors stop it: unknown/duplicate keys, unsupported characters, malformed command operands,
     embedded terminators, changed control sequences, or overflow in a known dialogue box.
     Commands are compared after encoding, so raw tags cannot bypass validation. Line breaks may
     move; waits, choices, names, numbers, pauses, highlighting and extra-page glyphs keep their
     count, order and operands. The compiler supplies the original page ending itself.
   - A dialogue page that overflows its box is rewrapped (`petext/wrap.py`): line breaks at
     spaces, continuation lines indented as the English indents them, and a screen still too tall
     split into screens of the box's height with added waits (an extra button press in game). A
     screen with a choice is never split and its choice rows are never moved; a word wider than
     the box stays as it is. Pages that fit keep the translator's breaks. The build notes every
     rewrapped page for review; `--no-wrap` turns it off.
   - Dialogue checks live in `petext/layout.py`. The default box allows 297 px and four lines per
     screen. Widths are the game's spacing table, with the English box's narrower space (it pulls
     each space back 7 px). A styled box neither insets nor clips its text, so going past its rectangle is a
     warning and only the screen edge is an error. Waits reset the line count. `{name}` is
     measured as the pack's `sys/030` (Aya's default name, which the dialogue uses until the
     player renames her; the English is laid out for "Aya"), and numbers reserve five 12-pixel
     digits. With these rules the original English passes every check.
   - A page no script ties to a box is checked against every box its bank's rooms open (only
     opcodes 13 and 209 show a bank's pages), as a warning, since the page may be unused or opened
     from a room whose scripts don't decode.
   - Menu checks live in `petext/menu.py` and `data/menu_layouts.json`. Menu text is measured as the
     menus draw it (the game's glyph metrics, which `extract_text.py` saves as `menu_glyphs.json`).
     Each context names the code that draws it and its limits:
     - help line: 294 px (all descriptions, most `misc`, the option hints);
     - item list: 104 px, or 86 px for guns and ammo crates, whose count is drawn after the name;
     - Parasite Energy list: 98 px; config menu labels: 64 px; save file location names: 138 px
       (the panel cuts off the rest, so this one is an error); the day label before its number;
     - the other centred labels (OK/Cancel, Yes/No, Mono/Stereo, storage tabs and choice, colour
       options): their cell width less the 4 px the centring adds;
     - dialog lines and confirm labels: 63 bytes (they are copied unbounded into 64-byte buffers)
       and the screen width;
     - the item message: 31 bytes, an item's name and `sys/016`/`017` together (more and the game
       shows nothing);
     - the rename grids (`sys/115`–`117`): the same number of cells as the English;
     - the default name `sys/030`: 8 letters.

     Byte limits, the screen edge and clipped text are errors; the other widths are warnings.
     The help line, item list, Parasite Energy list, config labels and location names were
     confirmed in game with packs at exactly those widths (pe-native-debug `lang_items` and
     `lang_menus`). Text the game joins (a name and its suffix) is checked with the pack's
     longest of each part. Keys no context names yet are compared with the widest English entry
     of their table.
   - Every choice's option meaning/order also needs human review; matching control bytes cannot
     prove that.
   - For deliberate overflow/pseudo testing only, `--allow-overflow` turns known layout failures
     into warnings. It never permits malformed or changed controls.
5. **Play.** Put the `.pelang` in `lang/` beside the game, or in `lang/` in the user folder (beside
   `pe_mods.ini`). Build the game with `pe_lang` in `PE_MOD_LIST`, turn All mods on, and pick the
   language under F1 > Language.

`pelang.py pseudo` makes a test language from every entry: letters swap case, and each entry is
marked with `!` and padded about 30%. Build it like any pack (`--lang qps`) to see what text a pack
reaches and where longer text overflows. Use `--allow-overflow` when compiling this deliberately
expanded test text. Extracted rows containing unsupported control bytes are left untranslated
with a note, retaining the game's own text rather than bypassing validation.

Language selection is stored separately in `pe_lang_selection.dat` beside `pe_mods.ini`, using
the pack's language code and display/edition name. Filenames and list positions are not identity.
Keep these two header fields stable across pack updates; give different editions distinct names.
An identical identity in the user folder overrides the copy beside the game. If a selected pack
is missing, Game text is used and the identity is retained until a new selection is made.
Old numeric `Language/Language` preferences are intentionally ignored: select your language once
after upgrading, because the old index cannot establish which pack was intended.

## How the text is swapped

The mod never changes the game's data. At the moment the game takes a piece of text, a hook hands
the mod the pointer, and the mod gives back a pointer into the pack: the loaded text, the scripts
and the saves stay as they are, and turning a pack off (or leaving an entry out) lets the original
through.

- **Text tables.** `Text_LookupTable4` / `8` / `C` / `10` and the map location names send
  `PE_HOOK_TEXT_LOOKUP` with the table (4, 8, 12, 16) and index; keys `sys/`, `item/`, `desc/`,
  `misc/`.
- **Dialogue.** When a box finds its page in the room's message bank (`textbox.c`), it sends
  `PE_HOOK_TEXT_PAGE` with the bank and the page. Banks are shared by an area's rooms and keyed by
  the first room that uses one, so the pack names each bank by the CRC32 of its English bytes
  (`banks.json` from the extractor), and the mod measures the bank a box reads from once per room.
  A bank the pack doesn't know (another disc version, a modded room) keeps the game's text.
- **Choices** are read back by position. The compiler preserves choice commands and their counts;
  translators must also preserve the option meanings and order. A page that ends in F9 keeps that end.

## Pack format

Little-endian, original version 1 (`pelang.py` writes it, `mods/pe_lang/pe_lang_pack.c` reads it).
CJK uses the v2 extension documented below; non-CJK output retains this format:

| Part | Layout |
| --- | --- |
| Header, 64 bytes | `"PELG"`, u32 version, u32 entry count, u32 offset of the text, char lang[16], char name[32] (UTF-8, NUL-padded) |
| Entries | count × {u32 key hash, u32 text offset from the text start}, sorted by hash |
| Text | each entry's glyph bytes followed by `FF` (a page that ends in F9 keeps it before the `FF`) |

The key hash is 32-bit FNV-1a of the key's UTF-8 (`item/054`). The compiler refuses a collision,
and the reader checks contiguous entry spans, complete commands and exact terminators at load,
refusing a damaged pack whole. Entries use the same order in the table and text payload, without gaps. Besides
the translations, a pack holds one `bank/<crc32>` entry per message bank, whose value is the bank's
key in ASCII (`M001`).

## The Japanese source

`extract_text.py --ja` reads the Japanese release (SLPS-01230 / 01231) and puts each Japanese line
on the same key as its US line. The Japanese scripts open the same pages in the same order as the
US ones in 806 of 810 rooms; M195 and the debug room M360 differ, and their pages are flagged
`ja_aligned: false`.

Decoding uses `data/ja_glyphs.tsv`, which maps each glyph of the Japanese font to its character.
It holds character identities only, no game text or art.
- `build_glyph_table.py` rebuilds it with Windows' Japanese OCR (`ocr.ps1`; needs the Japanese
  language pack), reading glyphs in rows and then every real line for context.
- A character in the `fixed` column wins over OCR and survives a rebuild; 181 glyphs are read by
  eye.
- `out/font/review.png` shows what still needs an eye, including any glyph read as a rare kanji
  (OCR tends to pick variant forms such as 逹 for 達).

The Japanese needs a proofread by a Japanese reader before it is used as more than a translator's
reference.

## Tests

- `python -I -m unittest discover -s localization/tests`: templates, the test language, the
  compiler's checks, and the pack round trip.
- `ctest -R mod-lang-pack`: the C reader's lookups by key, table and bank page, the bank measure
  and CRC, and damaged packs refused whole.
- `ctest -R mod-lang-font`: the C composer against `latin.py` on synthetic letters, page fill
  (only empty cells change), widths.
- In game: pe-native-debug's `scenarios/lang_items.py` loads a save with a pack and without, and
  takes pictures of the item screen and a phone's dialogue box.

## Spanish pilot and mod-owned text

The first pilot is **Spanish**. See [`localization/pilots/README.md`](../localization/pilots/README.md)
for the coverage and local build command. It is a partial draft for testing the pipeline, not a full
game translation or a proofread release. Missing entries use the game's English.

Mod-owned game labels use `mods/host/pe_translate.h`, separate from resident game-table lookups:

```c
#include "pe_translate.h"
/* Append one plain label to a caller-owned PeText message. */
pe_t_tr(&text, "mod/pe_economy/wayne/workshop", "Workshop", 24);
```

`pe_tr(key)` queries the optional `translations` v1 service and returns game-encoded glyphs or
NULL. `pe_t_tr` appends a bounded single-line translation, using the original English when the
provider/entry is missing or an invalid label is supplied. It rejects command bytes, line breaks,
empty labels and labels exceeding the caller's limit; it never inserts a new choice or treats
translation text as a format string. The host disables the service when pe_lang or All mods is off.
There is no dependency requiring economy mods to install or enable pe_lang.

The key and layout catalogue is `localization/data/mod_strings.json`. Each entry supplies the
mod's own fallback text, a glyph limit and a pixel limit. Add it explicitly with `--mod-strings`
to `pelang.py template`, `pseudo` or `build`. It is merged with extracted game keys in memory;
duplicate keys, unknown keys and namespace collisions are rejected. Non-CJK packs stay v1; CJK
mod labels use the same optional v2 dictionary as game text.
`petext/modtext.py` owns this validation separately from game dialogue and menu checks. A mod label
cannot contain controls even when deliberate layout overflow is enabled. Fragment limits cover
the fragment itself; a complete message that combines names, counts and fragments still needs an
in-game layout check.

Wayne's added Mod Permit Card and Workshop rows, Workshop options/titles, paging, permit prompts
and small status labels are the first consumers (20 keys). His original choice order is preserved.
The hand-in option is identified before a language pack changes its wording, keeping its Junk
totals under translation. Other mod messages, effect names, F1 overlay strings and native image
labels remain future coverage; this API does not imply they are already translated.

The headless scenario suite and new mod-text contract checks stay local in `pe-native-debug`, which
has no remote. Existing repository unit tests remain where they already were. See the Cyrillic and CJK sections below for font support added after the pilot. Image text and
Japanese human proofreading remain separate milestones.

### Pilot verification (October 29, 2026)

- Native localization game rebuilt successfully.
- Existing Python compiler/layout suite: 31 passed. Existing C pack/font/selection checks: 3 passed.
- New local mod-text compiler contracts: 6 passed; native optional-service/fallback/buffer checks passed.
- Local headless suite with the Spanish pilot and `--unchanged-text menu`: 11/11 passed, including
  disabled/missing/unselected pack equivalence and all six lines in Wayne's expanded storage menu.
- Spanish inventory names, save-phone choice and Wayne's added rows were visually inspected in
  captured game frames. Screenshots/reports stay in `pe-native-debug/runs/lang_suite`.
- The F1 raster now decodes UTF-8 codepoints consistently for width and drawing. An actual-font
  local check renders “Español - piloto” correctly and verifies malformed/truncated input handling.
  This fixes language-pack display names; it does not add Cyrillic/CJK glyph pages to the game's
  own text renderer. Those language/font milestones remain pending.
- The compiler still reports the two unproven phone-page layouts listed in the pilot README;
  neither warnings nor partial semantic coverage are described as a proofread full translation.

## Cyrillic

The Russian alphabet and Ukrainian/Belarusian additions are supported in both cases: **76 Unicode
characters**. Visually identical ASCII/Latin forms share existing glyph codes (including Ё/ё with
Ë/ë and Ї/ї with Ï/ï). The other **52 glyphs** are authored in `petext/cyrillic.py`, assigned
`0xBC..0xEF`, after the stable Latin range `0x4D..0xBB` and below the command range starting at
`0xF1`. Every new cell was verified empty in the retail USA font. No game font art is distributed.

The compiler accepts these characters wherever existing extended text is allowed, including
dialogue, item names, system strings and `pe_tr` mod labels. It uses the Cyrillic advances for
dialogue layout and the runtime's metric bytes for menu layout. Mixed Latin/Cyrillic text is
supported without a script flag or a new pack format. The Spanish pilot recompiles unchanged.
Latin/Cyrillic-only packs remain v1, and missing translations keep the original game text.

`pe_lang_cyrillic.c/.h` renders the authored strokes and native-style shadow, checks page bounds
and never overwrites occupied cells. `pe_lang_cyrillic_data.h` is generated from the Python module:

```sh
python -I localization/pelang.py cyrillic-header
```

The runtime scans validated pack strings for new single-byte glyph codes, skipping command and
extra-page operands. A Latin-only pack does not install the new Cyrillic pixels or metrics.
Switching between Cyrillic, Latin and Game text rebuilds from the saved original page and restores
the previous metrics before applying the next set. Font uploads and scene changes reinstall the
chosen set through the existing hooks. Existing Latin code assignments and pack identity are unchanged.

Limits: this covers Russian, Ukrainian and Belarusian, not every letter in the Unicode Cyrillic
blocks. Serbian, Macedonian, Kazakh and other extended sets need more font capacity/design work;
the compiler rejects unsupported letters. See below for CJK. Image labels, translations/proofreading of complete
languages and broader mod-message coverage remain separate milestones. Glyphs should be reviewed
by fluent readers at the game's native resolution before publishing a language pack.

### Cyrillic verification

All new fixtures, vectors, previews and smoke packs stay in local `pe-native-debug`:

- `tools/test_cyrillic.py`: all alphabets, aliases, code ranges, generated-header consistency,
  dialogue/menu advances, descenders/shadows and an actual-disc font preview.
- `tools/pe_cyrillic_test.c`: Python/native pixel equality for all 52 cells, occupied-cell and
  bounds guards, neighbouring pixels, idempotence, command-operand skipping and Latin-only pack detection.
- `tools/pe_cyrillic_switch_test.c`: production font-switching code restores exact pixels,
  menu metrics and original dialogue-width pointers across Latin → Cyrillic → Latin → Game text.
- Local headless suite with a mixed-script smoke pack: **11/11 passed**, including missing,
  unselected and disabled-pack equivalence, inventory/phone text and Wayne's six menu lines.
- The existing compiler/layout suite: **31 passed**; the existing native pack/font/selection checks:
  **3 passed**. Its old unsupported-character sample was changed from Russian to still-unsupported
  CJK; the new Cyrillic test suite was not added to the game repository.
- Native game rebuild succeeded. Captured Cyrillic inventory, save-phone choice and Wayne menu
  frames were visually reviewed. The test pack is not a Russian translation release.


## CJK (Chinese, Japanese and Korean)

This is font/encoding support for translator-authored packs, not a completed translation.
The compiler accepts Han (including supported supplementary ranges), hiragana, katakana,
Hangul syllables and jamo, Bopomofo, CJK punctuation, fullwidth and halfwidth forms listed in
`petext/cjk.py:supported`. It does not claim coverage of every historical Unicode CJK block.
Korean packs should normally use precomposed Hangul syllables: this renderer does not shape or
compose separate jamo into syllable blocks. Unsupported characters still fail compilation.

### Build a CJK pack

Install Pillow in the Python environment used for compilation. Supply an external TTF/OTF/TTC
font with `--cjk-font`; repeat the flag to provide fallbacks. TTC uses face zero. The compiler
checks Unicode cmap coverage before rasterizing, so missing characters are errors rather than
silently accepted replacement boxes. The first font covering a character wins. Choose the
appropriate locale font first: Han shapes differ between Chinese, Japanese and Korean fonts.

```sh
python -I localization/pelang.py build ja.csv \
  --strings localization/out/text/strings.json \
  --mod-strings localization/data/mod_strings.json \
  --lang ja --name "Japanese" --cjk-font /path/to/NotoSansJP-Regular.ttf \
  -o ja.pelang
```

Use a Chinese or Korean font for those languages. Fonts are build inputs only. Packs embed a
dictionary of glyph pixels, not a font file or reference to a machine's font path; players do
not need those fonts installed. This change includes no third-party font binary. Pack authors
must follow the chosen font's license when distributing derived glyph images (and include any
required notices). Local smoke tests used the installed Noto Sans JP/KR/SC fonts.

The glyphs use 12×12 native cells and a fixed 12 px advance, with ink and a one-pixel shadow.
Rasterization uses a shared em box to retain punctuation alignment; it must still be reviewed
at native resolution by fluent readers. This does not add CJK to the separate F1 overlay font
or translate text embedded in images. Use an ASCII/Latin display name for the F1 language row.

### Modular implementation

- `localization/petext/cjk.py`: supported ranges, sfnt/TTC Unicode coverage, rasterization and
  deterministic pack-local IDs. Only CJK compilation imports Pillow.
- `validation`, `layout`, `menu`, `wrap` and `modtext`: treat a CJK token as one glyph, four
  bytes and 12 px. Word wrapping adds CJK boundaries and basic Japanese/Chinese punctuation
  restrictions. Choice rows retain their existing protection from automatic reflow/splitting.
- `mods/pe_lang/pe_lang_pack.c`: validates both existing v1 packs and the v2 dictionary.
- `mods/pe_lang/pe_lang_cjk.c/.h`: cache state independent of SDL/game globals, with injected
  synchronization/upload callbacks for local tests.
- `mods/pe_lang/pe_lang.c`: connects the active pack, native font uploads and frame boundaries
  to the cache. Pack changes reset IDs even when the Latin/Cyrillic font mode stays the same.
- `include/pe1/mod_text.h` and `PE_HOOK_TEXT_GLYPH`: small optional encoding/render hook. Native
  menu/dialogue renderers retain packet allocation, clipping behavior, ordering, dimming,
  highlighting and their original palettes. With no provider, the original byte path is used.
- `pe_tr` and item-name fragments copy complete tokens; their existing fallback behavior remains.

### Format and compatibility

Non-CJK packs remain **v1**, byte-for-byte compatible with the previous compiler. CJK packs use
**v2** and require this runtime (older runtimes reject them). The first 64 bytes keep the v1
field positions, with version=2. Four additional little-endian u32 fields at offsets 64/68/72/76
are the absolute glyph-dictionary offset, glyph count, record size=76, and reserved=0. Entries
start at byte 80; their layout, ordering and text-relative offsets are unchanged. Text ends at
the dictionary offset, not the file end. The dictionary exactly fills the remaining file.

Each record contains a u32 Unicode scalar followed by 72 packed 4bpp bytes (12 rows, six bytes
per row; left pixel in the low nibble). Values are 0 transparent, 1 ink, 2 shadow. Records have
strictly increasing unique scalars. The reader checks lengths, offsets, record stride, reserved
field, scalar validity, pixels and every token index before exposing any text.

The glyph token is `F1 C5 hi lo`, with `hi`/`lo` both below 240 and dictionary index
`hi*240+lo`. Maximum dictionary size is **57,600 glyphs**. The magic prevents native rename-grid
F1 bytes from being mistaken for tokens; operands contain neither terminators nor commands.
The compiler supplies the ordinary FF/F9 page endings and keeps control-sequence validation.
Existing Latin/Cyrillic codes and v1 selection identities are unchanged.

The cache uses only originally empty cells **250..440** of the USA base font: **191 eligible
cells** on the tested disc. Cells through 249 are reserved because the menu renderer and rename
grids can address those as ordinary bytes. Area font page 1 and the base page's palette row are
untouched. Nonempty candidate cells are excluded rather than overwritten. The whole dictionary
stays in CPU memory; successive scenes can use different cachefuls.

Cells referenced by the current frame are pinned. A miss waits for the previous GPU frame
before uploading, then uses an unpinned slot; frame completion clears pins for the next build.
If a frame needs more distinct CJK glyphs than eligible cells, the extra glyphs show `?` and a
single warning is logged, rather than overwriting a glyph still referenced by a visible packet.
The original font page is restored when choosing Game text. Native byte buffers, layout limits
and packet-pool limits still apply; four bytes per glyph means fewer characters in fixed byte
fields. This feature does not increase menu sizes or change save formats.

**Player names and rename grids:** CJK default player names (`sys/030`) are rejected, because
pack-local IDs must not be persisted into the save format's eight-byte name field. Keep Aya's
original name. The fixed one-byte rename grids also reject CJK translations. Weapon/armor
rename defaults use `Text_LookupEditableName` (the existing lookup hook with `persistent=1`):
if the translated default contains CJK IDs, `pe_lang` leaves the original native name in that
editor. Inventory/equipment display names remain translated. This prevents pack-local IDs
from being copied into saved equipment names. Existing translated
dialogue may still insert the saved original name with `{name}`. These checks are not bypassed
by `--allow-overflow`. Future CJK name input needs a separately designed stable save encoding.

### CJK verification

All new test source, packs, fonts previews, native executables and captures remain in the local
`pe-native-debug` repository, which has no remote. No CJK test suite is added to this repository.

- Eight local Python checks: Japanese/Chinese/Korean encoding and widths; explicit font and
  actual font coverage; malformed tokens; punctuation-aware wrapping; control validation;
  saved-name/rename-grid guards; mod-label contracts; v1/v2 serialization and index bounds.
- Native cache/parser fixture: a 250-glyph dictionary across frames, pinned-slot exhaustion,
  eviction, upload synchronization, occupied-cell exclusion, upload failure, corrupted v2
  headers/dictionary/pixels/tokens, and skipping token operands when detecting Cyrillic.
- Production provider/switch fixture: measurement performs no upload, uploaded pixels match
  the dictionary, switching between two CJK dictionaries clears old IDs, and switching through
  Spanish to Game text restores original pixels, menu metrics and dialogue widths exactly.
  Editable-name lookups reject pack-local IDs while display-only lookups retain translations.
- Local headless suite: 11/11 passed with a mixed CJK smoke pack. Missing, unselected and
  disabled packs remain pixel-identical to the baseline; inventory names, save-phone prompts
  and Wayne's translated mod rows render, and all six Wayne menu lines remain present.
- Existing suites: 31 Python compiler tests and three native pack/font/selection tests pass.
  The native mod build succeeds. The Spanish pilot retains its previous v1 bytes.

The smoke pack is not a published translation. It retains the Spanish pilot's two unproven
phone-layout warnings (`dlg/M026/006` and `dlg/M290/006`); those pages still need in-game review.


## Latin American Spanish expansion

The active Spanish pack is `localization/pilots/es-419/`, with separate CSV modules for native
menus, names, descriptions, effects/help, Wayne fragments and interface ownership areas. See
[pilot scope, build steps, vocabulary and verification](../localization/pilots/es-419/README.md).
The original `es.csv` remains as a historical pilot; the new pack has identity `es-419` /
`Español latino` and does not replace a user's selected language automatically.

`pe_localize.h` exposes optional display-only UTF-8 translations through `interface_text` v1.
`pe_lang_text.c/.h` supplies a bounded game-glyph/UTF-8 bridge and per-pack label cache. Existing
v1/v2 pack formats and the explicit `translations` v1 API remain compatible. Shared printf
contracts and Unicode-header generation live in `petext/interfaces.py`. Native/F1 renderers opt
in at presentation boundaries; settings paths, save IDs and collection keys are not translated.
New tests, test shims, packs and visual evidence stay in the local debug repository.
