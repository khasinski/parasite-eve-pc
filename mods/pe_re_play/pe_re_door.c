/* Going through a door. See pe_re_door.h. */
#include <stdio.h>
#include <string.h>

#include "pe_re_door.h"
#include "pe_re_session.h"
#include "pe_re_stage.h"
#include "pe_mod_host.h"
#include "pe_services.h"

#define FADE_OUT 12                       /* frames */
#define HOLD 45
#define FADE_IN 12
#define CLOSE_AT 28                       /* frames into the hold: the door shuts */

static int s_frame = -1;                  /* -1: idle */
static PeAssetHandle s_wave[2];
static int s_played[2];

static void release(void) {
    const PeAssetsApi *a = pe_service_get("assets", 1);
    for (int i = 0; i < 2; i++) {
        if (a && s_wave[i]) a->release(s_wave[i]);
        s_wave[i] = 0, s_played[i] = 0;
    }
}

void pe_re_door_begin(const char *game, const PeImpDoor *door) {
    const PeAssetsApi *a = pe_service_get("assets", 1);
    char id[PE_IMP_ID_MAX];
    release();
    for (int i = 0; a && i < 2; i++) {                  /* prepared on a worker while the room dims */
        pe_re_door_sound_id(game, door, (unsigned)i + 1, id, sizeof id);
        s_wave[i] = a->acquire_sound(id);
    }
    s_frame = 0;
    pe_re_stage_fade(128, 0, 1);
}

static void play(int i) {
    const PeAssetsApi *a = pe_service_get("assets", 1);
    const PeAudioApi *au = pe_service_get("audio", 1);
    if (s_played[i] || !s_wave[i] || !a || !au) return;
    const int ready = a->status(s_wave[i]);
    if (ready < 0) { s_played[i] = 1; return; }          /* not imported: the door is silent */
    if (ready != 1) return;
    const int played = au->preview_clip(s_wave[i]);
    if (played != 0) s_played[i] = 1;
    if (played > 0) {
        pe_mod_log("pe_re_play", "door: wave %d playing", i + 1);
    }
}

PeReDoorStep pe_re_door_step(void) {
    if (s_frame < 0) return PE_RE_DOOR_IDLE;
    const int f = s_frame++;
    if (f < FADE_OUT) {
        pe_re_stage_fade(128 - 128 * (f + 1) / FADE_OUT, 0, 1);
        return PE_RE_DOOR_RUNNING;
    }
    if (f < FADE_OUT + HOLD) {
        pe_re_stage_fade(0, 1, 1);
        if (f - FADE_OUT < CLOSE_AT) play(0);              /* waits for it to be ready, a little */
        else play(1);
        return f == FADE_OUT ? PE_RE_DOOR_SWAP : PE_RE_DOOR_RUNNING;
    }
    if (f < FADE_OUT + HOLD + FADE_IN) {
        pe_re_stage_fade(128 * (f - FADE_OUT - HOLD + 1) / FADE_IN, 0, 1);
        return PE_RE_DOOR_RUNNING;
    }
    pe_re_door_cancel();
    return PE_RE_DOOR_IDLE;
}

int pe_re_door_busy(void) { return s_frame >= 0; }

void pe_re_door_cancel(void) {
    s_frame = -1;
    release();
    pe_re_stage_fade(128, 0, 0);
}
