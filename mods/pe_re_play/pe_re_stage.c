/* An imported room on the field screen. See pe_re_stage.h. Grown from the pe_rooms prototype:
 * PE's own background tiles are skipped (PE_HOOK_FIELD_BACKGROUND stage 0) and the room's picture
 * is drawn behind everything (stage 1), uploaded as a 15-bit image where PE keeps its background
 * pages (320,256)-(639,495), as two textured quads at the far end of the ordering table. The RE2
 * camera (RID) drives the native actor renderer (PE_HOOK_FIELD_CAMERA) and the foreground cutouts
 * are linked at their PRI depths. Movement (PE_HOOK_FIELD_MOVE) walks a separate position in the
 * room's coordinates against its collision (SCA). Desktop only for now. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_re_stage.h"
#include "pe_re_session.h"

#include "pe_imp.h"
#include "pe_imp_mdec.h"
#include "pe_imp_room.h"
#include "pe_mod_host.h"
#include "pe_game.h"
#include "pe1/boot_display.h"
#include "pe1/field_movement.h"
#include "pe1/geom_state.h"
#include "pe1/pad_state.h"
#include "pe1/player_entity.h"
#include "pe1/psyq_gpu.h"
#include "pe1/render_packets.h"
#include "pe1/render_prim.h"
#include "pe_hooks.h"

#define VRAM_X 320
#define VRAM_Y 256
#define PIC_W 320
#define PIC_H 240
#define FIELD_H 224
#define PIC_TOP ((PIC_H - FIELD_H) / 2)
#define PIC_SPLIT (PIC_W / 2)
#define ROOM_BACKGROUND_OT 4095
#define FIELD_UI_OT_LAST 7
#define RE_UNITS_PER_PE_UNIT 3
#define AYA_ROOM_RADIUS 280

typedef struct RoomMaskPacket {
    RenderTexturedQuad quad[2];
    uint16_t depth;
} RoomMaskPacket;

static const char *s_store;
static int s_show, s_foreground = 1, s_view;
static RenderTexturedQuad s_quads[2][2];               /* per draw slot: left and right halves */
static uint16_t *s_original_picture;
static RoomMaskPacket *s_masks;
static size_t s_mask_count;

static struct {
    GteMatrix view;
    double eye[3];
    double right_x, right_z;
    int distance;
} s_camera;

static struct {
    PeRe2RoomCollision collision;
    double x, z;
    int yaw;
    int active;
    int ghost;              /* landed where the collision has no free floor: walk freely until clear */
} s_walk;

static void free_masks(void) {
    free(s_masks);
    s_masks = NULL;
    s_mask_count = 0;
}

/* RE2 RID stores distance in the upper half of the projection word, in Q7 pixels.
 * Build a right/down/forward basis in the games' shared Y-down convention. */
static void prepare_camera(const PeImpRoomView *view) {
    s_camera.distance = 0;
    if (view->game != PE_IMP_RE2) return;
    int distance = view->projection >> 23;
    double x = (double)view->target[0] - view->eye[0];
    double y = (double)view->target[1] - view->eye[1];
    double z = (double)view->target[2] - view->eye[2];
    double level = hypot(x, z), length = hypot(level, y);
    if (!distance || !length) return;
    double rx = level ? z / level : 1.0, rz = level ? -x / level : 0.0;
    s_camera.right_x = rx;
    s_camera.right_z = rz;
    for (int i = 0; i < 3; i++) s_camera.eye[i] = view->eye[i];
    double basis[3][3] = {
        {rx, 0, rz},
        {y * rz / length, level / length, -y * rx / length},
        {x / length, y / length, z / length}
    };
    for (int row = 0; row < 3; row++)
        for (int col = 0; col < 3; col++)
            s_camera.view.m[row][col] = (s16)lround(4096 * basis[row][col]);
    s_camera.distance = distance;
}

/* Prefer the ground under the middle of the view. Try nearby points on
 * the floor to avoid spawning inside a desk, staircase, or railing. */
static int spawn_on_floor(void) {
    static const int offsets_y[] = {56, 72, 88, 40, 24, 4};
    static const int offsets_x[] = {0, -24, 24, -48, 48, -80, 80};
    const GteMatrix *m = &s_camera.view;
    for (unsigned i = 0; i < sizeof offsets_y / sizeof *offsets_y; i++) {
        double sy = (double)offsets_y[i] / s_camera.distance;
        double ray_y = (m->m[2][1] + sy * m->m[1][1]) / 4096.0;
        if (fabs(ray_y) < 0.0001) continue;
        double depth = -s_camera.eye[1] / ray_y;
        if (depth < 2200 || depth > 12000 * RE_UNITS_PER_PE_UNIT) continue;
        for (unsigned j = 0; j < sizeof offsets_x / sizeof *offsets_x; j++) {
            double sx = (double)offsets_x[j] / s_camera.distance;
            double x = s_camera.eye[0] + depth * (m->m[2][0] + sy * m->m[1][0] + sx * m->m[0][0]) / 4096.0;
            double z = s_camera.eye[2] + depth * (m->m[2][2] + sy * m->m[1][2] + sx * m->m[0][2]) / 4096.0;
            if (pe_imp_re2_blocked(&s_walk.collision, x, z, AYA_ROOM_RADIUS)) continue;
            s_walk.x = x;
            s_walk.z = z;
            s_walk.yaw = (int)lround(atan2(-s_camera.right_z, s_camera.right_x) * (2048.0 / acos(-1.0)));
            return 1;
        }
    }
    return 0;
}

