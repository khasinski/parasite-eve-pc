# Wardrobe gown animations

Melissa and the opera actress have a segmented floor-length skirt. Playing
Aya's full leg motion on those segments tears the skirt during running,
kneeling and other actions.

The Wardrobe skin loader repairs the two built-in skin folders (`melissa` and
`opera_actress`) once, in its cached, private copies of `model.bin`, `day1.bin`
through `day8.bin`, and optional `rooms.bin`. Installed assets and the original
story/cutscene Melissa model are never modified. Other skins, imported packs
and Aya's original outfits do not enter this repair path.

The six leg bones (21–23 and 25–27 in the 29-part Aya layout) follow 20% of
their animation's rotation from the skin's own idle pose (row 0x15, frame 0).
Quaternion interpolation follows the shortest rotation, including poses whose
Euler representations cross a wrap or gimbal boundary. The upper body, weapon
hand, pelvis, root translation, frame counts and hit values retain their
original data. Gown wearers glide with subdued leg movement rather than taking
Aya's full stride.

Repaired rows are appended to the private containers and their record offsets
are updated. This allows a formerly constant Euler channel to become sampled
when the quaternion blend couples axes. Room fingerprints remain the keys of
Aya's original rows. Invalid or unsupported gown rows reject the skin before
its pointers are installed in the game.

## Skin builder metadata

A builder that already applies this gown correction should append ASCII
`GWN1` at bytes 8–11 of `meta.bin`: `PEMT`, the existing little-endian 32-bit
gun part, then `GWN1` (12 bytes total). The Wardrobe skips the repair for that
skin. Legacy 8-byte metadata continues to work, including its gun part.
Previously corrected assets without this marker should be marked by their
builder to prevent additional damping. The game never writes a marker or
changes files on disk.

## Validation

Configure with `-DPE_MODS=ON -DBUILD_TESTING=ON`, build `test-skin-gown`, then
run `ctest --test-dir <build> -R wardrobe-skin-gown --output-on-failure`.
The synthetic tests cover both angle encodings, mixed constant/sampled
channels, wrap/gimbal rotations, preservation of non-leg channels, aliased
records, malformed data, folder scope and the prepared metadata marker.

For an optional offline check on locally extracted assets:
`test-skin-gown <legacy-skin-directory> <existing-output-directory>`.
This writes private repaired animation containers for rendering comparisons.
Do not commit disc-derived models, textures or animations.

In game, restart after changing skin files, wear Melissa in the Wardrobe,
then check running, door opening, kneeling/falling and battle shooting. Repeat
with the opera actress, switch back to Aya, and confirm story Melissa still
uses her original animations.
