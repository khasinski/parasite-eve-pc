# Current native PS5 source export — installer, guarded checkpoints and presentation

This is an isolated, source-only refresh for manager review. It reconstructs the native source used by `dialogue-modern-owned-20261101-a1` (reviewed B0ED native candidate), with explicit public-input differences below. It includes the controller first-run installer, persisted display/menu policy, bounded optional movie cleanup, presentation-only sprite filtering, seven-tab menus, source-known model particle lights, actual player texture matching, optional Latin dialogue Plex alpha and typed guarded checkpoint/only-assists ownership. No game assets, private background/player pack indexes, saves, personal configuration, credentials, SDK/library/game binaries or executable proofs belong in this package.

The manager owns Git/PR publication. No checkout/submodule metadata was restored, no game was launched and no console upload was performed to prepare this export. Historical proof files are named HISTORICAL-* and are not current public-build claims.

## Exact source basis

The patch bases remain the previously reviewed pins in manifest.json: game8c52cdde5d60ee52f00913517ffb593e9521bf2d, PsyZdb796262b2a696241ec8f3965ebbfbcbc45bfc30, SDLfa2c02bb6e21974a89ea9824bc53c9932abe5f9c. Selected native game/PsyZ source includes selective upstream0.9 integration (gameb7c7274d8d97167ab0f5a856f161f93c98325bee, PsyZdeb979278b3a1bee4c0a4123587fb530afa4dfcf) plus original project native changes. It is NOT an untouched whole0.9 checkout. Existing source notices/pins are retained; see CURRENT-SOURCE-PROVENANCE.json and DEPENDENCIES-PINS-LICENSES.json.

148 substantive normalized source records reconstruct through three original archive pins;40 SDL/Khronos source mirrors derive from that same SDL source. State modules and native support are source files, not old full-file boot-main substitutions or prebuilt CRT objects. Original UTF8 CRLF/LF differences are checked by normalized digest and normalized before applying exact patches. Generated shader headers are reproduced from retained original GLSL and pinned glslang, including current sprite filtering, Duck player-image sampling, dialogue alpha and movie cleanup. Embedded original game UI shader provenance is retained as inherited source; no new claim that all embedded UI arrays were independently regenerated is made.

## Prepare without downloading

Supply the exact game.zip, psyz.zip and sdl.zip source archives whose SHA256 values are in manifest.json:

```
node prepare-source.cjs <pinned-archive-directory> <fresh-export-root>
```

The destination must not exist. The pinned overlays are exact-position unified text; a small MIT Node applicator rejects unexpected paths, stale context and count mismatches, without Git or fuzz. Preparation checks archive/patch/preimage/output hashes, applies only into that new source tree, copies the declared source-only support/state files, mirrors SDL source headers and creates a fail-closed no-pack sentinel. It never changes an existing Git checkout/index/submodule or retrieves dependencies.

The package intentionally excludes the8146-entry private HD pack index. If no pack is supplied, the installer native-HD import verifier explicitly returns failure; game/disc installation without optional HD remains available. It never treats an empty verification loop as a valid pack. To bind your own compatible flat native pack locally:

```
node generate-native-pack-index.cjs <fresh-export-root> <your-owned-native-pack>
```

This reads only regular unlinked flat files, bounds count/file size/total size, rejects unsupported names and records exact SHA256/size. No archive extraction/network download is used. The generated index is private caller input; do not publish it or assets with the source package. It is identity verification of supplied files, not a promise of every scene's asset coverage. The normal native loader still validates/loads assets using its existing behavior.

## Build interface

Use the reviewed public-prefix layout, dependency/input manifests and generic build-tools example described in PUBLIC-PATHS.md:

```
node build-native.cjs <fresh-export-root> <retained-public-prefix> <tools-config.json>
```

The optional `nativePack` build-tool field invokes the same local index generator; it is not an app preference and no developer account or credential is needed. The build regenerates all7 selected shader headers, then creates an actual public-prepared source/dependency cohort identity, rebuilds original public CRT/compiler helpers/SDL/PsyZ/game and keeps source/digest checks. The source identity is32bytes and is NOT an executable self-hash. A public-generated source/pack identity cannot load a private49FD checkpoint. No proprietary Sony SDK/module is shipped. Retained Mesa/RADV/platform libraries remain digest-verified public-prefix inputs, not newly rebuilt driver claims.

