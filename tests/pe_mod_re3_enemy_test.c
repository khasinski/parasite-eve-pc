/* RE3's enemies for the importer (mods/pe_src_re3/pe_src_re3_emd.c, pe_src_re3_tones.c, and the
 * PC folder source mods/host/import/pe_imp_rofs.c).
 *   rofs      made-up archives: packing (literals and a back reference into the starting window),
 *             the encryption, two archives in a folder with a gap opened like a disc image, files
 *             read back plain and packed; a broken archive refused
 *   emd       a made-up two-bone enemy: a rigid quad, a bound triangle (one corner on bone 1, one
 *             with a second bone) moved to the model frame; two moves, an in-between step half way,
 *             a step's sound number and hit flag as events; truncated copies refused, never read past
 *   store     Model v2 (skin) and Animation saved and loaded back unchanged; a rigid model still v1
 *   tones     a made-up bank: rates from the tone table, resampled to 22050 Hz, the tone's volume
 *   scan      the made-up folder scanned into a store: re3:enemy/nemesis (the naming table), its
 *             moves and E_34's tones in the catalog and loaded back
 *   real      with PE_TEST_RE3_PC (the RE3 PC folder): EM34 read from rofs9 (16 bones, 18 parts, the
 *             walk's footsteps on frames 2 and 26, hit frames only in m7-m10), E_34's 16 tones at
 *             the rates the Python prototype found */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp.h"
#include "pe_imp_internal.h"
#include "pe_imp_rofs.h"
#include "pe_src_re3.h"

static char s_dir[1024];

/* ---- a byte builder ---- */
typedef struct { uint8_t *b; uint32_t n, cap; } Bytes;
static void put(Bytes *o, const void *p, uint32_t len) {
    if (o->n + len > o->cap) { o->cap = (o->n + len) * 2 + 64; o->b = realloc(o->b, o->cap); assert(o->b); }
    memcpy(o->b + o->n, p, len);
    o->n += len;
}
static void b8(Bytes *o, unsigned v) { uint8_t c = (uint8_t)v; put(o, &c, 1); }
static void b16(Bytes *o, unsigned v) { b8(o, v & 255), b8(o, v >> 8 & 255); }
static void b32(Bytes *o, uint32_t v) { b16(o, v & 0xffff), b16(o, v >> 16); }
static void pad(Bytes *o, uint32_t to) { while (o->n < to) b8(o, 0); }
static void set16(Bytes *o, uint32_t at, unsigned v) { o->b[at] = (uint8_t)v, o->b[at + 1] = (uint8_t)(v >> 8); }
static void set32(Bytes *o, uint32_t at, uint32_t v) { set16(o, at, v & 0xffff), set16(o, at + 2, v >> 16); }

static void write_file(const char *path, const Bytes *o) {
    FILE *f = fopen(path, "wb");
    assert(f && fwrite(o->b, 1, o->n, f) == o->n);
    fclose(f);
}

/* ---- rofs ---- */
typedef struct { uint8_t *b; uint32_t bits, cap; } Bits;
static void bit(Bits *w, int v) {
    if (w->bits / 8 >= w->cap) { w->cap = w->cap * 2 + 64; w->b = realloc(w->b, w->cap); assert(w->b); }
    if (!(w->bits & 7)) w->b[w->bits / 8] = 0;
    if (v) w->b[w->bits / 8] |= (uint8_t)(0x80 >> (w->bits & 7));
    w->bits++;
}
static void bits(Bits *w, unsigned v, int n) { for (int i = n - 1; i >= 0; i--) bit(w, v >> i & 1); }

/* "Hi" as literals, then 4 bytes from the starting window (row 0x41: "AAAA"), then "!" */
static uint32_t packed_sample(uint8_t *out) {
    Bits w = {0};
    bit(&w, 0), bits(&w, 'H', 8);
    bit(&w, 0), bits(&w, 'i', 8);
    unsigned start = 0x41 * 16, len = 4;
    bit(&w, 1), bits(&w, start >> 4 & 0xff, 8), bits(&w, (start & 15) << 4 | (len - 2), 8);
    bit(&w, 0), bits(&w, '!', 8);
    uint32_t n = (w.bits + 7) / 8;
    memcpy(out, w.b, n);
    free(w.b);
    return n;
}

