#ifndef PE_SKIN_ROWS_H
#define PE_SKIN_ROWS_H
/* Private row-binding core shared verbatim by the loader and its regression
 * fixture. No file I/O, pack providers, hooks, textures or asset-loading calls.
 * Static storage belongs to the including loader (or isolated test instance).
 */
#include <stdlib.h>
#include <string.h>
#include "pe1/field_actor.h"
#include "pe1/game_state.h"
#include "pe1/player_entity.h"
#include "pe1/scene_assets.h"

void pe_log(const char *tag, const char *fmt, ...);   /* pe_mod_host.h; the fixture stubs it */

#define PE_MAX_SKINS     64
#define PE_ROWS_PER_BANK 48
#define PE_ROOM_SLOT0    24          /* room animation rows: bank_rows[0][24..47] */
#define PE_ROOM_SLOTS    24
#define PE_BANK_LONG_DRESS 11
#define PE_IDLE_ROW      0x15        /* her idle row: the rest pose for bones a row lacks */

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
static int s_fitting;                   /* a game outfit's rows are fitted to its model (pe_fit_rows) */
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

/* Hold rows: the skin's idle pose for a game row that doesn't fit the skin and has no copy. */
#define PE_HOLD_MAX (12u + (3u + 64u * 3u) * 4u)
static u8 s_hold[PE_ROWS_PER_BANK + 1][PE_HOLD_MAX];   /* one per slot, the last for a running row */

static int pe_is_hold(const void *p) {
    return (const u8 *)p >= s_hold[0] && (const u8 *)p < s_hold[0] + sizeof s_hold;
}

