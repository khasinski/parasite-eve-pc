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
#include <psyz/video.h>

#include "pe_re_stage.h"
#include "../../pe_re_session.h"
#include "pe_re_box.h"

#include "pe_imp.h"
#include "pe_imp_mdec.h"
#include "pe_imp_room.h"
#include "re_encounter_rooms.h"
#include "pe_mod_host.h"
#include "pe_game.h"
#include "pe1/boot_display.h"
#include "pe1/field_movement.h"
#include "pe1/game_state.h"
#include "pe1/geom_state.h"
#include "pe1/pad_state.h"
#include "pe1/player_entity.h"
#include "pe1/psyq_gpu.h"
#include "pe1/render_packets.h"
#include "pe1/render_prim.h"
#include "pe_hooks.h"
#include "pe_model.h"
#include "pe_model_view.h"

#define VRAM_X 320
#define VRAM_Y 256
#define PIC_W 320
#define PIC_H 240
#define FIELD_H 224
#define PIC_TOP ((PIC_H - FIELD_H) / 2)
#define PIC_SPLIT (PIC_W / 2)
#define ROOM_BACKGROUND_OT 4095
#define FIELD_UI_OT_LAST 7
#define RE_PERSON_HEIGHT 3037.0  /* an RE2 person, standing, in room units (Leon's mesh, the model audit) */
#define PE_AYA_BANK 10           /* her day-1 bundle: the model measured for her height */
#define AYA_ROOM_RADIUS 280
#define FIELD_MESH_TRIANGLES 1024
#define FIELD_TEX_X 832
#define FIELD_TEX_Y 256
#define FIELD_TEX_WORDS 128
#define FIELD_TEX_HEIGHT 256
#define FIELD_CLUT_X 576
#define FIELD_CLUT_Y 496
#define FIELD_CLUT_ROWS 16
#define FIELD_IDLE_KIND 0x1f
#define FIELD_IDLE_MOVE 0
#define BACKDROP_SETTLE 10       /* frames Aya stands still before the battle's camera is fixed */
#define FIELD_TURN_RADIANS (6.283185307179586 / 4096.0)

typedef struct RoomMaskPacket {
    RenderTexturedQuad quad[2];
    uint16_t depth;
} RoomMaskPacket;

static const char *s_store;
static int s_show, s_foreground = 1, s_view, s_size = 100;
/* RE room units per PE unit: Aya is drawn at her own size, the room divided by this, so it sets her
 * size in the room. Measured from her model so she stands as tall as an RE person (pe_re_scale). */
static double s_scale, s_aya_height;
#define RE_UNITS_PER_PE_UNIT s_scale
static RenderTexturedQuad s_quads[2][2];               /* per draw slot: left and right halves */
static RenderTexturedQuad s_curtain_quads[2][2];       /* black, in front of everything */
static int s_shade = 128, s_curtain, s_hold;           /* a door's fade: picture brightness, black, no walking */
static uint16_t *s_original_picture;
static uint16_t *s_picture;                            /* the room's picture as uploaded, kept for battles */
/* The room behind a battle: the trip's fight takes place in a PE room, but its picture is
 * swapped for the imported room's and PE's own view matrix for the room's camera, anchored so
 * Aya's place when the battle begins is where she stood in the room. Every native renderer
 * (actors, effects, cursors) reads that matrix, so they agree with the picture. PE's pages
 * and matrix come back when the battle ends. */
static struct {
    int armed, on, still;                              /* still: frames she hasn't moved */
    double room[3];                                    /* where she stood, room units */
    double anchor[3];                                  /* her PE position when the battle began */
    GteMatrix saved_view;
    s32 saved_distance;
    uint16_t *saved_pages;
    RenderLightColor light, saved_light;               /* the room's light (from the shell room), the PE room's */
    s8 tint_level, saved_tint_level;
    u8 shade, light_up, light_down;                    /* Aya's lighting in the room, given to every actor */
} s_backdrop;
static RoomMaskPacket *s_masks;
static size_t s_mask_count;
static int s_placement_markers;
static int s_placement_meshes;
static int s_placement_idle;
static PeReEnemyPlacement s_placements[PE_RE_ENEMIES_MAX];
static PeReSceneActor s_scene_actors[16];
static int s_scene_count;
static int s_placement_count;
static RenderColorTrianglePacket s_marker_packets[2][PE_RE_ENEMIES_MAX][2];
static RenderColorTrianglePacket s_placeholder_packets[2][PE_RE_ENEMIES_MAX][2];
static RenderColorTrianglePacket s_mesh_packets[2][PE_RE_ENEMIES_MAX][FIELD_MESH_TRIANGLES];
typedef struct FieldTexturedTriangle {
    RenderNativeTag tag;
    RenderPacketColor color;
    uint16_t x0, y0;
    uint8_t u0, v0;
    uint16_t clut, x1, y1;
    uint8_t u1, v1;
    uint16_t page, x2, y2;
    uint8_t u2, v2;
    uint16_t pad;
} FieldTexturedTriangle;
PE1_STATIC_ASSERT(PE1_OFFSETOF(FieldTexturedTriangle, pad) == sizeof(RenderNativeTag) + 26,
                  field_textured_triangle_layout);
static FieldTexturedTriangle s_tex_packets[2][PE_RE_ENEMIES_MAX][FIELD_MESH_TRIANGLES];
static int s_texture_kind = -1, s_texture_checked;
static uint16_t s_texture_words[FIELD_TEX_WORDS * FIELD_TEX_HEIGHT];
static uint16_t s_texture_clut[FIELD_CLUT_ROWS][256];
static uint16_t s_vram_check[FIELD_TEX_WORDS * FIELD_TEX_HEIGHT];
static struct {
    PeImpModel model;
    int tried;
    int64_t foot_y;
    unsigned first_page, first_palette;
    int16_t *idle;
    unsigned idle_frames, fps;
    PeImpAnimation scene_moves;
} s_field_mesh[0x40];

static struct {
    GteMatrix view;
    double eye[3];
    double right_x, right_z;
    int distance;
} s_camera;

