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
| French | Submitted pack (`localization/`), not shipped; inventory labels fit the original menu geometry. Dialogue layout review remains incomplete |
| Spanish pilot | Submitted pilot, not shipped; untranslated keys keep the original text. Needs Spanish-speaker review |
| What a release holds | No translated files. Players put `.pelang` packs in `lang/` themselves (`PE_BUNDLE_LANGUAGE_PACKS`, off by default, compiles the submitted ones beside the game for work on them) |
| Mod-owned text (`pe_tr`) | Explicit-key fragments plus optional UTF-8 interface translation, checked printf templates, Catalog/shops/inventory/collection integration |
| Battle messages compiled into the port (Scan, running away) | Keyed as `battle/NNN` (table 20), imported from fan patches; not yet played |
| Text drawn into images | Not keyed |
| Accented letters (Latin Extended: French, German, Spanish, Italian, Portuguese, Polish, Czech, Turkish) | Done |
| Japanese semantic proofreading | Pending human review; extraction/alignment does not establish translation accuracy |
| Cyrillic | Russian, Ukrainian and Belarusian alphabets supported; modular authored glyphs and per-pack activation |
| Chinese, Japanese, Korean | Optional external-font compilation, v2 glyph dictionaries and a native cache; see CJK scope and limits below |
| Slovak, Romanian, Croatian, Serbian, Icelandic, Lithuanian, Latvian, Vietnamese, Greek, Macedonian, Kazakh | A letter table in the pack, one byte a letter; see Letter tables below. Not yet played. Greek letters, Icelandic ð þ and a few Serbian, Macedonian and Kazakh letters show `?` until someone draws them (see Letters still to draw) |
| Hebrew, Arabic | Right to left, Arabic joined; see Right-to-left text below. Sample packs only; the letters show `?` until someone draws them |

`python -I localization/pelang.py languages` lists every language the project means to support
(`petext/alphabets.py`) and which letters each still lacks. Persian and Urdu still lack letters;
Hindi, Bengali and Thai need new text layout as well as letters.

To see a language's letters in the game before anyone has translated it, make a test language: the
game's own tags and page breaks with that language's words in place of the text.

```sh
python -I localization/pelang.py sample localization/out/text/strings.json pl -o out/pl-test.csv
python -I localization/pelang.py build out/pl-test.csv --strings localization/out/text/strings.json \
  --lang pl --name "Test Polish" --allow-overflow -o out/pl-test.pelang
```

Name several codes to mix languages in one pack. It stays on your machine like any pack built from
`strings.json`.

French uses abbreviated inventory labels where the original menu reserves space for ammunition
counts, and `J.` for the save slot's day label. Labels baked into images, such as **Level** and
**Time** on the save screen, still use the original English graphics. Compiler warnings about
unresolved dialogue boxes remain review items; successful compilation does not prove their
in-game layout.

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
   room MNNN, page PPP), `battle/NNN` for the battle messages (below). The 148 table slots that
   hold filler the game never shows are left out.
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
     - the default name `sys/030`: 8 letters;
     - battle messages (`battle/NNN`): one line, 300 px (they draw from x 20 to the screen edge).

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
   `pe_mods.ini`), and start the game (built with `pe_lang` in `PE_MOD_LIST`). The first start
   that finds packs with no language chosen yet asks which one to use, once, in the Mods
   panel's look and before the game reads anything from the disc (`pe_menu_choose`). With no packs
   it never asks. The language in use is fixed when the game starts: F1 >
   Language saves the choice for the next start and says to restart. Language packs don't need
   All mods: they are the one mod that switch doesn't hold back, and their own row in F1 > Manage
   mods turns them off.

`pelang.py pseudo` makes a test language from every entry: letters swap case, and each entry is
marked with `!` and padded about 30%. Build it like any pack (`--lang qps`) to see what text a pack
reaches and where longer text overflows. Use `--allow-overflow` when compiling this deliberately
expanded test text. Extracted rows containing unsupported control bytes are left untranslated
with a note, retaining the game's own text rather than bypassing validation.

