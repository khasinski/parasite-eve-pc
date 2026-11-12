/*
 * Skin loader: puts a character skin on Aya, natively (the psxrecomp build's pe_collection.c
 * costume code, on host pointers). A skin is a folder of the game's own asset formats, built from
 * the user's disc by the bridge's tools/pe_assets/build_skin.py, kept beside the preferences:
 *   pe_skins/<folder>/texture.bin   the player texture blob (the game's bank + 3 file: TIMs)
 *                     model.bin     the player bundle (bank + 8: model, root and rows)
 *                     day1-8.bin    the per-day row blobs
 *                     rooms.bin     optional: room animations retargeted ("PERR", fingerprint table)
 *                     meta.bin      optional: "PEMT", the part the weapon follows
 * Any pack can name a folder (PeCostume.bundle); the Wardrobe loads it here. A bundle starting
 * "@" is a character pack's ("@<pack id>/<folder>", pe_packs.c), read from its zip or folder. A game outfit
 * (PeCostume.bank >= 0) may name a room pack: a folder with rooms.bin alone (the bridge's
 * build_outfit_rooms.py), whose rows replace the room's animations for Aya's other skeletons.
 *
 * The native game resolves these files' offsets into pointers (SceneAsset_ResolveOffset) and keeps
 * them in g_GameState.bank_slots / bank_rows, so a skin goes in the same way, pointing at our copy
 * in host memory -- nothing in the game's own buffers is overwritten (the day blob's stream tables
 * feed the battle loader and stay the game's):
 *   - Aya's model load (PE_HOOK_PLAYER_BANK): at the texture upload (entity_texture_phase 3) the
 *     game's own texture blob is kept, and a worn skin's TIMs go to VRAM instead of Aya's;
 *   - before her actor takes its model and rows (PE_HOOK_PLAYER_ROWS), and every frame (day changes
 *     and battles re-resolve the game's rows), bank_slots[0] and the rows point at the skin;
 *   - the weapon follows the skin's own hand part (meta.bin), room animations (door opening, room
 *     events: Aya's skeleton) are swapped for their retargeted copies, matched by a fingerprint of
 *     Aya's row (FNV-1a over the row's own length, as build_skin.py writes them).
 * Changing costume in free field is instant: textures, pointers, then her actor's model is set up
 * again the way the room load does it (Scene_InitEntityPlayer step 0x27). No room reload.
 * Nothing while Liberate runs (flags 0x20000) or outside the gameplay banks 10-13.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_wardrobe.h"
#include "pe_skin_collision.h"
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe1/entity_animation.h"
#include "pe1/game_state.h"
#include "pe1/pe_image.h"
#include "pe1/player_entity.h"
#include "pe1/render_lighting.h"
#include "pe1/render_setup.h"
#include "pe1/render_shadow.h"
#include "pe1/scene_assets.h"
#include "pe1/scene_room_load.h"
#include "pe1/field_actor.h"
#include "pe_skin_rows.h"

#define PE_FILE_MAX      0x400000u   /* rooms.bin is ~800 KB */
#define PE_GUN_AYA       0x12        /* Aya's right hand, second finger segment */
#define PE_IDLE_MODE     0x15
#define PE_LIBERATE      0x20000u
#define PE_NEW_BUNDLE    0x200000u
#define PE_SECTOR        2048u

static PeBlob s_game_tex;               /* the game's own player textures, kept at its upload */
static int s_game_tex_bank = -1;
static PeSkin *s_tex;                   /* whose textures are in VRAM: NULL the game's */
static int s_tex_known;
static PeSkin *s_pointed;               /* the skin bank_slots / rows point at, or NULL */
static int s_gun_written = -1;
static int s_instant;                   /* F1: swap in place (off until it is proven in game) */
static uint32_t s_checkpoint_epoch=1;
static void pe_checkpoint_changed(void){if(s_checkpoint_epoch<UINT32_MAX)++s_checkpoint_epoch;}


int pe_skin_instant(void) { return s_instant; }

int pe_skin_appearance_ready(const PeCostume *c) {
    if (!c) return s_pointed == NULL;
    if (c->bank >= 0) return !s_pointed && g_GameState.loaded_entity_bank == c->bank;
    return !c->load && c->bundle && s_pointed && s_tex == s_pointed &&
           !strcmp(s_pointed->folder, c->bundle);
}

