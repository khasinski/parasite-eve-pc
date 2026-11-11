/* Every room through the real loader, for the randomizer's room map. Scene_LoadRoom must accept each
 * room on the disc; the task block it relocates must walk cleanly from every entity entry: each step
 * reached has a native handler, no step overlaps data a step points at, and every door (op 49) names
 * a room on the disc. With a path argument the walk is written there, one line per step, for
 * mods/pe_randomizer/tools. PE_TEST_DISC2, when set, must hold the same rooms as disc 1.
 *
 * Walk rules (from the native handlers): a step is two words (opcode | count << 13 | modes << 17, then
 * the modes of operands 6+) and its operands. Script offsets are halfwords from the entry of the entity
 * running them. Code targets: op 0 goto, 5 jump if zero (not on a constant nonzero), 29 branch, 18 child
 * routine, 20 event routine (selector 1-3). Data targets: ops 172, 226, 227. A routine ends at op 0,
 * 1 or 32, at the FFFFFFFF word between routines, or at a door followed by that word. Door lines end
 * with the target: M0000, M5000 and M8888 are the scenes named at door_room.
 *
 * The randomizer's table (pe_rand_spots.h) against the loaded rooms: every site must hold the 16 bytes
 * it expects and start a walked step, every listed GiveItem must be a walked op 167. A second path
 * argument gets one line per site and one per walked GiveItem (op 167), naming the spot that lists it
 * or "-": the gives the randomizer doesn't touch.
 *
 * Opened / taken flags: a chest sets its flag just before its slot give (op 167, item from an actor
 * slot), a pickup just after; the flag is an op 9 OR of one bit into a temp, stored back by the op 10
 * that follows. Each such write belongs to the nearest slot give of its entity within 0x100 bytes
 * (tools/spottable.py merges spots by the same rule). Gives sharing a flag are one chest or pickup:
 * they must all be one spot, or all be left out. Flag lines go to the second output too.
 *
 * Placed actors: a room places its starting actors (Script_DrawSyncAndFlush: at the task block's first
 * word, a count byte, then type and sub id pairs) and the types its walked spawn steps name (op 8, op 17
 * 2500). Every listed give must be in a placed actor: another actor's script never runs, and the item the
 * randomizer puts there is lost (tools/spottable.py leaves those gives out by the same rule). Their flag
 * writes never run either and are left out of the flag check.
 *
 * Locks (pe_rand_locks.h): every walked item-count step (op 168) on a key must be a table lock with that
 * key, and every table lock such a step. The second output gets one keytest line each (tools/locktable.py). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "common.h"
#include "pe1/asset_table.h"
#include "pe1/boot_disc_check.h"
#include "pe1/boot_services.h"
#include "pe1/cdrom.h"
#include "pe1/game_state.h"
#include "pe1/reloc_block.h"
#include "pe1/scene_room.h"
#include "pe1/scene_room_load.h"
#include "pe1/script_queue.h"
#include "pe_rand_spots.h"
#include "pe_rand_locks.h"

/* Fixture I/O: the test-disc object renames the real image reads to these. */
int CdRom_ImageReadSectorsFromLba(u32 lba, void *buffer, u32 count);
int CdRom_ImagePollReady(void);
int CdRom_ReadSectorsFromLba(u32 lba, void *buffer, u32 count) { return CdRom_ImageReadSectorsFromLba(lba, buffer, count); }
int CdRom_PollReady(void) { return CdRom_ImagePollReady(); }
int Asset_Find08w(int id, int mode, int x, int y, int z) { (void)id; (void)mode; (void)x; (void)y; (void)z; return 0; }

enum { WORD_NONE, WORD_STEP, WORD_DATA, WORD_END };
static unsigned failures;
#define FAIL(...) do { if (++failures <= 40) { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static u32 word_at(const u8 *block, size_t offset) { u32 v; memcpy(&v, block + offset, 4); return v; }

