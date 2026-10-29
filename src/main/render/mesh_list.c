/* Geometry sprite entries and mesh lists, the screen fade and tint, the
 * background scroll/parallax step and the room shadow and primitive colour
 * draws (G0833/G0835). */

#include "common.h"
#include "pe1/game_state.h"
#include "pe1/geom_state.h"
#include "pe1/gte.h"
#include "pe1/gte_types.h"
#include "pe1/psyq_nop.h"
#include "pe1/render_buffers.h"
#include "pe1/render_camera.h"
#include "pe1/render_prim.h"
#include "pe1/render_shadow.h"
#include "pe1/room_floor.h"
#include <psyz/gpu.h>
#include <psyz/video.h>
#include <string.h>

/* Matching debt: fixed-pins and scheduling constraints preserve the stock PSYQ instruction schedule. */

/* A packet or ordering-table slot address as the 24-bit link word. */
typedef union RenderLinkAddress {
    void *pointer;
    u8 *bytes;
    u32 *words;
    GeomEntry *entry;
    uintptr_t word;
} RenderLinkAddress;

#define LINK_PACKET_PRELOADED(packet, ordering, packet_tag, mask24, maskTop) \
    do { \
        RenderNativeTag *slot = (RenderNativeTag *)(ordering); \
        (packet)->address = slot->address; \
        slot->address = (uintptr_t)(packet); \
    } while (0)
#define LINK_PAGE(packet, ordering, mask24, maskTop) \
    LINK_PACKET_PRELOADED(packet, ordering, 0, 0, 0)

/* Widescreen: background tiles are kept up to RENDER_WIDESCREEN_MARGIN past
 * each side of the 320-pixel screen; the stored backgrounds of scrolling rooms
 * are wider than the screen. A repeating layer (flags 4) places each tile once
 * inside a window as wide as the layer, so it is widened only as far as its
 * width allows. The presenter shows past the screen's edges only the columns
 * the linked tiles cover as fully as the screen's own edge columns (per
 * 16-line band of the 224 lines), contiguous from the edge outwards; any gap
 * or end of the stored picture stays black. A tile counts only from its first
 * to its last column holding picture: the stored picture can stop inside a
 * tile (a wall's last tiles hold a few columns of picture and the rest
 * transparent), which showed as a black strip at that side. The edge
 * columns' bands come from the whole tiles, so the picture never reaches past
 * them. A band may end before the others where its picture ends in black:
 * the stored pictures leave out tiles inside their black areas (M235's
 * corridor has a few in the dark above its walls), and the 4:3 screen shows
 * those gaps black too, so the other bands carry on past them. */
#define WS_FIRST_X (-112)
#define WS_COLUMNS 544
#define WS_BANDS 14
static struct {
    u16 bands[WS_COLUMNS]; /* picture: from the tiles' texels */
    u16 tiled[WS_COLUMNS]; /* whole linked tiles: for the screen's edge */
    u16 lit[WS_COLUMNS];   /* picture that is not black: a band can't end next to it */
    int tiles;
} s_WidescreenCover[2];

/* Darkest picture a band may end on: every 5-bit channel at most this. */
#define WS_DARK 2

/* Whether the tile's texel (i, j) holds picture, read from PsyZ's VRAM copy:
 * 0 transparent, 1 black (or nearly), 2 any other colour; -1 when the copy
 * cannot tell (not uploaded, drawn over, 15-bit page...). Opaque black counts
 * as picture: dark rooms store real black past the screen. */
static int Render_WidescreenTexel(const RenderTilePacket *sprite, const RenderTexturePagePacket *page,
                                  int i, int j) {
    const unsigned char *known;
    const unsigned short *vram = Psyz_GpuGetVramCopy(&known);
    u32 tp = page->command;
    int bx = (tp & 0xF) * 64;
    int by = ((tp >> 4) & 1) * 256;
    int depth = (tp >> 7) & 3;
    int cx = (sprite->clut & 0x3F) * 16;
    int cy = (sprite->clut >> 6) & 0x1FF;
    int tu = (sprite->u + i) & 0xFF;
    int tv = (sprite->v + j) & 0xFF;
    int at;
    u16 c;

    if (vram == NULL || depth == 3) return -1;
    if (depth == 0) {
        at = (by + tv) * 1024 + ((bx + tu / 4) & 1023);
        if (!known[at]) return -1;
        at = cy * 1024 + ((cx + ((vram[at] >> ((tu & 3) * 4)) & 0xF)) & 1023);
    } else if (depth == 1) {
        at = (by + tv) * 1024 + ((bx + tu / 2) & 1023);
        if (!known[at]) return -1;
        at = cy * 1024 + ((cx + ((vram[at] >> ((tu & 1) * 8)) & 0xFF)) & 1023);
    } else {
        at = (by + tv) * 1024 + ((bx + tu) & 1023);
    }
    if (!known[at]) return -1;
    c = vram[at];
    if (c == 0) return 0;
    return (c & 31) <= WS_DARK && ((c >> 5) & 31) <= WS_DARK && ((c >> 10) & 31) <= WS_DARK ? 1 : 2;
}

