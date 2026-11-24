# Hosting imported Resident Evil rooms

`pe-re-play-core` owns imported room logic, scripts, enemy placements, camera decisions, the RE inventory and flags, and the choice of music and sound cues. Link exactly one adapter that supplies `re_host()`. The CMake portability check keeps PE1 engine dependencies out of the shared sources. The shared runtime accepts an import-store path through `pe_re_play_init()`. It uses common mod services for logging, notifications, assets, and encounter publication. PE1's menu module registers its own controls after initialization. A PE2 shell can use the runtime's controls with its own UI.

## Bring up a host

1. Implement the required callbacks in `mods/pe_re_play/re_host.h`. Bind the shared frame and lifecycle callbacks. Project the shared room, camera, floor, and player pose into the native engine. Keep the imported unit system at the interface, including 4096 units per turn and facing zero toward negative z. Translate the host's input before reporting it.
2. Bring up the field first. Leave optional save, backdrop, title, traversal, and native audio callbacks absent until the corresponding host features work. The headless adapter exercises an exploration-only configuration. Port scene actors, foreground mask groups, effects and input locks before using a cinematic to check visual parity.
3. Connect native combat through `mods/host/re_play_api.h`. Use `mods/host/pe_trip_result.h` for approach selection, scripted boss selection, roster selection, and defeat reporting. Read the room snapshot and copy the selected placements and their source slots before calling `pause_for_battle`. Use the host's battle engine to run the encounter and map defeated actors back to those slots. Always release the pause on exit, including failed launches. Report a `PeReBattleResult` after the room returns. The shared runtime checks the game, stage and room and applies the authored defeat rules. A host can omit the backdrop callbacks if it renders its battles elsewhere.
4. Connect persistence through `register_progress` and `enable_progress`. The shared payload has a key, version, byte count and upgrade function. PE1 stores the existing payload as its companion section. A future PE2 adapter can store the same bytes with its own saves. Deliver save-read after loading the bytes, new-game after clearing them, and save-committed only after the native write succeeds.
5. Supply `re_field_audio` using `mods/host/re_field_audio.h` to play the room's music, voices, effects, and doors. The existing SDL backend implements this contract. A host with a different mixer can supply the service using the same asset handles. Keep the music pause position when crossing into native combat.
6. Use the same store and replay inputs for each adapter. Verify room entry, Kendo's scene and audio, the music handoff into battle, the return pose, defeated slots, and a save/reload. Use an exploration-only host early and the full PE1 adapter as the native reference.

PE2 will still need its own renderer and player rig bridge, native battle integration, and persistence implementation. None of those require duplicating the imported room VM or disc readers.

## Verification

Run `test-mod-re-session --host-store <import-store>` with a catalog containing Leon's RE2 rooms. This exercises the PE1 menu bindings through the test host, checks the battle result and save/reload path, and starts the same room through the runtime directly with native save and backdrop support omitted.