typedef struct Step { u32 op, count, modes[15], values[15]; size_t bytes; } Step;
static void decode(const u8 *block, size_t at, Step *step) {
    u32 header = word_at(block, at), modes = header >> 17;
    step->op = header & 0x1FFF; step->count = (header >> 13) & 15; step->bytes = 8 + 4 * (size_t)step->count;
    for (u32 k = 0; k < step->count; ++k) {
        if (k == 5) modes = word_at(block, at + 4);
        step->modes[k] = modes & 7; step->values[k] = word_at(block, at + 8 + 4 * (size_t)k); modes >>= 3;
    }
}

/* Three door tokens are scenes of the main loop, not rooms (boot/main.c): M0000 runs the effect-scene
 * loop, M5000 the title screen, M8888 the boot display (then M431). */
enum { DOOR_NONE = -1, DOOR_EFFECT_SCENE = 0, DOOR_TITLE = 5000, DOOR_BOOT_DISPLAY = 8888 };

/* Room number a door token names, a scene above, or DOOR_NONE. */
static int door_room(u32 token) {
    char name[8] = {0};
    Text_EncodeBase32(name, token); /* upper case: M0005I */
    if (name[0] != 'M' || name[5] != 'I') return DOOR_NONE;
    for (int i = 1; i < 5; ++i) if (name[i] < '0' || name[i] > '9') return DOOR_NONE;
    int room = atoi(name + 1);
    if (room == DOOR_EFFECT_SCENE || room == DOOR_TITLE || room == DOOR_BOOT_DISPLAY) return room;
    return room >= 1 && room <= SCENE_ROOM_COUNT && g_RoomSectorTable[room - 1].roomSectors ? room : DOOR_NONE;
}

static const char *const spot_kinds[] = { "chest", "pickup", "gift" };
static unsigned sites_total, gives_listed, gives_walked, gives_unlisted, gives_unplaced, locks_walked;

/* Spot of the listed GiveItem at this room offset, or -1. */
static int listed_give(unsigned room, size_t at) {
    for (unsigned i = 0; i < PE_RAND_GIVES; ++i)
        if (g_pe_rand_gives[i].map == room && g_pe_rand_gives[i].at == at) return g_pe_rand_gives[i].spot;
    return -1;
}

/* A table offset (from the room data's start) that starts a walked step of the task block at offset. */
static int walked(const u8 *kind, size_t offset, size_t base, size_t at) {
    return at >= offset && at - offset < base && (at - offset) % 4 == 0 && kind[(at - offset) / 4] == WORD_STEP;
}

/* Every flag write next to a slot give: (variable, bit) and the give that owns it. */
typedef struct FlagOwner { u32 var, bit, at; unsigned room; int spot; } FlagOwner;
static FlagOwner flag_owners[4096];
static unsigned flag_count;

static void find_flags(unsigned room, const u8 *assets, size_t offset, size_t base, const u8 *kind, const u16 *entity_of,
                       const u8 *placed, const u32 *gives, const u32 *give_entity, unsigned give_count, FILE *spots) {
    for (size_t at = 0; at + 8 <= base; at += 4) {
        if (kind[at / 4] != WORD_STEP || !placed[entity_of[at / 4]]) continue;   /* an actor never placed never sets it */
        Step s, t; decode(assets + offset, at, &s);
        if (s.op != 9 || s.count != 4 || s.modes[0] || s.values[0] != 2 || s.modes[1] != 3 || s.modes[2] != 2 || s.modes[3]) continue;
        size_t next = at + s.bytes;
        if (next + 8 > base || kind[next / 4] != WORD_STEP || entity_of[next / 4] != entity_of[at / 4]) continue;
        decode(assets + offset, next, &t);
        if (t.op != 10 || t.count != 2 || t.modes[0] != 2 || t.values[0] != s.values[2] || t.modes[1] != 3 || t.values[1] != s.values[1]) continue;
        int owner = -1; size_t best = 0x101;
        for (unsigned i = 0; i < give_count; ++i) {
            Step g; decode(assets + offset, gives[i], &g);
            size_t gap = gives[i] > at ? gives[i] - at : at - gives[i];
            if (give_entity[i] == entity_of[at / 4] && g.count == 2 && g.modes[0] == 1 && gap < best) { owner = (int)i; best = gap; }
        }
        if (owner < 0) continue;
        if (flag_count == sizeof(flag_owners) / sizeof(flag_owners[0])) { FAIL("more than %u flag writes", flag_count); return; }
        FlagOwner *f = &flag_owners[flag_count++];
        f->var = s.values[2]; f->bit = s.values[3]; f->at = (u32)(offset + gives[owner]); f->room = room;
        f->spot = listed_give(room, f->at);
        if (spots && f->spot >= 0) fprintf(spots, "flag\tM%03u\t+%06X\tV%#x|%#x\tspot=%d\n", room, f->at, f->var, f->bit, f->spot);
        else if (spots) fprintf(spots, "flag\tM%03u\t+%06X\tV%#x|%#x\t-\n", room, f->at, f->var, f->bit);
    }
}

