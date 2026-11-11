# HD backgrounds

Optional replacement backgrounds use the original room renderer. Enable a locally generated pack with `PE_HD_BACKGROUNDS=/path/to/pack`.
Missing assets retain original rendering. The scaled atlas path requires SDL_GPU.

The initial Day 1 study covers M001–M037: 33 maps with 2D layers, 43 views and 62 original
animation controllers. Original tile coverage, transparency, depth, timing,
scripts, native VRAM and audio remain authoritative. Atlas storage is recycled
at room transitions.

Offline study scripts are retained under `docs/assets/hd-poc/` and
`docs/assets/hd-day1/`. They require local extracted inputs, generated references,
model weights and recorded replay fixtures; some paths are still workstation
specific. Images, packs, disc data and private test captures are deliberately
excluded from Git. The private checkpoint includes these files and their
provenance; the scripts alone are not a downloadable HD pack.

The current M037 approach keeps one cabin/mirror frame and reconstructs street
and mirror-reflection motion from all 19 original phases. Yellow road markings
are source driven. Static pillar layers use their own original material rather
than phase-zero street colors. This prevents background lights from becoming
part of the car silhouette. M027 uses source-guided movement of one HD water
material. These remain visual research candidates.

Local validation reached Day 2 through 59 checkpoints with identical original
state, native VRAM and continuous PCM. That route visits 25 of the 33 maps;
optional maps such as M037 require separate packed-composition and offscreen
preview checks. A visual review is still necessary: numeric parity does not
prove artistic fidelity.

## Lossless PNG distribution packs

The SDL_GPU atlas loader accepts PNG and BMP. The game prefers `<key>.png`, then
tries `<key>.bmp`, retaining native rendering if neither atlas loads. PNG changes
storage only: decoded pixels, native reference colors, coverage and shader input
remain the same. GPU texture memory is unchanged.

After assembling and reviewing the BMP working pack, create a separate distribution
pack with Python and Pillow:

```
python docs/assets/hd-poc/compress_pack.py SOURCE DESTINATION --version 0.1
```

The converter checks source hashes and compares every decoded PNG pixel with its
source. It records file hashes, decoded-pixel hashes and compression totals in the
manifest. An existing destination is rejected; a failed conversion removes its
staging directory. Original BMP working packs and reviewed correction overrides
remain intact. The current combined 4069-atlas pack shrinks from 8,962,308,558 to
1,375,341,132 image bytes (about 85% smaller), with identical decoded pixels.

Offline converter regression: `python tests/hd_pack_png.py` (requires Pillow).
The PsyZ GPU regression compares PNG/BMP rendered pixels and native VRAM; the
original-game HD integration test can use either pack format through
`PE_TEST_HD_BACKGROUNDS`.


## Title and widescreen artwork

The PNG pack can contain `title.png`, a wide title backdrop. The title module
keeps the original native RGB24 menu, including its animated options and input.
The presenter substitutes the HD background, crops its center in 4:3 and shows
its full width in 16:9. Missing artwork retains the native title.

Optional `<geometry-key>-wide.bin` sidecars describe additional side tiles by
original view group. Their PNG materials use the existing atlas cache. The room
renderer anchors them to the original scrolling coordinates, behind the original
scene, and inherits its original fade/tint packet colors. Their explicit GPU marker discards them from native VRAM and from 4:3
presentation. They currently require SDL_GPU at an internal resolution above 1x,
like the existing HD mesh path. At 1x they are not displayed.

A single backdrop sits behind every actor, so side continuations of a room's
foreground layers (M001's ambulance and police car, M002's canopy) were drawn
under the characters walking past them. `PEW2` sidecars fix this with
depth-sorted layers. Each record gains a `u32` kind after its bounds: kind 0 is
the `PEW1` backdrop; kind 1 is followed, after its positions, by one `u16`
native depth per cell (the foreground entry's depth plus its tile offset,
1..4095). The renderer links such a cell exactly where `Render_DrawSpriteEntry`
links a native tile of that depth, and skips it when the native tile would be
skipped, so it covers and is covered by actors like the native part of the same
object. A view may have several kind-1 layers next to its backdrop; their art
must be transparent outside the object (alpha below 0.5 draws nothing, in both
the GPU and CPU paths). Depths come from the foreground entry's tiles nearest
the seam, extrapolated across the side. `PEW1` sidecars load unchanged.