static void Render_AddWidescreenTile(int slot, int x, int y, const RenderTilePacket *sprite,
                                     const RenderTexturePagePacket *page) {
    /* Only columns next to the screen's edge or past it are asked whether
     * they are black, so the tiles inside it stop at their first picture. */
    int edge = x < 1 || x + 16 > 319;
    unsigned lit = 0;
    int first = 16;
    int last = 0;
    int band;
    int column;
    int i;
    int j;

    /* The tile's picture runs from its first to its last column holding any
     * picture, on any row; transparent texels between them are part of it. */
    for (i = 0; i < 16; ++i) {
        for (j = 0; j < 16; ++j) {
            int texel = Render_WidescreenTexel(sprite, page, i, j);
            if (texel < 0) {
                first = 0;
                last = 16;
                lit = 0xFFFF;
                i = j = 16;
            } else if (texel) {
                first = i < first ? i : first;
                last = i + 1;
                if (texel > 1) lit |= 1u << i;
                if (texel > 1 || !edge) break;
            }
        }
    }
    for (band = 0; band < WS_BANDS; ++band) {
        int middle = band * 16 + 8;
        if (middle < y || middle >= y + 16) continue;
        for (column = x; column < x + 16; ++column) {
            if (column < WS_FIRST_X || column >= WS_FIRST_X + WS_COLUMNS) continue;
            s_WidescreenCover[slot].tiled[column - WS_FIRST_X] |= (u16)(1u << band);
            if (column >= x + first && column < x + last)
                s_WidescreenCover[slot].bands[column - WS_FIRST_X] |= (u16)(1u << band);
            if (lit & (1u << (column - x)))
                s_WidescreenCover[slot].lit[column - WS_FIRST_X] |= (u16)(1u << band);
        }
    }
    s_WidescreenCover[slot].tiles++;
}

static u16 Render_WidescreenEdgeBands(int slot, int first) {
    u16 rows = 0xFFFF;
    int column;

    for (column = first; column < first + 8; ++column)
        rows &= s_WidescreenCover[slot].tiled[column - WS_FIRST_X];
    return rows;
}

/* Whether the next column out keeps the picture going: some of the bands
 * still open have picture there, and every band that stops there ends on
 * black in the column before it. */
static int Render_WidescreenNextColumn(int slot, u16 *open, int column, int previous) {
    u16 have = s_WidescreenCover[slot].bands[column - WS_FIRST_X] & *open;

    if (have == 0 || (*open & ~have & s_WidescreenCover[slot].lit[previous - WS_FIRST_X]) != 0) return 0;
    *open = have;
    return 1;
}

void Render_GetWidescreenPicture(int slot, int *left, int *right) {
    u16 open;

    *left = 0;
    *right = 320;
    if (s_WidescreenCover[slot].tiles == 0) return;
    open = Render_WidescreenEdgeBands(slot, 0);
    while (*left > -RENDER_WIDESCREEN_MARGIN && Render_WidescreenNextColumn(slot, &open, *left - 1, *left))
        --*left;
    open = Render_WidescreenEdgeBands(slot, 312);
    while (*right < 320 + RENDER_WIDESCREEN_MARGIN && Render_WidescreenNextColumn(slot, &open, *right, *right - 1))
        ++*right;
}

/* Where Render_LinkFieldSubpixel found the slot's room belongs this frame. */
static struct {
    int active;
    float dx, dy;
    int camera_x;
} s_FieldShift[2];

void Render_CommitWidescreenPicture(int slot, int buffer_y) {
    if (buffer_y >= 0 && Psyz_VideoGetWidescreen()) {
        int left, right;
        Render_GetWidescreenPicture(slot, &left, &right);
        int hd_left,hd_right;
        if (s_WidescreenCover[slot].tiles && Render_GetHdWidescreenPicture(&hd_left,&hd_right)) {
            if(hd_left<left)left=hd_left;
            if(hd_right>right)right=hd_right;
        }
        if (s_FieldShift[slot].active)
            Psyz_VideoSetWidescreenPictureShifted(buffer_y, left, right, s_FieldShift[slot].dx,
                                                  s_FieldShift[slot].dy, s_FieldShift[slot].camera_x);
        else
            Psyz_VideoSetWidescreenPicture(buffer_y, left, right);
    }
    memset(&s_WidescreenCover[slot], 0, sizeof(s_WidescreenCover[slot]));
    memset(&s_FieldShift[slot], 0, sizeof(s_FieldShift[slot]));
}

/* A custom GP0 command switches centred drawing on at the head of the top
 * entry and off at the head of entry 0, in the ordering table's own order. */