/* Gives sharing a flag are one spot, or all unlisted. */
static void check_flags(void) {
    for (unsigned i = 0; i < flag_count; ++i)
        for (unsigned j = 0; j < i; ++j) {
            const FlagOwner *a = &flag_owners[i], *b = &flag_owners[j];
            if (a->var == b->var && a->bit == b->bit && a->spot != b->spot)
                FAIL("V%#x|%#x: M%03u +%06X (spot %d) and M%03u +%06X (spot %d) share it", a->var, a->bit,
                     b->room, b->at, b->spot, a->room, a->at, a->spot);
        }
}

static void audit_spots(unsigned room, const u8 *assets, size_t room_bytes, size_t offset, size_t base, const u8 *kind,
                        const u16 *entity_of, const u8 *placed, const u32 *gives, const u32 *give_entity,
                        unsigned give_count, FILE *spots) {
    for (unsigned i = 0; i < PE_RAND_SITES; ++i) {
        const PeRandSite *s = &g_pe_rand_sites[i];
        if (s->map != room) continue;
        ++sites_total;
        int same = s->at + 16 <= room_bytes;
        for (unsigned w = 0; same && w < 4; ++w) same = word_at(assets, s->at + 4 * w) == s->expect[w];
        int on_step = walked(kind, offset, base, s->at);
        if (!same) FAIL("M%03u site +%06X (spot %u) differs from the table's bytes", room, s->at, s->spot);
        if (!on_step) FAIL("M%03u site +%06X (spot %u) is no walked step", room, s->at, s->spot);
        if (spots) fprintf(spots, "site\tM%03u\t+%06X\tspot=%u\t%s\top=%u\t%s\n", room, s->at, s->spot,
                           spot_kinds[g_pe_rand_spots[s->spot].kind], s->expect[0] & 0x1FFF, on_step ? "walked" : "unwalked");
    }
    for (unsigned i = 0; i < PE_RAND_GIVES; ++i) {
        const PeRandGive *g = &g_pe_rand_gives[i];
        if (g->map != room) continue;
        ++gives_listed;
        if (!walked(kind, offset, base, g->at) || (word_at(assets, g->at) & 0x1FFF) != 167)
            FAIL("M%03u give +%06X (spot %u) is no walked GiveItem", room, g->at, g->spot);
        else if (!placed[entity_of[(g->at - offset) / 4]])
            FAIL("M%03u give +%06X (spot %u): actor %u is never placed", room, g->at, g->spot, entity_of[(g->at - offset) / 4]);
    }
    for (unsigned i = 0; i < give_count; ++i) {
        size_t at = offset + gives[i];
        int spot = listed_give(room, at);
        Step s; decode(assets, at, &s);
        ++gives_walked; if (spot < 0) ++gives_unlisted; if (!placed[give_entity[i]]) ++gives_unplaced;
        if (!spots) continue;
        fprintf(spots, "give\tM%03u\t+%06zX\t%u\t%#x", room, at, give_entity[i], gives[i]);
        for (u32 k = 0; k < s.count; ++k) fprintf(spots, "%s%u:%#x", k ? "," : "\t", s.modes[k], s.values[k]);
        if (spot >= 0) fprintf(spots, "\tspot=%d\t%s\n", spot, spot_kinds[g_pe_rand_spots[spot].kind]);
        else fputs("\t-\n", spots);
    }
}

