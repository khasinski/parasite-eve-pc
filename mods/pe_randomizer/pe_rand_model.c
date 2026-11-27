/* Battle randomizer, models: an enemy actor of one room shows up with another actor's model and animations.
 *
 * An actor of type E (its room-script entity, under 10) takes its model from the room's bank slot E
 * (g_GameState.bank_slots, Scene_LoadMap) and its animation rows from bank_rows[E]. Its textures sit in
 * the enemy area of VRAM: 64 x 128 blocks from x 512, column by column (block 0 = 512,0; 1 = 512,128;
 * 2 = 576,0 ...; 8 at most, up to x 768), one palette row per block, mostly 450 + the block (a few Museum
 * rooms put some at 508 and on, or blocks at y 256: those slots are left alone). The room's bank record
 * for slot E keeps where (its last word, as Scene_LoadMap hands it to Render_SetupEntityPrims): first block
 * (x, y), first palette row, and the block count (the "mode"; 0: not placed in blocks, left alone here).
 * A model's own pages and palettes count from block 0 and row 450, and Render_InitPrimBlock moves each
 * actor's copy of its faces to the record's place, so a model goes anywhere in the area as is. Two slots
 * of one creature may share blocks (Hospital M156, Chrysler M291 / M380, some rat rooms).
 *
 * So a swap, done when the room's banks are filled (PE_HOOK_PLAYER_ROWS, every room load, before the
 * actors spawn): read the source room from the disc (kept until the next room load), point bank slot E at
 * its model and bank_rows[E] at its rows, copy its blocks and palette rows into VRAM (into the actor's own
 * blocks and rows when it fits and no other slot shares them, else into the free blocks after the room's
 * last one and the free rows after its last one from 450) and rewrite the record's word to the new place. The actor keeps its own AI for now, which asks for its own row ids: one the new model
 * doesn't have gets the new model's idle row (2), or its lowest.
 */
#include <stdlib.h>
#include <string.h>
#include "pe_mod_host.h"
#include "pe_game.h"
#include "pe_hooks.h"
#include "pe_disc_assets.h"
#include "pe1/game_state.h"
#include "pe1/psyq_gpu.h"
#include "pe1/scene_room.h"
#include "pe_randomizer.h"

#define MOD_ID "pe_randomizer"
enum { PE_RAND_MODELS = 512, PE_RAND_TYPES = 10, PE_RAND_BLOCKS = 8, PE_RAND_ROWS = 48, PE_RAND_IDLE_ROW = 2, PE_RAND_PAL_ROW = 450 };

typedef struct PeRandModelMap {
    uint16_t room, from_room;
    uint8_t entity, from_entity;
} PeRandModelMap;
static PeRandModelMap s_map[PE_RAND_MODELS];
static unsigned s_map_count;
/* The source rooms of the swaps done at the last room load, and what each bank slot was given. */
static PeRoomBlobs s_rooms[PE_RAND_TYPES];
static unsigned s_room_count;
static void *s_given[PE_RAND_TYPES];

static uint32_t pe_word(const uint8_t *p, uint32_t at) { uint32_t v; memcpy(&v, p + at, 4); return v; }

/* A blob's records of one directory field: 12 bytes each (TIMs: 20). */
static const uint8_t *pe_records(const uint8_t *blob, uint32_t field, unsigned *count) {
    const uint32_t desc = pe_word(blob, pe_word(blob, 4) + field);
    *count = desc >> 22;
    return blob + (desc & 0x3FFFFF);
}

static int pe_block(unsigned x, unsigned y) { return x >= 512 && x < 768 && y < 256 ? (int)((x - 512) / 64 * 2 + y / 128) : -1; }

/* A bank record's place: first block, block count; -1 if it isn't placed in the enemy area. */
static int pe_place(uint32_t word, int *count) {
    *count = (word >> 8) & 0xF;
    return *count ? pe_block((word >> 6) & 0x3C0, (word >> 9) & 0x180) : -1;
}

static unsigned pe_pal(uint32_t word) { return ((word >> 18) & 0xFF) + 0x1C0; }

static uint32_t pe_placed(uint32_t word, int block, int count, unsigned row) {
    const unsigned x = 512 + block / 2 * 64, y = block % 2 * 128;
    word &= ~(0xFu << 8 | 0xFu << 12 | 3u << 16 | 0xFFu << 18);
    return word | (uint32_t)count << 8 | (x >> 6) << 12 | (y >> 7) << 16 | (row - 0x1C0) << 18;
}

