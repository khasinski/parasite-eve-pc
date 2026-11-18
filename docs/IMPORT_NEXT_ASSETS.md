# Cross-game import beyond models

Research baseline: `786ac4fd8`. This survey covers the tested local PS1 images and the importer at that commit. No game assets, captures, or extracted output are included.

## PC preview implementation

The Windows importer was tested with the local `External Games` folder on a clean
profile. The game cataloged 778 Sound entries, using the same public services for all
sources. F1 > Import > Sounds offers a source game selector and pages of 64 samples.

| Game | Cataloged samples | Verified extraction path |
| --- | ---: | --- |
| Resident Evil Director's Cut Dual Shock | 141 | 35 `PSXUSA/SOUND` HED/VB pairs using the shared VAB reader |
| Resident Evil 2 | 243 | EDH/VB pairs under `COMMON/SOUND` across the original and Dual Shock discs |
| Resident Evil 3 | 298 | 34 VH/VB pairs under `CD_DATA/SOUND`, split at each waveform's flag-7 terminator |
| Dino Crisis | 3 | Embedded VAB in the US `PSX/DATA/ST60E.DAT` room archive |
| Final Fantasy VII | 93 | Instrument sample addresses in `SOUND/INSTR.DAT` and their waveforms in `INSTR.ALL`, identical across the three discs |
| Dino Crisis 2 | 0 | Offline probe decoded non-silent XA PCM from channel 14 of `STAGE00.XAS`. The game importer still needs raw sectors, channel and clip boundaries, and a first content check. |

The Dino Crisis XA probe also decoded a channel from its `STAGE00.XAS`. XA and raw SPU
are separate formats. The shared SPU decoder handles the bank providers in the table.
For the remaining XA work, preserve the duplicated Mode 2 subheader, demultiplex by
file and channel, and check stream boundaries before assigning catalog IDs. The probe
decoded 48 sectors with FFmpeg in memory and wrote no audio files.

