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
#include <psyz/audio.h>
#include <SDL3/SDL.h>

#include "pe_re_stage.h"
#include "../../pe_re_session.h"
#include "pe_re_box.h"

#include "pe_imp.h"
#include "pe_imp_mdec.h"
#include "pe_imp_pe.h"
#include "pe_actor_texture_pages.h"
#include "pe1/entity_frame_update.h"
#include "pe_imp_rdt.h"
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

/* libetc's callback declarations conflict with the native PE callbacks. */
extern long GetVideoMode(void);
enum { PE_RE_PAL = 1 };

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
#define FIELD_TEX_X 320
#define FIELD_TEX_Y 64
#define FIELD_TEX_WORDS 64
#define FIELD_TEX_HEIGHT 128
/* the field meshes' palettes: the first empty spot of these for the mesh's rows (a PE room may have
 * filled the first: Central Park's does) */
static const struct { int x, y, rows; } k_field_clut_spots[] = {{320, 496, 16}, {0, 475, 5}, {0, 508, 4}};
static int s_clut_x = 320, s_clut_y = 496;
#define FIELD_CLUT_X s_clut_x
#define FIELD_CLUT_Y s_clut_y
#define FIELD_CLUT_ROWS 16
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
static struct {
    PeImpMovie *asset;
    PeImpMovieClip clip;
    uint8_t *rgb;
    uint16_t *pixels, *native_picture;
    uint32_t start_vblank, frame, hz, sector;
    SDL_AudioStream *audio;
    unsigned audio_rate, audio_channels;
    int audio_bytes;
} s_movie;
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
static struct { int kind; double x, z; } s_marks[PE_RE_STAGE_MARKS];   /* draw_marks */
static RenderColorTrianglePacket s_mark_packets[2][PE_RE_STAGE_MARKS];
static RoomMaskPacket *s_masks;
static size_t s_mask_count;
static uint8_t s_mask_groups[16];
static int s_placement_markers;
static int s_placement_meshes;
static int s_placement_idle;
static PeReEnemyPlacement s_placements[PE_RE_ENEMIES_MAX];
static uint32_t s_last_motion[PE_RE_ENEMIES_MAX];
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
static unsigned s_texture_palette_rows;
static uint16_t s_texture_words[FIELD_TEX_WORDS * FIELD_TEX_HEIGHT];
static uint16_t s_texture_clut[FIELD_CLUT_ROWS][256];
static uint16_t s_vram_check[64 * 256];
enum { FIELD_MESH_KINDS = 256 };

static struct {
    PeImpModel model;
    int tried;
    int64_t foot_y;
    unsigned first_page, first_palette;
    PeImpAnimation moves;
} s_field_mesh[FIELD_MESH_KINDS];
static struct {
    char asset[PE_IMP_ID_MAX];
    PeImpAnimation moves;
} s_actor_bank[PE_RE_ENEMIES_MAX];

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
    char room_id[PE_IMP_ID_MAX];
} s_walk;
static struct {
    PeImpBuf row;
    unsigned frame, frames;
    double x, z;
    int floor;
} s_climb;

static int s_scene_locked, s_scene_pose;
static unsigned s_scene_frame;

static struct {
    unsigned buttons;
    int analog_x, analog_y;
    double right_x, right_z;
} s_walk_input;

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

static void walk_input(unsigned buttons, int analog_x, int analog_y) {
    const int analog = analog_x || analog_y;
    const int previous_analog = s_walk_input.analog_x || s_walk_input.analog_y;
    if (analog && previous_analog) {
        const int dot = analog_x * s_walk_input.analog_x + analog_y * s_walk_input.analog_y;
        const int length = analog_x * analog_x + analog_y * analog_y;
        const int previous_length = s_walk_input.analog_x * s_walk_input.analog_x +
                                    s_walk_input.analog_y * s_walk_input.analog_y;
        /* About 22.5 degrees. Ignore small stick drift and changes in pressure. */
        if (dot > 0 && (double)dot * dot >= 0.854 * length * previous_length) return;
    } else if (!analog && !previous_analog && buttons == s_walk_input.buttons) {
        return;
    }
    s_walk_input.buttons = buttons;
    s_walk_input.analog_x = analog_x;
    s_walk_input.analog_y = analog_y;
    /* A held direction keeps the camera axes it started with, across cuts.
     * A new direction uses the view on screen when the player chose it. */
    s_walk_input.right_x = s_camera.right_x;
    s_walk_input.right_z = s_camera.right_z;
}

