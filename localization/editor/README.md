# Dialogue translation editor

The editor reads the original dialogue from USA Disc 1 or Disc 2. Its bundled catalog holds 2,161 unique dialogue lines plus room and bank views, control signatures, and layout metadata. Rooms with unresolved page references expose their shared bank. Scene labels use the game’s save-location table and known checkpoint names. The original-dialogue search runs locally on text decoded from the disc.

Install the build dependencies and run the recipe on each target operating system:

```text
python -m pip install -r localization/editor/requirements-build.txt
python localization/editor/build.py --out editor-dist
```

On Linux without a graphical session, run the build command under `xvfb-run -a`. The build starts the packaged application and checks that it previews both demo boxes. It also exercises the fixes and editable-pack round trips and includes their samples in the archive.

The **Translation editor** Actions workflow builds Windows x64, macOS Apple Silicon and Intel, and Linux x64 on Ubuntu 22.04. Download the archive from the matching job's artifacts. The macOS artifacts are development bundles without Developer ID notarization.

To launch from source with Python, Tkinter, and Pillow installed:

```text
python localization/editor/app.py
```

To add real-disc verification to the file round trip:

```text
python localization/editor/test_roundtrip.py editor-dist/demo --disc "path/to/USA Disc 1.cue"
```

The included font is a renamed 21-glyph Noto Sans CJK TC subset under the SIL Open Font License. The source and license are in `fonts/`. Editable packs contain their own raster glyphs, so reopening an exported pack does not require the original font.


To regenerate the text-free catalog from the pinned source manifest and both US discs:

```text
python localization/editor/build_catalog.py "USA Disc 1.bin" "USA Disc 2.bin"
```

AI exchange CSVs include the export scope and stable line IDs. Imports check the exact ID set for that scope, the original text hash, and the game tags. The earlier eight-line opera CSVs remain importable.