#define WS_CENTRED_OPCODE 0x1Eu
static struct {
    RenderNativeTag tag;
    uintptr_t code;
} s_WidescreenCentred[2][2];

static int Render_WidescreenCentredCommand(const u_long *words, int available, void *userdata) {
    (void)available;
    (void)userdata;
    Psyz_VideoSetWidescreenCentred((int)(words[0] & 1));
    return 1;
}

void Render_CentreWidescreenOverlay(int slot, RenderNativeTag *ordering, int top_entry) {
    static int registered;
    int i;

    if (top_entry <= 0 || !Psyz_VideoGetWidescreen()) return;
    if (!registered) {
        Psyz_GpuRegisterCommandHandler(WS_CENTRED_OPCODE, Render_WidescreenCentredCommand, NULL);
        registered = 1;
    }
    for (i = 0; i < 2; ++i) {
        RenderNativeTag *entry = i == 0 ? ordering + top_entry : ordering;
        s_WidescreenCentred[slot][i].tag.length = 1;
        s_WidescreenCentred[slot][i].code = (WS_CENTRED_OPCODE << 24) | (i == 0);
        s_WidescreenCentred[slot][i].tag.address = entry->address;
        entry->address = (uintptr_t)&s_WidescreenCentred[slot][i];
    }
}

/* Widescreen: the room and its models (ordering-table entries above the
 * screen layers: text and menus 1-2, battle HUD 4-7) are shown where the
 * camera's dropped fraction puts them (Render_GetCameraSubpixel), so pans
 * move evenly. A custom GP0 command marks them as the room
 * (Psyz_VideoSetWidescreenField): on at the head of the table, off at the
 * head of the top screen layer. */
#define FIELD_SUBPIXEL_OPCODE 0x1Du
#define FIELD_SUBPIXEL_SCREEN_TOP 7
static struct {
    RenderNativeTag tag;
    uintptr_t code;
} s_FieldSubpixel[2][2];

static int Render_FieldSubpixelCommand(const u_long *words, int available, void *userdata) {
    (void)available;
    (void)userdata;
    Psyz_VideoSetWidescreenField((int)(words[0] & 1));
    return 1;
}

void Render_LinkFieldSubpixel(int slot, RenderNativeTag *ordering) {
    static int registered;
    float dx, dy;
    int camera_x;
    int i;

    s_FieldShift[slot].active = 0;
    if (!Psyz_VideoGetWidescreen()) return;
    if (s_WidescreenCover[slot].tiles) Render_LinkHdWidescreenBackdrop(slot,ordering);
    if (!Render_GetCameraSubpixel(&dx, &dy, &camera_x)) return;
    if (!registered) {
        Psyz_GpuRegisterCommandHandler(FIELD_SUBPIXEL_OPCODE, Render_FieldSubpixelCommand, NULL);
        registered = 1;
    }
    for (i = 0; i < 2; ++i) {
        RenderNativeTag *entry = ordering + (i == 0 ? 4095 : FIELD_SUBPIXEL_SCREEN_TOP);

        s_FieldSubpixel[slot][i].tag.length = 1;
        s_FieldSubpixel[slot][i].code = (FIELD_SUBPIXEL_OPCODE << 24) | (i == 0);
        s_FieldSubpixel[slot][i].tag.address = entry->address;
        entry->address = (uintptr_t)&s_FieldSubpixel[slot][i];
    }
    s_FieldShift[slot].active = 1;
    s_FieldShift[slot].dx = dx;
    s_FieldShift[slot].dy = dy;
    s_FieldShift[slot].camera_x = camera_x;
}

