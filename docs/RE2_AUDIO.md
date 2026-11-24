# Imported RE2 audio

## Implementation plan

1. Keep the room script in charge of authored sound and dialogue timing. Decode each opcode into a cue with the original bank, index and source. Keep dialogue playback independent of subtitles and scene input. Log resolved assets and missing cues with enough information to trace them to the script. Preserve the room music and PE combat handoff.
2. Resolve sound-bank effects during import. Read the room EDT to select the VAB program and tone, then decode the referenced SPU waveform. Use the room and player in the asset ID so two variants can use the same cue number for different sounds. Extend the same resolver to common, equipment and enemy banks, including redirected and layered tones. Retain pitch, volume, priority and panning data for gameplay. Keep existing door and PE battle sound paths working while each bank is connected.
3. Resolve XA dialogue from the original disc's sector tables. Use the player, stage and track number, preserve the original interleave positions, and filter the XA file and channel. Store individual lines in the import catalog for asynchronous playback. Give dialogue its own one-shot stream. Apply the script's XA volume and expose completion to scripts that wait on the voice flag. Handle global and special-scenario sources explicitly.
4. Use the same mixer for actor and environment cues. Attach footsteps to contact frames and the floor surface, and attach enemy calls, crashes, shots and hits to the animation or event that triggers them. Resolve actor-relative cues to the actor's world position and use the active camera for attenuation and pan. Keep PE sounds for native combat, including the RE2 enemy cues already attached to imported battle profiles.
5. Handle movies separately. Keep MDEC frames and interleaved XA audio together, honor the movie script command, and synchronize audio with the movie playback clock and skip behavior.
6. Verify cue coverage by scanning every room and player variant. Record opcode counts, missing assets, and unresolved bank modes. Start with Kendo's introduction and window attack, followed by a surface transition, a room with overlapping effects, a scripted voice wait, a movie, and the return from PE combat. Repeat key dialogue checks for both RE2 discs. Check the content as well as mixer dispatch, since a successfully opened stream can still contain the wrong audio.

## First implementation

- Room-bank script cues now use the imported room's EDT and VAB. Independent one-shot effects can overlap in the mixer.
- XA script cues now select original stage or global dialogue from the standard Leon and Claire voice archives. They use a dedicated stream alongside room music, apply XA volume, and report whether dialogue is still playing to scene scripts. Dialogue stops when the room changes.
- On an existing profile, use **F1 Ã¢â€ â€™ Import Ã¢â€ â€™ Scan RE2** with the original raw disc images available. The updated readers add the room sounds and voice lines to the import catalog.
- Exact spatial mixing, redirected and layered tones, non-room script banks, special stage 8 dialogue, and field actor cues remain follow-up work. An ISO without the raw XA sectors cannot supply the voice tracks.


## Scripted FMV video

The PE1 host can play selected imported STR movies requested by RE2's
`Movie_on` room opcode. The room script pauses while the video plays. A new
Cross press skips the clip, and the host restores the room picture on exit.
Missing assets and unresolved movie IDs leave the script free to continue.

The RE2 title menu plays two opening movies for both characters.

| Scenario | First clip | Second clip | First room |
| --- | --- | --- | --- |
| Leon A | `OPN1STL` | `OPN2NDL` | 1-00 |
| Leon B | `OPN1STL` | `OPN2NDRL` | 1-04 |
| Claire A | `OPN1STC` | `OPN2NDC` | 1-00 |
| Claire B | `OPN1STC` | `OPN2NDRC` | 1-04 |

A fresh Cross press skips the current clip. If an asset is missing, the
sequence advances to the next clip or room.

The room-script mapping covers IDs 1, 2, 4Ã¢â‚¬â€œ14 and 17. Character-specific
entries (8, 10, 12Ã¢â‚¬â€œ14 and 17) use verified Leon filenames. MOV3 imports
preserve STR frame numbers as well as the video and XA sector positions.
This lets R200, R505 and Leon's R704 play the excerpt requested by the room
script, using the seek and end frame from the original movie table. Rescan
the original raw disc to enable excerpts. Older MOV2 imports retain
whole-movie playback with audio, and MOV1 imports play silently at 15 fps.

Rescan raw discs to correct previously imported STR clips whose directory
entries use 2,336-byte XA extent units. The importer uses the same sector
boundary for video and XA audio, so neighboring files stay out of the clip.

Playback places XA file 1, channel 1 on the 150-sector-per-second STR
timeline and selects video frames from the audio stream's consumed source
samples. If the track or audio device is unavailable, the host uses its
video clock. Verify the timing, excerpt boundaries, and channel choice on
retail discs. Several Claire-specific room movies remain unmapped.

## Ending and credits

When a room script sets `SYS_ENDING`, imported play freezes the room scripts
and hands off to the shared movie player. Scenario B plays movie 14 from
`R704L` or `R704C`. Scenario A uses the silent movie 17 excerpt. A fresh
Cross press skips the active clip. Natural completion and skipping both open
the host's campaign completion screen, which returns to the RE2 title menu.

The completion screen shows the character and scenario and plays the retail
results cue (`MAIN2A`). The cue stops when the player returns to the title.
Retail score calculations, clear-save unlocks and results art remain pending.
A missing ending movie still advances to the
completion screen.
