# PE Enhancements combat reference

[PE Enhancements player guide](README.md)

## Weapon and armor effects

Open **F1 → PE Enhancements → Combat → Mod effects**. **Rebalanced** is the default preset.
Choose **Vanilla** for the original values or change individual settings to use **Custom**.

| Setting | Vanilla | Rebalanced |
| --- | --- | --- |
| Heat/Frost against neutral, weak, or resistant enemies | 100%, 150%, 20% | 115%, 150%, 80% |
| Heat+Frost best of two | Off | On, uses the better match when both effects are equipped |
| RoF 7 and RoF 10 per shot | 18% and 13% | 20% and 15% |
| Installed RoF 5, 7, and 10 | Native firing speed and per-shot damage | Fast volleys and 200% damage per full volley |
| Weapon progression bonuses | Off | G3A3 gains 45 attack, M203 4 gains 15, and a selection of weaker guns and Club 1 gain 5–25 |
| Attack All crowd cap | Off | Each target can lose at most 75% of maximum HP in one Attack All turn against a crowd |
| Acid and Tranquilizer chance against resistant or ordinary targets | 0% and 50% | 20% and 60% |
| Acid poison damage | 100% per tick | 125% per tick |
| Critical bonus | +20 points | +40 points |
| Quickdraw bonus | Native speed-stat chance | Adds 15 points to the speed-stat chance |
| Damage taken with Attack Down or Slow Active Time armor | 100% for either | 75% with Attack Down or 85% with Slow Active Time |

The installed RoF options apply to effects added to a gun. A gun's built-in Rate of Fire uses
the per-shot rules.

## Combat styles

Choose **F1 → PE Enhancements → Combat styles → Style** outside battle. **Off** is the default
and is called **Vanilla** in the New Game prompt. The choice applies from the next encounter.

| Style | Benefit and tradeoff |
| --- | --- |
| **Quickstep** | A weapon action that first crosses an enemy's half-health threshold or kills an enemy while others remain returns 20% of the AT bar once and starts a three-second movement boost. Aya takes 20% more damage during the rush. |
| **Bulwark** | Guard below a full AT bar and hit back to recover some HP. Aya takes 15% more damage if she remains exposed at full AT. Bulwark falls back to Off while half-bar actions are enabled. |
| **Striker** | Close-range gun hits deal 25% more damage. Damage falls with distance, reaching 25% below normal at maximum range. |
| **Channeler** | Each finished weapon action that damages a living enemy grants one charge. One charge halves the next support power's PE cost. Two make it free and strengthen Heal 1–3 by 25%. At full charge, the first incoming attack warns you and a second can break one charge. |

Wardrobe outfits are cosmetic. Your combat style is set independently under **Style**.

### Reading the style cues

- **Quickstep** can trigger once per weapon action, even if the action hits several targets.
- **Bulwark** guards only while Aya can move freely and has less than full AT. While guarded,
  damage taken is 75% of normal. The next weapon action can spend stored damage on a counter and
  recover some of the lost HP. The blue bar shows guard, the pulsing red bar shows exposure,
  and the orange marker shows a stored counter.
- **Striker** records distance to each target when the gun action begins. The falloff is gradual
  between the close-range zone and the edge of the gun's range.
- **Channeler** spends its stored charges on the next support PE power. A multi-hit enemy attack
  counts as one attack for backlash while the hits are close together. Once Aya commits to a
  discounted spell, damage can no longer change that cast's price.