int Render_DrawSpriteEntry(GeomEntry *input)
{
    u32 *position_cursor;
    s32 workValue;
    GeomEntry *entry;
    s32 active;
    RenderTilePacket *sprite;
    RenderTexturePagePacket *page;
    RenderLinkAddress ordering;
    GeomState *state;
    u32 flags;
    u32 count;
    u32 i;
    s32 scroll_x;
    s32 scroll_y;
    s32 texture_base;
    u32 *pos;
    s32 raw_x;
    s32 raw_y;
    s32 screen_src;
    s32 margin;
    s32 cull;
    s32 edge;
    char frame_pad[9];

    margin = Psyz_VideoGetWidescreen() ? RENDER_WIDESCREEN_MARGIN : 0;
    /* Widescreen: one more tile past each edge for the room's shift
     * (Render_LinkFieldSubpixel) to bring into view. */
    edge = Psyz_VideoGetWidescreen() ? 16 : 0;
    cull = margin + edge;
    active = g_ActiveDrawSlot;
    entry = input;
    sprite = entry->u30.prim;
    count = entry->prim_count;
    ordering.pointer = D_800B0E38.ordering[active];
    if (active != 0) {
        sprite += count;
    }
    page = entry->u34.pagePackets;
    if (active != 0)
        page += count;

    state = g_GeomState;
    {
        s32 screen_add;
        screen_src = entry->scr_x;
        screen_add = (u16)state->out_disp_x;
        raw_x = screen_src + screen_add;
        screen_src = entry->scr_y;
        screen_add = (u16)state->out_disp_y;
        scroll_x = raw_x;
        raw_y = screen_src + screen_add;
        scroll_y = raw_y;
    }
    {
        workValue = *(u32 *)entry >> 8;
        workValue &= 0xFFF;
        screen_src = state->field26;
        texture_base = screen_src + workValue;
    }
    screen_src = entry->u28.pos_ptr;
    flags = entry->head.b.flags;
    {
        /* The position words lie pos_ptr bytes past the entry. */
        RenderLinkAddress positions;

        positions.entry = entry;
        positions.word += screen_src;
        pos = positions.words;
    }

    if (flags & 4) {
        s32 mod_x;
        s32 mod_y;
        s32 divisor_x;
        s32 remainder_y;
        s32 signed_x;
        s32 base_y;
        s32 wrap_margin;
        signed_x = (s16)raw_x;
        mod_x = entry->anim_mod_x;
        wrap_margin = ((s32)(mod_x & 0xFFFF) - 0x14F) / 2;
        if (wrap_margin > cull) wrap_margin = cull;
        if (wrap_margin < 0) wrap_margin = 0;
        signed_x -= 320;
        divisor_x = mod_x & 0xFFFF;
        signed_x += divisor_x;
        scroll_x = signed_x % divisor_x;
        mod_y = entry->anim_mod_y;
        workValue = (s16)raw_y;
        workValue -= 224;
        workValue += mod_y & 0xFFFF;
        remainder_y = workValue % (mod_y & 0xFFFF);
        i = 0;
        raw_x = mod_x - 320;
        scroll_x -= raw_x;
        base_y = mod_y;
        base_y -= 224;
        scroll_y = remainder_y - base_y;
        if (count != 0) {
            u32 mask24 = 0x00FFFFFF;
            u32 maskTop = 0xFF000000;
            RenderTexturePagePacket *page_cursor = page;
            RenderTilePacket *sprite_cursor = sprite;
            position_cursor = pos;
                do {
                s32 draw_x;
                {
                    u32 word_x = *position_cursor;
                    s32 x = scroll_x + (word_x >> 22);
                    draw_x = x;
                    if ((s16)x >= 320 + wrap_margin) {
                        s32 modulus_x = entry->anim_mod_x;
                        draw_x = x - modulus_x;
                    } else if ((s16)x < -15 - wrap_margin) {
                        s32 modulus_x = entry->anim_mod_x;
                        draw_x = modulus_x + x;
                    }
                }
                if ((u16)(draw_x + 15 + wrap_margin) < 0x14F + 2 * wrap_margin) {
                        s32 draw_y;
                        {
                            u32 word_y = *position_cursor;
                            s32 y = scroll_y + ((word_y >> 12) & 0x3FF);
                            draw_y = y;
                            if ((s16)y >= 224) {
                                workValue = entry->anim_mod_y;
                                draw_y = y - workValue;
                            } else if ((s16)y < -15) {
                                workValue = entry->anim_mod_y;
                                draw_y = workValue + y;
                            }
                        }
                        if ((u16)(draw_y + 15) < 0xEF) {
                            s32 tile_u = texture_base + (*(u16 *)position_cursor & 0xFFF);
                            if (((tile_u - 8) & 0xFFFFU) < 0xFF1) {
                                u32 sprite_tag;
                                s32 ot_index = tile_u;
                                RenderLinkAddress ot;
                                ot.pointer = &((RenderNativeTag *)ordering.pointer)[(s16)ot_index];
                                sprite_tag = *(u32 *)sprite_cursor;
                                (sprite_cursor)->x = draw_x;
                                (sprite_cursor)->y = draw_y;
                                LINK_PACKET_PRELOADED(sprite_cursor, ot.bytes, sprite_tag, mask24, maskTop);
                                LINK_PAGE(page_cursor, ot.bytes, mask24, maskTop);
                                if (margin != 0)
                                    Render_AddWidescreenTile(active, (s16)draw_x, (s16)draw_y, sprite_cursor, page_cursor);
                            }
                        }
                }
                page_cursor += 1;
                sprite_cursor += 1;
                ++i;
                ++position_cursor;
            } while (i < count);
        }
    } else {
        u32 mask24;
        u32 maskTop;
        RenderTilePacket *sprite_cursor;
        RenderTexturePagePacket *page_cursor;
        i = 0;
        if (count != 0) {
            mask24 = 0x00FFFFFF;
            maskTop = 0xFF000000;
            sprite_cursor = sprite;
            page_cursor = page;
            position_cursor = pos;
            do {
            u32 word = *position_cursor;
            s32 draw_x;
            workValue = scroll_x + (word >> 22);
            draw_x = workValue;
            if ((u16)(workValue + 15 + cull) < 0x14F + 2 * cull) {
                    {
                    s32 y;
                    s32 draw_y;
                    s32 tile_u;
                    y = scroll_y + ((word >> 12) & 0x3FF);
                    draw_y = y;
                    if ((u16)(y + 15 + edge) < 0xEF + 2 * edge) {
                        tile_u = texture_base + (*(u16 *)position_cursor & 0xFFF);
                        if (((tile_u - 8) & 0xFFFFU) < 0xFF1) {
                            u32 sprite_tag;
                            s32 ot_index = tile_u;
                            RenderLinkAddress ot;
                            ot.pointer = &((RenderNativeTag *)ordering.pointer)[(s16)ot_index];
                            sprite_tag = *(u32 *)sprite_cursor;
                            (sprite_cursor)->x = draw_x;
                            (sprite_cursor)->y = draw_y;
                            LINK_PACKET_PRELOADED(sprite_cursor, ot.bytes, sprite_tag, mask24, maskTop);
                            LINK_PAGE(page_cursor, ot.bytes, mask24, maskTop);
                            if (margin != 0)
                                Render_AddWidescreenTile(active, (s16)draw_x, (s16)draw_y, sprite_cursor, page_cursor);
                        }
                    }
                    }
                }
            page_cursor += 1;
            sprite_cursor += 1;
            ++i;
            ++position_cursor;
            } while (i < count);
        }
    }

    entry->disp_x = scroll_x;
    entry->disp_y = scroll_y;
    return 0;
}


