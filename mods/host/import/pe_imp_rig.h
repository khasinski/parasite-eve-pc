#ifndef PE_IMP_RIG_H
#define PE_IMP_RIG_H
/* A rig profile for the rigid fit (skins): how a source game's skeleton sits on Aya's. Data owned
 * by the source game's mod (pe_src_re2 holds RE2's human rig); the fit itself knows no game.
 * Ported from tools/re2-skin/build_re2_skin.py's PARTS, SWAP, END_PIECES, FEET, TORSO, ... */
#include <stdint.h>

enum { PE_SEG_BONE, PE_SEG_MID, PE_SEG_MESH };
typedef struct { int kind, a, b; } PeImpSeg;     /* bone a; the midpoint of bones a, b; part a's mesh centre */

#define PE_RIG_PARTS 32
typedef struct PeImpRig {
    const char *name;                            /* "re2/human" */
    int parts;                                   /* source parts fitted: 0 .. parts-1 */
    struct { int bone; PeImpSeg src[2], dst[2]; } part[PE_RIG_PARTS];   /* Aya bone; source and Aya segment */
    int swap[PE_RIG_PARTS];                      /* left <-> right; -1 = itself */
    int end_piece[PE_RIG_PARTS];                 /* -2 = not an end piece; -1 = stays upright; else the part whose turn it takes */
    uint8_t foot[PE_RIG_PARTS];                  /* set level by their own rules */
    int torso[2];                                /* chest and hips, placed as one piece (-1: none) */
    PeImpSeg torso_src[2], torso_dst[2];
    int hips_part, hip_ring, hip_lift;           /* hip vertices above hip_ring (y down) are raised hip_lift */
    double slim_min;                             /* legs narrowed to Aya's width, at most this much */
    int ref_foot, ref_thigh_l, ref_thigh_r;      /* source foot and thigh bones that set the frame */
    int chest_part, bury_lids;                   /* drop the hips' and chest's hidden lids (RE2) */
} PeImpRig;

#endif
