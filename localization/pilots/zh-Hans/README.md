# Simplified Chinese rendering pilot

This is a small, playable opening-scene pilot, not a complete Chinese translation.
It includes opening dialogue and the original two-choice save/item prompts.
Untranslated pages retain the disc's original text.

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

The pilot was compiled and rendered by the actual game with a hidden GPU window.
Automated compiler tests also cover Chinese choices and punctuation using Noto
CJK on CI. Its limited translation coverage is independent of renderer support.