typedef struct { const char *name; const uint8_t *data; uint32_t size; int packed; } RFile;

/* one archive: folder names a/b, files encrypted with key 0x1234 + i, packed ones by the caller */
static void write_rofs(const char *path, const char *a, const char *b, const RFile *files, int count) {
    Bytes o = {0};
    b32(&o, 3), b32(&o, 1);
    pad(&o, 21);
    put(&o, a, (uint32_t)strlen(a) + 1);
    uint32_t dir_at_field = o.n;
    b32(&o, 0), b32(&o, 0);
    put(&o, b, (uint32_t)strlen(b) + 1);
    pad(&o, 4096);
    uint32_t offs[8];
    for (int i = 0; i < count; i++) {
        pad(&o, (o.n + 7) & ~7u);
        offs[i] = o.n;
        uint32_t key = 0x1234 + (uint32_t)i, hoff = 16 + 8;
        b16(&o, hoff), b16(&o, 1), b32(&o, files[i].packed ? 6 + 1 : files[i].size);
        /* the id XORed with its last byte; last byte 0x5a */
        const char *id = files[i].packed ? "Hi_Comp" : "Plain__";
        for (int k = 0; k < 7; k++) b8(&o, (unsigned)(id[k] ^ 0x5a));
        b8(&o, 0x5a);                                        /* id[7] = 0x5a ^ 0x5a = 0 */
        b32(&o, key), b32(&o, files[i].size);
        uint8_t *enc = malloc(files[i].size);
        memcpy(enc, files[i].data, files[i].size);
        pe_imp_rofs_decrypt(enc, files[i].size, key);   /* XOR: the same call encrypts */
        put(&o, enc, files[i].size);
        free(enc);
    }
    pad(&o, (o.n + 7) & ~7u);
    set32(&o, dir_at_field, o.n / 8);
    b32(&o, (uint32_t)count);
    for (int i = 0; i < count; i++) {
        b32(&o, offs[i] / 8), b32(&o, files[i].packed ? 7 : files[i].size);
        put(&o, files[i].name, (uint32_t)strlen(files[i].name) + 1);
    }
    write_file(path, &o);
    free(o.b);
}

static void test_rofs(void) {
    /* the packer on its own */
    uint8_t src[64], out[64];
    uint32_t n = packed_sample(src);
    assert(pe_imp_rofs_depack(src, n, out, 7) == 7 && !memcmp(out, "HiAAAA!", 7));   /* the length stops it: padding bits */
    assert(pe_imp_rofs_depack(src, n, out, 3) == 3 && !memcmp(out, "HiA", 3));   /* bounded */
    /* encryption round trip */
    uint8_t buf[3000], copy[3000];
    for (int i = 0; i < 3000; i++) buf[i] = (uint8_t)(i * 7);
    memcpy(copy, buf, sizeof buf);
    pe_imp_rofs_decrypt(buf, sizeof buf, 99);
    assert(memcmp(buf, copy, sizeof buf));
    pe_imp_rofs_decrypt(buf, sizeof buf, 99);
    assert(!memcmp(buf, copy, sizeof buf));

    assert(pe_imp_rofs_path("C:/x/rofs12.dat") && pe_imp_rofs_path("ROFS1.DAT") && !pe_imp_rofs_path("rofs.dat") &&
           !pe_imp_rofs_path("rofs1.dat.bak") && !pe_imp_rofs_path("game.cue"));

    char path[1200], why[200];
    static const uint8_t plain[] = "plain file";
    RFile one[] = {{"A.BIN", plain, sizeof plain, 0}, {"B.BIN", src, n, 1}};
    snprintf(path, sizeof path, "%s/rofs1.dat", s_dir);
    write_rofs(path, "DATA", "ETC", one, 2);
    RFile three[] = {{"C.BIN", plain, 5, 0}};
    snprintf(path, sizeof path, "%s/rofs3.dat", s_dir);
    write_rofs(path, "ROOM", "EMD", three, 1);

    PeImpDisc *d = pe_imp_disc_open(path, why, sizeof why);         /* named by rofs3: the folder */
    assert(d);
    assert(pe_imp_disc_files(d) == 3);
    int a = pe_imp_disc_find(d, "DATA/ETC/A.BIN"), b = pe_imp_disc_find(d, "data/etc/b.bin"), c = pe_imp_disc_find(d, "ROOM/EMD/C.BIN");
    assert(a >= 0 && b >= 0 && c >= 0);
    uint8_t *data;
    uint32_t size;
    assert(!pe_imp_disc_read(d, a, &data, &size, why, sizeof why) && size == sizeof plain && !memcmp(data, plain, size));
    free(data);
    assert(!pe_imp_disc_read(d, b, &data, &size, why, sizeof why) && size == 7 && !memcmp(data, "HiAAAA!", 7));
    free(data);
    assert(!pe_imp_disc_read(d, c, &data, &size, why, sizeof why) && size == 5 && !memcmp(data, "plain", 5));
    free(data);
    assert(pe_imp_disc_sectors(d, 0, 1, buf, why, sizeof why) == -1);   /* no sectors in a PC folder */
    assert(strstr(pe_imp_disc_label(d), "(PC)"));
    pe_imp_disc_close(d);

    /* a broken archive is skipped; a folder with none readable is refused */
    snprintf(path, sizeof path, "%s/broken", s_dir);
    pe_imp_mkdirs(path);
    snprintf(path, sizeof path, "%s/broken/rofs1.dat", s_dir);
    Bytes junk = {0};
    for (int i = 0; i < 5000; i++) b8(&junk, (unsigned)i);
    write_file(path, &junk);
    free(junk.b);
    assert(!pe_imp_disc_open(path, why, sizeof why) && strstr(why, "rofs"));
    printf("rofs: packing, encryption, a two-archive folder read like a disc\n");
}

