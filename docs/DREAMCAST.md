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
| Matrix stack (`g_RenderTransformStack`) | 65536 entries | 256 |
| Room TIM workspace (`D_8010BD00`) | 8 MB | 2 MB |

The option also compiles out two desktop presentation features
([`include/pe1/port_features.h`](../include/pe1/port_features.h)):

- **PGXP** (`PE_PGXP=0`, and `PSYZ_NO_PGXP` in PsyZ): the double-precision
  bone and model matrices, their caches and the subpixel vertex paths in the
  game, and the PGXP work inside PsyZ's GTE commands. Dynamic actor lighting
  reads the same matrices and goes too. The PsyZ PGXP calls stay valid and
  behave as with PGXP switched off.
- **Frame interpolation** (`PE_FRAME_INTERPOLATION=0`): the game no longer
  registers packet pairs or drives the 60 Hz presentation, so a backend does
  not need to implement it.

The desktop menu still shows these settings; in a small build they have no
effect. After this, `object.c`, `fieldfx/model.c`, `fieldfx/geometry.c`,
`fieldfx/sprite_render.c` and PsyZ's `libgte.c` contain no floating-point
instructions. Floating point remains in the widescreen camera
(`render/camera.c`, `render/mesh_list.c`), host timing (`boot/init.c`, PsyZ
`libcd.c`, `kernel.c`) and one battle routine (`battle/enemy_movement.c`).

## PsyZ fork

`third_party/psyz` points to
[andruxaster/parasite-eve-psyz](https://github.com/andruxaster/parasite-eve-psyz),
branch `dc-small-memory`, a fork of khasinski/parasite-eve-psyz taken at the
commit this game release pins. Dreamcast changes to PsyZ (overridable PGXP
cache sizes, `PSYZ_NO_PGXP` and the KallistiOS backend) go there. After pulling a change
to the submodule, run `git submodule sync && git submodule update --init third_party/psyz`.

## Measurements (8 November 2026, release 0.11)

Code / initialised data / zero-initialised data in MB, summed over object
files before `--gc-sections`:

| Part | x86-64 | 32-bit | 32-bit small |
|---|---|---|---|
| Game | 0.89 / 0.11 / 24.25 | 0.95 / 0.07 / 23.88 | 0.93 / 0.07 / 9.38 |
| PsyZ core (GTE, SPU, MDEC...) | 0.18 / 0.01 / 3.48 | 0.20 / 0.01 / 2.90 | 0.19 / 0.01 / 0.72 |
| PsyZ SDL3 backend (replaced on Dreamcast) | 0.12 / 0.06 / 7.41 | 0.14 / 0.06 / 6.78 | 0.14 / 0.06 / 6.78 |
| Desktop shell, SDL3, webp, cJSON, miniz (not used on Dreamcast) | ~2.1 / 0.5 / 2.1 | same | same |

Largest game buffers left in the small build: `D_80120D08` (2 MB, the scene
arena), `D_8010BD00` (2 MB, room textures and mesh packets), the CD/movie
work area `g_BootDisplayWorkArea` (1 MB), a diagnostic VRAM copy in
`init.c` (1 MB, not needed on Dreamcast), the memory card screens (0.5 MB),
the movie buffers (0.34 MB) and several 256 KB tables. The VRAM copy is now
allocated only when a capture is requested.

Estimate for the Dreamcast: game and PsyZ core about 11.3 MB, plus a native
backend with its 1 MB VRAM copy and KallistiOS, about 12.3 MB of 16 MB.
Peaks recorded with real discs will show whether the texture workspace and
the CD work area can be made smaller.

## SH-4 build (KallistiOS)

The game links for the Dreamcast against the PsyZ Dreamcast backend
(`third_party/psyz/psyz/src/dreamcast/`: PowerVR renderer, AICA sound) and
the host in [`platform/dreamcast/`](../platform/dreamcast/README.md):

```sh
source /opt/toolchains/dc/kos/environ.sh
cmake -S . -B build-dc -G Ninja -DCMAKE_TOOLCHAIN_FILE=$KOS_CMAKE_TOOLCHAIN \
  -DCMAKE_BUILD_TYPE=Release -DPE_SMALL_MEMORY=ON
cmake --build build-dc
```

Port notes:

- sh-elf GCC makes `int32_t` a `long`. The game declares the same symbols
  with `int`, `s32` and `int32_t` in different headers, which agree on every
  desktop target; the Dreamcast build redefines GCC's `__INT32_TYPE__` family
  as `int` (same size and ABI) instead of touching hundreds of declarations.
- newlib declares but does not build `flockfile`/`funlockfile`;
  `platform/dreamcast/dc_libc.c` wraps newlib's per-`FILE` lock.
- `_GNU_SOURCE` is defined because newlib hides POSIX declarations under
  `-std=c11` when a file sets `_POSIX_C_SOURCE` after its first include.
- No frame pointers: they only serve the desktop crash reporter.

Every game, PsyZ core and PsyQ decomp source compiled for the SH-4 with no
source change beyond the above.

### Measured (GCC 15.2, `-O3`, KallistiOS master, 8 November 2026)

`sh-elf-size build-dc/parasite-eve.elf`:

| text | data | bss | total |
|---|---|---|---|
| 1.18 MB | 0.08 MB | 10.54 MB | **11.80 MB** |

With the PVR renderer the ELF is 1.20 MB text, 0.08 MB data and 10.55 MB
bss: **11.83 MB**. Textures live in the PVR's own 8 MB (about 30 decoded
256x256 pages beside the framebuffers, a 512x512 movie frame and a 1 MB
vertex buffer), so main RAM grows only by the renderer's code.

