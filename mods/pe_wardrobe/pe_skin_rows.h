#ifndef PE_SKIN_ROWS_H
#define PE_SKIN_ROWS_H
/* Private row-binding core shared verbatim by the loader and its regression
 * fixture. No file I/O, pack providers, hooks, textures or asset-loading calls.
 * Static storage belongs to the including loader (or isolated test instance).
 */
#include <string.h>
#include "pe1/field_actor.h"
#include "pe1/game_state.h"
#include "pe1/player_entity.h"
#include "pe1/scene_assets.h"

#define PE_MAX_SKINS     64
#define PE_ROWS_PER_BANK 48
#define PE_ROOM_SLOT0    24          /* room animation rows: bank_rows[0][24..47] */
#define PE_ROOM_SLOTS    24
#define PE_BANK_LONG_DRESS 11

typedef struct { u8 *data; u32 size; } PeBlob;

typedef struct {
    char folder[72];
    int checked, present;   /* model.bin found (lists ask every frame) */
    int loaded, ok;
    PeBlob model, tex, day[9], rooms;
    u32 nrooms;
    int gun_part;
} PeSkin;

static PeSkin s_skin[PE_MAX_SKINS];
static int s_skins;
static PeSkin *s_room_pack;             /* the room pack whose rows are in, with the game's model */
static void *s_replaced[PE_ROWS_PER_BANK];   /* the game's row each pointed slot held */

static u32 rd32(const u8 *b, u32 o) { return (u32)b[o] | (u32)b[o + 1] << 8 | (u32)b[o + 2] << 16 | (u32)b[o + 3] << 24; }

/* A blob in the game's container format: its directory is inside it. */
static const SceneAssetDirectory *pe_dir(const PeBlob *b) {
    u32 at;
    if (!b->data || b->size < 8) return NULL;
    at = rd32(b->data, 4);
    return at + sizeof(SceneAssetDirectory) <= b->size ? (const SceneAssetDirectory *)(b->data + at) : NULL;
}

/* ---- pointers ---- */
/* The bundle's model and rows, and a day blob's rows, as the game resolves them (Scene_Load
 * EntityTexture step 6, Scene_LoadEntityTextures steps 6 / 13; not the day blob's stream tables). */
static void pe_point_bundle(u8 *blob) {
    const SceneAssetDirectory *dir = SceneAsset_ResolveOffset(blob, ((SceneAssetBlob *)blob)->directoryOffset);
    const SceneBankAssetRecord *root = SceneAsset_ResolveOffset(blob, dir->bankRootEntries & 0x3FFFFFu);
    const SceneBankAssetRecord *rec = SceneAsset_ResolveOffset(blob, dir->bankRowEntries & 0x3FFFFFu);
    u32 i;
    g_GameState.bank_slots[0] = SceneAsset_ResolveOffset(blob, root->source.offsetAndId & 0xFFFFFFu);
    for (i = 0; i < dir->bankRowEntries >> 22; ++i) {
        const unsigned id = rec[i].source.bytes.id;
        /* The Day 1 long dress keeps its scene rows (28-34) in room slots: pe_room_rows swaps them
         * for their copies; the skin's jacket row 28 would replace the scene's animation. */
        if (id >= PE_ROOM_SLOT0 && g_SceneAreaType == PE_BANK_LONG_DRESS) continue;
        if (id < 10 * PE_ROWS_PER_BANK)
            g_GameState.bank_rows[id / PE_ROWS_PER_BANK][id % PE_ROWS_PER_BANK] =
                SceneAsset_ResolveOffset(blob, rec[i].source.offsetAndId & 0xFFFFFFu);
    }
}

static void pe_point_day(u8 *blob, int remember) {
    const SceneAssetDirectory *dir = SceneAsset_ResolveOffset(blob, ((SceneAssetBlob *)blob)->directoryOffset);
    const SceneBankAssetRecord *rec = SceneAsset_ResolveOffset(blob, dir->bankRowEntries & 0x3FFFFFu);
    u32 i;
    for (i = 0; i < dir->bankRowEntries >> 22; ++i) {
        const unsigned id = rec[i].source.bytes.id;
        void *want = SceneAsset_ResolveOffset(blob, rec[i].source.offsetAndId & 0xFFFFFFu);
        if (id >= PE_ROWS_PER_BANK || g_GameState.bank_rows[0][id] == want) continue;
        if (remember) s_replaced[id] = g_GameState.bank_rows[0][id];
        g_GameState.bank_rows[0][id] = want;
    }
}

static int pe_day(void) {
    const int day = g_GameState.current_story_day;
    return day >= 1 && day <= 8 ? day : 1;
}

