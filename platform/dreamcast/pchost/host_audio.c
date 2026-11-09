/* Audio for the harness in place of dc_audio.c: one vblank (735 frames at
 * 44.1 kHz) of SPU per virtual vblank, mixed by host_aica.c, written to
 * DCHOST_WAV (between vblanks DCHOST_WAV_FROM and DCHOST_WAV_TO if set).
 * Built with HOST_AUDIO_REFERENCE, the portable software SPU (psyz_spu.c)
 * makes the sound instead, for comparison; DCHOST_NO_REVERB=1 then mutes
 * its reverb, which the AICA path does not have. */
#define _GNU_SOURCE 1
#include <psyz.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../third_party/psyz/decomp/src/libspu/libspu_private.h"
#include "../../../third_party/psyz/psyz/src/dreamcast/dc_spu.h"

#define FRAMES 735

static pthread_mutex_t lock = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
static int is_init, is_paused;
static unsigned vblank, output_volume = 100;
static FILE* wav;
static unsigned wav_from, wav_to = ~0u, wav_frames;

void HostAica_Render(int* out, unsigned frames);

void Psyz_AudioSetCaptureCallback(PsyzAudioCaptureCallback cb, void* data) { (void)cb; (void)data; }

static void put32(unsigned char* p, unsigned v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void wav_header(void) {
    unsigned char h[44] = "RIFF....WAVEfmt \x10\0\0\0\x01\0\x02\0....\0\0\0\0\x04\0\x10\0data....";
    put32(h + 4, 36 + wav_frames * 4);
    put32(h + 24, 44100);
    put32(h + 28, 44100 * 4);
    put32(h + 40, wav_frames * 4);
    fseek(wav, 0, SEEK_SET);
    fwrite(h, 1, 44, wav);
    fseek(wav, 0, SEEK_END);
}

int Psyz_AudioInit(void) {
    if (is_init)
        return 0;
    Psyz_SpuInit();
    const char* path = getenv("DCHOST_WAV");
    if (path && (wav = fopen(path, "wb"))) {
        if (getenv("DCHOST_WAV_FROM")) wav_from = (unsigned)atoi(getenv("DCHOST_WAV_FROM"));
        if (getenv("DCHOST_WAV_TO")) wav_to = (unsigned)atoi(getenv("DCHOST_WAV_TO"));
        wav_header();
    }
    is_init = 1;
    return 0;
}

/* called by fakepvr.c at every virtual vblank */
void host_audio_vblank(void) {
    vblank++;
    if (!is_init || is_paused)
        return;
    short out[FRAMES * 2];
#ifdef HOST_AUDIO_REFERENCE
    if (getenv("DCHOST_NO_REVERB")) {
        _spu_RXX->rxx.rev_vol.left = 0;
        _spu_RXX->rxx.rev_vol.right = 0;
    }
    Psyz_SpuPullSamples(out, FRAMES);
#else
    int mix[FRAMES * 2];
    memset(mix, 0, sizeof(mix));
    DcSpu_Update(FRAMES);
    HostAica_Render(mix, FRAMES);
    DcSpu_MixCd(out, FRAMES);
    for (int i = 0; i < FRAMES * 2; i++) {
        int s = mix[i] + out[i];
        out[i] = (short)(s < -32768 ? -32768 : s > 32767 ? 32767 : s);
    }
#endif
    static FILE* envx;
    if (!envx && getenv("DCHOST_ENVX"))
        envx = fopen(getenv("DCHOST_ENVX"), "wb");
    if (envx) {
        /* every voice's ENVX and key-on address, for comparing backends */
        unsigned short regs[48];
        for (int v = 0; v < 24; v++) {
            regs[v * 2] = _spu_RXX->rxx.voice[v].volumex;
            regs[v * 2 + 1] = _spu_RXX->rxx.voice[v].addr;
        }
        fwrite(regs, sizeof(regs), 1, envx);
    }
    if (wav && vblank >= wav_from && vblank < wav_to) {
        fwrite(out, 4, FRAMES, wav);
        wav_frames += FRAMES;
        if (!(wav_frames % (FRAMES * 600)))
            wav_header();
    }
}

void Psyz_AudioDestroy(void) {
    if (wav) {
        wav_header();
        fclose(wav);
        wav = NULL;
    }
}
int Psyz_AudioWaitForTimer(void) { return 0; }
void Psyz_AudioPause(void) { is_paused = 1; }
int Psyz_AudioIsPaused(void) { return is_paused; }
void Psyz_AudioUnpause(void) { is_paused = 0; }
int Psyz_AudioSetOutputVolume(unsigned p) { output_volume = p > 100 ? 100 : p; return 0; }
unsigned Psyz_AudioGetOutputVolume(void) { return output_volume; }
void Psyz_AudioLock(void) { pthread_mutex_lock(&lock); }
void Psyz_AudioUnlock(void) { pthread_mutex_unlock(&lock); }
