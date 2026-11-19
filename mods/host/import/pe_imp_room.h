#ifndef PE_IMP_ROOM_H
#define PE_IMP_ROOM_H

#include "pe_imp.h"

enum { PE_IMP_RE1 = 1, PE_IMP_RE2, PE_IMP_RE3 };
#define PE_IMP_ROOM_MASK_MAX 1024u

typedef struct {
    int32_t x, y;                          /* destination on the background */
    uint16_t u, v, width, height, depth;    /* atlas coordinates and source depth */
} PeImpRoomMask;

typedef struct {
    uint32_t slot, game, projection;        /* original BSS slot and unmodified projection word */
    int32_t eye[3], target[3];              /* source coordinates, Y down */
    uint32_t mask_count;
    PeImpRoomMask masks[PE_IMP_ROOM_MASK_MAX];
} PeImpRoomView;

/* A missing RDT still produces an ImageSet and preserves the original slot numbers. */
int pe_imp_emit_re_room(PeImpSink *sink, unsigned game, const char *path,
                        const uint8_t *bss, uint32_t bss_size, const uint8_t *rdt, uint32_t rdt_size);
int pe_imp_re_room_companion(PeImpDisc *disc, const char *path, unsigned game);
/* RE3 keeps a room's RDT inside its .ARD (entry 8). 0 with the range, or -1. */
int pe_imp_re3_ard_rdt(const uint8_t *ard, uint32_t size, const uint8_t **rdt, uint32_t *rdt_size);

/* Legacy ImageSets return a slot number and game == 0. Malformed metadata returns -1. */
int pe_imp_room_view(const PeImpImageSet *set, uint32_t frame, PeImpRoomView *view);

/* Write a transparent foreground at the ImageSet dimensions, in R, G, B, A byte order.
 * Returns 0 with pixels, 1 for no atlas, or -1 for invalid data. */
int pe_imp_room_foreground(const PeImpImageSet *set, uint32_t frame, const PeImpRoomView *view, uint8_t *rgba);

/* Same foreground cutout as above. Returns the nearest raw PRI depth for
 * each pixel, or UINT16_MAX for a transparent pixel. Size: width * height. */
int pe_imp_room_foreground_depth(const PeImpImageSet *set, uint32_t frame,
                                 const PeImpRoomView *view, uint16_t *depth);

/* RE2 ground-floor collision. Read an imported COLL section, or the same SCA
 * payload from an older collision-preview ImageSet. Returns 1 if absent. */
#define PE_RE2_COLLIDER_MAX 512u
typedef struct {
    int32_t x0, z0, x1, z1;
    uint8_t shape;
} PeRe2Collider;

typedef struct {
    unsigned count;
    PeRe2Collider entries[PE_RE2_COLLIDER_MAX];
} PeRe2RoomCollision;

int pe_imp_re2_collision(const uint8_t *sca, uint32_t size, PeRe2RoomCollision *out);
int pe_imp_re2_image_collision(const PeImpImageSet *set, PeRe2RoomCollision *out);
int pe_imp_re2_blocked(const PeRe2RoomCollision *room, double x, double z, double radius);
void pe_imp_re2_slide(const PeRe2RoomCollision *room, double *x, double *z,
                      double dx, double dz, double radius);

#endif
