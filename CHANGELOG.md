# Changelog

## Unreleased

- Keep the last working graphics resolution when a GPU allocation fails, and show the actual settings in the menu.
- Recreate Direct3D 12 window buffers safely when switching fullscreen or resizing the window.

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
