# Hosting imported Resident Evil rooms

`pe-re-play-core` owns imported room logic, scripts, enemy placements, camera decisions, the RE inventory and flags, and the choice of music and sound cues. Link exactly one adapter that supplies `re_host()`. The CMake portability check keeps PE1 engine dependencies out of the shared sources. The current registration and asset services are supplied by the mod host.

## Bring up a host

1. Implement the required callbacks in `mods/pe_re_play/re_host.h`. Bind the shared frame and lifecycle callbacks. Project the shared room, camera, floor, and player pose into the native engine. Keep the imported unit system at the interface, including 4096 units per turn and facing zero toward negative z. Translate the host's input before reporting it.
2. Bring up the field first. Leave optional save, backdrop, title, traversal, and native audio callbacks absent until the corresponding host features work. The headless adapter exercises an exploration-only configuration. Port scene actors, foreground mask groups, effects and input locks before using a cinematic to check visual parity.
3. Connect native combat through `mods/host/re_play_api.h`. Read the room snapshot and copy the selected placements and their source slots before calling `pause_for_battle`. Use the host's battle engine to run the encounter and map defeated actors back to those slots. Always release the pause on exit, including failed launches. Report a `PeReBattleResult` after the room returns. The shared runtime checks the game, stage and room and applies the authored defeat rules. A host can omit the backdrop callbacks if it renders its battles elsewhere.
4. Connect persistence through `register_progress` and `enable_progress`. The shared payload has a key, version, byte count and upgrade function. PE1 stores the existing payload as its companion section. A future PE2 adapter can store the same bytes with its own saves. Deliver save-read after loading the bytes, new-game after clearing them, and save-committed only after the native write succeeds.
5. Use the same store and replay inputs for each adapter. Verify room entry, Kendo's scene and audio, the music handoff into battle, the return pose, defeated slots, and a save/reload. Use an exploration-only host early and the full PE1 adapter as the native reference.

PE2 will still need its own renderer and player rig bridge, native battle integration, and persistence implementation. None of those require duplicating the imported room VM or disc readers.