static int on_move(void *ctx, void *user) {
    (void)user;
    const FieldActor *aya = ctx;
    if (!s_show || !s_walk.active || aya != g_PlayerEntity || !pe_free_field()) return 0;
    int analog = (g_PadPackets[0].header & 0xF000) == 0x7000 &&
                 (abs(g_PadPackets[0].leftX - 128) > 16 || abs(g_PadPackets[0].leftY - 128) > 16);
    if (analog || (g_FieldPadBits & (FIELD_PAD_UP | FIELD_PAD_DOWN | FIELD_PAD_LEFT | FIELD_PAD_RIGHT))) {
        /* Recover the native walk/run step in screen axes. The native pad
         * mapping gives physical Up a positive local Z step. */
        double x = ((double)aya->pos_x - aya->base_x) * RE_UNITS_PER_PE_UNIT / 65536.0;
        double z = ((double)aya->pos_z - aya->base_z) * RE_UNITS_PER_PE_UNIT / 65536.0;
        const GteMatrix *basis = &g_GeomScroll.state.position.cameraBasis;
        double right = (basis->m[0][0] * x + basis->m[2][0] * z) / 4096.0;
        double forward = (basis->m[0][2] * x + basis->m[2][2] * z) / 4096.0;
        double dx = right * s_camera.right_x - forward * s_camera.right_z;
        double dz = right * s_camera.right_z + forward * s_camera.right_x;
        if (dx || dz) {
            s_walk.yaw = (int)lround(atan2(-dx, -dz) * (2048.0 / acos(-1.0)));
            if (s_walk.ghost) {
                s_walk.x += dx, s_walk.z += dz;
                s_walk.ghost = pe_imp_re2_blocked(&s_walk.collision, s_walk.x, s_walk.z, AYA_ROOM_RADIUS);
            } else {
                pe_imp_re2_slide(&s_walk.collision, &s_walk.x, &s_walk.z, dx, dz, AYA_ROOM_RADIUS);
            }
        }
    }
    return 1;
}

static int on_camera(void *ctx, void *user) {
    (void)user;
    PeHookFieldCamera *camera = ctx;
    const FieldActor *aya = camera->player;
    if (!s_show || !s_camera.distance || !aya || !s_walk.active) return 0;
    /* Only the render changes position. The native player stays at the
     * host location for its floor, contact checks, and save data. */
    double delta[3] = {
        (s_walk.x - s_camera.eye[0]) / RE_UNITS_PER_PE_UNIT - aya->pos_x / 65536.0,
        -s_camera.eye[1] / RE_UNITS_PER_PE_UNIT - aya->pos_y / 65536.0,
        (s_walk.z - s_camera.eye[2]) / RE_UNITS_PER_PE_UNIT - aya->pos_z / 65536.0
    };
    for (int row = 0; row < 3; row++) {
        const s16 *m = s_camera.view.m[row];
        s_camera.view.t[row] = (s32)lround((m[0] * delta[0] + m[1] * delta[1] + m[2] * delta[2]) / 4096.0);
    }
    camera->player_only = 1;
    camera->player_yaw = s_walk.yaw;
    *(GteMatrix *)camera->matrix = s_camera.view;
    camera->distance = s_camera.distance;
    return 1;
}

/* The right half begins at x=480 in VRAM, on the texture page at x=448.
 * Rectangles use the same texels and crop for the backdrop and the masks. */
static void picture_quad(RenderTexturedQuad *q, int x0, int y0, int x1, int y1) {
    memset(q, 0, sizeof *q);
    q->tag.length = 9;
    q->color.bytes.r = q->color.bytes.g = q->color.bytes.b = 128;
    q->color.bytes.code = 0x2C;
    int page_offset = x0 < PIC_SPLIT ? 0 : 128;
    q->tpage = (u16)(((VRAM_X + page_offset) / 64) | (VRAM_Y / 256) << 4 | 2 << 7);
    q->x0 = q->x2 = (u16)x0;
    q->x1 = q->x3 = (u16)x1;
    q->y0 = q->y1 = (u16)(y0 - PIC_TOP);
    q->y2 = q->y3 = (u16)(y1 - PIC_TOP);
    q->u0 = q->u2 = (u8)(x0 - page_offset);
    q->u1 = q->u3 = (u8)(x1 - page_offset);
    q->v0 = q->v1 = (u8)y0;
    q->v2 = q->v3 = (u8)y1;
}

