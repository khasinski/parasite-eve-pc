/* Playing Aya's rows on another skeleton (retarget.py) and the rows rooms install for her
 * (build_skin.room_player_rows). Python dicts keep insertion order and a re-assigned key keeps its
 * place; label tables below do the same, including Python's index -1 = the last element. */
#include <stdlib.h>
#include <string.h>

#include "pe_imp_pe.h"

static const PeM3 I3 = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

/* parent of each part from the command list (-1 push, -2 pop); -1 = None, -2 = not a part */
int pe_imp_parents(const PeImpMd *md, int parent[PE_IMP_PARTS_MAX]) {
    int stack[64], sp = 0, cur = -1;
    for (int i = 0; i < PE_IMP_PARTS_MAX; i++) parent[i] = -2;
    for (int i = 0; i < md->ncmds; i++) {
        int c = md->cmds[i];
        if (c == -1) { if (sp == 64) return -1; stack[sp++] = cur; continue; }
        if (c == -2) { if (!sp) return -1; cur = stack[--sp]; continue; }
        if (c < 0 || c >= PE_IMP_PARTS_MAX) return -1;
        parent[c] = cur, cur = c;
    }
    return 0;
}

typedef struct { int n; int k[PE_IMP_PARTS_MAX]; } Kids;

static int kids_of(const PeImpMd *md, Kids kids[PE_IMP_PARTS_MAX + 1]) {     /* [0] = None, [p+1] = p */
    int parent[PE_IMP_PARTS_MAX];
    if (pe_imp_parents(md, parent)) return -1;
    memset(kids, 0, sizeof(Kids) * (PE_IMP_PARTS_MAX + 1));
    for (int c = 0; c < PE_IMP_PARTS_MAX; c++)                              /* sorted(par) */
        if (parent[c] != -2) { Kids *k = &kids[parent[c] + 1]; k->k[k->n++] = c; }
    return 0;
}

int pe_imp_chains(const PeImpMd *md, PeImpChain out[CH_COUNT]) {
    Kids kids[PE_IMP_PARTS_MAX + 1];
    if (kids_of(md, kids) || kids[0].n < 1) return -1;
    int root = kids[0].k[0];
    if (kids[root + 1].n < 2) return -1;
    int chest = kids[root + 1].k[0], pelvis = kids[root + 1].k[1];
    if (kids[chest + 1].n < 3 || kids[pelvis + 1].n < 2) return -1;
    int starts[CH_COUNT] = {root, chest, pelvis, kids[chest + 1].k[0], kids[chest + 1].k[1], kids[chest + 1].k[2],
                            kids[pelvis + 1].k[0], kids[pelvis + 1].k[1]};
    for (int c = 0; c < CH_COUNT; c++) {
        PeImpChain *ch = &out[c];
        int n = starts[c];
        ch->n = 0;
        ch->p[ch->n++] = n;
        if (c <= CH_PELVIS) continue;
        while (kids[n + 1].n == 1 && ch->n < PE_IMP_PARTS_MAX) n = kids[n + 1].k[0], ch->p[ch->n++] = n;
    }
    return 0;
}

/* ---- label_chain ---- */
enum { L_LEG, L_NECK, L_SKULL, L_EXTRA, L_SHOULDER, L_UPPER, L_BETWEEN, L_FOREARM, L_HAND, L_FINGER };
enum { K_ARM, K_HEAD, K_LEG };
typedef struct { int n; int key[2 * PE_IMP_PARTS_MAX]; int name[2 * PE_IMP_PARTS_MAX], num[2 * PE_IMP_PARTS_MAX]; } Labels;

static void lab_set(Labels *l, int key, int name, int num) {
    for (int i = 0; i < l->n; i++)
        if (l->key[i] == key) { l->name[i] = name, l->num[i] = num; return; }
    l->key[l->n] = key, l->name[l->n] = name, l->num[l->n] = num, l->n++;
}

