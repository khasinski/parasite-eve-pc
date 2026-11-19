/* Resident Evil 1 room animations: a room file's sections 9 and 10 (EMR, EDD) hold the room's own
 * moves for the player (cutscenes, a ladder, a push), one set per room file (Chris's and Jill's
 * differ). Layout from IntelOrca's biohazard-utils (Model/Emr.cs, Model/Edd1.cs; MIT, Ted John):
 *   EMR  u16 tree offset, u16 key frames offset, u16 bones, u16 key frame size. A key frame: s16
 *        x, y, z root offset, s16 x, y, z speed, then s16 x, y, z angles per bone (RE2 and RE3
 *        pack theirs in 12 bits).
 *   EDD  per move u16 frames, u16 offset (the first offset / 4 = the moves); a frame u32: low 12
 *        bits the key frame, the rest flags.
 * The set becomes one Animation "roomanim/<stage><room>-<player>" at 30 frames a second: per frame
 * the key frame's root offset and its angles (4096 = a full turn). Read only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp_rdt.h"
#include "pe_src_re1.h"

static uint32_t le16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }
static int16_t s16(const uint8_t *p) { uint32_t v = le16(p); return (int16_t)(v < 0x8000 ? (int)v : (int)v - 0x10000); }

static PeImpAnimation *animation(const uint8_t *rdt, uint32_t size, uint32_t emr, uint32_t edd) {
    if (!emr || !edd || emr + 8 > size || edd + 4 > size) return NULL;
    uint32_t kf = emr + le16(rdt + emr + 2), bones = le16(rdt + emr + 4), kf_size = le16(rdt + emr + 6);
    if (!bones || bones > PE_IMP_MODEL_BONES_MAX || kf_size < 12 + 6 * bones || kf >= size) return NULL;
    uint32_t keys = (size - kf) / kf_size;                       /* bounded again by the frames below */
    if (edd > kf && (edd - kf) / kf_size < keys) keys = (edd - kf) / kf_size;
    uint32_t moves = le16(rdt + edd + 2) / 4;
    if (!keys || !moves || moves > PE_IMP_ANIM_MOVES_MAX || edd + 4 * moves > size) return NULL;
    PeImpAnimation *a = calloc(1, sizeof *a);
    if (!a) return NULL;
    a->bones = bones, a->fps = 30, a->moves = moves;
    a->move = calloc(moves, sizeof *a->move);
    if (!a->move) { free(a); return NULL; }
    for (uint32_t mv = 0; mv < moves; mv++) {
        uint32_t frames = le16(rdt + edd + 4 * mv), at = edd + le16(rdt + edd + 4 * mv + 2);
        if (!frames || frames > PE_IMP_ANIM_FRAMES_MAX || at + 4ull * frames > size) continue;   /* left empty */
        PeImpMove *m = &a->move[mv];
        m->data = calloc((size_t)frames * (3 + 3 * bones), sizeof *m->data);
        m->event = calloc(frames, sizeof *m->event);
        if (!m->data || !m->event) { pe_imp_animation_free(a); free(a); return NULL; }
        m->frames = frames;
        for (uint32_t f = 0; f < frames; f++) {
            uint32_t key = le32(rdt + at + 4 * f) & 0xFFF;
            if (key >= keys) key = keys - 1;
            const uint8_t *k = rdt + kf + key * kf_size;
            int16_t *out = m->data + (size_t)f * (3 + 3 * bones);
            out[0] = s16(k), out[1] = s16(k + 2), out[2] = s16(k + 4);
            for (uint32_t b = 0; b < 3 * bones; b++) out[3 + b] = s16(k + 12 + 2 * b);
        }
    }
    char quiet[120];
    if (pe_imp_animation_check(a, quiet, sizeof quiet)) { pe_imp_animation_free(a); free(a); return NULL; }
    return a;
}

static int read_anims(const uint8_t *rdt, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    uint32_t stage, room, player;
    if (pe_imp_re_room_name(PE_IMP_RE1, path, &stage, &room, &player) || size < 0x48 + 19 * 4) return 0;
    PeImpAnimation *a = animation(rdt, size, le32(rdt + 0x48 + 4 * 9), le32(rdt + 0x48 + 4 * 10));
    if (!a) return 0;                                                   /* not one we read */
    char id[40], name[48];
    snprintf(id, sizeof id, "roomanim/%x%02x-%u", (unsigned)stage + 1, (unsigned)room, (unsigned)player);
    snprintf(name, sizeof name, "Room %u-%02X animations (%s)", (unsigned)stage + 1, (unsigned)room, player ? "Jill" : "Chris");
    if (pe_imp_emit(sink, PE_IMP_ANIMATION, id, name, a)) {
        if (why && n) snprintf(why, n, "couldn't catalog %s", id);
        return -1;
    }
    return 0;
}

const PeImpReader g_re1_room_anims_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re1", .platform = "ps1", .format = "rdt-anims",
    .version = 1, .produces = PE_IMP_ANIMATION, .match = "*/STAGE?/ROOM*.RDT",
    .tags = "room animation", .read = read_anims, .path_key = 1
};