/* ---- a made-up EMD + TIM ---- */
static const int k_rel[2][3] = {{0, -800, 0}, {0, -300, 0}};

static void put_angles(Bytes *o, const unsigned a[2][3]) {   /* two bones in 9 bytes */
    unsigned x0 = a[0][0], y0 = a[0][1], z0 = a[0][2], x1 = a[1][0], y1 = a[1][1], z1 = a[1][2];
    b8(o, x0 & 255), b8(o, x0 >> 8 | (y0 & 15) << 4), b8(o, y0 >> 4);
    b8(o, z0 & 255), b8(o, z0 >> 8 | (x1 & 15) << 4), b8(o, x1 >> 4);
    b8(o, y1 & 255), b8(o, y1 >> 8 | (z1 & 15) << 4), b8(o, z1 >> 4);
}

static void build_emd(Bytes *o) {
    memset(o, 0, sizeof *o);
    uint32_t off[15] = {0};
    b32(o, 0), b32(o, 15);
    /* section 0: binding for part 0 (the first real part whose bone is listed): vertex 0 -> bone 0,
     * 1 -> bone 1, 2 -> bone 1 and half bone 0 */
    off[0] = o->n;
    b32(o, 0), b16(o, 0), b16(o, 1);
    uint32_t e = o->n;
    b16(o, 0), b16(o, 0), b16(o, 0), b16(o, 0);
    uint32_t la = o->n;
    b8(o, 0), b8(o, 1), b8(o, 0), b8(o, 0), b8(o, 0);            /* bone 0: vertex 0 */
    b8(o, 1), b8(o, 1), b8(o, 1), b8(o, 2), b8(o, 2);            /* bone 1: vertices 1, 2 */
    b8(o, 0xff);
    uint32_t lb = o->n;
    b8(o, 0), b8(o, 1), b8(o, 2), b8(o, 2), b8(o, 2), b8(o, 0xff);   /* vertex 2 also follows bone 0 */
    set16(o, e, la - e), set16(o, e + 2, lb - e);
    pad(o, (o->n + 3) & ~3u);
    /* sections 2/3: moves and the skeleton */
    off[2] = o->n;
    b16(o, 2), b16(o, 16), b16(o, 0), b16(o, 0);                 /* move 0: 2 steps at 16, first key 0 */
    b16(o, 3), b16(o, 20), b16(o, 1), b16(o, 0);                 /* move 1: 3 steps at 20, first key 1 */
    b16(o, 0x0000), b16(o, 0x0001);
    b16(o, 0x0000), b16(o, 0x0180), b16(o, 0xa801);              /* key 1, half way, key 2 + sound 10 + hit */
    pad(o, (o->n + 3) & ~3u);
    off[3] = o->n;
    uint32_t emr = o->n;
    b16(o, 0), b16(o, 0), b16(o, 2), b16(o, 17);
    for (int i = 0; i < 2; i++) for (int c = 0; c < 3; c++) b16(o, (unsigned)(uint16_t)k_rel[i][c]);
    set16(o, emr, o->n - emr);
    b16(o, 1), b16(o, 8), b16(o, 0), b16(o, 0);                  /* bone 0: one child at +8 */
    b8(o, 1);
    pad(o, (o->n + 1) & ~1u);
    set16(o, emr + 2, o->n - emr);
    static const unsigned keys[3][2][3] = {{{0, 0, 0}, {0, 0, 0}}, {{100, 4000, 0}, {0, 0, 0}}, {{300, 100, 0}, {0, 1024, 0}}};
    static const int height[3] = {-800, -780, -760};
    for (int k = 0; k < 3; k++) {
        b16(o, 0), b16(o, 0), b16(o, 0), b16(o, (unsigned)(uint16_t)height[k]);
        put_angles(o, keys[k]);
    }
    pad(o, (o->n + 3) & ~3u);
    /* section 14: two objects */
    off[14] = o->n;
    uint32_t m = o->n;
    b32(o, 0), b32(o, 2);
    uint32_t base = o->n;
    pad(o, base + 48);
    /* object 0: bound, 4 vertices (the last unused), one triangle; object 1: rigid, one quad */
    static const int v1[4][3] = {{-10, 0, 0}, {10, 0, 0}, {-10, 20, 0}, {10, 20, 0}};
    static const int v0[3][3] = {{0, 0, 5}, {5, 0, 0}, {0, 5, 0}};
    uint32_t vo1 = o->n - base;
    for (int i = 0; i < 4; i++) for (int c = 0; c < 4; c++) b16(o, c < 3 ? (unsigned)(uint16_t)v1[i][c] : 0);
    uint32_t no1 = o->n - base;
    for (int i = 0; i < 4; i++) for (int c = 0; c < 4; c++) b16(o, c == 1 ? 4096 : 0);
    uint32_t qo1 = o->n - base;
    uint8_t q[16] = {0, 0, 4, 0, 8, 0, 1, 0, 0, 8, 0, 1, 8, 8, 2, 3};   /* page 4: u + 8; clut 1 */
    put(o, q, 16);
    uint32_t vo0 = o->n - base;
    for (int i = 0; i < 4; i++) for (int c = 0; c < 4; c++) b16(o, i < 3 && c < 3 ? (unsigned)(uint16_t)v0[i][c] : 0);
    uint32_t no0 = o->n - base;
    for (int i = 0; i < 4; i++) for (int c = 0; c < 4; c++) b16(o, 0);
    uint32_t to0 = o->n - base;
    uint8_t t[12] = {1, 1, 0, 0, 2, 2, 0, 0, 3, 3, 1, 2};
    put(o, t, 12);
    uint32_t objs[2][12] = {{vo0, 0, no0, 0, 4, 0, to0, 0, 0, 0, 1, 0}, {vo1, 0, no1, 0, 4, 0, 0x7777, 0, qo1, 0, 0, 1}};
    for (int i = 0; i < 2; i++) for (int k = 0; k < 12; k++) set16(o, base + 24 * i + 2 * k, objs[i][k]);
    set32(o, m, o->n - m);
    /* the directory */
    pad(o, (o->n + 3) & ~3u);
    set32(o, 0, o->n);
    for (int i = 0; i < 15; i++) b32(o, off[i] ? off[i] : off[14]);
    /* sections without data point somewhere harmless; sets 2 and 3 must be empty */
    for (int i = 4; i < 8; i++) set32(o, o->n - 60 + 4 * (uint32_t)i, 0);
    set32(o, o->n - 60 + 4, 0);
}

