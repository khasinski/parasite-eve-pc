# Translating Parasite Eve

How to add or improve a language for the `pe_lang` mod. You need your own copy of the game (the
US discs, and the Japanese discs if you want the Japanese text beside the English), Python 3.11
or newer, and a spreadsheet program. No programming.

**The one rule:** the game's own text never goes into the repository. You extract it from your own
discs, translate with it on your machine, and submit only your translations: a file of `key`,
`translation`, `notes`. The tools make that file for you, and CI refuses one with English or
Japanese in it.

## 1. Get the text from your discs

```
python -I localization/extract_text.py "Parasite Eve (USA) (Disc 1).cue" "Parasite Eve (USA) (Disc 2).cue" --ja "Parasite Eve (Japan) (Disc 1).cue" "Parasite Eve (Japan) (Disc 2).cue"
python -I localization/pelang.py template localization/out/text/strings.json -o work-de.csv
```

`--ja` is optional. `localization/out/` is ignored by git: it stays on your machine. If the
language already exists, carry its translations into your working file:

```
python -I localization/pelang.py template localization/out/text/strings.json -o work-de.csv --merge localization/translations/de.csv
```

## 2. Translate

Open `work-de.csv` and fill the `translation` column. An empty translation keeps the game's text,
so you can submit a language a part at a time.

| Column | What it is |
|---|---|
| `key` | Which text: `dlg/M001/009` is page 9 of the message bank first used in room M001; `item/`, `desc/`, `sys/`, `misc/` are the menus' item names, descriptions, system messages and the rest |
| `english`, `japanese` | The game's text, for reference (from your discs) |
| `speaker`, `before`, `after` | Who speaks, and the pages before and after in the same scene |
| `box`, `widest_px` | Where it is shown and how much room there is |
| `notes` | Yours: `AI` for a machine draft, `reviewed` once a native speaker has checked it |

**Keep the tags.** `{br}` line break, `{wait}` next screen (a button press), `{name}` Aya's name,
`{choice 3}` a choice of 3 (the three option rows follow, one per line), `{red}`…`{/red}` highlight,
`{num}` a number, `{pause}` and `{fb07 NN}` timing. Waits, choices, names, numbers and pauses must
stay in the same number and order; line breaks may move.

**Room.** Dialogue that doesn't fit is rewrapped for you (line breaks at spaces, and an extra
screen if needed). Menus can't be: item names have 104 px (86 for guns), dialog messages 63 bytes,
a stolen item's name and its message 31 bytes together. The `box` column says what each row has.

**Letters.** The font has A-Z, the Latin accented letters of the western, central and northern
European languages, Russian/Ukrainian/Belarusian Cyrillic, and CJK (built with `--cjk-font`).
A character without a glyph is an error, with the line it's in.

**A glossary.** Keep the terms that recur (characters, Parasite Energies, items, places) in
`localization/translations/de.glossary.csv` with two columns, `english,translation`, terms only,
never whole lines. The build warns where a line uses a term differently, and where a line names
an item differently from that item's own translation.

### With AI

A machine draft is a fine first round. Give it a scene at a time, not lines on their own: the
rows of one bank in key order, with `speaker`, `before` and `after`, both the English and the
Japanese (the Japanese is the original, the English the US localization; they differ on purpose
in places, and the Japanese came from the discs by OCR, so rare kanji can be misread), the
glossary, and these rules: keep every `{…}` tag exactly, keep the option rows after `{choice N}`,
respect the `box` limits, translate meaning and tone rather than word for word. Mark the rows `AI`
in `notes`. Then build (step 3) and fix what it reports. A native speaker should review the
language before it is released; they change `AI` to `reviewed`.

## 3. Check and play it

```
python -I localization/pelang.py build work-de.csv --strings localization/out/text/strings.json --lang de --name "Deutsch" --glossary localization/translations/de.glossary.csv -o de.pelang
```

Errors stop the build (a missing glyph, a changed tag, menu text past its room); warnings are for
you to look at (rewrapped pages, glossary differences, a row left in English). Put `de.pelang` in
`lang/` beside the game, start with All mods on, and pick it under F1 > Language.

## 4. Submit

```
python -I localization/pelang.py export work-de.csv -o localization/translations/de.csv
```

Add the language's name to `localization/translations/languages.json` the first time
(`"de": "Deutsch"`). Then commit `de.csv` (and the glossary, if you made one) on a branch named
`lang/de` and open a pull request with the translation template:
`...compare/main...you:lang/de?template=translation.md`.

CI builds every language from `localization/data/manifest.json`, a file with the shape of the
game's text (its tags, lengths and boxes) and none of its words, so it needs no discs. It refuses
a file with source text in it, and attaches the packs and reports to the run.

## For maintainers

After a change to the extractor or the game's text tables, regenerate the manifest from the US
discs and commit it:

```
python -I localization/pelang.py manifest localization/out/text/strings.json -o localization/data/manifest.json
```

The build from it is byte for byte the build from the discs' `strings.json` (a test checks it).
The `localization` workflow posts the summary and packs to Discord when the
`DISCORD_WEBHOOK_LOCALIZATION` secret is set (a channel's Integrations > Webhooks).