static void label_chain(const PeImpMd *md, const PeImpChain *ch, int kind, Labels *lab) {
    lab->n = 0;
    int len = ch->n;
    if (kind == K_LEG) { for (int i = 0; i < len; i++) lab_set(lab, i, L_LEG, i); return; }
    int h = 0;                                                     /* first with the most vertices */
    for (int i = 1; i < len; i++) if (md->ptab[ch->p[i]].vc > md->ptab[ch->p[h]].vc) h = i;
    if (kind == K_HEAD) {
        for (int i = 0; i < h; i++) lab_set(lab, i, L_NECK, i - h);
        lab_set(lab, h, L_SKULL, 0);
        for (int i = h + 1; i < len; i++) lab_set(lab, i, L_EXTRA, i - h);
        return;
    }
    int L[PE_IMP_PARTS_MAX];
    for (int i = 0; i < len; i++) {
        int tz = i + 1 < len ? md->ptab[ch->p[i + 1]].tz : 0;
        L[i] = tz < 0 ? -tz : tz;
    }
    int f = h - 1, u = 0;
    if (f > 0)                                                     /* (L[i], i): ties -> the later */
        for (int i = 1; i < f; i++) if (L[i] >= L[u]) u = i;
    for (int i = 0; i < u; i++) lab_set(lab, i, L_SHOULDER, i - u);
    lab_set(lab, u, L_UPPER, 0);
    for (int i = u + 1; i < f; i++) lab_set(lab, i, L_BETWEEN, i - u);
    lab_set(lab, f, L_FOREARM, 0);
    lab_set(lab, h, L_HAND, 0);
    for (int i = h + 1; i < len; i++) lab_set(lab, i, L_FINGER, i - h);
}

static int at_index(const PeImpChain *ch, int i) { return i < 0 ? ch->p[ch->n + i] : ch->p[i]; }

int pe_imp_semantic_mapping(const PeImpMd *src, const PeImpMd *dst, PeImpMap *out) {
    PeImpChain s[CH_COUNT], d[CH_COUNT];
    for (int i = 0; i < PE_IMP_PARTS_MAX; i++) out->map[i] = -1;
    if (pe_imp_chains(src, s) || pe_imp_chains(dst, d)) return -1;
    for (int k = 0; k < CH_COUNT; k++) {
        int kind = k == CH_ARMA || k == CH_ARMB ? K_ARM : k == CH_HEAD ? K_HEAD : k == CH_LEGA || k == CH_LEGB ? K_LEG : -1;
        if (kind < 0) { out->map[d[k].p[0]] = s[k].p[0]; continue; }
        Labels sl, dl;
        label_chain(src, &s[k], kind, &sl);
        label_chain(dst, &d[k], kind, &dl);
        for (int i = 0; i < dl.n; i++) {
            if (dl.name[i] == L_BETWEEN || dl.name[i] == L_EXTRA) continue;
            int found = 0, any = 0;                                /* sl as {label: i}: the last wins */
            for (int j = 0; j < sl.n; j++)
                if (sl.name[j] == dl.name[i] && sl.num[j] == dl.num[i]) found = sl.key[j], any = 1;
            if (!any) continue;
            if (dl.key[i] >= d[k].n || -dl.key[i] > d[k].n || found >= s[k].n || -found > s[k].n) continue;
            out->map[at_index(&d[k], dl.key[i])] = at_index(&s[k], found);
        }
    }
    return 0;
}

void pe_imp_straight_parts(const PeImpMd *dst, uint8_t straight[PE_IMP_PARTS_MAX]) {
    PeImpChain d[CH_COUNT];
    memset(straight, 0, PE_IMP_PARTS_MAX);
    if (pe_imp_chains(dst, d)) return;
    for (int k = CH_ARMA; k <= CH_ARMB; k += CH_ARMB - CH_ARMA) {
        Labels l;
        label_chain(dst, &d[k], K_ARM, &l);
        for (int i = 0; i < l.n; i++)
            if (l.name[i] == L_BETWEEN) straight[at_index(&d[k], l.key[i])] = 1;
    }
}