static void walk_room(unsigned room, FILE *out, FILE *spots, unsigned *steps_total, unsigned *doors_total) {
    const u8 *assets = (const u8 *)g_GameState.loaded_scene_assets;
    size_t room_bytes = (size_t)g_RoomSectorTable[room - 1].roomSectors * 2048;
    const SceneRoomDirectory *directory = (const void *)(assets + word_at(assets, 4));
    u32 table = directory->taskBlock & 0x3FFFFF;
    size_t size = word_at(assets, table) & 0xFFFFFF, offset = word_at(assets, table + 4) & 0xFFFFFF;
    const u8 *block = assets + offset;
    u32 base = word_at(block, 0), count = word_at(block, 4);
    const RelocBlock *tasks = g_GameState.bank_reset_944[0];
    if (offset + size > room_bytes || base > size || 8 + 4 * (size_t)count > size) { FAIL("M%03u task block out of range", room); return; }
    if (!tasks || tasks->count != count || tasks->u0.base != block + base) { FAIL("M%03u relocated task block differs from the room's", room); return; }
    u32 *entries = malloc(count * sizeof(u32));
    u8 *kind = calloc(size / 4 + 1, 1);
    u16 *entity_of = calloc(size / 4 + 1, sizeof(u16));
    size_t *work = malloc((size / 4 + count + 1) * sizeof(size_t));
    u32 *gives = malloc((size / 8 + 1) * sizeof(u32)), *give_entity = malloc((size / 8 + 1) * sizeof(u32));
    unsigned doors = 0, steps = 0, give_count = 0;
    u8 placed[256] = {0};   /* actor types the room places */
    if (base + 1 + 2 * (size_t)block[base] > size) FAIL("M%03u starting actors past the block", room);
    else for (u32 i = 0; i < block[base]; ++i) placed[block[base + 1 + 2 * i]] = 1;
    for (u32 t = 0; t < count; ++t) {
        entries[t] = word_at(block, 8 + 4 * (size_t)t);
        if (tasks->offsets[t] != block + entries[t]) FAIL("M%03u entry %u differs from the relocated one", room, t);
    }
    if (out) {
        fprintf(out, "room\tM%03u\ttype=%u\tbank=%u\tentries=%u\tcode_end=%#x", room, g_GameState.room_type,
                g_GameState.requested_entity_bank, count, base);
        for (u32 t = 0; t < count; ++t) fprintf(out, "%s%#x", t ? "," : "\t", entries[t]);
        fputc('\n', out);
    }
    for (u32 t = 0; t < count; ++t) {
        size_t top = 0; work[top++] = entries[t];
        while (top) {
            size_t at = work[--top];
            for (;;) {
                if (at % 4 || at + 4 > base) { FAIL("M%03u entity %u: step at %#zx outside the code", room, t, at); break; }
                if (kind[at / 4] == WORD_STEP || kind[at / 4] == WORD_END) break;
                if (word_at(block, at) == 0xFFFFFFFFu) { kind[at / 4] = WORD_END; break; }
                if (at + 8 > base) { FAIL("M%03u entity %u: step at %#zx outside the code", room, t, at); break; }
                if (kind[at / 4] == WORD_DATA) { FAIL("M%03u entity %u: step at %#zx is data", room, t, at); break; }
                Step s; decode(block, at, &s);
                if (at + s.bytes > base) { FAIL("M%03u entity %u: step at %#zx runs past the code", room, t, at); break; }
                if (!g_ScriptQueueOpcodeHandlers[s.op]) { FAIL("M%03u entity %u: op %u at %#zx has no handler", room, t, s.op, at); break; }
                kind[at / 4] = WORD_STEP; entity_of[at / 4] = (u16)t; ++steps;
                if (s.op == 167) { gives[give_count] = (u32)at; give_entity[give_count++] = t; }
                /* A key's item-count test (a lock): room offset, entity, block offset, item; locktable.py's input. */
                if (s.op == 168 && s.count && s.modes[0] == 0 && s.values[0] >= 0xC8 && s.values[0] <= 0xDC) {
                    unsigned l = 0;
                    while (l < PE_RAND_LOCKS && !(g_pe_rand_locks[l].map == room && g_pe_rand_locks[l].at == offset + at)) ++l;
                    if (l == PE_RAND_LOCKS || g_pe_rand_locks[l].item != s.values[0])
                        FAIL("M%03u key test +%06zX (item %#x) is no table lock", room, offset + at, s.values[0]);
                    else ++locks_walked;
                    if (spots) fprintf(spots, "keytest\tM%03u\t+%06zX\t%u\t%#zx\t%#x\n", room, offset + at, t, at, s.values[0]);
                }
                if (s.op == 8 && s.count > 0 && s.modes[0] == 0) placed[s.values[0] & 0xFF] = 1;
                if (s.op == 17 && s.count > 1 && s.modes[0] == 0 && s.values[0] == 2500 && s.modes[1] == 0) placed[s.values[1] & 0xFF] = 1;
                int target = -1, data = -1;
                if (s.op == 0 || s.op == 18) target = 0;
                else if (s.op == 5 && !(s.modes[0] == 0 && s.values[0])) target = 1;
                else if (s.op == 29) target = 2;
                else if (s.op == 20 && s.count > 1 && s.modes[0] == 0 && s.values[0] >= 1 && s.values[0] <= 3) target = 1;
                else if (s.op == 172 || s.op == 227) data = 0;
                else if (s.op == 226) data = 3;
                if (target >= 0 && target < (int)s.count && s.modes[target] == 0 && s.values[target] < 0x80000000u)
                    work[top++] = entries[t] + (size_t)s.values[target] * 2;
                if (data >= 0 && data < (int)s.count && s.modes[data] == 0) {
                    size_t d = entries[t] + (size_t)s.values[data] * 2;
                    if (d + 4 > size) FAIL("M%03u op %u at %#zx: data %#zx outside the block", room, s.op, at, d);
                    else if (kind[d / 4] == WORD_STEP) FAIL("M%03u op %u at %#zx: data %#zx is a step", room, s.op, at, d);
                    else if (d % 4 == 0) kind[d / 4] = WORD_DATA;
                }
                if (s.op == 49) {
                    if (s.count != 1) FAIL("M%03u door at %#zx has %u operands", room, at, s.count);
                    else if (s.modes[0] == 0 && door_room(s.values[0]) == DOOR_NONE) FAIL("M%03u door at %#zx: token %08x is no room", room, at, s.values[0]);
                    else if (s.modes[0] == 0) ++doors;
                }
                if (out) {
                    fprintf(out, "step\tM%03u\t%u\t%#zx\t%u", room, t, at, s.op);
                    for (u32 k = 0; k < s.count; ++k) fprintf(out, "%s%u:%#x", k ? "," : "\t", s.modes[k], s.values[k]);
                    if (s.op == 49 && s.count == 1 && s.modes[0] == 0) fprintf(out, "\tM%03d", door_room(s.values[0]));
                    fputc('\n', out);
                }
                if (s.op == 0 || s.op == 1 || s.op == 32) break;
                if (s.op == 49 && at + s.bytes + 4 <= size && word_at(block, at + s.bytes) == 0xFFFFFFFFu) break;
                at += s.bytes;
            }
        }
    }
    *steps_total += steps; *doors_total += doors;
    audit_spots(room, assets, room_bytes, offset, base, kind, entity_of, placed, gives, give_entity, give_count, spots);
    find_flags(room, assets, offset, base, kind, entity_of, placed, gives, give_entity, give_count, spots);
    free(entries); free(kind); free(entity_of); free(work); free(gives); free(give_entity);
}