static void build_tim(Bytes *o) {
    memset(o, 0, sizeof *o);
    b32(o, 0x10), b32(o, 9);
    b32(o, 12 + 2 * 256 * 2), b16(o, 0), b16(o, 0), b16(o, 256), b16(o, 2);
    for (int r = 0; r < 2; r++) for (int c = 0; c < 256; c++) b16(o, (unsigned)(c + r));
    b32(o, 12 + 16 * 4), b16(o, 0), b16(o, 0), b16(o, 8), b16(o, 4);
    for (int i = 0; i < 64; i++) b8(o, (unsigned)i);
}

static void test_emd(void) {
    Bytes emd, tim;
    build_emd(&emd);
    build_tim(&tim);
    PeImpModel m;
    PeImpAnimation a;
    char why[200];
    int rc = re3_emd_parse(emd.b, emd.n, tim.b, tim.n, &m, &a, why, sizeof why);
    if (rc) printf("emd: %s\n", why);
    assert(!rc);
    assert(m.bones == 2 && m.parent[0] == -1 && m.parent[1] == 0);
    assert(m.pos[1][1] == -1100 && m.parts == 2);
    assert(m.w == 16 && m.h == 4 && m.pal_rows == 2 && m.pal[1][5] == 6 && m.image[63] == 63);
    /* rigid quad in bone 1's frame, page byte and palette row kept */
    assert(m.part[1].quads == 1 && m.part[1].tris == 0 && !m.part[1].quad_skin && !m.part[1].tri_skin);
    const PeImpFace *q = &m.part[1].quad[0];
    assert(q->v[0][0] == -10 && q->v[3][1] == 20 && q->page == 4 && q->clut == 1 && q->uv[1][0] == 8 && q->n[0][1] == 4096);
    /* bound triangle (part 0) */
    const PeImpPart *p1 = &m.part[0];
    assert(p1->tris == 1 && p1->tri_skin);
    const PeImpFace *t = &p1->tri[0];
    /* corner 0 = vertex index r[7] = 0, corner 1 = r[10] = 1, corner 2 = r[11] = 2 */
    assert(t->v[0][2] == 5 && t->v[0][1] == -800);                /* moved to the model frame (root -800) */
    assert(p1->tri_skin[0][0][0] == 0 && p1->tri_skin[0][0][1] == PE_IMP_NO_BONE);
    assert(p1->tri_skin[0][1][0] == 1 && p1->tri_skin[0][1][1] == PE_IMP_NO_BONE);
    assert(p1->tri_skin[0][2][0] == 1 && p1->tri_skin[0][2][1] == 0);
    /* moves */
    assert(a.bones == 2 && a.fps == 30 && a.moves == 2);
    assert(a.move[0].frames == 2 && a.move[1].frames == 3);
    const int16_t *f = a.move[1].data;                           /* 9 words a frame */
    assert(f[1] == -780 && f[3] == 100 && f[4] == -96);           /* key 1: 4000 -> -96 */
    assert(f[9 + 1] == -770 && f[9 + 3] == 200);                  /* half way: height, angle */
    assert(f[9 + 4] == 2);                                        /* 4000 -> 100 the short way, through 0 */
    assert(f[18 + 3 + 3 + 1] == 1024);
    assert(a.move[1].event[0] == 0 && a.move[1].event[1] == 0 && a.move[1].event[2] == (10 | PE_IMP_EVENT_HIT));

    /* store round trip: skinned -> v2, loads back equal */
    char folder[1100];
    snprintf(folder, sizeof folder, "%s/model", s_dir);
    pe_imp_mkdirs(folder);
    assert(!pe_imp_model_save(&m, folder, why, sizeof why));
    PeImpModel back;
    assert(!pe_imp_model_load(folder, &back, why, sizeof why));
    assert(back.parts == 2 && back.part[0].tri_skin && !back.part[1].quad_skin && !back.part[1].tri_skin);
    assert(!memcmp(back.part[0].tri_skin[0], p1->tri_skin[0], sizeof p1->tri_skin[0]));
    assert(!memcmp(&back.part[0].tri[0], t, sizeof *t));
    pe_imp_model_free(&back);
    {   /* a rigid model is still written as version 1 */
        free(m.part[0].tri_skin), free(m.part[0].quad_skin);
        m.part[0].tri_skin = NULL, m.part[0].quad_skin = NULL;
        assert(!pe_imp_model_save(&m, folder, why, sizeof why));
        char path[1200];
        snprintf(path, sizeof path, "%s/model.bin", folder);
        FILE *fp = fopen(path, "rb");
        uint8_t head[8];
        assert(fp && fread(head, 1, 8, fp) == 8);
        fclose(fp);
        assert(head[4] == 1);
        assert(!pe_imp_model_load(folder, &back, why, sizeof why) && !back.part[0].tri_skin);
        pe_imp_model_free(&back);
    }
    snprintf(folder, sizeof folder, "%s/anim", s_dir);
    pe_imp_mkdirs(folder);
    assert(!pe_imp_animation_save(&a, folder, why, sizeof why));
    PeImpAnimation ab;
    assert(!pe_imp_animation_load(folder, &ab, why, sizeof why));
    assert(ab.moves == 2 && ab.move[1].frames == 3 && !memcmp(ab.move[1].data, a.move[1].data, 27 * 2) &&
           ab.move[1].event[2] == a.move[1].event[2]);
    pe_imp_animation_free(&ab);
    pe_imp_model_free(&m);
    pe_imp_animation_free(&a);

    /* truncated copies: refused, never read past (run under the sanitizer-free build: no crash) */
    for (uint32_t len = 0; len < emd.n; len += 7) {
        uint8_t *cut = malloc(len ? len : 1);
        memcpy(cut, emd.b, len);
        if (!re3_emd_parse(cut, len, tim.b, tim.n, &m, &a, why, sizeof why)) pe_imp_model_free(&m), pe_imp_animation_free(&a);
        free(cut);
    }
    for (uint32_t len = 0; len < tim.n; len += 5)
        assert(re3_emd_parse(emd.b, emd.n, tim.b, len, &m, &a, why, sizeof why) == -1);
    free(emd.b), free(tim.b);
    printf("emd: skeleton, a rigid and a bound part, moves with events, store round trip\n");
}