void pe_imp_world_rotations(const PeImpMd *md, const int (*ang)[3], int nang, PeM3 *out, uint8_t *has) {
    PeM3 cur, st[64];
    int sp = 0;
    memcpy(cur, I3, sizeof cur);
    memset(has, 0, PE_IMP_PARTS_MAX);
    for (int i = 0; i < md->ncmds; i++) {
        int c = md->cmds[i];
        if (c == -1) { if (sp < 64) memcpy(st[sp++], cur, sizeof cur); continue; }
        if (c == -2) { if (sp) memcpy(cur, st[--sp], sizeof cur); continue; }
        if (c < 0 || c >= PE_IMP_PARTS_MAX) continue;
        PeM3 R;
        if (c < nang) pe_imp_rot_yxz(ang[c], R);
        else memcpy(R, I3, sizeof R);
        pe_imp_mm(cur, R, cur);
        memcpy(out[c], cur, sizeof cur), has[c] = 1;
    }
}

void pe_imp_shoulder_deltas(const PeImpMd *src, const PeImpMd *dst, const PeImpMap *mp,
                            const int (*src_idle)[3], int nsrc, const int (*dst_idle)[3], int ndst, PeImpCorr *out) {
    PeM3 sw[PE_IMP_PARTS_MAX], tw[PE_IMP_PARTS_MAX];
    uint8_t shas[PE_IMP_PARTS_MAX], thas[PE_IMP_PARTS_MAX];
    PeImpChain d[CH_COUNT];
    memset(out->has, 0, sizeof out->has);
    pe_imp_world_rotations(src, src_idle, nsrc, sw, shas);
    pe_imp_world_rotations(dst, dst_idle, ndst, tw, thas);
    if (pe_imp_chains(dst, d)) return;
    for (int k = CH_ARMA; k <= CH_ARMB; k += CH_ARMB - CH_ARMA)
        for (int i = 0; i < 2 && i < d[k].n; i++) {
            int p = d[k].p[i], s = mp->map[p];
            if (s >= 0 && shas[s] && thas[p]) {
                PeM3 t;
                pe_imp_transpose(sw[s], t);
                pe_imp_mm(t, tw[p], out->m[p]);
                out->has[p] = 1;
            }
        }
}

void pe_imp_retarget_world(const PeImpMd *src, const int (*ang)[3], int nang, const PeImpMd *dst,
                           const PeImpMap *mp, const int (*idle)[3], int nidle, const PeImpCorr *corr,
                           const uint8_t *straight, int (*out)[3]) {
    PeM3 sw[PE_IMP_PARTS_MAX], tw[PE_IMP_PARTS_MAX];
    uint8_t shas[PE_IMP_PARTS_MAX];
    int parent[PE_IMP_PARTS_MAX];
    pe_imp_world_rotations(src, ang, nang, sw, shas);
    pe_imp_parents(dst, parent);
    for (int i = 0; i < dst->ncmds; i++) {
        int c = dst->cmds[i];
        if (c < 0 || c >= PE_IMP_PARTS_MAX) continue;
        const double (*pw)[3] = parent[c] >= 0 ? (const double (*)[3])tw[parent[c]] : I3;
        PeM3 local;
        int s = mp->map[c];
        if (s >= 0 && shas[s]) {
            PeM3 want, t;
            memcpy(want, sw[s], sizeof want);
            if (corr && corr->has[c]) pe_imp_mm(want, corr->m[c], want);
            pe_imp_transpose(pw, t);
            pe_imp_mm(t, want, local);
            pe_imp_to_yxz(local, out[c]);
        } else {
            if (straight && straight[c]) out[c][0] = out[c][1] = out[c][2] = 0;
            else if (c < nidle) memcpy(out[c], idle[c], sizeof out[c]);
            else out[c][0] = out[c][1] = out[c][2] = 0;
            pe_imp_rot_yxz(out[c], local);
        }
        pe_imp_mm(pw, local, tw[c]);
    }
}

/* ---- room_player_rows ---- */
#define FP_BYTES 372