static int pe_ours(const PeSkin *k, const void *p) {
    const u8 *b = p;
    int d;
    if (pe_is_hold(p)) return 1;
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

/* The skin bundle's own row `id`, or NULL. */
static const u8 *pe_skin_row(const PeSkin *k, unsigned id) {
    const SceneAssetDirectory *dir = pe_dir(&k->model);
    const SceneBankAssetRecord *rec;
    u32 i;
    if (!dir) return NULL;
    rec = SceneAsset_ResolveOffset(k->model.data, dir->bankRowEntries & 0x3FFFFFu);
    for (i = 0; i < dir->bankRowEntries >> 22; ++i)
        if (rec[i].source.bytes.id == id) return SceneAsset_ResolveOffset(k->model.data, rec[i].source.offsetAndId & 0xFFFFFFu);
    return NULL;
}

/* Frame 0 of each channel (3 translations, then 3 angles per bone). Constant or not, the first
 * sample sits right after the marker: a halfword for wide channels, a byte otherwise. */
static int pe_first_samples(const u8 *p, s16 *out, unsigned max) {
    const unsigned frames = p[2], n = 3u + (p[1] + 1u) * 3u;
    const int shrt = (p[0] & 3u) == 2u;
    const u32 stride = ((frames >> 1) + 1u) * 4u, bstride = shrt ? stride : ((frames >> 2) + 1u) * 4u;
    u32 cur = 12u;
    unsigned c;
    if (n > max || !pe_row_length(p, 0x10000u)) return 0;
    for (c = 0; c < n; ++c) {
        const int wide = c < 3 || shrt;
        const int constant = wide ? (p[cur] | p[cur + 1] << 8) != 0 : p[cur] != 0;
        out[c] = wide ? (s16)(p[cur + 2] | p[cur + 3] << 8) : (s16)p[cur + 1];
        cur += constant ? 4u : wide ? stride : bstride;
    }
    return (int)n;
}

/* A row the skin can play in place of `game` (more or fewer bones than its parts): the skin's idle
 * pose held still, with the game row's frame count, hit values and starting position, so scripts
 * waiting on the animation time it the same and nothing reads past the skin's matrices. */
static void *pe_hold_row(const PeSkin *k, const u8 *game, int slot) {
    const RenderObjectHeader *h = g_GameState.bank_slots[0];
    const u8 *idle = pe_skin_row(k, PE_IDLE_ROW);
    s16 g[3 + 128 * 3], s[3 + 64 * 3];
    u8 *o = s_hold[slot];
    unsigned c, n;
    int shrt;
    if (!h || !idle || idle[1] + 1u != h->part_count || !pe_first_samples(game, g, 3 + 128 * 3) ||
        !(n = (unsigned)pe_first_samples(idle, s, 3 + 64 * 3)))
        return NULL;
    shrt = (idle[0] & 3u) == 2u;
    memcpy(o, idle, 2);
    o[2] = game[2];
    memcpy(o + 3, game + 3, 9);
    for (c = 0; c < n; ++c) {
        u8 *ch = o + 12u + 4u * c;
        const s16 v = c < 3 ? g[c] : s[c];
        if (c < 3 || shrt) { ch[0] = 1; ch[1] = 0; ch[2] = (u8)v; ch[3] = (u8)((u16)v >> 8); }
        else { ch[0] = 1; ch[1] = (u8)v; ch[2] = 0; ch[3] = 0; }
    }
    return o;
}

/* Any slot still holding a game row the skin can't play gets a hold row. */
static void pe_guard_rows(const PeSkin *k) {
    int slot;
    for (slot = 0; slot < PE_ROWS_PER_BANK; ++slot) {
        u8 *p = g_GameState.bank_rows[0][slot];
        void *hold;
        if (!p || pe_ours(k, p) || pe_fits(p) || !pe_row_length(p, 0x10000u)) continue;
        if (!(hold = pe_hold_row(k, p, slot))) continue;
        pe_log("skin", "%s: slot %d row has %u bones, no copy: idle pose held", k->folder, slot, p[1] + 1u);
        if (!s_replaced[slot]) s_replaced[slot] = p;
        g_GameState.bank_rows[0][slot] = hold;
    }
}

/* ---- rows fitted to the model ----
 * A game outfit worn where the story has another skeleton plays that skeleton's room animations
 * when no room pack has a copy: a jacket row (29 bones) on the evening dress (31 parts) leaves her
 * last two parts undecoded. Their matrices are composed in place every frame, so the stale ones
 * compound into a flickering, stretched piece of the gown (issue #220); a longer row would write
 * past her matrices. A fitted copy drives exactly her parts: missing bones hold her idle row's
 * first pose (identity when that row doesn't fit either), extra bones are dropped. Copies are
 * kept by content for the session, so a slot or running animation never points at freed memory. */
#define PE_MAX_FITTED 512

typedef struct { u32 hash, size; const void *model; u8 *data; } PeFitted;
static PeFitted s_fitted[PE_MAX_FITTED];
static int s_nfitted;

/* The offset of channel `index` (0-2 the root translation, then three per bone), or 0. */
static u32 pe_channel_at(const u8 *p, u32 size, u32 index) {
    const unsigned flags = p[0], frames = p[2];
    const u32 stride = ((frames >> 1) + 1u) * 4u;
    const int shrt = (flags & 3u) == 2u;
    const u32 bstride = shrt ? stride : ((frames >> 2) + 1u) * 4u;
    u32 cur = 12u, k;
    for (k = 0; k < index; ++k) {
        if (cur > size || size - cur < 4u) return 0;
        if (k < 3) cur += (p[cur] | p[cur + 1] << 8) ? 4u : stride;
        else cur += (shrt ? (p[cur] | p[cur + 1] << 8) != 0 : p[cur] != 0) ? 4u : bstride;
    }
    return cur <= size && size - cur >= 4u ? cur : 0;
}

/* Bone `bone`'s angle on `axis` at frame 0, in the decoder's 4096-per-turn units. */
static unsigned pe_rest_angle(const u8 *p, u32 size, unsigned bone, unsigned axis) {
    const u32 at = pe_channel_at(p, size, 3u + bone * 3u + axis);
    if (!at) return 0;
    if ((p[0] & 3u) == 2u) return (unsigned)(p[at + 2] | p[at + 3] << 8);
    return p[at + 1] * 0x10u;
}

static u32 pe_row_hash(const u8 *p, u32 n) {
    u32 h = 0x811C9DC5u, i;
    for (i = 0; i < n; ++i) h = (h ^ p[i]) * 0x01000193u;
    return h;
}

static int pe_fitted_ours(const void *p) {
    int i;
    for (i = 0; i < s_nfitted; ++i) if (s_fitted[i].data == p) return 1;
    return 0;
}

/* `row` driving exactly the worn model's parts: the row itself when it already does, else a
 * fitted copy, NULL if it can't be made. */
static void *pe_fit_row(void *row) {
    const RenderObjectHeader *h = g_GameState.bank_slots[0];
    const u8 *p = row, *rest = g_GameState.bank_rows[0][PE_IDLE_ROW];
    u32 n, size, hash, rest_size;
    unsigned have, want, bone, axis;
    u8 *copy;
    int i;
    if (!row || !h || !h->part_count || pe_fitted_ours(row)) return row;
    have = p[1] + 1u;
    want = h->part_count;
    if (have == want) return row;
    if (!(n = pe_row_length(p, 0x10000u))) return NULL;
    hash = pe_row_hash(p, n);
    for (i = 0; i < s_nfitted; ++i)
        if (s_fitted[i].hash == hash && s_fitted[i].size == n && s_fitted[i].model == h) return s_fitted[i].data;
    if (s_nfitted == PE_MAX_FITTED) return NULL;
    size = want < have ? pe_channel_at(p, n, 3u + want * 3u) : n + (want - have) * 12u;
    if (!size || !(copy = malloc(size))) return NULL;
    memcpy(copy, p, size < n ? size : n);
    copy[1] = (u8)(want - 1u);
    rest_size = rest && rest[1] + 1u == want ? pe_row_length(rest, 0x10000u) : 0;
    for (bone = have; bone < want; ++bone)
        for (axis = 0; axis < 3; ++axis) {
            u8 *c = copy + n + ((bone - have) * 3u + axis) * 4u;
            const unsigned angle = rest_size ? pe_rest_angle(rest, rest_size, bone, axis) : 0;
            c[0] = 1;                                    /* a constant channel */
            c[1] = c[2] = c[3] = 0;
            if ((p[0] & 3u) == 2u) {
                c[2] = (u8)angle;
                c[3] = (u8)(angle >> 8);
            } else c[1] = (u8)(angle >> 4);
        }
    s_fitted[s_nfitted++] = (PeFitted){hash, n, h, copy};
    return copy;
}

/* Every room slot and the running animation fitted to the worn game outfit's model; 1 if any
 * pointer changed. */
static int pe_fit_rows(void) {
    FieldActor *player = g_PlayerEntity;
    void *p, *fit;
    int slot, changed = 0;
    for (slot = PE_ROOM_SLOT0; slot < PE_ROOM_SLOT0 + PE_ROOM_SLOTS; ++slot) {
        p = g_GameState.bank_rows[0][slot];
        if (!p || (fit = pe_fit_row(p)) == p || !fit) continue;
        if (!s_replaced[slot]) s_replaced[slot] = p;
        g_GameState.bank_rows[0][slot] = fit;
        changed = 1;
    }
    if (player && player->model_state.model_header == g_GameState.bank_slots[0] && (p = player->action_data) &&
        (fit = pe_fit_row(p)) && fit != p) {
        player->action_data = fit;
        changed = 1;
    }
    return changed;
}

static int pe_any_ours(const void *p) {
    int i;
    if (pe_fitted_ours(p)) return 1;
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
    s_fitting = 0;
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
    else if (!pe_fits(a) && (copy = pe_hold_row(k, a, PE_ROWS_PER_BANK)) != NULL) {
        pe_log("skin", "%s: running row has %u bones, no copy: idle pose held", k->folder, ((u8 *)a)[1] + 1u);
        player->action_data = copy;
    }
}

#endif