static int mask_bucket(uint16_t pri_depth) {
    /* PRI stores camera Z / 32 in RE2 units. PE sorts at camera Z / 4.
     * Leave the foreground UI buckets free and discard depths behind the backdrop. */
    int depth = (int)pri_depth * 8 / RE_UNITS_PER_PE_UNIT;
    return depth > FIELD_UI_OT_LAST ? depth : FIELD_UI_OT_LAST + 1;
}

/* Group adjacent opaque pixels at the same depth into rectangles once per view.
 * Sample the background at the cutout coordinates so the mask is seamless when
 * no actor covers it, and needs no separate VRAM pages. */
static int build_masks(const PeImpImageSet *set, const PeImpRoomView *view, RoomMaskPacket **out, size_t *count) {
    if (!s_camera.distance || !view->mask_count) return 1;
    uint16_t *depths = malloc(PIC_W * PIC_H * sizeof *depths);
    if (!depths) return -1;
    int rc = pe_imp_room_foreground_depth(set, (uint32_t)s_view, view, depths);
    size_t capacity = 0;
    if (rc) goto done;
    for (int y = PIC_TOP; y < PIC_TOP + FIELD_H; y++) {
        for (int x = 0; x < PIC_W; x++) {
            uint16_t depth = depths[y * PIC_W + x];
            int bucket = mask_bucket(depth);
            if (bucket >= ROOM_BACKGROUND_OT) continue;
            int edge = x < PIC_SPLIT ? PIC_SPLIT : PIC_W;
            int end_x = x + 1, end_y = y + 1;
            while (end_x < edge && depths[y * PIC_W + end_x] == depth) end_x++;
            size_t width = (size_t)(end_x - x) * sizeof *depths;
            while (end_y < PIC_TOP + FIELD_H && !memcmp(depths + y * PIC_W + x, depths + end_y * PIC_W + x, width))
                end_y++;
            if (*count == capacity) {
                size_t next = capacity ? capacity * 2 : 128;
                RoomMaskPacket *packets = realloc(*out, next * sizeof *packets);
                if (!packets) {
                    free(*out);
                    *out = NULL;
                    *count = 0;
                    rc = -1;
                    goto done;
                }
                *out = packets;
                capacity = next;
            }
            RoomMaskPacket *packet = &(*out)[(*count)++];
            picture_quad(&packet->quad[0], x, y, end_x, end_y);
            packet->quad[1] = packet->quad[0];
            packet->depth = (uint16_t)bucket;
            for (int row = y; row < end_y; row++) memset(depths + row * PIC_W + x, 0xff, width);
            x = end_x - 1;
        }
    }
done:
    free(depths);
    return rc;
}

int pe_re_stage_show(const char *room_id, const PeImpRoomLogic *logic, int camera, int place, double x, double z,
                     int facing, char *why, size_t n) {
    PeImpImageSet *set = NULL;
    int rc = -1;
    uint32_t size = 0;
    if (!s_store || pe_imp_catalog_imageset(s_store, room_id, &set, why, n)) {
        if (why && n && !*why) snprintf(why, n, "%s isn't imported", room_id);
        return -1;
    }
    const uint8_t *frame = pe_imp_imageset_frame(set, (uint32_t)camera, &size);
    uint8_t *rgb = malloc(PIC_W * PIC_H * 3);
    uint16_t *pix = malloc(PIC_W * PIC_H * 2);
    PeImpRoomView view;
    if (!frame) snprintf(why, n, "%s has no camera %d (%u views)", room_id, camera, set->count);
    else if (!rgb || !pix) snprintf(why, n, "out of memory");
    else if (set->width != PIC_W || set->height != PIC_H) snprintf(why, n, "not a 320 x 240 room picture");
    else if (pe_imp_room_view(set, (uint32_t)camera, &view) || view.game != PE_IMP_RE2)
        snprintf(why, n, "only RE2 cameras are drawn so far");
    else if (pe_imp_mdec_decode(frame, size, PIC_W, PIC_H, rgb, why, n)) {}
    else {
        s_view = camera;
        prepare_camera(&view);
        uint32_t coll = 0;
        const uint8_t *data = logic ? pe_imp_logic_section(logic, PE_IMP_LOGIC_COLL, NULL, &coll) : NULL;
        if (!s_camera.distance) snprintf(why, n, "camera %d has no projection", camera);
        else if ((!data || pe_imp_re2_collision(data, coll, &s_walk.collision)) &&
                 pe_imp_re2_image_collision(set, &s_walk.collision))
            snprintf(why, n, "%s has no collision", room_id);
        else if (!place && !spawn_on_floor()) snprintf(why, n, "no free floor in camera %d", camera);
        else {
            if (place) {                                     /* a door's landing point: clear of walls */
                s_walk.x = x, s_walk.z = z, s_walk.yaw = facing;
                /* The collision knows the ground floor only: upstairs a landing can be all "wall". */
                s_walk.ghost = !pe_re_free_spot(&s_walk.collision, &s_walk.x, &s_walk.z, AYA_ROOM_RADIUS, 2000);
            } else {
                s_walk.ghost = 0;
            }
            for (int i = 0; i < PIC_W * PIC_H; i++) {
                unsigned r = rgb[i * 3] >> 3, g = rgb[i * 3 + 1] >> 3, b = rgb[i * 3 + 2] >> 3;
                uint16_t v = (uint16_t)(r | g << 5 | b << 10);
                pix[i] = v ? v : 0x8000;                    /* 0 would be see-through: keep black opaque */
            }
            RECT r = {VRAM_X, VRAM_Y, PIC_W, PIC_H};
            DrawSync(0);
            if (!s_original_picture && (s_original_picture = malloc(PIC_W * PIC_H * sizeof *s_original_picture))) {
                StoreImage(&r, s_original_picture);
                DrawSync(0);
            }
            LoadImage(&r, pix);
            DrawSync(0);                                     /* retires packets for the previous view */
            free_masks();
            build_masks(set, &view, &s_masks, &s_mask_count);
            s_walk.active = 1;
            s_show = 1;
            rc = 0;
        }
    }
    free(rgb);
    free(pix);
    free(set);
    return rc;
}