/* ---- tones ---- */
static void build_bank(Bytes *vh, Bytes *vb) {
    memset(vh, 0, sizeof *vh);
    memset(vb, 0, sizeof *vb);
    pad(vh, 0x80);
    pad(vh, 0x80 + 32);
    set16(vh, 0x80 + 18, 1), set16(vh, 0x80 + 20, 2), set16(vh, 0x80 + 22, 2);
    pad(vh, 0x80 + 32 + 16);
    uint8_t tone[2][32] = {{0}};
    tone[0][2] = 127, tone[0][4] = 60, tone[0][5] = 0, tone[0][22] = 1;    /* note 60 on centre 60: 44100 */
    tone[1][2] = 64, tone[1][4] = 73, tone[1][5] = 0, tone[1][22] = 2;     /* note 61 on centre 73: an octave down */
    put(vh, tone[0], 32), put(vh, tone[1], 32);
    b16(vh, 0), b16(vh, 4), b16(vh, 8);                          /* sample 1: 0-32, sample 2: 32-64 */
    for (int blk = 0; blk < 4; blk++) {
        b8(vb, 0x00), b8(vb, 0);                                 /* filter 0, shift 0: nibble * 4096 */
        for (int i = 0; i < 14; i++) b8(vb, 0x11);              /* every sample 1 -> 4096 */
    }
}

