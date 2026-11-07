# Simplified Chinese rendering pilot

This is a small, playable opening-scene pilot, not a complete Chinese translation.
It includes 12 opening-dialogue/save/item prompts and 104 resident menu,
inventory and memory-card labels (116 entries total). Most story pages, item
names/descriptions and text baked into graphics still retain the original text.

Build it using a licensed Chinese font you supply, for example Noto Sans CJK SC:

```sh
python -I localization/pelang.py build localization/pilots/zh-Hans/pilot.csv \
  --submission --strings localization/data/manifest.json \
  --lang zh-Hans --name 'Chinese (preview)' \
  --cjk-font /path/to/NotoSansCJKsc-Regular.otf -o zh-Hans.pelang
```

Pillow is needed only when compiling CJK packs. The game has no font-installation
or Python requirement: the pack contains its own small glyph dictionary. Place
`zh-Hans.pelang` in `lang` beside the game or in the preferences folder. Enable
Language packs and select Chinese (preview) under F1 → Language. The mod host must
be enabled; other gameplay mods can stay disabled.

Chinese characters, full-width punctuation and the ellipsis are supported.
The compiler preserves choices, variable substitutions and script commands, and
wraps Chinese between characters while keeping closing punctuation with the
preceding text. Missing font coverage is an error, never silently a tofu glyph.
The same pipeline accepts traditional Chinese with an appropriate font and text.

The localization CI artifact includes `zh-Hans.pelang` and the font license. It
uses a pinned, checksum-verified Noto Sans CJK SC font; this preview is still not
selected or bundled by the standard game build.

The pilot was compiled and rendered by the actual game with a hidden GPU window.
The menu expansion was checked at 95 native replay snapshots: complete game states
matched the opening-only pilot, with 29 frames changing visually. This is menu
coverage, not a full-campaign or translation proofreading claim.
Automated compiler tests also cover Chinese choices and punctuation using Noto
CJK on CI. Its limited translation coverage is independent of renderer support.
