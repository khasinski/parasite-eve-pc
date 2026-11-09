# Sega Dreamcast host

`dc_main.c` replaces the desktop `src/main/boot/main.c` on KallistiOS: it
mounts the two USA disc images and runs the original scene dispatch loop.
`dc_sdl.c` provides the handful of SDL3 calls the game modules make (save
files, environment, ticks); SDL itself is not built. `dc_libc.c` adds the
stdio locks newlib declares but does not build.

The PsyZ side lives in `third_party/psyz/psyz/src/dreamcast/`: a PowerVR
renderer (see its README for what is and isn't emulated), AICA sound and
memory cards on VMUs (card 1 in controller port A, card 2 in port B).

## Build

Needs a KallistiOS toolchain (`sh-elf` GCC, newlib) and a built KOS tree.

```sh
source /opt/toolchains/dc/kos/environ.sh
cmake -S . -B build-dc -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$KOS_CMAKE_TOOLCHAIN \
  -DCMAKE_BUILD_TYPE=Release -DPE_SMALL_MEMORY=ON
cmake --build build-dc
sh-elf-size build-dc/parasite-eve.elf
```

This build logs to the debug console: GPU statistics and the profiler
(`prof:` lines) every 300 frames, movie timings, mounted discs. For players,
add `-DPE_DC_RELEASE=ON` (in its own build directory): no KOS banner, no
logs, no profiler.

## Disc images

Raw MODE2/2352 BIN images need no CUE sheet: the image type comes from its
ISO9660 header and which disc it is from its contents. The first set that
opens wins:

| Where | Disc 1 | Disc 2 |
| --- | --- | --- |
| burnt disc (ISO9660) | `/cd/PE1.BIN` | `/cd/PE2.BIN` |
| | `/cd/PE1.CUE` | `/cd/PE2.CUE` |
| dc-tool host folder | `/pc/pe1.bin` | `/pc/pe2.bin` |
| | `/pc/pe1.cue` | `/pc/pe2.cue` |
| SD card adapter | `/sd/pe1.bin` | `/sd/pe2.bin` |
| | `/sd/pe1.cue` | `/sd/pe2.cue` |

The two images (about 495 and 647 MB) do not fit on one CD-R, so burn two
discs: `1ST_READ.BIN` with `PE1.BIN`, and `1ST_READ.BIN` with `PE2.BIN`.
Boot disc 1. When the game needs the other disc, a prompt asks for it:
open the lid, change the disc and close it; the game goes on once the
right disc is read (a wrong one is ignored). Booting disc 2 asks for disc 1
for the title screen the same way. Over dc-tool or SD both images are
mounted from the start, so there is no prompt.

Disc images are never part of this repository.