Language selection is stored separately in `pe_lang_selection.dat` beside `pe_mods.ini`, using
the pack's language code and display/edition name. Filenames and list positions are not identity.
Keep these two header fields stable across pack updates; give different editions distinct names.
An identical identity in the user folder overrides the copy beside the game. If a selected pack
is missing, English (the game's own text, the first row under F1 > Language) is used and the
identity is retained until a new selection is made.
Old numeric `Language/Language` preferences are intentionally ignored: select your language once
after upgrading, because the old index cannot establish which pack was intended.

## Translation files

What a player downloads is a `.pelang` file. It is one of two things, told apart by its first
bytes, so both keep the one ending:

- **A pack**, exactly as `build` writes it (below).
- **A zip** holding that pack as `text.bin`, written by `bundle`, and its pictures with words in
  them as `pictures.bin` when it has any. Other parts are left alone. `readme.txt` is for the
  player: credits, version, notes.

```
python -I localization/pelang.py import-patch disc1.cue cd1.ppf -o es1.csv --report es1.json --pictures es1.bin
python -I localization/pelang.py bundle fr.pelang -o "French.pelang" --readme credits.txt --pictures es1.bin --pictures es2.bin
```

**Pictures.** A fan patch's pictures with words (signs in rooms, main menu words, menu headings)
come across as pieces of PE.IMG sectors (`localization/petext/pictures.py` describes the format).
Each piece holds the patch's bytes and the CRC-32 of the English bytes it replaces. While the
language is in use, `pe_lang` puts a piece in as the game reads that sector from the disc
(`PE_HOOK_DISC_READ`, in `CdRom_ReadSectorsFromLba`), and only where the bytes it finds there have
that CRC. So a piece never lands on another disc version, and Disc 1 and Disc 2 pieces share one
file. Picture data means each room's first two runs (both picture bundles), entries 9 and 76,
and every TIM in the other entries except the letters page at VRAM 320,0, which a translation
file's letters already replace. Everything else a patch changes goes in the import report under
`pictures.changes_left_out`, never into the file. A picture already on screen keeps its look until
the game reads it again: the title screen's words follow the language picked at start.

`bundle` writes the same file each time for the same pack. The game opens zips of up to 256 MB,
with a pack of up to 64 MB inside. It refuses a zip that is damaged (its checksum is tested), has
no `text.bin` at its top level (names match exactly), or holds an encrypted pack.

**F1 > Language > Add a translation** opens a file window for `.pelang` files. The chosen file
is checked first, then copied as-is into `lang/` in the user folder and saved as the language
for the next start; the toast says to restart. A file whose language code and name match a pack
already in the list replaces it, which is how an update installs (the pack in use keeps drawing
from the copy the game started with). A file that can't be read is not copied, and the toast says
why. If the copy fails, nothing is added. The row isn't on PS5,
which has no file window. Zipped files need the zip reader that desktop builds have
(`PE_DESKTOP_MOD_DOWNLOADS`). Without it, plain packs still load and zips are skipped with a log
line.

## How the text is swapped

The mod never changes the game's data. At the moment the game takes a piece of text, a hook hands
the mod the pointer, and the mod gives back a pointer into the pack: the loaded text, the scripts
and the saves stay as they are, and turning a pack off (or leaving an entry out) lets the original
through.

- **Text tables.** `Text_LookupTable4` / `8` / `C` / `10` and the map location names send
  `PE_HOOK_TEXT_LOOKUP` with the table (4, 8, 12, 16) and index; keys `sys/`, `item/`, `desc/`,
  `misc/`.
- **Battle messages.** A few lines live in the boot program, not the text tables, and are
  compiled into the port (`src/main/text/tables.c`): the Scan panel's HP line and the enemy's
  weaknesses, and the two lines when running away fails or isn't allowed.
  `Text_LookupBattleMessage` sends `PE_HOOK_TEXT_LOOKUP` with table 20 and a `BATTLE_TEXT_*`
  index when the English one is shown; keys `battle/000`-`010`, numbered in the order they sit in
  the boot program (`localization/petext/battle.py`):

  | Key | What it is |
  |---|---|
  | `battle/000` | Scan: HP not known yet |
  | `battle/001` | Scan: HP and its maximum (two `{num}`, which a translation must keep) |
  | `battle/002`-`008` | Scan: weak to poison, acid, sleep (tranquilizer), heat, cold; items easily stolen; easily confused |
  | `battle/009` | Running away failed |
  | `battle/010` | Running away isn't allowed in this fight |

  The textbox draws straight from the pointer it gets, so nothing is copied into a fixed buffer:
  the only limit is the one line from x 20 to the screen edge.
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

**The Japanese discs themselves** can also be played (pe1/pe_image.h, `g_GameRegion`): the game reads
their executable's tables, shows the rooms' Japanese bank with the language index at 0, and draws
the Japanese font with the Japanese box rule (codes 10-4B with their own spacing, everything else,
two-byte letters included, as 12 px cells on a 14 px pitch; 14 px lines). Language packs are never
used with them: `pe_lang` keeps its saved choice but answers none of its hooks, asks nothing, puts
no letter on the font page (whose cells past 4C hold the kana and kanji) and no picture into the
disc reads. The extraction tools below read the Japanese discs as before.

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
- `ctest -R mod-lang-bundle`: plain and zipped `.pelang` files, pictures put in only where the
  English bytes are, and damaged zips or pictures refused. `test-mod-lang-bundle file.pelang ...`
  opens any files and says what each holds; `--apply file.pelang in out` runs the picture swap
  over saved sectors.
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
translation text as a format string. The host disables the service when pe_lang is off.
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


## Letter tables

The single-byte codes 0x4D-0xEF are all spoken for by the Latin Extended and Cyrillic sets, but no
one language uses them all. `petext/letters.py` holds a catalog of further letters, and a pack can
give the codes its own text doesn't use to the ones it needs. Nothing to do when building: `build`
reads the translation and makes the table.

Letters stay one byte each, which matters. The game's text fields are fixed: 31 bytes for an item
message, 63 for a dialog line. The four bytes of a CJK token would leave seven letters of Greek in
the first and fifteen in the second. It also means widths go in the game's own tables, so wrapping,
and menus treat these letters like any others.

A letter comes one of three ways. No game art goes in a pack, so a letter built on the game's own is
a recipe, carried out on the player's font page when it is uploaded:

- **base**: the game's letter as it is. A Greek capital Alpha is the game's A, but it stays a real
  letter;
- **marks**: the game's letter plus up to three marks, each a list of points in the record;
- **art**: drawn in `letters.py`, or built there on a drawn letter.

### Letters still to draw

Letters drawn from nothing for this game came out hard to read, so they are placeholders for now:
each draws as a question mark (`PLACEHOLDER_LETTERS` in `letters.py`, and every Arabic shape). They
are Greek (Γ Δ Θ Λ Ξ Π Σ Φ Ψ Ω, the small letters the game has no look-alike for, and those with
accents), Icelandic ð þ Þ, Serbian and Macedonian Ђ Ћ Љ Њ Џ, Kazakh Ә ү ұ, Hebrew and Arabic.
Everything else already works on them: the pack, name letters, right-to-left order and Arabic
joining. To give one its real picture, for example from a fan translation's own letters, add it to
`ART` and take it out of `PLACEHOLDER_LETTERS`; an Arabic shape needs a joining stroke on row 7 at
each edge it joins.

A pack with a table is **v3**. Its header is 88 bytes: the v2 fields, with a glyph count that may be
0, and two more u32 at offsets 80 and 84, the table's offset and its letter count (at most 240 with a
code: 163 of the pack's own and the redrawn game letters below them, see below). The
table comes last and fills the rest of the file. Packs with no table are unchanged. Each record is
80 bytes, by strictly rising code:

| Bytes | |
|---|---|
| 0-3 | Unicode character |
| 4 | Code, 0x4D-0xEF; below 0x4D only for art of the game's own letter on that code (a redrawn letter); 0 for a letter with no code (v4, see below), except a first record whose letter is `0`, the game's code 0 |
| 5 | Art: the advance, 1-15. Otherwise what's added to the base letter's advance and menu width, 0-8 |
| 6 | The base letter's code, below 0x4D, or FF for art |
| 7 | Steps on the base letter: 1 drop its dot, 2 turn it upside down, 4 room for a mark above an ascender. For art: the menus' metrics byte (width << 4 \| first column), or 0 to take the width from the advance |
| 8-79 | Art: 72 bytes of pixels, as in a CJK record, shadow included. Otherwise three `PeLatinMark`, 19 bytes each: point count (0 for none, at most 8), fixed rows, anchor, eight (dx, row) |

**What a code means now belongs to the pack**, so a table's code must not outlive it. The default
player name and the rename grids can't use a table's letters, and `build` refuses them, because what
the player picks there is saved. A weapon or armor rename default with one keeps the game's name in
that editor (`persistent` in `PE_HOOK_TEXT_LOOKUP`), as for CJK. One limit: the bytes of a name saved
while a pack was in use are read with whatever pack is in use later, so accented letters in a saved
name can change if the next pack gave those codes away.

The English rename grids also hold raw codes from 0x4D up, which English draws as empty cells
(`sys/117`'s `{g4D}`, between the comma and the question mark). With a pack in use such a cell would
show the pack's letter on that code and save it, so `pe_lang` hands the game the grid, the pack's or
the English one, with those cells as spaces (`pe_lang_grid_blank`), and `build` makes such a tag
kept in a translated grid a space (`empty_grid_cells`), not the letter the code stands for.

`pe_lang` writes table letters after the Latin and Cyrillic ones, so a letter takes over its code's
cell. Interface text uses the same table (`pe_lang_text_encode`).

**Redrawn letters (a fan patch's look).** A record whose letter is the one its code already means
(a game letter below 0x4D, or a fixed Latin Extended or Cyrillic one) is the pack's own picture of
that letter, art only: `build --patch-letters` writes them from what `import-patch --patch-letters`
read in a patch. The code keeps its meaning, so such letters may go in names, grids and saves like
the game's, and other packs read those bytes as before (`pe_lang_letter_redraws`;
`pe_lang_pack_has_letter` doesn't count them). While the pack is in use `pe_lang` puts the redrawn
game letters on the font page first, so the accented letters it makes from them are made from the
pack's pictures, and gives the dialogue box their advance with no left gap and the menus their
metrics byte; the game's page, widths and metrics come back with Game text. `build` measures with
the same widths (`gametext.PACK_DRAWN`), and the space is still pulled back 7 px in the box as the
English box pulls it.

**More letters than codes (v4).** When a pack's text needs more catalog letters than it has free
codes, `build` gives the codes to the letters used most, and first to those in text that keeps one
byte a letter (item names, `mod/` text; with `--bytes-only` also names and grids). The rest go after
the coded records with code 0, by strictly rising Unicode, and the header's count at 84 covers both
(at most 163 coded, 57,600 without). A letter with no code reaches the screen only in the text
renderer's stretches and in name letters: `pe_lang` makes it from its record, on the game's own font
page, into a free cell of the letter cache (cells 250-440, shared with the CJK dictionary) when it
is first drawn, with its own advance in dialogue; menus cut the cell as they cut the game's own
letters, from the first inked column, as wide as drawn (the `w` of `PE_HOOK_TEXT_LETTER`). While building, such a letter is written
as `F1 C7 hi lo`; no pack holds that token. `build` refuses one in text that keeps one byte a letter,
and `--bytes-only` keeps the old limit of 163.

`python localization/preview_letters.py DISC.cue` draws every letter from the game font on your disc,
with a line of each language, for choosing shapes. It needs Pillow, so run it without `-I`. To add a
letter, add it to `letters.py` and run `pelang.py latin-header`, which also regenerates
`tests/pe_lang_letter_vectors.h`: the C composer has to match Python pixel for pixel.

## Right-to-left text

A pack whose language code reads right to left (`he`, `ar`, `fa`, `ur`, `yi`, `ps`, `sd`, `ug`,
`ckb`, `dv`, or any code with the script subtag `Arab`, `Hebr` or `Thaa`) is drawn a line at a time
by the game's text renderer (`PeText_LineFinish` in `src/main/draw/text_render.c`). The pack format
doesn't change: `pe_lang` reads the direction from the code given to `build --lang`.

- Hebrew and Arabic run right to left. English words and names (Aya, Carnegie Hall), numbers with
  their signs and separators (1997, 12:30, 50%), and the box's number command keep left to right
  inside the line; spaces and punctuation between take the direction around them; brackets turn
  round. The rules are the Unicode bidirectional algorithm, simplified for one line of
  right-to-left text.
- Arabic letters take their joined shapes (alone, start, middle, end; the Unicode presentation
  forms) and lam then alef becomes one letter, in the game as it draws, so a name typed on the name
  entry screen joins too. Joined letters touch: `pe_lang` cuts them to their strokes and shadow and
  the renderer closes the gap between those that join.
- Dialogue lines end at the box's right side (x 300 in the default box, mirroring its 20 px left
  margin), so `build` wraps right-to-left dialogue to 280 px. Menus keep their places: text starts
  where the game puts it, only the order inside changes.
- Vowel marks (Hebrew points, Arabic harakat) are left out, as everyday text writes them.

Write the translation in plain reading order with the base letters (not presentation forms).
`build` puts every joined shape of the pack's Arabic letters in the pack as letters with no code,
and measures lines with the letters joined (`petext/rtl.py`, which follows the renderer's rules;
`tests/text_render_rtl_cases.txt` holds cases both must pass).

The Hebrew and Arabic letters are placeholders for now and draw as question marks (see Letters
still to draw); the order and the joining are the real thing. Hebrew names save with the shared name numbers; Arabic letters didn't fit below the per-pack
numbers, so an Arabic pack numbers them as its own letters, as Chinese does.

Test languages: `pelang.py sample -o he.csv strings.json he` (or `ar`), then `build --lang he`.
The disc test `original-game-text-renderer`, part `rtl`, checks both on screen;
`original-game-text-renderer-battle` checks a battle line (Escape in the first battle).

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

**A fan patch's own letters instead of a font.** A patch for the Japanese discs draws its
characters as 12×12 pictures. Once each picture is named (`localization/import_ja_patch.py`,
then `localization/name_ja_patch_letters.py`, which writes `letters.tsv`), the pack can hold those
pictures as its dictionary, so it draws the translator's own letters and needs no font:

```sh
python -I localization/pelang.py build disc1.csv disc2.csv --from-patch \
  --strings localization/data/manifest.json --lang zh-Hans --name "Chinese (Simplified)" \
  --cjk-letters letters.tsv --cjk-pictures disc1-pictures.json --cjk-pictures disc2-pictures.json \
  --leave-out M081 --leave-out M347 -o zh-Hans.pelang
```

A character drawn by more than one picture takes the most used one (the build lists them); the
full-width space is drawn empty; Latin letters, digits and signs come from the game's own font.
Several CSVs (one per disc) may share rows only with the same text. `--leave-out ROOM_OR_KEY`
keeps the game's text for a room's dialogue or one row (debug rooms' untranslated test lines);
`--adapt CSV` replaces rows, e.g. a line whose controls the USA game writes differently.
`localization/tests/test_cjk_pictures.py` (CTest `localization-chinese-pack`) covers this; CTest
`original-game-text-renderer-chinese` checks such a pack on screen when `PE_TEST_ZH_PACK` names it.

**Its pictures with words.** The Japanese and the USA discs keep their pictures at different places,
so a Japanese-disc patch's pictures can't go by place as a USA-disc patch's do. But some pictures
are English on both releases, byte for byte the same (the title menu's words, the map's place
names). `import_ja_patch.py --disc-pictures cd1.bin` (with `--us`, once per disc) writes the ones
the patch redrew as a `pictures.bin` for the USA disc; `bundle --pictures cd1.bin --pictures cd2.bin`
puts them in the player file. A redrawn picture the USA disc draws differently is left out and
listed in the report (`disc_pictures`); `--whole-picture ENTRY/N` takes one whole all the same, when
it is the same kind and size at the same place (a title menu word drawn a little differently). The
rest is `petext/pictures.py` (`across`), tested in `localization/tests/test_pictures.py`; CTest
`localization-chinese-pictures` checks the real pack's pictures against the patched Japanese game
on both USA discs.

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

## Importing any fan patch

The port plays the original game data, so a patch for the disc can't be played as it is.
`import-patch` compares your disc with the patched one and writes the text as a translation file,
and the patch's own letters with their widths. It reads PPF (1, 2, 3) and xdelta files itself, or a
patched copy of the disc. It never runs a patcher, and it leaves your disc image alone.

```sh
python3 -I localization/pelang.py import-patch "Disc 1.cue" patch.xdelta \
  -o local.csv --report local-report.json --patch-letters local-letters.json
python3 -I localization/pelang.py build local.csv --strings localization/data/manifest.json \
  --from-patch --submission --patch-letters local-letters.json --lang es --name "Español (parche)" \
  -o es-patch.pelang
```

Put the `.pelang` in `lang/` and the picture in `mods/fonts/`, then choose them under F1 > Language
and F1 > Fonts. Either disc gives the same text. Nothing here goes into the repository.

- **Letters** are worked out from the patch's font, so a new patch needs no code: a copy of a
  letter (coloured speaker sets), or a base letter plus a mark. The report lists what was found.
  Name anything it couldn't with `--letters 5A=Ñ`; pages using an unnamed symbol keep the English.
- **Letters' look**: with `--patch-letters` the import writes each letter the patch draws its own
  way (the game's letters it redrew, its accented letters) as its picture, its dialogue advance and
  its menu metrics, from the patched font page, the patched boot program's width table (the game's
  two bytes a letter, or the one byte a letter some patches switch the code to, however the patch
  writes the moves that switch it: the Spanish and Italian patches write them differently) and the
  patched menu metrics. `build --patch-letters` puts them in the pack as redrawn letters (see Letter tables)
  and lays the text out with those widths, so lines break where the patch broke them. The file
  holds the patch's art, so like the rest it stays out of the repository.
- **Symbols of the patch's own** (a wrench for "Tool"): name the code like a letter, e.g.
  `--letters 98=🔧`. With `--patch-letters` the symbol's picture and widths go in the letters file
  and `build` gives it a code of the pack's table, so the pages using it come in too.
- **Controls**: `--from-patch` lets waits, names and timing differ from the English, because a
  translator moves them and the patch played that way. Choices must match, since scripts act on them.
- **How a box closes**: where the patch changed a dialogue page's end (the game's F9 closes the
  box by itself, FF waits for a button), the row ends in `{end FF}` or `{end F9}` and `build` uses
  that end instead of the game's. Any translation file may use it, on dialogue pages only.
- **Name entry grid**: the patch's grid (sys/115-117) comes in when it changed, letter for letter,
  as name letters; a game symbol the English grid has too stays, and empty cells past the 99 the
  game reads are dropped.
- **Space**: a patch's own font is often narrower than the game's. Without `--patch-letters`
  pages are rewrapped to fit the game's widths. A message box line too wide for the screen goes on two lines instead, split at
  the space that leaves the longer half shortest (a stretch holding only U+10FE7F,
  `PE_TEXT_LINE_BREAK`, marks the break; the box grows by a line). The card slot line and "is not
  formatted" go on two lines when both slot lines are translated. Any other menu line too wide
  for its place, or a line with no space to split at, is drawn narrower to fit (the report says
  how much): the room comes from its spaces and the gaps between letters first, and letters are
  drawn narrower only for what is still missing. Text the game joins (a card slot and "is not
  formatted") is narrowed together. A line that would need less than 75% keeps the game's text.
  The pack carries this as the stretch's first letter, U+10FE80 + k for (128 + k) / 256 of the
  width (`PE_TEXT_NARROW_FIRST`); a build without `--from-patch` still stops at a line too wide,
  so a translator can shorten it instead.
- **Battle messages** come from the patched boot program (`petext/battle.py`). A patch may move
  them and point the code at the new place, or keep the Japanese beside its own lines, so the
  importer takes the lines in that part of the boot program in order, leaves out any that is
  still one of the original Japanese ones, and expects one per key. If the count is off, all of
  them keep the English and the report says why. The HP line keeps its numbers even with
  `--from-patch`.
- **Not carried over**, all listed in the report: edited room scripts (`rooms_with_edited_scripts`
  should be empty; if not, those rooms won't match the patch), pictures with text in them
  (`rooms_with_edited_pictures`), and symbols with no letter named for them. Mod labels stay in English
  because no disc patch has them.
- **Checked on screen**: CTest `original-game-text-renderer-spanish` plays the Spanish pack
  (`PE_TEST_ES_PACK`, the zipped player file) on USA Disc 1 and, with `PE_TEST_DISC2`, Disc 2 (a
  Day 6 sample save from `save-editor/samples` loads there). The first box, the boxes on the ship
  and the name entry's grid light exactly the pixels of the pack's letter pictures at its widths;
  the title screen and the Day 6 title card are pixel for pixel the frames of a disc copy with the
  pack's picture pieces written in. Without the pack it is skipped.

## Local Italian text import

Download the original [SadNES cITy Italian patch](https://www.sadnescity.it/traduzioni/pe/pe.php)
and extract `itpch_pe_v090.ppf`. The importer reads that patch and an unmodified USA Disc 1 or
Disc 2 image locally; it does not run the bundled patcher or modify the image.

```sh
python3 -I localization/pelang.py import-italian "Disc 1.cue" itpch_pe_v090.ppf \
  -o italian-local.csv --report italian-local-report.json
python3 -I localization/pelang.py build italian-local.csv \
  --strings localization/data/manifest.json --submission --lang it --name Italiano -o it.pelang
```

Use new CSV and report paths. The report lists pages that retain the original text because
translated pages are missing or their control commands differ. Original page IDs and text
hashes must match the US catalog; an unknown patch, region or glyph fails the import.
The six accented letters are mapped from the patch's original font into the port's Latin font.
The v0.90 M026 bank has an extra newline between page records. The importer
accepts that separator for this verified Italian profile, recovering the following
18 pages; original banks and other profiles retain strict contiguous parsing.
Three exact v0.90 pages also add name references absent from the original. The
importer omits those added addresses and lists them in
`dialogue_control_adaptations`; existing name commands, waits and choices still
pass the original strict control checks. Other text variants are not rewritten.
The exact M271/014 patch-only pause is also omitted without changing its words;
the native default box fits the rewrapped four-line text. Original pauses remain
required, and the adaptation is included in the import report.

This imports dialogue and the resident menu labels, names and descriptions. Native rename
glyph grids remain original. Eight reviewed label abbreviations preserve the original inventory
name/ammo-count boundaries and combined memory-card notice width; the report records each change.
Compiler layout warnings still need in-game review. Install the resulting pack in `lang/` and
select Italiano with Language packs enabled.
Credits: Mickey, _Ombra_ and Chop, SadNES cITy. The repository does not include their patch or
translated text; each user supplies the original patch for this local conversion.

## Local Spanish text import (partial)

Download the **Chicago (02-02-2025)** patch from the
[translation author's page](https://parasiteevetranslationproject.blogspot.com/2025/01/nuevos-parches-disponibles-con-sorpresa.html).
Apply it to a separate copy of your USA Disc 1 or Disc 2 using the author's patcher, keeping
your original image. The importer accepts that exact translated image, checks its size and
SHA-256, and reads both images without modifying either. The DS font variant is not supported.

```sh
python3 -I localization/pelang.py import-spanish "Original Disc 1.cue" "Spanish Disc 1.bin" \
  -o spanish-local.csv --report spanish-local-report.json
python3 -I localization/pelang.py build spanish-local.csv \
  --strings localization/data/manifest.json --submission --lang es --name Español -o es.pelang
```

Either disc produces the same text pack. Use new CSV and report paths. Install `es.pelang`
in `lang/` and select Español with Language packs enabled. Continue playing from the unmodified
images: the patched executable and scripts are never used by the port.

This is a **partial import**, not a complete Spanish release: 2427 text rows are imported;
161 entries are reported as not imported, including a patch-only page. Changed waits, names,
choices and other control commands remain explicit fallbacks to the original text. The compiler
also reports 299 warnings requiring layout/content review; compilation alone does not verify them.
Unknown custom symbols and native rename grids remain original.

The importer maps Chicago's accents and yellow speaker letters into the original port font,
keeps original speaker colours, and removes the patch-only opening credits and repeated line.
Credits are retained in the report. Two reviewed memory-card label abbreviations fit the original
menu fields and are recorded in that report. No patch, disc data or imported translation is
included in the repository. Translation: Green_goblin and TheMarkusBoy; ROM hacking: CUE, Ombra,
Chop and Phoenix.
