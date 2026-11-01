# Native GPU source export and build proof

This is an independent opt-in source-export recipe. It preserves the fork's existing CPU/default path and original source checkout. It exports exact original0.8, PsyZ and SDL pins into a fresh directory, applies reviewable component patches, then builds the native SDL_GPU/RADV route. No whole private old main.c, retail data, saves, proprietary modules or compiled library/executable is shipped here.

The game patch consists of changed hunks against original0.8, not a replacement source file. Existing PS5 path/control/audio/assist/display-reset changes needed by the reviewed candidate are included; legacy CPU fallback source remains guarded and is not expanded here. This is a larger predecessor export than the later six-file conformance/HD fix alone. The manifest enumerates every affected file for review.

Pins: game `8c52cdde5d60ee52f00913517ffb593e9521bf2d`, PsyZ `db796262b2a696241ec8f3965ebbfbcbc45bfc30`, SDL `fa2c02bb6e21974a89ea9824bc53c9932abe5f9c`. Public Mesa/RADV and platform pins/digests are retained in DEPENDENCIES-PINS-LICENSES.json and build-inputs.json. This recipe verifies retained prebuilt target libraries; it does not rebuild Mesa/ACO/libc++ or certify their upstream source-to-binary equivalence.

## Preparation

Supply original source ZIPs produced by `git archive --format=zip` at the above pins, named game.zip, psyz.zip and sdl.zip. Archive SHA256 values in manifest.json identify the exact retained exports; preparation rejects any drift. No downloader or hidden Git/index mutation is used by the recipe. It never alters the upstream checkout/submodules.

```powershell
node ./prepare-source.cjs <archive-directory> <fresh-export-output>
node ./build-native.cjs <fresh-export-output> <retained-public-prefix> <tools-config.json>
```

Tools config provides `cmake`, `ninja` and `webpSource` paths. An optional `capacityGuard` file may provide a caller-owned utc timestamp, numeric usedPercent and optional stopUsedPercent (maximum95); stale/invalid/at-threshold data fails closed. No developer account, credentials or account-service API is required. The old explicit `mainCapacity` config key remains an alias for local callers. Project automation always supplies its actual main-account guard; this optional public interface does not change its reserve policy. Pinned retained inputs are verified as described below. Caller host tools have an explicit recorded/pinned policy rather than being silently treated as recipe-pinned. The retained prefix has the layout named in build-inputs.json; original dependency archives/notices must remain available to the reviewer.

Source preparation verifies every patched output, copies the already pinned SDL UI shader headers into the game's expected nested include path, and stages only text support. Four PsyZ GLSL modules are regenerated with the pinned glslang compiler at build. Original UI shadercross generation/compiler provenance is not fully available; existing pinned source header bytes are retained, not falsely described as regenerated.

Native CRT, compiler-rt integer/TLS/CPU helpers, heap bridge, SDL, PsyZ and game are compiled from source into the new output. Original precompiled app_crt.o or previous build objects are never reused. A derived RADV archive renames only the six directory ABI imports; original archive stays unchanged. A derived SDK libc archive excludes the payload loader's dlfcn.o. Existing public provider mappings and unsupported Linux readlink behavior are retained rather than replaced with fake success.

The native application startup remains distinct from payload CRT. Native linker and unwind scripts come from the pinned public PSXS5 source. Support sources retain their original notices. This recipe only links an intermediate native-title ELF; SCE conversion, import/runtime audit, packaging and human console testing remain manager-owned subsequent steps.

## Input-integrity scope and caller host tools

`build-inputs.json` pins selected retained target libraries/source/tool executables, including llvm-ranlib and llvm-readelf. `target-inputs.json` additionally binds complete sorted file inventories and raw-byte hashes for SDK target headers and libraries, SDK toolchain/win/bin/linker-script trees, RADV SDK libc++ headers, Clang18 resource headers and the public platform include tree. It also binds the complete configured WebP source tree; the prior per-file WebP hashes remain. Missing, added, changed or symlinked members fail preflight. These small tree digests cover 4469 retained files without shipping those headers or binaries in the recipe.

Caller-selected CMake and Ninja are **not recipe-pinned by default**. Their resolved executable paths, SHA256 values and `--version` outputs are recorded in the new export's `INPUT-PREFLIGHT.json`. Callers may supply `toolHashes.cmake` and `toolHashes.ninja` in their tools configuration to require independently trusted executable SHA256 values; mismatch fails before querying that executable's version. The build uses the resolved recorded paths. This does not hash CMake's surrounding module installation, arbitrary host DLLs, shell, Node runtime or other operating-system dependencies. It is explicit retained-target input integrity plus recorded caller-host identity, not complete pristine-archive reproducibility or an execution sandbox. Keep the selected prefix and tools immutable during a build.

The Windows host's `tar.exe` source extractor, Git patch engine and Node runtime remain caller-installed prerequisites outside this build-input pin set. Source archive/patch digests and all patched output hashes are checked independently. Public dependency source-to-binary closure (including Mesa/ACO/libc++) remains separately disclosed above.

The independent G07/G16 shader ABI gates are **not exported or enforced by this recipe**. Shader regeneration, pinned bytes and provenance records do not establish descriptor/layout/vertex ABI validation. No G07/G16 success should be inferred from this build.

## Evidence boundaries

Successful compilation/linking does not establish firmware admission, scanout, actual GPU rendering, texture replacement, widescreen, save behavior or exit correctness. Existing conformance opt-in is tightly identified/default-off; required device features/queues/extensions remain checked. Existing local-HD options distinguish cheap manifest presence and requested/enabled flags from textures actually rendered. Display/HD settings reset for the owner's current tests; control/audio/save/asset paths are retained. The conformance/local-HD source changes do not include later cache syscalls, retirement experiments or automatic CPU failover.

## License/publication boundary

SDL and adapted audio/input/display sources retain Zlib notices. PsyZ platform changes are identified MPL-2.0; other PsyZ directories include differently licensed or unlicensed material. Public native CRT/platform/PSXS5 link support are GPL-3.0-or-later; LLVM runtime is Apache-2.0 with LLVM exceptions, and glslang carries its bundled notices. The game repository has not selected a project-wide license. These source patches do not relicense upstream files or grant permission to redistribute a GPL-linked full game. Manager must review source publication rights and per-file obligations before a PR or binary release. Included notices and exact source/library pins are review inputs, not a blanket license clearance claim.
