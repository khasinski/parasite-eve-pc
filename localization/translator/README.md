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
     that show the line. The help line at the top of the menus (item descriptions and the menus'
     hints) is drawn the same way: the menus' own window and letters, the clock at its right, over a
     room darkened as the menus darken the scene, so you see where a long line runs under the clock.
     An item's name is drawn in the item list: Aya's first items (M84F, N Vest, Club 1, Ammo Crate,
     Medicine 1) in your words where you have translated them, the item under the cursor (in
     Medicine 1's place when it isn't one of them), its icon and count box as the game shows them,
     and its description in the help line above. Not drawn that way yet: the other menu places
     (boxes and choices), battle lines, right-to-left languages, and the button icons in a few
     lines (the preview says so).
   - **Dialogue MAP** is the city map: the place title shown when Aya arrives somewhere
     (lines 1-10) and the map's narration (11-14). Type only the words, with `{br}` between lines:
     the tool centres each line and puts a title on the box's third line, as the English has them.
     A line from a pack that already has its own spacing keeps it until you change its words.
4. **Save** (Ctrl+S). Ctrl+Up and Ctrl+Down move between lines.

## Pictures with words

**Pictures…** opens the pictures that have words in them: for now the title screen's "Press Start
Button", New Game, Continue, Tutorial, EX Game and the publisher line, the city map's place
names (one picture with all ten), and the battle words (one picture with the words, numbers and
marks of a fight: Miss, NO TARGET, ENERGY SHOT, SCAN, CONFUSE and the rest), the day title
cards ("Day 1 RESONANCE" to "Epilogue SYMBIOSIS", one picture each), the Tutorial (its menu
and its 29 pages of words, one picture per screen), the Game Over words, and the menus' words and
the menu icons with a word on them (50, one picture each: Bonus Point, Equipment, Next Level, STEAL,
SORT…). They are read from your own
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
   The battle words are in greys: draw with the greys of the saved PNG, and the game tints them as
   it draws them (Miss red, the damage numbers white, green, yellow or pink). The window shows your
   picture in those colours too. Each word is cut out by a box of its own (the window lists their
   sizes), so keep a translated word inside the English one's box. The numbers, tabs and marks need
   no translating, but you may redraw them.
   A day title card is the whole screen as the game shows it, 320×224. Redraw its words; the rest
   may stay. The card is made of 16×16 squares, and each square can use only one of the card's
   sets of 256 colours: for each square you change, the tool picks the set that fits your drawing
   best, and each pixel becomes its nearest colour in that set. Day 1's card may never be shown in
   the US game (it did not appear on a New Game or when continuing a Day 1 save, and its English
   picture still has the Japanese word 共鳴), but you can redraw it like the others.
   A Tutorial page is the whole screen too, 320×224, as the game shows it. Redraw the words at the
   bottom and, if you like, the game screenshot above them (it shows the game's menus with their
   English words; it, and the Game Over screen, may stay as they are). Only the words and the
   screenshots are taken from your PNG: the PE picture behind, the shadow and the controller and
   save room pictures stay the game's whatever the PNG has there. The words use 16×16 squares and
   colour sets as a day card does. On two rooms' pages (About Icons 5-9, How to Save) the frame's
   shadow falls on the top rows of the words: draw them as you see them, and the tool takes the
   shadow back off. The Tune-up screenshot is on two pages (About Icons 7 and 8): a change loaded
   from either PNG goes on both; if both PNGs change it differently, the tool names the two PNGs and
   doesn't load the second until they match. The Tutorial menu is drawn once, with no choice
   picked: the game keeps it six times (once plain, five times with the green ring on one choice),
   and the tool puts yours into all six, with the game's green ring drawn back on the five at the
   English places, so keep the choices where they are.
   Game Over is the words alone, 120×24, with 16 colours: each pixel becomes the nearest of them,
   and the first one, black, is see-through (the screen behind is black; Aya, lying behind the
   words, shows only there). The game draws the words a little darker than the PNG, with the
   PlayStation's fine dot pattern; the window shows your picture that way too.
   A menu word is one word in its own box (Bonus Point is 64×12, Range 32×12): keep your word
   inside it, squeezed or shortened if need be. 16 colours, which stay as they are (other words
   use them too): each pixel becomes the nearest of them, and black is see-through (the menu
   behind shows there). The menus draw it in exactly these colours, and some of them (the status
   menu's icons that aren't picked) at half brightness; the window shows your picture both ways.
   A menu icon with a word (STEAL, SORT, ESC…) is the same: redraw the word, and the picture with it
   if you like. The plain icons, numbers and marks of that page are not in the list.
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

