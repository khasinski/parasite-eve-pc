/* Battle randomizer, models (mods/pe_randomizer/pe_rand_model.c): an enemy actor shows up with another
 * actor's model, textures and animation rows. Headless, through the real loader and actor spawn.
 *
 * First every room with an enemy (pe_rand_enemies.h) is loaded once, to check what the swap counts on:
 * every model placed in the enemy VRAM area (64 x 128 blocks from x 512, column by column) ends inside it,
 * and no TIM of the room lands in a block after the room's last one, or in a palette row from 450 to 457
 * after the room's last one (those are free). Slots sharing blocks are noted (a swap into one goes to
 * free blocks). Then each creature, as its first placed actor, is swapped into a placed enemy actor
 * of another room where it fits (rotating through them, so most rooms get one), and checked:
 * - the new place: the actor's own blocks and palette rows when the model fits there and no other slot
 *   shares them, else the room's first free blocks and rows;
 * - VRAM: the new blocks and palette rows hold what the source room's loader put in its own; every other
 *   block of the area and palette row from 450 to 511 is as the room's loader left it;
 * - bank slot: the source model's bytes; rows: the source's rows by id, and the source's idle row (2, or
 *   its lowest) for an id only the actor had;
 * - a second apply in the same load does nothing;
 * - the actor spawned (Scene_LoadMap, full prims): every textured face's page and palette lie in the new
 *   blocks and rows, or both past them, as at home (the Monkey has a face on its thrown prop's block, the
 *   next slot's at home: counted, not failed).
 * And one swap that can't fit is refused (bank slot and VRAM untouched). One line per swap to argv[1].
 * Then seeds' foe plans (pe_rand_foes_generate, pe_rand_foes.h): each room with a planned swap loaded with all of
 * its swaps forced, model and AI, and every one done by the real switches (none refused for VRAM or its block).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "common.h"
#include "pe1/asset_table.h"
#include "pe1/boot.h"
#include "pe1/boot_disc_check.h"
#include "pe1/boot_services.h"
#include "pe1/cdrom.h"
#include "pe1/entity_allocation.h"
#include "pe1/field_actor.h"
#include "pe1/game_state.h"
#include "pe1/psyq_gpu.h"
#include "pe1/render_object.h"
#include "pe1/scene_room.h"
#include "pe1/scene_room_load.h"
#include "pe1/script_queue.h"
#include "pe_randomizer.h"
#include "pe_rand_enemies.h"
#include "pe_rand_foes.h"

/* Fixture I/O: the test-disc object renames the real image reads to these. */
int CdRom_ImageReadSectorsFromLba(u32 lba, void *buffer, u32 count);
int CdRom_ImagePollReady(void);
int CdRom_ReadSectorsFromLba(u32 lba, void *buffer, u32 count) { return CdRom_ImageReadSectorsFromLba(lba, buffer, count); }
int CdRom_PollReady(void) { return CdRom_ImagePollReady(); }
int Asset_Find08w(int id, int mode, s16 x, s16 y, s16 z) { (void)id; (void)mode; (void)x; (void)y; (void)z; return 0; }
void Entity_RelocateSceneData(void);
#define FAIL(...) do { if (++failures <= 40) { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)
enum { BLOCKS = 8, PAL = 450, PAL_ROWS = 62 };   /* palette rows 450-511 */
static unsigned failures;

static u32 word_at(const u8 *p, size_t at) { u32 v; memcpy(&v, p + at, 4); return v; }

static int load(unsigned room) {
    char name[16];
    snprintf(name, sizeof(name), "m%04ui", room);
    return Scene_LoadRoom((u32)Text_ParseBase32Id((signed char *)name)) == 0;
}

static const u8 *records(const u8 *blob, u32 field, unsigned *n) {
    u32 d = word_at(blob, word_at(blob, 4) + field);
    *n = d >> 22;
    return blob + (d & 0x3FFFFF);
}

