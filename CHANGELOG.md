# Changelog

## Unreleased

- Added the **Japanese release**: select your own Japanese discs (SLPS-01230/01231) and the game plays as it does there, in Japanese with its own font, kana name entry and balance (its weapon, enemy and boss numbers come from the disc), Circle to confirm, and its own memory card files. Disc 1 and Disc 2 must come from the same release. Language packs are for the USA discs and stay off with the Japanese ones.

## 0.12

- Added **language packs**: put a `.pelang` file in the new `lang` folder beside the game, or pick one with **F1 → Language → Add a translation**. The first start that finds a pack asks which language to use; to change it later, pick another under F1 → Language and restart. A pack can replace the game's text, letters and pictures with words, and works with All mods off. Releases contain no translations: packs made from fan translations (German, French, Spanish, Italian, Simplified Chinese) are shared separately.
- Widescreen: HD packs can now give the side art depth, so things like cars at the screen edge hide characters walking behind them, as they do in the 4:3 picture.
- Fixed a thin dark line where the widescreen side art meets the right edge of the 4:3 picture.
- Fixed critical hits and long-range grazes never happening: every gun hit landed as a normal hit with base damage, which also left the rebalance mod's **Critical** effect with nothing to do.
- Added a first-launch **setup wizard** (storage, discs, settings and mods), available again from **Escape → Discs**.
- **Quick save** and **quick load** now work at any moment of a running game, including battles, events and movies, and quick load also works from the title screen.
- Added optional controller shortcuts for the Settings menu (a two-button combo) and the Mods menu in **Escape → Controls**; both start unbound.
- Added CHD disc images (Disc 1 and Disc 2) next to CUE/BIN, read with libchdr.
- Added `--game <path>`: Disc 1 for this run, keeping a saved Disc 2. Releases include a `recomplauncher.json` for RecompLauncher.
- Added an optional **Phone Calls** mod (`pe_phone`): after saving at a phone, "Stay on the line?" plays a short call. Calls are plain text files players can edit or add to.
- Reworked the economy mods: rewards, Mod Catalog, shops and Wardrobe are separate mods, with Workshop, Requisition, Rebuild and Mod Permit screens from Wayne's menu, opt-in Economy playstyles, and mod data kept in a companion file beside each save (also carried by the save editor).
- Added the **Challenges** mod (`pe_challenges`) for custom builds: harder per-run rules such as No PE, Glass cannon and Hardcore. It is not in the default mod list.
- Fixed every **pe_qol** feature (Always run, speeds, key item pocket, repeat interact) silently turning off after loading a save made from a fresh New Game.
- Fixed Warner opening the item storage for equipment and the reverse.
- Wardrobe character skins keep Aya's own movement clearance, so wider NPC bodies no longer get stuck in narrow passages.
- Rebalance: Club 1 gets a +8 attack bonus, and weapons held as plain item ids now receive their bonuses.
- Fixed builds with GCC 14 and newer, and builds without the desktop download services.

## 0.11

- Improved model rendering, polygon edges, texture gradients and frame pacing across graphics backends.
- Fixed distorted Wardrobe skins during Aya’s long-dress scenes.
- Fixed several crashes involving missing animation poses and targets that have already disappeared.
- Fixed save errors leaving the save screen waiting, and cleaned up interrupted movie playback.
- Corrected Bestiary species and boss records.
- Added portable storage, clearer fast-forward controls, a refined Mods menu and optional held-button interaction retries.
- Improved HD background layer edges and pack compatibility checks. Background pack 0.2 provides reviewed artwork corrections.
- Parked experimental language packs and translation tools for further development.

## 0.10

