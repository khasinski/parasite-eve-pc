# HD interface artwork (preview)

The optional artwork pack can now replace the original menu graphics, icons,
portrait, menu/dialogue glyphs, digits and simple title-menu RGB24 image nodes.
Procedural panels already render at the selected internal resolution. The Escape
settings overlay has its own high-resolution font and needs no texture replacement.

The original C modules still build the same draw packets. Positions, advances,
line breaks, clipping, colors, fades, controller logic and game timing stay with
the original game. The SDK receives presentation metadata beside those packets.
It fingerprints each source rectangle, dimensions and decoded native palette
colors (including the PS1 mask bit). It does not hash unrelated VRAM on the same
page. Missing or invalid replacements retain native rendering.

Indexed UI artwork uses `ui-<16-digit-key>.webp` (PNG is also supported), at
exactly four times its native dimensions. Materials share a bounded 2048×2048
RGBA GPU atlas with replicated gutters. An installed older background pack has
no matching files and continues to render its original UI. Artwork applies above
1x internal resolution, on the SDL GPU backend. The same Mods switch controls
the whole pack; hot replacement resets UI materials too.

The title screen is different: its original CPU RGB24 compositor is retained.
Simple image nodes provide the original position and intensity to the HD title
presenter. Matching `ui-rgb-<key>.webp` files use the original max-channel blend.
The short rotating-image transitions retain their original compositor output.
No replacement enters native VRAM, readback, model textures or texture feedback.

## Local artwork pipeline

`tools/hd_interface.py extract` takes the player's North American Disc 1 BIN and
a raw 1024×512 RGB555 VRAM capture. It enumerates resident menu descriptors,
menu glyph metrics, base dialogue font cells, small digits, HUD digits/symbols and title image
resources. The title image header measures width in 16-bit words: its RGB pixel
width is **two thirds** of that value. The exporter preserves this distinction.

```
python tools/hd_interface.py extract --disc-bin DISC1.bin --vram vram.bin --output SOURCES
python tools/hd_interface.py build SOURCES UI_OUTPUT
python tools/hd_interface.py combine BACKGROUND_PACK UI_OUTPUT COMBINED_PACK --version 0.2-preview
```

Dependencies: Pillow, NumPy, OpenCV (colored panel lettering), potrace and librsvg (`rsvg-convert`). Font outlines
are traced from the actual original glyph masks and shadow layers with a 2x
intermediate mask, retaining serifs and their original cells. Gold panel lettering is traced from its original masks over restored panel artwork; other graphics use edge-directed reconstruction. Output is verified lossless WebP. The build
refuses to overwrite an existing destination. It does not distribute font files.

`PSYZ_DUMP_INTERFACE=/existing/directory` exports additional indexed source
regions used by an actual replay, using the same keys as the loader. This helps
find unvisited variants. An observed missing glyph/material remains native.
Do not call coverage exhaustive until the relevant menus and save variants have
been exercised; enumerating a descriptor is not a visual validation.

For a combined pack, place the `ui-*.webp` files at its root and add each to the
normal manifest's `extras` with its SHA-256. Flat filenames preserve the existing
installer's path restrictions and checksum/activation safeguards. Background
atlases and their reference planes stay intact. Keep the prior working pack.

The local preview (618 materials, less than 1 MiB of UI artwork), sources, generated portrait and provenance are retained under
`docs/assets/hd-interface/` (ignored game assets). The portrait uses built-in
ImageGen; its source and exact prompt belong with the asset checkpoint. This
preview has not replaced the pack served to players.

## Tests

- `hd-interface-pack-format`: stable rectangle keys, palette/mask changes,
  unrelated texels, lossless WebP and overwrite protection (skips without Pillow/NumPy).
- `original-gpu-interface-pack`: actual rendered replacement pixels, unchanged
  native VRAM, unmarked-packet fallback, unrelated page updates, palette swaps,
  RGB24 title nodes and node removal. Includes PNG decoding for synthetic fixtures.
- Existing GPU, interpolation, descriptor-batch and title regressions remain enabled.

Local real-game comparisons cover dialogue, title UI and inventory loaded from a real save with exact native
state/VRAM/PCM equality. Pixel comparisons and a visual review are both needed
before distributing new artwork.

## Character texture experiment

The preview also supports explicitly marked original Gouraud model triangles
and quads. `src/main/render/object.c` groups each model's original UVs by page
and CLUT and registers the complete shared material rectangle for both packet
buffers. The renderer uses the same content key and 4x `ui-<key>.webp` material
format. All faces sharing a region sample the same restored image, including
when the original game reuses a previously built packet on later frames.
There are at most 32 regions per model; unusual models retain native materials.
Missing images, palette changes, unsupported texture windows and the OpenGL
backend retain native rendering. Geometry, UVs, lighting, animation, native
VRAM, readback and gameplay are unchanged.

`PSYZ_DUMP_MODELS=/existing/directory` captures the actual decoded regions and
logs their page, CLUT and UV rectangle. Capture both the images and those logs;
UV coordinates must not be inferred from a rendered screenshot.

The first character experiment covers **18 materials of Aya's opening black
dress model only**. It is not a complete character pack. An ImageGen restoration
of the original sparse 256x256 UV atlas is normalized to 1024x1024, cropped at
the original island coordinates, and given the exact native alpha mask at 4x.
The original and generated atlas, prompt, region list and lossless WebP outputs
are retained under ignored `docs/assets/hd-interface/model-sources/` and
`aya-v1/`. Their combined playable preview is `preview-models/`; the older UI
preview and published pack remain available independently.

```
python tools/hd_interface.py models MODEL_SOURCES GENERATED_ATLAS MODEL_OUTPUT
python tools/hd_interface.py combine UI_PREVIEW MODEL_OUTPUT COMBINED_PACK --version 0.2-preview
PE_HD_BACKGROUNDS=/absolute/path/to/COMBINED_PACK ./build/parasite-eve-pc --disc DISC1.cue
```

The atlas is an art experiment: high resolution cannot remove the original
model's angular silhouette, and reconstructed details need inspection on the
animated model. Do not treat a good-looking UV sheet as sufficient validation.

`PE_TEST_MODEL_PACK` adds a headless loaded-save field comparison to
`tests/hd_interface_render.py` (requires `PE_TEST_UI_CARD`). Its baseline is
`PE_TEST_UI_PACK`, so any rendered difference comes from character textures.
`PE_TEST_MODEL_GRAPHICS=1` repeats that comparison with geometry correction,
antialiasing and model filtering. Both paths require identical native state,
VRAM and PCM. Synthetic GPU tests additionally cover Gouraud face replacement,
queued metadata, native readback and unmarked-packet fallback.

The initial local validation passed all 75 SDK GPU tests, the Windows cross
build, installer/settings checks, the three UI replay comparisons, and the Aya
field replay both with and without geometry correction/AA/filtering. The Aya
rendered difference is confined to her screen bounds; native state, VRAM and
PCM remain identical in each pair. A longer animated visual review and more
characters/outfits are still needed before distribution.
