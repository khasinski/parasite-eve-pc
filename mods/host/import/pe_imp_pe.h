#ifndef PE_IMP_PE_H
#define PE_IMP_PE_H
/*
 * The Parasite Eve side of the importer: Aya's bundles, rows and textures read from the player's
 * own PE disc images, the game's model and animation-row formats decoded and encoded, and the
 * retargeting of Aya's rows onto another skeleton.
 *
 * Ported from psxrecomp-decomp-bridge tools/pe_assets (render_models.py, retarget.py,
 * build_skin.py at 83f77cc, MIT) and kept operation for operation the same, including Python's
 * evaluation order, rounding (round half to even) and floor division, so a skin built here has
 * the same bytes as one built by tools/re2-skin (PR #4).
 */
#include <stddef.h>
#include <stdint.h>

#include "pe_imp.h"

typedef struct { uint8_t *b; uint32_t n; } PeImpBuf;             /* malloc'd bytes */
void pe_imp_buf_free(PeImpBuf *b);

/* ---- the PE source: Aya's data and the rooms ----
 * Either the player's disc images (pe_imp_pe_open: tests and development, rooms 1-999 as the
 * bridge scans them) or, in the game, its own open discs (the import mod gives these ops, rooms
 * 1-450: the game's room table). */
typedef struct PeImpPe PeImpPe;
typedef struct {
    int (*image)(void *user, int idx, PeImpBuf *out);              /* PE.IMG entry; 0 or -1 */
    int (*room)(void *user, int map, int disc, PeImpBuf *out);      /* a room's data; -1 if unreadable */
    void (*close)(void *user);
    int rooms;                                                      /* maps 1..rooms are scanned */
} PeImpPeOps;
PeImpPe *pe_imp_pe_from(const PeImpPeOps *ops, void *user);
/* disc2 may be NULL (then rooms are read from disc 1 only). */
PeImpPe *pe_imp_pe_open(const char *disc1, const char *disc2, char *why, size_t n);
void pe_imp_pe_close(PeImpPe *pe);
void pe_imp_pe_set_rooms(PeImpPe *pe, int rooms);
int pe_imp_pe_rooms(const PeImpPe *pe);
/* An entry of PE.IMG by its image index (Aya's jacket bundle is 20, her texture 15). */
int pe_imp_pe_image(PeImpPe *pe, int idx, PeImpBuf *out);
/* A room's data, as the bridge's room_blob: -1 if it can't be read (as the bridge raises). */
int pe_imp_pe_room(PeImpPe *pe, int map, int disc, PeImpBuf *out);

/* ---- blob records (SceneAssetBlob) ---- */
typedef struct { uint32_t off; uint8_t slot, group; } PeImpRec;
/* records(blob, field): 0x0C models, 0x10 rows. Returns the count (>= 0) or -1 if a word is
 * outside the blob; *recs is malloc'd. */
int pe_imp_records(const PeImpBuf *blob, uint32_t field, PeImpRec **recs);
int pe_imp_word(const PeImpBuf *b, uint32_t at, uint32_t *out);   /* -1 outside */

/* ---- models (render_models.model) ---- */
typedef struct { uint16_t vs, vc; uint8_t vis; int32_t tz; } PeImpPtab;
typedef struct { uint8_t kind; uint16_t idx[4]; } PeImpPrim;
#define PE_IMP_PARTS_MAX 64
typedef struct {
    int parts, verts, nd, ncmds;
    int pk[4];
    PeImpPtab ptab[PE_IMP_PARTS_MAX];
    int16_t (*V)[3];
    PeImpPrim *D;
    int8_t *cmds;
} PeImpMd;
int pe_imp_md_decode(const PeImpBuf *blob, uint32_t off, PeImpMd *md);
void pe_imp_md_free(PeImpMd *md);
/* model_extent: bytes of a model block (header .. last UV record) and where its UVs start */
int pe_imp_md_extent(const PeImpBuf *blob, uint32_t off, uint32_t *end, uint32_t *uv_start);

/* ---- animation rows ---- */
typedef struct { int tr[3]; int n; int ang[PE_IMP_PARTS_MAX][3]; } PeImpFrame;
int pe_imp_anim_frame(const PeImpBuf *blob, uint32_t off, int frame, PeImpFrame *out);  /* -1 outside */
int pe_imp_row_length(const uint8_t *data, uint32_t size, uint32_t off, uint32_t *len); /* -1 outside */
uint32_t pe_imp_fingerprint(const uint8_t *row, uint32_t len);
/* encode_row: per frame a root translation (doubles, truncated) and angles, flat:
 * ang[frame * parts + part][axis]; returns the row's bytes. */
