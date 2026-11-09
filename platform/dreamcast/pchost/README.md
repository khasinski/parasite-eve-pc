# PC harness for the Dreamcast build

Runs the Dreamcast build of the game on a PC: `dc_main.c`, the PsyZ
Dreamcast backend (`dc_gpu.c`, `dc_video.c`, `dc_audio.c`) and every game
source, compiled for 32-bit x86. Only what touches Dreamcast hardware is
swapped:

| Dreamcast | Harness |
| --- | --- |
| PowerVR (`dc/pvr.h`) | `fakepvr.c`: software rasteriser with the same blend factors, texture formats, bilinear filtering and perspective-correct texturing (z = 1/w); saves chosen frames as PPM |
| KOS threads (`dc_os.c`) | `host_os.c`: pthreads |
| Maple controller (`dc_input.c`) | `host_input.c`: released pad; buttons come from the game's own input schedule |
| vblank, timers | virtual clock: one frame per `vid_waitvbl()`, as fast as the host runs; kernel time (root counters, so the music sequencer) follows it |
| AICA, sound stream (`dc_aica.c`, `dc_audio.c`) | `host_aica.c`: software channels; `host_audio.c`: one vblank of sound per vblank |

It is for checking the renderer and memory against real game frames without
hardware or an emulator. It says nothing about SH-4 speed.

## Build

```sh
# a configured Dreamcast build (see docs/DREAMCAST.md) provides the sources and flags
python3 platform/dreamcast/pchost/build.py build-dc pchost-out
```

Needs `gcc-multilib`.

## Run

Put the discs where `dc_main.c` looks for them over dc-tool:
`/pc/pe1.cue` and `/pc/pe2.cue` (each next to its `.bin`).

```sh
# run 26000 vblanks; press Start at 10000, Up at 11000, Cross at 11500...
# (PSY-Q bits: 8 Start, 16 Up, 32 Right, 64 Down, 128 Left, 16384 Cross)
DCHOST_DIR=frames DCHOST_EVERY=600 PE_MEMORY_REPORT=1 \
  pchost-out/dchost 26000 10000:8 10010:0 11000:16 11010:0 11500:16384 11510:0
```

- `DCHOST_EVERY=N` saves a frame every N vblanks, or `DCHOST_SAVE=a,b,c`
  saves the first frame at or after each listed vblank.
- Frames go to `DCHOST_DIR/vbNNNNNN.ppm` (640x480, as the PVR would show
  them).
- At the limit the game prints its diagnostic JSON (scene, display mode,
  dialogue, actors) and exits.
- `DCHOST_WAV=file.wav` records the sound (`DCHOST_WAV_FROM`/`DCHOST_WAV_TO`
  bound it in vblanks); `DCHOST_ENVX=file` writes every voice's ENVX and
  start address per vblank.
- Built with `DCHOST_SPU_REFERENCE=1` (into another output directory), the
  portable software SPU makes the sound instead of the AICA path, to compare
  against; `DCHOST_NO_REVERB=1` mutes its reverb, which the AICA path lacks.
- `DCHOST_WALL_CLOCK=1` runs kernel time on the host clock (the music then
  depends on how fast the host is).
- `PE_MEMORY_REPORT=1` logs the game's buffer peaks (`[mem]`) and the
  heap's (`[heap]`).
- Built with `DCGPU_TRACE=1`, `dc_gpu.c` can log the primitives of the
  saved frames (add `fprintf`s under `#ifdef DCGPU_TRACE`).

The 26000-vblank schedule above (with Cross at 17000-21800 every 600, then
Start, Start, Cross, Cross at 23000-24800) is the one `tests/hd_hot_apply.py`
uses to reach the first field scene. Holding Up from 26000 to 26900, then
Cross every 600 with a short Up after each, plays on through the opera
house, the fire and the first movie of Aya in the auditorium (about 50000).
