/* RE rooms: the room runtime. It reads only what the importer stored (RoomLogic, room ImageSets),
 * never a disc. Design: the design doc "RE rooms in Parasite Eve".
 *
 * Prototype stage: F1 > RE rooms shows an imported room's camera view as the picture of the PE
 * room Aya is in. PE's own background tiles are skipped (PE_HOOK_FIELD_BACKGROUND stage 0) and the
 * RE picture is drawn behind everything (stage 1): uploaded to VRAM as a 15-bit image where PE
 * keeps its background pages (320,256)-(639,495), then two textured quads at the far end of the
 * ordering table. For RE2, the native actor renderer uses the selected RID camera, and
 * the foreground cutouts are linked at their PRI depths. With ground-floor SCA
 * data, movement advances a separate position in RE2 room coordinates.
 * Desktop only for now. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp.h"
#include "pe_imp_logic.h"
#include "pe_imp_mdec.h"
#include "pe_imp_room.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
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

#define MOD_ID "pe_rooms"
#define PAGE "RE rooms"
#define ROOMS_MAX 512
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

static char s_store[1100];
static PeImpAsset *s_rooms;
static int s_nrooms, s_listed, s_pick, s_view, s_show;
static int s_foreground = 1;
static char s_status[200] = "Off.";
static RenderTexturedQuad s_quads[2][2];               /* per draw slot: left and right halves */
static uint16_t *s_original_picture;
static RoomMaskPacket *s_masks;
static size_t s_mask_count;

static void free_masks(void) {
    free(s_masks);
    s_masks = NULL;
    s_mask_count = 0;
}

static struct {
    GteMatrix view;
    double origin[3];   /* PE-space camera-to-preview-origin translation */
    double eye[3];
    double right_x, right_z;
    int distance;
    int anchored;
} s_camera;

static struct {
    PeRe2RoomCollision collision;
    char room[PE_IMP_ID_MAX];
    double x, z;
    int yaw;
    int active;
} s_walk;

/* RE2 RID stores distance in the upper half of the projection word, in Q7 pixels.
 * Build a right/down/forward basis in the games' shared Y-down convention. */
static void prepare_camera(const PeImpRoomView *view) {
    s_camera.distance = 0;
    s_camera.anchored = 0;
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

    /* Place the preview origin along the ray to (160,160) on the 320x240 image.
     * Prefer its intersection with RE floor zero. Some views look across upper
     * floors or up from below, so use the target depth for those previews. */
    double slope = 40.0 / distance;
    double ray_y = basis[2][1] + slope * basis[1][1];
    double depth = fabs(ray_y) > 0.0001 ? -(double)view->eye[1] / ray_y : 0;
    /* Preview scale. PE models use smaller units than RE2's room coordinates. */
    /* Keep room for movement and actor geometry within PE's 4096-entry depth table. */
    const double max_depth = 12000 * RE_UNITS_PER_PE_UNIT;
    if (depth < 2200 || depth > max_depth)
        depth = fmin(max_depth, fmax(2200, length));
    s_camera.origin[0] = 0;
    s_camera.origin[1] = depth * slope / RE_UNITS_PER_PE_UNIT;
    s_camera.origin[2] = depth / RE_UNITS_PER_PE_UNIT;
    s_camera.distance = distance;
}

static int load_collision(const PeImpImageSet *set, const PeImpAsset *room) {
    const char *number = strchr(room->id, '/') + 1;
    /* Images are shared by discs. Prefer the scenario of the catalog's
     * selected image, then the other scenario if its room is all we have. */
    int player = 0;
    PeImpAsset *assets;
    int count = pe_imp_catalog_assets(s_store, "re2", &assets);
    char id[PE_IMP_ID_MAX];
    snprintf(id, sizeof id, "re2:logic/%s-1", number);
    for (int i = 0; i < count; i++) {
        if (!strcmp(assets[i].id, id) && !strcmp(assets[i].sha256, room->sha256)) {
            player = 1;
            break;
        }
    }
    free(assets);
    for (int attempt = 0; attempt < 2; attempt++) {
        PeImpRoomLogic *logic;
        snprintf(id, sizeof id, "re2:logic/%s-%d", number, player ^ attempt);
        if (pe_imp_catalog_roomlogic(s_store, id, &logic, NULL, 0)) continue;
        uint32_t size = 0;
        const uint8_t *data = pe_imp_logic_section(logic, PE_IMP_LOGIC_COLL, NULL, &size);
        int rc = pe_imp_re2_collision(data, size, &s_walk.collision);
        free(logic);
        if (!rc) return 0;
    }
    return pe_imp_re2_image_collision(set, &s_walk.collision);
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

static void prepare_walk(const PeImpImageSet *set, const PeImpAsset *room) {
    if (s_camera.distance && s_walk.active && !strcmp(s_walk.room, room->id)) return;
    s_walk.active = 0;
    if (!s_camera.distance || load_collision(set, room) || !spawn_on_floor()) return;
    snprintf(s_walk.room, sizeof s_walk.room, "%s", room->id);
    s_walk.active = 1;
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
            pe_imp_re2_slide(&s_walk.collision, &s_walk.x, &s_walk.z, dx, dz, AYA_ROOM_RADIUS);
        }
    }
    return 1;
}