static void test_tones(void) {
    Bytes vh, vb;
    build_bank(&vh, &vb);
    char why[200];
    assert(re3_tone_count(vh.b, vh.n) == 2);
    assert(fabs(re3_tone_rate(60, 60, 0) - 44100) < 0.01 && fabs(re3_tone_rate(60, 84, 0) - 11025) < 0.01);
    PeImpSound *s0 = re3_tone_sound(vh.b, vh.n, vb.b, vb.n, 0, why, sizeof why);
    assert(s0 && s0->rate == 22050 && s0->frames == 28 && s0->pcm[3] == 4096);   /* 56 frames at 44100 */
    PeImpSound *s1 = re3_tone_sound(vh.b, vh.n, vb.b, vb.n, 1, why, sizeof why);
    assert(s1 && s1->frames == 56 && abs(s1->pcm[3] - 2064) < 4);                /* 22050: as is; volume 64/127 */
    free(s0), free(s1);
    assert(!re3_tone_sound(vh.b, vh.n, vb.b, vb.n, 2, why, sizeof why));
    assert(!re3_tone_sound(vh.b, vh.n, vb.b, 40, 1, why, sizeof why));           /* sample past the VB */
    assert(re3_tone_count(vh.b, 100) == -1);
    free(vh.b), free(vb.b);
    printf("tones: rates from the tone table, resampled, volume\n");
}

