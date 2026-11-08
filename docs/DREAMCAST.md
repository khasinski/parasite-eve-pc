# Dreamcast port: memory budget

Goal: run the game on the Sega Dreamcast (SH-4, 16 MB main RAM, 8 MB video
RAM, 2 MB sound RAM) through a PsyZ backend for KallistiOS. This page tracks
the first step: making the game build as a 32-bit program and measuring how
much memory it needs.

## Building a small-memory 32-bit binary on Linux

```sh
cmake -S . -B b32s -DPE_SMALL_MEMORY=ON -DPE_DESKTOP_MOD_DOWNLOADS=OFF \
  -DCMAKE_C_FLAGS=-m32 -DCMAKE_EXE_LINKER_FLAGS=-m32
cmake --build b32s -j
PE_MEMORY_REPORT=1 ./b32s/parasite-eve-pc --disc "Parasite Eve (Disc 1).cue" 2> mem.log
```

`-m32` needs `gcc-multilib` and 32-bit development packages for SDL's video
and audio backends. `PE_SMALL_MEMORY` also works in a normal 64-bit build;
its peaks are slightly higher because pointers are 8 bytes.

With `PE_MEMORY_REPORT=1` every new peak is printed to stderr:

```
[mem] new peak: room texture read = 344064 of 2097152 (M042)
```

Counters: transform stack depth, room texture read, room scratch read, room
asset read and mesh packet set. `OVER CAPACITY` marks a value that does not
fit the small budget. Instead of overwriting memory the game then stops that
step: a too-deep skeleton is not posed for that frame, and a room whose
textures do not fit fails to load with a message on stderr.

## `PE_SMALL_MEMORY`

Sizes live in [`include/pe1/memory_budget.h`](../include/pe1/memory_budget.h).
Without the option every size keeps its desktop value.

| Buffer | Desktop | Small |
|---|---|---|
| Matrix stack (`g_RenderTransformStack`, `g_PgxpTransformStack`) | 65536 entries | 256 |
| PGXP bone cache | 1024 sets | 128 |
| Room TIM workspace (`D_8010BD00`) | 8 MB | 2 MB |
| PsyZ PGXP address/screen caches | 8192 / 4096 sets | 1024 / 512 |

## PsyZ fork

`third_party/psyz` points to
[andruxaster/parasite-eve-psyz](https://github.com/andruxaster/parasite-eve-psyz),
branch `dc-small-memory`, a fork of khasinski/parasite-eve-psyz taken at the
commit this game release pins. Dreamcast changes to PsyZ (the PGXP cache
sizes now, the KallistiOS backend later) go there. After pulling a change
to the submodule, run `git submodule sync && git submodule update --init third_party/psyz`.

## Measurements (8 November 2026, release 0.11)

Code / initialised data / zero-initialised data in MB, summed over object
files before `--gc-sections`:

| Part | x86-64 | 32-bit | 32-bit small |
|---|---|---|---|
| Game | 0.89 / 0.11 / 24.25 | 0.95 / 0.07 / 23.88 | 0.95 / 0.07 / 9.46 |
| PsyZ core (GTE, SPU, MDEC...) | 0.18 / 0.01 / 3.48 | 0.20 / 0.01 / 2.90 | 0.20 / 0.01 / 0.99 |
| PsyZ SDL3 backend (replaced on Dreamcast) | 0.12 / 0.06 / 7.41 | 0.14 / 0.06 / 6.78 | 0.14 / 0.06 / 6.78 |
| Desktop shell, SDL3, webp, cJSON, miniz (not used on Dreamcast) | ~2.1 / 0.5 / 2.1 | same | same |

Largest game buffers left in the small build: `D_80120D08` (2 MB, the scene
arena), `D_8010BD00` (2 MB, room textures and mesh packets), the CD/movie
work area `g_BootDisplayWorkArea` (1 MB), a diagnostic VRAM copy in
`init.c` (1 MB, not needed on Dreamcast), the memory card screens (0.5 MB),
the movie buffers (0.34 MB) and several 256 KB tables.

Estimate for the Dreamcast: game and PsyZ core about 11.5 MB, plus a native
backend with its 1 MB VRAM copy and KallistiOS, about 12.5 MB of 16 MB.
Peaks recorded with real discs will show whether the texture workspace and
the CD work area can be made smaller.

## Next steps

1. Record peaks with both discs: several rooms, a battle, a movie, the
   memory card screen.
2. Remove the PGXP and frame interpolation paths from small builds (the SH-4
   is slow with `double`, and the Dreamcast GPU corrects perspective itself).
3. Write the PsyZ backend for KallistiOS, starting from PsyZ's PSP backend.
