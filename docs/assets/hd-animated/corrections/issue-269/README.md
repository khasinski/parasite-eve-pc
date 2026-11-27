# M163 restaurant and signs re-registration (#269, #268)

Chinatown M163 (the street with the lamp post and the 飯店 restaurant) showed rows of black
texels along the hanging sign and the restaurant's awning (#269), and the restaurant looked
like it stood in the street, so Aya seemed to walk into it (#268). Both came from the HD
material, not the game: the generated anchor `4f85e561` re-laid out the restaurant block,
its awning and the hanging signs. Its registration was flagged for review (scale 0.92,
translation 14/−21 native px) and one global affine could only match 25% of the features,
so the restaurant material landed on the wrong native tiles. Native coverage, depth and
walkable area were always the original's.

Checks that ruled out other causes (headless GPU, pack 0.5.1): the dots stay on the same
native texels at 2x, 4x and 8x (not sampling); a diagnostic shader shows no palette
fallback (they are drawn by the normal HD path); filling black or miscoloured texels from the
native picture does not remove them and damages the baked night lighting.

## Fix (pack 0.5.2)

1. New anchor `-v2b` with the built-in imagegen tool (Codex), using the recorded prompt plus a
   note pinning the restaurant block and both signs to their source positions. Two candidates;
   both register at 85–86% inliers (`M163-registration.json`, `register_one.py`).
2. `assemble_sources.py --room M163` and `../../../hd-poc/pack.py OpenAI-source-guided --room M163
   --no-family-lock --preserve-source-black --entry-overrides corrections/entry-overrides`
   in an isolated root with only this anchor changed. Rebuilding with the old anchor reproduces
   0.5.1's eight M163 atlases (references identical, HD within WebP noise), so the route is the
   release's. The Chinatown road keeps its source herringbone (`source-material-regions.json`).
3. `build_pack.py --baseline <0.5.1> --atlases <pack.py output> --output <0.5.2>`: only the two
   static atlases whose material changed are replaced (`e8b5cc1a6bb39a1e`, `c8d06ff0bca6eb49`),
   encoded as `issue-56/encode_webp.py` does. Native references, the six controller-phase
   atlases, every other room and every extra stay byte identical (`M163-pack-audit.json`).

Runtime proof (headless GPU, 4x, with and without widescreen/interpolation): the restaurant,
awning and signs coincide with the original background along the whole street walk, and the
black texels are gone. The published archive was downloaded, verified and installed by the
game's own `--download-backgrounds`.

Published `upscale-v0.5.2.zip` (sha256 `0e8a7e5b…8a4c`, 341,923,063 bytes) for game 0.13+;
0.5.1 and older archives remain immutable.