static int fp_cmp(const void *a, const void *b) {
    uint32_t x = ((const PeImpRoomRow *)a)->fp, y = ((const PeImpRoomRow *)b)->fp;
    return x < y ? -1 : x > y;
}
static int u32_cmp(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

void pe_imp_room_rows_free(PeImpRoomRow *rows, int count) {
    for (int i = 0; i < count; i++) pe_imp_buf_free(&rows[i].data);
    free(rows);
}

int pe_imp_room_player_rows(PeImpPe *pe, PeImpRoomRow **rows_out, int *count_out, const PeImpProgress *progress) {
    if (pe_imp_pe_kept_rows(pe, rows_out, count_out)) return 0;     /* scanned already for this source */
    PeImpRoomRow *rows = NULL;
    int n = 0, cap = 0, rc = 0;
    for (int disc = 1; disc <= 2 && !rc; disc++) {
        for (int m = 1; m <= pe_imp_pe_rooms(pe) && !rc; m++) {
            if (m % 10 == 1) PE_IMP_PROGRESS(progress, "room animations", (disc - 1) * pe_imp_pe_rooms(pe) + m, 2 * pe_imp_pe_rooms(pe));
            PeImpBuf b;
            PeImpRec *recs;
            if (pe_imp_pe_room(pe, m, disc, &b)) continue;              /* as the bridge's except: continue */
            int nr = pe_imp_records(&b, 0x10, &recs);
            if (nr < 0) { pe_imp_buf_free(&b); continue; }
            uint32_t *offs = malloc((size_t)(nr + 1) * sizeof *offs);
            int no = 0;
            if (!offs) { rc = -1; free(recs); pe_imp_buf_free(&b); break; }
            for (int i = 0; i < nr; i++) offs[no++] = recs[i].off;
            qsort(offs, (size_t)no, sizeof *offs, u32_cmp);
            int u = 0;
            for (int i = 0; i < no; i++) if (!u || offs[u - 1] != offs[i]) offs[u++] = offs[i];
            offs[u++] = b.n;
            for (int i = 0; i < nr && !rc; i++) {
                if (recs[i].group != 0 || recs[i].slot < 24 || recs[i].slot >= 48) continue;
                uint32_t o = recs[i].off, next = 0;
                int found = 0;
                for (int k = 0; k < u; k++) if (offs[k] > o) { next = offs[k], found = 1; break; }
                if (!found) { rc = -1; break; }                     /* the bridge would raise here */
                uint32_t lo = o < b.n ? o : b.n, hi = next < b.n ? next : b.n, len;
                if (hi < lo) hi = lo;
                if (hi - lo < FP_BYTES) continue;
                if (pe_imp_row_length(b.b + lo, hi - lo, 0, &len)) { rc = -1; break; }   /* the bridge raises */
                if (len > hi - lo) len = hi - lo;                   /* data[:row_length(data)] clamps */
                uint32_t fp = pe_imp_fingerprint(b.b + lo, len);
                int dup = 0;
                for (int k = 0; k < n; k++) if (rows[k].fp == fp) { dup = 1; break; }
                if (dup) continue;                                   /* setdefault: the first wins */
                if (n == cap) {
                    cap = cap ? cap * 2 : 64;
                    PeImpRoomRow *g = realloc(rows, (size_t)cap * sizeof *g);
                    if (!g) { rc = -1; break; }
                    rows = g;
                }
                rows[n].fp = fp;
                rows[n].data.b = malloc(len ? len : 1);
                rows[n].data.n = len;
                if (!rows[n].data.b) { rc = -1; break; }
                memcpy(rows[n].data.b, b.b + lo, len);
                n++;
            }
            free(offs), free(recs);
            pe_imp_buf_free(&b);
        }
    }
    if (rc) { pe_imp_room_rows_free(rows, n); return -1; }
    if (n) qsort(rows, (size_t)n, sizeof *rows, fp_cmp);
    pe_imp_pe_keep_rows(pe, rows, n);                               /* the source keeps this copy */
    return pe_imp_pe_kept_rows(pe, rows_out, count_out) ? 0 : -1;
}