int Render_SetEntryVisible(int index, int enabled) {
    GeomStateAddress table, base;
    GeomEntry *entry = (GEOM_STATE_OFFSET(table, base, entry_offset, 0), table.entry) + index;

    if (enabled != 0) {
        entry->head.b.flags |= 2;
    } else {
        entry->head.b.flags &= 0xFD;
    }
    return 0;
}

int Render_SetEntryScrolled(int index, int enabled, unsigned int arg2, unsigned int arg3) {
    GeomStateAddress table, base;
    GeomEntry *entry = (GEOM_STATE_OFFSET(table, base, entry_offset, 0), table.entry) + index;

    if (enabled != 0) {
        entry->head.b.flags |= 4;
    } else {
        entry->head.b.flags &= 0xFB;
    }
    entry->speedX = arg2 >> 8;
    entry->speedY = arg3 >> 8;
    return 0;
}

int Render_SetEntryMirrored(int index, int enabled, unsigned int arg2, unsigned int arg3) {
    GeomStateAddress table, base;
    GeomEntry *entry = (GEOM_STATE_OFFSET(table, base, entry_offset, 0), table.entry) + index;

    if (enabled != 0) {
        entry->head.b.flags |= 8;
    } else {
        entry->head.b.flags &= 0xF7;
    }
    entry->speedX = (0x10000 - arg2) >> 8;
    entry->speedY = (0x10000 - arg3) >> 8;
    return 0;
}

int Render_SetEntryPosition(int index, int x, int y) {
    GeomStateAddress table, base;
    GeomEntry *entry = (GEOM_STATE_OFFSET(table, base, entry_offset, 0), table.entry) + index;
    int *flags = &g_GeomScroll.state.flags;

    entry->scr_x = x;
    entry->base_x = x;
    entry->scr_y = y;
    entry->base_y = y;
    *flags |= 0x80;
    return 0;
}

int Render_LoadGeomState(int);
/* Loads the selected group's viewport, centres the camera on it and builds
 * every mesh entry's packets into `buffer`; *end returns the free space. */
int Render_DrawMeshList(void *buffer, void **end)
{
    GeomState *state = g_GeomState;
    u8 *group = &g_GeomScroll.state.position.groupSelect;
    GeomStateAddress address;
    CameraViewport *view;
    GeomEntry *entry;
    unsigned int count;
    unsigned int i;
    int x;

    Render_LoadGeomState(*group);
    Render_PrepareHdBackgrounds();
    address.state = state;
    address.word += state->entry_offset_1C;
    view = &address.viewport[*group];
    x = (view->minX + view->maxX) / 2;
    g_GeomScroll.state.position.x = g_GeomScroll.state.position.originX = x;
    g_GeomScroll.state.position.y = g_GeomScroll.state.position.originY =
        (view->minY + view->maxY) / 2;
    g_GeomScroll.state.position.boundsMinX = view->minX;
    g_GeomScroll.state.position.boundsMaxX = view->maxX;
    i = 0;
    g_GeomScroll.state.position.boundsMinY = view->minY;
    g_GeomScroll.state.position.boundsMaxY = view->maxY;
    x -= 160;
    count = state->entry_count06;
    state->out_disp_x = state->disp_src_x - x;
    state->out_disp_y = state->disp_src_y - ((u16)g_GeomScroll.state.position.y - 112);
    address.state = state;
    address.word += state->entry_offset;
    *end = buffer;
    if (i < count) {
        int min = -32768, max = 32767;

        entry = address.entry;
        do {
            if (Render_LoadMeshEntry(entry, *end, end))
                return -18;
            entry->bounds.minX = min;
            entry->bounds.maxX = max;
            entry->bounds.minY = min;
            entry->bounds.maxY = max;
            entry++;
        } while (++i < count);
    }
    {
        int *flags = &g_GeomScroll.state.flags;

        g_RenderFadeFrameCount = g_RenderFadeFrameCounter = 0;
        *flags &= ~0xC00;
    }
    return 0;
}

