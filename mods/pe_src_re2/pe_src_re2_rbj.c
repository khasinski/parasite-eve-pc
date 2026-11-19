/* Resident Evil 2 room animations (RDT section 22, RBJ): the room's own moves for its people and
 * props (cutscenes, a body falling, a door being forced). Layout from IntelOrca's biohazard-utils
 * (Room/Rbj.cs, Model/Edd1.cs, Model/Emr.cs; MIT, Ted John):
 *   RBJ  u32 offset of the table, u32 count; the table: per animation u32 flags offset, u32 EDD
 *        offset. The EMR follows its flags word; the EDD runs to the next animation's flags (the
 *        last one to the table).
 *   EMR  u16 tree offset, u16 key frames offset, u16 bones, u16 key frame size. A key frame: s16
 *        x, y, z root offset, s16 x, y, z speed, then 12-bit angles x, y, z per bone, two bones in
 *        9 bytes (as RE3's, see pe_src_re3_emd.c).
 *   EDD  per move u16 frames, u16 offset (the first offset / 4 = the moves); a frame u32: low 12
 *        bits the key frame, the rest flags.
 * Each animation becomes an Animation "roomanim/<stage><room>-<n>" at 30 frames a second: per frame
 * the key frame's root offset and its angles (4096 = a full turn, as -2048..2047). Read only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp_rdt.h"
#include "pe_src_re2.h"

static uint32_t le16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }
static int16_t s16(const uint8_t *p) { uint32_t v = le16(p); return (int16_t)(v < 0x8000 ? (int)v : (int)v - 0x10000); }
static int16_t angle(unsigned a) { return (int16_t)(a < 2048 ? (int)a : (int)a - 4096); }

/* one RBJ animation (EMR at emr, EDD from edd to end, offsets inside rdt) as an Animation */
PeImpAnimation *re2_moves(const uint8_t *rdt, uint32_t size, uint32_t emr, uint32_t edd, uint32_t end, uint32_t kf_end) {
    if (emr + 8 > size || edd + 4 > end || end > size || kf_end > size) return NULL;
    uint32_t kf = emr + le16(rdt + emr + 2), bones = le16(rdt + emr + 4), kf_size = le16(rdt + emr + 6);
    if (!bones || bones > PE_IMP_MODEL_BONES_MAX || kf_size < 12 + (bones * 9 + 1) / 2 || kf > size) return NULL;
    uint32_t keys = (kf_end > kf ? kf_end - kf : 0) / kf_size;
    uint32_t moves = le16(rdt + edd + 2) / 4;
    if (!keys || !moves || moves > PE_IMP_ANIM_MOVES_MAX || edd + 4 * moves > end) return NULL;
    PeImpAnimation *a = calloc(1, sizeof *a);
    if (!a) return NULL;
    a->bones = bones, a->fps = 30, a->moves = moves;
    a->move = calloc(moves, sizeof *a->move);
    if (!a->move) { free(a); return NULL; }
    for (uint32_t mv = 0; mv < moves; mv++) {
        uint32_t frames = le16(rdt + edd + 4 * mv), at = edd + le16(rdt + edd + 4 * mv + 2);
        if (!frames || frames > PE_IMP_ANIM_FRAMES_MAX || at + 4ull * frames > end) continue;   /* left empty */
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
            for (uint32_t b = 0; b < bones; b++) {
                const uint8_t *p = k + 12 + (b >> 1) * 9;
                unsigned x, y, z;
                if (!(b & 1)) x = p[0] | (p[1] & 15u) << 8, y = p[1] >> 4 | (unsigned)p[2] << 4, z = p[3] | (p[4] & 15u) << 8;
                else x = p[4] >> 4 | (unsigned)p[5] << 4, y = p[6] | (p[7] & 15u) << 8, z = p[7] >> 4 | (unsigned)p[8] << 4;
                out[3 + 3 * b] = angle(x), out[4 + 3 * b] = angle(y), out[5 + 3 * b] = angle(z);
            }
        }
    }
    char quiet[120];
    if (pe_imp_animation_check(a, quiet, sizeof quiet)) { pe_imp_animation_free(a); free(a); return NULL; }
    return a;
}

static int read_rbj(const uint8_t *rdt, uint32_t size, const char *path, PeImpSink *sink, char *why, size_t n) {
    uint32_t stage, room, player, at, len;
    if (pe_imp_re_room_name(PE_IMP_RE2, path, &stage, &room, &player) || pe_imp_rdt_section(rdt, size, 22, &at, &len) ||
        len < 8)
        return 0;
    uint32_t table = at + le32(rdt + at), count = le32(rdt + at + 4);
    if (count > 64 || table > size || 8ull * count > size - table) return 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t flags = at + le32(rdt + table + 8 * i), edd = at + le32(rdt + table + 8 * i + 4);
        uint32_t end = i + 1 < count ? at + le32(rdt + table + 8 * (i + 1)) : table;
        PeImpAnimation *a = re2_moves(rdt, size, flags + 4, edd, end, edd);
        if (!a) continue;                                                /* not one we read */
        char id[40], name[48];
        snprintf(id, sizeof id, "roomanim/%x%02x-%u", (unsigned)stage + 1, (unsigned)room, (unsigned)i);
        snprintf(name, sizeof name, "Room %u-%02X animation %u", (unsigned)stage + 1, (unsigned)room, (unsigned)i);
        if (pe_imp_emit(sink, PE_IMP_ANIMATION, id, name, a)) {
            if (why && n) snprintf(why, n, "couldn't catalog %s", id);
            return -1;
        }
    }
    return 0;
}

const PeImpReader g_re2_rbj_ps1 = {
    .api = {PE_IMP_API_VERSION, sizeof(PeImpReader)},
    .game = "re2", .platform = "ps1", .format = "rdt-rbj",
    .version = 1, .produces = PE_IMP_ANIMATION, .match = "PL?/RDT/ROOM*.RDT",
    .tags = "room animation", .read = read_rbj, .path_key = 1
};