static int on_camera(void *ctx, void *user) {
    (void)user;
    PeHookFieldCamera *camera = ctx;
    const FieldActor *aya = camera->player;
    if (!s_show || !s_camera.distance || !aya) return 0;
    if (s_walk.active) {
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
    } else if (!s_camera.anchored) {
        /* Keep the PE simulation at its original coordinates. Bake the PE
         * starting position into the view translation once per selection. */
        double pos[3] = {aya->pos_x / 65536.0, aya->pos_y / 65536.0, aya->pos_z / 65536.0};
        for (int row = 0; row < 3; row++) {
            const s16 *m = s_camera.view.m[row];
            s_camera.view.t[row] = (s32)lround(s_camera.origin[row] -
                 (m[0] * pos[0] + m[1] * pos[1] + m[2] * pos[2]) / 4096.0);
        }
        s_camera.anchored = 1;
    }
    *(GteMatrix *)camera->matrix = s_camera.view;
    camera->distance = s_camera.distance;
    return 1;
}

static void list_rooms(void) {
    if (s_listed) return;
    s_listed = 1;
    static const char *const games[] = {"re1", "re2", "re3"};
    free(s_rooms);
    s_rooms = calloc(ROOMS_MAX, sizeof *s_rooms);
    s_nrooms = 0;
    for (unsigned g = 0; s_rooms && g < 3; g++) {
        PeImpAsset *a;
        int n = pe_imp_catalog_assets(s_store, games[g], &a);
        for (int i = 0; i < n && s_nrooms < ROOMS_MAX; i++)
            if (!strcmp(a[i].type, "ImageSet") && !strncmp(strchr(a[i].id, ':') + 1, "room/", 5)) s_rooms[s_nrooms++] = a[i];
        free(a);
    }
}

static unsigned room_count(void) { list_rooms(); return s_nrooms ? (unsigned)s_nrooms : 1; }
static const char *room_name(unsigned i) {
    static char name[96];
    if (i >= (unsigned)s_nrooms) return "No rooms imported: scan an RE disc in F1 > Import";
    snprintf(name, sizeof name, "%s (%.*s)", s_rooms[i].name, (int)(strchr(s_rooms[i].id, ':') - s_rooms[i].id), s_rooms[i].id);
    return name;
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
static int build_masks(const PeImpImageSet *set, const PeImpRoomView *view,
                       RoomMaskPacket **out, size_t *count) {
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
            while (end_y < PIC_TOP + FIELD_H &&
                   !memcmp(depths + y * PIC_W + x, depths + end_y * PIC_W + x, width))
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
            for (int row = y; row < end_y; row++)
                memset(depths + row * PIC_W + x, 0xff, width);
            x = end_x - 1;
        }
    }
done:
    free(depths);
    return rc;
}

/* decode the chosen view and put it in VRAM; 0 or -1 (s_status says why) */
static int upload(void) {
    if (s_pick >= s_nrooms) { snprintf(s_status, sizeof s_status, "No room chosen."); return -1; }
    char why[200] = "";
    PeImpImageSet *set;
    if (pe_imp_catalog_imageset(s_store, s_rooms[s_pick].id, &set, why, sizeof why)) {
        snprintf(s_status, sizeof s_status, "%s", why);
        return -1;
    }
    int rc = -1;
    uint32_t size;
    const uint8_t *frame = pe_imp_imageset_frame(set, (uint32_t)s_view, &size);
    uint8_t *rgb = malloc(PIC_W * PIC_H * 3);
    uint16_t *pix = malloc(PIC_W * PIC_H * 2);
    if (!frame) snprintf(s_status, sizeof s_status, "%s has %u views.", s_rooms[s_pick].name, set->count);
    else if (!rgb || !pix) snprintf(s_status, sizeof s_status, "Out of memory.");
    else if (set->width != PIC_W || set->height != PIC_H) snprintf(s_status, sizeof s_status, "Not a 320 x 240 room picture.");
    else if (pe_imp_mdec_decode(frame, size, PIC_W, PIC_H, rgb, why, sizeof why)) snprintf(s_status, sizeof s_status, "%s", why);
    else {
        for (int i = 0; i < PIC_W * PIC_H; i++) {
            unsigned r = rgb[i * 3] >> 3, g = rgb[i * 3 + 1] >> 3, b = rgb[i * 3 + 2] >> 3;
            uint16_t v = (uint16_t)(r | g << 5 | b << 10);
            pix[i] = v ? v : 0x8000;                    /* 0 would be see-through: keep black opaque */
        }
        RECT r = {VRAM_X, VRAM_Y, PIC_W, PIC_H};
        if (!s_original_picture) {
            s_original_picture = malloc(PIC_W * PIC_H * sizeof *s_original_picture);
            if (!s_original_picture) {
                snprintf(s_status, sizeof s_status, "Out of memory.");
                goto cleanup;
            }
            DrawSync(0);
            StoreImage(&r, s_original_picture);
            DrawSync(0);
        }
        LoadImage(&r, pix);
        DrawSync(0);
        PeImpRoomView view;
        if (pe_imp_room_view(set, (uint32_t)s_view, &view)) view.game = 0;
        prepare_camera(&view);
        prepare_walk(set, &s_rooms[s_pick]);
        /* DrawSync above retired any packets for the previous view. */
        free_masks();
        int masks = build_masks(set, &view, &s_masks, &s_mask_count);
        rc = 0;
        const char *foreground = s_mask_count ? ", foreground" : masks < 0 ? ", foreground unavailable" : "";
        snprintf(s_status, sizeof s_status, "Showing %s, view %d (%s camera%s).",
                 s_rooms[s_pick].name, s_view, s_camera.distance ? "RE2" : "PE", foreground);
    }
cleanup:
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
    if (s_foreground) {
        for (size_t i = 0; i < s_mask_count; i++)
            link_quad(&ordering[s_masks[i].depth], &s_masks[i].quad[b->slot]);
    }
    return 1;
}

