# Required local PointSprite data boundary

The public source contains independently authored typed-owner/P118 logic. The32 newly restored instruction words from the supplied original executable are intentionally omitted from both the native script initializer and its exact checkpoint descriptor. No original executable, fixed executable offset/address, original fingerprint, extraction proof, raw216-byte table or generated initializer is distributed.

This package cannot build a game without the required local input. There is no22-word fallback, invented tail, skipped checkpoint owner or fake success. The header emits a clear compile error if input is absent; build-native.cjs rejects missing/invalid configuration before toolchain work.

Prepare the pinned source into a fresh tree outside this package/repository. Supply your own authorized, version-correct54-word signed little-endian table as an exact216-byte local file. In a local tools configuration set pointSpriteOwnerData.file and pointSpriteOwnerData.sha256 to that file and its independently pinned hash. Do not commit that file or local config. Run the normal build recipe, or call the generator explicitly with a local JSON object containing file and sha256:

node generate-owner-point-script.cjs <prepared-root> <local-input-config.json>

The generator checks the previously published22-word prefix and the independently authored bounded opcode/handler and typed stopped-state contract for controls0/1/2. It does not establish authenticity, ownership, copyright permission or a redistribution license for supplied data. It generates only game-src/include/pe1/point_sprite_owner.inc and a local POINTSPRITE-OWNER-INPUT.json, outside the distributable package. Both generated names are ignored. Keep them and their fingerprints local. Both C consumers use the same initializer; no stale hardcoded tail remains. Generated input must be included in the actual local source cohort before compiling.

The actual local cohort and input preflight may identify supplied data or local paths. Keep STATE-SOURCE-IDENTITY.json, INPUT-PREFLIGHT.json and the generated state_build_identity.h local too; these generated names are ignored. Publish the distributed source-only preparation records, not a local owner-data build tree.

Public source reconstruction verifies the distributed logic and missing-input boundary, not the omitted game data or equality with the private5805 cohort. Public complete build, gameplay and license clearance remain unclaimed. Manager review is required before publication; this task performs no Git operation.
