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
- On an existing profile, use **F1 → Import → Scan RE2** with the original raw disc images available. The updated readers add the room sounds and voice lines to the import catalog.
- Exact spatial mixing, redirected and layered tones, non-room script banks, special stage 8 dialogue, field actor cues, and synchronized movie audio remain follow-up work. An ISO without the raw XA sectors cannot supply the voice tracks.


## Scripted FMV video

The PE1 host can play selected imported STR movies requested by RE2's
`Movie_on` room opcode. The room script pauses while the video plays. A new
Cross press skips the clip, and the host restores the room picture on exit.
Missing assets and unresolved movie IDs leave the script free to continue.

The initial mapping covers IDs 4, 8–13, with IDs 8, 10, 12 and 13 limited to
the verified Leon filenames. Playback uses an initial 15 fps cadence. The
MOV1 catalog stores ordered MDEC payloads and compacted XA records, without
the original video frame numbers or sector positions. Exact timing,
scenario-specific movie variants, and movies that seek to a segment need a
timestamped import format and further disc verification. This initial
player displays video silently.