/* Disc 2 must carry the same room table and room bytes as disc 1. */
/* Third file: each room's actor models for the battle randomizer's model swap (bank slot, payload size,
 * the bank root's VRAM word as Render_SetupEntityPrims splits it, model counts, row ids) and the TIM
 * rectangles the loader uploads from the scratch and texture blobs. */
static void list_tims(unsigned room, FILE *out, const char *blob_name, const u8 *blob) {
    const SceneRoomDirectory *dir = (const SceneRoomDirectory *)(blob + word_at(blob, 4));
    for (unsigned i = 0; i < dir->tims >> 22; ++i) {
        const u8 *r = blob + (dir->tims & 0x3FFFFF) + 20 * i;
        u32 src = word_at(r, 4), rect = word_at(r, 8), sec = word_at(r, 12), rect2 = word_at(r, 16);
        fprintf(out, "M%03u\ttim\t%s\t%u,%u,%u,%u", room, blob_name, (rect >> 10) & 0x7FF, rect >> 21, rect & 0x3FF,
                (src >> 24) ? (src >> 24) : 256u);
        if (sec & 0xFFFFFF) fprintf(out, "\t%u,%u,%u,%u", (rect2 >> 10) & 0x7FF, rect2 >> 21, rect2 & 0x3FF, sec >> 24);
        fputc('\n', out);
    }
}