/* ---- files ---- */
static int pe_read(const char *folder, const char *name, PeBlob *b) {
    char rel[160], path[1200];
    FILE *f;
    long n;
    if (folder[0] == '@') {                                  /* a character pack's */
        unsigned char *data;
        unsigned size;
        if (!pe_pack_read(folder, name, &data, &size, PE_FILE_MAX)) return 0;
        if (size <= 8) { free(data); return 0; }
        b->data = data;
        b->size = (u32)size;
        return 1;
    }
    snprintf(rel, sizeof rel, "pe_skins/%s/%s", folder, name);
    if (pe_mod_path(path, sizeof path, rel) != 0 || !(f = fopen(path, "rb"))) return 0;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 8 || (u32)n > PE_FILE_MAX || !(b->data = malloc((size_t)n))) { fclose(f); return 0; }
    b->size = (u32)n;
    if (fread(b->data, 1, (size_t)n, f) != (size_t)n) { free(b->data); b->data = NULL; fclose(f); return 0; }
    fclose(f);
    return 1;
}

static PeSkin *pe_find(const char *folder, int create) {
    int i;
    if (!folder || !*folder) return NULL;
    for (i = 0; i < s_skins; ++i) if (strcmp(s_skin[i].folder, folder) == 0) return &s_skin[i];
    if (!create || s_skins == PE_MAX_SKINS || strlen(folder) >= sizeof s_skin[0].folder) return NULL;
    snprintf(s_skin[s_skins].folder, sizeof s_skin[0].folder, "%s", folder);
    return &s_skin[s_skins++];
}

static PeSkin *pe_load_failed(const char *folder, const char *file) {
    pe_log("skin", "%s: %s missing or unreadable, skin not worn", folder, file);
    return NULL;
}

static void pe_read_rooms(PeSkin *k) {
    if (pe_read(k->folder, "rooms.bin", &k->rooms) && !memcmp(k->rooms.data, "PERR", 4)) {
        const u32 n = rd32(k->rooms.data, 4);
        if (n <= (k->rooms.size - 8u) / 12u) k->nrooms = n;
    }
}

/* A room pack: rooms.bin alone, for one of the game's own outfits (PeCostume.bank >= 0). */
static PeSkin *pe_load_rooms(const char *folder) {
    PeSkin *k = pe_find(folder, 1);
    if (!k) return NULL;
    if (!k->loaded) {
        k->loaded = 1;
        k->gun_part = -1;
        pe_read_rooms(k);
        k->ok = k->nrooms != 0;
        if (k->ok) pe_log("skin", "%s room pack loaded: rooms %u", folder, k->nrooms);
        else pe_log("skin", "%s: rooms.bin missing or unreadable (room animations stay the game's)", folder);
    }
    return k->ok ? k : NULL;
}

static PeSkin *pe_load(const char *folder) {
    PeSkin *k = pe_find(folder, 1);
    PeBlob meta = { 0, 0 };
    char name[16];
    int d;
    if (!k) return NULL;
    if (k->loaded) return k->ok ? k : NULL;
    k->loaded = 1;
    if (!pe_read(folder, "model.bin", &k->model) || !pe_dir(&k->model)) return pe_load_failed(folder, "model.bin");
    /* Costumes must not change movement clearance. Applies to loose skins and
     * character packs without rewriting installed assets or combat hit bounds. */
    if (!pe_skin_normalize_movement(k->model.data, k->model.size)) return pe_load_failed(folder, "model.bin collision bounds");
    if (!pe_read(folder, "texture.bin", &k->tex) || !pe_dir(&k->tex)) return pe_load_failed(folder, "texture.bin");
    for (d = 1; d <= 8; ++d) {
        snprintf(name, sizeof name, "day%d.bin", d);
        if (!pe_read(folder, name, &k->day[d]) || !pe_dir(&k->day[d])) return pe_load_failed(folder, name);
    }
    pe_read_rooms(k);
    if (!k->nrooms) pe_log("skin", "%s: rooms.bin missing or unreadable (room animations stay Aya's)", folder);
    k->gun_part = -1;
    if (pe_read(folder, "meta.bin", &meta) && !memcmp(meta.data, "PEMT", 4) && meta.data[4] < 64) k->gun_part = meta.data[4];
    free(meta.data);
    pe_log("skin", "%s loaded: model %u, textures %u, rooms %u", folder, k->model.size, k->tex.size, k->nrooms);
    k->ok = 1;
    return k;
}