Artwork coverage is **not complete**. The first installed static candidates are
M001 and M002. The working catalog has 490 background views, with 488 still
pending. Wider native rooms continue using their original stored picture; other
unprepared rooms retain the existing widescreen behavior. Animated edge material
must follow the original phases rather than freeze an independently generated
still. The static export tool refuses views containing animation controllers or script-switched/scrolling layers.

`docs/assets/hd-widescreen/prepare.py` registers a reviewed outpainting against
its current 4x center, rejects poor feature alignment and uncovered margins, and
packages only the added sides. Original atlas pixels are never rewritten.
Sources, prompts, registration evidence and private artwork are kept with the
asset checkpoint. The title extraction and prompt are in `docs/assets/hd-title/`.
The converter preserves checksum-verified `extras` (title and widescreen files)
when producing a separate distribution pack.

Regression coverage includes title/UI/native-VRAM separation, widescreen-only
GPU materials, and `original-game-hd-widescreen`: a real fresh-game replay checks
exact 4:3 output, state/VRAM/PCM parity and persistent side artwork during a
stationary-camera dialogue. The latter requires `PE_TEST_DISC` and
`PE_TEST_HD_BACKGROUNDS` pointing to the reviewed M002 pack, otherwise it skips.


## High-quality WebP distribution

`compress_webp.py SOURCE DESTINATION --quality 95` creates a separate pack.
Each `<key>.webp` stores only the 4x HD material. Its `<key>.ref.png` retains the
native reference plane losslessly, so palette fallback, STP and native VRAM
remain unchanged. Legacy materials use native coverage; reviewed RGBA materials
can refine opaque HD presentation coverage as described below. The title uses lossless WebP. Original PNG packs remain
intact. The runtime prefers WebP, then PNG/BMP; a missing or invalid reference
plane rejects that WebP and allows the existing fallback path.

PsyZ links the pinned libwebp 1.6.0 decoder statically through CMake. No external
codec installation is needed to play. GPU memory usage is unchanged after decode.
The current 4071-atlas-plus-title distribution is 325,619,280 bytes including its
compact manifest, compared with 1,434,337,656 bytes for the PNG pack. Full authoring
metadata stays in the source pack; distribution records keep hashes and asset IDs.

Offline regression: `python tests/hd_pack_webp.py` (Pillow and cwebp required).
The GPU regression exercises lossy WebP with exact native references, changed
palette fallback, native VRAM equality, and missing-reference rejection. Real
headless HD and widescreen regressions also pass with the WebP-only pack.


## Download service

`https://pe.polanka.ovh/api/mods/upscale/latest` advertises the immutable ZIP,
mod version, byte size, format and SHA-256. Escape → Mods downloads on a worker
thread, displays progress, supports cancellation and verifies the archive, ZIP
CRC and every material/reference file. Extraction accepts only flat known asset
names, rejects duplicates, symlinks and oversized files, and uses UTF-8 SDL I/O.
The active pointer changes atomically only after complete verification. Previous
version directories are retained. A bad current copy can be verified and repaired;
an unchanged healthy version does not download again. Installation and on/off immediately refresh presentation metadata for the current
room and title, preserving native packets, animated descriptors and game state.

On/off is saved under `[mods] hd_backgrounds` in the user's TOML. The active
version/directory is in `mods/upscale/active.json` beside those preferences.
`PE_HD_BACKGROUNDS` remains a development override with integrity checks bypassed.
`--download-backgrounds` and `--verify-backgrounds` use the same installer without
a window. `PE_CONFIG_HOME` selects an isolated profile for headless checks;
`PE_HD_SERVER=http://127.0.0.1:PORT` is a localhost-only test override.