static struct {
    PeRe2RoomCollision collision;
    double x, z, y;         /* y: the floor's height (floor n at -1800 n) */
    int yaw;
    int active;
    int ghost;              /* landed where the collision has no free floor: walk freely until clear */
    int floor;              /* the floor whose walls she meets; ramps change it */
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

static int blocked_here(double x, double z) {
    return pe_imp_re2_blocked_on(&s_walk.collision, (unsigned)s_walk.floor, x, z, AYA_ROOM_RADIUS);
}

/* Prefer the ground under the middle of the view. Try nearby points on
 * the floor to avoid spawning inside a desk, staircase, or railing. */
static int spawn_on_floor(void) {
    static const int offsets_y[] = {56, 72, 88, 40, 24, 4};
    static const int offsets_x[] = {0, -24, 24, -48, 48, -80, 80};
    const GteMatrix *m = &s_camera.view;
    int fallback = 0;
    double fx = 0, fz = 0;
    for (unsigned i = 0; i < sizeof offsets_y / sizeof *offsets_y; i++) {
        double sy = (double)offsets_y[i] / s_camera.distance;
        double ray_y = (m->m[2][1] + sy * m->m[1][1]) / 4096.0;
        if (fabs(ray_y) < 0.0001) continue;
        double depth = (s_walk.y - s_camera.eye[1]) / ray_y;      /* where the ray meets this floor */
        if (depth < 2200 || depth > 12000 * RE_UNITS_PER_PE_UNIT) continue;
        for (unsigned j = 0; j < sizeof offsets_x / sizeof *offsets_x; j++) {
            double sx = (double)offsets_x[j] / s_camera.distance;
            double x = s_camera.eye[0] + depth * (m->m[2][0] + sy * m->m[1][0] + sx * m->m[0][0]) / 4096.0;
            double z = s_camera.eye[2] + depth * (m->m[2][2] + sy * m->m[1][2] + sx * m->m[0][2]) / 4096.0;
            if (!fallback) fallback = 1, fx = x, fz = z;
            if (blocked_here(x, z)) continue;
            s_walk.x = x;
            s_walk.z = z;
            s_walk.ghost = 0;
            s_walk.yaw = (int)lround(atan2(-s_camera.right_z, s_camera.right_x) * (2048.0 / acos(-1.0)));
            return 1;
        }
    }
    if (!fallback) return 0;
    /* No free floor in view (the collision knows the ground floor only): stand on the first floor
     * point in view and walk without collision until clear, as after a door. */
    s_walk.x = fx, s_walk.z = fz, s_walk.ghost = 1;
    s_walk.yaw = (int)lround(atan2(-s_camera.right_z, s_camera.right_x) * (2048.0 / acos(-1.0)));
    return 1;
}

static int on_move(void *ctx, void *user) {
    (void)user;
    const FieldActor *aya = ctx;
    if (!s_show || !s_walk.active || aya != g_PlayerEntity || !pe_free_field()) return 0;
    if (pe_re_box_busy() || s_hold) return 1;         /* reading a message, or a door: she stands still */
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
                s_walk.ghost = blocked_here(s_walk.x, s_walk.z);
            } else {
                pe_imp_re2_slide_on(&s_walk.collision, (unsigned)s_walk.floor, &s_walk.x, &s_walk.z, dx, dz, AYA_ROOM_RADIUS);
            }
            double ground;                             /* on a slope or stairs: their height, maybe a new floor */
            if (pe_imp_re2_ground(&s_walk.collision, s_walk.x, s_walk.z, &ground)) {
                s_walk.y = ground;
                s_walk.floor = pe_re_floor_of((int)lround(ground));
            } else {
                s_walk.y = -(double)s_walk.floor * PE_RE2_FLOOR_HEIGHT;
            }
        }
    }
    return 1;
}

static void backdrop_off(int restore) {
    if (!s_backdrop.on) return;
    if (restore) {
        GeomScrollCoordinates *c = &g_GeomScroll.state.position;
        if (c->matrixWords) *(GteMatrix *)c->matrixWords = s_backdrop.saved_view;
        if (c->projectionDistance) *c->projectionDistance = s_backdrop.saved_distance;
        c->lightTint = s_backdrop.saved_light, c->meshTintLevel = s_backdrop.saved_tint_level;
        if (s_backdrop.saved_pages) {
            RECT r = {VRAM_X, VRAM_Y, PIC_W, PIC_H};
            DrawSync(0);
            LoadImage(&r, s_backdrop.saved_pages);
            DrawSync(0);
        }
    }
    free(s_backdrop.saved_pages);
    s_backdrop.saved_pages = NULL;
    s_backdrop.on = 0;
}

static void backdrop_on(const FieldActor *aya) {
    GeomScrollCoordinates *c = &g_GeomScroll.state.position;
    RECT r = {VRAM_X, VRAM_Y, PIC_W, PIC_H};
    if (!c->matrixWords || !c->projectionDistance || !s_picture) return;
    s_backdrop.anchor[0] = aya->pos_x / 65536.0;
    s_backdrop.anchor[1] = aya->pos_y / 65536.0;
    s_backdrop.anchor[2] = aya->pos_z / 65536.0;
    s_backdrop.still = 0;
    s_backdrop.saved_view = *(GteMatrix *)c->matrixWords;
    s_backdrop.saved_distance = *c->projectionDistance;
    s_backdrop.saved_light = c->lightTint, s_backdrop.saved_tint_level = c->meshTintLevel;
    DrawSync(0);
    if ((s_backdrop.saved_pages = malloc(PIC_W * PIC_H * sizeof *s_backdrop.saved_pages)) != NULL) {
        StoreImage(&r, s_backdrop.saved_pages);
        DrawSync(0);
    }
    LoadImage(&r, s_picture);
    DrawSync(0);
    s_backdrop.on = 1;
}

/* During the battle: the room's camera as PE's own, the screen held where the picture is; the
 * actors drawn through the same view (`out`). */
