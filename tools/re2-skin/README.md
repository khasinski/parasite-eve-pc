# RE2 player models as Wardrobe costumes

`build_re2_skin.py` turns a Resident Evil 2 PC player model (`PLxx.PLD`, as BioRand's datapacks
keep them, e.g. `data/re2/pld1/claire.re2/PL01.PLD`) into a Wardrobe character pack
(`mods/pe_wardrobe/pe_packs.c`, format 1). It runs on the player's computer, from the player's own
files. Nothing from Capcom or from the Parasite Eve disc is in this folder, and the packs it makes
are for personal use only: don't share them.

## How the model is fitted

RE2 player models have 15 parts; Aya has 29. Instead of retargeting Aya's animations to a new
skeleton, each RE2 part is placed over the Aya bone it matches in her idle pose and stored in that
bone's frame. Aya's own animations then play unchanged (`model.bin`, `day1-8.bin`), and only the rows
that rooms install for her evening dress (31 parts) and long dress (23 parts) are retargeted into
`rooms.bin`. The module docstring in `build_re2_skin.py` has the details: per-part segment fitting,
chest and hips fitted as one piece, legs slimmed toward Aya's width, feet levelled, hidden lids
left out, and the 8-bit RE2 texture packed into the player's 4-bit page.

Tested with Claire (`claire.re2`): checked in game on walking, running, battle, the mirror rooms and
the Day 1 dress scenes. Other RE2 player models share the same 15-part layout and should build,
but haven't been checked in game.

## Needs

- Python 3 with Pillow.
- A checkout of [psxrecomp-decomp-bridge](https://github.com/sirebacon/psxrecomp-decomp-bridge)
  (uses `tools/pe_assets/render_models.py`, `retarget.py`, `build_skin.py`; built against 83f77cc).
- Your Parasite Eve (USA) disc 1 image (raw `.bin`): Aya's model and animations are read from it.
- This repository (`src/main/render/object.c` gives the game's normal table).

## Build

```
set PE_BRIDGE_DIR=C:\path\to\psxrecomp-decomp-bridge
set PE_DISC_DIR=C:\path\to\folder\with\disc
python tools/re2-skin/build_re2_skin.py <biorand>/data/re2/pld1/claire.re2/PL01.PLD --check
```

`PE_DISC_NAME` overrides the disc file name (default `Parasite Eve (USA) (Disc %d).bin`).

This writes the pack folder `re2-local/` (`--pack` picks another; running again with another
model adds it to the same pack):

```
re2-local/
  pack.json  LICENSE
  characters/claire.re2/  character.json  model.bin  texture.bin  day1.bin ... day8.bin  rooms.bin  meta.bin
```

Copy the folder into `<preferences>/packs/` and restart. The costume shows in the Wardrobe under
"Resident Evil" (`--group`), named from the folder ("Claire (RE2)", or `--name`), with key
`re2-local/claire.re2`. `--tier` sets its price when Economy charges for costumes (default `extra`).
`--check` also writes `claire.re2_check.png`: Aya on top, the skin below, in the same frames.

`pld_info.py` prints what a PLD holds; `re2_model.py` is the reader; `pe_disc.py` reads the three
tables the bridge's decoders want straight from the disc (they normally take a save state).
