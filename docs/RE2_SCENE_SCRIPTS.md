# Imported RE2 scene scripts

## Implementation plan

1. **Keep the room VM authoritative.** Decode the original instruction, resolve the actor selected by the current task, and record its pending action against its room slot. Keep authored writes to position, facing, scratch words, camera and foreground flags in the VM. The host consumes the resulting state. It does not decide which story branch runs.
2. **Use one actor command path across rooms.** Start with direct playback and the ordinary destination modes. Make the commands replace each other, suspend and resume, and relinquish control on return. Clear pending work when the room initializes or a new actor replaces the slot. Keep arrival flags tied to reaching the destination, including walks that end at world origin. Suppress field pursuit while a script owns a placement so an actor cannot fight its authored direction.
3. **Resolve clips by actor slot.** Use the actor's stock bank and the room binding for the active scenario. Allow the renderer to draw an actor selected by the script even when its ID is above the normal enemy range. Reuse the same world pose and fixed-camera renderer. Check model compatibility and frame bounds before sampling. Keep the camera, mask groups, and effect paths independent of animation state.
4. **Make scene state inspectable.** Extend the existing enemy movement readout with the selected clip, elapsed frame, destination and pause state. Keep unsupported instructions visible in the existing VM counters. A later parity pass can identify where a scene breaks by looking for the first command that fails to affect state.
5. **Finish specialized commands in groups.** Wire neck targets and attachments once the imported renderer can apply them consistently. Add weapon pose, muzzle and sound handling through the shared effect path. Add explicit loop, hold, completion and root-motion semantics using the original sequence data, then send completion back to the VM for scripts that wait on it. Connect player and partner pose commands through the host's rig adapters.
6. **Verify by opcode across scenes.** Use the existing import store and a separate profile. For each scene, record the procedure, actor slot, command, selected asset, first and last pose, arrival flag, camera, and frame at which player control returns. Compare matched renders for mask coverage and retain captures only for useful regressions.

## First implementation

| Area | Connected | Follow-up |
| --- | --- | --- |
| Direct clips | Mode-zero `PLC_MOTION` for imported enemy and NPC slots, room and stock animation banks, elapsed time and reverse playback | Other motion modes, loop flags and completion callbacks |
| Destinations | `PLC_DEST` has a shared position step for Aya and imported actors. The actor publishes arrival at the destination. `PLC_RET` releases it and `PLC_STOP` pauses and resumes it. | Exact per-actor speeds, authored root motion and special destination modes. The partner still uses its existing path. |
| Script fields | `PLC_FLG` supports set, OR and XOR. `PLC_ROT` writes the selected scratch word. `PLC_CNT` sets the starting sample for a direct clip. | Playback flags beyond bank selection and reverse. `PLC_STOP` currently pauses playback and movement without exposing the native routine-word transition. |
| Renderer | A scene actor can draw through the shared field renderer regardless of its enemy ID range. Room animation banks resolve by scenario and actor slot and cache per slot. Existing dedicated actor routines remain in place. | Skinned meshes, character-specific walk clips, aiming, equipment, and partner and player pose retargeting. |
| Camera and foreground | Room scripts continue to own camera cuts, input locks, and per-camera mask group visibility. These remain separate from actor animation. | Compare matched actor and camera captures on both sides of a foreground edge. |

## Replay coverage

- **1-01, Kendo.** Enter through the street door and run the introduction and window trigger. Check poses, forward and reverse clips, destinations, arrivals, and both releases of player control.
- **1-09, crow corridor.** Check the script commands that toggle camera 1 and 2 mask groups while the corridor event runs. Use a still actor and identical camera for mask-on and mask-off captures. The crows' flight logic needs a separate implementation.
- **2-04, Licker reveal.** Confirm script-owned actors render and the dedicated release handshake still executes. Check that field pursuit waits for release.
- **4-0A and 7-02, boss entrance.** Confirm script-specific acknowledgments, camera cuts and encounter launches survive the shared change.
- **3-02 and 6-13, enemy variants.** Exercise a grounded spider and a wall-mounted actor. Mounting, drop and takeoff behavior stay separate from the ordinary destination code.

The Windows target and session test executable were built. Runtime replays and test execution are deferred for the next pass.
