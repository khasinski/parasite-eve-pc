# Mod directory

Mod-enabled downloads include built-in mods. Use the **F1** panel to turn them on and change their
settings. This directory also links to guides for optional packs and custom builds.

To write your own mod, see the [developer guide](../mods/README.md) instead.

## Why the `mods` folder is empty

The `mods` folder in the Windows download holds only `README.md`, which is the developer guide.
The game never loads anything from that folder. The mods are part of `parasite-eve-pc.exe`, so
there is nothing to copy there.

Anything you add yourself goes in your **preferences folder**. That is the folder holding
`parasite-eve.toml`: `%APPDATA%\Parasite Eve` on Windows,
`~/Library/Application Support/Parasite Eve` on macOS, and `~/.config/parasite-eve` on Linux. In
portable mode it is the `userdata` folder beside the game. The game creates these items there:

| Item | What it is |
|---|---|
| `pe_mods.ini` | Your F1 settings. Saved when you close the panel. |
| `pe_mods.log` | Mod messages, such as mistakes found in your own phone calls. |
| `mods/upscale/` | The HD background pack. Use **Escape → Mods → Background pack** rather than copying files here. |
| `pe_calls/` | Phone Calls text files, which you can edit (see [Phone Calls](PHONE_CALLS.md)). |
| `pe_skins/`, `packs/` | Wardrobe character skins and character packs (see [Wardrobe](pe-enhancements/README.md#wardrobe)). |
| `lang/` | Language packs added with **F1 → Language → Add a translation**. |
| `quicksave_<slot>.pec` | Quick save slots. The Japanese release uses `quicksave_jp_<slot>.pec`. |
| `pe_collection.ini`, `pe_wardrobe.ini` | Bestiary and Wardrobe unlocks, kept across every game. |

Language packs can also go in the `lang` folder beside the game.

## Turning mods on

1. Press **F1** and open **Manage mods**.
2. Turn on **All mods**. It is off by default.
3. Restart the game.

While **All mods** is off, no mod runs and the game plays as the original. The one exception is
**Language packs**: a language you picked still applies.

Under **All mods** there is one switch for each mod, and every one is on by default. A mod turned
off doesn't start, and neither do the mods that need it. Changes in Manage mods apply the next time
the game starts. Most other F1 settings apply straight away.

Some rows are marked "press twice", such as **Buy** and **Dismantle**. The first press asks, the
second press does it. Rows on **F1 → PE Enhancements → Debug** are tuning and test tools. They
change the balance and are not needed for normal play.

## Gameplay

- [PE Enhancements player guide](pe-enhancements/README.md). Built into mod-enabled builds. Open **F1 → PE Enhancements** for movement, combat, inventory, the Junk economy, shops, and collection features.

## Visuals and language

- [HD backgrounds](HD_BACKGROUNDS.md). Download and manage the optional background pack through **Escape → Mods → Background pack**.
- **Fonts.** Use **F1 → Font** on desktop mod builds to choose a shipped font or keep Original, the default.

### Language packs

Manage mods name: **Language packs**. Page: **F1 → Language**.

This mod runs even while **All mods** is off. The game ships with no translations, only the
original English text.
- **Adding a pack**: put a `.pelang` file in the `lang` folder beside the game, or use **Add a
  translation**, which checks the file and copies it into your preferences folder.
- **First start**: the first start that finds packs asks which language to use.
- **Changing language**: choose it under **Language**, then restart.

Text missing from a pack is shown in English. See [Translating](TRANSLATING.md) to make a pack.

## Extra content

- [Phone Calls](PHONE_CALLS.md). Built in. Open **F1 → Phone Calls** to configure optional conversations after saves or to write your own calls.

### Enemy import (Fights)

Manage mods name: **Enemy import**. Page: **F1 → Fights**. Off until you turn it on.

**Area** and **Fight** pick one of the game's own random battles (Day 1 sewer to Day 4 hospital).
**Start fight** takes Aya to that room after the panel closes and the battle starts there. After a
win or running away she comes back to where she stood, with the story as it was; rewards are kept.
Losing is the game's normal Game Over. **Last fight** shows how the last one ended.

**Imported enemy** adds an enemy you made from another game with the enemy importer tool (folders
in `pe_enemies` beside the settings), or **None**. It joins the fight as an extra enemy with its own
model, texture, moves and fight behaviour: it walks up to Aya, attacks when its time bar is full,
flinches when shot and falls when beaten, giving its own EXP. **How many** adds 1 to 3 of it.
**Room's enemies** chooses whether the room's own foes **Fight too** or are **Sent away**.
**Imported status** shows how the last one did. Packs are built on your own PC from your own game
files and are for your own use only.

To make one in the game: turn on **Enemy import**, add your Resident Evil 3 PC folder (the one with
`rofs1.dat`) under **F1 → Import**, and Nemesis is built by himself, with his own moves and sounds.
Other enemies with a profile show in Import's list with **Build as an enemy**.

## Tools and cheats

### Quick save

Manage mods name: **Quick save**. Page: **F1 → Quick save**.

There are ten slots, 0 to 9.
- With **Hotkeys** on (the default), **F10** saves to the current slot, **F11** loads it and
  **F12** picks the next slot.
- The page also has **Save** and **Load** buttons and shows whether the slot is used.
- Quick load also works from the title screen. Loading restarts the saved room. Quick save and quick load wait for room transfers to finish. A save captures the same game progress as a memory card save.
- Older `quicksave_<slot>.mcd` files still load.

### Cheats

Manage mods name: **Cheats**. Page: **F1 → Cheats**.

The page has **God mode**, **Infinite ammo and PE**, **Win this battle**, **Learn all PE powers**
and **Restore PE powers**. Every cheat is off until you choose it. While they are off, the mod
changes nothing in the game.

### Warp

Manage mods name: **Warp**. Page: **F1 → Warp**.

**Warp to** picks one of 41 story checkpoints, from Day 1 at Carnegie Hall to the finale. **Warp**
jumps there after the panel closes, in the field. It never warps during a battle, an event, a room
change or a movie.

Warping sets the story to that point, so keep a normal save if you want to go back.

## Optional builds

Custom builds can include additional mods with `PE_EXTRA_MODS`. The [developer guide](../mods/README.md) covers build options. See the [Battle Point overhaul](../mods/pe_bp/README.md) for that opt-in mod.