/* Field geometry: clamp the display-source point into the clip window and
 * draw the selected group's sprite entries relative to the camera.
 * Contiguous default-profile pair on g_GeomState (main_tu_evidence G0836
 * joins them with the mesh renderer before and the fades after). */

/* Offsets a point by the display origin and clamps it into the clip
 * window; the result becomes the display source point and depth. */
int Render_ClipPoint(int x, int y, int z) {
    GeomState *state = g_GeomState;
    u16 clippedX = state->clip_offset_x + x;
    u16 clippedY = state->clip_offset_y + y;
    int depth = state->depth_offset + z;

    if ((s16)clippedX < state->clip_min_x)
        clippedX = state->clip_min_x;
    else if (state->clip_max_x < (s16)clippedX)
        clippedX = state->clip_max_x;
    if ((s16)clippedY < state->clip_min_y)
        clippedY = state->clip_min_y;
    else if (state->clip_max_y < (s16)clippedY)
        clippedY = state->clip_max_y;
    state->disp_src_x = clippedX;
    state->disp_src_y = clippedY;
    state->field26 = depth;
    return 0;
}

/* Re-derives the display offsets from the camera and draws the sprite
 * entries of the selected group. */
s32 Render_BuildMeshList(void) {
    GeomStateAddress base;
    GeomEntry *entry;
    s32 count;
    s32 i;

    base.state = g_GeomState;
    base.state->out_disp_x = base.state->disp_src_x - ((u16)g_GeomScroll.state.position.x - 160);
    count = base.state->entry_count06;
    base.state->out_disp_y = base.state->disp_src_y - ((u16)g_GeomScroll.state.position.y - 112);
    base.word += base.state->entry_offset;
    i = 0;
    if (i < count) {
        entry = base.entry;
        do {
            if ((entry->head.b.flags & 2) && entry->group == g_GeomScroll.state.position.groupSelect)
                Render_DrawSpriteEntry(entry);
            i++;
            entry++;
        } while (i < count);
    }
    return 0;
}

int Render_StartFadeIn(int arg0)
{
  int new_var;
  int *flags;
  int value;
  flags = &g_GeomScroll.state.flags;
  g_RenderFadeFrameCount = arg0;
  g_RenderFadeFrameCounter = 0;
  new_var = *flags;
  value = new_var;
  value &= ~0xC00;
  value |= 0x400;
  *flags = value;
  return 0;
}

/* Steps the screen fade: every sprite primitive of every entry gets the
 * current grey level; when the fade has run its frames the fade-in mode is
 * switched off and the fade-out flag set. */
int Render_StepFade(void) {
    GeomState *state;
    GeomStateAddress entries;
    GeomEntry *entry;
    u8 *counter;
    int level;
    int count;
    int i;

    if (!(g_GeomScroll.state.flags & 0x400))
        return 0;
    level = (g_RenderFadeFrameCounter << 7) / (g_RenderFadeFrameCount - 1);
    state = g_GeomState;
    entries.state = g_GeomState;
    entries.word += state->entry_offset;
    count = state->entry_count06;
    level = 0x80 - level;
    entry = entries.entry;
    for (i = 0; i < count; i++, entry++) {
        RenderTilePacket *prim = entry->u30.prim;
        u32 primCount = entry->prim_count;
        u8 grey = level;
        u32 j;

        if (g_ActiveDrawSlot != 0)
            prim += primCount;
        for (j = 0; j < primCount; j++) {
            prim->b = grey;
            prim->g = grey;
            prim->r = grey;
            prim++;
        }
    }
    counter = &g_RenderFadeFrameCounter;
    if (++*counter >= g_RenderFadeFrameCount) {
        g_GeomScroll.state.flags = (g_GeomScroll.state.flags & ~0xC00) | 0x800;
        g_GeomState->field26 = 0x1FF0;
    }
    return 0;
}

/* Starts the scene-load fade: sets the load flag (toggling the 0x2000
 * phase bit) and selects tint mode 1. */
int Render_BeginSceneLoad(void) {
    {
        int *flags = &g_GeomScroll.state.flags;
        int old = *flags;
        int value = old | 0x1000;

        *flags = value;
        if (value & 0x2000)
            *flags = value & ~0x2000;
        else
            *flags = old | 0x3000;
    }
    {
        int *flags = &g_GeomScroll.state.flags;

        *flags = (*flags & ~0xC000) | 0x4000;
    }
    return 0;
}

