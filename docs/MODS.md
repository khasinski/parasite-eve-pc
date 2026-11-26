# Built-in mods

The downloads come with a set of mods already built into the game. You don't install them. You
turn them on and change their settings in the **F1** panel. This page describes each mod: what it
does, where its settings are, and how it starts.

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
| `pe_calls/` | Phone Calls text files, which you can edit (see [Phone Calls](#phone-calls)). |
| `pe_skins/`, `packs/` | Wardrobe character skins and character packs (see [Wardrobe](#wardrobe)). |
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

## PE Enhancements

These mods share one page, **F1 → PE Enhancements**, which is sorted by topic: Movement, Combat,
Economy, Inventory and Collection. Each mod has its own switch in Manage mods, named
"PE Enhancements: …".

### Quality of life

Manage mods name: **PE Enhancements: quality of life**.

**Movement** page:
- **Always run**: on by default. Hold the run button to walk.
- **Walk speed** and **Run speed**: 1.75x by default. Choices go from Normal to 3x.
- **Combat speed**: Normal by default.
- **Battle start speed**: speeds up the battle-entry sequence. Normal by default, up to 4x.
- **Repeat interact**: off by default. When on, holding the action button keeps interacting.
- **Hold to skip dialogue**: Off by default, or Square, R1 or L1. Holding that button turns
  dialogue pages quickly. Pages with a question still wait for you to answer.

**Combat** page:
- **Battle frequency**: Normal, More (2x), Lots (4x) or Every check. Story battles and room rules
  still apply. This changes how often fights happen, not how big they are.
- **No random battles**: this switch is saved with your game, not in `pe_mods.ini`.
- **Auto Medicine order**: Strongest first (the original) or Weakest first. Sets which medicine
  the Auto Medicine armor effect uses first when Aya drops below a fifth of her HP.

**Inventory** page:
- **Key item pocket**: on by default. Story keys and the Chrysler elevator keys leave the bag but
  still count for the story. Browse them with the **Key Items** entry that appears in Aya's main
  menu. Turning the pocket off puts them back in the bag as space allows.
- **Bag sort**: Manual or Auto, saved with your game. Auto keeps the game's own sort order. Moving
  an item by hand switches back to Manual. Square in the Use Item list opens sorting.
- **Inventory tips**: on by default.

### Rebalance

Manage mods name: **PE Enhancements: rebalance**.

- **Combat → Mod effects**: how gun and armor effects behave in battle. **Rules** is Rebalanced by
  default. Vanilla is the original game, and changing any value below it makes it Custom. The
  effects covered include Heat/Frost, Rate of Fire, gun progression bonuses, Attack All crowd
  damage, status chances, Critical Up, Quickdraw and armor tradeoffs.
- **Combat → Parasite Energy**: Heal amounts and power costs. **Rules** is Rebalanced by default.
  - **Half-bar turns** is on by default. Items, quick PE powers and weapon changes need only half
    the turn bar, and a gauge mark shows when that is ready. Attacks and the other powers still
    need a full bar. Changing armor and escaping on half a bar are off by default.
  - **Quick powers** chooses which powers count as quick. By default these are Heal 1, Heal 2,
    Detox, Medic, Scan, Slow and Confuse.
  - **Panacea** is on by default.
- **Combat → Combine items**: two of the same Medicine 1/2/3 become the next tier. Medicine 4 and
  Full Cure become Panacea, which restores 400 HP and cures ailments. Combine is also an action in
  Aya's item menu. It needs **Junk economy** to be on.
- **Weapon power (DPS)**: on by default. Shows weapon damage per second in the help text.

### Stacked items

Manage mods name: **PE Enhancements: stacked items**.

**Inventory → Stacked items** is on by default. You can hold up to 99 of a consumable in one bag
or storage slot. Turning it off unstacks items as space allows.

### Economy (Junk)

Manage mods name: **PE Enhancements: economy**. The Shops, Mod Catalog and Battle rewards mods all
need this one.

With **Economy → Junk economy** on (the default), Junk stops taking up a bag slot and goes into a
shared counter that holds up to 99999.
- Junk from a won battle is paid even if you leave it on the items-found list.
- Stolen Junk is paid when you win the battle.
- An empty steal still gets you a little Junk.
- Super Junk (75) and Duper Junk (150) in the bag or in storage are turned into counter Junk when
  you are in the field.
- **Inventory → Junk counter**, on by default, shows your Junk beside Quantity in Aya's inventory.
- **Take out Junk** moves counter Junk back into the bag and storage.
- Turning Junk economy off puts Junk back in the bag, as in the original game.

**Wayne hand-in** sets what Wayne's Discard junk gives him: **Next gun** (the default) gives only
what the next gun needs, **All Junk** gives everything. **Quick hand-in**, on by default, has
Wayne count Junk 100 at a time instead of one by one, so 300 Junk takes three of his steps
(a few seconds) rather than well over a minute. His count, the Junk spent and the gun at 300 come out
the same. It works for counter Junk and for Junk in the bag (Junk economy off, or stacked with
Stacked items); turn it off for the original pace.

**Choose playstyle** and **Apply playstyle** pick how generous the economy is. Applying only works
in the field.
- The playstyles are Enhanced (the default), Vanilla drops + inventory QoL, Light economy, Steady
  Income, Treasure Hunter and Custom.
- The **Economy → Battle Income**, **Bonus Drops**, **Bundle Redemption** and **Junk Storage**
  pages tune the Custom playstyle. Their values are saved with your game.
- Enhanced has its own **Difficulty** under **Economy → Advanced Legacy**: Relaxed, Normal (the
  default) or Grind. It sets shop prices to 75%, 100% or 125%. With Battle rewards on, it also
  sets reward Junk to 150%, 100% or 75% and changes how soon a rare drop is guaranteed.

**Economy → Costume unlock rules** is on by default, so Wardrobe outfits must be bought with Junk.
Turn it off to make every installed outfit free to wear.

**Combat → Smoke Grenades** sells Smoke Grenades for Junk (15 before discounts). A Smoke Grenade
escapes an ordinary battle.

### Battle rewards

Manage mods name: **PE Enhancements: battle rewards**. Needs Economy.

- **Extra Junk**: won battles pay extra Junk, based on the battle's EXP, with a bonus for taking
  few hits.
- **Rare drops**: these show in gold on the items-found list. In order of priority, a rare drop is
  an economy effect (it then unlocks in the Mod Catalog), a Super Tool, a Mod Permit, Duper or
  Super Junk, a Tool, or a stat booster.
- **Rare drop within**, on the Economy page, shows how many more won battles until a rare drop is
  guaranteed.

### Shops: Requisition and Wayne's Workshop

Manage mods name: **PE Enhancements: shops**. Needs Economy.

- **Requisition** sells supplies for Junk. Buy from **F1 → PE Enhancements → Economy →
  Requisition** or from the Workshop.
  - Supplies: Medicine 1–4, Cure-P/D/C/M, Full Cure, bullets, rockets, Smoke Grenades, Revive and
    Tools. A Tool gets dearer each time you buy one, and is hidden while you carry a Tool Kit.
  - The four economy effects: Bargain, Scavenger, Lucky Find and Salvager.
  - If Aya has no usable weapon, she gets free bullets and a spare Club.
- **Wayne's menu** gains **Workshop**, which offers Requisition, Rebuild, Wardrobe and Mod Permit
  Card. With a Mod Permit in the bag, Mod Permit Card adds one empty mod slot to a gun or armor,
  up to that item's limit, with no risk.
- **Warner helps out** is on by default. Wherever Warner stands guard (Day 4), he offers storage
  and the Workshop.
- **Wayne junk refund** is off by default. When on, a failed craft refunds 300 Junk while Junk
  economy is off.

### Mod Catalog

Manage mods name: **PE Enhancements: Mod Catalog**. It uses Junk, so it needs Economy running.

The catalog lets you collect gun and armor effects and move them between equipment. Open it from
the **Mod Catalog** entry in Aya's main menu or under **F1 → PE Enhancements → Economy**.
- **Dismantle** breaks down a gun or armor you aren't wearing. It unlocks that item's effects,
  pays Junk and adds the item to the Rebuild list. It never takes equipped gear or your last
  Club.
- **Discard dismantles** is on by default. It turns Discard on gear, in the item menu and in
  storage, into Dismantle.
- **Install** puts an unlocked effect on your equipped gun or armor for Junk.
  - Premium effects also use a Tool unless you carry a Tool Kit.
  - Only one effect per group is allowed, so installing replaces an effect of the same group.
  - Bag-capacity armor effects are never removed or replaced.
- **Remove** takes an effect off. The effect stays unlocked.
- **Rebuild** buys a dismantled item back with its bonuses and slot count, but with empty slots.

### Bestiary

Manage mods name: **PE Enhancements: collection**.

The Bestiary is a record of 75 creatures, kept across every game in `pe_collection.ini`. Open it
from **Bestiary** in Aya's main menu, which appears once you have met a creature, or from
**F1 → PE Enhancements → Collection → Bestiary**.
- **Basics**: revealed by the first kill.
- **Weaknesses**: revealed by casting Scan on the creature, or after 5 kills. For a boss, beating
  it.
- **Loot**: revealed after 15 kills. For a boss, only by buying it.
- **Buy next tier**: any tier can be bought with Junk, which needs Junk economy.

In the menu, Cross buys, Triangle shows the model full screen, Square pauses it, Select shows the
next form and L1/R1 change the area.

### Wardrobe

Manage mods name: **PE Enhancements: wardrobe**.

The Wardrobe lets Aya wear other outfits. Unlocks are kept across every game in `pe_wardrobe.ini`.
- **Buying**: buy outfits once with Junk under **F1 → PE Enhancements → Collection → Wardrobe**,
  or in Wayne's Workshop.
- **Wearing**: once you own one, **Wardrobe** appears in Aya's main menu. Wearing an outfit
  reloads the room.
- **Instant swap (experimental)** is off by default.
- **Outfits**: the evening dress and T-shirt use the game's own data, so they are always offered.
- **Character skins**: these let Aya look like other characters. They are built from your own
  disc with the separate decomp bridge tools and placed in `pe_skins/<character>/` in your
  preferences folder. The game doesn't include them, and a skin that isn't there stays hidden.
- **Character packs**: these are zip files or folders with a `pack.json`, placed in `packs/` in
  your preferences folder. They are read when the game starts. The **Packs** page shows each pack
  and why one was refused.

## Quick save

Manage mods name: **Quick save**. Page: **F1 → Quick save**.

There are ten slots, 0 to 9.
- With **Hotkeys** on (the default), **F10** saves to the current slot, **F11** loads it and
  **F12** picks the next slot.
- The page also has **Save** and **Load** buttons and shows whether the slot is used.
- Quick save works in the field, but not during battles, movies or room changes. Loading restarts
  the saved room, as loading a memory card save does.
- Older `quicksave_<slot>.mcd` files still load.

## Cheats

Manage mods name: **Cheats**. Page: **F1 → Cheats**.

The page has **God mode**, **Infinite ammo and PE**, **Win this battle**, **Learn all PE powers**
and **Restore PE powers**. Every cheat is off until you choose it. While they are off, the mod
changes nothing in the game.

## Warp

Manage mods name: **Warp**. Page: **F1 → Warp**.

**Warp to** picks one of 41 story checkpoints, from Day 1 at Carnegie Hall to the finale. **Warp**
jumps there after the panel closes, in the field. It never warps during a battle, an event, a room
change or a movie.

Warping sets the story to that point, so keep a normal save if you want to go back.

## Enemy import (Fights)

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
`rofs1.dat`) under **F1 → Import**, and Nemesis, a Zombie, Cerberus, a Hunter, a Giant Spider and
Drain Deimos are built by themselves, each with its own moves and sounds.
Every other monster from a game you've imported (Resident Evil 1, 2 or 3) shows in Import's list
with **Build as an enemy** too. Its moves are picked for it from how they look (standing still,
walking, being hit, falling down, reaching out), and its size, HP and strength from how big it is.
The picks are saved in its folder as `profile.json`: change a move number there (moves count from 0)
and press **Build as an enemy** again to use your change.

## Phone Calls

Manage mods name: **Phone Calls**. Page: **F1 → Phone Calls**.

After Aya saves at a phone, she can stay on the line for a short call.

| Setting | Choices | Default |
|---|---|---|
| **Phone calls** | Ask after saving, Always call, Off | Ask after saving |
| **Ask without saving too** | Off, On | On |
| **Call tone** | Mixed, Serious only, Funny only | Mixed |
| **Phone sounds** | Off, On | On |

- Calls you have heard are remembered in your save. **Forget heard calls** clears that list.
- **Writing calls** page: write your own calls as text files in `pe_calls/` in your preferences
  folder. **Open calls folder** opens it.
- **`pe1.txt`**: the game's own calls, and the format is explained at the top of this file.
  Editing it replaces the built-in calls, and deleting it brings them back.
- **New calls from an update**: after you edit `pe1.txt`, a newer version of the game puts its
  updated calls beside yours as `pe1.txt.new`, which the game doesn't load.
- **Other files**: every other `.txt` file in `pe_calls/` adds more calls.
- **Testing**: **Reload calls** picks up your changes without restarting, and **Try call** plays
  one. Mistakes are written to `pe_mods.log`.

## Language packs

Manage mods name: **Language packs**. Page: **F1 → Language**.

This mod runs even while **All mods** is off. The game ships with no translations, only the
original English text.
- **Adding a pack**: put a `.pelang` file in the `lang` folder beside the game, or use **Add a
  translation**, which checks the file and copies it into your preferences folder.
- **First start**: the first start that finds packs asks which language to use.
- **Changing language**: choose it under **Language**, then restart.

Text missing from a pack is shown in English. See [Translating](TRANSLATING.md) to make a pack.