/* ---- room animations ---- */
static u32 pe_row_length(const u8 *p, u32 size) {
    if (size < 12u) return 0;
    const unsigned flags = p[0], last = p[1], frames = p[2];
    const u32 stride = ((frames >> 1) + 1u) * 4u;
    const int shrt = (flags & 3u) == 2u;
    const u32 bstride = shrt ? stride : ((frames >> 2) + 1u) * 4u;
    u32 cur = 12u, k;
    for (k = 0; k < 3; ++k) {
        if (cur > size || size - cur < 4u) return 0;
        cur += (p[cur] | p[cur + 1] << 8) ? 4u : stride;
    }
    for (k = 0; k < (last + 1u) * 3u; ++k) {
        if (cur > size || size - cur < 4u) return 0;
        cur += (shrt ? (p[cur] | p[cur + 1] << 8) != 0 : p[cur] != 0) ? 4u : bstride;
    }
    if (cur > size) return 0;
    return cur;
}

static u32 pe_fingerprint(const u8 *p) {
    const u32 n = p ? pe_row_length(p, 0x10000u) : 0;
    u32 h = 0x811C9DC5u, i;
    if (n < 300u) return 0;     /* the shortest: one frame on the 23-part long dress */
    for (i = 0; i < n; ++i) h = (h ^ p[i]) * 0x01000193u;
    return h;
}

/* The retargeted copy of Aya's room row `p`, or NULL (the table is sorted by fingerprint). */
static void *pe_room_copy(const PeSkin *k, const void *p) {
    const u32 fp = pe_fingerprint(p);
    const u8 *t = k->rooms.data + 8;
    int lo = 0, hi = (int)k->nrooms - 1;
    if (!fp) return NULL;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        const u32 f = rd32(t, 12u * (u32)mid);
        if (f == fp) {
            const u32 off = rd32(t, 12u * (u32)mid + 4), size = rd32(t, 12u * (u32)mid + 8);
            if (off > k->rooms.size || size > k->rooms.size - off) return NULL;
            return pe_row_length(k->rooms.data + off, size) ? k->rooms.data + off : NULL;
        }
        if (f < fp) lo = mid + 1;
        else hi = mid - 1;
    }
    return NULL;
}

static int pe_ours(const PeSkin *k, const void *p) {
    const u8 *b = p;
    int d;
    if (b >= k->model.data && b < k->model.data + k->model.size) return 1;
    if (k->nrooms && b >= k->rooms.data && b < k->rooms.data + k->rooms.size) return 1;
    for (d = 1; d <= 8; ++d) if (b >= k->day[d].data && b < k->day[d].data + k->day[d].size) return 1;
    return 0;
}

/* A row drives exactly as many bones as the model Aya wears has parts; more would write past her
 * matrices. */
static int pe_fits(const void *row) {
    const RenderObjectHeader *h = g_GameState.bank_slots[0];
    return h && ((const u8 *)row)[1] + 1u == h->part_count;
}

static int pe_any_ours(const void *p) {
    int i;
    for (i = 0; i < s_skins; ++i) if (s_skin[i].ok && pe_ours(&s_skin[i], p)) return 1;
    return 0;
}

/* Put the game's own row back in every slot still pointing at a skin's or room pack's copy, before
 * another goes on: a copy is matched by Aya's original's fingerprint, never by another copy's, so
 * one left in place would play on the next model (a 31-bone evening row on 29-part Harlequin).
 * A slot the room load has refilled since is the game's already and stays. */
static void pe_restore_rows(void) {
    int slot;
    for (slot = 0; slot < PE_ROWS_PER_BANK; ++slot)
        if (s_replaced[slot] && pe_any_ours(g_GameState.bank_rows[0][slot])) g_GameState.bank_rows[0][slot] = s_replaced[slot];
    memset(s_replaced, 0, sizeof s_replaced);
    s_room_pack = NULL;
}

static void pe_room_rows(const PeSkin *k) {
    int slot;
    if (!k->nrooms) return;
    for (slot = PE_ROOM_SLOT0; slot < PE_ROOM_SLOT0 + PE_ROOM_SLOTS; ++slot) {
        void *p = g_GameState.bank_rows[0][slot], *copy;
        if (!p || pe_ours(k, p) || !(copy = pe_room_copy(k, p)) || !pe_fits(copy)) continue;
        s_replaced[slot] = p;
        g_GameState.bank_rows[0][slot] = copy;
    }
}

/* A row the game started before we pointed its slot (a door script installs and starts it in one
 * frame): swap the running animation for ours, same frame count. */
static void pe_running_row(const PeSkin *k) {
    FieldActor *player = g_PlayerEntity;
    void *a, *copy = NULL;
    int slot;
    if (!player || !(a = player->action_data) || pe_ours(k, a)) return;
    for (slot = 0; slot < PE_ROWS_PER_BANK && !copy; ++slot)
        if (s_replaced[slot] == a && pe_ours(k, g_GameState.bank_rows[0][slot])) copy = g_GameState.bank_rows[0][slot];
    if (!copy && k->nrooms) copy = pe_room_copy(k, a);
    if (copy && ((u8 *)copy)[2] == ((u8 *)a)[2] && pe_fits(copy)) player->action_data = copy;
}

#endif
