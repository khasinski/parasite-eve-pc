# Dialogue translation editor

The editor reads the original dialogue from USA Disc 1 or Disc 2. Its bundled catalog holds 2,161 unique dialogue lines plus room and bank views, control signatures, and layout metadata. Rooms with unresolved page references expose their shared bank. Scene labels use the game’s save-location table and known checkpoint names. The original-dialogue search runs locally on text decoded from the disc.

The editor is a Rust program and needs no Python. Build and package it on each target operating system with a Rust toolchain and a C compiler (FreeType is compiled into the application):

```text
cargo run --release --manifest-path localization/editor/Cargo.toml --bin package -- --out editor-dist
```

The build checks the demo's two preview boxes and lays out the interface without opening a window. It also exercises the fixes and editable-pack round trips and includes their samples in the archive.

The **Translation editor** Actions workflow builds Windows x64, macOS Apple Silicon and Intel, and Linux x64 on Ubuntu 22.04. Download the archive from the matching job's artifacts. The macOS artifacts are development bundles without Developer ID notarization.

To launch from source:

```text
cargo run --release --manifest-path localization/editor/Cargo.toml
```

On Linux, building needs the X11 and Wayland development packages listed in the workflow. The editor needs OpenGL 2 or later. Text boxes display CJK with the system's fonts (Microsoft JhengHei, YaHei, Meiryo, Yu Gothic or Malgun Gothic on Windows; PingFang, Hiragino or Apple SD Gothic on macOS; Noto Sans CJK on Linux).

To run the tests, and to add real-disc verification to the file round trip:

```text
cargo test --manifest-path localization/editor/Cargo.toml
cargo run --release --manifest-path localization/editor/Cargo.toml --bin test_roundtrip -- editor-dist/demo --disc "path/to/USA Disc 1.cue"
```

The included font is a renamed 21-glyph Noto Sans CJK TC subset under the SIL Open Font License. The source and license are in `fonts/`. Editable packs contain their own raster glyphs, so reopening an exported pack does not require the original font.

To regenerate the text-free catalog from the pinned source manifest and both US discs:

```text
cargo run --release --manifest-path localization/editor/Cargo.toml --bin build_catalog -- "USA Disc 1.bin" "USA Disc 2.bin"
```

AI exchange CSVs include the export scope and stable line IDs. Imports check the exact ID set for that scope, the original text hash, and the game tags. The earlier eight-line opera CSVs remain importable.

The dialogue compiler here follows `localization/pelang.py` and `localization/petext/`, which remain the command-line tools for full language packs. A change to the text encoding, control validation, layout, wrapping or pack format in those files needs the same change in `src/`. Project files and packs interchange between the two.
