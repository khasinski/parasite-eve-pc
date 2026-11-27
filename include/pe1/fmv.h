#ifndef PE1_FMV_H
#define PE1_FMV_H

/* FMV / streamed-video subsystem (opening movie, cutscenes).
 *
 * Characterised live on real BIOS (DuckStation GDB) by stepping through the
 * first FMV sequence: captured the game-state transitions in/out of playback.
 *
 * == PC PLAYBACK ==
 * Both movie players look up the authored VideoEntry name on the mounted
 * disc, for example \FMV1\FMV001.STR;1. The directory is FMV1 or FMV2.
 * PEDISC01.IDF and PEDISC02.IDF identify the discs. The movie payloads are
 * in the individual STR files (see menu_memcard/video_open.inc).
 *
 * video_frame.inc assembles STR frames and uses the stream header's
 * frameCount for end-of-clip and volume fade decisions. XA audio uses its
 * own sector reader. video_step.inc starts MDEC decoding, and
 * video_upload.inc transfers the decoded slices to the display regions.
 *
 * == STATE-MACHINE BRACKETING (verified live) ==
 *   FMV is dispatched as its own SCENE through the boot/main.c dispatcher on
 *   g_SceneDispatchToken (0x8009D280, the scene-dispatch token; see boot/main.c). For the
 *   opening movie:
 *       before FMV : g_SceneDispatchToken = 0xA80001C8   flags = 0x40000000
 *       FMV playing: g_SceneDispatchToken = 0xA8067148   flags = 0x4000A240
 *       after  FMV : g_SceneDispatchToken = 0xA8000248   flags = 0x40002000
 *   (token values are per-scene; the point is FMV gets a distinct token and
 *    returns to a gameplay token on completion.)
 *
 *   FMV-ACTIVE FLAGS in g_GameStateFlagsWord[0] (0x800B0CD8): the bits set only while a
 *   movie plays are 0x8000 | 0x200 | 0x40 = 0x8240. They clear on exit. (0x2000
 *   is a general "game running" bit that persists into gameplay; 0x40000000 is
 *   always set here.)
 *
 * == PER-FRAME ACTIVITY (verified by snapshot-diff during playback) ==
 *   Main RAM churn during playback is small (~53 dwords in 0x80090000..0x800B2000),
 *   i.e. the VIDEO does NOT pass through observable main RAM (see below). What
 *   advances each frame:
 *     - a synchronised frame/stream counter, mirrored at 0x8009D250,
 *       0x800A34xx and the low half of 0x800B0DBC (all step together).
 *     - SPU voice state at 0x800B0028..0x800B00C0 (per-voice volumes 0x7FFF /
 *       0x6D3E / ...) -> this is the FMV AUDIO (XA / streamed-to-SPU).
 *
 * == VIDEO PATH ==
 *   MDEC-decoded frames are DMA'd straight to VRAM; the pixel data never lands
 *   in CPU-visible main RAM (a 0x801D0000 buffer that the scene-load pointers
 *   reference stays 0% changed during playback -- it is static loaded data, not
 *   the video). Consequence: the frames are invisible to the GDB main-RAM path
 *   (DuckStation's RSP does not expose VRAM). The PC path above implements this sequence.
 *
 * Quick live probe (tools at /tmp/fmv_probe.py): scene token = *(u32*)0x8009D280;
 *   FMV-active iff (*(u32*)0x800B0CD8 & 0x8240) != 0; frame counter at 0x8009D250.
 */

#endif /* PE1_FMV_H */