static uint8_t *pe_bank_record(const uint8_t *data, unsigned slot) {
    unsigned n;
    const uint8_t *r = pe_records(data, 0x0C, &n);
    for (unsigned i = 0; i < n; ++i, r += 12) if (r[7] == slot) return (uint8_t *)r;
    return NULL;
}

/* One rectangle of a TIM record: the parts in blocks [from, from + count) go to the same place in blocks
 * from `to` on, and palette rows [from_row, from_row + count) to the rows from `to_row` on. */
static void pe_carry_rect(const uint8_t *pixels, uint32_t rect, unsigned h, int from, int count, int to,
                          unsigned from_row, unsigned to_row) {
    static uint16_t piece[64 * 128];
    const unsigned x = (rect >> 10) & 0x7FF, y = rect >> 21, w = rect & 0x3FF;
    for (unsigned r = 0; r < h; ++r) {                         /* palette rows: whole rows */
        const unsigned row = y + r;
        if (x < 512 && row >= from_row && row < from_row + (unsigned)count) {
            RECT dst = {(s16)x, (s16)(to_row + row - from_row), (s16)w, 1};
            LoadImage(&dst, (void *)(pixels + 2 * (size_t)r * w));
        }
    }
    for (unsigned cx = x - x % 64; cx < x + w; cx += 64)       /* textures: by block */
        for (unsigned ry = y - y % 128; ry < y + h; ry += 128) {
            const int b = pe_block(cx, ry);
            if (b < from || b >= from + count) continue;
            const unsigned x0 = cx > x ? cx : x, x1 = cx + 64 < x + w ? cx + 64 : x + w;
            const unsigned y0 = ry > y ? ry : y, y1 = ry + 128 < y + h ? ry + 128 : y + h;
            const int d = to + b - from;
            for (unsigned yy = y0; yy < y1; ++yy)
                memcpy(piece + (yy - y0) * (x1 - x0), pixels + 2 * ((size_t)(yy - y) * w + (x0 - x)), 2 * (x1 - x0));
            RECT dst = {(s16)(512 + d / 2 * 64 + x0 % 64), (s16)(d % 2 * 128 + y0 % 128), (s16)(x1 - x0), (s16)(y1 - y0)};
            LoadImage(&dst, piece);
        }
}

static void pe_carry_tims(const uint8_t *blob, int from, int count, int to, unsigned from_row, unsigned to_row) {
    unsigned n;
    const uint8_t *r = pe_records(blob, 0x28, &n);
    for (unsigned i = 0; i < n; ++i, r += 20) {
        const uint32_t src = pe_word(r, 4), sec = pe_word(r, 12);
        const uint8_t *pixels = blob + (src & 0xFFFFFF);
        pe_carry_rect(pixels, pe_word(r, 8), src >> 24 ? src >> 24 : 256, from, count, to, from_row, to_row);
        if (sec & 0xFFFFFF) pe_carry_rect(pixels + (sec & 0xFFFFFF), pe_word(r, 16), sec >> 24, from, count, to, from_row, to_row);
    }
}

int pe_rand_model_force(unsigned room, unsigned entity, unsigned from_room, unsigned from_entity) {
    if (s_map_count == PE_RAND_MODELS || entity >= 10 || from_entity >= 10) return 0;
    s_map[s_map_count++] = (PeRandModelMap){(uint16_t)room, (uint16_t)from_room, (uint8_t)entity, (uint8_t)from_entity};
    return 1;
}

void pe_rand_model_clear(void) { s_map_count = 0; }

static void pe_rand_model_release(void) {
    for (unsigned i = 0; i < s_room_count; ++i) pe_disc_room_free(&s_rooms[i]);
    s_room_count = 0;
    memset(s_given, 0, sizeof(s_given));
}