static int on_move(void *ctx, void *user) {
    (void)user;
    const FieldActor *aya = ctx;
    if (aya != g_PlayerEntity) return 0;
    if (s_movie.asset && !s_show) return 1;
    if (!s_show || !s_walk.active) return 0;
    const int free_field = pe_free_field();
    if (!free_field || pe_re_box_busy() || s_hold || s_scene_locked || s_climb.row.b) {
        walk_input(0, 0, 0);
        return free_field;
    }
    int analog_x = g_PadPackets[0].leftX - 128, analog_y = g_PadPackets[0].leftY - 128;
    if ((g_PadPackets[0].header & 0xF000) != 0x7000 || (abs(analog_x) <= 16 && abs(analog_y) <= 16))
        analog_x = analog_y = 0;
    const unsigned buttons = g_FieldPadBits & (FIELD_PAD_UP | FIELD_PAD_DOWN | FIELD_PAD_LEFT | FIELD_PAD_RIGHT);
    walk_input(analog_x || analog_y ? 0 : buttons, analog_x, analog_y);
    if (analog_x || analog_y || buttons) {
        /* Recover the native walk/run step in screen axes. The native pad
         * mapping gives physical Up a positive local Z step. */
        double x = ((double)aya->pos_x - aya->base_x) * RE_UNITS_PER_PE_UNIT / 65536.0;
        double z = ((double)aya->pos_z - aya->base_z) * RE_UNITS_PER_PE_UNIT / 65536.0;
        const GteMatrix *basis = &g_GeomScroll.state.position.cameraBasis;
        double right = (basis->m[0][0] * x + basis->m[2][0] * z) / 4096.0;
        double forward = (basis->m[0][2] * x + basis->m[2][2] * z) / 4096.0;
        double dx = right * s_walk_input.right_x - forward * s_walk_input.right_z;
        double dz = right * s_walk_input.right_z + forward * s_walk_input.right_x;
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

static void clear_climb(void) {
    free(s_climb.row.b);
    memset(&s_climb, 0, sizeof s_climb);
}

static void climb_source_tree(const PeImpModel *model, int bone, int8_t *commands, int *count) {
    commands[(*count)++] = -1;
    commands[(*count)++] = (int8_t)bone;
    for (unsigned child = (unsigned)bone + 1; child < model->bones; child++)
        if (model->parent[child] == bone) climb_source_tree(model, (int)child, commands, count);
    commands[(*count)++] = -2;
}

static void re2_rotation(const int16_t angle[3], PeM3 out) {
    const double sx = sin(angle[0] * FIELD_TURN_RADIANS), cx = cos(angle[0] * FIELD_TURN_RADIANS);
    const double sy = sin(angle[1] * FIELD_TURN_RADIANS), cy = cos(angle[1] * FIELD_TURN_RADIANS);
    const double sz = sin(angle[2] * FIELD_TURN_RADIANS), cz = cos(angle[2] * FIELD_TURN_RADIANS);
    const double rotation[3][3] = {
        {cy * cz, -cy * sz, sy},
        {sx * sy * cz + cx * sz, cx * cz - sx * sy * sz, -sx * cy},
        {sx * sz - cx * sy * cz, cx * sy * sz + sx * cz, cx * cy}
    };
    memcpy(out, rotation, sizeof rotation);
}

static void climb_source_angles(const int16_t *sample, unsigned bones, int angles[][3]) {
    /* Convert RE2's XYZ Euler order and align RE2 +X with PE -Z. */
    const PeM3 basis = {{0, 0, 1}, {0, 1, 0}, {-1, 0, 0}};
    for (unsigned i = 0; i < bones; i++) {
        PeM3 rotation;
        re2_rotation(sample + 3 + 3 * i, rotation);
        if (i == 0) pe_imp_mm(basis, rotation, rotation);
        pe_imp_to_yxz(rotation, angles[i]);
    }
}

static int climb_row(const char *asset, unsigned player) {
    if (!g_PlayerEntity || !g_PlayerEntity->action_data) return 0;
    const RenderObjectEntity *aya = &g_PlayerEntity->render_object;
    RenderAnimationDataHeader *pose = g_PlayerEntity->action_data;
    if (!aya->header || !aya->matrix_commands || !aya->parts ||
        aya->header->part_count > PE_IMP_PARTS_MAX || pose->last_bone_index >= aya->header->part_count) return 0;
    const int parts = aya->header->part_count;
    /* Live room Aya can use a different mesh and skeleton than the model viewer. */
    const unsigned channels = 3 + 3 * (pose->last_bone_index + 1u);
    const PeImpBuf idle_row = {(uint8_t *)pose, sizeof *pose + channels * 4 * ((pose->frame_count >> 1) + 1u)};
    PeImpFrame idle;
    if (pe_imp_anim_frame(&idle_row, 0, (s16)g_PlayerEntity->anim.parts.integer, &idle)) return 0;

    const char *model_id = player == 12 ? "re2:player/hunk" : player ? "re2:player/claire" : "re2:player/leon";
    char why[160];
    PeImpModel model = {0};
    PeImpAnimation animation = {0};
    int ok = 0;
    if (pe_imp_catalog_model(s_store, model_id, &model, why, sizeof why) ||
        pe_imp_catalog_animation(s_store, asset, &animation, why, sizeof why)) {
        pe_mod_log("pe_re_play", "climb asset: %s", why);
        goto done;
    }
    static const int16_t parent[] = {-1, 0, 1, 2, 3, 1, 5, 6, 0, 0, 9, 10, 0, 12, 13};
    if (model.bones != sizeof parent / sizeof *parent || memcmp(model.parent, parent, sizeof parent) ||
        animation.bones != model.bones || animation.moves <= 6) goto done;
    const PeImpMove *move = &animation.move[6];
    /* The human one-level climb in pl_mv08 uses room move 6 and lands at frame 29. */
    if (move->frames != 50) goto done;
    int8_t commands[3 * PE_IMP_PARTS_MAX];
    PeImpMd from = {.parts = model.bones, .cmds = commands};
    PeImpMd to = {.parts = parts, .ncmds = aya->header->matrix_command_bytes, .cmds = aya->matrix_commands};
    for (unsigned i = 0; i < model.bones; i++) {
        if (model.parent[i] >= (int)i) goto done;
        if (model.parent[i] == -1) climb_source_tree(&model, i, commands, &from.ncmds);
        if (i < model.parts) from.ptab[i].vc = model.part[i].tris + model.part[i].quads;
    }
    for (int i = 0; i < parts; i++) to.ptab[i].vc = aya->parts[i].vertex_count;
    /* RE2 has no separate pelvis bone and its head branches from the root.
     * Use the shared PE chain decoder for Aya and the verified RE2 layout. */
    PeImpChain chains[CH_COUNT];
    PeImpMap map;
    if (pe_imp_chains(&to, chains)) {
        pe_mod_log("pe_re_play", "climb target does not have the expected Aya bone chains");
        goto done;
    }
    for (int i = 0; i < PE_IMP_PARTS_MAX; i++) map.map[i] = -1;
    map.map[chains[CH_ROOT].p[0]] = 0;
    map.map[chains[CH_PELVIS].p[0]] = 0;
    map.map[chains[CH_CHEST].p[0]] = 1;
    map.map[chains[CH_HEAD].p[chains[CH_HEAD].n - 1]] = 8;
    static const unsigned limbs[] = {CH_ARMA, CH_ARMB, CH_LEGA, CH_LEGB};
    static const int first[] = {2, 5, 9, 12};
    for (unsigned i = 0; i < 4; i++) {
        const PeImpChain *chain = &chains[limbs[i]];
        const int skip = i < 2 && chain->n > 3;
        for (int bone = 0; bone < 3 && bone + skip < chain->n; bone++)
            map.map[chain->p[bone + skip]] = first[i] + bone;
    }
    int packed[50 * PE_IMP_PARTS_MAX][3] = {0}, input[PE_IMP_PARTS_MAX][3];
    double root[50][3];
    PeM3 source_rest[PE_IMP_PARTS_MAX], target_rest[PE_IMP_PARTS_MAX];
    uint8_t source_has[PE_IMP_PARTS_MAX], target_has[PE_IMP_PARTS_MAX];
    PeImpCorr correction = {0};
    /* Use the opening pose to calibrate each joint against the live PE
     * skeleton. RE2 and PE meshes use different axes for the same bones. */
    climb_source_angles(move->data, model.bones, input);
    pe_imp_world_rotations(&from, input, model.bones, source_rest, source_has);
    pe_imp_world_rotations(&to, idle.ang, idle.n, target_rest, target_has);
    for (int i = 0; i < parts; i++) {
        int source = map.map[i];
        if (source < 0 || !source_has[source] || !target_has[i]) continue;
        PeM3 inverse;
        pe_imp_transpose(source_rest[source], inverse);
        pe_imp_mm(inverse, target_rest[i], correction.m[i]);
        correction.has[i] = 1;
    }
    for (unsigned f = 0; f < 50; f++) {
        const int16_t *sample = move->data + f * (3 + 3 * model.bones);
        climb_source_angles(sample, model.bones, input);
        pe_imp_retarget_world(&from, input, model.bones, &to, &map, idle.ang, idle.n,
                              &correction, NULL, packed + f * parts);
        for (int k = 0; k < 3; k++) root[f][k] = idle.tr[k];
        /* RE2 faces local +X and Aya faces local -Z. The offsets reset
         * against the new world origin at the ledge's landing frame. */
        root[f][0] += (sample[2] - move->data[2]) / s_scale;
        root[f][1] += (sample[1] - move->data[1]) / s_scale;
        root[f][2] -= (sample[0] - move->data[0]) / s_scale;
    }
    const uint16_t hit[] = {pose->object_value74, pose->object_value76, pose->object_value78, pose->object_value7c};
    ok = !pe_imp_encode_row(50, root, packed, parts, hit, &s_climb.row);
done:
    pe_imp_model_free(&model);
    pe_imp_animation_free(&animation);
    return ok;
}

static int climb_surface(const PeRe2Collider *c, int floor) {
    const int top = ((c->type >> 6) & 31) * PE_RE2_FLOOR_HEIGHT + (c->type >> 11) * 100;
    return c->shape == 9 && (c->id & 0x8000) && (c->floors & (1u << floor)) &&
           top == (floor + 1) * PE_RE2_FLOOR_HEIGHT;
}

int pe_re_stage_climb(int pressed, const char *asset, unsigned player) {
    if (s_climb.row.b) {
        ++s_climb.frame;
        if (s_climb.frame == 29 * 2) {
            s_walk.x = s_climb.x, s_walk.z = s_climb.z, s_walk.floor = s_climb.floor;
            s_walk.y = -(double)s_walk.floor * PE_RE2_FLOOR_HEIGHT;
        }
        if (s_climb.frame < s_climb.frames) return 1;
        clear_climb();
        return 2;
    }
    if (!pressed || !*asset || !s_show || !s_walk.active || s_hold || s_walk.floor < 0 || s_walk.floor >= 31) return 0;
    const int dir = (s_walk.yaw + 1024 + 512) & 0xc00;
    const double angle = dir * FIELD_TURN_RADIANS, dx = cos(angle), dz = -sin(angle);
    const double probe_x = s_walk.x + 800 * dx, probe_z = s_walk.z + 800 * dz;
    const double landing_x = s_walk.x + 1000 * dx, landing_z = s_walk.z + 1000 * dz;
    for (unsigned i = 0; i < s_walk.collision.count; i++) {
        const PeRe2Collider *c = &s_walk.collision.entries[i];
        if (!climb_surface(c, s_walk.floor) ||
            probe_x < c->x0 || probe_x > c->x1 || probe_z < c->z0 || probe_z > c->z1) continue;
        if (pe_imp_re2_blocked_on(&s_walk.collision, (unsigned)s_walk.floor + 1, landing_x, landing_z, AYA_ROOM_RADIUS)) continue;
        if (!climb_row(asset, player)) {
            pe_mod_log("pe_re_play", "cannot retarget climb %s move 6 for player %u", asset, player);
            return 0;
        }
        s_walk.yaw = pe_re_facing_of(dir);
        s_climb.x = landing_x, s_climb.z = landing_z, s_climb.floor = s_walk.floor + 1;
        s_climb.frames = 100;
        walk_input(0, 0, 0);
        pe_mod_log("pe_re_play", "climb %s move 6: floor %d to %d", asset, s_walk.floor, s_climb.floor);
        return 1;
    }
    return 0;
}

static int on_player_scene_render(void *ctx, void *user) {
    (void)user;
    const PeHookActorDraw *draw = ctx;
    FieldActor *aya = draw->actor;
    if ((!s_climb.row.b && !s_scene_locked) || !aya || aya != g_PlayerEntity) return 0;
    RenderAnimationDataHeader *row = s_climb.row.b ? (RenderAnimationDataHeader *)s_climb.row.b
                                                   : g_GameState.bank_rows[0][s_scene_pose];
    RenderObjectEntity *object = &aya->render_object;
    if (!row || !row->frame_count || !object->header || row->last_bone_index >= object->header->part_count) return 0;
    const unsigned frame = s_climb.row.b ? s_climb.frame / 2 : s_scene_frame % row->frame_count;
    uint32_t *view = (uint32_t *)draw->view;
    Render_InitRoomPrimState(object);
    Anim_BuildRotationMatrices(object, row, frame);
    Render_TransformVertices(object);
    Render_TransformSkinnedVertices(object, view);
    Render_TransformMorphVertices(object, view);
    PeModelTextures_Actor(aya);
    Render_DrawEntity(object, view);
    return 1;
}

/* Aya's battle moves against the imported room's walls, not the PE room's under it: once she has
 * settled, the PE room's floor checks leave her (flag 0x80, Entity_UpdateList), and each frame her
 * step, taken into the room by the backdrop's mapping, slides along the room's collision as her
 * field walk does. Otherwise the PE room's own floor bounded her battle, a different shape. */
static struct { int on, own_flag; double x, z; } s_battle_walk;
static const PeImpRoomLogic *s_logic;   /* the shown room's logic: its camera zones, for a battle's camera */

static void battle_walk_off(void) {
    if (s_battle_walk.on && g_PlayerEntity && !s_battle_walk.own_flag) g_PlayerEntity->flags &= ~0x80u;
    /* she stays where the battle left her in the room (it may have taken her into another camera) */
    if (s_battle_walk.on) s_walk.x = s_battle_walk.x, s_walk.z = s_battle_walk.z;
    s_battle_walk.on = 0;
}

/* The battle's camera follows her across the room's camera zones, as her field walk does
 * (docs/RE_PLAY_BATTLES.md, D6; the director takes this decision over in phase 4's hookup). */
static void battle_follow(void) {
    if (!s_logic) return;
    const double x = s_battle_walk.on ? s_battle_walk.x : s_walk.x, z = s_battle_walk.on ? s_battle_walk.z : s_walk.z;
    const int camera = pe_re_camera_at(s_logic, s_view, s_walk.floor, x, z);
    if (camera == s_view) return;
    const int from = s_view;
    if (!pe_re_stage_battle_camera(camera)) pe_mod_log(NULL, "re play: battle camera %d -> %d", from, camera);
}

static void battle_walk(FieldActor *aya) {
    const double k = RE_UNITS_PER_PE_UNIT;
    const double x = s_backdrop.room[0] + (aya->pos_x / 65536.0 - s_backdrop.anchor[0]) * k;
    const double z = s_backdrop.room[2] + (aya->pos_z / 65536.0 - s_backdrop.anchor[2]) * k;
    if (!s_battle_walk.on) {
        s_battle_walk.on = 1, s_battle_walk.own_flag = (aya->flags & 0x80) != 0;
        aya->flags |= 0x80;
        s_battle_walk.x = x, s_battle_walk.z = z;
        return;
    }
    const double dx = x - s_battle_walk.x, dz = z - s_battle_walk.z;
    if (dx == 0 && dz == 0) return;
    double wx = s_battle_walk.x, wz = s_battle_walk.z;
    pe_imp_re2_slide_on(&s_walk.collision, (unsigned)s_walk.floor, &wx, &wz, dx, dz, AYA_ROOM_RADIUS);
    s_battle_walk.x = wx, s_battle_walk.z = wz;
    if (wx != x || wz != z) {                         /* a wall: where the slide left her */
        aya->pos_x = (int)lround((s_backdrop.anchor[0] + (wx - s_backdrop.room[0]) / k) * 65536.0);
        aya->pos_z = (int)lround((s_backdrop.anchor[2] + (wz - s_backdrop.room[2]) / k) * 65536.0);
    }
}

static void backdrop_off(int restore) {
    battle_walk_off();
    if (!s_backdrop.on) return;
    if (restore) {
        GeomScrollCoordinates *c = &g_GeomScroll.state.position;
        if (c->matrixWords) *(GteMatrix *)c->matrixWords = s_backdrop.saved_view;
        if (c->projectionDistance) *c->projectionDistance = s_backdrop.saved_distance;
        c->lightTint = s_backdrop.saved_light, c->meshTintLevel = s_backdrop.saved_tint_level;
        /* the room stays up through a battle: its current camera's picture (the battle may have
         * changed camera); else what the battle covered */
        const uint16_t *pages = s_show && s_picture ? s_picture : s_backdrop.saved_pages;
        if (pages) {
            RECT r = {VRAM_X, VRAM_Y, PIC_W, PIC_H};
            DrawSync(0);
            LoadImage(&r, (void *)pages);
            DrawSync(0);
        }
    }
    free(s_backdrop.saved_pages);
    s_backdrop.saved_pages = NULL;
    s_backdrop.on = 0;
    memset(s_marks, 0, sizeof s_marks);
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
            } else if (s_walk.collision.count) {
                battle_walk((FieldActor *)aya);
                battle_follow();
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
    int rc = pe_imp_room_foreground_depth_groups(set, (uint32_t)s_view, view, s_mask_groups, depths);
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

static void clear_effects(int restore);


/* The decoded picture into VRAM, kept for a battle backdrop, with its foreground masks. Takes `pix`. */
static void install_picture(const uint8_t *rgb, uint16_t *pix, const PeImpImageSet *set, const PeImpRoomView *view) {
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
    s_picture = pix;                                 /* kept for a battle backdrop */
    free_masks();
    build_masks(set, view, &s_masks, &s_mask_count);
}

int pe_re_stage_show(const char *room_id, const PeImpRoomLogic *logic, int camera, int floor, int place, double x,
                     double z, int facing, char *why, size_t n) {
    s_logic = logic;
    if (strcmp(room_id, s_walk.room_id)) clear_effects(1);
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
        const int camera_only = s_walk.active && place && camera != s_view &&
                                !strcmp(room_id, s_walk.room_id) && floor == s_walk.floor &&
                                x == s_walk.x && z == s_walk.z;
        s_view = camera;
        prepare_camera(&view);
        uint32_t coll = 0;
        const uint8_t *data = logic ? pe_imp_logic_section(logic, PE_IMP_LOGIC_COLL, NULL, &coll) : NULL;
        if (!s_camera.distance) snprintf(why, n, "camera %d has no projection", camera);
        else if (!camera_only && (!data || pe_imp_re2_collision_all(data, coll, &s_walk.collision)) &&
                 pe_imp_re2_image_collision(set, &s_walk.collision))
            snprintf(why, n, "%s has no collision", room_id);
        else if (!place && !spawn_on_floor()) snprintf(why, n, "no free floor in camera %d", camera);
        else {
            if (!camera_only) {
                walk_input(0, 0, 0);
                s_walk.floor = floor;
                s_walk.y = -(double)floor * PE_RE2_FLOOR_HEIGHT;
                if (place) {                                 /* a door's landing point: clear of walls */
                    s_walk.x = x, s_walk.z = z, s_walk.yaw = facing;
                    s_walk.ghost = !pe_re_free_spot(&s_walk.collision, floor, &s_walk.x, &s_walk.z, AYA_ROOM_RADIUS, 2000);
                } else {
                    s_walk.ghost = 0;
                }
            }
            snprintf(s_walk.room_id, sizeof s_walk.room_id, "%s", room_id);
            install_picture(rgb, pix, set, &view);
            pix = NULL;
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

/* G3: another camera of the shown room in battle: its picture, foreground masks and view. Aya, the
 * collision and the backdrop's saved pages are left as they are. 0, or -1 (logged). */
int pe_re_stage_battle_camera(int camera) {
    char why[160] = "";
    PeImpImageSet *set = NULL;
    PeImpRoomView view;
    uint32_t size = 0;
    int rc = -1;
    if (!s_backdrop.on || !s_show || camera == s_view) return -1;
    if (pe_imp_catalog_imageset(s_store, s_walk.room_id, &set, why, sizeof why)) {
        pe_mod_log(NULL, "re play: battle camera %d: %s", camera, why);
        return -1;
    }
    const uint8_t *frame = pe_imp_imageset_frame(set, (uint32_t)camera, &size);
    uint8_t *rgb = malloc(PIC_W * PIC_H * 3);
    uint16_t *pix = malloc(PIC_W * PIC_H * 2);
    if (frame && rgb && pix && set->width == PIC_W && set->height == PIC_H &&
        !pe_imp_room_view(set, (uint32_t)camera, &view) && view.game == PE_IMP_RE2 &&
        !pe_imp_mdec_decode(frame, size, PIC_W, PIC_H, rgb, why, sizeof why)) {
        const int old_view = s_view;
        s_view = camera;
        prepare_camera(&view);
        if (s_camera.distance) {
            install_picture(rgb, pix, set, &view);
            pix = NULL;
            rc = 0;
        } else {
            s_view = old_view;                        /* no projection: keep the one we have */
            snprintf(why, sizeof why, "camera %d has no projection", camera);
        }
    }
    if (rc) pe_mod_log(NULL, "re play: battle camera %d: %s", camera, why[0] ? why : "not drawable");
    free(rgb);
    free(pix);
    free(set);
    return rc;
}

static void upload_movie_picture(uint16_t *pixels) {
    RECT rect = {VRAM_X, VRAM_Y, PIC_W, PIC_H};
    DrawSync(0);
    LoadImage(&rect, pixels);
    DrawSync(0);
}

/* The STR header describes the visible rectangle. MDEC decodes complete blocks. */
static unsigned movie_stride(unsigned size) { return (size + 15) & ~15u; }

static int draw_movie_frame(uint32_t frame) {
    const PeImpMovie *m = s_movie.asset;
    char why[160] = "";
    if (pe_imp_mdec_decode(m->frame_data + m->offset[frame], m->size[frame],
                           movie_stride(m->width), movie_stride(m->height), s_movie.rgb, why, sizeof why)) {
        pe_mod_log(NULL, "RE2 movie frame %u: %s", (unsigned)frame, why);
        return 0;
    }
    /* Fit the source in the PE field viewport, with opaque black bars. */
    const unsigned width = m->width * FIELD_H > m->height * PIC_W ? PIC_W : m->width * FIELD_H / m->height;
    const unsigned height = m->width * FIELD_H > m->height * PIC_W ? m->height * PIC_W / m->width : FIELD_H;
    const unsigned left = (PIC_W - width) / 2, top = (PIC_H - height) / 2;
    for (unsigned i = 0; i < PIC_W * PIC_H; i++) s_movie.pixels[i] = 0x8000;
    for (unsigned y = 0; y < height; y++) {
        for (unsigned x = 0; x < width; x++) {
            const uint8_t *rgb = s_movie.rgb + 3 * ((y * m->height / height) * movie_stride(m->width) + x * m->width / width);
            uint16_t pixel = (uint16_t)((rgb[0] >> 3) | (rgb[1] >> 3) << 5 | (rgb[2] >> 3) << 10);
            s_movie.pixels[(top + y) * PIC_W + left + x] = pixel ? pixel : 0x8000;
        }
    }
    upload_movie_picture(s_movie.pixels);
    s_movie.frame = frame;
    return 1;
}

static void clear_movie(void) {
    if (s_movie.audio) SDL_DestroyAudioStream(s_movie.audio);
    pe_imp_movie_free(s_movie.asset);
    free(s_movie.rgb);
    free(s_movie.pixels);
    free(s_movie.native_picture);
    memset(&s_movie, 0, sizeof s_movie);
}

static void start_movie_audio(void) {
    const char *clock = SDL_getenv("PSYZ_AUDIO_CLOCK");
    if (clock && !strcmp(clock, "video")) return;    /* deterministic headless playback */
    uint32_t frames;
    /* RE2's movie player sets its CD-XA filter to file 1, channel 1. */
    int16_t *pcm = pe_imp_movie_pcm(s_movie.asset, &s_movie.clip, 1, 1, &frames,
                                   &s_movie.audio_rate, &s_movie.audio_channels);
    if (!pcm) return;
    SDL_AudioSpec spec = {SDL_AUDIO_S16, (int)s_movie.audio_channels, (int)s_movie.audio_rate};
    SDL_AudioStream *stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    const int bytes = (int)(frames * s_movie.audio_channels * sizeof *pcm);
    if (stream && SDL_SetAudioStreamGain(stream, (float)Psyz_AudioGetOutputVolume() / 100.0f) &&
        SDL_PutAudioStreamData(stream, pcm, bytes) && SDL_FlushAudioStream(stream) && SDL_ResumeAudioStreamDevice(stream)) {
        s_movie.audio = stream;
        s_movie.audio_bytes = bytes;
    } else if (stream) {
        SDL_DestroyAudioStream(stream);
    }
    free(pcm);
}

int pe_re_stage_movie_start(const char *asset, unsigned start_sector, unsigned last_frame, int silent) {
    char why[160] = "";
    if (s_movie.asset || s_backdrop.on || (!s_show && !pe_free_field())) return 0;
    if (pe_imp_catalog_movie(s_store, asset, &s_movie.asset, why, sizeof why)) {
        pe_mod_log(NULL, "%s: %s", asset, why);
        return 0;
    }
    const PeImpMovie *m = s_movie.asset;
    const PeImpMovieClip *clip = &s_movie.clip;
    /* Older imports can play from the beginning, but cannot seek or match STR frame numbers. */
    if (!start_sector && !m->frame_number) last_frame = 0;
    if (!pe_imp_movie_clip(m, start_sector, last_frame, &s_movie.clip)) {
        pe_mod_log(NULL, "%s: clip at sector %u through frame %u unavailable (rescan raw disc for movie timing)",
                   asset, start_sector, last_frame);
        clear_movie();
        return 0;
    }
    s_movie.rgb = malloc((size_t)movie_stride(m->width) * movie_stride(m->height) * 3);
    s_movie.pixels = malloc(PIC_W * PIC_H * sizeof *s_movie.pixels);
    if (!s_show) s_movie.native_picture = malloc(PIC_W * PIC_H * sizeof *s_movie.native_picture);
    if (!s_movie.rgb || !s_movie.pixels || (!s_show && !s_movie.native_picture)) {
        clear_movie();
        return 0;
    }
    if (s_movie.native_picture) {
        /* A scenario opening owns only the field texture until it finishes. */
        RECT rect = {VRAM_X, VRAM_Y, PIC_W, PIC_H};
        DrawSync(0);
        StoreImage(&rect, s_movie.native_picture);
        DrawSync(0);
    }
    if (!draw_movie_frame(clip->first_frame)) {
        clear_movie();
        return 0;
    }
    if (m->frame_sector && m->frame_sector[clip->first_frame] > clip->start_sector) {
        for (unsigned i = 0; i < PIC_W * PIC_H; i++) s_movie.pixels[i] = 0x8000;
        upload_movie_picture(s_movie.pixels);
        s_movie.frame = UINT32_MAX;
    }
    if (!silent) start_movie_audio();
    s_movie.hz = GetVideoMode() == PE_RE_PAL ? 50 : 60;
    s_movie.start_vblank = (uint32_t)Psyz_VideoVSync(-1);
    return 1;
}

int pe_re_stage_movie_tick(void) {
    const PeImpMovie *m = s_movie.asset;
    if (!m) return 0;
    const uint32_t now = (uint32_t)Psyz_VideoVSync(-1);
    const uint32_t elapsed = now - s_movie.start_vblank;
    if (!m->frame_sector) {
        /* Older MOV1 imports have frame order only. */
        const uint64_t frame = (uint64_t)elapsed * 15 / s_movie.hz;
        return frame < m->frames && (frame == s_movie.frame || draw_movie_frame((uint32_t)frame));
    }

    const PeImpMovieClip *clip = &s_movie.clip;
    uint32_t sector = (uint32_t)((uint64_t)elapsed * PE_IMP_MOVIE_SECTOR_HZ / s_movie.hz);
    if (s_movie.audio) {
        const int queued = SDL_GetAudioStreamQueued(s_movie.audio);
        if (queued >= 0 && queued <= s_movie.audio_bytes) {
            /* The stream reports queued source bytes, before device resampling. */
            const unsigned frames = (unsigned)(s_movie.audio_bytes - queued) /
                                    (s_movie.audio_channels * sizeof(int16_t));
            sector = frames / (s_movie.audio_rate / PE_IMP_MOVIE_SECTOR_HZ);
        } else {
            SDL_DestroyAudioStream(s_movie.audio);
            s_movie.audio = NULL;
            s_movie.start_vblank = now - s_movie.sector * s_movie.hz / PE_IMP_MOVIE_SECTOR_HZ;
            sector = s_movie.sector;
        }
    }
    if (sector >= clip->end_sector - clip->start_sector) return 0;
    if (sector < s_movie.sector) sector = s_movie.sector;
    s_movie.sector = sector;
    const uint32_t source_sector = sector + clip->start_sector;
    if (source_sector < m->frame_sector[clip->first_frame]) return 1;
    uint32_t frame = s_movie.frame == UINT32_MAX ? clip->first_frame : s_movie.frame;
    while (frame + 1 < clip->end_frame && m->frame_sector[frame + 1] <= source_sector) frame++;
    return frame == s_movie.frame || draw_movie_frame(frame);
}

void pe_re_stage_movie_stop(void) {
    if (!s_movie.asset) return;
    if (s_movie.native_picture) upload_movie_picture(s_movie.native_picture);
    else if (s_show && s_picture) upload_movie_picture(s_picture);
    clear_movie();
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

/* A person standing at x, z on floor height y is in the picture: their feet or their chest is. */
int pe_re_stage_in_view(double x, double y, double z) {
    if (!s_show || !s_camera.distance) return 0;
    for (int i = 0; i < 2; i++) {
        double screen[3];
        if (project_field_point(x, y - 1500.0 * i, z, screen) && screen[0] >= 0 && screen[0] < PIC_W &&
            screen[1] >= 0 && screen[1] < FIELD_H)
            return 1;
    }
    return 0;
}

static void free_field_mesh(unsigned kind) {
    pe_imp_model_free(&s_field_mesh[kind].model);
    pe_imp_animation_free(&s_field_mesh[kind].moves);
    memset(&s_field_mesh[kind], 0, sizeof s_field_mesh[kind]);
}

static void clear_field_texture(void) {
    if (s_texture_kind >= 0) {
        const RECT areas[] = {{FIELD_TEX_X, FIELD_TEX_Y, FIELD_TEX_WORDS, FIELD_TEX_HEIGHT},
                              {FIELD_CLUT_X, FIELD_CLUT_Y, 256, s_texture_palette_rows}};
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

static int ledge_camera(void) {
    PeImpImageSet *set = NULL;
    char why[160];
    if (pe_imp_catalog_imageset(s_store, s_walk.room_id, &set, why, sizeof why)) return s_view;
    PeImpRoomView current;
    if (pe_imp_room_view(set, (uint32_t)s_view, &current)) {
        free(set);
        return s_view;
    }
    int best = s_view;
    double score = HUGE_VAL;
    const double yaw = s_walk.yaw * FIELD_TURN_RADIANS;
    for (uint32_t i = 0; i < set->count; i++) {
        PeImpRoomView view;
        if (pe_imp_room_view(set, i, &view)) continue;
        prepare_camera(&view);
        double start[3], end[3];
        if (!s_camera.distance ||
            !project_field_point(s_walk.x, s_walk.y - 1000, s_walk.z, start) ||
            !project_field_point(s_walk.x - 1000 * sin(yaw), s_walk.y - 2800,
                                 s_walk.z - 1000 * cos(yaw), end)) continue;
        const double distance = fmax(hypot(start[0] - PIC_W / 2, start[1] - FIELD_H / 2),
                                     hypot(end[0] - PIC_W / 2, end[1] - FIELD_H / 2));
        if (distance < score) best = (int)i, score = distance;
    }
    prepare_camera(&current);
    free(set);
    return best;
}

int pe_re_stage_to_ledge(int *camera) {
    if (!s_walk.active || s_climb.row.b || s_walk.floor < 0 || s_walk.floor >= 31) return 0;
    const double margin = AYA_ROOM_RADIUS + 40;
    for (unsigned i = 0; i < s_walk.collision.count; i++) {
        const PeRe2Collider *c = &s_walk.collision.entries[i];
        if (!climb_surface(c, s_walk.floor)) continue;
        const double mid_x = (c->x0 + c->x1) / 2.0, mid_z = (c->z0 + c->z1) / 2.0;
        const double x[] = {c->x0 - margin, mid_x, c->x1 + margin, mid_x};
        const double z[] = {mid_z, c->z1 + margin, mid_z, c->z0 - margin};
        static const int dx[] = {1, 0, -1, 0}, dz[] = {0, -1, 0, 1};
        for (int dir = 0; dir < 4; dir++) {
            if (blocked_here(x[dir], z[dir]) || pe_imp_re2_blocked_on(&s_walk.collision,
                (unsigned)s_walk.floor + 1, x[dir] + 1000 * dx[dir], z[dir] + 1000 * dz[dir], AYA_ROOM_RADIUS)) continue;
            s_walk.x = x[dir], s_walk.z = z[dir], s_walk.yaw = pe_re_facing_of(dir * 1024);
            s_walk.ghost = 0;
            walk_input(0, 0, 0);
            *camera = ledge_camera();
            return 1;
        }
    }
    return 0;
}

static int empty_texture_area(RECT *rect) {
    DrawSync(0);
    StoreImage(rect, s_vram_check);
    DrawSync(0);
    for (int i = 0; i < rect->w * rect->h; i++)
        if (s_vram_check[i]) return 0;
    return 1;
}

static const PeReSceneActor *scene_actor_for(const PeReEnemyPlacement *p) {
    for (int i = 0; i < s_scene_count; i++)
        if (s_scene_actors[i].slot == p->slot && s_scene_actors[i].kind == p->kind) return &s_scene_actors[i];
    return NULL;
}

static int show_placement_mesh(const PeReEnemyPlacement *p) {
    /* Kendo is a room-script actor above the enemy model range. */
    if (p->kind == PE_RE_KENDO || scene_actor_for(p)) return 1;
    return p->kind < 0x40 && (pe_re_scene_kind(p->kind) || s_placement_meshes == PE_RE_MESH_ALL ||
           (s_placement_meshes == PE_RE_MESH_ENCOUNTERS &&
            ((p->kind >= 0x10 && p->kind < 0x20) || pe_re2_proximity_enemy(p, 0))));
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
        if (model->parts && model->h <= FIELD_TEX_HEIGHT * 2 && model->pal_rows &&
            model->pal_rows <= FIELD_CLUT_ROWS) {
            kind = (int)candidate;
            break;
        }
    }
    if (kind < 0) {               /* no mesh loaded yet (they load as placements show): next frame */
        s_texture_checked = 0;
        return;
    }
    RECT texture = {FIELD_TEX_X, FIELD_TEX_Y, FIELD_TEX_WORDS, FIELD_TEX_HEIGHT};
    const PeImpModel *model = &s_field_mesh[kind].model;
    if (!empty_texture_area(&texture)) return;
    RECT clut = {0, 0, 256, model->pal_rows};
    int spot = -1;
    for (int i = 0; spot < 0 && i < (int)(sizeof k_field_clut_spots / sizeof *k_field_clut_spots); i++) {
        if ((int)model->pal_rows > k_field_clut_spots[i].rows) continue;
        clut.x = k_field_clut_spots[i].x, clut.y = k_field_clut_spots[i].y;
        if (empty_texture_area(&clut)) spot = i;
    }
    if (spot < 0) return;
    s_clut_x = clut.x, s_clut_y = clut.y;
    /* Pack two 128-texel source columns at half resolution into a single
     * checked VRAM rectangle. The full atlas does not fit beside native textures. */
    const unsigned width = model->w < 4 * FIELD_TEX_WORDS ? model->w : 4 * FIELD_TEX_WORDS;
    memset(s_texture_words, 0, sizeof s_texture_words);
    memset(s_texture_clut, 0, sizeof s_texture_clut);
    for (unsigned y = 0; y < model->h; y += 2)
        for (unsigned x = 0; x < width; x += 2)
            s_texture_words[(y / 2) * FIELD_TEX_WORDS + x / 4] |=
                (uint16_t)model->image[y * model->w + x] << (8 * ((x / 2) & 1));
    memcpy(s_texture_clut, model->pal, model->pal_rows * sizeof s_texture_clut[0]);
    LoadImage(&texture, s_texture_words);
    LoadImage(&clut, &s_texture_clut[0][0]);
    DrawSync(0);
    s_texture_palette_rows = model->pal_rows;
    s_texture_kind = kind;
}

static int field_rig_supported(const PeImpModel *model) {
    for (uint32_t b = 0; b < model->bones; b++)
        if (model->parent[b] >= (int)b || (b && model->parent[b] < 0)) return 0;
    for (uint32_t p = 0; p < model->parts && p < model->bones; p++)
        if (model->part[p].tri_skin || model->part[p].quad_skin) return 0;
    return model->bones != 0;
}

static void load_field_moves(unsigned kind, const PeImpModel *model) {
    if (!field_rig_supported(model)) return;
    char id[PE_IMP_ID_MAX], why[160];
    PeImpAnimation animation = {0};
    snprintf(id, sizeof id, "re2:enemy/em%02x/moves", kind);
    if (pe_imp_catalog_animation(s_store, id, &animation, why, sizeof why)) return;
    if (animation.bones == model->bones) {
        s_field_mesh[kind].moves = animation;
        return;
    }
    pe_imp_animation_free(&animation);
}

static void load_field_meshes(void) {
    int wanted[FIELD_MESH_KINDS] = {0};
    for (int i = 0; i < s_placement_count; i++) {
        const unsigned kind = s_placements[i].kind;
        if (show_placement_mesh(&s_placements[i])) wanted[kind] = 1;
    }
    for (unsigned kind = 0; kind < FIELD_MESH_KINDS; kind++) {
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
        load_field_moves(kind, model);
    }
}

static void clear_actor_banks(void) {
    for (unsigned i = 0; i < PE_RE_ENEMIES_MAX; i++) {
        s_actor_bank[i].asset[0] = 0;
        pe_imp_animation_free(&s_actor_bank[i].moves);
    }
}

static const PeImpAnimation *field_actor_moves(const PeReSceneActor *scene, const PeImpModel *model,
                                               const PeImpAnimation *stock) {
    if (!scene->direct || !scene->bank[0] || scene->slot >= PE_RE_ENEMIES_MAX ||
        (!scene->room_clip && scene->move < stock->moves) || !field_rig_supported(model)) return stock;
    const unsigned slot = scene->slot;
    if (strcmp(s_actor_bank[slot].asset, scene->bank)) {
        char why[160];
        pe_imp_animation_free(&s_actor_bank[slot].moves);
        snprintf(s_actor_bank[slot].asset, sizeof s_actor_bank[slot].asset, "%s", scene->bank);
        pe_imp_catalog_animation(s_store, scene->bank, &s_actor_bank[slot].moves, why, sizeof why);
    }
    const PeImpAnimation *room = &s_actor_bank[slot].moves;
    return room->bones == model->bones && scene->move < room->moves ? room : stock;
}

static int on_import_published(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    /* Placements are independent of imported models. Replace the renderer's cache only. */
    clear_actor_banks();
    clear_field_texture();
    for (unsigned kind = 0; kind < FIELD_MESH_KINDS; kind++)
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
        PeM3 rotation;
        re2_rotation(angle, rotation);
        for (int r = 0; r < 3; r++) {
            if (b == 0) {
                memcpy(pose[b].r[r], rotation[r], sizeof rotation[r]);
                /* Field simulation supplies horizontal displacement. Keep the animated bounce. */
                pose[b].t[r] = scene ? frame[r] : model->pos[0][r] + (r == 1 ? (double)frame[r] - first[r] : 0);
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
                              unsigned first_page, unsigned first_palette, unsigned *clut, unsigned *u_offset) {
    const unsigned column = (face->page & 15u) - first_page;
    const unsigned palette = (face->clut >> 6) - first_palette;
    if (column >= 2 || palette >= model->pal_rows) return 0;
    for (int i = 0; i < 3; i++) {
        const unsigned u = face->uv[corners[i]][0], v = face->uv[corners[i]][1];
        if (u >= 128 || column * 128 + u >= model->w || v >= model->h)
            return 0;
    }
    *u_offset = column * 64;
    *clut = (FIELD_CLUT_Y + palette) << 6 | (FIELD_CLUT_X / 16);
    return 1;
}

static void draw_placement_meshes(RenderNativeTag *ordering, int slot, unsigned shade) {
    static const int split[2][3] = {{0, 1, 2}, {1, 3, 2}};
    const uint64_t vblanks = (uint32_t)Psyz_VideoVSync(-1);
    for (int i = 0; i < s_placement_count; i++) {
        const PeReEnemyPlacement *p = &s_placements[i];
        if (!show_placement_mesh(p)) continue;
        const PeReSceneActor *scene = scene_actor_for(p);
        if (!scene && pe_re_scene_kind(p->kind)) continue;
        const PeImpModel *model = &s_field_mesh[p->kind].model;
        const double world_x = scene ? scene->x : p->x;
        const double world_y = scene && !scene->field_pose ? scene->y : p->y - (double)s_field_mesh[p->kind].foot_y;
        const double world_z = scene ? scene->z : p->z;
        /* Imported meshes and Cdir_y already share RE2's +X forward axis.
         * Only Aya's native PE model needs pe_re_facing_of's quarter-turn. */
        const double angle = (scene ? scene->dir : p->dir) * FIELD_TURN_RADIANS;
        const double cs = cos(angle), sn = sin(angle);
        FieldBonePose pose[PE_IMP_MODEL_BONES_MAX];
        const PeImpAnimation *moves = &s_field_mesh[p->kind].moves;
        if (scene) moves = field_actor_moves(scene, model, moves);
        int animated = 0;
        if (scene && !scene->destination_pose) {
            if (scene->move < moves->moves) {
                const PeImpMove *move = &moves->move[scene->move];
                if (move->frames) {
                    const unsigned step = scene->direct ? scene->start_frame + scene->frame * moves->fps / 60u : scene->frame;
                    /* a direct clip holds its last frame unless the script asked it to loop (Sce_flg 0x04) */
                    unsigned frame = (scene->field_pose && !scene->direct) || (scene->direct && scene->loop) ?
                        step % move->frames : step < move->frames ? step : move->frames - 1;
                    if (scene->reverse) frame = move->frames - 1 - frame;
                    field_pose(model, move->data + frame * (3u + 3u * model->bones), move->data,
                               scene->field_pose ? NULL : scene, pose);
                    animated = 1;
                }
            }
        } else if ((s_placement_idle || scene) && moves->moves) {
            const unsigned type = p->behaviour & 0x3f;
            const int zombie = p->kind >= 0x10 && p->kind < 0x20;
            const PeReZombieMode mode = zombie ? pe_re_zombie_mode(type, 0) : PE_RE_ZOMBIE_UNSUPPORTED;
            const int moving = s_last_motion[i] && (uint32_t)vblanks - s_last_motion[i] <= 3;
            /* EM10.C uses move 0 as one of the zombie walk cycles. EM20.C uses move 2 for the dog's run. */
            unsigned index = 0;
            if (p->kind == 0x20) index = moving ? 2 : 0;
            else if (p->kind == 0x24 || pe_re2_ivy(p->kind)) index = moving ? 1 : 0;
            else if (p->kind == 0x25 && !(type & 0xf)) index = moving ? 2 : 0;
            else if (zombie) {
                if (mode == PE_RE_ZOMBIE_CRAWL) index = moving ? 4 : 0x17;
                else if (type == 2 || type == 5) index = 0x16;
                else if (type == 4 || type == 7) index = 0x17;
                else if (type == 8) index = 0x12;
            }
            if (index < moves->moves && moves->move[index].frames) {
                const PeImpMove *move = &moves->move[index];
                const int animate = moving || (zombie &&
                                               (mode == PE_RE_ZOMBIE_IDLE || mode == PE_RE_ZOMBIE_PURSUIT));
                const unsigned frame = animate ? (vblanks * moves->fps / 60 + 7u * p->slot) % move->frames : 0;
                field_pose(model, move->data + frame * (3u + 3u * model->bones), move->data, NULL, pose);
                animated = 1;
            }
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
                        unsigned clut, u_offset;
                        const unsigned first_page = s_field_mesh[p->kind].first_page;
                        const unsigned first_palette = s_field_mesh[p->kind].first_palette;
                        if (s_texture_kind == p->kind &&
                            field_face_texture(model, f, v, first_page, first_palette, &clut, &u_offset)) {
                            FieldTexturedTriangle *packet = &s_tex_packets[slot][i][emitted++];
                            *packet = (FieldTexturedTriangle){
                                .color.bytes = {shade, shade, shade, 0x24},
                                .x0 = (uint16_t)(int)lround(a[0]), .y0 = (uint16_t)(int)lround(a[1]),
                                .x1 = (uint16_t)(int)lround(d[0]), .y1 = (uint16_t)(int)lround(d[1]),
                                .x2 = (uint16_t)(int)lround(c[0]), .y2 = (uint16_t)(int)lround(c[1]),
                                .u0 = u_offset + f->uv[v[0]][0] / 2, .v0 = FIELD_TEX_Y + f->uv[v[0]][1] / 2, .clut = clut,
                                .u1 = u_offset + f->uv[v[1]][0] / 2, .v1 = FIELD_TEX_Y + f->uv[v[1]][1] / 2,
                                .page = 0x80u | (FIELD_TEX_X / 64),
                                .u2 = u_offset + f->uv[v[2]][0] / 2, .v2 = FIELD_TEX_Y + f->uv[v[2]][1] / 2
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

/* Native actor texture pages are live while imported effects are drawn. */
enum { EFFECT_PAGE_Y = 256, EFFECT_FIRST_ROW = 24, EFFECT_PIXEL_END = 240, EFFECT_CLUT_Y = 244,
       EFFECT_BANK_SIZE = 8, EFFECT_PACKET_MAX = 256 };
/* effect pages: 64-wide columns of VRAM's lower half, used where empty; the first two are free
 * under Carnegie Hall's room, the last two also under Central Park's (whose Aya fills 640-767) */
#define EFFECT_PAGES 4
static const int s_effect_page_x[EFFECT_PAGES] = {640, 704, 832, 896};
static const PeImpRoomLogic *s_effect_room;
static PeReEffect s_effects[PE_RE_EFFECT_MAX];
static int s_effect_count;
static struct {
    unsigned height, palettes;
} s_effect_pages[EFFECT_PAGES];
typedef struct {
    PeImpRe2RoomEffect asset;
    uint8_t *frame_data;
    unsigned width, height, v, palette, palette_count, page;
    uint8_t id;
} EffectTexture;
static EffectTexture s_effect_textures[EFFECT_BANK_SIZE];
static unsigned s_effect_texture_count;
static RenderTexturedQuad s_effect_packets[2][EFFECT_PACKET_MAX];

static void clear_effects(int restore) {
    for (unsigned i = 0; i < s_effect_texture_count; i++) free(s_effect_textures[i].frame_data);
    for (int i = 0; i < EFFECT_PAGES; i++) {
        if (restore && s_effect_pages[i].height) {
            RECT pixels = {s_effect_page_x[i], EFFECT_PAGE_Y + EFFECT_FIRST_ROW, 64, s_effect_pages[i].height};
            RECT clut = {s_effect_page_x[i], EFFECT_PAGE_Y + EFFECT_CLUT_Y, 16, s_effect_pages[i].palettes};
            DrawSync(0);
            memset(s_vram_check, 0, sizeof s_vram_check);
            LoadImage(&pixels, s_vram_check);
            LoadImage(&clut, s_vram_check);
            DrawSync(0);
        }
        s_effect_pages[i].height = s_effect_pages[i].palettes = 0;
    }
    s_effect_texture_count = 0;
    s_effect_room = NULL;
    s_effect_count = 0;
}

static unsigned effect_word(const uint8_t *p) {
    return p[0] | (unsigned)p[1] << 8;
}

static void load_effect_texture(EffectTexture *texture, const PeImpRoomLogic *logic) {
    uint32_t size = 0;
    const uint8_t *rdt = pe_imp_logic_section(logic, PE_IMP_LOGIC_RDT0, NULL, &size);
    if (pe_imp_re2_room_effect(rdt, size, texture->id, &texture->asset)) return;
    const uint8_t *tim = texture->asset.tim;
    const unsigned clut_size = effect_word(tim + 8);
    if (tim[4] != 8 || clut_size < 44 || clut_size > texture->asset.tim_size - 20) return;
    const uint8_t *image = tim + 8 + clut_size;
    const unsigned w = effect_word(image + 8), h = effect_word(image + 10);
    const unsigned palettes = (clut_size - 12) / 32;
    if (w > 64) return;
    for (unsigned i = 0; i < EFFECT_PAGES; i++) {
        const unsigned v = EFFECT_FIRST_ROW + s_effect_pages[i].height, p = s_effect_pages[i].palettes;
        if (v + h > EFFECT_PIXEL_END || p + palettes > 256 - EFFECT_CLUT_Y) continue;
        RECT reserved = {s_effect_page_x[i], EFFECT_PAGE_Y + v, 64, h};
        RECT clut = {s_effect_page_x[i], EFFECT_PAGE_Y + EFFECT_CLUT_Y + p, 16, palettes};
        if (!empty_texture_area(&reserved) || !empty_texture_area(&clut)) continue;
        const size_t frames = texture->asset.frame_count * 8, sprites = texture->asset.sprite_count * 4;
        texture->frame_data = malloc(frames + sprites);
        if (!texture->frame_data) return;
        memcpy(texture->frame_data, texture->asset.frames, frames);
        memcpy(texture->frame_data + frames, texture->asset.sprites, sprites);
        texture->asset.frames = texture->frame_data;
        texture->asset.sprites = texture->frame_data + frames;
        texture->asset.tim = NULL;
        RECT pixels = {s_effect_page_x[i], EFFECT_PAGE_Y + v, w, h};
        uint16_t colours[16 * (256 - EFFECT_CLUT_Y)];
        for (unsigned c = 0; c < palettes * 16; c++) {
            const unsigned rgb = effect_word(tim + 20 + c * 2);
            colours[c] = rgb ? rgb | 0x8000 : 0;
        }
        DrawSync(0);
        LoadImage(&pixels, (void *)(image + 12));
        LoadImage(&clut, colours);
        DrawSync(0);
        texture->width = w * 4;
        texture->height = h;
        texture->v = v;
        texture->palette = clut.y;
        texture->palette_count = palettes;
        texture->page = i;
        s_effect_pages[i].height += h;
        s_effect_pages[i].palettes += palettes;
        return;
    }
}

static const EffectTexture *effect_texture(unsigned id) {
    for (unsigned i = 0; i < s_effect_texture_count; i++)
        if (s_effect_textures[i].id == id) return &s_effect_textures[i];
    return NULL;
}

void pe_re_stage_set_effects(const PeImpRoomLogic *logic, const PeReEffect *effects, int count) {
    if (logic != s_effect_room) {
        clear_effects(1);
        s_effect_room = logic;
    }
    for (int i = 0; i < count; i++) {
        if (effect_texture(effects[i].id) || s_effect_texture_count == EFFECT_BANK_SIZE) continue;
        EffectTexture *texture = &s_effect_textures[s_effect_texture_count++];
        *texture = (EffectTexture){.id = effects[i].id};
        load_effect_texture(texture, logic);
        if (!texture->width) pe_mod_log("pe_re_play", "Cannot upload room effect 0x%02x", texture->id);
    }
    memcpy(s_effects, effects, (size_t)count * sizeof *effects);
    s_effect_count = count;
}

static void draw_effects(RenderNativeTag *ordering, int slot, unsigned shade) {
    unsigned emitted = 0;
    for (int i = 0; i < s_effect_count; i++) {
        const PeReEffect *e = &s_effects[i];
        const EffectTexture *texture = effect_texture(e->id);
        if (!texture || !texture->width) continue;
        double screen[3];
        if (!project_field_point(e->x, e->y, e->z, screen)) continue;
        const unsigned phase = e->loop ? (e->age + i * 6) % e->duration : e->age;
        unsigned time = phase / 2;
        PeImpRe2EffectFrame frame;
        unsigned frame_index = 0;
        while (!pe_imp_re2_effect_frame(&texture->asset, frame_index, &frame) &&
               frame.time && frame.time != 0xff && time >= frame.time) {
            time -= frame.time;
            frame_index++;
        }
        if (pe_imp_re2_effect_frame(&texture->asset, frame_index, &frame) || !frame.time || frame.time == 0xff) continue;
        const double scale = s_camera.distance * e->scale / (screen[2] * 4 * RE_UNITS_PER_PE_UNIT);
        const unsigned palette = e->type >> 3;
        for (unsigned n = 0; n < frame.sprite_count && emitted < EFFECT_PACKET_MAX; n++) {
            PeImpRe2EffectSprite sprite;
            if (pe_imp_re2_effect_sprite(&texture->asset, frame.first_sprite + n, &sprite)) continue;
            if (sprite.u >= texture->width || sprite.v >= texture->height) continue;
            unsigned w = frame.size, h = frame.size;
            if (sprite.u + w >= texture->width) w = texture->width - 1 - sprite.u;
            if (sprite.v + h >= texture->height) h = texture->height - 1 - sprite.v;
            if (!w || !h) continue;
            const int x = (int)lround(screen[0] + (sprite.x - frame.origin_x) * scale);
            const int y = (int)lround(screen[1] + (sprite.y - frame.origin_y) * scale);
            const int right = x + (int)lround(w * scale), bottom = y + (int)lround(h * scale);
            if (right <= 0 || bottom <= 0 || x >= PIC_W || y >= FIELD_H || right == x || bottom == y) continue;
            RenderTexturedQuad *q = &s_effect_packets[slot][emitted++];
            *q = (RenderTexturedQuad){0};
            q->tag.length = 9;
            q->color.bytes.r = q->color.bytes.g = q->color.bytes.b = shade;
            q->color.bytes.code = 0x2e;
            const unsigned page_x = s_effect_page_x[texture->page];
            q->tpage = (page_x / 64) | (EFFECT_PAGE_Y / 256 << 4) | (1 << 5);
            q->clut = ((texture->palette + (palette < texture->palette_count ? palette : 0)) << 6) | page_x / 16;
            q->x0 = q->x2 = x;
            q->x1 = q->x3 = right;
            q->y0 = q->y1 = y;
            q->y2 = q->y3 = bottom;
            q->u0 = q->u2 = sprite.u;
            q->u1 = q->u3 = sprite.u + w;
            q->v0 = q->v1 = texture->v + sprite.v;
            q->v2 = q->v3 = texture->v + sprite.v + h;
            const int depth = (int)screen[2];
            link_quad(&ordering[depth > FIELD_UI_OT_LAST ? depth : FIELD_UI_OT_LAST + 1], q);
        }
    }
}
/* Battle marks (docs/RE_PLAY_BATTLES.md): the chosen target, drawn only while it is off the picture, as
 * an arrow at the picture's edge toward it (D10); and a joining enemy's cue, over it, or at the edge
 * while it is out of the picture (D19). Positions are PE's (host units), taken into the room. */

static void draw_marks(RenderNativeTag *ordering, int slot) {
    const double k = RE_UNITS_PER_PE_UNIT, cx = PIC_W / 2.0, cy = FIELD_H / 2.0;
    if (!s_camera.distance) return;
    for (int i = 0; i < PE_RE_STAGE_MARKS; i++) {
        if (!s_marks[i].kind) continue;
        const double point[3] = {s_backdrop.room[0] + (s_marks[i].x - s_backdrop.anchor[0]) * k,
                                 s_backdrop.room[1] - 1800.0,
                                 s_backdrop.room[2] + (s_marks[i].z - s_backdrop.anchor[2]) * k};
        double view[3] = {0};
        for (int row = 0; row < 3; row++)
            for (int col = 0; col < 3; col++)
                view[row] += s_camera.view.m[row][col] * (point[col] - s_camera.eye[col]) / 4096.0;
        double dx = view[0], dy = view[1];
        if (view[2] > 1) dx = s_camera.distance * view[0] / view[2], dy = s_camera.distance * view[1] / view[2];
        const int on = view[2] > 1 && fabs(dx) < cx - 4 && fabs(dy) < cy - 4;
        if (s_marks[i].kind == PE_RE_MARK_TARGET && on) continue;
        double ex, ey, ux, uy;                          /* where, and which way the arrow points */
        if (on) {
            ex = cx + dx, ey = cy + dy - 8, ux = 0, uy = 1;
        } else {
            const double len = sqrt(dx * dx + dy * dy);
            if (len < 1e-6) continue;
            ux = dx / len, uy = dy / len;
            const double tx = fabs(ux) > 1e-6 ? (cx - 10) / fabs(ux) : 1e9, ty = fabs(uy) > 1e-6 ? (cy - 10) / fabs(uy) : 1e9;
            const double t = tx < ty ? tx : ty;
            ex = cx + ux * t, ey = cy + uy * t;
        }
        const int cue = s_marks[i].kind == PE_RE_MARK_CUE;
        RenderColorTrianglePacket *p = &s_mark_packets[slot][i];
        *p = (RenderColorTrianglePacket){
            .color.bytes = {cue ? 255 : 250, cue ? 210 : 60, cue ? 40 : 60, 0x20},
            .x0 = (u16)lround(ex + ux * 6), .y0 = (u16)lround(ey + uy * 6),
            .x1 = (u16)lround(ex - ux * 4 - uy * 5), .y1 = (u16)lround(ey - uy * 4 + ux * 5),
            .x2 = (u16)lround(ex - ux * 4 + uy * 5), .y2 = (u16)lround(ey - uy * 4 - ux * 5),
        };
        link_field_triangle(&ordering[FIELD_UI_OT_LAST + 1], p);
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
        /* D8: the room's effects and its foreground in front of the battle, as in the field (the
         * enemy packs keep out of their VRAM: pe_re_stage_vram_reserved) */
        draw_effects((RenderNativeTag *)b->ordering, b->slot, 128);
        if (s_foreground)
            for (size_t i = 0; i < s_mask_count; i++) {
                RenderTexturedQuad *m = &s_masks[i].quad[b->slot];
                m->color.bytes.r = m->color.bytes.g = m->color.bytes.b = 128;
                link_quad(&((RenderNativeTag *)b->ordering)[s_masks[i].depth], m);
            }
        draw_marks((RenderNativeTag *)b->ordering, b->slot);
        return 1;
    }
    if (!s_show && !s_movie.asset) return 0;
    if (b->stage == 0) return 1;                       /* skip the PE room's own tiles */
    if (b->slot < 0 || b->slot > 1 || !b->ordering) return 0;
    RenderNativeTag *ordering = b->ordering;
    RenderTexturedQuad *quads = s_quads[b->slot];
    if (s_movie.asset) {
        picture_quad(&quads[0], 0, PIC_TOP, PIC_SPLIT, PIC_TOP + FIELD_H);
        picture_quad(&quads[1], PIC_SPLIT, PIC_TOP, PIC_W, PIC_TOP + FIELD_H);
        link_quad(&ordering[0], &quads[0]);
        link_quad(&ordering[0], &quads[1]);
        return 1;
    }
    const u8 shade = (u8)(s_shade < 0 ? 0 : s_shade > 128 ? 128 : s_shade);   /* the texture times shade / 128 */
    picture_quad(&quads[0], 0, PIC_TOP, PIC_SPLIT, PIC_TOP + FIELD_H);
    picture_quad(&quads[1], PIC_SPLIT, PIC_TOP, PIC_W, PIC_TOP + FIELD_H);
    for (int k = 0; k < 2; k++) quads[k].color.bytes.r = quads[k].color.bytes.g = quads[k].color.bytes.b = shade;
    link_quad(&ordering[ROOM_BACKGROUND_OT], &quads[0]);
    link_quad(&ordering[ROOM_BACKGROUND_OT], &quads[1]);
    if (!s_hold) draw_effects(ordering, b->slot, shade);
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
    clear_movie();
    backdrop_off(0);                                   /* a new room's pages and camera are coming */
    clear_climb();
    clear_effects(0);
    clear_field_texture();
    if (s_masks) DrawSync(0);
    free_masks();
    free(s_original_picture);
    s_original_picture = NULL;
    s_show = s_walk.active = 0;
    return 0;
}

void pe_re_stage_hide(void) {
    pe_re_stage_movie_stop();
    clear_actor_banks();
    pe_re_stage_scene_controls(0, 0);
    s_shade = 128, s_curtain = s_hold = 0;
    clear_climb();
    clear_effects(1);
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
    const uint32_t now = (uint32_t)Psyz_VideoVSync(-1);
    int changed = count != s_placement_count;
    for (int i = 0; i < count; i++) {
        const PeReEnemyPlacement *old = &s_placements[i], *next = &placements[i];
        if (i >= s_placement_count || old->slot != next->slot || old->kind != next->kind ||
            old->killed_flag != next->killed_flag) {
            changed = 1;
            s_last_motion[i] = 0;
            continue;
        }
        if (show_placement_mesh(old) != show_placement_mesh(next)) changed = 1;
        if (old->x != next->x || old->z != next->z) s_last_motion[i] = now;
    }
    if (changed) clear_field_texture();
    s_placement_count = count;
    if (count) memcpy(s_placements, placements, (size_t)count * sizeof *placements);
    if (!count) s_scene_count = 0;
    if (changed) load_field_meshes();
}
void pe_re_stage_set_scene_actors(const PeReSceneActor *actors, int count) {
    if (count > PE_RE_ENEMIES_MAX) count = PE_RE_ENEMIES_MAX;
    int changed = count != s_scene_count;
    for (int i = 0; i < count && !changed; i++)
        changed = s_scene_actors[i].slot != actors[i].slot || s_scene_actors[i].kind != actors[i].kind;
    if (count) memcpy(s_scene_actors, actors, (size_t)count * sizeof *actors);
    s_scene_count = count;
    if (changed) {
        clear_field_texture();
        load_field_meshes();
    }
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
const PeRe2RoomCollision *pe_re_stage_collision(void) { return &s_walk.collision; }
void pe_re_stage_collision_id(unsigned slot, unsigned id) {
    pe_imp_re2_collision_set_id(&s_walk.collision, slot, id);
    /* A scene can enable a collider around Aya after placing her. Let her walk out. */
    if (s_walk.active && (id & 0x8000) && blocked_here(s_walk.x, s_walk.z))
        s_walk.ghost = 1;
}
void pe_re_stage_place(double x, double z) {
    s_walk.x = x, s_walk.z = z;
    s_walk.ghost = blocked_here(x, z);
}
void pe_re_stage_set_masks(int on) { s_foreground = on; }

void pe_re_stage_set_mask_groups(const uint8_t groups[16]) {
    if (!s_show || !memcmp(groups, s_mask_groups, sizeof s_mask_groups)) return;
    PeImpImageSet *set = NULL;
    char why[160];
    if (pe_imp_catalog_imageset(s_store, s_walk.room_id, &set, why, sizeof why)) return;
    PeImpRoomView view;
    if (!pe_imp_room_view(set, (uint32_t)s_view, &view)) {
        memcpy(s_mask_groups, groups, sizeof s_mask_groups);
        DrawSync(0);
        free_masks();
        build_masks(set, &view, &s_masks, &s_mask_count);
    }
    free(set);
}
void pe_re_stage_scene_controls(int locked, int move_mode) {
    /* The PE field banks already have Aya's standing, walking and running poses. */
    const int pose = !move_mode ? 21 : move_mode == 5 ? 23 : 22;
    s_scene_frame = locked && s_scene_locked && pose == s_scene_pose ? s_scene_frame + 1 : 0;
    s_scene_locked = locked;
    s_scene_pose = pose;
    if (locked && s_movie.asset && !s_show) g_FieldPadPressed = 0;
    else if (locked) g_FieldPadPressed &= ~0x80u;  /* the native Aya inventory shortcut */
}
void pe_re_stage_fade(int shade, int curtain, int hold) { s_shade = shade, s_curtain = curtain, s_hold = hold; }
void pe_re_stage_set_size(int percent) { if (percent != s_size) s_size = percent, pe_re_scale(); }
double pe_re_stage_scale(void) { return s_scale; }

void pe_re_stage_init(const char *mod_id, const char *store) {
    s_store = store, s_scale = 3;
    memset(s_mask_groups, 0xff, sizeof s_mask_groups);
    pe_imp_mdec_set_table(MdecDiag_ExpandDecoderTable);
    pe_hook_add(PE_HOOK_FIELD_BACKGROUND, on_background, NULL, PE_PRIORITY_LATE, mod_id);
    pe_hook_add(PE_HOOK_FIELD_CAMERA, on_camera, NULL, PE_PRIORITY_LATE, mod_id);
    pe_hook_add(PE_HOOK_FIELD_MOVE, on_move, NULL, PE_PRIORITY_LATE, mod_id);
    pe_hook_add(PE_HOOK_ACTOR_RENDER, on_player_scene_render, NULL, PE_PRIORITY_LATE, mod_id);
    pe_hook_add(PE_HOOK_SCENE_START, on_scene, NULL, PE_PRIORITY_LATE, mod_id);
    pe_hook_add(pe_event_id(PE_EVENT_IMPORT_PUBLISHED), on_import_published, NULL, PE_PRIORITY_NORMAL, mod_id);
}

void pe_re_stage_shutdown(void) {
    pe_re_stage_movie_stop();
    clear_actor_banks();
    s_show = s_walk.active = 0;
    clear_climb();
    clear_effects(1);
    clear_field_texture();
    for (unsigned kind = 0; kind < FIELD_MESH_KINDS; kind++)
        if (s_field_mesh[kind].tried) free_field_mesh(kind);
    free_masks();
    free(s_original_picture);
    s_original_picture = NULL;
    backdrop_off(0);
    s_backdrop.armed = 0;
    free(s_picture);
    s_picture = NULL;
}

/* G2: the VRAM the room's drawing uses now, so a battle's enemy packs keep out of it: the picture,
 * the effect pages in use and their palettes, the field meshes' texture and palettes. Returns how
 * many rectangles (x, y, w, h) went to `out`, at most `max`. */
int pe_re_stage_vram_reserved(int *out, int max) {
    int n = 0;
#define RESERVE(X, Y, W, H) do { if (n < max) { out[n * 4] = (X), out[n * 4 + 1] = (Y), out[n * 4 + 2] = (W), out[n * 4 + 3] = (H); n++; } } while (0)
    if (!s_show) return 0;
    RESERVE(VRAM_X, VRAM_Y, PIC_W, PIC_H);
    for (int i = 0; i < EFFECT_PAGES; i++)
        if (s_effect_pages[i].height) {
            RESERVE(s_effect_page_x[i], EFFECT_PAGE_Y + EFFECT_FIRST_ROW, 64, (int)s_effect_pages[i].height);
            RESERVE(s_effect_page_x[i], EFFECT_PAGE_Y + EFFECT_CLUT_Y, 16, (int)s_effect_pages[i].palettes);
        }
    if (s_texture_kind >= 0) {
        RESERVE(FIELD_TEX_X, FIELD_TEX_Y, FIELD_TEX_WORDS, FIELD_TEX_HEIGHT);
        RESERVE(FIELD_CLUT_X, FIELD_CLUT_Y, 256, (int)s_texture_palette_rows);
    }
#undef RESERVE
    return n;
}

/* The backdrop's mapping: room = room_at_start + (host - anchor) * k, with the anchor where Aya
 * settled for the battle (backdrop_camera uses the same). */
void pe_re_stage_battle_mark(int id, int kind, double x, double z) {
    if (id < 0 || id >= PE_RE_STAGE_MARKS) return;
    s_marks[id].kind = s_backdrop.on ? kind : 0, s_marks[id].x = x, s_marks[id].z = z;
}

int pe_re_stage_battle_point(int to_room, double x, double z, double *out_x, double *out_z) {
    const double k = RE_UNITS_PER_PE_UNIT;
    if (!s_backdrop.on || !(k > 0)) return -1;
    if (to_room) {
        *out_x = s_backdrop.room[0] + (x - s_backdrop.anchor[0]) * k;
        *out_z = s_backdrop.room[2] + (z - s_backdrop.anchor[2]) * k;
    } else {
        *out_x = s_backdrop.anchor[0] + (x - s_backdrop.room[0]) / k;
        *out_z = s_backdrop.anchor[2] + (z - s_backdrop.room[2]) / k;
    }
    return 0;
}