static void link_quad(RenderNativeTag *entry, RenderTexturedQuad *quad) {
    quad->tag.address = entry->address;
    entry->address = (uintptr_t)quad;
}

static int on_background(void *ctx, void *user) {
    (void)user;
    PeHookFieldBackground *b = ctx;
    if (!s_show) return 0;
    if (b->stage == 0) return 1;                       /* skip the PE room's own tiles */
    if (b->slot < 0 || b->slot > 1 || !b->ordering) return 0;
    RenderNativeTag *ordering = b->ordering;
    RenderTexturedQuad *quads = s_quads[b->slot];
    picture_quad(&quads[0], 0, PIC_TOP, PIC_SPLIT, PIC_TOP + FIELD_H);
    picture_quad(&quads[1], PIC_SPLIT, PIC_TOP, PIC_W, PIC_TOP + FIELD_H);
    link_quad(&ordering[ROOM_BACKGROUND_OT], &quads[0]);
    link_quad(&ordering[ROOM_BACKGROUND_OT], &quads[1]);
    if (s_foreground)
        for (size_t i = 0; i < s_mask_count; i++) link_quad(&ordering[s_masks[i].depth], &s_masks[i].quad[b->slot]);
    return 1;
}

/* PE loaded another room of its own: its pages are already in VRAM, so just stop. */
static int on_scene(void *ctx, void *user) {
    (void)ctx, (void)user;
    if (s_masks) DrawSync(0);
    free_masks();
    free(s_original_picture);
    s_original_picture = NULL;
    s_show = s_walk.active = 0;
    return 0;
}

void pe_re_stage_hide(void) {
    if (s_original_picture) {
        RECT r = {VRAM_X, VRAM_Y, PIC_W, PIC_H};
        DrawSync(0);
        LoadImage(&r, s_original_picture);
        DrawSync(0);
        free(s_original_picture);
        s_original_picture = NULL;
    }
    free_masks();
    s_show = s_walk.active = 0;
}

int pe_re_stage_active(void) { return s_show && s_walk.active; }
void pe_re_stage_player(double *x, double *z, int *facing) { *x = s_walk.x, *z = s_walk.z, *facing = s_walk.yaw; }
void pe_re_stage_set_masks(int on) { s_foreground = on; }

void pe_re_stage_init(const char *mod_id, const char *store) {
    s_store = store;
    pe_imp_mdec_set_table(MdecDiag_ExpandDecoderTable);
    pe_hook_add(PE_HOOK_FIELD_BACKGROUND, on_background, NULL, PE_PRIORITY_LATE, mod_id);
    pe_hook_add(PE_HOOK_FIELD_CAMERA, on_camera, NULL, PE_PRIORITY_LATE, mod_id);
    pe_hook_add(PE_HOOK_FIELD_MOVE, on_move, NULL, PE_PRIORITY_LATE, mod_id);
    pe_hook_add(PE_HOOK_SCENE_START, on_scene, NULL, PE_PRIORITY_LATE, mod_id);
}

void pe_re_stage_shutdown(void) {
    s_show = s_walk.active = 0;
    free_masks();
    free(s_original_picture);
    s_original_picture = NULL;
}
