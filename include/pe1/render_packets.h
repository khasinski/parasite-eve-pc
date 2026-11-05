#ifndef PE1_RENDER_PACKETS_H
#define PE1_RENDER_PACKETS_H

#include "common.h"

/* PsyZ native ordering-table entry and model packet prefix. */
typedef struct RenderNativeTag {
    uintptr_t address, length;
} RenderNativeTag;

/* GPU DMA link: 24-bit next-packet address and an 8-bit command length. */
typedef union RenderPacketTag {
    u32 word;
    struct { u8 address[3], length; } bytes;
    struct { u32 address : 24, length : 8; } link;
} RenderPacketTag;

PE1_STATIC_ASSERT(sizeof(RenderPacketTag) == 4, render_packet_tag_size);
PE1_STATIC_ASSERT(PE1_OFFSETOF(RenderPacketTag, bytes.length) == 3,
                  render_packet_tag_length_offset);

/* Colour word of a GPU primitive: three colour bytes and the command code.
 * The 24-bit view is the PSY-Q setRGB0 store that keeps the code byte. */
typedef union RenderPacketColor {
    u32 word;
    struct { u8 r, g, b, code; } bytes;
    struct { u32 rgb : 24, code : 8; } fields;
} RenderPacketColor;

PE1_STATIC_ASSERT(sizeof(RenderPacketColor) == 4, render_packet_color_size);

/* Variable-size textured sprite packet used by the glyph renderer. */
typedef struct RenderSpritePacket {
    RenderNativeTag tag;
    union {
        u32 word;
        struct {
            u8 r, g, b, code;
        } bytes;
    } color;
    u16 x, y;
    u8 u, v;
    u16 clut, width, height;
} RenderSpritePacket;

PE1_STATIC_ASSERT(sizeof(RenderSpritePacket) == sizeof(RenderNativeTag) + 16, render_sprite_packet_size);
PE1_STATIC_ASSERT(PE1_OFFSETOF(RenderSpritePacket, x) == sizeof(RenderNativeTag) + 4, render_sprite_x_offset);
PE1_STATIC_ASSERT(PE1_OFFSETOF(RenderSpritePacket, clut) == sizeof(RenderNativeTag) + 10, render_sprite_clut_offset);
PE1_STATIC_ASSERT(PE1_OFFSETOF(RenderSpritePacket, width) == sizeof(RenderNativeTag) + 12, render_sprite_width_offset);

/* Prefix of the buffer pointers initialized by Boot_InitMemoryLayout.
 * The active draw slot selects one of the two ordering/packet buffers.
 * Six unrelated buffer pointers separate the two pairs. */
typedef struct RenderBufferPrefix {
    RenderNativeTag *ordering[2];
    char *other_buffers[6];
    char *packets[2];
} RenderBufferPrefix;
/* Historical buffer symbol aliases the original game state. Includers that
 * access it must include game_state.h after renderer types are complete. */
#define D_800B0E38 (g_GameState.render_buffers)

/* A buffer-table slot addressed by the draw slot's word offset. */
typedef union RenderBufferSlotAddress {
    char **buffer;
    u32 **ordering;
    unsigned char **otBuffer; /* g_OtBufferTable */
    u32 word;
} RenderBufferSlotAddress;
/* The buffer table seen from other_buffers[1]; offset by the draw slot's
 * word, its ordering[0] is that slot's ordering table. */
#define RENDER_BUFFERS_FROM_GLYPH_SLOT(pointer) \
    ((RenderBufferPrefix *)((char *)(pointer) - \
        PE1_OFFSETOF(RenderBufferPrefix, other_buffers[1])))

/* Widescreen: game pixels drawn past each side of the 320-pixel screen (0.3
 * of it: the presenter's 3/4 squeeze holds a sixth past each side, plus up
 * to 39 more on the side the view slides to at a room's end). */
#define RENDER_WIDESCREEN_MARGIN 96
/* The span of the background picture, in screen x, that the tiles linked
 * into a draw slot's ordering table cover without a gap (always at least
 * 0..320). Commit hands it to the presenter for the buffer at VRAM row
 * buffer_y and starts the slot's next frame; buffer_y < 0 only discards it. */
void Render_GetWidescreenPicture(int slot, int *left, int *right);
void Render_CommitWidescreenPicture(int slot, int buffer_y);
/* Draws the 2D layers, ordering-table entries top_entry down to 1, centred in
 * the 16:9 view instead of following the view's shift at a room's end, so a
 * menu or dialogue sits in the middle while the room keeps filling the view
 * (and stays put while a cutscene scrolls the room). Entry 1 holds the menu
 * and text code, entry 2 the dialogue's backdrop; top_entry 0 centres
 * nothing. Call after the frame's packets are linked. */
void Render_CentreWidescreenOverlay(int slot, RenderNativeTag *ordering, int top_entry);

/* Widescreen: shows the room and its models (entries above the screen
 * layers) at the camera's sub-pixel position, so camera pans move evenly at
 * any internal resolution. Nothing outside widescreen or when the camera did
 * not follow its point this frame. Call after the frame's packets are linked
 * and before Render_CommitWidescreenPicture, which hands the position on. */
void Render_LinkFieldSubpixel(int slot, RenderNativeTag *ordering);
void Render_LinkHdWidescreenBackdrop(int slot, RenderNativeTag *ordering);
void Render_LinkHdUnderlayBackdrop(int slot, RenderNativeTag *ordering);

#endif
