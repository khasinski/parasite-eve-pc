# Parasite Eve Translator

A window for translating the game into a new language. It opens the English pack, shows the game
line by line, checks each line you type with the port's own pack compiler, and saves one `.pelang`
that the game loads.

```
python -I localization/translate.py
```

It needs Python 3.11 or newer with Tk (the standard Windows and macOS installers have it). No disc
is needed for text.

Or the download, which needs no Python: one program file for Windows
(`Parasite-Eve-Translator-windows-x86_64.exe`), one app for macOS (`...-macos-arm64.zip` for Apple
chips, `...-macos-x86_64.zip` for Intel) and one program for Linux (`...-linux-x86_64.tar.gz`). The
`Translator` workflow builds them. They are not signed, so Windows SmartScreen and macOS Gatekeeper
warn the first time: on Windows "More info", then "Run anyway"; on macOS right-click the app, then
"Open".

## Using it

1. **Open English pack.** The English pack is the game's own text as a `.pelang`, made once from
   your own discs (`pelang.py english`, see `docs/TRANSLATING.md`). The Translator refuses a file
   whose lines are not the game's. It remembers the pack for next time, and never copies the
   English into your translation.
2. **New translation.** Pick the language and where to save it. Or **Open translation** to go on
   with one.
3. Pick a part of the game on the left and a line in the middle. Type the translation on the right.
   - "Keep these, in this order" lists the tags the line must keep (`{name}`, `{wait}`,
     `{choice 2}` ...). The buttons above the box insert them. `{br}` is a line break; you can
     leave line breaks out and let the tool break the line.
   - Under the box: what the compiler says about the line. An error means the game will show its
     own English for that line until it is fixed.
   - The preview draws a dialogue line as the game will break it, screen by screen. Widths and
     line breaks are the game's; the letter shapes are your computer's font.
4. **Save** (Ctrl+S). Ctrl+Up and Ctrl+Down move between lines.

## The file

A translation is one `.pelang`. It is the file players add to the game, and the file you open
again to go on. It is a zip holding:

| part | what |
|---|---|
| `text.bin` | the compiled pack: every line that passes the checks |
| `translator/work.json` | your lines as you typed them, lines that don't pass yet, and your notes |
| `pictures.bin`, `readme.txt`, anything else | kept exactly as they were |

The game reads `text.bin` and leaves the rest alone. Saving keeps the version before as
`<name>.pelang.before-last-save`.

A pack made from a fan patch (the Spanish, Italian, German, French and Chinese player packs)
keeps the rules it was made with, and the title bar says "a fan patch's rules": a line's pauses
may differ from the English, and a menu line a little too wide is drawn a little narrower. Lines
you change or add follow the same rules. The patch's own letter shapes and widths come from the
pack itself, so saving it untouched gives back the same pack, byte for byte. If even those rules
would change lines nobody edited, the file opens for reading only and saving is switched off.

Chinese, Japanese and Korean: characters the pack has no picture of are drawn from a font file.
Pick one with **Font…** at the top; it is remembered on your computer, not put in the file. Once
saved, those characters' pictures are in the pack, so it opens and builds again without the font.

## Not there yet

- Pictures with words, and drawing missing letters.

## Code

- `project.py`: all the work, no window. It runs `pelang.py build` on your lines, so the file is
  byte for byte what the command line writes from the same lines. `DATA_FILES` lists the only
  files other than code it reads; none holds game text.
- `app.py`: the window.
- `package.py`: builds the download for the system it runs on and checks the built program:
  it starts it, types a line, saves and quits (`--report`), compares the file with what the source
  tool saves, and searches everything inside for game text. Build tools are pinned in
  `requirements-download.txt`:

  ```
  python -m pip install -r localization/translator/requirements-download.txt
  python -I localization/translator/package.py --out translator-dist [--english "English pack.pelang"] [--packs DIR]
  ```
- Tests: `localization/tests/test_translator.py` (CTest `localization-translator`) and the
  `translator` part of `tests/text_renderer_game.py` (CTest `original-game-translator-file`), which
  runs the game with a file the Translator saved.
