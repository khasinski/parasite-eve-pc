# Fixtures: your own saves

Nothing here is committed: saves are yours (and the game's data). The checks look for:

| Path | What | Used by |
|---|---|---|
| `cards/BASLUS-006620000000C` | a memory card file saved to **slot C** at a **save phone**, Day 2 or later, with a weapon equipped and room to walk into a random encounter. The checks were written against a Central Park save (map 87, Day 2) | `field-commands`, `bp-challenges-state`, `battle-win`, `battle-loss`, `phone-save-call`, `reset-new-game`, selftest |
| `scenarios/wayne_day4/` | a scenario save at Wayne's bench in the precinct (map 48), Day 4 or later, with some Junk | `wayne-menu` |

Card files are the game's own saves: copy one from your memory-card folder (the save editor shows
where it is) and rename it to the slot letter you want (the last character). A save's
companion file, when it has one, goes in `cards/.pe-ext/` beside it.

Scenario saves are made from a game you are playing:

```
python pe.py play                      # the game in a window, debug channel on
# walk to the spot, stand in free field
python pe.py capture wayne_day4 --note "Wayne's bench, Day 4"
```

A check whose fixture is missing is skipped with the reason (`pe.py list` shows them).