static const u8 *bank_record(unsigned slot) {
    unsigned n;
    const u8 *r = records((const u8 *)g_GameState.loaded_scene_assets, 0x0C, &n);
    for (unsigned i = 0; i < n; ++i, r += 12) if (r[7] == slot) return r;
    return NULL;
}

static int block_of(unsigned x, unsigned y) { return x >= 512 && x < 768 && y < 256 ? (int)((x - 512) / 64 * 2 + y / 128) : -1; }
static int place(u32 w, int *count) { *count = (w >> 8) & 0xF; return *count ? block_of((w >> 6) & 0x3C0, (w >> 9) & 0x180) : -1; }
static unsigned pal_of(u32 w) { return ((w >> 18) & 0xFF) + 0x1C0; }

/* The enemy area and the palette rows, as one picture. */
typedef struct Area { u16 tex[256 * 256], pal[PAL_ROWS][256]; } Area;
static void grab(Area *a) {
    RECT t = {512, 0, 256, 256}, p = {0, PAL, 256, PAL_ROWS};
    StoreImage(&t, a->tex); StoreImage(&p, a->pal); DrawSync(0);
}
static int same_block(const Area *a, int ba, const Area *b, int bb) {
    for (int y = 0; y < 128; ++y)
        if (memcmp(&a->tex[(ba % 2 * 128 + y) * 256 + ba / 2 * 64], &b->tex[(bb % 2 * 128 + y) * 256 + bb / 2 * 64], 128)) return 0;
    return 1;
}
static int same_row(const Area *a, unsigned ra, const Area *b, unsigned rb) { return !memcmp(a->pal[ra - PAL], b->pal[rb - PAL], sizeof(a->pal[0])); }

/* Each placed enemy actor: its room, entity, creature, place (first block, count, first palette row), whether
 * another slot shares its blocks, and its room's first free block and palette row (from 450). */
typedef struct Placed { u16 room; u8 entity, creature; int block, count; unsigned row; int shared, end, rows_end; } Placed;
static Placed placed[PE_RAND_ENEMIES];
static unsigned placed_count;

static void census(void) {
    unsigned done[SCENE_ROOM_COUNT + 1] = {0};
    for (unsigned i = 0; i < PE_RAND_ENEMIES; ++i) {
        const PeRandEnemy *e = &g_pe_rand_enemies[i];
        if (done[e->room]++ || !load(e->room)) continue;
        unsigned n, users[BLOCKS] = {0};
        int end = 0, rows_end = 0;
        const u8 *r = records((const u8 *)g_GameState.loaded_scene_assets, 0x0C, &n);
        for (unsigned k = 0; k < n; ++k, r += 12) {
            int count, b = place(word_at(r, 8), &count), row = (int)pal_of(word_at(r, 8)) - PAL;
            if (count && row >= 0 && row < BLOCKS && row + count > rows_end) rows_end = row + count;
            if (b < 0) continue;
            if (b + count > BLOCKS) FAIL("M%03u slot %u: blocks %d-%d past the area", e->room, r[7], b, b + count - 1);
            for (int k2 = b; k2 < b + count && k2 < BLOCKS; ++k2) ++users[k2];
            if (b + count > end) end = b + count;
        }
        const u8 *blobs[2] = {(const u8 *)g_GameState.scene_load_scratch, (const u8 *)g_GameState.texture_load_scratch};
        for (int bl = 0; bl < 2; ++bl) {
            const u8 *t = records(blobs[bl], 0x28, &n);
            for (unsigned k = 0; k < n; ++k, t += 20) {
                u32 rect = word_at(t, 8), x = (rect >> 10) & 0x7FF, y = rect >> 21;
                int b = block_of(x, y);
                if (b >= end) FAIL("M%03u: a TIM at %u,%u is in free block %d", e->room, x, y, b);
                if (x < 512 && y >= PAL + (unsigned)rows_end && y < PAL + BLOCKS) FAIL("M%03u: a TIM at %u,%u is in a free palette row", e->room, x, y);
            }
        }
        for (unsigned j = i; j < PE_RAND_ENEMIES; ++j) {
            const PeRandEnemy *f = &g_pe_rand_enemies[j];
            const u8 *rec;
            int count, b;
            if (f->room != e->room || !(rec = bank_record(f->entity)) || (b = place(word_at(rec, 8), &count)) < 0) continue;
            int shared = 0;
            for (int k2 = b; k2 < b + count; ++k2) shared |= users[k2] > 1;
            placed[placed_count++] = (Placed){f->room, f->entity, f->creature, b, count, pal_of(word_at(rec, 8)), shared, end, rows_end};
        }
    }
}

