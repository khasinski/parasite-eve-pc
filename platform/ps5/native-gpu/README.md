> Selective upstream update: the current export adds PR35 widescreen-band coverage, PR40 released-target guards and PR36 M319 mirror correction. Preparation now verifies 76 source records. THREEFIX-REVIEW.md and THREEFIX-SOURCE-PROOF.json describe exact pins and compatibility. The full public build proof below is historical to CE80; no new public baseline compile is claimed for these three source updates. The separate selected native 7195 build compiled these same changes with its particle stack.
# Native GPU source export: committed presentation and native card enumeration

Source-only update to draft PR18 at97cd8e121. Selected production source is widescreen-card-loading-20261031-a1 (local eboot identity11FD5508105667C7595186E6A3F1465F2894818D19848DEABB65643F5BA70D80, recorded only as provenance; no binary is supplied). It retains upstream game/PsyZ/SDL pins, source notices and reviewed PR18 linker quoting/input-integrity safeguards. It does not mutate the checkout or existing CPU/default build, fetch dependencies, run a game or publish anything.

This update exports committed per-buffer presentation metadata and queue boundaries so frozen native settings do not reinterpret an old raster with newly requested aspect settings; the new frame supplies its own presentation contract on Resume. It also binds game directory enumeration through the already linked public ps5platform provider, isolating provider/game DIR and dirent layouts and handling scandir as well as direct streams. It does not create missing widescreen room art or certify console save detection/loading.

The selected source also includes its prerequisite UI/effects behavior: persisted display choices, visible Cheats / assists entry with unchanged OFF defaults/activation rules, resolution observations, model fill, actual FSR1 and movie side fill. These are preserved from the chosen source rather than silently deleting prerequisite code. No particles, CRT filter, G11/G12/G13 or separate G07/G16 ABI-gate experiment is included. UPDATE-SCOPE.json lists the exact omitted particle delta separately.

## Prepare and build

Original pins: game8c52cdde5d60ee52f00913517ffb593e9521bf2d, PsyZdb796262b2a696241ec8f3965ebbfbcbc45bfc30, SDLfa2c02bb6e21974a89ea9824bc53c9932abe5f9c. Supply the exact three original source archives/digests documented in manifest.json. No archive or SDK binary is shipped here.

```
node prepare-source.cjs <original-pinned-archive-directory> <fresh-export-root>
node build-native.cjs <fresh-export-root> <retained-public-prefix> <tools-config.json>
```

Use tools-config.example.json and PUBLIC-PATHS.md for the public-prefix and caller CMake/Ninja/WebP layout. Capacity input is optional and caller-owned; no developer account/helper/credential is needed. A supplied guard must be fresh and below its cutoff. The recipe verifies pinned source/archive/patch/dependency/tool inputs, records or optionally digest-pins caller CMake/Ninja, rebuilds the original reviewed CRT/compiler helpers/heap bridge and SDL/PsyZ/game from source, and regenerates shader arrays from GLSL. No precompiled app_crt.o is copied.

There are74 reviewed patched source outputs (game25/PsyZ26/SDL23), plus six generated shader headers. All patches are against the clean original pins, not a copied whole private main.c. The native-directory bridge is compiled in a separate object with the matching retained provider SDK headers; the game adapter keeps the game's SDK context. Full provider header-tree digests are now included alongside the previous consumed input closure. A local source errno include adapter uses the existing public sys/errno.h; no SDK metadata or links are restored.

Generated PSX/clear, FSR1 and movie side-fill shader headers are NOT included as hand-copied arrays in patches. The pinned compiler recipe reproduces the selected production headers byte-for-byte (normalized LF), and checks their manifest digests. AMD's original MIT source headers/license and pin are preserved. This is source/compiler provenance, not a claimed G07/G16 shader ABI gate or device execution.

## Evidence and limits

Fresh isolated preparation against the three original archives applies all patches and verifies all74 source outputs. Regeneration verifies all six selected production shader headers. One full fresh build of this exact public recipe passed from the reviewed source copy. Native CRT, compiler-rt helpers, heap bridge, SDL, PsyZ, game and GLSL were rebuilt; eleven retained-input trees and all74 source/six generated-header digests verify. Resulting ELF SHA256B63669986087FA4178BEA998A6498A84456D49F8A610DF3060FAAC750BA9CE3B passes the public import/runtime audits (352 imports; existing libkernel getdents, no refused libc directory imports). BUILD-PROOF.json records sanitized commands/tool identities and explicit limits. No target game/console execution or eboot conversion/staging occurred. Historical original-export evidence remains separately labelled in HISTORICAL-README-97cd8e121.md.

Retained Mesa/RADV, ACO, SDK and libc++ libraries are digest-verified inputs, not newly rebuilt here. Caller CMake module installations, arbitrary host DLLs/shell/Node and extraction tools are outside a pristine host closure. No claim of dependency source-to-binary equivalence, full-game stability, all-scene widescreen or successful console save loading follows from compilation/static checks.

No retail data, private texture pack, save, credential, proprietary module, SDK/tool/game binary or process-memory capture belongs in this export. Original game LICENSE.md currently selects no blanket open-source license; this source/patch review does not grant rights over original authors' code or clear distribution of GPL-linked game binaries. Retain all original/file-level notices, including PsyZ, SDL, GPL platform components and AMD MIT. Manager owns actual Git/PR actions and licensing review. Upstream PR403 and prior metadata-restoration denial are not acted on by this recipe.

