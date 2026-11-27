# Parasite Eve

[![Discord](https://img.shields.io/badge/Discord-Join%20the%20community-5865F2?logo=discord&logoColor=white)](https://discord.gg/bQkCVvrkem)

A native PC port of Parasite Eve, built from the original game code with
[our PsyZ fork](https://github.com/khasinski/parasite-eve-psyz).

![Parasite Eve running in the native port](docs/images/gameplay.png?v=3)

## Play

Download the game from [GitHub Releases](https://github.com/khasinski/parasite-eve-pc/releases).
This release is in beta. See the release notes for available platforms and known issues.
See the [changelog](CHANGELOG.md) for changes.

Works with [RecompLauncher](https://recomplauncher.com): it installs and updates the game from these releases and sets it up with your Disc 1 image.

You need your own **North American CUE/BIN or CHD images**, or the **Japanese** ones (SLPS-01230/01231). Game data is not included.
With the Japanese discs the game plays as the Japanese release does: Japanese text, kana name entry, its balance, Circle to confirm and its own memory card files. Don't mix discs of the two releases.
Keep each CUE file next to its BIN file. A CHD is a single file (its first track must be the data track, as `chdman createcd` makes from a CUE/BIN).

1. Launch Parasite Eve and select your disc images. Selecting both discs enables automatic switching.
2. You can start with either disc. If the other is needed, the game asks for it by name.
3. Press **Escape** for graphics, controls, audio, disc and mod settings. You can add the other disc there later.

**HD backgrounds:** open **Escape → Mods → Background pack** to download or update the optional artwork.
The game shows download progress and checks the files before installing. Use **2x or higher** in Graphics; the artwork applies immediately.
You can also [download the pack manually](https://pe.polanka.ovh/). See [how to use the extracted pack](docs/MODS.md#using-a-manually-downloaded-hd-background-pack). Widescreen uses the artwork available for each room; some rooms still have black borders.

**Enter** is START and skips movies; in a story movie, START pauses it and asks whether to skip. View or change all bindings in **Escape → Controls**. Choose **Fast forward** there to assign a key or controller button: the game runs at 2x while it is held, or 3x or 4x with the arrows either side of the number (Left/Right on that row). **Fast-forward button: Toggle** makes each press switch the speed-up on or off instead. It is unbound until you choose a key or button, and keeps your VSync setting.
The left stick provides analog movement. **Escape → Controls → Field run threshold** sets how far to push it before Aya runs (85% by default).
Settings and selected disc paths are saved automatically. Memory cards are kept in `memory-cards` beside your user preferences. Older `bu00`/`bu10` cards from the launch or executable folder are imported once, keeping the original files.

For portable storage, create an empty `portable.txt` beside the executable (beside the `.app` on macOS), or launch with `--portable`. Settings, memory cards, mods and downloaded backgrounds then use the `userdata` folder there. Keep the application in a writable folder. An explicit `PE_CONFIG_HOME` takes precedence. Existing user saves remain in their original location; copy your preferences folder into `userdata` to move them.
The game ships with no translations. Put a `.pelang` language pack in the `lang` folder beside the game and start it: the first start that finds packs asks which language to use. To change it later, choose it under **F1 → Language** and restart. Language packs work with **All mods** off. Missing translations use the original text.

Translators can try the [Simplified Chinese rendering pilot](localization/pilots/zh-Hans/README.md). It demonstrates Chinese fonts and dialogue; it is not a complete translation.

**Escape → Graphics** includes geometry correction, antialiasing, model texture filtering and an optional **Widescreen** aspect ratio. Widescreen shows more of the original background where available; narrow scenes and movies stay in 4:3.

On the desktop GPU renderer, you can supply your own upscaled movie frames. Set
`PE_FMV_DIR` to the parent folder and use the movie's disc filename without its
extension as the subfolder name. For example, `FMV001/000001.webp` replaces
frame 1 of `FMV001.STR`. Use six-digit frame numbers matching the original STR
and 4:3 WebP images up to 4096 pixels on each side. The original disc supplies
the timing and audio. Missing frames use the disc image, and START still skips
or pauses the movie as usual.


For smoother motion, enable **Escape → Graphics → Gameplay display → 60 Hz (interpolated)**. This optional setting smooths models and camera movement while keeping the original game speed. It is off by default.

**Escape → Graphics → VSync** offers Auto, On and Off. Auto synchronizes near 60 Hz and uses a frame limiter on other displays; On prevents tearing at any refresh rate. Game speed stays the same.

For text, **F1 → Font** offers Original, Pixel Sans, Departure Mono and Plain Sans. The separate [Translator](localization/translator/README.md) can create and edit language packs.

If the game crashes, it saves a report and shows where to find it. Attach that file when reporting the problem on Discord.

## Mods

Downloads include an optional mod host. When building from source, enable it with `-DPE_MODS=ON`. Without it, the
`PE_HOOK(...)` lines in the game compile to nothing. In a mod build, **F1** opens the Mods panel;
mods start only after **F1 → Manage mods → All mods** is turned on and the game restarted.
Settings are saved in `pe_mods.ini`. The [mod directory](docs/MODS.md) lists available mods, where to find them, and which ones need extra files. See the [developer guide](mods/README.md) for writing a mod.

### PE Enhancements

**In game, press F1 and open PE Enhancements.**

- [PE Enhancements player guide](docs/pe-enhancements/README.md) explains movement, combat,
  inventory, shops, and collection features, including what’s enabled by default.
- [Super Junk and Duper Junk](docs/pe-enhancements/economy.md#super-junk-and-duper-junk) explains
  how to earn bundles, turn them into spendable Junk, and change the rules.

- **Randomizer** (`pe_randomizer`): **F1 → Randomizer**, before New Game — shuffles items and keys
  from a six-digit seed, with keys moved only where the key logic allows, and writes a spoiler file. It runs alone: with
  it on, the other mods stay off until it is turned off and the game restarted.

Mod builds are experimental; a passing build or test does not establish full in-game compatibility.

## Build

Requires Git, CMake 3.21 or newer and a C11 compiler. Linux builds also need libcurl development headers (`libcurl4-openssl-dev` on Ubuntu).

```sh
git clone https://github.com/khasinski/parasite-eve-pc
cd parasite-eve-pc
git submodule update --init third_party/psyz
git -C third_party/psyz submodule update --init external/SDL
cmake -S . -B build -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
./build/parasite-eve-pc
```

### Build with mods

```sh
cmake -S . -B build-mods -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPE_MODS=ON
cmake --build build-mods --parallel
./build-mods/parasite-eve-pc
```

`PE_MOD_LIST` picks the mods by folder name (default: `pe_qol;pe_rebalance;pe_economy;pe_rewards;pe_catalog;pe_shops;pe_collection;pe_wardrobe;pe_stacks;pe_warp;pe_quicksave;pe_cheats`).

### PS5 payload & native GPU options

Cross-compilation support for the open-source PS5 payload SDK (`-DPE_PS5_GAME=ON`, producing `parasite-eve-ps5.elf`) and optional extended native GPU / state / assist features (`-DPE_PS5_GPU_NATIVE=ON`) live in [`platform/ps5/`](platform/ps5/) and [`cmake/ps5/`](cmake/ps5/) (contributed by [@GUMMIBRI](https://github.com/GUMMIBRI)).

For development details, see [code organization](docs/CODE_ORGANIZATION.md).
[Parasite Eve Lighting Lab](tools/lighting-editor/README.md) is a separate desktop tool for experimenting with room lighting. Download it from [Releases](https://github.com/khasinski/parasite-eve-pc/releases). Lighting profiles are not yet used in the game.