static void backdrop_camera(PeHookFieldCamera *out) {
    GeomScrollCoordinates *c = &g_GeomScroll.state.position;
    GteMatrix v = s_camera.view;
    const double delta[3] = {
        (s_backdrop.room[0] - s_camera.eye[0]) / RE_UNITS_PER_PE_UNIT - s_backdrop.anchor[0],
        (s_backdrop.room[1] - s_camera.eye[1]) / RE_UNITS_PER_PE_UNIT - s_backdrop.anchor[1],
        (s_backdrop.room[2] - s_camera.eye[2]) / RE_UNITS_PER_PE_UNIT - s_backdrop.anchor[2]
    };
    for (int row = 0; row < 3; row++) {
        const s16 *m = v.m[row];
        v.t[row] = (s32)lround((m[0] * delta[0] + m[1] * delta[1] + m[2] * delta[2]) / 4096.0);
    }
    *(GteMatrix *)c->matrixWords = v;
    *(GteMatrix *)out->matrix = v;
    out->distance = s_camera.distance;
    out->player_only = 0;
    *c->projectionDistance = s_camera.distance;
    c->screenOffsetX = 160, c->screenOffsetY = 112;
    c->lightTint = s_backdrop.light, c->meshTintLevel = s_backdrop.tint_level;   /* lit as she was in the room */
    for (FieldActor *a = g_FieldActorListHead; a; a = a->next) {   /* not by the PE room's light zones */
        a->render_object.shade = s_backdrop.shade;
        a->render_object.lightPositiveY = s_backdrop.light_up;
        a->render_object.lightNegativeY = s_backdrop.light_down;
    }
}

int pe_re_stage_backdrop_arm(void) {
    if (!s_show || !s_walk.active || !s_picture || !s_camera.distance) return 0;
    backdrop_off(1);
    s_backdrop.room[0] = s_walk.x, s_backdrop.room[1] = s_walk.y, s_backdrop.room[2] = s_walk.z;
    s_backdrop.light = g_GeomScroll.state.position.lightTint;
    s_backdrop.tint_level = g_GeomScroll.state.position.meshTintLevel;
    if (g_PlayerEntity) {
        s_backdrop.shade = g_PlayerEntity->render_object.shade;
        s_backdrop.light_up = g_PlayerEntity->render_object.lightPositiveY;
        s_backdrop.light_down = g_PlayerEntity->render_object.lightNegativeY;
    }
    s_backdrop.armed = 1;
    return 1;
}

void pe_re_stage_backdrop_release(void) {
    backdrop_off(1);
    s_backdrop.armed = 0;
}

int pe_re_stage_backdrop_shown(void) { return s_backdrop.on; }

