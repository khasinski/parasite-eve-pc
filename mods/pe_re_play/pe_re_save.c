/* The imported game's progress in the save. See pe_re_save.h. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "pe_re_save.h"

#define LOW_GROUPS 10                      /* stored whole */
#define ALL_GROUPS 36
#define HIGH_BYTES 16                      /* the first 128 bits of the others */

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xFFFF), put16(p + 2, v >> 16); }
static uint32_t get16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t get32(const uint8_t *p) { return get16(p) | get16(p + 2) << 16; }

static int game_number(const PeReGame *g) {
    for (int i = 0; i < PE_RE_GAMES; i++) if (pe_re_game(i) == g) return i + 1;
    return 0;
}

void pe_re_save_encode(const PeReSession *s, int playing, uint8_t out[PE_RE_SAVE_SIZE]) {
    memset(out, 0, PE_RE_SAVE_SIZE);
    if (!s->game) return;
    const PeReProgress *p = &s->progress;
    out[0] = (uint8_t)game_number(s->game), out[1] = (uint8_t)(playing != 0), out[2] = (uint8_t)s->player;
    out[3] = (uint8_t)s->stage, out[4] = (uint8_t)s->room, out[5] = (uint8_t)s->camera;
    put16(out + 6, (uint32_t)(s->facing & 4095));
    put32(out + 8, (uint32_t)(int32_t)lround(s->x)), put32(out + 12, (uint32_t)(int32_t)lround(s->z));
    memcpy(out + 16, p->unlocked, 32);
    for (int i = 0; i < p->held && i < PE_RE_POCKET_MAX; i++)
        put16(out + 48 + 4 * i, p->pocket[i].item), put16(out + 50 + 4 * i, p->pocket[i].count);
    for (int g = 0; g < LOW_GROUPS; g++) memcpy(out + 304 + 32 * g, p->flags.bits[g], 32);
    for (int g = LOW_GROUPS; g < ALL_GROUPS && g < PE_RE_FLAG_GROUPS; g++)
        memcpy(out + 624 + HIGH_BYTES * (g - LOW_GROUPS), p->flags.bits[g], HIGH_BYTES);
}

int pe_re_save_decode(const uint8_t in[PE_RE_SAVE_SIZE], PeReSaved *out) {
    memset(out, 0, sizeof *out);
    if (!in[0]) return 0;                                  /* no game */
    out->game = pe_re_game(in[0] - 1);
    if (!out->game || in[2] > 1) return -1;
    out->playing = in[1] != 0, out->player = in[2], out->stage = in[3], out->room = in[4], out->camera = in[5];
    out->facing = (int)get16(in + 6) & 4095;
    out->x = (int32_t)get32(in + 8), out->z = (int32_t)get32(in + 12);
    PeReProgress *p = &out->progress;
    memcpy(p->unlocked, in + 16, 32);
    for (int i = 0; i < PE_RE_POCKET_MAX; i++) {
        const uint16_t item = (uint16_t)get16(in + 48 + 4 * i), count = (uint16_t)get16(in + 50 + 4 * i);
        if (!item) break;
        p->pocket[p->held].item = item, p->pocket[p->held++].count = count;
    }
    for (int g = 0; g < LOW_GROUPS; g++) memcpy(p->flags.bits[g], in + 304 + 32 * g, 32);
    for (int g = LOW_GROUPS; g < ALL_GROUPS && g < PE_RE_FLAG_GROUPS; g++)
        memcpy(p->flags.bits[g], in + 624 + HIGH_BYTES * (g - LOW_GROUPS), HIGH_BYTES);
    return 0;
}

int pe_re_session_resume(PeReSession *s, const char *store, const PeReSaved *saved, char *why, size_t n) {
    if (!saved->game) { snprintf(why, n, "no game in this save"); return -1; }
    if (pe_re_session_start(s, store, saved->game, saved->player, why, n)) return -1;
    s->progress = saved->progress;                        /* before the room's scripts run */
    if (pe_re_session_enter(s, store, saved->stage, saved->room, saved->camera, saved->x, saved->z, saved->facing, why, n)) {
        pe_re_session_end(s);
        return -1;
    }
    return 0;
}