static void list_models(unsigned room, FILE *out) {
    const u8 *assets = (const u8 *)g_GameState.loaded_scene_assets;
    const SceneRoomDirectory *dir = (const SceneRoomDirectory *)(assets + word_at(assets, 4));
    for (unsigned i = 0; i < dir->bankRoots >> 22; ++i) {
        const SceneRoomRecord *r = (const SceneRoomRecord *)(assets + (dir->bankRoots & 0x3FFFFF)) + i;
        const u8 *m = assets + (r->source.offsetAndSlot & 0xFFFFFF);
        u32 v = r->u.handlerAddress, size = word_at((const u8 *)r, 0) & 0xFFFFFF;
        fprintf(out, "M%03u\tmodel\t%u\tsize=%u\tx=%u\ty=%u\tpal=%u\tmode=%u\tparts=%u\tverts=%u\trows=", room,
                r->source.bytes.slot, size, (v >> 6) & 0x3C0, (v >> 9) & 0x180, ((v >> 18) & 0xFF) + 0x1C0, (v >> 8) & 0xF,
                m[2], m[6] | m[7] << 8);
        const char *sep = "";
        for (unsigned k = 0; k < dir->bankRows >> 22; ++k) {
            const SceneRoomRecord *row = (const SceneRoomRecord *)(assets + (dir->bankRows & 0x3FFFFF)) + k;
            if (row->u.row.group != r->source.bytes.slot) continue;
            fprintf(out, "%s%u", sep, row->source.bytes.slot); sep = ",";
        }
        fputc('\n', out);
    }
    list_tims(room, out, "scratch", (const u8 *)g_GameState.scene_load_scratch);
    list_tims(room, out, "texture", (const u8 *)g_GameState.texture_load_scratch);
}

static int same_rooms(PeDisc *one, const char *path) {
    PeDisc two = {0}; PeDiscFile exe1, exe2, img1, img2; int ok = 1;
    static u8 a[2048 * 64], b[2048 * 64]; u8 ta[SCENE_ROOM_COUNT * 8], tb[SCENE_ROOM_COUNT * 8];
    if (!pe_disc_open(&two, path) || !pe_disc_find(one, "SLUS_006.62", &exe1) || !pe_disc_find(&two, "SLUS_006.68", &exe2) ||
        !pe_disc_find(one, "PE.IMG", &img1) || !pe_disc_find(&two, "PE.IMG", &img2) ||
        !pe_disc_read(one, &exe1, 0x800u + 0x80093378u - 0x80010000u, ta, sizeof(ta)) ||
        !pe_disc_read(&two, &exe2, 0x800u + 0x80093378u - 0x80010000u, tb, sizeof(tb))) { pe_disc_close(&two); return 0; }
    if (memcmp(ta, tb, sizeof(ta))) { FAIL("disc 2 room table differs"); ok = 0; }
    for (unsigned m = 0; ok && m < SCENE_ROOM_COUNT; ++m) {
        SceneRoomSectorRange r; memcpy(&r, ta + m * 8, 8);
        u64 at = (u64)r.start * 2048, left = (u64)(r.scratchSectors + r.textureSectors + r.roomSectors) * 2048;
        while (left) {
            size_t n = left > sizeof(a) ? sizeof(a) : (size_t)left;
            if (!pe_disc_read(one, &img1, at, a, n) || !pe_disc_read(&two, &img2, at, b, n)) { ok = 0; break; }
            if (memcmp(a, b, n)) { FAIL("disc 2 M%03u differs", m + 1); ok = 0; break; }
            at += n; left -= n;
        }
    }
    pe_disc_close(&two);
    return ok;
}