/* ---- a made-up PC folder scanned into a store ---- */
static void test_scan(void) {
    Bytes emd, tim, vh, vb;
    build_emd(&emd), build_tim(&tim), build_bank(&vh, &vb);
    char folder[1100], path[1200], store[1200], why[200];
    snprintf(folder, sizeof folder, "%s/re3pc", s_dir);
    pe_imp_mkdirs(folder);
    RFile sound[] = {{"E_34.VH", vh.b, vh.n, 0}, {"E_34.VB", vb.b, vb.n, 0}};
    snprintf(path, sizeof path, "%s/rofs7.dat", folder);
    write_rofs(path, "DATA", "SOUND", sound, 2);
    RFile models[] = {{"EM34.EMD", emd.b, emd.n, 0}, {"EM34.TIM", tim.b, tim.n, 0}};
    snprintf(path, sizeof path, "%s/rofs9.dat", folder);
    write_rofs(path, "ROOM", "EMD", models, 2);

    pe_imp_registry_reset();
    assert(!pe_imp_add_game("re3", "Resident Evil 3", "Resident Evil"));
    assert(!pe_imp_add_reader(&g_re3_emd_pc) && !pe_imp_add_reader(&g_re3_tones_pc));
    assert(!pe_imp_set_naming("re3", g_re3_naming));
    assert(!pe_imp_add_enemy("re3", "enemy/nemesis", &g_re3_nemesis));
    assert(pe_imp_add_enemy("re3", "enemy/nemesis", &g_re3_nemesis) == -1);
    assert(pe_imp_enemy_for("re3:enemy/nemesis") == &g_re3_nemesis && !pe_imp_enemy_for("re3:enemy/em35"));

    snprintf(store, sizeof store, "%s/store", s_dir);
    PeImpScan sc;
    int rc = pe_imp_scan(store, path, NULL, &sc);
    if (rc) printf("scan: %s\n", sc.last_error);
    assert(!rc && !strcmp(sc.game, "re3") && sc.refused == 0);
    PeImpAsset *as;
    int n = pe_imp_catalog_assets(store, "re3", &as), model = 0, moves = 0, tones = 0;
    for (int i = 0; i < n; i++) {
        model += !strcmp(as[i].id, "re3:enemy/nemesis") && !strcmp(as[i].type, "Model") && !strcmp(as[i].platform, "pc");
        moves += !strcmp(as[i].id, "re3:enemy/nemesis/moves") && !strcmp(as[i].type, "Animation");
        tones += !strncmp(as[i].id, "re3:sound/e_34/tone", 19) && !strcmp(as[i].type, "Sound");
    }
    free(as);
    assert(model == 1 && moves == 1 && tones == 2);
    PeImpModel m;
    PeImpAnimation a;
    PeImpSound *s;
    assert(!pe_imp_catalog_model(store, "re3:enemy/nemesis", &m, why, sizeof why) && m.part[0].tri_skin);
    assert(!pe_imp_catalog_animation(store, "re3:enemy/nemesis/moves", &a, why, sizeof why) && a.moves == 2);
    assert(pe_imp_catalog_animation(store, "re3:enemy/nemesis", &a, why, sizeof why) == -1);   /* a Model */
    assert(!pe_imp_catalog_sound(store, "re3:sound/e_34/tone01", &s, why, sizeof why) && s->frames == 56);
    free(s);
    pe_imp_model_free(&m);
    /* scanned again: nothing read twice */
    assert(!pe_imp_scan(store, path, NULL, &sc) && sc.skipped == 2);
    free(emd.b), free(tim.b), free(vh.b), free(vb.b);
    printf("scan: the made-up PC folder -> re3:enemy/nemesis, its moves, E_34's tones\n");
}

