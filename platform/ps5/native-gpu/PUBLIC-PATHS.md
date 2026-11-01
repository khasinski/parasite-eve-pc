# Public logical path mapping

The dependency/shader metadata in this review copy is normalized publication metadata, not the original machine-local log file. Original local proof and original metadata remain untouched. Original content/tool/archive SHA256 values are preserved. The normalized JSON files themselves have new hashes recorded in the manager's review-copy inventory; do not identify them by the historical JSON file hashes.

| Logical reference | Caller-defined meaning |
| --- | --- |
| `<PUBLIC_PREFIX>/...` | The retained, digest-verified public dependency prefix passed as the build script's second positional argument. Existing `deps/...` and `upstream/...` metadata paths are relative to this prefix. |
| `<EXPORT_ROOT>/...` | The fresh source/build output passed to source preparation and the build script. |
| `<WEBP_SOURCE>` | `webpSource` in the explicit tools configuration; its source files are separately hash-verified. |
| `<REFERENCE_ROOT>/native-vulkan/...` | Historical independently authored triangle/quad demo shader evidence. Those reference-demo inputs are not built by this game export, and the export does not claim to reproduce that separate demo. |
| `@ROOT@`, `@BASE@` in support templates | Build-time tokens replaced only with the caller's export and public-prefix arguments; no developer's home/work directory is required. |

Historical shader command arrays keep the same flags, argument order and source/output association. Only absolute path roots and separators changed to logical references. They are evidence records, not directly executable commands until the caller resolves the documented roots. Actual build commands are generated from the caller's explicit paths and recorded under the new export.

The historical game documentation examples inside the source patch use `$env:CMAKE_EXE` and `$env:NINJA_EXE` instead of one developer's tool installation paths. Their documentation-only output SHA is adjusted explicitly in the source manifest; its prior reviewed hash remains in `reviewedHistoricalAfter`. No compiled source hunk or shader byte is changed by this documentation normalization.

The public build does not require a particular developer account, helper, credential or account API. An optional caller-supplied `capacityGuard` supports local policy; project automation continues to supply its actual main guard. No account probing, model inference, download or runtime execution is part of this recipe.

Input-integrity scope is defined by the listed file/tree manifests, not every file on a caller's machine. Caller CMake/Ninja identity is recorded (optionally caller-pinned); surrounding host installations remain outside that scope. INPUT-PREFLIGHT.json contains the caller's actual local paths and should be kept with local evidence rather than blindly published.
