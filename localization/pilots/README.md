# Spanish pilots

The active expanded draft is [Latin American Spanish](es-419/README.md): 1,349 entries covering
menus, items and player-facing mod interfaces. The section below documents the historical pilot.

## Original pilot

`es.csv` is a partial Spanish draft with **174 entries**: 103 system/menu/save strings, 21 item
names, 30 dialogue pages (opening, phone/inventory choices and Wayne's storage menus), and 20
Wayne/Workshop mod labels. Accents, ñ, inverted punctuation and Sí exercise the existing Latin font.
This is not a full-game translation, and a Spanish speaker should review wording and choice
meanings before a public language release. Item descriptions and most story dialogue fall back to
English. Cyrillic/CJK, image text and Japanese proofreading are outside this pilot.

The CSV contains only stable keys and Spanish translations, with no extracted English/Japanese
reference columns. Extracted originals and compiled packs remain in ignored local directories.

From the repository root, after extracting your own discs to `localization/out/text`:

```sh
python -I localization/pelang.py build localization/pilots/es.csv --strings localization/out/text/strings.json --mod-strings localization/data/mod_strings.json --lang es --name "Español - piloto" -o localization/out/es-pilot.pelang
```

The pilot builds with strict control/layout validation, without `--allow-overflow`: **zero errors** against the local extracted USA font metrics. Two save-phone pages,
`dlg/M026/006` and `dlg/M290/006`, retain unknown-layout warnings because no decoded script ties
them to a definite box; their box fit still needs an in-game check. The other entries have no
compiler warnings. The original choice counts,
waits, inserted-name controls and option order are retained; semantic proofreading is still needed.
Re-extract after changing disc sources; do not bypass errors from a different source revision.

To translate additional mod labels, generate a template with the same `--mod-strings` manifest:

```sh
python -I localization/pelang.py template localization/out/text/strings.json --mod-strings localization/data/mod_strings.json --merge localization/pilots/es.csv -o localization/out/es-template.csv
```

Copy the compiled `es-pilot.pelang` to `lang/` beside the localization executable, or to `lang/`
beside the user preferences. Build with `pe_lang` in `PE_MOD_LIST`, enable All mods and Language
packs, then select **Español - piloto (es)** in F1 > Language. It is not selected automatically.
Choose Game text to restore the original text. Keep the language code and edition name stable so
the saved selection follows pack updates.

The `pe-native-debug` harness keeps test sessions, cards, logs and screenshots local. Its existing
`scenarios/lang_suite.py` checks baseline equivalence with a pack installed but not selected,
selected but missing, pe_lang disabled and All mods disabled; it also checks the translated
screens and Wayne's extra rows. New mod-label compiler/helper checks are local in
`tools/test_mod_translation.py` and `tools/pe_tr_test.c`.

For this partial pilot, run the local scenario suite with its coverage expectation stated explicitly:

```sh
python -I scenarios/lang_suite.py --exe "D:/Recomp Games/pe-localization/build-mods/parasite-eve-pc.exe" --pack "D:/Recomp Games/pe-localization/localization/out/es-pilot.pelang" --unchanged-text menu
```

Run that command from `pe-native-debug`. `--unchanged-text menu` requires the untranslated Aya
status screen to remain identical, while every other captured text screen must change. The
status screen is mostly native image labels and equipment names kept in English by this pilot.
Without that option, the suite still expects a full-coverage/pseudo pack and tests every text
screen for a change. Off/missing/unselected comparisons remain byte-for-byte checks in both modes.