/* ---- the real RE3 PC folder ---- */
static int real(void) {
    const char *dir = getenv("PE_TEST_RE3_PC");
    if (!dir || !*dir) { printf("real: PE_TEST_RE3_PC not set, skipped\n"); return 0; }
    char path[1200], why[200];
    snprintf(path, sizeof path, "%s/rofs1.dat", dir);
    PeImpDisc *d = pe_imp_disc_open(path, why, sizeof why);
    if (!d) { printf("real: %s\n", why); assert(0); }
    int e = pe_imp_disc_find(d, "ROOM/EMD/EM34.EMD"), t = pe_imp_disc_find(d, "ROOM/EMD/EM34.TIM");
    int h = pe_imp_disc_find(d, "DATA/SOUND/E_34.VH"), b = pe_imp_disc_find(d, "DATA/SOUND/E_34.VB");
    assert(pe_imp_disc_files(d) > 3000 && e >= 0 && t >= 0 && h >= 0 && b >= 0);
    uint8_t *emd, *tim, *vh, *vb;
    uint32_t en, tn, hn, bn;
    assert(!pe_imp_disc_read(d, e, &emd, &en, why, sizeof why) && !pe_imp_disc_read(d, t, &tim, &tn, why, sizeof why));
    assert(!pe_imp_disc_read(d, h, &vh, &hn, why, sizeof why) && !pe_imp_disc_read(d, b, &vb, &bn, why, sizeof why));
    PeImpModel m;
    PeImpAnimation a;
    int rc = re3_emd_parse(emd, en, tim, tn, &m, &a, why, sizeof why);
    if (rc) printf("real: EM34: %s\n", why);
    assert(!rc);
    int skinned = 0;
    for (uint32_t p = 0; p < m.parts; p++) skinned += m.part[p].tri_skin != NULL;
    printf("real: EM34 %u bones, %u parts (%d skinned), texture %ux%u, %u moves, walk %u frames\n",
           m.bones, m.parts, skinned, m.w, m.h, a.moves, a.move[1].frames);
    assert(m.bones == 16 && m.parts == 18 && skinned == 5 && m.w == 384 && m.h == 256 && m.pal_rows == 3);
    assert(a.move[1].frames == 52 && (a.move[1].event[2] & PE_IMP_EVENT_SOUND) == 2 && (a.move[1].event[26] & PE_IMP_EVENT_SOUND) == 2);
    for (uint32_t mv = 0; mv < 33 && mv < a.moves; mv++) {
        int hit = 0;
        for (uint32_t k = 0; k < a.move[mv].frames; k++) hit |= (a.move[mv].event[k] & PE_IMP_EVENT_HIT) != 0;
        if (mv != 16) assert(hit == (mv >= 7 && mv <= 10));       /* m16's lone 0x800 frames: a grab */
    }
    pe_imp_model_free(&m);
    pe_imp_animation_free(&a);
    assert(re3_tone_count(vh, hn) == 16);
    static const int rate0[] = {11025, 15196, 11025};
    for (int k = 0; k < 16; k++) {
        PeImpSound *s = re3_tone_sound(vh, hn, vb, bn, k, why, sizeof why);
        if (!s) printf("real: tone %d: %s\n", k, why);
        assert(s && s->rate == 22050 && s->frames > 100);
        free(s);
    }
    const uint8_t *t0 = vh + 0x80 + 32 + 16 * (vh[0x80 + 18] | vh[0x80 + 19] << 8);
    for (int k = 0; k < 3; k++) assert(lround(re3_tone_rate(60 + k, t0[32 * k + 4], t0[32 * k + 5])) == rate0[k]);
    free(emd), free(tim), free(vh), free(vb);
    pe_imp_disc_close(d);
    printf("real: EM34 and E_34 from the RE3 PC folder\n");
    return 0;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *base = getenv("PE_TEST_DIR");
    snprintf(s_dir, sizeof s_dir, "%s/re3-enemy-test", base && *base ? base : ".");
    pe_imp_mkdirs(s_dir);
    test_rofs();
    test_emd();
    test_tones();
    test_scan();
    real();
    printf("ok\n");
    return 0;
}