static int own(const Placed *a, const Placed *b) { return b->count <= a->count && !a->shared; }
static int fits(const Placed *a, const Placed *b) { return own(a, b) || (a->end + b->count <= BLOCKS && a->rows_end + b->count <= BLOCKS); }

static Area home, before, after;
static unsigned swaps, refused, spawned_faces, borrowed_faces;

static void swap(const Placed *a, const Placed *b, FILE *out, int expect_fit) {
    /* the source, as its own room's loader leaves it */
    if (!load(b->room)) { FAIL("M%03u doesn't load", b->room); return; }
    grab(&home);
    const u8 *src = (const u8 *)g_GameState.loaded_scene_assets, *rec = bank_record(b->entity);
    u32 model_at = word_at(rec, 4) & 0xFFFFFF, model_size = word_at(rec, 0) & 0xFFFFFF;
    u8 *model = malloc(model_size);
    memcpy(model, src + model_at, model_size);
    u32 row_at[48] = {0};
    unsigned n;
    const u8 *row = records(src, 0x10, &n);
    for (unsigned i = 0; i < n; ++i, row += 12) if (row[11] == b->entity && row[7] < 48) row_at[row[7]] = word_at(row, 4) & 0xFFFFFF;
    u32 idle_at = row_at[2];
    for (int i = 47; !row_at[2] && i >= 0; --i) if (row_at[i]) idle_at = row_at[i];

    if (!load(a->room)) { FAIL("M%03u doesn't load", a->room); free(model); return; }
    grab(&before);
    int had[48];
    for (int i = 0; i < 48; ++i) had[i] = g_GameState.bank_rows[a->entity][i] != NULL;
    void *slot_was = g_GameState.bank_slots[a->entity];
    pe_rand_model_clear();
    pe_rand_model_force(a->room, a->entity, b->room, b->entity);
    int did = pe_rand_model_apply(a->room);
    grab(&after);
    if (!expect_fit) {
        if (did || g_GameState.bank_slots[a->entity] != slot_was || memcmp(&before, &after, sizeof(before)))
            FAIL("M%03u e%u <- M%03u e%u: should be refused (%d blocks into %d, room ends at %d)", a->room, a->entity, b->room, b->entity, b->count, a->count, a->end);
        else ++refused;
        if (out) fprintf(out, "refused\tM%03u e%u <- M%03u e%u\t%d blocks, own %d, room ends at %d\n", a->room, a->entity, b->room, b->entity, b->count, a->count, a->end);
        free(model);
        return;
    }
    const char *what = "";
    int count, block = place(word_at(bank_record(a->entity), 8), &count), want = own(a, b) ? a->block : a->end;
    unsigned pal_row = pal_of(word_at(bank_record(a->entity), 8)), want_row = own(a, b) ? a->row : PAL + (unsigned)a->rows_end;
    if (did != 1) what = "not applied";
    else if (block != want || count != b->count || pal_row != want_row) what = "wrong place";
    else if (memcmp(g_GameState.bank_slots[a->entity], model, model_size)) what = "bank slot isn't the model";
    else {
        const u8 *base = (const u8 *)g_GameState.bank_slots[a->entity] - model_at;
        for (int i = 0; i < 48 && !*what; ++i) {
            const void *want_row = row_at[i] ? base + row_at[i] : had[i] ? base + idle_at : NULL;
            if (g_GameState.bank_rows[a->entity][i] != want_row) what = "rows";
        }
        for (int k = 0; k < BLOCKS && !*what; ++k) {
            if (k >= block && k < block + count) { if (!same_block(&after, k, &home, b->block + k - block)) what = "new blocks"; }
            else if (!same_block(&after, k, &before, k)) what = "another block changed";
        }
        for (unsigned k = PAL; k < PAL + PAL_ROWS && !*what; ++k) {
            if (k >= pal_row && k < pal_row + (unsigned)count) { if (!same_row(&after, k, &home, b->row + k - pal_row)) what = "new palette rows"; }
            else if (!same_row(&after, k, &before, k)) what = "another palette row changed";
        }
    }
    if (!*what && pe_rand_model_apply(a->room)) what = "applied twice";
    if (!*what) {   /* spawn it: the prims Scene_LoadMap sets up point at the new blocks */
        Entity_InitFreePool(); Script_InitNodeFreeList(); Entity_RelocateSceneData();
        u8 scene[2] = {a->entity, 0};
        FieldActor *actor = Scene_LoadMap(scene, NULL, 1);
        if (!actor || actor->model_state.model_header != g_GameState.bank_slots[a->entity]) what = "spawn";
        else {
            const RenderObjectHeader *h = actor->render_object.header;
            const RenderPacket34 *q = (const RenderPacket34 *)actor->render_object.primitive_buffer;
            const RenderPacket28 *t = (const RenderPacket28 *)(q + 2 * h->packet34_count);
            for (int i = 0; i < h->packet34_count + h->packet28_count && !*what; ++i) {
                u16 page = i < h->packet34_count ? q[2 * i].page_bits : t[2 * (i - h->packet34_count)].page_bits;
                u16 clut = i < h->packet34_count ? q[2 * i].clut : t[2 * (i - h->packet34_count)].clut;
                u8 v = i < h->packet34_count ? q[2 * i].v0 : t[2 * (i - h->packet34_count)].v0;
                int fb = block_of((page & 0xF) * 64, (page & 0x10 ? 256 : 0) + v);
                int rb = fb - block, rr = (int)(clut >> 6) - (int)pal_row;
                if (rb >= count && rr >= count) ++borrowed_faces;   /* past its blocks, as at home (Monkey: its prop's) */
                else if (rb < 0 || rb >= count || rr < 0 || rr >= count) what = "a face outside the new blocks";
                ++spawned_faces;
            }
        }
    }
    if (*what) FAIL("M%03u e%u <- M%03u e%u: %s", a->room, a->entity, b->room, b->entity, what);
    else ++swaps;
    if (out) fprintf(out, "swap\tM%03u e%u <- M%03u e%u\tblocks %d-%d%s\t%s\n", a->room, a->entity, b->room, b->entity, block,
                     block + count - 1, own(a, b) ? " (own)" : " (free)", *what ? what : "ok");
    free(model);
}