- Added an optional **Screenshot** binding in **Escape → Controls** for keyboard and controller, opening an in-game modal with issue tags, a note field (with an on-screen controller keyboard), PNG + JSON metadata export (including exact background and widescreen IDs) and optional automatic webhook upload.
- Added an optional **Quick save** mod (`pe_quicksave`): ten save slots in the game's own card format. **F10** saves to the current slot, **F11** loads it, **F12** cycles the slot (also **F1 → Quick save**). Saves are kept beside the preferences as `quicksave_<slot>.mcd` and include every mod's records; loading restarts the saved room the way a loaded save does. Works in the free field, outside battles, movies and room changes.
- Added an optional **Cheats** mod (`pe_cheats`): **F1 → Cheats** — God mode, Infinite ammo and PE, Win this battle, and Learn all / Restore PE powers. Every cheat is off until chosen; while they are off the mod writes nothing to the game state.
- Added a **Hold / Toggle** mode for the speed-up button in **Escape → Controls**.
- Fixed the **Combine** item action and `F1` recipe shortcut when item stacking (`pe_stacks`) is enabled so stacked ingredients in a single slot can be combined and results merge onto existing stacks.
- Fixed Tool and Super Tool parameter transfers, Mod Catalog installs, Wayne's Mod Permits and Requisition purchases consuming or miscounting entire stacks when item stacking (`pe_stacks`) is enabled.
- Integrated open-source PS5 payload build support (`-DPE_PS5_GAME=ON`), native GPU presentation/upscaling options (`-DPE_PS5_GPU_NATIVE=ON`), state snapshots (`src/main/state/`), particle mesh augmentation and `pe_assists` mod support (contributed by @GUMMIBRI in #42).

## 0.9

- Keep the last working graphics resolution when a GPU allocation fails, and show the actual settings in the menu.
- Recreate Direct3D 12 window buffers safely when switching fullscreen or resizing the window.
- Added optional 60 Hz interpolated gameplay presentation, dynamic actor lighting and effect geometry PGXP in **Escape → Graphics**.
- Fixed 1px vertical seams on HD widescreen backgrounds and removed odd internal resolution multipliers (`3x`, `5x`, `7x`) that produced tile seam artifacts.
- Fixed widescreen background edge detection past black cutouts and straightened Aya's mirror line in the rehearsal room (`M319`).
- Fixed a battle crash when a delayed hit lands on an already released target.
- Memory cards: import saves copied beside the game after the first launch, and automatically open the active memory card in the save editor.
- Redesigned Mod Catalog with native Tune Up panels and independent scrolling, and upgraded the Wardrobe menu with a Bestiary-style browser and 3D previews.
- Added optional stacked consumables (`pe_stacks`) and story checkpoint Warp menu (`pe_warp`) mods.
- Added optional language pack support (`pe_lang`) with multilingual fonts, localization build tools and a Latin American Spanish interface pilot.

## 0.8

- Added optional HD backgrounds, including the title screen. Download or update them in **Escape → Mods**, with progress, cancellation and file verification. Failed updates keep the previous copy; installation and on/off apply immediately.
- Fixed HD installation and settings paths containing accented or non-Latin characters on Windows.
- Reduced the background pack to about 327 MB using high-quality WebP and lossless reference data. Original backgrounds remain available with the mod off.
- Added reviewed widescreen side artwork for the first two views; other scenes retain their existing framing.
- Fixed native pointer and callback errors affecting field effects, inventory menus and late-game effects.
- Hardened effect allocation failures and validated saved equipment slot counts.

- Handle effect allocation failures without leaving incomplete effects active.

- Improved inventory menu safety and corrected 64-bit game/SDK call signatures, including Windows crashes in Liberty Island effects.

- Fixed a Windows x64 crash while drawing the beacon effect in the NYPD boss fight.
- Added optional Junk economy, equipment dismantling/rebuilding and workshop services.
- Added optional Bestiary and Wardrobe menus.
- The save editor can inspect mod records and edit supported mod data while preserving unknown records.

## 0.7

- Fixed crashes when repeated or status-effect shots hit an enemy that has already been defeated.
- Improved Windows Direct3D 12 stability during demanding rendering, including high resolutions and fast-forward.
- Made GPU uploads, VRAM readback and device resets safer when an operation fails.
- Reset mod save data correctly when starting New Game, Tutorial or EX Game, while preserving data when an EX Game command is cancelled.
- Improved preservation of saved mod data and added optional combat and Parasite Energy rebalance settings, weapon DPS display and half-bar turns. Mods remain off by default.

## 0.6

- Added optional widescreen in **Escape → Graphics → Aspect ratio**, with centred menus and smoother camera movement. Narrow scenes and movies keep their original framing.
- Fixed a possible audio-related freeze during movie playback and transitions.
- Fixed movement mods interfering with scripted movement and battle positioning.
- Added local crash reports with information to help investigate problems.
- Fixed save editor equipment icons, weapon types and inventory capacity; added disc-based item data and safer validation.
- Added the optional key-item pocket and remembered inventory sorting.

- Keep memory cards in the user data directory on every desktop platform; import old cards without deleting or overwriting saves.
- Reset speed hold when a controller disconnects and handle keyboard and controller holds independently.
- Preserve mod settings if writing the settings file fails.

- Show keyboard bindings using the active keyboard layout.
- Keep smooth color gradients when dithering is disabled.

- Preserve the previous save if writing a new save fails.
- Reject malformed inventory data when loading and prevent invalid equipment from crashing sorting.
- Check room task data before resolving its offsets into native pointers.

## 0.5

- Added analog movement with a configurable field walk/run threshold in Controls settings.
- Added a configurable 2x–4x speed hold.
- Added the optional F1 Mods menu with movement, encounter and inventory conveniences. Mods are off by default.

## 0.4

- Press Start during a story movie to pause it and choose whether to skip. Skipping also stops music started for that movie.
- Added optional 4x SSAA antialiasing and bilinear filtering for model textures in Graphics settings. Both are off by default.
- Fixed text and image shimmering when the game switches framebuffers, at all internal resolutions.
- Fixed geometry correction and model filtering having no visible effect during gameplay.

## 0.3-alpha

- Moved the settings menu (`Esc`) to an in-window overlay rendered directly over the main game window instead of opening a separate window.
- Added 3D Geometry Correction (`PGXP`) for higher internal resolutions (`2x`–`8x`), eliminating PS1 vertex snapping and polygon wobbling while preserving seamless animated character models without jitter or seams.
- Fixed 2D sprite UV sampling at higher internal resolutions (`2x`–`8x`) so small menu fonts, save/load screen digits (`Level`, `HP`, `Time`), and HUD elements render cleanly without texel artifacts.

## 0.2-alpha

- Fixed streamed effects selecting the wrong instrument samples.
- Fixed the sound of music, footsteps, gunfire and other effects: corrected PS1 sample interpolation and the timing of sound playback.
- Restored the PS1 reverb filter for more faithful ambience and instrument sound.
- Fixed sound envelopes, pitch modulation, noise and mixing at high volumes.
- Fixed a menu selection bug.

## 0.1-alpha

- First public desktop release for macOS, Linux and Windows.
- Disc image selection, automatic disc switching, graphics, sound and control settings.
- Save editor included with the Mac download and available separately for Windows and Linux ARM64.