The game advertises `X-PE-HD-Features: underlay-v1,static-matte-v1,selective-matte-v1` when requesting
the catalog and archive. The server keeps versioned catalogs so older games
without these capabilities continue receiving a compatible pack. The client
rejects unknown or malformed `required_features` before downloading the ZIP,
retaining its active pack. Feature compatibility is independent of development
build version strings. ZIP delivery remains a static nginx transfer.

Downloads report `User-Agent: ParasiteEve/VERSION`, `X-PE-Version`, `X-PE-Build`,
`X-PE-Platform` and an ephemeral per-download ID. The server counts download starts before nginx sends the ZIP; update checks and HEAD requests do not count. It stores
coarse client metadata and keyed daily visitor hashes, not raw IP addresses.
Server source/deployment lives separately in `~/Projects/pe-mod-server`.

Headless GPU control: `backgrounds 0|1 /absolute/pack/path` applies the same refresh
and advances eight blanks. `tests/hd_hot_apply.py` compares late activation and
off/on against a startup-loaded pack on the title and a live field scene, including
exact native state, VRAM and PCM checks.


### Reviewed foreground coverage (issue #56)

Legacy packs also receive inward edge feathering for matched opaque HD room
sprites above 1x. This filters native coverage inside its existing silhouette;
it never adds foreground colour outside native coverage. Tile bounds are clamped
to prevent neighbouring VRAM texels from contaminating the matte. SPIR-V, Metal
and DXIL are generated from the same shader. This fallback reduces hard cutout
edges in the current pack; coordinated artwork and authored mattes address the
remaining material seams in the reviewed replacement pack.

Regular RGB packs keep the original native cutout rules. An RGBA material with
non-opaque alpha explicitly opts into a refined coverage matte for opaque HD
presentation at internal resolutions above 1x. Its native reference plane still
owns palette matching, PS1 STP, native VRAM/readback and gameplay. Semitransparent
primitives keep native coverage and ABR blending. Existing packs remain compatible.
The default matte can only refine pixels inside live native coverage: RGB palette
matching cannot distinguish transparent zero from opaque black, so an HD alpha
value must not resurrect a native-transparent pixel or a packet's rectangle.

