# Phone Calls

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

See the [mod directory](MODS.md) for the preferences folder location.