int pe_skin_available(const char *folder) {
    char rel[160], path[1200];
    FILE *f;
    PeSkin *k = pe_find(folder, 1);
    if (!k) return 0;
    if (k->loaded) return k->ok;
    if (!k->checked) {
        k->checked = 1;
        if (folder[0] == '@') return k->present = pe_pack_has(folder, "model.bin");
        snprintf(rel, sizeof rel, "pe_skins/%s/model.bin", folder);
        if (pe_mod_path(path, sizeof path, rel) == 0 && (f = fopen(path, "rb"))) { fclose(f); k->present = 1; }
    }
    return k->present;
}

/* ---- what should be worn ---- */
static int pe_gameplay_bank(void) { return g_SceneAreaType >= 10 && g_SceneAreaType <= 13; }
/* pe_save_loaded, not pe_game_loaded: during a room load the player actor is rebuilt. */
static int pe_can_apply(void) { return pe_save_loaded() && pe_gameplay_bank() && !(g_GameState.flags & PE_LIBERATE); }

static PeSkin *pe_wanted(void) {
    const PeCostume *c = pe_wardrobe_worn();
    return c && c->bank < 0 && c->bundle ? pe_load(c->bundle) : NULL;
}

/* The worn game outfit's room pack, while that outfit is the loaded player bundle. */
static PeSkin *pe_wanted_rooms(void) {
    const PeCostume *c = pe_wardrobe_worn();
    return c && c->bank >= 0 && c->bundle && c->bank == g_GameState.loaded_entity_bank ? pe_load_rooms(c->bundle) : NULL;
}

/* ---- textures ---- */
static void pe_upload(const PeBlob *b) {
    const SceneAssetDirectory *dir = pe_dir(b);
    TimUploadRecord *tim;
    u32 i, n;
    if (!dir) return;
    tim = SceneAsset_ResolveOffset(b->data, dir->timEntries & 0x3FFFFFu);
    n = dir->timEntries >> 22;
    for (i = 0; i < n; ++i) Draw_LoadTimAsset(&tim[i], b->data);
}

/* The game's own texture blob, just read for the upload it is about to do. */
static void pe_keep_game_textures(void) {
    const int index = g_SceneAreaType + 3;
    const u32 size = (u32)(g_PeImageSectorBoundsNext[index] - g_PeImageSectorTable[index]) * PE_SECTOR;
    u8 *copy;
    if (!size || size > PE_FILE_MAX || !(copy = realloc(s_game_tex.data, size))) return;
    memcpy(copy, g_GameState.scene_load_scratch, size);
    pe_checkpoint_changed();
    s_game_tex.data = copy;
    s_game_tex.size = size;
    s_game_tex_bank = g_SceneAreaType;
}

/* Put `k`'s textures (NULL: the game's) in VRAM; 0 if the game's aren't at hand. */
static int pe_textures(PeSkin *k) {
    if (s_tex_known && s_tex == k) return 1;
    if (k) pe_upload(&k->tex);
    else if (s_game_tex.data && s_game_tex_bank == g_GameState.loaded_entity_bank) pe_upload(&s_game_tex);
    else return 0;
    s_tex = k;
    s_tex_known = 1;
    return 1;
}

/* The weapon model follows the player's part animation_id (0x12, Aya's hand). */
static void pe_gun_part(const PeSkin *k) {
    s16 *id = &g_GameState.scene_object.animation_id;
    const int want = k && k->gun_part >= 0 ? k->gun_part : -1;
    if (want >= 0) {
        if (*id != want && (*id == PE_GUN_AYA || *id == s_gun_written)) { *id = (s16)want; s_gun_written = want; }
    } else if (s_gun_written >= 0) {
        if (*id == s_gun_written) *id = PE_GUN_AYA;
        s_gun_written = -1;
    }
}