Packs made before the city map's lines were text kept them as raw bytes in `pictures.bin`. Opening
one says so: the game shows those bytes, not lines typed for Dialogue MAP, so use a newer copy of
the pack.

Chinese, Japanese and Korean: characters the pack has no picture of are drawn from a font file.
Pick one with **Font…** at the top; it is remembered on your computer, not put in the file. Once
saved, those characters' pictures are in the pack, so it opens and builds again without the font.

## Not there yet

- Drawing missing letters.

## Code

- `project.py`: all the work, no window. It runs `pelang.py build` on your lines, so the file is
  byte for byte what the command line writes from the same lines. `DATA_FILES` lists the only
  files other than code it reads; none holds game text.
- `app.py`: the window. `gamelook.py`: the "As in the game" preview (the game's dialogue drawing
  and the language mod's letters, followed from their C code; the room pictures and which room
  shows each line, from the disc). `picture_window.py`: the Pictures window; `artwork.py`: the pictures'
  list, PNG out and in, and their pieces in `pictures.bin` (TIM pixels: `petext/tim.py`; a day
  card's tiles and colour rows read from its room, as `gamelook.room_views` draws them; a Tutorial
  screen's layers read the same way and drawn in the game's order, by depth, the shadow subtracted;
  Game Over and the menu words found through the game's glyph table on their page of words; Game
  Over drawn darker and dithered, the menu words in their own colours and at half). `menulook.py`:
  the "As in the game" preview of the menus' lines (their letters, windows and the darkened scene,
  followed from the C code; the window and list places from the boot program's own tables on the
  disc, the items' icons and kinds from the resident data).
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
  names redrawn in it (Disc 1: no sample save reaches the map on Disc 2); part `translator-battle` (CTest
  `original-game-translator-battle`) runs the first fight with the battle words redrawn in it (Disc 1:
  the route starts a New Game); part `translator-cards` (CTest `original-game-translator-cards`)
  continues the sample saves of Days 2-5 (Disc 1) and Day 6 (Disc 2) with day cards redrawn in them
  (no route reaches the Epilogue's card or Day 1's); part `translator-tutorial` (CTest
  `original-game-translator-tutorial`) goes through the whole Tutorial with all 30 screens redrawn in
  it (Disc 1, the pad route of CTest `original-game-tutorial`); part `translator-game-over` (CTest
  `original-game-translator-game-over`) loses the first fight with Game Over redrawn (Disc 1, part
  battle's route, Aya's HP set to 1 by the game's own test switch); part `translator-menu-words`
  (CTest `original-game-translator-menu-words`) opens the status menu with eight of its words redrawn
  (Disc 1, part menus' route); part `translator-place-title`
  (CTest `original-game-translator-place-title`) types a place title as words and checks it on the
  city map (Disc 1, part translator-map's route, Central Park chosen); part `translator-look` (CTest
  `original-game-translator-look`) checks the game look against the game's own frames; part
  `translator-menu-look` (CTest `original-game-translator-menu-look`) checks the menus' help line the
  same way, with the game's own text and with four lines the Translator saved (Disc 1, part menus'
  route on into the item list).