/* Tints every sprite primitive with the tint level (or neutral grey while
 * the tint is off) and advances the two-step tint mode. */
int Render_ApplyScreenTint(void) {
    GeomState *state;
    GeomStateAddress entries;
    GeomEntry *entry;
    int *flags;
    int mode;
    int masked;
    int level;
    int count;
    int i;

    if (!(g_GeomScroll.state.flags & 0x1000))
        return 0;
    level = 0x80;
    if (g_GeomScroll.state.flags & 0x2000)
        level = g_GeomScroll.state.position.meshTintLevel;
    state = g_GeomState;
    entries.state = g_GeomState;
    entries.word += state->entry_offset;
    count = state->entry_count06;
    entry = entries.entry;
    for (i = 0; i < count; i++, entry++) {
        RenderTilePacket *prim = entry->u30.prim;
        u32 primCount = entry->prim_count;
        u8 grey = level;
        u32 j;

        if (g_ActiveDrawSlot != 0)
            prim += primCount;
        for (j = 0; j < primCount; j++) {
            prim->b = grey;
            prim->g = grey;
            prim->r = grey;
            prim++;
        }
    }
    flags = &g_GeomScroll.state.flags;
    mode = *flags;
    masked = mode & ~0xC000;
    *flags = masked;
    switch (mode & 0xC000) {
    case 0x4000:
        *flags = masked | 0x8000;
        break;
    case 0x8000:
        *flags = masked & ~0x1000;
        break;
    default:
        return 0;
    }
    return 0;
}

/* Applies texture scrolling and camera-relative parallax to the entries. */
int Render_ScrollEntries(void)
{
    if (!(g_GameStateFlags & 0x104)) {
        int i;
        GeomState *state = g_GeomState;
        GeomStateAddress table;
        GeomEntryView *entries;
        int count;
        GeomScrollEntry *entry;

        table.state = g_GeomState;
        table.word += state->entry_offset;
        entries = table.views;
        count = state->entry_count06;

        i = 0;
        if (i < count) {
            GeomScrollCoordinates *camera = &g_GeomScroll.state.position;

            do {
                entry = &entries[i].scroll;
                int position;

                if (entry->flags & 4) {
                    int x, y, fracX;

                    position = (entry->x * 256) | entry->fractionX.byte;
                    position += entry->speedX;
                    x = (position >> 8) % entry->modulusX;
                    fracX = position & 255;
                    position = (entry->y * 256) | entry->fractionY.byte;
                    position += entry->speedY;
                    y = (position >> 8) % entry->modulusY;
                    entry->fractionX.word = fracX;
                    entry->fractionY.word = position & 255;
                    entry->x = x;
                    entry->y = y;
                }
                if (entry->flags & 8) {
                    position = entry->baseX * 256 +
                        (camera->x - camera->originX) * entry->speedX;
                    entry->x = position >> 8;
                    entry->fractionX.word = position & 255;
                    position = entry->baseY * 256 +
                        (camera->y - camera->originY) * entry->speedY;
                    entry->y = position >> 8;
                    entry->fractionY.word = position & 255;
                }
            } while (++i < count);
        }
        if (g_GeomScroll.state.flags & 0x80) {
            unsigned short x = g_GeomScroll.state.position.x, y = g_GeomScroll.state.position.y;
            g_GeomScroll.state.flags &= ~0x80;
            g_GeomScroll.state.position.savedX = x;
            g_GeomScroll.state.position.savedY = y;
        }
    }
    return 0;
}

/* Sets the colour of every primitive in both halves of the double-buffered
 * primitive list. */
void Render_SetPrimColour(PrimObj *obj, unsigned char r, unsigned char g, unsigned char b) {
    int i;

    for (i = 0; i < obj->count; i++) {
        PrimEntry *entries = obj->entries;

        entries[i].r = r;
        entries[i].g = g;
        entries[i].b = b;
        entries = obj->entries + obj->count;
        entries[i].r = r;
        entries[i].g = g;
        entries[i].b = b;
    }
}

/* Builds the ground-aligned shadow transform for `actor` and links its
 * shadow quad, projected from a square of the model's shadow radius. */
