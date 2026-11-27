/* The Animation type: check, free, and its store form.
 *
 * Store folder: anim.bin (below, little-endian) and anim.json (a summary for people: counts only;
 * the loader never trusts it).
 *   "PEIA" u32 version, bones, fps, moves
 *   moves x (frames u32)
 *   per move, per frame: s16 root[3], s16 angles[3 * bones], u16 event
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp.h"

static int fail(char *why, size_t n, const char *fmt, long a, long b) {
    if (why && n) snprintf(why, n, fmt, a, b);
    return -1;
}

void pe_imp_animation_free(PeImpAnimation *a) {
    if (!a) return;
    if (a->move)
        for (uint32_t i = 0; i < a->moves; i++) free(a->move[i].data), free(a->move[i].event);
    free(a->move);
    memset(a, 0, sizeof *a);
}

int pe_imp_animation_check(const PeImpAnimation *a, char *why, size_t n) {
    if (!a->bones || a->bones > PE_IMP_MODEL_BONES_MAX) return fail(why, n, "animation: %ld bones", (long)a->bones, 0);
    if (!a->fps || a->fps > 120) return fail(why, n, "animation: %ld frames a second", (long)a->fps, 0);
    if (!a->moves || a->moves > PE_IMP_ANIM_MOVES_MAX || !a->move) return fail(why, n, "animation: %ld moves", (long)a->moves, 0);
    for (uint32_t i = 0; i < a->moves; i++) {
        const PeImpMove *m = &a->move[i];
        if (m->frames > PE_IMP_ANIM_FRAMES_MAX) return fail(why, n, "animation: move %ld has %ld frames", (long)i, (long)m->frames);
        if (m->frames && (!m->data || !m->event)) return fail(why, n, "animation: move %ld has no frames", (long)i, 0);
    }
    return 0;
}

static size_t frame_words(const PeImpAnimation *a) { return 3 + 3 * (size_t)a->bones; }

int pe_imp_animation_save(const PeImpAnimation *a, const char *folder, char *why, size_t n) {
    if (pe_imp_animation_check(a, why, n)) return -1;
    char path[PE_IMP_PATH_MAX];
    snprintf(path, sizeof path, "%s/anim.bin", folder);
    FILE *f = fopen(path, "wb");
    if (!f) return fail(why, n, "can't write the store (anim.bin)", 0, 0);
    int bad = 0;
    uint8_t b[4];
#define W16(v) (b[0] = (uint8_t)(v), b[1] = (uint8_t)((uint16_t)(v) >> 8), bad |= fwrite(b, 1, 2, f) != 2)
#define W32(v) (b[0] = (uint8_t)(v), b[1] = (uint8_t)((v) >> 8), b[2] = (uint8_t)((v) >> 16), b[3] = (uint8_t)((v) >> 24), bad |= fwrite(b, 1, 4, f) != 4)
    bad |= fwrite("PEIA", 1, 4, f) != 4;
    W32(PE_IMP_ANIMATION_VERSION), W32(a->bones), W32(a->fps), W32(a->moves);
    uint64_t frames = 0;
    for (uint32_t i = 0; i < a->moves; i++) W32(a->move[i].frames), frames += a->move[i].frames;
    for (uint32_t i = 0; i < a->moves; i++)
        for (uint32_t k = 0; k < a->move[i].frames; k++) {
            const int16_t *d = a->move[i].data + k * frame_words(a);
            for (size_t w = 0; w < frame_words(a); w++) W16(d[w]);
            W16(a->move[i].event[k]);
        }
#undef W16
#undef W32
    bad |= fclose(f) != 0;
    if (bad) return fail(why, n, "can't write the store (anim.bin)", 0, 0);
    snprintf(path, sizeof path, "%s/anim.json", folder);
    if (!(f = fopen(path, "wb"))) return fail(why, n, "can't write the store (anim.json)", 0, 0);
    fprintf(f, "{\"type\": \"Animation\", \"version\": %d, \"bones\": %u, \"fps\": %u, \"moves\": %u, \"frames\": %llu, "
               "\"file\": \"anim.bin\"}\n",
            PE_IMP_ANIMATION_VERSION, a->bones, a->fps, a->moves, (unsigned long long)frames);
    return fclose(f) ? fail(why, n, "can't write the store (anim.json)", 0, 0) : 0;
}

static uint32_t g32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

int pe_imp_animation_load(const char *folder, PeImpAnimation *a, char *why, size_t n) {
    memset(a, 0, sizeof *a);
    char path[PE_IMP_PATH_MAX];
    snprintf(path, sizeof path, "%s/anim.bin", folder);
    FILE *f = fopen(path, "rb");
    if (!f) return fail(why, n, "the store has no anim.bin here", 0, 0);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 20 || size > (long)PE_IMP_FILE_MAX) { fclose(f); return fail(why, n, "anim.bin: %ld bytes", size, 0); }
    uint8_t *buf = malloc((size_t)size);
    size_t got = buf ? fread(buf, 1, (size_t)size, f) : 0;
    fclose(f);
    if (!buf || got != (size_t)size) { free(buf); return fail(why, n, "anim.bin: read error", 0, 0); }
    int rc = -1;
    const size_t total = (size_t)size;
    if (memcmp(buf, "PEIA", 4) || g32(buf + 4) != PE_IMP_ANIMATION_VERSION) { fail(why, n, "anim.bin: not an Animation v%ld", PE_IMP_ANIMATION_VERSION, 0); goto out; }
    a->bones = g32(buf + 8), a->fps = g32(buf + 12), a->moves = g32(buf + 16);
    if (!a->bones || a->bones > PE_IMP_MODEL_BONES_MAX || !a->moves || a->moves > PE_IMP_ANIM_MOVES_MAX ||
        total < 20 + 4 * (size_t)a->moves) { fail(why, n, "anim.bin: bad header", 0, 0); goto out; }
    if (!(a->move = calloc(a->moves, sizeof *a->move))) { fail(why, n, "out of memory", 0, 0); goto out; }
    size_t at = 20 + 4 * (size_t)a->moves, per = 2 * (frame_words(a) + 1);
    for (uint32_t i = 0; i < a->moves; i++) {
        uint32_t fr = g32(buf + 20 + 4 * i);
        if (fr > PE_IMP_ANIM_FRAMES_MAX || per * fr > total - at) { fail(why, n, "anim.bin: move %ld is cut short", (long)i, 0); goto out; }
        PeImpMove *m = &a->move[i];
        m->frames = fr;
        if (!fr) continue;
        m->data = malloc(fr * frame_words(a) * sizeof *m->data);
        m->event = malloc(fr * sizeof *m->event);
        if (!m->data || !m->event) { fail(why, n, "out of memory", 0, 0); goto out; }
        for (uint32_t k = 0; k < fr; k++) {
            for (size_t w = 0; w < frame_words(a); w++, at += 2) m->data[k * frame_words(a) + w] = (int16_t)(buf[at] | buf[at + 1] << 8);
            m->event[k] = (uint16_t)(buf[at] | buf[at + 1] << 8);
            at += 2;
        }
    }
    if (at != total) { fail(why, n, "anim.bin: wrong length", 0, 0); goto out; }
    rc = pe_imp_animation_check(a, why, n);
out:
    free(buf);
    if (rc) pe_imp_animation_free(a);
    return rc;
}