static int on_scene(void *ctx, void *user) {
    (void)ctx, (void)user;
    /* The new room has already uploaded its own pages. */
    if (s_masks) DrawSync(0);
    free_masks();
    free(s_original_picture);
    s_original_picture = NULL;
    s_show = 0;
    s_walk.active = 0;
    snprintf(s_status, sizeof s_status, "Off.");
    return 0;
}

static void show(void) {
    list_rooms();
    if (!upload()) s_show = 1;
}
static void hide(void) {
    if (s_original_picture) {
        RECT r = {VRAM_X, VRAM_Y, PIC_W, PIC_H};
        DrawSync(0);
        LoadImage(&r, s_original_picture);
        DrawSync(0);
        free(s_original_picture);
        s_original_picture = NULL;
    }
    free_masks();
    s_show = 0;
    s_walk.active = 0;
    snprintf(s_status, sizeof s_status, "Off.");
}
static void refresh(void) { s_listed = 0; list_rooms(); }
static void text_status(char *out, unsigned size) {
    if (s_walk.active)
        snprintf(out, size, "%s Ground floor x %.0f, z %.0f.", s_status, s_walk.x, s_walk.z);
    else
        snprintf(out, size, "%s", s_status);
}

static int init(void) {
    if (pe_mod_path(s_store, sizeof s_store, "import/store")) return 1;
    pe_imp_mdec_set_table(MdecDiag_ExpandDecoderTable);
    pe_menu_begin(PAGE);
    pe_menu_top_page(PAGE, "Preview an imported room with Aya. RE2 views use their original camera.", 62);
    pe_menu_register_dynamic_choice(0, "Room", "An imported room (F1 > Import scans them).", &s_pick, ROOMS_MAX,
                                    room_count, room_name);
    pe_menu_register_int(0, "Camera view", "Which of the room's camera views to show.", &s_view, 0, 31, 1);
    pe_menu_register_toggle(0, "Foreground masks", "Let foreground objects cover actors in RE2 views.",
                            &s_foreground, NULL);
    pe_menu_register_action(0, "Show it here", "Shows the selected picture and, for RE2, its camera and foreground.", show);
    pe_menu_register_action(0, "Stop showing it", "Restore the current PE room and camera.", hide);
    pe_menu_register_action(0, "Refresh the list", "Lists rooms imported since the game started.", refresh);
    pe_menu_register_info(0, "Status", "What's shown.", text_status);
    pe_hook_add(PE_HOOK_FIELD_BACKGROUND, on_background, NULL, PE_PRIORITY_LATE, MOD_ID);
    pe_hook_add(PE_HOOK_FIELD_CAMERA, on_camera, NULL, PE_PRIORITY_LATE, MOD_ID);
    pe_hook_add(PE_HOOK_FIELD_MOVE, on_move, NULL, PE_PRIORITY_LATE, MOD_ID);
    pe_hook_add(PE_HOOK_SCENE_START, on_scene, NULL, PE_PRIORITY_LATE, MOD_ID);
    return 0;
}

static void shutdown_rooms(void) {
    s_show = 0;
    free_masks();
    free(s_original_picture);
    s_original_picture = NULL;
    free(s_rooms);
    s_rooms = NULL;
}

const PeMod pe_mod_pe_rooms = {MOD_ID, "RE rooms (prototype)", "0.1", NULL, init, shutdown_rooms};
