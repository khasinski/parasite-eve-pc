# Room lighting workbench

A local browser editor using the player's own North American disc images and
Aya captured from the original C game. Choose any of the 414 field rooms and
509 camera views, place the posed model on the original collision mesh, and
position lights using 3D handles or XYZ fields. Sloped floors use the original
plane equations. The same room-space light positions apply across its cameras.

The main view uses the room's original camera over the entire stored background.
The orbit view exposes the 3D floor, Aya and lights. Point and spot lights use
distance attenuation; directional and ambient lights are also available. The
original mesh, vertex normals, UVs, palettes and textures remain intact. The
preview uses per-pixel standard-material lighting through pinned Three.js.

This is an editor experiment, **not yet an in-game lighting option**. Profiles
are saved locally for later renderer integration. Neither game saves nor the
normal renderer are modified by editing. There are no shadows on the 2D scene,
no reconstructed furniture occlusion and no background relighting. Animated
backgrounds show their authored default static layers. The captured Aya is a
single pose/outfit; use another suitable save to capture another original pose.

## Desktop application

The desktop version is **Parasite Eve Lighting Lab**. It has an English
interface, native file pickers, a File menu, and native JSON/PNG save dialogs.
The application bundles Electron and a private Python asset reader: running a
packaged build requires neither Python, Node.js, a browser nor a terminal.
Game images and model captures are not included.

On first launch, choose your North American **Disc 1 CUE/BIN/ISO** and a profile
folder. Aya is imported automatically, using the bundled original C engine
headlessly in a temporary directory. No save or developer export is required.
An existing Aya capture remains an optional advanced input. The application
remembers those choices and keeps a private copy of the small capture. Saved
profiles and window settings live outside the app bundle; on macOS the default
is `~/Library/Application Support/Parasite Eve Lighting Lab/`. Browser drafts
use a stable application origin and survive app restarts. Closing the window
also stops its private asset-reader process.

Build on the target platform (macOS builds produce a local ad-hoc-signed `.app`):

```sh
build/lighting-editor/venv/bin/pip install -r tools/lighting-editor/requirements-desktop.txt
npm ci --prefix tools/lighting-editor
# If npm suppressed Electron's installation script:
node tools/lighting-editor/node_modules/electron/install.js
build/lighting-editor/venv/bin/python tools/lighting-editor/build_desktop.py
```

The output is under `build/lighting-editor/desktop/`. `--reuse-backend` skips
rebuilding the private runtime while iterating on the desktop interface.
Build the original asset importer first (`cmake --build build --target parasite-eve-pc`).
Only application code, the original engine importer, runtime dependencies and licenses are packaged.
For source development, set `PE_LIGHTING_PYTHON` to the Python environment above
and run `npm start --prefix tools/lighting-editor`.

The packaged-app test runs with hidden windows and scratch data directories:

```sh
PE_LIGHTING_APP='/path/to/Parasite Eve Lighting Lab.app/Contents/MacOS/Parasite Eve Lighting Lab' \
PE_TEST_DISC='/path/to/Disc 1.cue' \
PE_LIGHTING_MODEL="$PWD/build/lighting-editor/model.json" \
NODE_PATH="$PWD/tools/lighting-editor/node_modules" \
node tools/lighting-editor/test_desktop.cjs
```

It covers English labels, native picker cancellation and recovery, private
capture import, profile and draft persistence across restarts, native exports,
renderer isolation and helper-process cleanup. These process-lifecycle checks
currently target macOS/Linux. The browser edition remains available below.

## Browser setup

Run from the repository root. Use Python 3.10+ with the dependencies below, and
Node.js/npm for the pinned browser library. Once installed, the editor makes no
requests to external services.

```sh
python3 -m venv build/lighting-editor/venv
build/lighting-editor/venv/bin/pip install -r tools/lighting-editor/requirements.txt
npm ci --prefix tools/lighting-editor
cmake --build build --target parasite-eve-pc -j 8
```

