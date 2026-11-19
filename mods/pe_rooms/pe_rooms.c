/* RE rooms: the room runtime. It reads only what the importer stored (RoomLogic, room ImageSets),
 * never a disc. Design: the design doc "RE rooms in Parasite Eve".
 *
 * Prototype stage: F1 > RE rooms shows an imported room's camera view as the picture of the PE
 * room Aya is in. PE's own background tiles are skipped (PE_HOOK_FIELD_BACKGROUND stage 0) and the
 * RE picture is drawn behind everything (stage 1): uploaded to VRAM as a 15-bit image where PE
 * keeps its background pages (320,256)-(639,495), then two textured quads at the far end of the
 * ordering table. Aya still walks on the PE room's floor with the PE camera; the RE camera,
 * collision, masks, doors and scripts come next. Turning it off leaves the PE room's pages
 * overwritten until the next room loads. Desktop only for now. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_imp.h"
#include "pe_imp_mdec.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe1/boot_display.h"
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

static char s_store[1100];
static PeImpAsset *s_rooms;
static int s_nrooms, s_listed, s_pick, s_view, s_show;
static int s_uploaded = -1, s_uploaded_view = -1;      /* what VRAM holds: room index and view */
static char s_status[200] = "Off.";
static RenderTexturedQuad s_quads[2][2];               /* per draw slot: left and right halves */

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

/* decode the chosen view and put it in VRAM; 0 or -1 (s_status says why) */
static int upload(void) {
    if (s_uploaded == s_pick && s_uploaded_view == s_view) return 0;
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
    if (!frame) snprintf(s_status, sizeof s_status, "%s has %u views (0-%u).", s_rooms[s_pick].name, set->count, set->count - 1);
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
        LoadImage(&r, pix);
        DrawSync(0);
        s_uploaded = s_pick, s_uploaded_view = s_view, rc = 0;
        snprintf(s_status, sizeof s_status, "Showing %s, view %d.", s_rooms[s_pick].name, s_view);
    }
    free(rgb);
    free(pix);
    free(set);
    return rc;
}

/* a 160 x 224 half of the picture: 15-bit texture page at x0 (a multiple of 64), u from u0;
 * the picture's 240 lines are cropped by 8 at top and bottom to the field's 224 */
static void quad(RenderTexturedQuad *q, int screen_x, int page_x, int u0) {
    memset(q, 0, sizeof *q);
    q->tag.length = 9;
    q->color.bytes.r = q->color.bytes.g = q->color.bytes.b = 128;
    q->color.bytes.code = 0x2C;                        /* textured four-point polygon, opaque */
    q->tpage = (u16)((page_x / 64) | (VRAM_Y / 256) << 4 | 2 << 7);   /* 15-bit direct colour */
    int x0 = screen_x, x1 = screen_x + 160, v0 = 8, v1 = 8 + 224;
    q->x0 = (u16)x0, q->y0 = 0, q->u0 = (u8)u0, q->v0 = (u8)v0;
    q->x1 = (u16)x1, q->y1 = 0, q->u1 = (u8)(u0 + 160), q->v1 = (u8)v0;
    q->x2 = (u16)x0, q->y2 = 224, q->u2 = (u8)u0, q->v2 = (u8)v1;
    q->x3 = (u16)x1, q->y3 = 224, q->u3 = (u8)(u0 + 160), q->v3 = (u8)v1;
}

static int on_background(void *ctx, void *user) {
    (void)user;
    PeHookFieldBackground *b = ctx;
    if (!s_show || s_uploaded < 0) return 0;
    if (b->stage == 0) return 1;                       /* skip the PE room's own tiles */
    if (b->slot < 0 || b->slot > 1 || !b->ordering) return 0;
    RenderNativeTag *far_end = (RenderNativeTag *)b->ordering + 4095;
    quad(&s_quads[b->slot][0], 0, VRAM_X, 0);           /* VRAM x 320-479 */
    quad(&s_quads[b->slot][1], 160, VRAM_X + 128, 32);  /* VRAM x 480-639, page at 448 */
    for (int k = 0; k < 2; k++) {
        s_quads[b->slot][k].tag.address = far_end->address;
        far_end->address = (uintptr_t)&s_quads[b->slot][k];
    }
    return 0;
}

static int on_scene(void *ctx, void *user) {
    (void)ctx, (void)user;
    s_uploaded = s_uploaded_view = -1;                 /* the room load replaced the pages */
    if (s_show) upload();
    return 0;
}

static void show(void) {
    list_rooms();
    s_uploaded = -1;
    s_show = !upload();
}
static void hide(void) {
    s_show = 0;
    snprintf(s_status, sizeof s_status, "Off. The PE room's picture comes back at the next room.");
}
static void refresh(void) { s_listed = 0; list_rooms(); }
static void text_status(char *out, unsigned size) { snprintf(out, size, "%s", s_status); }

static int init(void) {
    if (pe_mod_path(s_store, sizeof s_store, "import/store")) return 1;
    pe_imp_mdec_set_table(MdecDiag_ExpandDecoderTable);
    pe_menu_begin(PAGE);
    pe_menu_top_page(PAGE, "Play imported Resident Evil rooms (prototype: show a room's picture).", 62);
    pe_menu_register_dynamic_choice(0, "Room", "An imported room (F1 > Import scans them).", &s_pick, ROOMS_MAX,
                                    room_count, room_name);
    pe_menu_register_int(0, "Camera view", "Which of the room's camera views to show.", &s_view, 0, 31, 1);
    pe_menu_register_action(0, "Show it here", "Shows the room's picture as this room's background.", show);
    pe_menu_register_action(0, "Stop showing it", "Back to the PE room's own picture (at the next room).", hide);
    pe_menu_register_action(0, "Refresh the list", "Lists rooms imported since the game started.", refresh);
    pe_menu_register_info(0, "Status", "What's shown.", text_status);
    pe_hook_add(PE_HOOK_FIELD_BACKGROUND, on_background, NULL, PE_PRIORITY_LATE, MOD_ID);
    pe_hook_add(PE_HOOK_SCENE_START, on_scene, NULL, PE_PRIORITY_LATE, MOD_ID);
    return 0;
}

static void shutdown_rooms(void) {
    s_show = 0;
    free(s_rooms);
    s_rooms = NULL;
}

const PeMod pe_mod_pe_rooms = {MOD_ID, "RE rooms (prototype)", "0.1", NULL, init, shutdown_rooms};
