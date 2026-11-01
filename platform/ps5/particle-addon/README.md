# Optional native debris source add-on

This separate source-only proposal applies to the PR18 prepared11FD native source export. It does not change that export's default build. It reproduces exactly13 LF-normalized final D9D4 game outputs:7 modified and6 new files. `manifest.json` binds every before/after digest and the public source pins. No binaries, shader arrays, dependencies, assets, saves or private paths are included.

Scope: one optional `3D debris` Mods row, Original / Hybrid / Geometry only. Original is the runtime default. FallingSprite phase2 and BurstOrbParticle mode2 states2/3 have a real packet/render connection. This is independently authored procedural tetrahedral decoration, not smoke, universal effects or faithful artist-authored replacement. Original simulation, RNG, collision/damage, stars/shadows/rings/halos/beams are preserved. Geometry-only suppression is limited to exactly captured original FT4 groups after successful real-arena admission; rejection retains originals. No broad OT unlink. Native game/GPU visual or performance results are not established by this export.

## Source preparation and optional build

1. Prepare a fresh source output using the public11FD `prepare-source.cjs` and its pinned archives. Keep its base support/startup project intact.
2. Review this add-on and `particles.patch`. Do not apply it to an unrelated checkout. `node apply-addon.cjs /path/to/prepared/game-src --check-only` checks the13 preimages and retained public PsyZ prerequisites without source writes.
3. For manual CMake configuration, replace only the existing project-include argument with this package's `project-include.cmake`, retaining all other public native build arguments. Pass:

```text
-DCMAKE_PROJECT_parasite-eve-pc_INCLUDE=/path/to/addon/project-include.cmake
-DPE_PS5_BASE_STARTUP_PROJECT=/path/to/prepared/game-support/startup-project.cmake
-DPE_PS5_PARTICLE_ADDON=ON
```

Alternatively, `build-native-optional.patch` is a small, separate adapter for the exact public11FD build-native.cjs digest recorded in the review report. It keeps the original path when the optional config is absent. With that adapter reviewed/applied, tools-config.json may add:

```json
{
  "particleAddon": "/path/to/addon",
  "enableParticles": true
}
```

Keep all existing tools/dependency/capacity configuration fields. This adapter passes the same CMake wrapper and explicit flag. `enableParticles` must be boolean true; absent/false defaultsOFF. The original public build-native.cjs without this adapter does not know these options and would keep its original include path.

`PE_PS5_PARTICLE_ADDON` defaultsOFF. OFF applies no particle patch and registers no particle sources/definitions/UI changes. ON validates all preimages, applies the exact source patch to the caller's prepared export, then registers mesh/adapter/renderer sources and explicit `__psyz`, `PE_PS5=1`, `PE_PS5_GPU_NATIVE=1` target definitions; only the Falling adapter receives `PE_BATTLE_PARTICLE_PROTOTYPE=1`. Runtime preference still defaultsOriginal. Exact already-applied outputs are idempotently verified. Mixed or unrelated sources are rejected. To change a patched export back toOFF, make a fresh public export: CMake rejects an already-patched tree rather than leaving unavailable controls or unresolved hooks. Do not alternate flags in one patched export.

The public baseline recipe still owns shader generation, provider/CRT construction, dependency/input guards and linking. This package does not modify PsyZ/SDL/shaders/save bridge/aspect code. A fresh full SDK build of this optional portable ON recipe passed independently of the selected private D9D4 build. See BUILD-PROOF.json: all13 particle outputs, combined85 reviewed source outputs and six generated shader headers match; all1,546 exported PsyZ files remain unchanged,11 retained input trees verify,352 imports match the selected set, and static runtime audit passes. Native CRT/helpers/heap bridge, SDL, PsyZ, game and GLSL were rebuilt. Mesa was verified retained, not rebuilt. This establishes portable build integration, not console appearance/performance or gameplay. Manager owns publication/integration.

## Render contracts and bounds

Original render computations always execute. Hybrid adds geometry. Geometry only changes stable private link-only anchors for complete admitted groups; near-plane, projection/depth, changed-edge, slot, queue or real-arena capacity failures keep originals. The native synchronous DrawOTagEnv→psyz_addque2→GPU_Enqueue path copies packet words/PGXP companions before returning; asynchronous hardware references uploaded vertex/index buffers, not these host anchors. EndFrame preserves anchor memory; two slot pools are not assumed hardware fences. Zero-length and terminal tags follow the retained native parser contract.

Shared limits:64 new triangles/frame, maximum2,048 late bytes in the actual57,344-byte animation arena per slot;32 groups;256 anchors/slot,20KiB total private anchor storage. No texture allocations, new shader pass, gameplay state/RNG writes or view/GTE/PGXP mutation. BurstOrb uses named RoomDampedSpark fields with original16/24-tick lifetimes; other states remain unchanged. Large scenes can fall back to originals.

## Provenance and licensing

Projector header `pe_particle_project.h` is an MPL-2.0 adaptation of retained PsyZ integer GTE math/table. Its SPDX/source/modification/license notice stays in the actual source patch. `PROJECT-PROVENANCE.json` binds the retained original file and repository license; `PsyZ-LICENSE.txt` preserves the repository's directory-specific license mapping. The source game and other linked components retain their existing licensing boundaries: this package does not grant permission to redistribute game assets or linked game binaries. Newly authored mesh/adapter/ownership logic is project source; inherited source files retain their original provenance. New export/CMake/Node glue is MIT as marked.

Review verification applies the patch to a copied real public prepared tree and compares all13 outputs to selected D9D4, checks defaultOFF/ON/idempotence/reject-disable wiring, and validates the optional public recipe patch. These are source/configure checks, not mocked gameplay or console validation. Human owns appearance, performance and gameplay testing.