This native recipe deliberately builds the reviewed mods host target and includes the state component assembly plus only-assists gate. Default desktop source builds do not enable these PS5 state definitions. The native application linker/startup path is distinct from payload CRT. No account/helper-specific guard is required; an optional caller-owned capacity guard remains supported.

## Checkpoint and visual behavior limits

Checkpoints are experimental PARTIAL same-process/current-room/cohort/actor/settings operations, not arbitrary-frame/cross-room/process state support. They allow either strict All mods Off since launch or only source-owned Playtest assists active since launch; other active/unknown mods and live host resources reject. Exact assist choices are compatibility words, never restored preferences. Supported Original/Hybrid/New geometry and optional particle lights have explicit typed ownership and exact settings. Player replacements may remain On once their valid private index loads. GPU schema4 also owns native dialogue-font authority and regenerates derived glyph sheets; no permanent font-Off/never-used restriction applies. Unsupported live effects/resources still reject. Unowned native event/effect rooms remain rejected. Explicit Capture/Load closes its menu and queues at the CPU barrier; queued is never reported completed. Native VRAM is restored while high-resolution presentation is reconstructed, not exact saved high-resolution pixel history. Normal memory-card saves remain separate and untouched by checkpoint files.

Seven native tabs are Graphics/Mods/Cheats/Save states/Audio/Controls/System, switched by L1/R1 with Dpad/left-stick selection and per-tab scrolling. L2+R2+Touchpad opens Save states only; L1+R1+Touchpad opens Cheats. Pinned Resume/Back/Quit, existing controls/audio/asset paths and normal-save bridge remain. Cleanup/filter toggles retain Original/Off fallbacks; no invented HD art, DLSS, ray tracing or60Hz implementation is claimed by this batch.

## Current evidence, accurately bounded

Fresh preparation from the three exact archives passed:148 patched source outputs and40 mirrors verify. Actual pinned GLSL compilation reproduces all7 selected-production headers byte-for-byte after LF normalization. The public-only no-pack verifier adapter compiled as its actual SDK TU with the fail-closed generated sentinel. The private selectedB0ED source has a full SDK/native conversion and independent source reviews; that is NOT a full fresh build of this changed public recipe. No new full public game rebuild was performed for this export, and no target/runtime/controller/gameplay test was performed. CURRENT-PREPARATION-PROOF.json records those limits. Host installation/DLL/source-to-binary dependency closure remains incomplete as previously documented.

## License and publication boundary

No blanket project license is invented. The upstream game LICENSE.md still selects no blanket open-source license, and original authors retain their rights. New source files explicitly marked MIT retain that file-level notice; cleanup/bootstrap/source tooling MIT notices are in notices/native-module-MIT.txt. Checkpoint/core integration without such a notice retains existing project copyright status. Preserve original PsyZ, SDL, GPL public platform/runtime, AMD FSR1, IBM Plex OFL and other component notices. Source export does not assert that a combined GPL-linked game binary is cleared for public redistribution, and software notices grant no rights to retail discs/art/supplied textures.

Only this package directory is the proposed public payload. Its parent contains local preparation/proof scratch and must not be copied wholesale to a repository. No public publication/message is performed by this task.

## B0ED-specific behavior

Graphics Dialogue font defaults Original; Modern(Plex) changes supported Latin dialogue only at2x or above with original layout and glyph fallback. It does not replace Triangle-menu/title/day art. Actual unchanged-font frames avoid readback; palette/reference authority and queued marker lifetimes are explicit. StateGPU Begin now drains the native copied-command queue with empty direct-vertex/index preconditions before the witnessed SDL fence, fixing a source-backed capture-order hazard; this is not runtime qualification. Existing normal saves, input/audio and game/movie presentation remain separate. No private67PNG/player-index subset or native8146-entry background index is supplied by this public export.