static int on_camera(void *ctx, void *user) {
    (void)user;
    PeHookFieldCamera *camera = ctx;
    const FieldActor *aya = camera->player;
    if (s_backdrop.armed) {
        const int battle = aya && pe_in_battle();
        if (battle && !s_backdrop.on) backdrop_on(aya);
        else if (!battle && s_backdrop.on) backdrop_off(1);
        if (s_backdrop.on) {
            /* PE walks her to her battle place first: follow her until she has stood still a moment */
            if (s_backdrop.still < BACKDROP_SETTLE) {
                const double x = aya->pos_x / 65536.0, y = aya->pos_y / 65536.0, z = aya->pos_z / 65536.0;
                const int moved = x != s_backdrop.anchor[0] || y != s_backdrop.anchor[1] || z != s_backdrop.anchor[2];
                s_backdrop.anchor[0] = x, s_backdrop.anchor[1] = y, s_backdrop.anchor[2] = z;
                s_backdrop.still = moved ? 0 : s_backdrop.still + 1;
            }
            backdrop_camera(camera);
            return 1;
        }
    }
    if (!s_show || !s_camera.distance || !aya || !s_walk.active) return 0;
    /* Only the render changes position. The native player stays at the
     * host location for its floor, contact checks, and save data. */
    double delta[3] = {
        (s_walk.x - s_camera.eye[0]) / RE_UNITS_PER_PE_UNIT - aya->pos_x / 65536.0,
        (s_walk.y - s_camera.eye[1]) / RE_UNITS_PER_PE_UNIT - aya->pos_y / 65536.0,
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

/* Aya's standing height in PE units, from her model's first idle frame (once). */
static double aya_height(void) {
    if (s_aya_height <= 0) {
        PeModelSource src;
        PeViewFit fit;
        memset(&src, 0, sizeof src);
        src.kind = PE_MODEL_SKIN, src.map = PE_AYA_BANK;
        const PeModel *m = pe_model_load(&src);
        s_aya_height = m && pe_model_fit(m, &fit) && fit.y1 - fit.y0 > 50 ? fit.y1 - fit.y0 : 600;
        pe_mod_log(NULL, "re play: Aya stands %.0f PE units tall", s_aya_height);
    }
    return s_aya_height;
}

static void pe_re_scale(void) {
    double k = RE_PERSON_HEIGHT / aya_height() * 100.0 / (s_size > 0 ? s_size : 100);
    s_scale = k < 1 ? 1 : k > 40 ? 40 : k;
}

int pe_re_stage_show(const char *room_id, const PeImpRoomLogic *logic, int camera, int floor, int place, double x,
                     double z, int facing, char *why, size_t n) {
    PeImpImageSet *set = NULL;
    pe_re_scale();
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
        else if ((!data || pe_imp_re2_collision_all(data, coll, &s_walk.collision)) &&
                 pe_imp_re2_image_collision(set, &s_walk.collision))
            snprintf(why, n, "%s has no collision", room_id);
        else if (!place && !spawn_on_floor()) snprintf(why, n, "no free floor in camera %d", camera);
        else {
            s_walk.floor = floor;
            s_walk.y = -(double)floor * PE_RE2_FLOOR_HEIGHT;
            if (place) {                                     /* a door's landing point: clear of walls */
                s_walk.x = x, s_walk.z = z, s_walk.yaw = facing;
                /* The collision knows the ground floor only: upstairs a landing can be all "wall". */
                s_walk.ghost = !pe_re_free_spot(&s_walk.collision, floor, &s_walk.x, &s_walk.z, AYA_ROOM_RADIUS, 2000);
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
            free(s_picture);
            s_picture = pix, pix = NULL;                     /* kept for a battle backdrop */
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

static int project_field_point(double x, double y, double z, double screen[3]) {
    const double relative[] = {x - s_camera.eye[0], y - s_camera.eye[1], z - s_camera.eye[2]};
    double view[3] = {0};
    for (int row = 0; row < 3; row++)
        for (int col = 0; col < 3; col++)
            view[row] += s_camera.view.m[row][col] * relative[col] / 4096.0;
    const double depth = view[2] / (4 * RE_UNITS_PER_PE_UNIT);
    if (depth < 1 || depth >= ROOM_BACKGROUND_OT) return 0;
    screen[0] = PIC_W / 2 + s_camera.distance * view[0] / view[2];
    screen[1] = FIELD_H / 2 + s_camera.distance * view[1] / view[2];
    screen[2] = depth;
    return fabs(screen[0]) < 1024 && fabs(screen[1]) < 1024;
}

static void free_field_mesh(unsigned kind) {
    pe_imp_model_free(&s_field_mesh[kind].model);
    pe_imp_animation_free(&s_field_mesh[kind].scene_moves);
    free(s_field_mesh[kind].idle);
    memset(&s_field_mesh[kind], 0, sizeof s_field_mesh[kind]);
}

static void clear_field_texture(void) {
    if (s_texture_kind >= 0) {
        const RECT areas[] = {{FIELD_TEX_X, FIELD_TEX_Y, FIELD_TEX_WORDS, FIELD_TEX_HEIGHT},
                              {FIELD_CLUT_X, FIELD_CLUT_Y, 256, FIELD_CLUT_ROWS}};
        const uint16_t *pixels[] = {s_texture_words, &s_texture_clut[0][0]};
        for (int i = 0; i < 2; i++) {
            const size_t bytes = (size_t)areas[i].w * areas[i].h * sizeof(uint16_t);
            RECT rect = areas[i];
            DrawSync(0);
            StoreImage(&rect, s_vram_check);
            DrawSync(0);
            /* Scene loading may have assigned this rectangle to native graphics already. */
            if (memcmp(s_vram_check, pixels[i], bytes) != 0) continue;
            memset(s_vram_check, 0, bytes);
            LoadImage(&rect, s_vram_check);
            DrawSync(0);
        }
    }
    s_texture_kind = -1;
    s_texture_checked = 0;
}

static int empty_texture_area(RECT *rect) {
    DrawSync(0);
    StoreImage(rect, s_vram_check);
    DrawSync(0);
    for (int i = 0; i < rect->w * rect->h; i++)
        if (s_vram_check[i]) return 0;
    return 1;
}

static int show_placement_mesh(const PeReEnemyPlacement *p) {
    return p->kind < 0x40 && (pe_re_scene_kind(p->kind) || s_placement_meshes == PE_RE_MESH_ALL ||
           (s_placement_meshes == PE_RE_MESH_ENCOUNTERS && pe_re2_proximity_enemy(p, 0)));
}

static void prepare_field_texture(void) {
    if (s_texture_checked) return;
    s_texture_checked = 1;
    /* Pick one compatible model. All placements using it share a single atlas upload. */
    int kind = -1;
    for (int i = 0; i < s_placement_count; i++) {
        const unsigned candidate = s_placements[i].kind;
        if (!show_placement_mesh(&s_placements[i])) continue;
        const PeImpModel *model = &s_field_mesh[candidate].model;
        if (model->parts && model->h <= FIELD_TEX_HEIGHT && model->pal_rows <= FIELD_CLUT_ROWS) {
            kind = (int)candidate;
            break;
        }
    }
    if (kind < 0) return;
    RECT texture = {FIELD_TEX_X, FIELD_TEX_Y, FIELD_TEX_WORDS, FIELD_TEX_HEIGHT};
    RECT clut = {FIELD_CLUT_X, FIELD_CLUT_Y, 256, FIELD_CLUT_ROWS};
    if (!empty_texture_area(&texture) || !empty_texture_area(&clut)) return;
    const PeImpModel *model = &s_field_mesh[kind].model;
    const unsigned width = model->w < 2 * FIELD_TEX_WORDS ? model->w : 2 * FIELD_TEX_WORDS;
    memset(s_texture_words, 0, sizeof s_texture_words);
    memset(s_texture_clut, 0, sizeof s_texture_clut);
    for (unsigned y = 0; y < model->h; y++)
        for (unsigned x = 0; x < width; x++)
            s_texture_words[y * FIELD_TEX_WORDS + x / 2] |= (uint16_t)model->image[y * model->w + x] << (8 * (x & 1));
    memcpy(s_texture_clut, model->pal, model->pal_rows * sizeof s_texture_clut[0]);
    LoadImage(&texture, s_texture_words);
    LoadImage(&clut, &s_texture_clut[0][0]);
    DrawSync(0);
    s_texture_kind = kind;
}

static void load_field_idle(unsigned kind, const PeImpModel *model) {
    const int scene = pe_re_scene_kind(kind);
    if (!scene && kind != FIELD_IDLE_KIND) return;
    for (uint32_t b = 0; b < model->bones; b++)
        if (model->parent[b] >= (int)b || (b != 0 && model->parent[b] < 0)) return;
    for (uint32_t p = 0; p < model->parts && p < model->bones; p++)
        if (model->part[p].tri_skin || model->part[p].quad_skin) return;
    char id[PE_IMP_ID_MAX], why[160];
    PeImpAnimation animation = {0};
    snprintf(id, sizeof id, "re2:enemy/em%02x/moves", kind);
    if (pe_imp_catalog_animation(s_store, id, &animation, why, sizeof why)) return;
    if (scene && animation.bones == model->bones) {
        s_field_mesh[kind].scene_moves = animation;
        return;
    }
    if (animation.bones == model->bones && animation.moves > FIELD_IDLE_MOVE) {
        PeImpMove *idle = &animation.move[FIELD_IDLE_MOVE];
        s_field_mesh[kind].idle = idle->data;
        s_field_mesh[kind].idle_frames = idle->frames;
        s_field_mesh[kind].fps = animation.fps;
        idle->data = NULL;
    }
    pe_imp_animation_free(&animation);
}

static void load_field_meshes(void) {
    int wanted[0x40] = {0};
    for (int i = 0; i < s_placement_count; i++) {
        const unsigned kind = s_placements[i].kind;
        if (show_placement_mesh(&s_placements[i])) wanted[kind] = 1;
    }
    for (unsigned kind = 0; kind < 0x40; kind++) {
        PeImpModel *model = &s_field_mesh[kind].model;
        if (!wanted[kind]) {
            if (s_field_mesh[kind].tried) free_field_mesh(kind);
            continue;
        }
        if (s_field_mesh[kind].tried) continue;
        s_field_mesh[kind].tried = 1;
        char id[PE_IMP_ID_MAX], why[160] = "";
        snprintf(id, sizeof id, "re2:enemy/em%02x", kind);
        if (pe_imp_catalog_model(s_store, id, model, why, sizeof why)) continue;
        int64_t foot = INT64_MIN;
        unsigned first_page = 16, first_palette = 512;
        for (uint32_t b = 0; b < model->parts && b < model->bones; b++) {
            const PeImpPart *part = &model->part[b];
            for (int quad = 0; quad < 2; quad++) {
                const PeImpFace *faces = quad ? part->quad : part->tri;
                const uint32_t count = quad ? part->quads : part->tris;
                const int offset = (quad ? part->quad_skin : part->tri_skin) ? 0 : model->pos[b][1];
                for (uint32_t i = 0; i < count; i++) {
                    const unsigned page = faces[i].page & 15u, palette = faces[i].clut >> 6;
                    if (page < first_page) first_page = page;
                    if (palette < first_palette) first_palette = palette;
                    for (int v = 0; v < 3 + quad; v++) {
                        const int64_t y = (int64_t)faces[i].v[v][1] + offset;
                        if (y > foot) foot = y;
                    }
                }
            }
        }
        s_field_mesh[kind].foot_y = foot == INT64_MIN ? 0 : foot;
        s_field_mesh[kind].first_page = first_page;
        s_field_mesh[kind].first_palette = first_palette;
        load_field_idle(kind, model);
    }
}

static int on_import_published(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    /* Placements are independent of imported models. Replace the renderer's cache only. */
    clear_field_texture();
    for (unsigned kind = 0; kind < 0x40; kind++)
        if (s_field_mesh[kind].tried) free_field_mesh(kind);
    load_field_meshes();
    return 0;
}

typedef struct FieldBonePose {
    double r[3][3], t[3];
} FieldBonePose;

static void field_pose(const PeImpModel *model, const int16_t *frame, const int16_t *first,
                       const PeReSceneActor *scene, FieldBonePose *pose) {
    for (uint32_t b = 0; b < model->bones; b++) {
        const int16_t *angle = frame + 3 + 3 * b;
        const double sx = sin(angle[0] * FIELD_TURN_RADIANS), cx = cos(angle[0] * FIELD_TURN_RADIANS);
        const double sy = sin(angle[1] * FIELD_TURN_RADIANS), cy = cos(angle[1] * FIELD_TURN_RADIANS);
        const double sz = sin(angle[2] * FIELD_TURN_RADIANS), cz = cos(angle[2] * FIELD_TURN_RADIANS);
        const double rotation[3][3] = {
            {cy * cz, -cy * sz, sy},
            {sx * sy * cz + cx * sz, cx * cz - sx * sy * sz, -sx * cy},
            {sx * sz - cx * sy * cz, cx * sy * sz + sx * cz, cx * cy}
        };
        for (int r = 0; r < 3; r++) {
            if (b == 0) {
                memcpy(pose[b].r[r], rotation[r], sizeof rotation[r]);
                /* The placement already supplies the root's position. Keep only its idle motion. */
                pose[b].t[r] = scene ? frame[r] : model->pos[0][r] + (double)frame[r] - first[r];
                continue;
            }
            const FieldBonePose *parent = &pose[model->parent[b]];
            pose[b].t[r] = parent->t[r];
            for (int c = 0; c < 3; c++) {
                int offset = model->rel[b][c];
                if (scene && scene->kind == 0x3f && c == 1) {
                    const int part = b == 1 ? 0 : b == 2 ? 1 : b >= 4 && b <= 10 && !(b & 1) ? 2 : -1;
                    if (part >= 0) offset = (scene->absolute_parts ? 0 : offset) + scene->part_y[part];
                }
                pose[b].t[r] += parent->r[r][c] * offset;
                pose[b].r[r][c] = parent->r[r][0] * rotation[0][c] + parent->r[r][1] * rotation[1][c] +
                                  parent->r[r][2] * rotation[2][c];
            }
        }
    }
}

static unsigned face_colour(const PeImpModel *model, const PeImpFace *face, const int corners[3],
                            unsigned first_page, unsigned first_palette) {
    unsigned sum[3] = {0}, pixels = 0;
    const unsigned page = (face->page & 15u) - first_page, palette = (face->clut >> 6) - first_palette;
    if (palette >= model->pal_rows) return 0;
    for (int i = 0; i < 3; i++) {
        const unsigned u = face->uv[corners[i]][0] + 128u * page;
        const unsigned v = face->uv[corners[i]][1];
        if (u >= model->w || v >= model->h) continue;
        const unsigned pixel = model->pal[palette][model->image[v * model->w + u]];
        if (!pixel) continue;
        for (int c = 0; c < 3; c++) sum[c] += (pixel >> (5 * c)) & 31;
        pixels++;
    }
    if (!pixels) return 0;
    return 0x10000u | (sum[0] / pixels) | (sum[1] / pixels) << 5 | (sum[2] / pixels) << 10;
}

static int field_face_texture(const PeImpModel *model, const PeImpFace *face, const int corners[3],
                              unsigned first_page, unsigned first_palette, unsigned *page, unsigned *clut) {
    const unsigned column = (face->page & 15u) - first_page;
    const unsigned palette = (face->clut >> 6) - first_palette;
    if (column >= FIELD_TEX_WORDS / 64 || palette >= model->pal_rows) return 0;
    for (int i = 0; i < 3; i++) {
        const unsigned u = face->uv[corners[i]][0], v = face->uv[corners[i]][1];
        if (column * 128 + u >= 2 * FIELD_TEX_WORDS || column * 128 + u >= model->w || v >= model->h)
            return 0;
    }
    *page = 0x80u | (FIELD_TEX_Y / 256) << 4 | (FIELD_TEX_X / 64 + column);
    *clut = (FIELD_CLUT_Y + palette) << 6 | (FIELD_CLUT_X / 16);
    return 1;
}

static void draw_placement_meshes(RenderNativeTag *ordering, int slot, unsigned shade) {
    static const int split[2][3] = {{0, 1, 2}, {1, 3, 2}};
    const uint64_t vblanks = (uint32_t)Psyz_VideoVSync(-1);
    for (int i = 0; i < s_placement_count; i++) {
        const PeReEnemyPlacement *p = &s_placements[i];
        if (!show_placement_mesh(p)) continue;
        const PeReSceneActor *scene = NULL;
        if (pe_re_scene_kind(p->kind)) {
            for (int j = 0; j < s_scene_count; j++)
                if (s_scene_actors[j].slot == p->slot && s_scene_actors[j].kind == p->kind) {
                    scene = &s_scene_actors[j];
                    break;
                }
            if (!scene) continue;
        }
        const PeImpModel *model = &s_field_mesh[p->kind].model;
        const double world_x = scene ? scene->x : p->x;
        const double world_y = scene ? scene->y : p->y - (double)s_field_mesh[p->kind].foot_y;
        const double world_z = scene ? scene->z : p->z;
        const double angle = (scene ? scene->dir : pe_re_facing_of(p->dir)) * FIELD_TURN_RADIANS;
        const double cs = cos(angle), sn = sin(angle);
        FieldBonePose pose[PE_IMP_MODEL_BONES_MAX];
        const int16_t *idle = s_field_mesh[p->kind].idle;
        const unsigned frames = s_field_mesh[p->kind].idle_frames;
        int animated = 0;
        if (scene) {
            const PeImpAnimation *moves = &s_field_mesh[p->kind].scene_moves;
            if (scene->move < moves->moves) {
                const PeImpMove *move = &moves->move[scene->move];
                if (move->frames) {
                    const unsigned frame = scene->frame < move->frames ? scene->frame : move->frames - 1;
                    field_pose(model, move->data + frame * (3u + 3u * model->bones), NULL, scene, pose);
                    animated = 1;
                }
            }
        } else if (s_placement_idle && idle && frames) {
            const unsigned frame = (vblanks * s_field_mesh[p->kind].fps / 60 + 7u * p->slot) % frames;
            field_pose(model, idle + frame * (3u + 3u * model->bones), idle, NULL, pose);
            animated = 1;
        }
        int emitted = 0;
        for (uint32_t b = 0; b < model->parts && b < model->bones; b++) {
            const PeImpPart *part = &model->part[b];
            static const int32_t zero[3] = {0};
            for (int quad = 0; quad < 2; quad++) {
                const PeImpFace *faces = quad ? part->quad : part->tri;
                const uint32_t count = quad ? part->quads : part->tris;
                const int32_t *offset = (quad ? part->quad_skin : part->tri_skin) ? zero : model->pos[b];
                for (uint32_t j = 0; j < count && emitted < FIELD_MESH_TRIANGLES; j++) {
                    const PeImpFace *f = &faces[j];
                    double screen[4][3];
                    int visible = 0;
                    for (int k = 0; k < 3 + quad; k++) {
                        double mesh[3];
                        for (int a = 0; a < 3; a++)
                            mesh[a] = animated ? pose[b].t[a] + pose[b].r[a][0] * f->v[k][0] +
                                      pose[b].r[a][1] * f->v[k][1] + pose[b].r[a][2] * f->v[k][2] :
                                      (double)f->v[k][a] + offset[a];
                        const double x = mesh[0], z = mesh[2];
                        if (project_field_point(world_x + cs * x + sn * z, world_y + mesh[1],
                                                world_z - sn * x + cs * z, screen[k])) visible |= 1 << k;
                    }
                    for (int t = 0; t <= quad && emitted < FIELD_MESH_TRIANGLES; t++) {
                        const int *v = split[t];
                        if ((visible & (t ? 14 : 7)) != (t ? 14 : 7)) continue;
                        const double *a = screen[v[0]], *d = screen[v[1]], *c = screen[v[2]];
                        if ((a[0] < 0 && d[0] < 0 && c[0] < 0) || (a[0] >= PIC_W && d[0] >= PIC_W && c[0] >= PIC_W) ||
                            (a[1] < 0 && d[1] < 0 && c[1] < 0) || (a[1] >= FIELD_H && d[1] >= FIELD_H && c[1] >= FIELD_H)) continue;
                        const int depth = (int)((a[2] + d[2] + c[2]) / 3);
                        const int bucket = depth <= FIELD_UI_OT_LAST ? FIELD_UI_OT_LAST + 1 : depth;
                        unsigned page, clut;
                        const unsigned first_page = s_field_mesh[p->kind].first_page;
                        const unsigned first_palette = s_field_mesh[p->kind].first_palette;
                        if (s_texture_kind == p->kind &&
                            field_face_texture(model, f, v, first_page, first_palette, &page, &clut)) {
                            FieldTexturedTriangle *packet = &s_tex_packets[slot][i][emitted++];
                            *packet = (FieldTexturedTriangle){
                                .color.bytes = {shade, shade, shade, 0x24},
                                .x0 = (uint16_t)(int)lround(a[0]), .y0 = (uint16_t)(int)lround(a[1]),
                                .x1 = (uint16_t)(int)lround(d[0]), .y1 = (uint16_t)(int)lround(d[1]),
                                .x2 = (uint16_t)(int)lround(c[0]), .y2 = (uint16_t)(int)lround(c[1]),
                                .u0 = f->uv[v[0]][0], .v0 = f->uv[v[0]][1], .clut = clut,
                                .u1 = f->uv[v[1]][0], .v1 = f->uv[v[1]][1], .page = page,
                                .u2 = f->uv[v[2]][0], .v2 = f->uv[v[2]][1]
                            };
                            packet->tag.length = 7;
                            packet->tag.address = ordering[bucket].address;
                            ordering[bucket].address = (uintptr_t)packet;
                            continue;
                        }
                        const unsigned rgb = face_colour(model, f, v, first_page, first_palette);
                        if (!rgb) continue;
                        RenderColorTrianglePacket *packet = &s_mesh_packets[slot][i][emitted++];
                        *packet = (RenderColorTrianglePacket){
                            .color.bytes = {(rgb & 31) * 8 * shade / 128, ((rgb >> 5) & 31) * 8 * shade / 128,
                                            ((rgb >> 10) & 31) * 8 * shade / 128, 0x20},
                            .x0 = (uint16_t)(int)lround(a[0]), .y0 = (uint16_t)(int)lround(a[1]),
                            .x1 = (uint16_t)(int)lround(d[0]), .y1 = (uint16_t)(int)lround(d[1]),
                            .x2 = (uint16_t)(int)lround(c[0]), .y2 = (uint16_t)(int)lround(c[1])
                        };
                        packet->tag.length = 4;
                        packet->tag.address = ordering[bucket].address;
                        ordering[bucket].address = (uintptr_t)packet;
                    }
                }
            }
        }
    }
}

/* Project a placement with the same fixed camera and ordering depth as the field actors. */
static void link_field_triangle(RenderNativeTag *entry, RenderColorTrianglePacket *packet) {
    packet->tag.length = 4;
    packet->tag.address = entry->address;
    entry->address = (uintptr_t)packet;
}

static void draw_placement_markers(RenderNativeTag *ordering, int slot, unsigned shade) {
    for (int i = 0; i < s_placement_count; i++) {
        const PeReEnemyPlacement *p = &s_placements[i];
        if (p->kind >= 0x40) continue;     /* people are outside encounter placement testing */
        double screen[3];
        if (!project_field_point(p->x, p->y, p->z, screen)) continue;
        const double x = screen[0], y = screen[1], depth = screen[2];
        if (x < 5 || x >= PIC_W - 5 || y < 5 || y >= FIELD_H - 5) continue;
        const int sx = (int)lround(x), sy = (int)lround(y);
        const int bucket = depth <= FIELD_UI_OT_LAST ? FIELD_UI_OT_LAST + 1 : (int)depth;
        if (s_placement_meshes && show_placement_mesh(p) && !s_field_mesh[p->kind].model.parts) {
            double head[3];
            if (project_field_point(p->x, p->y - 2000, p->z, head)) {
                const int height = (int)lround(y - head[1]);
                if (height > 4) {
                    const int width = height < 64 ? 3 + height / 5 : 15;
                    RenderColorTrianglePacket *body = s_placeholder_packets[slot][i];
                    body[0] = (RenderColorTrianglePacket){
                        .color.bytes = {240 * shade / 128, 160 * shade / 128, 48 * shade / 128, 0x20},
                        .x0 = sx, .y0 = sy - height, .x1 = sx - width, .y1 = sy - height / 2,
                        .x2 = sx + width, .y2 = sy - height / 2
                    };
                    body[1] = body[0];
                    body[1].color.bytes.r /= 2;
                    body[1].color.bytes.g /= 2;
                    body[1].y0 = sy;
                    link_field_triangle(&ordering[bucket], &body[0]);
                    link_field_triangle(&ordering[bucket], &body[1]);
                }
            }
        }
        if (!s_placement_markers) continue;
        RenderColorTrianglePacket *top = &s_marker_packets[slot][i][0];
        RenderColorTrianglePacket *bottom = &s_marker_packets[slot][i][1];
        *top = (RenderColorTrianglePacket){
            .color.bytes = {255 * shade / 128, 40 * shade / 128, 195 * shade / 128, 0x20},
            .x0 = sx - 5, .y0 = sy, .x1 = sx, .y1 = sy - 4, .x2 = sx + 5, .y2 = sy
        };
        *bottom = *top;
        bottom->y1 = sy + 4;
        link_field_triangle(&ordering[bucket], bottom);
        link_field_triangle(&ordering[bucket], top);
    }
}

static int on_background(void *ctx, void *user) {
    (void)user;
    PeHookFieldBackground *b = ctx;
    if (s_backdrop.on) {                               /* a battle in front of the room's picture */
        if (b->stage == 0) return 1;
        if (b->slot < 0 || b->slot > 1 || !b->ordering) return 0;
        RenderTexturedQuad *quads = s_quads[b->slot];
        picture_quad(&quads[0], 0, PIC_TOP, PIC_SPLIT, PIC_TOP + FIELD_H);
        picture_quad(&quads[1], PIC_SPLIT, PIC_TOP, PIC_W, PIC_TOP + FIELD_H);
        for (int k = 0; k < 2; k++) quads[k].color.bytes.r = quads[k].color.bytes.g = quads[k].color.bytes.b = 128;
        link_quad(&((RenderNativeTag *)b->ordering)[ROOM_BACKGROUND_OT], &quads[0]);
        link_quad(&((RenderNativeTag *)b->ordering)[ROOM_BACKGROUND_OT], &quads[1]);
        return 1;
    }
    if (!s_show) return 0;
    if (b->stage == 0) return 1;                       /* skip the PE room's own tiles */
    if (b->slot < 0 || b->slot > 1 || !b->ordering) return 0;
    RenderNativeTag *ordering = b->ordering;
    RenderTexturedQuad *quads = s_quads[b->slot];
    const u8 shade = (u8)(s_shade < 0 ? 0 : s_shade > 128 ? 128 : s_shade);   /* the texture times shade / 128 */
    picture_quad(&quads[0], 0, PIC_TOP, PIC_SPLIT, PIC_TOP + FIELD_H);
    picture_quad(&quads[1], PIC_SPLIT, PIC_TOP, PIC_W, PIC_TOP + FIELD_H);
    for (int k = 0; k < 2; k++) quads[k].color.bytes.r = quads[k].color.bytes.g = quads[k].color.bytes.b = shade;
    link_quad(&ordering[ROOM_BACKGROUND_OT], &quads[0]);
    link_quad(&ordering[ROOM_BACKGROUND_OT], &quads[1]);
    if ((s_placement_meshes || s_scene_count) && !s_hold) {
        prepare_field_texture();
        draw_placement_meshes(ordering, b->slot, shade);
    }
    if ((s_placement_markers || s_placement_meshes) && !s_hold) draw_placement_markers(ordering, b->slot, shade);
    if (s_foreground)
        for (size_t i = 0; i < s_mask_count; i++) {
            RenderTexturedQuad *m = &s_masks[i].quad[b->slot];
            m->color.bytes.r = m->color.bytes.g = m->color.bytes.b = shade;
            link_quad(&ordering[s_masks[i].depth], m);
        }
    if (s_curtain) {                                   /* the picture's texels at colour 0: black, Aya too */
        RenderTexturedQuad *c = s_curtain_quads[b->slot];
        picture_quad(&c[0], 0, PIC_TOP, PIC_SPLIT, PIC_TOP + FIELD_H);
        picture_quad(&c[1], PIC_SPLIT, PIC_TOP, PIC_W, PIC_TOP + FIELD_H);
        for (int k = 0; k < 2; k++) {
            c[k].color.bytes.r = c[k].color.bytes.g = c[k].color.bytes.b = 0;
            link_quad(&ordering[FIELD_UI_OT_LAST + 1], &c[k]);
        }
    }
    return 1;
}

/* PE loaded another room of its own: its pages are already in VRAM, so just stop. */
static int on_scene(void *ctx, void *user) {
    (void)ctx, (void)user;
    backdrop_off(0);                                   /* a new room's pages and camera are coming */
    clear_field_texture();
    if (s_masks) DrawSync(0);
    free_masks();
    free(s_original_picture);
    s_original_picture = NULL;
    s_show = s_walk.active = 0;
    return 0;
}

void pe_re_stage_hide(void) {
    s_shade = 128, s_curtain = s_hold = 0;
    clear_field_texture();
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
void pe_re_stage_set_placement_markers(int on) { s_placement_markers = on; }
void pe_re_stage_set_placement_idle(int on) { s_placement_idle = on; }
void pe_re_stage_set_placement_meshes(int on) {
    if (s_placement_meshes == on) return;
    s_placement_meshes = on;
    clear_field_texture();
    load_field_meshes();
}
void pe_re_stage_set_placements(const PeReEnemyPlacement *placements, int count) {
    clear_field_texture();
    s_placement_count = count;
    if (count) memcpy(s_placements, placements, (size_t)count * sizeof *placements);
    if (!count) s_scene_count = 0;
    load_field_meshes();
}
void pe_re_stage_set_scene_actors(const PeReSceneActor *actors, int count) {
    s_scene_count = count;
    if (count) memcpy(s_scene_actors, actors, (size_t)count * sizeof *actors);
}
void pe_re_stage_player(double *x, double *z, int *facing) { *x = s_walk.x, *z = s_walk.z, *facing = s_walk.yaw; }
int pe_re_stage_floor(void) { return s_walk.floor; }

/* Testing: stand at the foot of the room's next slope or stairs, just outside it. */
int pe_re_stage_to_ramp(void) {
    static unsigned s_next;
    for (unsigned k = 0; k < s_walk.collision.count; k++) {
        const unsigned i = (s_next + k) % s_walk.collision.count;
        const PeRe2Collider *c = &s_walk.collision.entries[i];
        if (c->shape != 11 && c->shape != 12) continue;
        double x = (c->x0 + c->x1) / 2.0, z = (c->z0 + c->z1) / 2.0;
        switch ((c->type >> 4) & 3) {                   /* the bottom end, a body's width outside */
        case 0: x = c->x0 - AYA_ROOM_RADIUS; break;
        case 1: x = c->x1 + AYA_ROOM_RADIUS; break;
        case 2: z = c->z0 - AYA_ROOM_RADIUS; break;
        default: z = c->z1 + AYA_ROOM_RADIUS; break;
        }
        uint32_t f = c->floors;
        int low = 0;
        if (c->shape == 12) for (; f && !(f & 1) && low < 31; low++, f >>= 1) {}
        else low = ((c->type >> 6) & 0x1F) - 1;
        s_walk.floor = low < 0 ? 0 : low;
        s_walk.y = -(double)s_walk.floor * PE_RE2_FLOOR_HEIGHT;
        pe_re_stage_place(x, z);
        s_next = i + 1;
        return 1;
    }
    return 0;
}
void pe_re_stage_face(int facing) { s_walk.yaw = facing & 4095; }
int pe_re_stage_blocked(double x, double z) { return blocked_here(x, z); }
void pe_re_stage_collision_id(unsigned slot, unsigned id) { pe_imp_re2_collision_set_id(&s_walk.collision, slot, id); }
void pe_re_stage_place(double x, double z) {
    s_walk.x = x, s_walk.z = z;
    s_walk.ghost = blocked_here(x, z);
}
void pe_re_stage_set_masks(int on) { s_foreground = on; }
void pe_re_stage_fade(int shade, int curtain, int hold) { s_shade = shade, s_curtain = curtain, s_hold = hold; }
void pe_re_stage_set_size(int percent) { if (percent != s_size) s_size = percent, pe_re_scale(); }
double pe_re_stage_scale(void) { return s_scale; }

void pe_re_stage_init(const char *mod_id, const char *store) {
    s_store = store, s_scale = 3;
    pe_imp_mdec_set_table(MdecDiag_ExpandDecoderTable);
    pe_hook_add(PE_HOOK_FIELD_BACKGROUND, on_background, NULL, PE_PRIORITY_LATE, mod_id);
    pe_hook_add(PE_HOOK_FIELD_CAMERA, on_camera, NULL, PE_PRIORITY_LATE, mod_id);
    pe_hook_add(PE_HOOK_FIELD_MOVE, on_move, NULL, PE_PRIORITY_LATE, mod_id);
    pe_hook_add(PE_HOOK_SCENE_START, on_scene, NULL, PE_PRIORITY_LATE, mod_id);
    pe_hook_add(pe_event_id(PE_EVENT_IMPORT_PUBLISHED), on_import_published, NULL, PE_PRIORITY_NORMAL, mod_id);
}

void pe_re_stage_shutdown(void) {
    s_show = s_walk.active = 0;
    clear_field_texture();
    for (unsigned kind = 0; kind < 0x40; kind++)
        if (s_field_mesh[kind].tried) free_field_mesh(kind);
    free_masks();
    free(s_original_picture);
    s_original_picture = NULL;
    backdrop_off(0);
    s_backdrop.armed = 0;
    free(s_picture);
    s_picture = NULL;
}