Capture Aya using a **copy of a single PsyZ save file** with field gameplay.
The first Carnegie Hall save works well. The helper runs the original Continue
path headlessly, in an isolated temporary card directory, then exports the
original posed model and native texture memory. `--verify` additionally compares
an otherwise identical replay: native game state, VRAM and PCM must match.

```sh
build/lighting-editor/venv/bin/python tools/lighting-editor/capture_model.py \
  --game build/parasite-eve-pc --disc '/path/to/Disc 1.cue' \
  --card '/path/to/bu00/BASLUS-006620000000A' \
  --output build/lighting-editor/model.json --verify

build/lighting-editor/venv/bin/python tools/lighting-editor/serve.py \
  --disc '/path/to/Disc 1.bin' --model build/lighting-editor/model.json
```

Open **http://127.0.0.1:8766**. The server binds only to loopback. It reads either
USA disc; room geometry is shared between the pair. BIN/ISO data are read
locally, not uploaded. Room data, actor data and decoded textures are not shipped
with this source tree. Put output captures in ignored `build/` storage.

## Controls and profiles

- Choose a room and camera. The main view displays the entire stored picture;
  it does not crop a scrolling room to a 320x224 gameplay viewport.
- Click a floor triangle to position Aya. Drag X/Z handles or use XYZ fields;
  Y follows the actual floor surface. Invalid placements retain the last valid
  position. On stacked floors, a vertical snap prefers the closest height.
- Select a light in the list or by its marker, then drag its XYZ handles in
  either viewport. Add point, spot or directional lights; edit color, intensity,
  range and cone angle. “Aim at Aya” updates a spot/directional target.
- Orbit view: left-drag rotates, right-drag pans, wheel zooms.
- Toggle helpers and the mesh to review the composite cleanly. Screenshot
  export includes both the original background and lit Aya.
- Save writes `build/lighting-editor/profiles/Mnnn.json` atomically. JSON export
  and import are available. Browser drafts survive room changes/reloads until
  saved. Resetting the browser's local site storage removes only those drafts.

Stored XYZ coordinates use the **original game axes/units**: X, downward Y and
forward Z. The browser converts to Y-up internally, at 100 game units per scene
unit. Yaw is degrees relative to the captured pose. Profiles include a format
version and capture identity. They currently support up to 16 lights per room.

## Tests

```sh
PE_TEST_BIN='/path/to/Disc 1.bin' \
  build/lighting-editor/venv/bin/python tools/lighting-editor/test_assets.py
```

The optional real-disc survey checks all room cameras/floors and samples decoded
backgrounds. Synthetic tests cover flat and sloped heights, palette coordinates,
transparent black vs PS1 mask-bit black, and preserving a valid profile when an
incomplete save is rejected. CTest runs the synthetic tests when Python has the
required packages, otherwise skips them.

Run the browser tests against a **separate server with scratch profiles**:

```sh
build/lighting-editor/venv/bin/python tools/lighting-editor/serve.py \
  --disc '/path/to/Disc 1.bin' --model build/lighting-editor/model.json \
  --profiles build/lighting-editor/test-profiles --port 8767
# In another terminal (install Playwright's browser once if necessary):
NODE_PATH="$PWD/tools/lighting-editor/node_modules" \
  node tools/lighting-editor/test_editor.cjs
```

Browser tests stay headless and exercise live lighting, actual gizmo dragging,
adding/removing lights, picking original floor triangles, rejecting off-mesh
positions, room switching, profile save/reload and screenshot composition.
`PE_TEST_CHROMIUM` may select an already-installed headless Chromium executable.
`PE_LIGHTING_URL` and `PE_LIGHTING_EVIDENCE` override the scratch server URL and
screenshot directory. Use an empty scratch profile directory for each run.

The editor's read-only `window.lightingDiagnostics()` exposes room, actor and
light state, plus projected gizmo locations for autonomous UI tests.
