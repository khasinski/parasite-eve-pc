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
entries (8, 10-14 and 17) use filenames from the respective retail discs.
Claire's imported room scripts request the following clips. Procedure numbers
refer to the room's main script.

| Room | Procedure | Movie ID | Retail clip | Last STR frame |
| --- | ---: | ---: | --- | ---: |
| 1-08 | 2 | 8 | R108C | 1326 |
| 2-04 | 9 | 10 | R204C | 500 |
| 4-08 | 8 | 11 | R408 | 4287 |
| 7-00 | 7 | 12 | R700C | 3043 |
| 7-03 | 13 | 13 | R703C | 2345 |

R108C is a Claire B scene triggered by entering room 1-08 from 1-07.
The entry script checks that the scene has not already played. The Windows test
used door 1 in room 1-07, matched a rendered frame to the retail clip, and
confirmed the completion flag in a native save after skipping the movie.

In Claire A, event zone 7 in room 2-04 starts the cutscene that plays
R204C. The Windows test reached the clip through that event.

In Claire A, event zone 6 in room 4-08 starts the cutscene whose
dialogue leads to R408. The Windows test reached that clip and
continued the dialogue after a skip.

The 7-00 exit event (zone 2) is registered only in scenario A. In the
Windows test, using that zone started R700C, and skipping it handed off
to Claire A's ending movie. Claire B instead uses the train-controls
event in 7-03 to start R703C.

MOV3 imports
preserve STR frame numbers as well as the video and XA sector positions.
When the catalog has multiple copies, the player prefers STR frame numbers
and then sector timing.
This lets R10B and R109 end at their retail frame cutoffs (150 and 676).
The Windows mixed-store check resolved both clips to their numbered copies.
Using both Red Jewels in room 1-0B in Claire A started R10B.
In Claire B, the Valve Handle sequence in room 1-08 changes door zone 1
into an event for R109. The Windows test reached that event position, but
the interaction has yet to start the clip.
It also lets R200, R505 and Leon's R704 play the excerpt requested by the room
script, using the seek and end frame from the original movie table. Rescan
the original raw disc to enable excerpts. Clips that start at sector zero retain whole-movie
playback on older imports. MOV2 includes audio, and MOV1 plays silently at 15 fps.

Rescan raw discs to correct previously imported STR clips whose directory
entries use 2,336-byte XA extent units. The importer uses the same sector
boundary for video and XA audio, so neighboring files stay out of the clip.

Playback places XA file 1, channel 1 on the 150-sector-per-second STR
timeline and selects video frames from the audio stream's consumed source
samples. If the track or audio device is unavailable, the host uses its
video clock. Verify the timing, excerpt boundaries, and channel choice on
retail discs.

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


## Scripted room music table

The room loader uses the scenario's starting cue table. The `Sce_bgmtbl_set`
opcode can replace the entry for the current room or a different room. The
change applies the next time the target room loads, without interrupting a
cue already playing. The sequence enable flags and silent banks follow the
script operands. Overrides live in the imported play session. Reloading a
native save rebuilds this table from the scenario defaults and room scripts.