FFVII's entries are instrument waveforms. Its effect sequences and music tracks still
need their own playback or rendering path. The instrument layout was cross-checked against
[the sample bank format](https://wiki.ffrtt.ru/index.php/FF7/PSX/Sound/INSTRx.ALL) and
[the instrument index format](https://wiki.ffrtt.ru/index.php?title=FF7%2FPSX%2FSound%2FINSTRx.DAT).
The imported waveforms use a fixed audition pitch. Native event bindings and interactive
F1 playback were outside the headless verification.

### Why Dino Crisis imported three samples

The current reader at `mods/pe_src_dc1/pe_src_dc1_sound.c` matches exactly
`PSX/DATA/ST60E.DAT`. It uses that archive's one conventional VAB header and fixed
body range, which contain three waveforms. The three entries measure the reader's
coverage of one archive.

The local US Track 1 ISO directory has 422 DAT files under `PSX/DATA`. Walking their
archive entry tables found 389 archives with sound and 490 adjacent type-3/type-4 pairs.
The type-3 entry is metadata and type 4 is SPU ADPCM. All 490 pairs match on the
archive's SPU address and tag fields. Only `ST60E.DAT` uses `pBAV`. The other 489
use `Gian`. [PSX-SPX documents the archive and audio entry types](https://psx-spx.consoledev.net/dev/ps1/cdr/cdromfileformats/archives/#dino-crisis-1-and-2-psxdatadat-and-dbs-and-tex-dummy-header).

For every `Gian` bank, the little-endian count at header offset 10 agreed with the
number of SPU blocks with flag byte `0x03` or `0x07` in its paired body, and the
final block was a terminator. All type-4 blocks had supported predictor and range
values. The `Gian` banks contain **3,698 sample occurrences**, representing **910
distinct encoded clips** by exact SHA-256. The largest encoded clip was 91,184 bytes.
An in-memory probe decoded representative samples from `CORE.DAT`, `DOOR00.DAT`,
and both banks in `CLEAR.DAT` to non-silent PCM, including a flag-`0x03` clip.
The counts cover embedded SPU banks. XA streams and type-5 entries were outside this
pass. Pitch, loop metadata interpretation and gameplay cue names still need verification.

[Modern Edition](https://github.com/Rotmill/Dino-Crisis-Modern-Edition) provides runtime
context. Its companion [Dino Crisis ME Studio](https://github.com/Rotmill/Dino-Crisis-Studio)
documents disc-based sound browsing and exporting. Its
[CMake source list](https://github.com/Rotmill/Dino-Crisis-Studio/blob/main/CMakeLists.txt)
identifies `src/dat.cpp`, `src/sound.cpp`, and `src/synth.cpp` as reference points.
Studio is a candidate for comparing exports against the same US disc revision. This
survey checked the public project documentation and source list and derived the bank
counts independently from the local disc.

For the importer, share a bounded archive walker with the existing Dino Crisis source
mod, route VAB and Gian banks into the shared SPU decoder, and preserve the existing
`dc1:sound/st60e/waveNN` IDs. Use archive, bank and waveform identifiers for the new
entries so identical payloads can share storage while retaining their source references.
Check that a broad sound reader and the costume reader can both run for a matching
archive, or use a deliberately disjoint match on verified files. The current scanner
chooses the first matching reader. The local costume `CORE0?.DAT` archives did not
contain type-4 blocks. No production reader was changed in this pass.

## First implementation target: one Resident Evil 2 sound effect

The strongest extraction result is RE2's `COMMON/SOUND/ARMS01.EDH` and `ARMS01.VB`. Both files matched across the two tested original US discs and Dual Shock US Disc 1. The 3,240-byte EDH contains a VAB header at byte 128. Its one program declares six tones referencing a bank of six waveforms. The VAB size table accounts for all 23,600 bytes in the VB. All six samples decoded in memory to non-silent PCM in a local probe, including waveform 2 (2,080 compressed bytes, 3,640 decoded mono frames before trimming). This proves extraction of the waveforms. The exact gameplay event, pitch, and envelope for a given tone still need verification.

1. Add a versioned Sound type whose neutral payload is bounded mono PCM with source bank, program/tone, and waveform provenance. Preserve pitch and loop information when verified. Give a waveform a stable source ID, for example `re2:sound/arms01/wave02`.
2. Add a RE2 file-pair reader for just this bank. Take its source identity from **both** file hashes so changes in the header invalidate any cached output. Reject inconsistent tables and out-of-range waveform spans. Keep the existing costume cache working alongside the new type.
3. Make the first in-game gate scan, select, and audition one clip from F1 > Import. Use the public asset and audio services described below with the existing game audio output and a generated waveform test fixture. Confirm repeat scans reuse the catalog entry, corrupt banks fail cleanly, and closing or switching previews releases playback state.
4. After checking a tone against reference gameplay, bind the clip to one native weapon cue. Keep the original cue when selection, loading, or playback preparation fails. Verify the native cue event and equipped weapon ID before adding a hook. `src/main/cdrom/scene_assets.c:117` and `:740` are shared audio dispatch, which also serves unrelated cues. First identify the actual weapon event caller.

For the probe, the ISO directory was walked through the data track recorded in each CUE. Normal file bytes came from the 2,048-byte data windows of raw sectors. For ARMS01 the probe walked the VAB program/tone table and its little-endian waveform lengths in eight-byte units, then decoded each bounded 16-byte SPU ADPCM block to 28 mono frames. Playback pitch, loop behavior, and a bit-exact comparison with a second decoder remain open. The probe wrote no media files.

## Public asset service for mods

Publish a host-owned, versioned `assets` service through `pe_service_publish` and document its table in `mods/host/pe_services.h`. Mods obtain it with `pe_service_get("assets", 1)`, which can return NULL. Keep the public header independent of any source reader, import job, builder, or UI. F1 > Import and the first gameplay integration should use the same service as an independent mod.

| Operation | Contract for a mod |
| --- | --- |
| Browse | Query indexed entries by asset kind, source game, tag, or pack, in bounded pages. Return display name, capabilities, and a stable asset reference. Listing never decodes payloads. |
| Acquire by reference | Use an explicit asset chosen by the player or mod. Persist the stable reference as a string, and reacquire it on load. A missing pack or unsupported type returns an explicit status. |
| Resolve a slot | Ask for the selected asset for a slot such as `weapon:<key>/sound/fire`. Apply the shared player-choice and pack-priority rules, then return the same kind of handle used by direct acquire. The calling mod supplies its native fallback. |
| Check status and release | Acquire may return pending while preparation runs. Use a ready handle with the appropriate playback or rendering API. Release it at the end of use. Scope requests to the owning mod so disable/unload cancels its pending work and releases its claims. |

Use two persistent identifiers. A source catalog ID such as `re2:sound/arms01/wave02` identifies the input for previews and build recipes. A pack entry ID identifies a prepared runtime asset, including its variant. A single source can produce several runtime variants. Physical paths, cache hashes, and transient handles stay private to the implementation. Store only IDs or slot choices in mod saves.

Keep four roles independent:

1. **Source providers** register file discovery and decoders. The RE2 reader emits Sound entries from two files. A later game can emit the same type without changes to the UI or consuming mods.
2. **Type handlers** define schema version, validation, storage, preview preparation and runtime loading for a kind. Shared indexing and resolver code reacts to the registered kind. Sound, Song and ImageSet can be added independently. Reader and handler registration belong to the provider API. Ordinary consuming mods only need the asset service and the relevant type's playback or rendering API.
3. **Builders** convert catalog entries into prepared pack entries with optional slot offers. The shared pack registry indexes manifests and selects candidates. The resolver loads prepared entries and owns their residency. Import workers and previews can read the neutral store, while gameplay requests use prepared assets.
4. **Consumers** choose an asset by reference or by slot and hand the handle to a type-specific service, such as `audio.play_clip`. The audio service owns mixing and active voices. The asset service owns loading and residency. F1 previews use a bounded preview request for a catalog entry, with the resulting sound played through the same audio service as a gameplay clip.

Index metadata at pack registration or refresh. Load payloads on demand and deduplicate concurrent requests for the same prepared asset. Include variant and pack revision in the cache key so rebuilding a pack cannot change the bytes under an active handle. Bound worker queues and per-kind memory. Pin active handles and reclaim unpinned entries first. Schedule room transitions in batches where needed while accounting per asset. Audio playback and frame hooks must never wait on disc I/O or decoding. Keep the previous valid asset or use the native fallback if the new request is missing, rejected, over budget, or still pending at the cue.

**Consumer example.** A weapon mod can offer a sound picker filtered to the Sound kind. It saves the selected pack entry ID, preloads its handle on equip, and hands the ready handle to the audio service on fire. A separate mod can use the same clip for a menu action or bind it to its own slot without knowing how RE2 stores sounds.

**Acceptance gate.** One RE2 Sound reader, one F1 preview and one independent sample consumer should exercise the service using generated test audio in CI. The independent consumer includes only public mod headers and plays a prepared clip by ID and by slot. Verify that both paths share a single resident copy, that the consumer works when the importer UI is disabled, and that removing a pack or disabling a consumer during a pending load leaves the native fallback usable. Add a second test provider that emits the same Sound type to show that adding a game requires no change to either consumer.

## Other source targets

| Asset | Local evidence | Next verification |
| --- | --- | --- |
| RE2 music | The original US discs each expose 108 `.BGM` files. Dual Shock Disc 1 exposes 114. `COMMON/SOUND/MAIN00.BGM` matched byte for byte across all three (120,612 bytes, `pQES` at byte 0, `pBAV` at 1,568). | Render one verified sequence with its own instruments into PCM during import. Confirm the end/loop markers and retain the musical loop in the built track. Then stream one PE BGM slot and test start, fade, stop, resume policy and native fallback. |
| RE2 and RE3 room art | The tested RE2 discs each expose 125 `.BSS` files, and the RE3 disc exposes 169. RE2 `COMMON/BSS/ROOM100.BSS` is 589,824 bytes and has an MDEC-style header at successive 64 KiB boundaries. RE3 `CD_DATA/STAGE1/R100.BSS` is 131,072 bytes with the same boundary pattern. | Decode one RE2 view and inspect its foreground mask. For the first PE integration choose a single static view with a similar camera and author the alignment explicitly. Verify actor occlusion and keep native collision, camera, scripts, and layer timing. |
| Dino Crisis XA | `PSX/DATA_XA/STAGE00.XAS` contains 3,234 raw Mode 2 audio sectors with matching duplicate subheaders across eight file/channel combinations. | Reassemble one channel and identify its content before classifying it as music, dialogue, or another sound. Add a bounded raw-sector path when a confirmed in-scope asset requires it. |
| FFVII music and room art | Disc 1 files such as `ENEMY6/FAN2.SND` begin with `AKAO`. All three discs expose 787 `.MIM` entries. `FIELD/MD1_1.MIM` and its companion `.DAT` were located on Disc 1. | Identify the FFVII sample banks and render a cue with a source-specific AKAO implementation. The shared signature alone gives no playback compatibility guarantee. For backgrounds, verify the DAT tile map with the MIM graphics before attempting a PE fit. |

Six tested CUEs, covering both Dino Crisis games, RE1 Director's Cut Dual Shock, both original RE2 discs and RE2 Dual Shock Disc 1, have separate AUDIO tracks. After skipping the 150 sectors before INDEX 01, all six have exactly 15,750 sectors (210 seconds) and identical SHA-256 hashes. Treat these as a shared disc track of unverified content. Use the confirmed in-game banks for the first music and sound targets.

## BioRand as a reference

| Upstream | Use in the PE importer | Evidence and limits |
| --- | --- | --- |
| [BioRand Classic](https://github.com/biorand/classic) | Use its music categories as inspiration for optional catalog tags. A mod can ask for music tagged `safe`, `creepy`, or `danger` and then bind a particular choice to a PE slot. Tags stay editable catalog metadata. Asset IDs, pack selection, and playback timing belong to PE. | Classic documents WAV/OGG and tag folders for custom music. Its documented target is the Sourcenext PC ports with Classic REbirth, so its music workflow does not establish how to decode the PS1 banks. |
| [biohazard-utils](https://github.com/biorand/biohazard-utils) | Start here when looking for reusable format knowledge or desktop comparison tools for a classic Resident Evil asset. Verify the specific file and edition against local media, then keep the reader inside the relevant source provider and feed the common asset type. | Classic pins this .NET library as a submodule (the inspected tree showed `6095aee`). The library's [MIT license](https://github.com/biorand/biohazard-utils/blob/master/LICENSE) permits code reuse under its terms. The repository overview alone does not confirm support for PS1 EDH/VB, BGM or BSS. Track the exact source file, revision and applicable notice if a verified parser is adapted. |

The first PS1 milestone stays the independently decoded RE2 ARMS01 bank. If a BioRand parser or a PC source proves useful, add a comparison fixture or a separate PC reader when the source format is verified. Both platform readers should emit the same Sound interface. Keep source variants distinct unless their identity has been confirmed. The mod using the sound should depend only on the asset service and audio API. Keep the shipping reader in the C import pipeline for PC and PS5.

## Gaps between extraction and gameplay

- `mods/host/import/pe_imp.h` defines type bits for Sound, Song and ImageSet. The sink at `mods/host/import/pe_imp_store.c:69` only accepts Model. Cache markers and catalog reloads also assume Model (`:235`, `:250`, `:284`, `:406`). Add validation, serialization, and versioning for each type as it ships.
- The current reader callback takes bytes from one ISO file. Use a narrow paired-file lookup for EDH/VB with both inputs tracked. Keep XA separate. `mods/host/import/pe_imp_disc.c:54` chooses the first data track, and `:92` copies only 2,048 bytes from a raw sector, omitting the headers and end of XA audio sectors.
- `mods/pe_import/pe_import.c:131` filters the catalog as Model, and `:298` sends manual builds to the character-pack writer. Add type-aware browsing and an audio preview path first. Wire the gameplay sound slot through the planned registry/resolver when available so it inherits pack selection and native fallback.
- PE's BGM uses its AKAO driver (`src/main/akao/voice_driver.c:433`, `src/main/akao/sequencer.c:144`). The menu uses native fade controls (`src/main/item/active_list.c:43`, `:64`). Replace music at the BGM lifecycle boundary so native cue timing, SFX, and menu fade behavior survive. Stream bounded buffers through the game audio output. The earlier budget survey proposes a 4 MiB CPU cap for music.
- `docs/HD_BACKGROUNDS.md` and `src/main/render/mesh_entry.c:346` give the existing room replacement path. Reuse its native coverage and depth model and test PC and PS5 paths separately before claiming renderer parity. PS5 build and playback were outside this survey.

Format references: [Psy-Q sound file formats](https://psx.arthus.net/sdk/Psy-Q/DOCS/Devrefs/Filefrmt.pdf), [RE room background format](https://github.com/pmandin/reevengi-tools/wiki/.BSS), [FFVII field and tile map formats](https://qhimm-modding.fandom.com/wiki/FF7/Field_Module), and [FFVII PS1 audio overview](https://wiki.ffrtt.ru/index.php/FF7/PSX/Sound/Overview).
