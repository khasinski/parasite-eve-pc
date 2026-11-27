# PE Enhancements player guide

Open **F1 → Manage mods**, enable **All mods**, and restart the game. Then open **F1 → PE Enhancements**. Each part of the suite has its own switch in Manage mods. See the [mod directory](../MODS.md) for other mods and preferences.

**Manage mods** changes apply after a restart. Most feature controls apply immediately.
**Apply playstyle** commits economy settings to the current save, and a combat style change takes
effect in the next battle. Bestiary and Wardrobe unlocks are shared across saves in the same
preferences folder. Bag sort and **No random battles** belong to the current save.

| What you want to do | Where to look |
| --- | --- |
| Change movement, encounters, sorting, or key-item storage | [Quality of life](#quality-of-life) |
| Adjust weapon effects, Parasite Energy, turn costs, or medicine | [Rebalance](#rebalance) |
| Pick an alternate combat style | [Combat styles](#combat-styles) |
| Free up space in the bag or storage | [Stacked items](#stacked-items) |
| Earn, spend, or hand in Junk | [Junk economy](economy.md#economy-junk) |
| Get and redeem Junk bundles | [Super Junk and Duper Junk](economy.md#super-junk-and-duper-junk) |
| Tune drops, shop for supplies, or transfer equipment effects | [Battle rewards](economy.md#battle-rewards), [shops](economy.md#shops-requisition-and-waynes-workshop), [Mod Catalog](economy.md#mod-catalog) |
| Look up enemies or unlock and wear outfits | [Bestiary](#bestiary), [Wardrobe](#wardrobe) |
| Find testing tools and balance controls | [Debug and tuning](#debug-and-tuning) |

## Quality of life

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
- **No random battles**: overrides **Battle frequency** while enabled. Story fights still run.
  This switch is saved with your game.
- **Auto Medicine order**: Strongest first (the original) or Weakest first. Sets which medicine
  the Auto Medicine armor effect uses first when Aya drops below a fifth of her HP.

**Inventory** page:
- **Key item pocket**: on by default. Story keys and the Chrysler elevator keys leave the bag but
  still count for the story. Browse them with the **Key Items** entry that appears in Aya's main
  menu. Turning the pocket off puts them back in the bag as space allows.
- **Bag sort**: Manual or Auto, saved with your game. Auto keeps the game's own sort order. Moving
  an item by hand switches back to Manual. Square in the Use Item list opens sorting.
- **Inventory tips**: on by default. The Items row and empty bag slots show the Square sorting
  shortcut and whether Auto sort is active. The first bag visit in a session explains both.

## Rebalance

Manage mods name: **PE Enhancements: rebalance**.

- **Combat → Mod effects**: how gun and armor effects behave in battle. **Rules** is Rebalanced by
  default. Vanilla is the original game, and changing any value below it makes it Custom. The
  effects covered include Heat/Frost, Rate of Fire, gun progression bonuses, Attack All crowd
  damage, status chances, Critical Up, Quickdraw and armor tradeoffs.
- **Combat → Parasite Energy**: Heal amounts and power costs. **Rules** is Rebalanced by default.
  - **Heal 1, 2, and 3** restore 15%, 30%, and 65% of maximum HP, with minimums of 30, 60, and
    280 HP respectively. **Detox** costs 60 PE and **Scan** costs 30 PE.
  - **Scan: weak & strong** adds an on-screen summary of weaknesses and resistances.
  - **Half-bar turns** is on by default. Items, quick PE powers and weapon changes need only half
    the turn bar, and a gauge mark shows when that is ready. Attacks and the other powers still
    need a full bar. Changing armor and escaping on half a bar are off by default.
  - **Quick powers** chooses which powers count as quick. By default these are Heal 1, Heal 2,
    Detox, Medic, Scan, Slow and Confuse.
  - **Panacea** is on by default.
- **Combat → Combine items**: two of the same Medicine 1/2/3 become the next tier. Medicine 4 and
  Full Cure become Panacea, which restores 400 HP and cures ailments. Choose a recipe and confirm
  **Combine** outside battle. **Combine in item menu**, on by default, adds the shortcut when
  Aya carries both ingredients. Combining needs **Junk economy**.
- **Weapon power (DPS)**: on by default. Shows weapon damage per second in the help text.
- [Combat settings reference](combat.md) lists the weapon and armor values for each preset.

## Stacked items

Manage mods name: **PE Enhancements: stacked items**.

**Inventory → Stacked items** is on by default. You can hold up to 99 of a consumable in one bag
or storage slot. **Stacked copies** counts the items above the first copy in each stack. Turning
stacking off unstacks items as space allows.

## Combat styles

Manage mods name: **PE Enhancements: combat styles**.

Choose a [combat style](combat.md#combat-styles) through **F1 → PE Enhancements → Combat styles**.
The New Game prompt offers the same styles. Change your choice outside battle to use it in the next encounter.

## Bestiary

Manage mods name: **PE Enhancements: collection**.

The Bestiary is a record of 75 creatures, kept across every game in `pe_collection.ini`. Open it
from **Bestiary** in Aya's main menu, which appears once you have met a creature, or from
**F1 → PE Enhancements → Collection → Bestiary**.
- **Basics**: revealed by the first kill.
- **Weaknesses**: revealed by casting Scan on the creature, or after 5 kills. For a boss, beating
  it.
- **Loot**: revealed after 15 kills. For a boss, only by buying it.
- **Buy next tier**: any tier can be bought with Junk, which needs Junk economy.
- **Area** and **Show** filter the list by location, creatures you have met, or incomplete entries.
  Revealed entries show stats and kills, elemental strengths and weaknesses, and drop and steal odds.

In the menu, Cross buys, Triangle shows the model full screen, Square pauses it, Select shows the
next form and L1/R1 change the area.

## Wardrobe

Manage mods name: **PE Enhancements: wardrobe**.

The Wardrobe lets Aya wear other outfits. Unlocks are kept across every game in `pe_wardrobe.ini`.
- **Buying**: buy outfits once with Junk under **F1 → PE Enhancements → Collection → Wardrobe**,
  or in Wayne's Workshop.
- **Wearing**: once you own one, **Wardrobe** appears in Aya's main menu. Wearing an outfit
  reloads the room.
- **Group** and **Costume** browse the collection. **Wear Aya's own** returns to her current story outfit.
- **Instant swap (experimental)** is off by default.
- **Costume unlock rules** controls which installed outfits require a purchase. See
  [the economy guide](economy.md#economy-junk) for how this affects ownership.
- **Outfits**: the evening dress and T-shirt use the game's own data, so they are always offered.
- **Character skins**: these let Aya look like other characters. They are built from your own
  disc with the separate decomp bridge tools and placed in `pe_skins/<character>/` in your
  preferences folder. The game doesn't include them, and a skin that isn't there stays hidden.
- **Character packs**: these are zip files or folders with a `pack.json`, placed in `packs/` in
  your preferences folder. They are read when the game starts. The **Packs** page shows each pack
  and why one was refused.

## Debug and tuning

The **F1 → PE Enhancements → Debug** pages are for testing or deliberately changing the balance.
They can change your current save or collection progress.

| Page | What it contains |
| --- | --- |
| **Debug** | Fill bag stacks and inspect the measured DPS turn clock. |
| **Loot tuning** | Set or grant Junk, adjust empty-steal payout, and change Enhanced battle income, reward guarantees, prices, and Tool purchase scaling. |
| **Mod Catalog** | Unlock or clear the catalog, give test gear, and change effect prices and rebuild markup. |
| **Collection** | Grant Junk for purchases, add kills, unlock or reset Bestiary entries and their NEW marks, and unlock, lock, or reset Wardrobe costumes. |