`ctest -R '^original-gpu-hd-(coverage|reference)$' runs hidden GPU tests
with synthetic atlases. They check native-transparent holes, palette fallback,
authored partial alpha, PNG/WebP equivalence, native VRAM and semitransparent
draws. Windows CI runs both tests with D3D12.

Experimental `<geometry-key>-under.bin` sidecars reuse the PEW1 tile layout
for optional reconstructed backdrop artwork. Unlike `-wide.bin`, these tiles
also draw in 4:3. They are linked behind the original room ordering table and
use the room's original tint/fade. Their atlas command uses cell bit 22 instead
of the widescreen extension's bit 23. They render only with HD enabled above
1x, never into native VRAM; the software path ignores them. The installer accepts
these files as checksummed manifest extras. Existing packs without them retain
their current appearance. This is a mechanism for a reviewed coherent underlay,
not an automatic reconstruction of the missing floor. M097/M239 artwork and
native scene review remain unfinished; no new production pack has been published.

The extension GPU regression covers both aspects, 1x/2x, HD off/on, absent
atlases and original draws in front. The real-disc widescreen test additionally
loads a synthetic diagnostic underlay through the actual room-sidecar path and
compares original game state, VRAM and PCM at both checkpoints.

A reviewed **static** atlas with non-opaque alpha may opt into an HD silhouette
using a same-key `-matte.bin` sidecar containing exactly four bytes, `PEM1`.
This mode can extend beyond native-transparent pixels in the scaled opaque
presentation, so the atlas must have a coherent background or an appropriate
underlay beneath its edges. The pack author must verify that the layer has no
animation or palette-controlled visibility. Never mark animated or script-hidden
layers this way. Missing, invalid or overlong metadata keeps strict native
coverage; existing unmarked packs keep their current appearance.

For a local correction, `PEM2` followed by a cell-selection bitmap opts in only
the reviewed static cells. The bitmap has exactly `ceil(columns * rows / 8)`
bytes; bit 0 selects material cell 0, with row-order numbering. Reference-plane
cells are not counted. Empty, truncated, extended or out-of-range masks fall
back as a whole. Unselected cells keep their own alpha policy, including legacy
feathering for fully opaque material. Packs using this format must require
`selective-matte-v1`; older games then continue receiving a compatible catalog.
Both formats require review of the backdrop in every relevant animation pose.
Fractional-alpha pixels retain their authored straight RGB; colour extrusion
for filtering touches only fully transparent material pixels.

Static mattes do not affect native VRAM, 1x output or PS1 semitransparent draws.
Palette mismatch still falls back to native color. PNG and WebP use the same
metadata contract. The installer treats matte markers as checksummed extras;
corrupt markers invalidate verification and cannot replace a working pack.
GPU tests cover valid/invalid markers, PNG/WebP parity, native VRAM, palette
fallback, semitransparency and the legacy transparent-pixel guard.

`hd-poc/pack.py --coverage-aa` traces each complete native entry before cutting
16px tiles, simplifies long stair-step contours within half a native pixel,
and rasterizes an antialiased 4x mask. Small holes remain unsimplified. Newly
covered edge texels inherit foreground colour; matching gutters avoid tile seams.
The packer writes RGBA PNG; WebP compression preserves the alpha losslessly.
This is conservative contour reconstruction, not a hand-authored HD segmentation.

Material discontinuities need coherent artwork in every intersecting depth layer.
Do not remove a source-authored seam merely because an upscale emphasizes it.
For M103 the intact lobby is M046 (M041 is a different office); its wide and
close views must share one source-faithful bridge painting and NYPD plaque.
The fallen board in M099 must reuse the intact M042 board. Preserve foreground
objects crossing a corrected plane, including the globe lamp over the painting.

The offline compositor accepts WebP and RGBA coverage and `--capture-json` to
reconstruct exactly the active entries recorded in a screenshot report. These
previews omit actors, live scripts, mirrors and camera movement; they complement
hidden-GPU tests rather than proving those gameplay paths.

### Encoding corrected materials

The issue-56 `encode_webp.py` accepts `--source`, `--out` and `--lossless`.
It encodes PNG materials in a mixed candidate while preserving native references
and other assets, including title images. Lossless mode verifies every decoded
RGBA byte; both modes require exact alpha and reference hashes. The output must
be a fresh directory and an encoding audit is written beside it. For example:

```sh
python encode_webp.py --source CANDIDATE --out NEW_WEBP_DIRECTORY --lossless
```

Pass `--version 0.2` when staging a separately versioned candidate. Use a dotted
numeric version (up to 20 characters), matching the game's downloader. Without
this option the source version is retained. Encoding recomputes payload and
total directory bytes, including the final manifest; it never edits the source
manifest or overwrites an existing output directory.

### Reproducing the M097/M239 edge candidate

`docs/assets/hd-animated/corrections/issue-56/build_underlays.py` builds a fresh
review directory from a baseline pack and the locally archived artwork:

```sh
python build_underlays.py --assets PATH --baseline PACK --out NEW_DIRECTORY
```

It requires OpenCV, NumPy and Pillow. It rejects existing output paths and
foreground entries with animation controls. The baseline is never overwritten;
changed files are detached before writing. Source/reference planes and existing
covered foreground RGB stay intact. Long-edge masks, narrow reconstructed floor
strips and explicit static-matte metadata form the candidate.

`underlay-generation.json` records source/output hashes and generation prompts;
`underlay-review.json` records native captures, rejected methods and remaining
review. The generated masters are archived separately from Git. This recipe
requires the renderer support in PRs #79 and #80 and does not publish a pack.

Development atlas overrides must account for format priority: the runtime tries
WebP, then PNG, then BMP for each key. A corrected PNG does not override a
same-key WebP. The issue-56 candidate builder removes only the old candidate
link when replacing an atlas, preserving the baseline. It accepts
`--assets-root`, `--baseline` and `--out` for separate checkouts, refuses existing
outputs and writes its audit beside the new output directory.