int main(int argc, char **argv) {
    const char *path = getenv("PE_TEST_DISC"), *path2 = getenv("PE_TEST_DISC2");
    if (!path || !*path) return getenv("PE_REQUIRE_GAME_ASSETS") ? 1 : 77;
    PeDisc disc = {0};
    /* Nothing here draws, but ResetGraph opens psyz: keep it windowless whichever way the test runs. */
    if (!SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "PSYZ_HEADLESS", "1", true) ||
        !SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "SDL_AUDIODRIVER", "dummy", true)) return 1;
    if (!SDL_Init(SDL_INIT_EVENTS) || !pe_disc_open(&disc, path)) return 1;
    ResetGraph(0); CdRom_BindImage(&disc, NULL);
    if (Asset_OpenPeImage() != 0) return 1;
    Boot_InitGameState(); D_8009CDC8 = 'm';
    FILE *out = argc > 1 ? fopen(argv[1], "w") : NULL, *spots = argc > 2 ? fopen(argv[2], "w") : NULL;
    FILE *models = argc > 3 ? fopen(argv[3], "w") : NULL;
    if ((argc > 1 && !out) || (argc > 2 && !spots) || (argc > 3 && !models)) return 1;
    unsigned rooms = 0, steps = 0, doors = 0;
    for (unsigned room = 1; room <= SCENE_ROOM_COUNT; ++room) {
        if (!g_RoomSectorTable[room - 1].roomSectors) continue;
        char name[16]; snprintf(name, sizeof(name), "m%04ui", room);
        int status = Scene_LoadRoom((u32)Text_ParseBase32Id((signed char *)name));
        if (status) { FAIL("M%03u: Scene_LoadRoom %d", room, status); continue; }
        ++rooms; walk_room(room, out, spots, &steps, &doors);
        if (models) list_models(room, models);
    }
    if (out) fclose(out);
    if (spots) fclose(spots);
    if (models) fclose(models);
    if (sites_total != PE_RAND_SITES) FAIL("%u of %u table sites are in a loaded room", sites_total, PE_RAND_SITES);
    if (gives_listed != PE_RAND_GIVES) FAIL("%u of %u table gives are in a loaded room", gives_listed, PE_RAND_GIVES);
    if (locks_walked != PE_RAND_LOCKS) FAIL("%u of %u table locks are walked key tests", locks_walked, PE_RAND_LOCKS);
    check_flags();
    int discs = 1;
    if (path2 && *path2) { if (!same_rooms(&disc, path2)) FAIL("disc 2 rooms differ from disc 1"); else discs = 2; }
    CdRom_BindImage(NULL, NULL); pe_disc_close(&disc);
    if (failures) { fprintf(stderr, "%u failures\n", failures); return 1; }
    printf("Randomizer rooms: %u rooms loaded through Scene_LoadRoom, %u steps walked, %u doors to rooms or main-loop scenes, %u disc(s) checked\n",
           rooms, steps, doors, discs);
    printf("Randomizer spots: %u sites and %u gives of the table on walked steps in placed actors; %u GiveItem steps walked, "
           "%u not in the table (%u in actors never placed); %u opened/taken flag writes, none shared across spots; "
           "%u key tests, each a table lock\n",
           sites_total, gives_listed, gives_walked, gives_unlisted, gives_unplaced, flag_count, locks_walked);
    return 0;
}