int pe_imp_encode_row(int frames, const double (*tr)[3], const int (*ang)[3], int parts,
                      const uint16_t hit[4], PeImpBuf *out);
/* write_blob: model entries {raw, slot, trailing} and row entries {raw, id}. */
typedef struct { const uint8_t *raw; uint32_t n; uint8_t slot; uint32_t trailing; } PeImpBlobModel;
typedef struct { const uint8_t *raw; uint32_t n; uint8_t id; } PeImpBlobRow;
int pe_imp_write_blob(const PeImpBlobModel *m, int nm, const PeImpBlobRow *r, int nr, PeImpBuf *out);

/* ---- maths, as render_models / retarget ---- */
typedef double PeM3[3][3];
void pe_imp_rot_yxz(const int a[3], PeM3 out);
void pe_imp_mm(const PeM3 a, const PeM3 b, PeM3 out);
void pe_imp_mv(const PeM3 a, const double v[3], double out[3]);
void pe_imp_transpose(const PeM3 a, PeM3 out);
void pe_imp_to_yxz(const PeM3 m, int out[3]);
/* World (rotation, translation) of every part: pose_mats. has[c] set for parts the commands visit. */
void pe_imp_pose_mats(const PeImpMd *md, const int tr[3], const int (*ang)[3], int nang,
                      PeM3 *rot, double (*pos)[3], uint8_t *has);
/* World position of every vertex (render_models.pose); valid[i] 0 where Python has None. */
void pe_imp_pose(const PeImpMd *md, const int tr[3], const int (*ang)[3], int nang, double (*w)[3], uint8_t *valid);

/* ---- skeleton mapping (retarget.py) ---- */
typedef struct { int n; int p[PE_IMP_PARTS_MAX]; } PeImpChain;
enum { CH_ROOT, CH_CHEST, CH_PELVIS, CH_ARMA, CH_HEAD, CH_ARMB, CH_LEGA, CH_LEGB, CH_COUNT };
int pe_imp_chains(const PeImpMd *md, PeImpChain out[CH_COUNT]);        /* -1 if not a humanoid tree */
int pe_imp_parents(const PeImpMd *md, int parent[PE_IMP_PARTS_MAX]);   /* -1 = None */
typedef struct { int map[PE_IMP_PARTS_MAX]; } PeImpMap;                /* target part -> source part, -1 */
int pe_imp_semantic_mapping(const PeImpMd *src, const PeImpMd *dst, PeImpMap *out);
void pe_imp_straight_parts(const PeImpMd *dst, uint8_t straight[PE_IMP_PARTS_MAX]);
typedef struct { uint8_t has[PE_IMP_PARTS_MAX]; PeM3 m[PE_IMP_PARTS_MAX]; } PeImpCorr;
void pe_imp_world_rotations(const PeImpMd *md, const int (*ang)[3], int nang, PeM3 *out, uint8_t *has);
void pe_imp_shoulder_deltas(const PeImpMd *src, const PeImpMd *dst, const PeImpMap *mp,
                            const int (*src_idle)[3], int nsrc, const int (*dst_idle)[3], int ndst, PeImpCorr *out);
void pe_imp_retarget_world(const PeImpMd *src, const int (*ang)[3], int nang, const PeImpMd *dst,
                           const PeImpMap *mp, const int (*idle)[3], int nidle, const PeImpCorr *corr,
                           const uint8_t *straight, int (*out)[3]);

/* ---- the rows rooms install for Aya (room_player_rows), both discs, by fingerprint ---- */
typedef struct { uint32_t fp; PeImpBuf data; } PeImpRoomRow;
/* Sorted by fingerprint; a copy (free it). The scan of every room on both discs is slow, so the
 * source keeps the result: later calls on the same source copy it. */
int pe_imp_room_player_rows(PeImpPe *pe, PeImpRoomRow **rows, int *count, const PeImpProgress *progress);
/* The source's kept copy of the room rows (pe_imp_room_player_rows); 0 if none yet. */
int pe_imp_pe_kept_rows(PeImpPe *pe, PeImpRoomRow **rows, int *count);
void pe_imp_pe_keep_rows(PeImpPe *pe, PeImpRoomRow *rows, int count);   /* takes ownership */
void pe_imp_room_rows_free(PeImpRoomRow *rows, int count);

#endif
