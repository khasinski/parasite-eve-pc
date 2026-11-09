# Sega Dreamcast host

`dc_main.c` replaces the desktop `src/main/boot/main.c` on KallistiOS: it
mounts the two USA disc images and runs the original scene dispatch loop.
`dc_sdl.c` provides the handful of SDL3 calls the game modules make (save
files, environment, ticks); SDL itself is not built. `dc_libc.c` adds the
stdio locks newlib declares but does not build.

The PsyZ side lives in `third_party/psyz/psyz/src/dreamcast/`: a PowerVR
renderer (see its README for what is and isn't emulated) and AICA sound.

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

Disc images are never part of this repository.