/* Keep the pointers on `k` (NULL: the game's own, from its buffers). */
static void pe_keep(PeSkin *k) {
    if (k) {
        if (s_pointed != k) pe_restore_rows();
        pe_point_bundle(k->model.data);
        pe_point_day(k->day[pe_day()].data, 1);
        pe_room_rows(k);
        pe_running_row(k);
    } else if (s_pointed) {
        pe_restore_rows();
        pe_point_bundle(g_GameState.voice_bank_base_1400);
        pe_point_day((u8 *)g_GameState.entity_texture_blob, 0);
        pe_log("skin", "the game's own model and rows back");
    }
    pe_gun_part(k);
    s_pointed = k;
}

/* A game outfit worn where the story has another: the room's animations for that other skeleton
 * are swapped for the pack's copies, retargeted to the outfit's (the model stays the game's). */
static void pe_keep_rooms(void) {
    PeSkin *r;
    if (s_pointed) return;
    r = pe_can_apply() ? pe_wanted_rooms() : NULL;
    if (s_room_pack && s_room_pack != r) pe_restore_rows();     /* that outfit came off */
    if (!r) return;
    s_room_pack = r;
    pe_room_rows(r);
    pe_running_row(r);
}

/* Her actor takes the model and idle row again, as the room load does (step 0x27). */
static void pe_rebind_player(void) {
    FieldActor *player = g_PlayerEntity;
    s8 *setup;
    if (!player || !g_GameState.bank_slots[0]) return;
    player->model_state.model_header = g_GameState.bank_slots[0];
    player->action_data = g_GameState.bank_rows[0][PE_IDLE_MODE];
    Entity_SetActionMode((BattleEntity *)player, PE_IDLE_MODE);
    Render_SetupEntityPrims(&player->render_object, player->model_state.model_header,
                            (u8 *)player->allocation_block + 2 * sizeof(RenderShadowQuad), 0x3C0, 0x100, 0, 0x1C0, 2, &setup, 1);
    Render_InitRoomPrimState(&player->render_object);
    Render_DrawWithAnim(&player->render_object, player->action_data, 0, &g_RenderLightingState);
    player->render_object.header->shadow_radius = player->render_object.hit_cylinder.radius * 2;
}

/* ---- hooks ---- */
static int pe_on_player_bank(void *ctx, void *user) {
    PeSkin *k;
    (void)ctx;
    (void)user;
    if (!pe_save_loaded()) return 0;
    k = pe_can_apply() ? pe_wanted() : NULL;
    if (g_GameState.entity_texture_phase == 0)
        pe_log("skin", "model load: bank %d (loaded %d) new=%d wanted=%s vram=%s", g_SceneAreaType,
               g_GameState.loaded_entity_bank, (g_GameState.flags & PE_NEW_BUNDLE) != 0, k ? k->folder : "own",
               !s_tex_known ? "unknown" : s_tex ? s_tex->folder : "game");
    if (g_GameState.entity_texture_phase == 0 && !(g_GameState.flags & PE_NEW_BUNDLE)) {
        /* Unknown after a load (VRAM may hold anything), or the game's not at hand: let the game
         * load its bundle again, through the upload above. */
        if (!s_tex_known || (s_tex != k && !pe_textures(k))) {
            g_GameState.flags |= PE_NEW_BUNDLE;
            g_GameState.story_day_flags |= 1u;               /* a new bundle needs its own rows */
        }
    }
    return 0;
}

/* The game just read Aya's texture blob: keep it, and put a worn skin's TIMs in VRAM instead. */
static int pe_on_player_textures(void *ctx, void *user) {
    PeSkin *k;
    (void)user;
    if (!pe_save_loaded() || ctx != g_GameState.scene_load_scratch) return 0;
    pe_keep_game_textures();
    k = pe_can_apply() ? pe_wanted() : NULL;
    s_tex = k;
    s_tex_known = 1;
    if (!k) return 0;
    pe_upload(&k->tex);
    pe_log("skin", "%s textures in", k->folder);
    return 1;                                                 /* the game skips Aya's */
}

/* Not pe_can_apply: the room load's player steps (Scene_InitEntityPlayer 0x20) set flags 0x20000
 * until the end of step 0x27, so here it would always say no and her actor would take the game's
 * model under the skin's textures. Liberate's own model is bank 14, outside the gameplay banks. */
