# PE Enhancements economy

[PE Enhancements guide](README.md)

## Economy (Junk)

Manage mods name: **PE Enhancements: economy**. The Shops, Mod Catalog and Battle rewards mods all
need this one.

With **Economy → Junk economy** on (the default), Junk stops taking up a bag slot and goes into a
shared counter that holds up to 99999.
- Junk from a won battle is paid even if you leave it on the items-found list.
- Stolen Junk is paid when you win the battle.
- An empty steal still gets you a little Junk.
- [Super Junk and Duper Junk](#super-junk-and-duper-junk) are bundles that can be redeemed into the counter.
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
- The **Economy → Battle Income**, **Bonus Drops**, **Bundle Redemption** and **Junk Storage**
  pages tune the Custom playstyle. Their values are saved with your game.
- Enhanced has its own **Difficulty** under **Economy → Advanced Legacy**: Relaxed, Normal (the
  default) or Grind. It sets shop prices to 75%, 100% or 125%. With Battle rewards on, it also
  sets reward Junk to 150%, 100% or 75% and changes how soon a rare drop is guaranteed.

| Playstyle | What it adds |
| --- | --- |
| **Enhanced** (default) | Extra Junk based on battle EXP, a no-hit bonus, rare rewards that become more likely after dry wins, and automatic bundle redemption. |
| **Vanilla drops + inventory QoL** | Keeps ordinary enemy drops and routes Junk into the counter. |
| **Light economy** | Adds 1 Junk per normal victory or 3 per boss victory, and redeems Super Junk for 10 and Duper Junk for 20. |
| **Steady Income** | Adds 3 Junk per normal victory or 10 per boss victory. |
| **Treasure Hunter** | Adds a 10% chance for a bonus item per victory, with a guarantee on the tenth win without one. |
| **Custom** | Uses your chosen counter, income, drop, and redemption settings. |

### Custom economy settings

Open these pages under **F1 → PE Enhancements → Economy**. Use **Choose playstyle** to preview
added rewards, shop prices, and bundles that would be redeemed. **Apply playstyle** commits your
choice in the field. An Enhanced rare reward that could not fit stays held for Enhanced.

| Page | What you can change |
| --- | --- |
| **Battle Income** | Extra Junk per normal or boss victory, the payment chance, a bonus for each hit avoided below four hits taken, and the amount paid for stealing from an empty enemy. |
| **Bonus Drops** | Enable bonus rare items and set their chance and victory guarantee. A guarantee of zero disables the guarantee. Super Junk and Duper Junk have their own drop switches and chances, which can add up to at most 100%. Normal enemy drops still apply. |
| **Bundle Redemption** | Enable each bundle's conversion separately and set the Junk it pays. |
| **Junk Storage** | Choose Bag or Counter for ordinary Junk. Extra income and bundle redemption require Counter. |

**Economy → Costume unlock rules** is on by default, so Wardrobe outfits must be bought with Junk.
Turn it off to make every installed outfit free to wear.

**Combat → Smoke Grenades** sells Smoke Grenades for Junk (15 before discounts). A Smoke Grenade
escapes an ordinary battle.

## Super Junk and Duper Junk

Super Junk and Duper Junk are bundle items. When redeemed, they add ordinary Junk to the shared counter, which you can spend in shops or hand in to Wayne.

- **Get bundles.** Win battles. With the Enhanced playstyle, bundles can appear among battle rewards. For Custom, open **F1 → PE Enhancements → Economy → Bonus Drops** to set each bundle's chance per won battle. These rolls are separate from rare items.
- **Collect them.** Take the bundle from the items-found list. A bundle can be held in Aya's bag or Wayne's storage until redemption.
- **Redeem them.** With the default Enhanced playstyle and Junk economy enabled, Super Junk adds **75 Junk** and Duper Junk adds **150 Junk**. Bundles in the bag or storage are converted automatically once you're back in the field.
- **Change the rules.** For Custom, open **Economy → Bundle Redemption**. You can enable redemption and set the value separately for each bundle. Turning redemption off keeps that bundle as an item. Use **Apply playstyle** in the field to apply draft settings.

## Battle rewards

Manage mods name: **PE Enhancements: battle rewards**. Needs Economy.

- **Extra Junk**: won battles pay extra Junk, based on the battle's EXP, with a bonus for taking
  few hits.
- **Rare drops**: these show in gold on the items-found list. In order of priority, a rare drop is
  an economy effect (it then unlocks in the Mod Catalog), a Super Tool, a Mod Permit, Duper or
  Super Junk, a Tool, or a stat booster.
- **Rare drop within**, on the Economy page, shows how many more won battles until a rare drop is
  guaranteed.

### Economy armor effects

These effects can unlock through rare battle rewards or be bought through Requisition. Use the
Mod Catalog to put unlocked effects on armor.

| Effect | While equipped |
| --- | --- |
| **Bargain** | Discounts shop prices. |
| **Scavenger** | Increases extra Junk earned in battle. |
| **Lucky Find** | Advances rare-drop progress faster on wins without a rare. |
| **Salvager** | Increases the Junk payout when dismantling equipment. |

## Shops: Requisition and Wayne's Workshop

Manage mods name: **PE Enhancements: shops**. Needs Economy.

- **Requisition** sells supplies for Junk. Buy from **F1 → PE Enhancements → Economy →
  Requisition** or from the Workshop.
  - Supplies: Medicine 1–4, Cure-P/D/C/M, Full Cure, bullets, rockets, Smoke Grenades, Revive and
    Tools. A Tool gets dearer each time you buy one, and is hidden while you carry a Tool Kit.
  - The four economy effects: Bargain, Scavenger, Lucky Find and Salvager.
  - If Aya has no usable weapon, Requisition can issue free bullets or a free **Spare Club**.
- **Wayne's menu** gains **Workshop**, which offers Requisition, Rebuild, Wardrobe and Mod Permit
  Card. With a Mod Permit in the bag, Mod Permit Card adds one empty mod slot to a gun or armor,
  up to that item's limit, with no risk.
- **Warner helps out** is on by default. Wherever Warner stands guard (Day 4), he offers storage
  and the Workshop.
- **Wayne junk refund** is off by default. When on, a failed craft refunds 300 Junk while Junk
  economy is off.

## Mod Catalog

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