static unsigned plan_rooms, plan_swaps;

static void seed_plans(unsigned seeds, FILE *out) {
    static uint16_t plan[PE_RAND_FOES];
    for (uint32_t seed = 1; seed <= seeds; ++seed) {
        pe_rand_foes_generate(g_pe_rand_foes, g_pe_rand_foe_ok[0], PE_RAND_FOES, seed, plan);
        for (unsigned i = 0, j; i < PE_RAND_FOES; i = j) {
            const unsigned room = g_pe_rand_foes[i].room;
            int want = 0;
            pe_rand_model_clear();
            pe_rand_ai_clear();
            for (j = i; j < PE_RAND_FOES && g_pe_rand_foes[j].room == room; ++j) {
                const PeRandFoe *b = &g_pe_rand_foes[plan[j]];
                if (plan[j] == j) continue;
                pe_rand_model_force(room, g_pe_rand_foes[j].entity, b->room, b->entity);
                pe_rand_ai_force(room, g_pe_rand_foes[j].entity, b->room, b->entity);
                ++want;
                if (out) fprintf(out, "plan\t%u\tM%03u e%u %s <- M%03u e%u %s\n", (unsigned)seed, room, g_pe_rand_foes[j].entity,
                                 g_pe_rand_foes[j].name, b->room, b->entity, b->name);
            }
            if (!want) continue;
            if (!load(room)) { FAIL("M%03u doesn't load", room); continue; }
            const int models = pe_rand_model_apply((int)room), ais = pe_rand_ai_apply((int)room);
            if (models != want || ais != want)
                FAIL("seed %u M%03u: %d swaps planned, %d models and %d scripts done", (unsigned)seed, room, want, models, ais);
            ++plan_rooms;
            plan_swaps += (unsigned)want;
        }
    }
    pe_rand_model_clear();
    pe_rand_ai_clear();
}