static int pe_on_player_rows(void *ctx, void *user) {
    PeSkin *k;
    (void)ctx;
    (void)user;
    if (!pe_save_loaded()) return 0;
    k = pe_gameplay_bank() && s_tex_known ? s_tex : NULL;
    pe_log("skin", "player rows: bank %d flags %X vram=%s -> %s", g_SceneAreaType, (unsigned)g_GameState.flags,
           !s_tex_known ? "unknown" : s_tex ? s_tex->folder : "game", k ? k->folder : "game's model");
    pe_keep(k);
    pe_keep_rooms();
    return 0;
}

/* Every frame: keep the pointers; a costume changed in free field goes in at once. */
static int pe_on_frame(void *ctx, void *user) {
    PeSkin *k;
    (void)ctx;
    (void)user;
    if (!pe_game_loaded()) return 0;
    k = pe_can_apply() ? pe_wanted() : NULL;
    pe_keep_rooms();
    if (!s_tex_known && !k) return 0;                   /* nothing to put on; the next load tells us */
    if (s_instant && (k != s_tex || !s_tex_known) && !pe_in_battle() && pe_free_field() && g_PlayerEntity && pe_textures(k)) {
        pe_keep(k);
        pe_rebind_player();
        pe_log("skin", "wearing %s (instant)", k ? k->folder : "Aya's own");
    } else if (s_pointed || k) {
        pe_keep(s_tex == k ? k : s_pointed);
    }
    return 0;
}

static int pe_on_battle(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    if (s_pointed && pe_can_apply()) pe_keep(s_pointed);
    pe_keep_rooms();
    return 0;
}

/* A loaded save or a new room load brings the game's own buffers back. */
static int pe_on_load(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    s_tex_known = 0;
    s_tex = s_pointed = s_room_pack = NULL;
    s_gun_written = -1;
    memset(s_replaced, 0, sizeof s_replaced);
    return 0;
}

void pe_skin_register(const char *mod_id) {
    pe_hook_add(PE_HOOK_PLAYER_BANK, pe_on_player_bank, NULL, PE_PRIORITY_LATE, mod_id);   /* after the Wardrobe's bank */
    pe_hook_add(PE_HOOK_PLAYER_TEXTURES, pe_on_player_textures, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_PLAYER_ROWS, pe_on_player_rows, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_menu_register_toggle(PE_PAGE_WARDROBE, "Instant swap (experimental)",
        "Swap skins in place, no room reload. Off: the room reloads, as the recomp did.", &s_instant, NULL);
    pe_hook_add(PE_HOOK_FRAME, pe_on_frame, NULL, PE_PRIORITY_LATE, mod_id);
    pe_hook_add(PE_HOOK_BATTLE_UPDATE, pe_on_battle, NULL, PE_PRIORITY_LATE, mod_id);
    pe_hook_add(PE_HOOK_SAVE_READ, pe_on_load, NULL, PE_PRIORITY_NORMAL, mod_id);
}

#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe_checkpoint_collection.h"
int pe_skin_checkpoint_walk(const unsigned char*in,unsigned char*out,int apply){
if(s_checkpoint_epoch==UINT32_MAX||s_tex||s_pointed||s_room_pack)return 0;for(int i=0;i<s_skins;i++)if(s_skin[i].loaded)return 0;for(int i=0;i<PE_ROWS_PER_BANK;i++)if(s_replaced[i])return 0;
if((s_game_tex.size&&!s_game_tex.data)||s_game_tex.size>PE_FILE_MAX)return 0;
if(apply&&(!in||!pe_skin_checkpoint_walk(in,0,0)))return 0;PeCheckpointWire w={in,out,0,apply,1};PE_CK_U(w,s_checkpoint_epoch,1,UINT32_MAX-1,1);PE_CK_S(w,s_game_tex_bank,-1,255,1);PE_CK_U(w,s_game_tex.size,0,PE_FILE_MAX,1);PE_CK_S(w,s_instant,0,1,1);PE_CK_S(w,s_tex_known,0,1,0);PE_CK_S(w,s_gun_written,-1,INT_MAX,0);return w.ok&&w.at==24;}
#endif