By part, from the link map of the stub build (rounded; padding counted
twice):

| Part | Code + rodata | Data | bss |
|---|---|---|---|
| Game | 0.91 MB | 0.07 MB | 8.78 MB |
| PsyZ core + backend | 0.17 MB | 0.01 MB | 1.77 MB |
| KallistiOS | 0.33 MB | 0.01 MB | 0.04 MB |
| newlib, libgcc | 0.11 MB | — | — |

The ELF loads at `0x8c010000`, so about **4.1 MB** remain for the heap and
thread stacks. Biggest static buffers: the two 2 MB arenas in `init.c`
(`D_8010BD00` room textures, `D_80120D08` scene arena), the CD/movie work
area (1 MB), the backend's VRAM copy (1 MB), the SPU state with its 512 KB
sound RAM (0.53 MB), the memory card screens (0.5 MB) and the movie buffers
(0.34 MB).

The SPU's 512 KB copy can move to the AICA's own 2 MB once audio is real,
and the backend's VRAM copy partly into the PVR's 8 MB.

### Peaks with the discs (9 November 2026)

Measured with the PC harness below (the Dreamcast build's sources and
flags on 32-bit x86), from boot through the intro movies, title, name
entry, the Carnegie Hall street and entrance (M010, M431), the lobby and
the opera with its fire (M003, M004, M372), to Aya in the auditorium:

| Counter | Peak | Budget |
|---|---|---|
| room texture read | 374 KB (M372) | 2 MB |
| room asset read | 334 KB (M372) | — |
| mesh packet set | 125 KB (M004) | 2 MB |
| room scratch read | 66 KB (M010) | — |
| transform stack depth | 2 | 256 |
| heap in use | 1.78 MB | — |

So far: 11.83 MB static + 1.8 MB heap ≈ 13.6 MB of 16. Battles, later
rooms, the memory card screen and disc 2 are not measured yet; the two
2 MB arenas look far larger than these rooms need, but only a full
playthrough (or a save at the larger rooms) can say by how much.

## PC harness

[`platform/dreamcast/pchost/`](../platform/dreamcast/pchost/README.md)
runs the Dreamcast build of the game on a PC with a software PVR and saves
frames as the PVR would show them, with scripted input. It found three
renderer bugs before any hardware run: 24-bit movies, the display's
vertical position, and subtractive blending (shadows).

## Movies on the SH-4 (9 November 2026)

With `PE_MOVIE_PROFILE=1` (set in the Dreamcast build) both movie players
print where each frame's time goes. In Flycast the first build spent about
120 ms per frame in the MDEC (IDCT and colour conversion), so movies ran
at 6-8 frames per second instead of 15; reading sectors took 2 ms and VLC
4.5 ms. The pace is the game's own `VSync(4)`, not drive speed: the movie
players read sectors directly. PsyZ's MDEC now takes under 5 million SH-4
instructions per frame instead of about 20 million (about 30 ms), still
exact to the bit; see `third_party/psyz/psyz/src/dreamcast/README.md`.

## Sound (9 November 2026)

PsyZ's software SPU would need 160-600 million SH-4 instructions per second
of sound, so the voices play on AICA channels (`dc_spu.c`, see the PsyZ
Dreamcast README): PS1 ADPCM is decoded to PCM in sound RAM, envelopes run
on the CPU and set channel levels every 5 ms, CD/XA audio goes out on a KOS
sound stream. The SPU RAM copy stays in main RAM (the game reads it back).

## Picture (9 November 2026)

Polygons and movies are filtered bilinearly (sprites and text stay sharp),
and projected vertices keep the GTE's subpixel fraction and depth, so models
no longer snap to whole pixels and their textures are perspective-correct
(see the PsyZ Dreamcast README).

## Next steps

1. Boot the ELF on hardware or in Flycast with the discs (`/cd/PE1.BIN`
   and `/cd/PE2.BIN` on the burnt disc, no CUE needed; see
   `platform/dreamcast/README.md`) and check speed.
2. Measure battles, later rooms and the memory card screen; then size the
   two 2 MB arenas.
3. Reverb on the AICA DSP; speed (store-queue vertex submission, twiddled
   or paletted textures).
