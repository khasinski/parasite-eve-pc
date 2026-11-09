# Parasite Eve for Dreamcast

A port of Parasite Eve (PS1, 1998) to the Sega Dreamcast, built on
[KallistiOS](https://github.com/KallistiOS/KallistiOS). It runs the original
game code from the [native PC port](https://github.com/khasinski/parasite-eve-pc)
on [a PsyZ fork](https://github.com/andruxaster/parasite-eve-psyz)
(branch `dc-small-memory`) with a Dreamcast backend: a PowerVR renderer,
AICA sound, MDEC movies on the SH-4, memory cards on VMUs and disc swapping.

**Status:** early alpha. It plays from the title screen through the opening
of disc 1 (Carnegie Hall: field scenes, battles, menus, movies), saves to the
VMU and swaps discs. Tested mostly in Flycast; testing on hardware has just
begun. Later parts of the game are not checked yet.

## What you need

- Your own **North American (USA) disc images**, raw MODE2/2352 BIN
  (`Parasite Eve (USA) (Disc 1).bin`, `(Disc 2).bin`). No CUE sheet is
  needed. Game data is never part of this repository.
- A built `1ST_READ.BIN` (see [Build](#build)).
- To burn discs, BootDreams or any tool that makes a self-booting CDI
  from a folder with `1ST_READ.BIN`.

## Making the discs

The two images (about 495 and 647 MB) do not fit on one CD-R, so the game
takes two discs, like the original:

| Disc | Files |
| --- | --- |
| 1 | `1ST_READ.BIN`, `PE1.BIN` (disc 1 image renamed) |
| 2 | `1ST_READ.BIN`, `PE2.BIN` (disc 2 image renamed) |

Boot disc 1. When the game needs the other disc, a prompt asks for it: open
the lid, change the disc and close it. A wrong disc is ignored; the game goes
on once the right one is read. Booting disc 2 asks for disc 1 for the title
screen in the same way.

The images can also come from a dc-tool host folder (`/pc/pe1.bin`,
`/pc/pe2.bin`) or an SD card adapter (`/sd/...`); then both are mounted
from the start and there is no prompt. See
[platform/dreamcast/README.md](platform/dreamcast/README.md) for every path.

## Controls

| Dreamcast | PS1 |
| --- | --- |
| A / B / X / Y | Cross / Circle / Square / Triangle |
| L / R triggers | L1 / R1 |
| Start | Start |
| D-pad, analog stick | D-pad, left stick |
| C / Z / D (controllers that have them) | R2 / L2 / Select |

The first controller plays.

## Saves

Memory card 1 is the VMU in controller port A, card 2 the VMU in port B. A
port without a VMU shows as no card. Each save is one VMU file of 19 blocks
(of 200), with the save's own icon and title in the Dreamcast file manager,
so a VMU holds about ten.

## What works

- **Picture.** The PS1 GPU is emulated on the PowerVR: textures are decoded
  from a CPU copy of the VRAM into a texture cache, with CLUTs, the four
  semi-transparency modes, texture windows, the drawing-area clip and the
  24-bit display used by movies. Textures are point-sampled and models wobble
  as on the PS1.
- **Speed.** Field scenes run at the game's 30 fps; the heaviest room
  measured (the dressing room with the burnt bodies, about 5200 vertices a
  frame) holds 29.5 fps with about 7 ms of each frame to spare.
- **Movies** (MDEC) decode on the SH-4 at about 12-15 fps.
- **Sound.** The PS1 SPU voices play on AICA channels; CD and XA audio
  stream at their own 37.8 kHz.
- **Saves** on VMUs, **disc swap** between the two burnt discs.

## Known gaps

- No reverb (the AICA DSP is not programmed yet).
- Not emulated: drawing into VRAM and sampling it back as a texture,
  reading rendered pixels back, mask bits, dithering, per-texel STP on
  semi-transparent primitives, textured colours above 0x80 (the PVR
  modulates up to 1.0, the PS1 up to 2.0).
- Movies run below the PS1's 15 fps in places.
- The PC port's extras (HD backgrounds, widescreen, 60 Hz interpolation,
  mods, settings overlay) are not part of the Dreamcast build.

Report problems with the log of a normal (non-release) build: it prints
GPU statistics and a sampling profile every 300 frames.

## Build

Needs a KallistiOS toolchain (`sh-elf` GCC, newlib) and a built KOS tree
(tested with KOS v2.3.0 and GCC 15.2).

```sh
git clone https://github.com/andruxaster/parasite-eve-dc
cd parasite-eve-dc
git checkout dc-small-memory
git submodule update --init third_party/psyz
git -C third_party/psyz submodule update --init external/SDL

source /opt/toolchains/dc/kos/environ.sh
cmake -S . -B build-dc -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$KOS_CMAKE_TOOLCHAIN \
  -DCMAKE_BUILD_TYPE=Release -DPE_SMALL_MEMORY=ON
cmake --build build-dc

sh-elf-objcopy -R .stack -O binary build-dc/parasite-eve.elf pe.bin
$KOS_BASE/utils/scramble/scramble pe.bin 1ST_READ.BIN
```

This build logs to the debug console (dc-tool or the emulator's log). For
players, configure another directory with `-DPE_DC_RELEASE=ON`: no KOS
banner, no logs, no profiler.

The ELF can also run over dc-tool (`dc-tool-ip -t <ip> -x
build-dc/parasite-eve.elf`) with the images in the host folder.

## Documentation

- [docs/DREAMCAST.md](docs/DREAMCAST.md): memory budget, measurements and
  the history of the port.
- [platform/dreamcast/README.md](platform/dreamcast/README.md): the
  Dreamcast host, disc paths, build options.
- [PsyZ Dreamcast backend](https://github.com/andruxaster/parasite-eve-psyz/tree/dc-small-memory/psyz/src/dreamcast):
  renderer, sound, movies, memory cards, profiler.
- [platform/dreamcast/pchost](platform/dreamcast/pchost/README.md): a PC
  harness that runs the Dreamcast build with a software PowerVR, to check
  frames and sound without hardware.

## Credits

The game code comes from the
[Parasite Eve native PC port](https://github.com/khasinski/parasite-eve-pc)
and its PsyZ fork; see that project for the PC version, its mods and its
community. The Dreamcast port is by andruxaster.