int pe_rand_model_apply(int room) {
    uint8_t *assets = (uint8_t *)g_GameState.loaded_scene_assets;
    unsigned n, done = 0;
    int end = 0, rows_end = 0;   /* the first free block, and free palette row (from 450) */
    if (!assets) return 0;
    /* Still the same load: done already. A new load resets the slots its room has; a slot it doesn't
     * have keeps the old pointer, so the room must have that slot too. */
    for (unsigned e = 0; e < 10; ++e)
        if (s_given[e]) { if (g_GameState.bank_slots[e] == s_given[e] && pe_bank_record(assets, e)) return 0; break; }
    pe_rand_model_release();
    const uint8_t *r = pe_records(assets, 0x0C, &n);
    for (unsigned i = 0; i < n; ++i, r += 12) {               /* the room's blocks and palette rows in use */
        int count, block = pe_place(pe_word(r, 8), &count), row = (int)pe_pal(pe_word(r, 8)) - PE_RAND_PAL_ROW;
        if (block >= 0 && block + count > end) end = block + count;
        if (count && row >= 0 && row < PE_RAND_BLOCKS && row + count > rows_end) rows_end = row + count;
    }
    for (unsigned m = 0; m < s_map_count; ++m) {
        const PeRandModelMap *map = &s_map[m];
        uint8_t *mine = map->room == room ? pe_bank_record(assets, map->entity) : NULL;
        int count, block = mine ? pe_place(pe_word(mine, 8), &count) : -1, from_count;
        if (block < 0 || s_given[map->entity] || s_room_count == PE_RAND_TYPES) continue;
        PeRoomBlobs *src = &s_rooms[s_room_count];
        if (!pe_disc_room(map->from_room, src)) continue;
        const uint8_t *theirs = pe_bank_record(src->data, map->from_entity);
        const int from = theirs ? pe_place(pe_word(theirs, 8), &from_count) : -1;
        int own = from_count <= count;                         /* its own blocks, unless another slot shares them */
        unsigned pal_row = pe_pal(pe_word(mine, 8));
        const uint8_t *other = pe_records(assets, 0x0C, &n);
        for (unsigned i = 0; i < n && own; ++i, other += 12) {
            int c, b = other == mine ? -1 : pe_place(pe_word(other, 8), &c);
            if (b >= 0 && b < block + count && block < b + c) own = 0;
        }
        if (from < 0 || (!own && (end + from_count > PE_RAND_BLOCKS || rows_end + from_count > PE_RAND_BLOCKS))) {
            pe_log("rand_model", "map=%d e%u: M%03u e%u doesn't fit", room, map->entity, map->from_room, map->from_entity);
            pe_disc_room_free(src);
            continue;
        }
        ++s_room_count;
        if (!own) { block = end; end += from_count; pal_row = PE_RAND_PAL_ROW + (unsigned)rows_end; rows_end += from_count; }
        pe_carry_tims(src->scratch, from, from_count, block, pe_pal(pe_word(theirs, 8)), pal_row);
        pe_carry_tims(src->texture, from, from_count, block, pe_pal(pe_word(theirs, 8)), pal_row);
        const uint32_t word = pe_placed(pe_word(mine, 8), block, from_count, pal_row);
        memcpy(mine + 8, &word, 4);
        void *rows[PE_RAND_ROWS] = {0}, *idle = NULL;
        const uint8_t *row = pe_records(src->data, 0x10, &n);
        for (unsigned i = 0; i < n; ++i, row += 12)
            if (row[11] == map->from_entity && row[7] < PE_RAND_ROWS) rows[row[7]] = src->data + (pe_word(row, 4) & 0xFFFFFF);
        for (unsigned i = PE_RAND_ROWS; i-- > 0;) if (rows[i]) idle = rows[i];   /* the lowest */
        if (rows[PE_RAND_IDLE_ROW]) idle = rows[PE_RAND_IDLE_ROW];
        for (unsigned i = 0; i < PE_RAND_ROWS; ++i)
            g_GameState.bank_rows[map->entity][i] = rows[i] ? rows[i] : g_GameState.bank_rows[map->entity][i] ? idle : NULL;
        g_GameState.bank_slots[map->entity] = s_given[map->entity] = src->data + (pe_word(theirs, 4) & 0xFFFFFF);
        pe_log("rand_model", "map=%d e%u model of M%03u e%u, blocks %d-%d", room, map->entity, map->from_room,
               map->from_entity, block, block + from_count - 1);
        ++done;
    }
    return (int)done;
}

static int pe_rand_model_rows(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    if (s_map_count || s_room_count) pe_rand_model_apply(pe_room_number());
    return 0;
}

void pe_rand_model_register(void) {
    pe_hook_add(PE_HOOK_PLAYER_ROWS, pe_rand_model_rows, NULL, PE_PRIORITY_NORMAL, MOD_ID);
}
