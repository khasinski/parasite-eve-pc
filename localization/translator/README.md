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
   - The preview draws a dialogue line as the game will break it, screen by screen. Two looks:
     **Computer font** (widths and line breaks are the game's, letter shapes your computer's) and
     **As in the game**: the game's own letters, pixel for pixel as the game draws them (also a
     fan patch's own letters and Chinese characters), over the picture of the room that shows the
     line. That look reads the letters and rooms from your disc (the same disc as for Pictures; it
     is asked for the first time). Click the picture for the room's other views, or other rooms
     that show the line. Not drawn that way yet: menus, item names and battle lines, right-to-left
     languages, and the button icons in a few lines (the preview says so).
4. **Save** (Ctrl+S). Ctrl+Up and Ctrl+Down move between lines.

## Pictures with words

**Pictures…** opens the pictures that have words in them: for now the title screen's "Press Start
Button", New Game, Continue, Tutorial, EX Game and the publisher line, and the city map's place
names (one picture with all ten). They are read from your own
disc (**Choose disc…**: a Parasite Eve USA disc image, Disc 1 or Disc 2, unchanged; it is
remembered on your computer). Nothing of the disc is put anywhere but your translation.

1. Pick a picture and **Save as PNG…**.
2. Redraw it in any paint program. Keep its size. The window says what else to keep: the title
   menu's words are laid over the screen keeping the lighter colour of each pixel, so black lets the
   screen show through and any colour works; the publisher line has 16 colours, and each pixel
   becomes the nearest of them. The map's place names have 16 colours too, and each name is in the
   picture twice, side by side: the letters (laid over the map so they glow) and the same letters
   on a dark band (which darkens the map behind the name). Redraw both, inside 127×12 pixels a
   name, and leave the bar at the bottom alone. The place you are at shows the same pixels in gold.
   A translation that changed a picture's colours (the German and Chinese packs did, on the map)
   keeps them when you load a PNG.
3. **Load PNG…**. "Your picture" shows it as the game will. **Use the game's picture** undoes it.
4. Save the translation: the picture goes into its `pictures.bin`. Other pictures already in the
   file (a fan patch's) stay as they are, and can be redrawn the same way.

## The file

A translation is one `.pelang`. It is the file players add to the game, and the file you open
again to go on. It is a zip holding:

| part | what |
|---|---|
| `text.bin` | the compiled pack: every line that passes the checks |
| `translator/work.json` | your lines as you typed them, lines that don't pass yet, and your notes |
| `pictures.bin` | the redrawn pictures (kept exactly as they were until you load one) |
| `readme.txt`, anything else | kept exactly as they were |

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

- More pictures with words (battle words, day title cards, the tutorial, Game Over), and drawing
  missing letters.

## Code

- `project.py`: all the work, no window. It runs `pelang.py build` on your lines, so the file is
  byte for byte what the command line writes from the same lines. `DATA_FILES` lists the only
  files other than code it reads; none holds game text.
- `app.py`: the window. `gamelook.py`: the "As in the game" preview (the game's dialogue drawing
  and the language mod's letters, followed from their C code; the room pictures and which room
  shows each line, from the disc). `picture_window.py`: the Pictures window; `artwork.py`: the pictures'
  list, PNG out and in, and their pieces in `pictures.bin` (TIM pixels: `petext/tim.py`).
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
  runs the game with a file the Translator saved; part `translator-pictures` (CTest
  `original-game-translator-pictures`) runs the title screen with pictures redrawn in it, both discs;
  part `translator-map` (CTest `original-game-translator-map`) runs the city map with its place
  names redrawn in it (Disc 1: no sample save reaches the map on Disc 2); part `translator-look` (CTest
  `original-game-translator-look`) checks the game look against the game's own frames.