int Render_DrawActorShadow(RenderShadowActor *actor)
{
    GteShortVector origin = {0};
    GteShortVector corner = {0};
    GteVector axis;
    GteVector normal;
    GteVector up;
    GteMatrixStorage local;
    GteMatrix world;
    GteMatrixStorage *source;
    u32 first, last;
    s32 sxy;
    s32 p;
    s32 flag;
    RenderShadowQuad *quad;
    int radius;
    int shade;
    int depth;
    int maxDepth;

    if (actor->flags & 0x400)
        return 0;
    if (!actor->render_object.variant_visible)
        return 0;

    radius = actor->render_object.header->shadow_radius;
    origin.x = Pe1Fixed_Integer(&actor->pos_x);
    origin.y = Pe1Fixed_Integer(&actor->pos_y);
    origin.z = Pe1Fixed_Integer(&actor->pos_z);
    /* The part matrix without its translation turns +Z into the facing. */
    source = (GteMatrixStorage *)&actor->render_object.matrices[actor->shadow_matrix_index];
    first = source->words[0];
    local.words[1] = source->words[1];
    local.words[2] = source->words[2];
    local.words[3] = source->words[3];
    local.words[4] = source->words[4];
    local.words[5] = source->words[5];
    local.words[6] = source->words[6];
    last = source->words[7];
    corner.z = 0x1000;
    local.words[0] = first;
    corner.x = 0;
    corner.y = 0;
    local.matrix.t[1] = 0;
    local.matrix.t[0] = 0;
    local.words[7] = last;
    local.matrix.t[2] = 0;
    gte_ldrotmatrix(&local);
    gte_ldtransmatrix(&local);
    gte_ldv0(&corner);
    gte_rt();
    gte_stlvnl(&axis);
    axis.y = 0;
    if (axis.x == 0 && axis.z == 0) {
        normal.x = 0;
        normal.y = 0;
        normal.z = 0x1000;
    } else {
        VectorNormal(&axis, &normal);
    }
    /* Shadow basis: X = up x facing, Y = up, Z = facing. */
    local.matrix.m[1][1] = 0x1000;
    up.y = 0x1000;
    local.matrix.m[0][1] = 0;
    local.matrix.m[2][1] = 0;
    up.x = 0;
    up.z = 0;
    local.matrix.m[0][2] = normal.x;
    local.matrix.m[1][2] = normal.y;
    local.matrix.m[2][2] = normal.z;
    gte_ldopv1_psyq(&normal);
    gte_ldopv2(&up);
    gte_op12_psyq();
    gte_stlvnl(&axis);
    local.matrix.m[0][0] = axis.x;
    local.matrix.m[1][0] = axis.y;
    local.matrix.m[2][0] = axis.z;
    if (actor->flags & 0x4000000)
        local.matrix.t[1] = g_RoomFloorY->y;
    else
        local.matrix.t[1] = origin.y;
    local.matrix.t[0] = actor->render_object.matrices[actor->shadow_matrix_index].translation[0];
    local.matrix.t[2] = actor->render_object.matrices[actor->shadow_matrix_index].translation[2];
    gte_CompMatrix((GteMatrixWords *)g_EntityRenderScratch, &local, &world);
    SetRotMatrix(&world);
    SetTransMatrix(&world);
    SetGeomScreen(g_EntityRenderScratch[8]);

    quad = &actor->shadow_quads[g_ActiveDrawSlot];
    quad->tag.length = 9;
    quad->code = 0x2C;
    if (actor->render_object.flags_9C & 6)
        shade = (actor->render_object.fade_red + actor->render_object.fade_green + actor->render_object.fade_blue) / 3;
    else
        shade = 0x80;
    shade = shade * actor->shadow_intensity / 128;
    quad->r0 = shade / 2;
    quad->g0 = shade / 2;
    quad->b0 = shade / 2;
    quad->u0 = 0;
    quad->v0 = 0x40;
    quad->u1 = 0x3F;
    quad->v1 = 0x40;
    quad->u2 = 0;
    quad->v2 = 0x7F;
    quad->u3 = 0x3F;
    quad->v3 = 0x7F;
    quad->clut = 0x7210;
    quad->tpage = 0xCB;
    quad->code |= 2;

    maxDepth = -1;
    corner.y = 0;
    corner.x = -radius;
    corner.z = radius;
    depth = RotTransPers(&corner, &sxy, &p, &flag);
    quad->xy0 = sxy;
    if (maxDepth < depth)
        maxDepth = depth;
    corner.x = radius;
    corner.z = radius;
    depth = RotTransPers(&corner, &sxy, &p, &flag);
    quad->xy1 = sxy;
    if (maxDepth < depth)
        maxDepth = depth;
    corner.x = -radius;
    corner.z = -radius;
    depth = RotTransPers(&corner, &sxy, &p, &flag);
    quad->xy2 = sxy;
    if (maxDepth < depth)
        maxDepth = depth;
    corner.x = radius;
    corner.z = -radius;
    depth = RotTransPers(&corner, &sxy, &p, &flag);
    quad->xy3 = sxy;
    if (maxDepth < depth)
        maxDepth = depth;

    if (maxDepth >= 0 && maxDepth < 0x1000) {
        RenderNativeTag *entry = &D_800B0E38.ordering[g_ActiveDrawSlot][maxDepth];
        quad->tag.address = entry->address;
        entry->address = (uintptr_t)&quad->tag;
    }
    return 0;
}