int main(int argc, char **argv) {
    const char *path = getenv("PE_TEST_DISC");
    if (!path || !*path) return getenv("PE_REQUIRE_GAME_ASSETS") ? 1 : 77;
    PeDisc disc = {0};
    if (!SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "PSYZ_HEADLESS", "1", true) ||
        !SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "SDL_AUDIODRIVER", "dummy", true)) return 1;
    if (!SDL_Init(SDL_INIT_EVENTS) || !pe_disc_open(&disc, path)) return 1;
    ResetGraph(0); CdRom_BindImage(&disc, NULL);
    if (Asset_OpenPeImage() != 0) return 1;
    Boot_InitGameState(); D_8009CDC8 = 'm';
    if (Scene_LoadSceneData() < 0) { fputs("Scene_LoadSceneData failed\n", stderr); return 1; }
    FILE *out = argc > 1 ? fopen(argv[1], "w") : NULL;
    if (argc > 1 && !out) return 1;

    census();
    unsigned pairs = 0, fit = 0, creatures = 0, next = 0;
    for (unsigned i = 0; i < placed_count; ++i)
        for (unsigned j = 0; j < placed_count; ++j) { ++pairs; fit += fits(&placed[i], &placed[j]); }
    const Placed *no_a = NULL, *no_b = NULL;
    for (unsigned j = 0; j < placed_count; ++j) {
        unsigned k;
        for (k = 0; k < j && placed[k].creature != placed[j].creature; ++k) {}
        if (k < j) continue;                                 /* a creature once, as its first placed actor */
        ++creatures;
        for (k = 0; k < placed_count; ++k) {
            const Placed *a = &placed[(next + k) % placed_count];
            if (a->room != placed[j].room && fits(a, &placed[j])) { swap(a, &placed[j], out, 1); next = (next + k + 1) % placed_count; break; }
            if (!no_a && a->room != placed[j].room && !fits(a, &placed[j])) { no_a = a; no_b = &placed[j]; }
        }
        if (k == placed_count) FAIL("M%03u e%u: fits in no other room", placed[j].room, placed[j].entity);
    }
    if (no_a) swap(no_a, no_b, out, 0); else FAIL("no pair that doesn't fit");
    seed_plans(10, out);
    if (out) fclose(out);
    CdRom_BindImage(NULL, NULL); pe_disc_close(&disc);
    if (failures) { fprintf(stderr, "%u failures\n", failures); return 1; }
    printf("Randomizer models: %u placed enemy actors (%u of %u pairs fit alone); %u creatures swapped in and checked "
           "(model, rows, VRAM blocks and palettes, %u spawned faces, %u past its blocks as at home), %u misfit refused; "
           "10 seeds' foe plans: %u room loads, %u swaps done\n",
           placed_count, fit, pairs, swaps, spawned_faces, borrowed_faces, refused, plan_rooms, plan_swaps);
    return 0;
}
