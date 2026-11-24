/* Enemy import: an imported enemy joins a picked fight as an enemy of its own.
 *
 * Nothing of the room is replaced. The imported enemy is a new actor taken from the game's actor
 * pool and given an enemy record of its own in the battle's table (Entity_AllocSlot), so the
 * battle sees one more enemy:
 *   - Aya can target it, her shots hit it through the game's own rules (evasion, damage numbers,
 *     the impact effect), its time bar fills like any enemy's, and when its HP reaches 0 the
 *     game's own death steps count it down, fade it, give its EXP and free the actor; the battle
 *     is won when every enemy, the room's and the imported ones, is beaten;
 *   - it has no PE model: the actor is drawn by this mod (PE_HOOK_ACTOR_RENDER), posed with the
 *     pack's bones (every vertex on its bone, joint vertices half and half on two);
 *   - it fights by itself (pe_foe_think, every battle frame): it turns to Aya and walks to her
 *     (the game's floor and wall checks still apply to the move), and when its time bar is full
 *     and Aya is in reach it plays one of its attacks; at the attack's blow frame, if Aya is still
 *     in reach and in front, the blow lands through the game's own enemy-hit path (the flags an
 *     enemy's hit sphere sets): the game takes the HP (attack power less Aya's defense and armor,
 *     a chance of a critical), plays her hit reaction and shows the number. Shots stop it for its
 *     hurt anim (not mid-attack); at 0 HP it plays its death anim before the game fades it;
 *   - while the game stops enemies (Aya's command menu) it stands still.
 * The room's own enemies fight too, unless Fights > Room's enemies is "Sent away": they are taken
 * out of the battle as it opens (no EXP for them), and the imported enemy stands where the first
 * one was. When the opening outlasts PE_OPEN_LIMIT (a story line the player has not pressed
 * through yet, as M118's), the imported enemy joins then but the room's enemies stay until the
 * clock runs (Aya's time bar moves): the opening's scene uses them (M118's Giant Dogman), and
 * sent away mid-scene it never ends, nor the battle.
 * The texture and palettes go to VRAM blocks the room is not using, chosen per battle (see
 * "where the texture goes"), saved before and put back when the battle ends. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "pe_enemy_battle.h"
#include "../../pe_enemy_build.h"
#include "../../pe_enemy_pack.h"
#include "pe_imp_enemy.h"
#include "pe_imp_json.h"
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_services.h"
#include "pe1/battle.h"
#include "pe1/battle_entity_anim.h"
#include "pe1/battle_runtime.h"
#include "pe1/battle_start.h"
#include "pe1/battle_turn.h"
#include "pe1/memcard.h"
#include "pe1/menu_inventory.h"
#include "pe1/display_env.h"
#include "pe1/draw_buffers.h"
#include "pe1/entity_allocation.h"
#include "pe1/entity_frame_update.h"
#include "pe1/field_actor.h"
#include "pe1/field_collision.h"
#include "pe1/game_state.h"
#include "pe1/game_state_types.h"
#include "pe1/gte_types.h"
#include "pe1/player_entity.h"
#include "pe1/psyq_gpu.h"
#include "pe1/render_camera.h"
#include "pe1/render_object.h"
#include "pe1/render_packets.h"

#define MOD_ID "pe_enemy_import"
#define PE_FOES_MAX 3
#define PE_FORMS_MAX 5
#define PE_TEX_X 320          /* the texture's first choice when free (Central Park leaves it free) */
#define PE_TEX_Y 256
#define PE_TEX_W 256          /* VRAM words: 512 8-bit texels, the pack builder's limit */
#define PE_TEX_H 256
#define PE_CLUT_X 512         /* the palettes' first choice when free */
#define PE_CLUT_Y 500
#define PE_CLUTS_MAX 12
#define PE_PI 3.14159265358979323846
#define PE_FOE_TYPE 0xEE      /* actor type no room uses: no PE model, voice or script tables */
#define PE_CHARGE_FULL 0x2328 /* the battle's full time bar */
#define PE_OPEN_FRAMES 10     /* battle frames after the opening before joining */
#define PE_OPEN_LIMIT 900     /* an opening that never ends: join anyway, and start the clock */
#define PE_TURN_STEP 0x60     /* turning per frame, 4096 = a full turn */
#define PE_BATTLE_SQUARE 0x2000u /* Square after Boot_BuildRenderFlagTable maps the pad */

typedef struct {
    RenderNativeTag tag;
    u32 color, xy0, uv0, xy1, uv1, xy2, uv2;
} PeFt3;

enum { ROLE_ATTACK = PE_ANIM_ROLES, ROLE_ENTRANCE, ROLE_WAKE, ROLE_SUMMON };

typedef struct PeBattlePack {
    PeEnemyPack pack;
    struct PeBattlePack *next;
    char path[1200];
    RECT texture, clut;
    int height, uploaded;
    u16 saved_tex[PE_TEX_W * PE_TEX_H], saved_clut[PE_CLUTS_MAX][256];
} PeBattlePack;

typedef struct {
    FieldActor *actor;
    PeBattlePack *resource;
    unsigned serial;                 /* the actor's field_sfx_id: a reused actor has another */
    int anim, role, attack;          /* attack: index + 1 while attacking */
    double frame;
    int blow_done, stagger, last_hp, dead;
    int entrance_frames, entrance_loop, wake_anim, forms, lift;
    int drawn_frames, tris, hits, damage_done, attacks_started;
    int event_frame, sounds;         /* last frame whose events ran; sounds played */
    unsigned seen;                   /* bit per role: idle walk hurt die, attack = 1 << 4 */
} PeFoe;

static struct { char folder[64], name[32]; } s_list[PE_ENEMY_PACKS_MAX];
static int s_listed;
static char s_dir[1024];

static PeBattlePack s_resources[PE_FOES_MAX * PE_FORMS_MAX];
static int s_resource_count;
static PeBattlePack *s_roster[PE_FOES_MAX];
static struct { int pose, wake, loop; } s_entrance[PE_FOES_MAX];
static struct {
    PeBattlePack *offspring;
    int parent, anim;
} s_summon;
static int s_armed, s_canister_armed, s_launcher_armed;
static int s_send_away, s_count = 1;
enum { LAUNCHER_NONE, LAUNCHER_WAITING, LAUNCHER_DROPPED, LAUNCHER_HELD, LAUNCHER_FIRED };

static struct {
    int active, frames, open_frames, joined, sent_away, room_enemies;
    int away_later;                  /* joined during the opening: send the room's enemies away
                                      * once the clock runs */
    PeFoe foe[PE_FOES_MAX];
    int foes, placed_foes;
    PeFoe *last_blow;                /* the foe whose blow the game is taking now */
    int last_blow_frames;
    int canister_exposed, canister_detonated;
    int launcher;
    char result[96];
} s;

static int16_t s_posed[PE_ENEMY_MAX_VERTICES][3];
static PeFt3 s_prims[PE_FOES_MAX][2][PE_ENEMY_MAX_TRIANGLES];

static const char *const k_role_names[] = {"idle", "walk", "hurt", "die", "attack", "entrance", "wake", "summon"};

/* ---- the list of packs ---- */

static int pe_cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void pe_scan(void) {
    char **list;
    int count, i;
    s_listed = 0;
    if (pe_mod_path(s_dir, sizeof s_dir, "pe_enemies") != 0) return;
    if (!(list = SDL_GlobDirectory(s_dir, "*", 0, &count))) return;
    qsort(list, (size_t)count, sizeof *list, pe_cmp);
    for (i = 0; i < count && s_listed < PE_ENEMY_PACKS_MAX; ++i) {
        char path[1300];
        SDL_PathInfo info;
        if (list[i][0] == '.' || strlen(list[i]) >= sizeof s_list[0].folder) continue;
        snprintf(path, sizeof path, "%s/%s/enemy.json", s_dir, list[i]);
        if (!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_FILE) continue;
        snprintf(s_list[s_listed].folder, sizeof s_list[0].folder, "%s", list[i]);
        snprintf(s_list[s_listed].name, sizeof s_list[0].name, "%s", list[i]);
        PjNode *manifest = pj_load(path, NULL, 0);
        if (manifest) {
            const char *source = pj_str(pj_get(manifest, "source"), NULL);
            const char *name = pj_str(pj_get(manifest, "name"), NULL);
            const int scene = pe_imp_enemy_body(source) == PE_IMP_ENEMY_SCENE;
            if (name && *name && strlen(name) < sizeof s_list[0].name)
                snprintf(s_list[s_listed].name, sizeof s_list[0].name, "%s", name);
            pj_free(manifest);
            if (scene) continue;
        }
        pe_mod_log(MOD_ID, "enemy pack %s (%s) found", s_list[s_listed].folder, s_list[s_listed].name);
        s_listed++;
    }
    SDL_free(list);
}

unsigned pe_enemy_choice_count(void) { return 1u + (unsigned)s_listed; }

const char *pe_enemy_choice_name(unsigned i) {
    if (i == 0) return "None";
    return i <= (unsigned)s_listed ? s_list[i - 1].name : "";
}

int pe_enemy_battle_pack_path(int choice, char *out, size_t size) {
    if (choice <= 0 || choice > s_listed) return -1;
    const int written = snprintf(out, size, "%s/%s", s_dir, s_list[choice - 1].folder);
    return written < 0 || (size_t)written >= size ? -1 : 0;
}

static int pe_pack_height(const PeEnemyPack *pack) {
    int top = 0;
    for (int i = 0; i < pack->vertices; i++)
        if (-pack->vertex[i].y > top) top = -pack->vertex[i].y;
    return top > 50 ? top : 500;
}

static void pe_free_resources(void) {
    for (int i = 0; i < s_resource_count; i++) pe_enemy_pack_free(&s_resources[i].pack);
    s_resource_count = 0;
    s_summon.offspring = NULL;
}

static PeBattlePack *pe_load(const char *dir) {
    char err[200];
    for (int i = 0; i < s_resource_count; i++)
        if (!strcmp(s_resources[i].path, dir)) return &s_resources[i];
    if (s_resource_count == PE_FOES_MAX * PE_FORMS_MAX) return NULL;
    PeBattlePack *resource = &s_resources[s_resource_count];
    PeEnemyPack *pack = &resource->pack;
    if (pe_enemy_pack_load(pack, dir, err, sizeof err)) {
        pe_mod_log(MOD_ID, "enemy pack %s refused: %s", dir, err);
        pe_toast("Fights: that enemy pack can't be used (see pe_mods.log)");
        return NULL;
    }
    if (pack->tex_w > 2 * PE_TEX_W || (pack->tex_w & 1) || pack->tex_h > PE_TEX_H ||
        pack->palettes > PE_CLUTS_MAX) {
        pe_mod_log(MOD_ID, "enemy pack %s refused: texture must fit %dx%d with even width and up to %d palettes",
                   dir, 2 * PE_TEX_W, PE_TEX_H, PE_CLUTS_MAX);
        pe_enemy_pack_free(pack);
        return NULL;
    }
    snprintf(resource->path, sizeof resource->path, "%s", dir);
    resource->height = pe_pack_height(pack);
    resource->next = NULL;
    resource->uploaded = 0;
    s_resource_count++;
    pe_mod_log(MOD_ID, "enemy pack %s loaded: %s, %d bones, %d vertices, %d triangles, %d anims, %d attacks, hp %d",
               pack->folder, pack->name, pack->bones, pack->vertices, pack->triangles, pack->anims,
               pack->attacks, pack->hp);
    return resource;
}

int pe_enemy_battle_preview(PeEnemyPack *pack) {
    if (s.active || s_armed || pack->tex_w > 2 * PE_TEX_W || (pack->tex_w & 1) ||
        pack->tex_h > PE_TEX_H || pack->palettes > PE_CLUTS_MAX) return -1;
    pe_free_resources();
    PeBattlePack *resource = &s_resources[s_resource_count++];
    resource->pack = *pack;
    resource->path[0] = 0;
    resource->height = pe_pack_height(pack);
    resource->next = NULL;
    resource->uploaded = 0;
    s_roster[0] = resource;
    s_entrance[0].pose = s_entrance[0].wake = -1;
    s_entrance[0].loop = 0;
    s_send_away = s_count = s_armed = 1;
    return 0;
}

static PeBattlePack *pe_load_form(const char *asset) {
    char folder[64], dir[1200], manifest[1240], why[160];
    if (pe_enemy_pack_name(asset, folder, sizeof folder)) return NULL;
    snprintf(dir, sizeof dir, "%s/%s", s_dir, folder);
    const PeEnemyBuildApi *api = pe_service_get(PE_ENEMY_BUILD_SERVICE, PE_ENEMY_BUILD_SERVICE_VERSION);
    if (api && api->struct_size >= sizeof *api) api->ensure(asset, why, sizeof why);
    snprintf(manifest, sizeof manifest, "%s/enemy.json", dir);
    SDL_PathInfo info;
    return SDL_GetPathInfo(manifest, &info) && info.type == SDL_PATHTYPE_FILE ? pe_load(dir) : NULL;
}

/* Resolve successors before combat. A missing form ends the chain at the last available model. */
static void pe_load_forms(PeBattlePack *first) {
    PeBattlePack *seen[PE_FORMS_MAX] = {first};
    PeBattlePack *current = first;
    for (int i = 1; i < PE_FORMS_MAX && current->pack.next_form[0]; i++) {
        PeBattlePack *next = current->next;
        if (!next) {
            const char *asset = current->pack.next_form;
            const char *source = current->pack.source;
            const char *archive = strstr(source, "-cdemd");
            const char *colon = strchr(source, ':');
            char variant[sizeof current->pack.next_form];
            if (archive && colon && !strstr(asset, "-cdemd") &&
                !strncmp(source, asset, (size_t)(colon - source + 1)) &&
                snprintf(variant, sizeof variant, "%s%s", asset, archive) < (int)sizeof variant)
                next = pe_load_form(variant);
            if (!next) next = pe_load_form(asset);
        }
        if (!next) {
            pe_mod_log(MOD_ID, "next form %s unavailable for %s", current->pack.next_form, current->pack.name);
            break;
        }
        for (int j = 0; j < i; j++)
            if (seen[j] == next) {
                current->next = NULL;
                pe_mod_log(MOD_ID, "next form cycle for %s", current->pack.name);
                return;
            }
        current->next = next;
        seen[i] = current = next;
    }
}

void pe_enemy_battle_arm(int choice, int count, int send_away) {
    if (s.active) return;
    s_armed = 0;
    pe_free_resources();
    if (choice <= 0 || choice > s_listed) return;
    s_count = count < 1 ? 1 : count > PE_FOES_MAX ? PE_FOES_MAX : count;
    s_send_away = send_away;
    char dir[1200];
    if (pe_enemy_battle_pack_path(choice, dir, sizeof dir)) return;
    PeBattlePack *resource = pe_load(dir);
    if (!resource) return;
    pe_load_forms(resource);
    for (int i = 0; i < s_count; ++i) {
        s_roster[i] = resource;
        s_entrance[i].pose = s_entrance[i].wake = -1;
        s_entrance[i].loop = 0;
    }
    s_armed = 1;
}

/* Where each roster enemy stood beside Aya in the imported room (PE field units from her), so
 * the battle opens with them there instead of in a row ahead of her. */
static struct { int set; double dx, dz; } s_start[PE_FOES_MAX];

void pe_enemy_battle_set_start(int i, double dx, double dz) {
    if (i < 0 || i >= PE_FOES_MAX) return;
    s_start[i].set = 1, s_start[i].dx = dx, s_start[i].dz = dz;
}

void pe_enemy_battle_begin_roster(void) {
    if (s.active) return;
    memset(s_start, 0, sizeof s_start);
    s_armed = 0;
    s_count = 0;
    s.foes = s.placed_foes = 0;
    s_send_away = 1;
    pe_free_resources();
}

int pe_enemy_battle_add_pack(const char *pack, const char *entrance, const char *room_move, int follow_forms) {
    if (s.active || s_count >= PE_FOES_MAX) return 0;
    char dir[1200];
    const int written = snprintf(dir, sizeof dir, "%s/%s", s_dir, pack);
    if (written < 0 || (size_t)written >= sizeof dir) return 0;
    PeBattlePack *resource = pe_load(dir);
    if (!resource) return 0;
    if (follow_forms) pe_load_forms(resource);
    s_entrance[s_count].pose = entrance ? pe_enemy_pack_anim(&resource->pack, entrance) : -1;
    s_entrance[s_count].wake = -1;
    s_entrance[s_count].loop = 0;
    if (s_entrance[s_count].pose >= 0) {
        char wake[sizeof resource->pack.anim[0].name];
        const int written = snprintf(wake, sizeof wake, "%s" PE_IMP_ENEMY_WAKE_SUFFIX, entrance);
        if (written >= 0 && (size_t)written < sizeof wake)
            s_entrance[s_count].wake = pe_enemy_pack_anim(&resource->pack, wake);
    }
    const char *move_name = room_move ? strrchr(room_move, '/') : NULL;
    if (move_name) {
        char store[1100], why[160], name[24];
        snprintf(name, sizeof name, "feeding_%s", move_name + 1);
        int pose = pe_enemy_pack_anim(&resource->pack, name);
        PeImpAnimation moves;
        if (pose < 0 && !pe_mod_path(store, sizeof store, "import/store") &&
            !pe_imp_catalog_animation(store, room_move, &moves, why, sizeof why)) {
            pose = pe_enemy_add_room_move(&resource->pack, &moves, 0, name);
            pe_imp_animation_free(&moves);
        }
        if (pose >= 0) {
            s_entrance[s_count].pose = pose;
            s_entrance[s_count].wake = -1;
            s_entrance[s_count].loop = 1;
        }
    }
    s_roster[s_count++] = resource;
    s_armed = 1;
    return 1;
}

void pe_enemy_battle_arm_summon(int parent, const char *offspring, unsigned move) {
    if (!s_armed || s.active || parent < 0 || parent >= s_count || s_count >= PE_FOES_MAX) return;
    PeEnemyPack *pack = &s_roster[parent]->pack;
    char store[1100], asset[sizeof pack->source + sizeof "/moves"], why[160];
    PeImpAnimation moves;
    snprintf(asset, sizeof asset, "%s/moves", pack->source);
    if (pe_mod_path(store, sizeof store, "import/store") ||
        pe_imp_catalog_animation(store, asset, &moves, why, sizeof why)) return;
    const int anim = pe_enemy_add_room_move(pack, &moves, move, "summon");
    pe_imp_animation_free(&moves);
    if (anim < 0) return;
    const size_t length = strlen(offspring);
    for (int i = 0; i < s_count; ++i) {
        const char *source = s_roster[i]->pack.source;
        if (!strncmp(source, offspring, length) && (source[length] == '\0' || source[length] == '-')) {
            s_summon.offspring = s_roster[i];
            break;
        }
    }
    if (!s_summon.offspring) s_summon.offspring = pe_load_form(offspring);
    s_summon.parent = parent;
    s_summon.anim = anim;
}

unsigned pe_enemy_battle_defeated_mask(void) {
    unsigned defeated = 0;
    for (int i = 0; i < s.placed_foes; i++)
        if (s.foe[i].dead) defeated |= 1u << i;
    return defeated;
}

void pe_enemy_battle_arm_canister(void) { s_canister_armed = 1; }
void pe_enemy_battle_arm_launcher(void) { s_launcher_armed = 1; }
int pe_enemy_battle_canister_detonated(void) { return s.canister_detonated; }
int pe_enemy_battle_joined(void) { return s.active && s.joined; }

/* A battle started where Aya is, without a room battle (no spot, no room enemies): the steps of
 * the game's battle-entry opcode 85 (Boot_MemCardStepInit, phases 0 and 0x37..0x3B), here instead
 * of in a room script, which the opcode rewinds to run again. The room script before it only
 * plays its sounds and wakes its own enemies, which a battle of imported enemies doesn't want. */
static int s_here_phase = -1, s_here_mode, s_here_direct;
/* The room's own actors when the battle started here (its people): not in the battle, so not drawn
 * in front of the imported room's picture. Aya, the battle's own and the imported enemies are. */
#define PE_HERE_HIDDEN_MAX 64
static const FieldActor *s_here_hidden[PE_HERE_HIDDEN_MAX];
static int s_here_hidden_n, s_here_seen;

int pe_enemy_battle_start_here(int mode) {
    if (s_here_phase >= 0 || s.active) return -1;
    s_here_phase = 0, s_here_mode = mode;
    s_here_direct = 1;
    s_here_hidden_n = s_here_seen = 0;
    for (const FieldActor *a = g_FieldActorListHead; a && s_here_hidden_n < PE_HERE_HIDDEN_MAX; a = a->next)
        if (a != g_PlayerEntity) s_here_hidden[s_here_hidden_n++] = a;
    return 0;
}

int pe_enemy_battle_here_step(void) {
    Pe1GameState *base = &g_GameState;
    int status;
    for (;;) switch (s_here_phase) {
    case 0:
        base->flags |= 0x800000;
        if (base->story_day_flags & 3) return 0;             /* the game is still busy: next frame */
        s_here_phase = 0x37;
        break;
    case 0x37:
        if (!(base->flags & 0x400000)) MemCard_InitSlotState();
        s_here_phase = 0x38;
        break;
    case 0x38:
        if ((status = CdRom_StepReadState(1)) < 0) goto failed;
        if (status == 1) return 0;                            /* still reading the battle's data */
        if (!(base->flags & 0x400000)) Menu_SaveBgStartFadeOut();
        s_here_phase = 0x39;
        break;
    case 0x39:
        if ((status = Asset_LoadTimTextures(1)) < 0) goto failed;
        if (status == 1) return 0;
        s_here_phase = 0x3A;
        break;
    case 0x3A:
        g_GameStateFlags |= 2;
        Battle_StartEncounter(s_here_mode);
        s_here_phase = 0x3B;
        return 0;
    case 0x3B:
        if (base->story_day_flags & 3) return 0;
        base->flags &= ~0x800000u;
        s_here_phase = -1;
        return 1;
    default:
        return -1;
    }
failed:
    base->flags &= ~0x800000u;
    s_here_phase = -1;
    s_here_direct = 0;
    return -1;
}
void pe_enemy_battle_disarm(void) { s_armed = s_canister_armed = s_launcher_armed = 0; }

/* ---- actors ---- */

static int pe_actor_live(const PeFoe *f) {
    FieldActor *a;
    if (!f->actor) return 0;
    for (a = g_FieldActorListHead; a; a = a->next)
        if (a == f->actor) return a->type_id == PE_FOE_TYPE && a->field_sfx_id == f->serial;
    return 0;
}

static EnemyCombatant *pe_core(const PeFoe *f) {
    return pe_actor_live(f) ? (EnemyCombatant *)f->actor->state : NULL;
}

static PeFoe *pe_foe_of(const void *actor) {
    for (int i = 0; i < s.foes; ++i)
        if (s.foe[i].actor == actor && pe_actor_live(&s.foe[i])) return &s.foe[i];
    return NULL;
}

static int pe_is_room_enemy(FieldActor *a) {
    if (a == g_PlayerEntity || !a->state || a->type_id == PE_FOE_TYPE) return 0;
    for (int i = 0; i < 7; ++i)
        if (g_BattleEnemySlots[i].active && a->state == (void *)&g_BattleEnemySlots[i].combatant) return 1;
    return 0;
}

/* A new actor at the end of the list, like Scene_LoadMap's but with no model, script or callbacks. */
static FieldActor *pe_new_actor(void) {
    FieldActor *a = g_EntityFreeListHead, *t;
    if (!a) return NULL;
    g_EntityFreeListHead = a->next;
    memset(a, 0, sizeof *a);
    for (t = g_FieldActorListHead; t && t->next; t = t->next) {}
    a->prev = t;
    if (t) t->next = a;
    else g_FieldActorListHead = a;
    g_EntityFreePoolCount++;
    a->type_id = PE_FOE_TYPE;
    a->flags = 0x10000000 | 0x20;   /* 0x20: no hit-sphere pairs (it has no model parts) */
    a->move_factor = 0x50000;
    a->move_speed = 0x1000;
    a->pad_010 = 0xC8;
    a->field_sfx_id = (unsigned short)g_EntitySerialCounter++;
    a->shadow_intensity = 0x80;
    return a;
}

static void pe_set_anim(PeFoe *f, int role, int anim) {
    if (anim < 0) anim = f->resource->pack.role[PE_ANIM_IDLE];
    if (f->role == role && f->anim == anim) return;
    f->role = role;
    f->anim = anim;
    f->frame = 0;
    f->event_frame = -1;
    f->seen |= 1u << role;
}

/* The anim's sound events from the last frame run up to `to` (wrapping when it loops), on the
 * audio service's effect voices (pe_assets): footsteps, swings. Silent without the service. */
static int pe_play_sound(PeFoe *f, unsigned sound) {
    const PeEnemyPack *pack = &f->resource->pack;
    if (!sound || sound >= (unsigned)pack->sounds || !pack->sound[sound].pcm || pack->sound[sound].frames <= 1) return 0;
    static const PeAudioApi *audio;
    if (!audio) {
        const PeAudioApi *a = pe_service_get("audio", 1);
        if (a && a->struct_size >= sizeof *a && a->play_effect) audio = a;
    }
    f->sounds++;
    if (audio) audio->play_effect(pack->sound[sound].pcm, (uint32_t)pack->sound[sound].frames,
                                  (uint32_t)pack->sound[sound].rate, 1.0f);
    return 1;
}

static void pe_play_events(PeFoe *f, int to) {
    const PeEnemyPack *pack = &f->resource->pack;
    const PeEnemyAnim *an = &pack->anim[f->anim];
    if (!an->event || to == f->event_frame) return;
    for (int guard = 0, k = f->event_frame; guard < an->frames && k != to; guard++) {
        k = k + 1 >= an->frames ? 0 : k + 1;
        if (pe_play_sound(f, an->event[k] & PE_ENEMY_EVENT_SOUND) && f->anim == pack->role[PE_ANIM_DIE])
            pe_mod_log(MOD_ID, "battle: %s played its death cue", pack->name);
    }
    f->event_frame = to;
}

static int pe_angle_to(const FieldActor *from, const FieldActor *to) {
    double dx = (to->pos_x - from->pos_x) / 65536.0, dz = (to->pos_z - from->pos_z) / 65536.0;
    if (dx == 0 && dz == 0) return from->rot_y & 0xFFF;
    return (int)lrint(atan2(dx, dz) * 4096.0 / (2 * PE_PI)) & 0xFFF;
}

static double pe_dist(const FieldActor *a, const FieldActor *b) {
    double dx = (b->pos_x - a->pos_x) / 65536.0, dz = (b->pos_z - a->pos_z) / 65536.0;
    return sqrt(dx * dx + dz * dz);
}

static int pe_turn_diff(int from, int to) { return ((to - from + 0x800) & 0xFFF) - 0x800; }

static int pe_reach(const PeEnemyPack *pack, int attack) {
    return pack->attack[attack].range ? pack->attack[attack].range : pack->radius * 4;
}

/* Where Aya aims and where hits and numbers show: its middle, at its body size. */
static void pe_place_targets(PeFoe *f) {
    FieldActor *a = f->actor;
    const int height = f->resource->height, radius = f->resource->pack.radius;
    RenderObjectEntity *o = &a->render_object;
    const int x = a->pos_x >> 16, y = (a->pos_y >> 16) - f->lift, z = a->pos_z >> 16;
    o->target_x = (s16)x;
    o->target_y = (s16)(y - height * 6 / 10);
    o->target_z = (s16)z;
    o->hit_cylinder.value0 = (s16)x;
    o->hit_cylinder.value1 = (s16)y;
    o->hit_cylinder.value2 = (s16)z;
    o->hit_cylinder.radius = (s16)radius;
    o->hit_body.value0 = (s16)x;
    o->hit_body.value1 = (s16)(y - height / 2);
    o->hit_body.value2 = (s16)z;
    o->hit_body.radius = (s16)radius;
}

static void pe_set_combat_stats(EnemyCombatant *core, const PeEnemyPack *pack) {
    core->hpAlive = core->hpAliveCap = core->previousHP = pack->hp;
    core->defense = (u16)(pack->defense >= 0 ? pack->defense : 0);
    core->chargeStep = (u16)pack->charge;
    core->hpMirror = (u16)(pack->exp > 0xFFFF ? 0xFFFF : pack->exp);
}

static int pe_spawn(PeBattlePack *resource, int entrance, int wake, int loop, int x, int y, int z) {
    const PeEnemyPack *pack = &resource->pack;
    PeFoe *f = &s.foe[s.foes];
    FieldActor *a, *aya = g_PlayerEntity;
    EnemyCombatant *core;
    if (s.foes >= PE_FOES_MAX || !(a = pe_new_actor())) return 0;
    a->pos_x = a->base_x = x;
    a->pos_y = a->base_y = y;
    a->pos_z = a->base_z = z;
    a->rot_y = (short)(aya ? pe_angle_to(a, aya) : 0);
    /* the floor under it, for the game's wall and floor checks when it moves; none found
     * (off the room's floor): it keeps out of those checks */
    Entity_FindFloor((BattleEntity *)a);
    if (!a->collision_face) a->flags |= 0x80;
    Entity_AllocSlot((BattleEntity *)a);
    if (!(core = (EnemyCombatant *)a->state)) {   /* all 7 enemy places taken */
        a->flags |= 0x10;
        return 0;
    }
    pe_set_combat_stats(core, pack);
    core->deathAssetEnabled = 0;
    core->deathAnimFrame = 0;
    core->effect = &core->records[0];
    core->effect->state = 0;
    memset(f, 0, sizeof *f);
    f->actor = a;
    f->resource = resource;
    f->serial = a->field_sfx_id;
    f->role = -1;
    f->last_hp = pack->hp;
    f->wake_anim = wake;
    f->entrance_loop = loop;
    if (entrance >= 0) {
        pe_set_anim(f, ROLE_ENTRANCE, entrance);
        f->entrance_frames = loop ? 90 : 24;
        pe_mod_log(MOD_ID, "battle: %s starts with %s", pack->name, pack->anim[entrance].name);
    } else {
        pe_set_anim(f, PE_ANIM_IDLE, pack->role[PE_ANIM_IDLE]);
    }
    pe_place_targets(f);
    s.foes++;
    pe_mod_log(MOD_ID, "battle: %s joins at %d,%d (enemy place bit %d, hp %d%s)", pack->name, x >> 16, z >> 16,
               core->slotBit, pack->hp, a->collision_face ? "" : ", no floor found");
    return 1;
}

/* A room enemy out of the battle, as if never there: no EXP, no death steps. One already at 0 HP
 * (beaten before the room's enemies went, as by Cheats > Win this battle during an opening) is
 * left to the game's death steps: the last of those checks for the victory. */
static void pe_send_away(FieldActor *a) {
    EnemyCombatant *core = (EnemyCombatant *)a->state;
    FieldActor *c;
    if ((int)core->hpAlive <= 0) return;
    if (!(a->flags & 0x2000)) g_BattleLiveEnemyCount--;
    core->hpAlive = 0;
    a->flags |= 0x10;   /* freed at the end of the frame, with its scripts and parts */
    for (c = g_FieldActorListHead; c; c = c->next)
        if (c->parent == a) c->flags |= 0x10;
    Battle_SlotFree((BattleEntity *)a);
    s.sent_away++;
}

/* ---- VRAM ---- */

static void pe_vram(PeBattlePack *resource, int put_back) {
    RECT tex = resource->texture;
    const PeEnemyPack *pack = &resource->pack;
    const int clut_x = resource->clut.x, clut_y = resource->clut.y;
    int i;
    if (!put_back) {   /* save what is there, then ours */
        StoreImage(&tex, resource->saved_tex);
        for (i = 0; i < pack->palettes; ++i) {
            RECT c = {(short)clut_x, (short)(clut_y + i), 256, 1};
            StoreImage(&c, resource->saved_clut[i]);
        }
        DrawSync(0);
        LoadImage(&tex, pack->pixels);
        for (i = 0; i < pack->palettes; ++i) {
            RECT c = {(short)clut_x, (short)(clut_y + i), 256, 1};
            LoadImage(&c, pack->palette + 256 * i);
        }
    } else {
        LoadImage(&tex, resource->saved_tex);
        for (i = 0; i < pack->palettes; ++i) {
            RECT c = {(short)clut_x, (short)(clut_y + i), 256, 1};
            LoadImage(&c, resource->saved_clut[i]);
        }
    }
    DrawSync(0);
    resource->uploaded = !put_back;
}

/* ---- where the texture goes ----
 * Rooms keep their pictures in VRAM in no fixed place: background tiles, enemy and effect
 * textures (M152 keeps background tiles where Central Park leaves VRAM free). So the blocks for
 * the imported enemy's texture and palettes are chosen per battle. While the battle opens, every
 * textured packet of each frame marks the VRAM it reads (texels and palette) in s_used, a map of
 * 16-pixel columns per VRAM line; the display and draw areas are marked too. At the join, each
 * block goes where nothing drawn read, with the fewest non-black pixels (pictures loaded but not
 * drawn yet, like menus and effects, are not black); the first choice wins a tie. */

#define PE_COLS (1024 / 16)
#ifndef PSYZ_MERGED_PRIM_NOP
#define PSYZ_MERGED_PRIM_NOP 0x005A4D50U   /* PsyZ libgpu.h: marks a merged packet's packed words */
#endif
static u8 s_used[512][PE_COLS];
static u16 s_vram[1024 * 512];
static int s_used_frames;

static void pe_mark(int x0, int y0, int x1, int y1) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > 1023) x1 = 1023;
    if (y1 > 511) y1 = 511;
    if (x0 > x1) return;
    for (int y = y0; y <= y1; ++y) memset(&s_used[y][x0 >> 4], 1, (size_t)((x1 >> 4) - (x0 >> 4) + 1));
}

static void pe_mark_rect(const RECT *r) {
    if (r->w > 0 && r->h > 0) pe_mark(r->x, r->y, r->x + r->w - 1, r->y + r->h - 1);
}

/* Texels u0..u1, v0..v1 of a texture page and the palette they use. */
static void pe_mark_texels(u32 tpage, u32 clut, int u0, int v0, int u1, int v1) {
    const int bx = (int)(tpage & 15) * 64, by = (int)((tpage >> 4) & 1) * 256;
    const int depth = (int)((tpage >> 7) & 3), shift = depth == 0 ? 2 : depth == 1 ? 1 : 0;
    const int cx = (int)(clut & 0x3F) * 16, cy = (int)((clut >> 6) & 0x1FF);
    pe_mark(bx + (u0 >> shift), by + v0, bx + (u1 >> shift), by + v1);
    if (depth < 2) pe_mark(cx, cy, cx + (depth ? 255 : 15), cy);
}

/* One packet's GPU commands (32-bit words); tpage follows the draw mode as the GPU does. */
static void pe_mark_words(const u32 *w, int n, u32 *tpage) {
    int i = 0;
    while (i < n) {
        const u32 c = w[i] >> 24;
        int len = 1;
        if (c >= 0x20 && c < 0x40) {   /* polygons */
            const int gouraud = (c & 0x10) != 0, tex = (c & 4) != 0, verts = (c & 8) ? 4 : 3;
            len = 1 + verts * (tex ? 2 : 1) + (gouraud ? verts - 1 : 0);
            if (i + len > n) return;
            if (tex) {
                int u0 = 255, u1 = 0, v0 = 255, v1 = 0;
                u32 clut = 0;
                for (int k = 0; k < verts; ++k) {
                    const u32 uv = w[i + (gouraud ? 2 + 3 * k : 2 + 2 * k)];
                    const int u = (int)(uv & 0xFF), v = (int)((uv >> 8) & 0xFF);
                    if (u < u0) u0 = u;
                    if (u > u1) u1 = u;
                    if (v < v0) v0 = v;
                    if (v > v1) v1 = v;
                    if (k == 0) clut = uv >> 16;
                    if (k == 1) *tpage = (uv >> 16) & 0x1FF;
                }
                pe_mark_texels(*tpage, clut, u0, v0, u1, v1);
            }
        } else if (c >= 0x40 && c < 0x60) {   /* lines: no texture */
            if (c & 8) {   /* a polyline ends at its 0x5xxx5xxx word */
                for (len = 2; i + len < n && (w[i + len] & 0xF000F000u) != 0x50005000u; ++len) {}
                ++len;
            } else {
                len = (c & 0x10) ? 4 : 3;
            }
        } else if (c >= 0x60 && c < 0x80) {   /* rectangles */
            const int tex = (c & 4) != 0, size = (int)((c >> 3) & 3);
            len = 2 + tex + (size == 0);
            if (i + len > n) return;
            if (tex) {
                const u32 uv = w[i + 2];
                const int u = (int)(uv & 0xFF), v = (int)((uv >> 8) & 0xFF);
                int ww = size == 1 ? 1 : size == 2 ? 8 : 16, hh = ww;
                if (size == 0) {
                    ww = (int)(w[i + 3] & 0x3FF);
                    hh = (int)((w[i + 3] >> 16) & 0x1FF);
                }
                if (ww > 0 && hh > 0)
                    pe_mark_texels(*tpage, uv >> 16, u, v, u + ww - 1 > 255 ? 255 : u + ww - 1,
                                   v + hh - 1 > 255 ? 255 : v + hh - 1);
            }
        } else if (c == 0x02 || (c >= 0xC0 && c < 0xE0)) {
            len = 3;
        } else if (c >= 0x80 && c < 0xA0) {
            len = 4;
        } else if (c >= 0xA0 && c < 0xC0) {   /* an upload carries its pixels */
            if (i + 3 > n) return;
            len = 3 + (int)(((w[i + 2] & 0xFFFF) * (w[i + 2] >> 16) + 1) / 2);
        } else if (c == 0xE1) {
            *tpage = w[i] & 0x1FF;
        }
        i += len;
    }
}

/* Stage 1 of the room picture hook: the frame is linked, from entry 4095 (drawn first) down. */
static int pe_on_background(void *ctx, void *user) {
    const PeHookFieldBackground *b = (const PeHookFieldBackground *)ctx;
    const RenderNativeTag *p;
    u32 tpage = 0, words[256];
    int guard = 0;
    (void)user;
    if (b->stage != 1 || !s.active || s.joined || !b->ordering) return 0;
    for (p = (const RenderNativeTag *)b->ordering + 4095; p && guard < 1000000; ++guard) {
        const int n = (int)p->length;
        if (n > 0 && n <= 256) {
            const u32 *w = (const u32 *)(p + 1);
            int packed = (w[0] >> 24) >= 0x20 && (w[0] >> 24) < 0x80;
            for (int i = 0; i < n && !packed; ++i) packed = w[i] == PSYZ_MERGED_PRIM_NOP;
            if (packed) {
                pe_mark_words(w, n, &tpage);
            } else {   /* a u_long stream: each element's low word is the command word */
                const uintptr_t *lw = (const uintptr_t *)(p + 1);
                for (int i = 0; i < n; ++i) words[i] = (u32)lw[i];
                pe_mark_words(words, n, &tpage);
            }
        }
        if (p->address == 0xFFFFFF || p->address == 0) break;
        p = (const RenderNativeTag *)p->address;
    }
    s_used_frames++;
    return 0;
}

/* Cells the room read (times a million) plus pixels that are not black, in a VRAM block. */
static long long pe_block_cost(int x, int y, int w, int h) {
    long long used = 0, lit = 0;
    for (int j = y; j < y + h; ++j) {
        for (int c = x >> 4; c <= (x + w - 1) >> 4; ++c) used += s_used[j][c];
        for (int i = x; i < x + w; ++i) lit += s_vram[j * 1024 + i] != 0;
    }
    return used * 1000000LL + lit;
}

static int pe_overlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

static int pe_vram_reserved(int x, int y, int w, int h) {
    for (int i = 0; i < s_resource_count; i++) {
        const PeBattlePack *p = &s_resources[i];
        if (!p->uploaded) continue;
        if (pe_overlap(x, y, w, h, p->texture.x, p->texture.y, p->texture.w, p->texture.h) ||
            pe_overlap(x, y, w, h, p->clut.x, p->clut.y, p->clut.w, p->clut.h)) return 1;
    }
    return 0;
}

static int pe_place(PeBattlePack *resource) {
    const PeEnemyPack *pack = &resource->pack;
    const int tw = pack->tex_w / 2, th = pack->tex_h, rows = pack->palettes;
    RECT all = {0, 0, 1024, 512};
    long long best, cost;
    for (int i = 0; i < 2; ++i) {
        pe_mark_rect(&g_RenderDrawEnvArray[i].clip);
        pe_mark_rect(&g_RenderDispEnvArray[i].disp);
    }
    StoreImage(&all, s_vram);
    DrawSync(0);
    /* the texture: a texture page column (x a multiple of 64), its lines inside one page row */
    RECT texture = {PE_TEX_X, PE_TEX_Y, (short)tw, (short)th};
    best = pe_vram_reserved(texture.x, texture.y, tw, th) ? -1 : pe_block_cost(texture.x, texture.y, tw, th);
    for (int page = 0; page < 2; ++page)
        for (int dy = 0; dy + th <= 256; dy += 16)
            for (int x = 0; x + tw <= 1024; x += 64)
                if (!pe_vram_reserved(x, page * 256 + dy, tw, th) &&
                    ((cost = pe_block_cost(x, page * 256 + dy, tw, th)) < best || best < 0)) {
                    best = cost;
                    texture.x = x;
                    texture.y = page * 256 + dy;
                }
    if (best < 0) return 0;
    pe_mod_log(MOD_ID, "battle: %s texture at %d,%d (%lld cells read by the room, %lld pixels not black; %d frames looked at)",
               pack->name, texture.x, texture.y, best / 1000000LL, best % 1000000LL, s_used_frames);
    /* the palettes: 256 wide (x a multiple of 256), clear of the texture */
    RECT clut = {PE_CLUT_X, PE_CLUT_Y, 256, (short)rows};
    best = pe_overlap(clut.x, clut.y, 256, rows, texture.x, texture.y, tw, th) ||
           pe_vram_reserved(clut.x, clut.y, 256, rows) ? -1 : pe_block_cost(clut.x, clut.y, 256, rows);
    for (int x = 0; x < 1024; x += 256)
        for (int y = 0; y + rows <= 512; ++y)
            if (!pe_overlap(x, y, 256, rows, texture.x, texture.y, tw, th) &&
                !pe_vram_reserved(x, y, 256, rows) &&
                ((cost = pe_block_cost(x, y, 256, rows)) < best || best < 0)) {
                best = cost;
                clut.x = x;
                clut.y = y;
            }
    if (best < 0) return 0;
    pe_mod_log(MOD_ID, "battle: %s palettes at %d,%d (%lld cells read by the room, %lld pixels not black)",
               pack->name, clut.x, clut.y, best / 1000000LL, best % 1000000LL);
    resource->texture = texture;
    resource->clut = clut;
    return 1;
}

/* ---- the battle ---- */

static void pe_join(int early) {
    FieldActor *a, *first = NULL, *aya = g_PlayerEntity;
    int x, y, z, i;
    s.joined = 1;
    s.away_later = s_send_away && early;
    if (!aya) return;
    for (a = g_FieldActorListHead; a; a = a->next)
        if (pe_is_room_enemy(a) && !(a->flags & 0x4000) && ((EnemyCombatant *)a->state)->hpAlive > 0) {
            if (!first) first = a;
            s.room_enemies++;
        }
    if (first) { x = first->pos_x; y = first->pos_y; z = first->pos_z; }
    else {   /* no room enemy to stand by: in front of Aya */
        x = aya->pos_x + (int)(sin(aya->rot_y * 2 * PE_PI / 4096.0) * 2000.0 * 65536.0);
        y = aya->pos_y;
        z = aya->pos_z + (int)(cos(aya->rot_y * 2 * PE_PI / 4096.0) * 2000.0 * 65536.0);
    }
    if (s_send_away && !s.away_later)
        for (a = g_FieldActorListHead; a; a = a->next)
            if (pe_is_room_enemy(a)) pe_send_away(a);
    int radius = 0;
    for (i = 0; i < s_count; ++i)
        if (s_roster[i]->pack.radius > radius) radius = s_roster[i]->pack.radius;
    for (i = 0; i < s_count; ++i) {
        PeBattlePack *resource = s_roster[i];
        /* more than one: side by side across Aya's view of the spot; beside the room's enemy
         * when it stays */
        const double ang = atan2((x - aya->pos_x) / 65536.0, (z - aya->pos_z) / 65536.0) + PE_PI / 2;
        double side = (i - (s_count - 1) / 2.0) * (radius * 3.0);
        if ((!s_send_away || s.away_later) && first) side += radius * 3.0;
        if (!resource->uploaded && !pe_place(resource)) {
            pe_mod_log(MOD_ID, "battle: no VRAM left for %s", resource->pack.name);
            break;
        }
        int fx = x + (int)(sin(ang) * side * 65536.0), fz = z + (int)(cos(ang) * side * 65536.0);
        if (s_start[i].set)
            fx = aya->pos_x + (int)(s_start[i].dx * 65536.0), fz = aya->pos_z + (int)(s_start[i].dz * 65536.0);
        if (!pe_spawn(resource, s_entrance[i].pose, s_entrance[i].wake, s_entrance[i].loop, fx, y, fz)) break;
        if (!resource->uploaded) pe_vram(resource, 0);
    }
    s.placed_foes = s.foes;
    if (!s.foes) {
        pe_mod_log(MOD_ID, "battle: no foe joined for %s", s_roster[0]->pack.name);
    } else if (s.canister_exposed) {
        pe_toast("The Alligator has the canister. Shoot it.");
    }
}

static void pe_begin(void) {
    memset(&s, 0, sizeof s);
    memset(s_used, 0, sizeof s_used);
    s_used_frames = 0;
    s.active = 1;
    s.canister_exposed = s_canister_armed;
    if (s_here_direct) {        /* no room enemies to take their places and start the clock: now */
        s_here_direct = 0;
        g_BattleEnemyEscapeFlag = 1;
    }
    s.launcher = s_launcher_armed ? LAUNCHER_WAITING : LAUNCHER_NONE;
    s_canister_armed = s_launcher_armed = 0;
}

static void pe_end(const char *why) {
    int beaten = 0, i;
    if (!s.active) return;
    for (i = 0; i < s.foes; ++i) {
        PeFoe *f = &s.foe[i];
        if (pe_actor_live(f)) {   /* still standing (Aya ran, or lost): out with the battle */
            f->actor->flags |= 0x10;
            if (f->actor->state) Battle_SlotFree((BattleEntity *)f->actor);
        }
        beaten += f->dead;
    }
    for (i = 0; i < s_resource_count; ++i)
        if (s_resources[i].uploaded) pe_vram(&s_resources[i], 1);
    snprintf(s.result, sizeof s.result, "%s %s (beaten %d of %d)",
             s_resource_count == 1 ? s_roster[0]->pack.name : "Room enemies", why, beaten, s.foes);
    pe_mod_log(MOD_ID, "battle over: %s", s.result);
    s.active = 0;
}

static int pe_transform(PeFoe *f, EnemyCombatant *core) {
    PeBattlePack *old = f->resource, *next = old->next;
    if (!next || f->forms >= PE_FORMS_MAX - 1) return 0;
    int shared = 0;
    for (int i = 0; i < s.foes; i++)
        if (&s.foe[i] != f && s.foe[i].resource == old && pe_actor_live(&s.foe[i])) shared = 1;
    if (!shared) pe_vram(old, 1);
    if (!next->uploaded) {
        if (!pe_place(next)) {
            if (!shared) pe_vram(old, 0);
            pe_mod_log(MOD_ID, "battle: no VRAM left for next form %s", next->pack.name);
            return 0;
        }
        pe_vram(next, 0);
    }
    ++f->forms;
    f->resource = next;
    f->attack = f->stagger = f->entrance_frames = 0;
    f->wake_anim = -1;
    f->last_hp = next->pack.hp;
    pe_set_combat_stats(core, &next->pack);
    core->coreFlags &= 0x7FFFFFFFu;
    core->atbCharge = 0;
    pe_set_anim(f, PE_ANIM_IDLE, next->pack.role[PE_ANIM_IDLE]);
    pe_place_targets(f);
    pe_mod_log(MOD_ID, "battle: %s transforms into %s (form %d, hp %d)", old->pack.name, next->pack.name,
               f->forms + 1, core->hpAlive);
    return 1;
}

/* Keep the actor's floor anchor for the native wall checks. Rendering and targeting
 * use the same height, including the approach to a strike and the fall on death. */
static void pe_step_lift(PeFoe *f, const FieldActor *aya, int hp) {
    const int cruise = f->resource->pack.altitude;
    int target = cruise;
    int step = 12;
    if (hp <= 0) {
        target = 0;
        step = 30;
    } else if (f->attack && cruise) {
        const int floor_delta = aya ? (f->actor->pos_y >> 16) - (aya->pos_y >> 16) : 0;
        target = floor_delta + 300 - f->resource->height / 2;
        target = target < 0 ? 0 : target > cruise ? cruise : target;
        step = 24;
    }
    const int delta = target - f->lift;
    f->lift += delta < -step ? -step : delta > step ? step : delta;
}

static void pe_summon(PeFoe *parent) {
    PeBattlePack *offspring = s_summon.offspring;
    s_summon.offspring = NULL;
    if (!offspring->uploaded && !pe_place(offspring)) return;
    const FieldActor *a = parent->actor;
    const double angle = (a->rot_y + 768) * 2 * PE_PI / 4096.0;
    const double offset = (parent->resource->pack.radius + offspring->pack.radius) * 65536.0;
    if (pe_spawn(offspring, -1, -1, 0, a->pos_x + (int)(sin(angle) * offset), a->pos_y,
                 a->pos_z + (int)(cos(angle) * offset))) {
        if (!offspring->uploaded) pe_vram(offspring, 0);
        pe_mod_log(MOD_ID, "battle: %s summoned %s", parent->resource->pack.name, offspring->pack.name);
    }
}

static void pe_foe_think(PeFoe *f) {
    FieldActor *a = f->actor, *aya = g_PlayerEntity;
    EnemyCombatant *core = pe_core(f);
    const PeEnemyPack *pack = &f->resource->pack;
    const PeEnemyAnim *cur = &pack->anim[f->anim];
    int hp, loop, done, want, diff;
    double dist;

    a->flags |= 0x800000;   /* the game's anim step leaves it alone (it has no game anim) */
    a->anim_step = 0;
    if (!core) return;
    hp = core->hpAlive;
    if (hp <= 0 && !f->dead && pe_transform(f, core)) return;
    /* time stopped, as for the game's own enemies: they move only while the battle runs (mode 0);
     * Aya's command menu (mode 1, flag 4), her action and its aftermath, or an enemies-stop (0x100) */
    if ((g_GameStateFlags & (0x100 | 4)) || g_BattleModeState != 0) { pe_place_targets(f); return; }
    pe_step_lift(f, aya, hp);
    if (f->entrance_frames && hp > 0) {
        /* Hold the source pose through the battle opening or until a hit. */
        if (hp < f->last_hp || !--f->entrance_frames) {
            f->entrance_frames = 0;
            if (f->wake_anim >= 0) {
                pe_set_anim(f, ROLE_WAKE, f->wake_anim);
                pe_mod_log(MOD_ID, "battle: %s wakes with %s", pack->name, pack->anim[f->wake_anim].name);
            } else {
                pe_set_anim(f, PE_ANIM_IDLE, pack->role[PE_ANIM_IDLE]);
            }
        } else if (f->entrance_loop) {
            f->frame = fmod(f->frame + pack->fps / 30.0, cur->frames);
        }
        f->last_hp = hp;
        pe_place_targets(f);
        return;
    }

    /* the frame: idle and walk loop, the rest hold their last frame */
    loop = f->role == PE_ANIM_IDLE || f->role == PE_ANIM_WALK;
    f->frame += pack->fps / 30.0;
    if (f->frame >= cur->frames) f->frame = loop ? fmod(f->frame, cur->frames) : cur->frames - 1;
    done = f->frame >= cur->frames - 1;
    pe_play_events(f, (int)f->frame);

    if (hp <= 0) {   /* death: its anim plays, then the game fades and frees it */
        if (!f->dead) {
            const int die = pack->role[PE_ANIM_DIE];
            if (die < 0 && pe_play_sound(f, (unsigned)pack->death_sound))
                pe_mod_log(MOD_ID, "battle: %s played its death cue", pack->name);
            const int frames = die >= 0 ? (int)(pack->anim[die].frames * 30.0 / pack->fps) : 1;
            f->dead = 1;
            f->attack = 0;
            core->coreFlags &= 0x7FFFFFFFu;
            pe_set_anim(f, PE_ANIM_DIE, die);
            /* the game's death steps wait while the actor's frame is below deathAnimFrame */
            core->deathAnimFrame = (u8)(frames > 250 ? 250 : frames < 1 ? 1 : frames);
            core->deathPersist = 1;
            f->frame = 0;
            done = 0;
        }
        int elapsed = (int)(f->frame * 30.0 / pack->fps);
        if (f->lift && elapsed >= core->deathAnimFrame) elapsed = core->deathAnimFrame - 1;
        a->anim.fixed = (done && !f->lift ? core->deathAnimFrame : elapsed) << 16;
        pe_place_targets(f);
        return;
    }

    if (f->role == ROLE_WAKE || f->role == ROLE_SUMMON) {
        if (f->role == ROLE_SUMMON && s_summon.offspring && f->frame >= (cur->frames - 1) / 2.0)
            pe_summon(f);
        if (done) pe_set_anim(f, PE_ANIM_IDLE, pack->role[PE_ANIM_IDLE]);
        f->last_hp = hp;
        pe_place_targets(f);
        return;
    }

    /* hurt: a shot stops it for its hurt anim, unless it is mid-attack */
    if (hp < f->last_hp && !f->attack && pack->stagger && pack->role[PE_ANIM_HURT] >= 0) {
        f->role = -1;
        pe_set_anim(f, PE_ANIM_HURT, pack->role[PE_ANIM_HURT]);
        f->stagger = 1;
        done = 0;
    }
    if (hp < f->last_hp && pack->role[PE_ANIM_HURT] < 0) pe_play_sound(f, (unsigned)pack->hurt_sound);
    f->last_hp = hp;
    if (f->stagger) {
        if (!done) { pe_place_targets(f); return; }
        f->stagger = 0;
    }

    dist = aya ? pe_dist(a, aya) : 1e9;
    want = aya ? pe_angle_to(a, aya) : a->rot_y;

    if (f->attack) {
        const PeEnemyAttack *atk = &pack->attack[f->attack - 1];
        if (!f->blow_done && f->frame >= atk->hit) {
            f->blow_done = 1;
            diff = pe_turn_diff(a->rot_y, want);
            const int aim_y = aya ? (aya->pos_y >> 16) - 300 : 0;
            const int body_y = (a->pos_y >> 16) - f->lift - f->resource->height / 2;
            if (aya && aya->state && dist <= pe_reach(pack, f->attack - 1) * 1.15 && abs(diff) < 0x300 &&
                (!pack->altitude || abs(aim_y - body_y) <= 180)) {
                /* the enemy-hit path: the attack record (power, ready), the hit bits on the
                 * enemy and on Aya; Menu_MainUpdate takes the HP with her defense and armor */
                Combatant *ayc = (Combatant *)aya->state;
                core->effect = &core->records[0];
                core->effect->state = 1;
                core->effect->effectType = 0;
                core->effect->category = 0;
                core->effect->power = (u16)atk->power;
                core->coreFlags = (core->coreFlags & ~(7u << 21)) | 0x80000000u;
                ayc->stateFlags |= 0x4000;
                f->hits++;
                s.last_blow = f;
                s.last_blow_frames = 3;
            }
        }
        if (!done) { pe_place_targets(f); return; }
        f->attack = 0;
        core->coreFlags &= 0x7FFFFFFFu;   /* a blow the game did not take is over */
    }

    /* turn toward Aya */
    diff = pe_turn_diff(a->rot_y, want);
    a->rot_y = (short)((a->rot_y + (diff > PE_TURN_STEP ? PE_TURN_STEP : diff < -PE_TURN_STEP ? -PE_TURN_STEP : diff)) & 0xFFF);

    if (s_summon.offspring && s_summon.parent < s.placed_foes && f == &s.foe[s_summon.parent] &&
        core->curHP >= PE_CHARGE_FULL) {
        core->curHP = 0;
        pe_set_anim(f, ROLE_SUMMON, s_summon.anim);
        pe_place_targets(f);
        return;
    }

    /* attack when its time bar is full and Aya is in reach of one of its attacks */
    if (pack->attacks && core->curHP >= PE_CHARGE_FULL && abs(diff) < 0x200) {
        for (int k = 0; k < pack->attacks; ++k) {
            const int c = (f->attacks_started + k) % pack->attacks;
            if (dist <= pe_reach(pack, c)) {
                f->attack = c + 1;
                f->blow_done = 0;
                f->attacks_started++;
                core->curHP = 0;   /* its turn spent */
                f->role = -1;
                pe_set_anim(f, ROLE_ATTACK, pack->attack[c].anim);
                pe_place_targets(f);
                return;
            }
        }
    }

    {   /* walk to Aya, stopping a little inside its shortest reach */
        int reach = 0;
        for (int k = 0; k < pack->attacks; ++k)
            if (!reach || pe_reach(pack, k) < reach) reach = pe_reach(pack, k);
        if (!reach) reach = pack->radius * 3;
        const int moving = f->role == PE_ANIM_WALK;   /* starts a little further out than it stops */
        /* (at least a unit to go: steps shrink as it closes in, and would never quite end) */
        if (aya && dist > reach * (moving ? 0.8 : 0.9) && dist - reach * 0.8 >= 1.0 && pack->speed > 0 &&
            abs(diff) < 0x400) {
            const double left = dist - reach * 0.8;
            const double step = pack->speed < left ? pack->speed : left;
            const double ang = a->rot_y * 2 * PE_PI / 4096.0;
            a->base_x = a->pos_x;
            a->base_y = a->pos_y;
            a->base_z = a->pos_z;
            a->pos_x += (int)(sin(ang) * step * 65536.0);
            a->pos_z += (int)(cos(ang) * step * 65536.0);
            pe_set_anim(f, PE_ANIM_WALK, pack->role[PE_ANIM_WALK]);
        } else {
            pe_set_anim(f, PE_ANIM_IDLE, pack->role[PE_ANIM_IDLE]);
        }
    }
    pe_place_targets(f);
}

/* EM2B.C: damage opens the launcher sequence, and the Tyrant stays alive
 * until the finisher. Here the pickup and shot use a temporary battle action. */
static void pe_step_launcher(void) {
    if (s.launcher < LAUNCHER_WAITING || s.launcher >= LAUNCHER_FIRED || !s.foes || !g_PlayerEntity) return;
    EnemyCombatant *enemy = pe_core(&s.foe[0]);
    Combatant *aya = (Combatant *)g_PlayerEntity->state;
    if (!enemy || enemy->hpAlive <= 0 || !aya || !aya->curHP) return;
    if (s.launcher == LAUNCHER_WAITING && enemy->hpAlive <= enemy->maxHp / 2) {
        s.launcher = LAUNCHER_DROPPED;
        pe_toast("Rocket launcher dropped. Square to pick it up.");
        pe_mod_log(MOD_ID, "battle: Tyrant rocket launcher dropped");
        return;
    }
    if (s.launcher == LAUNCHER_WAITING || g_BattleModeState != 0 || !g_BattleEnemyEscapeFlag ||
        (g_GameStateFlags & 0x100) || !(g_FieldPadPressed & PE_BATTLE_SQUARE)) return;
    g_FieldPadPressed &= ~PE_BATTLE_SQUARE;
    if (s.launcher == LAUNCHER_DROPPED) {
        s.launcher = LAUNCHER_HELD;
        pe_toast("Rocket launcher ready. At full ATB, press Square to fire.");
        pe_mod_log(MOD_ID, "battle: Aya picked up the Tyrant rocket launcher");
    } else if (aya->atb >= PE_CHARGE_FULL) {
        s.launcher = LAUNCHER_FIRED;
        aya->atb = 0;
        enemy->hpAlive = 0;
        pe_toast("Rocket hit the Tyrant.");
        pe_mod_log(MOD_ID, "battle: Aya fired the Tyrant rocket launcher");
    } else {
        pe_toast("Wait for full ATB, then press Square to fire.");
    }
}

static int pe_on_battle(void *ctx, void *user) {
    (void)ctx; (void)user;
    if (s_here_phase == 0x3B) pe_enemy_battle_here_step();   /* the entry's last step, in the battle */
    if (!s.active && s_armed) {
        pe_begin();
        s_armed = 0;
    }
    if (!s.active) return 0;
    if (!s.joined) {
        /* Join once the opening is over: the room's battle script starts the clock
         * (Script_SignalExit) when its enemies are in place. Sent away before that, they never
         * would, and no time bar would fill. */
        ++s.frames;
        if (g_BattleEnemyEscapeFlag) s.open_frames++;
        if (s.open_frames >= PE_OPEN_FRAMES) pe_join(0);
        else if (s.frames >= PE_OPEN_LIMIT) {
            pe_mod_log(MOD_ID, "battle: the opening is still on; %s joins%s", s_roster[0]->pack.name,
                       s_send_away ? ", the room's enemies go when the clock runs" : "");
            g_BattleEnemyEscapeFlag = 1;
            pe_join(1);
        }
        return 0;
    }
    if (s.away_later && g_PlayerEntity && g_PlayerEntity->state && ((Combatant *)g_PlayerEntity->state)->atb > 0) {
        pe_mod_log(MOD_ID, "battle: the clock runs; the room's enemies go");
        s.away_later = 0;
    }
    if (s_send_away && !s.away_later) {   /* a room enemy that turns up later goes too */
        for (FieldActor *a = g_FieldActorListHead; a; a = a->next)
            if (pe_is_room_enemy(a) && !(a->flags & 0x10)) pe_send_away(a);
    }
    pe_step_launcher();
    for (int i = 0; i < s.foes; ++i)
        if (pe_actor_live(&s.foe[i])) pe_foe_think(&s.foe[i]);
    if (s.last_blow_frames && !--s.last_blow_frames) s.last_blow = NULL;
    return 0;
}

/* A pack was built (the importer's enemies target, maybe on its worker): list them again on the
 * main thread, between fights. */
static SDL_AtomicInt s_rescan;
void pe_enemy_packs_changed(void) { SDL_SetAtomicInt(&s_rescan, 1); }

static int pe_on_frame(void *ctx, void *user) {
    (void)ctx; (void)user;
    if (!s.active && !s_armed && SDL_GetAtomicInt(&s_rescan)) {
        SDL_SetAtomicInt(&s_rescan, 0);
        pe_free_resources();
        pe_scan();
    }
    if (s.active && !pe_in_battle()) {
        int beaten = 0;
        for (int i = 0; i < s.foes; ++i) beaten += s.foe[i].dead;
        pe_end(s.foes && beaten == s.foes ? "beaten" : "left behind");
    }
    return 0;
}

/* ---- drawing ---- */

static void pe_draw(PeFoe *f, const u32 *view_words) {
    FieldActor *a = f->actor;
    const PeBattlePack *resource = f->resource;
    const PeEnemyPack *pack = &resource->pack;
    EnemyCombatant *core = (EnemyCombatant *)a->state;
    const GteMatrix *view = (const GteMatrix *)view_words;
    PeFt3 *prims = s_prims[f - s.foe][g_ActiveDrawSlot & 1];
    GteMatrix eye;
    int i, r, c, n = 0, yaw, shade = 0x80;
    double cy, sy;

    pe_enemy_pose(pack, f->anim, (int)f->frame, s_posed);
    yaw = (a->rot_y + pack->yaw) & 0xFFF;
    cy = cos(yaw * 2 * PE_PI / 4096.0);
    sy = sin(yaw * 2 * PE_PI / 4096.0);
    {
        const double m[3][3] = {{cy, 0, sy}, {0, 1, 0}, {-sy, 0, cy}};
        const double t[3] = {a->pos_x / 65536.0, a->pos_y / 65536.0 - f->lift, a->pos_z / 65536.0};
        for (r = 0; r < 3; ++r) {   /* eye = view * world, in doubles (no GTE clamping) */
            double tr = view->t[r];
            for (c = 0; c < 3; ++c) {
                double e = 0;
                for (int k = 0; k < 3; ++k) e += view->m[r][k] * m[k][c];
                eye.m[r][c] = (s16)lrint(e);
                tr += view->m[r][c] * t[c] / 4096.0;
            }
            eye.t[r] = (s32)lrint(tr);
        }
    }
    SetRotMatrix(&eye);
    SetTransMatrix(&eye);
    SetGeomScreen((long)view_words[8]);
    if (!core) shade = 0;   /* freed: the game's fade is over */
    else if (core->deathAnimPhase >= 2) shade = 0x80 - core->deathFadeStep * 16;
    if (shade <= 0) { f->tris = 0; return; }

    {   /* screen points for the game's panels: feet, and the middle for damage numbers */
        GteShortVector feet = {0, 0, 0, 0}, mid = {0, (s16)(-resource->height * 6 / 10), 0, 0};
        int sxy = 0, p, flag;
        RotTransPers(&feet, &sxy, &p, &flag);
        a->render_object.projected_x = (s16)(sxy & 0xFFFF);
        a->render_object.projected_y = (s16)((unsigned)sxy >> 16);
        RotTransPers(&mid, &sxy, &p, &flag);
        a->render_object.projected_target_x = (s16)(sxy & 0xFFFF);
        a->render_object.projected_target_y = (s16)((unsigned)sxy >> 16);
    }

    for (i = 0; i < pack->triangles; ++i) {
        const PeEnemyTriangle *t = &pack->triangle[i];
        int sxy[3], z[3], p, flag, k, otz, base, umin, light;
        double e1[3], e2[3], nx, ny, nz, len;
        for (k = 0; k < 3; ++k) {
            const int16_t *v = s_posed[t->v[k]];
            GteShortVector sv = {v[0], v[1], v[2], 0};
            z[k] = (int)RotTransPers(&sv, &sxy[k], &p, &flag);
            if (z[k] <= 0) break;
        }
        if (k < 3) continue;
        otz = (z[0] + z[1] + z[2]) / 3;
        if (otz <= 0 || otz >= 0x1000) continue;
        for (k = 0; k < 3; ++k) {   /* light from above, turned with the yaw: a plain world light */
            e1[k] = s_posed[t->v[1]][k] - s_posed[t->v[0]][k];
            e2[k] = s_posed[t->v[2]][k] - s_posed[t->v[0]][k];
        }
        nx = e1[1] * e2[2] - e1[2] * e2[1];
        ny = e1[2] * e2[0] - e1[0] * e2[2];
        nz = e1[0] * e2[1] - e1[1] * e2[0];
        len = sqrt(nx * nx + ny * ny + nz * nz);
        light = len > 0 ? (int)(0.62 * shade + 0.38 * shade * fabs(-ny * 0.8 + (nx * sy + nz * cy) * 0.6) / len) : shade;
        if (light > 0xFF) light = 0xFF;
        umin = t->u[0] < t->u[1] ? t->u[0] : t->u[1];
        if (t->u[2] < umin) umin = t->u[2];
        base = umin / 128 * 128;
        {
            PeFt3 *q = &prims[n++];
            const int px = resource->texture.x + base / 2, dv = resource->texture.y & 255;
            const u32 tpage = 0x80u | ((unsigned)(resource->texture.y / 256) << 4) | (unsigned)(px >> 6);
            const u32 clut = ((unsigned)(resource->clut.y + t->palette) << 6) | ((unsigned)resource->clut.x >> 4);
            RenderNativeTag *entry = &D_800B0E38.ordering[g_ActiveDrawSlot][otz];
            q->tag.length = 7;
            q->color = (u32)light | (u32)light << 8 | (u32)light << 16 | 0x24u << 24;
            q->xy0 = (u32)sxy[0]; q->xy1 = (u32)sxy[1]; q->xy2 = (u32)sxy[2];
            q->uv0 = (u32)(t->u[0] - base) | (u32)(t->t[0] + dv) << 8 | clut << 16;
            q->uv1 = (u32)(t->u[1] - base) | (u32)(t->t[1] + dv) << 8 | tpage << 16;
            q->uv2 = (u32)(t->u[2] - base) | (u32)(t->t[2] + dv) << 8;
            q->tag.address = entry->address;
            entry->address = (uintptr_t)&q->tag;
        }
    }
    f->tris = n;
    f->drawn_frames++;
}

static int pe_on_render(void *ctx, void *user) {
    PeHookActorDraw *d = (PeHookActorDraw *)ctx;
    FieldActor *a = (FieldActor *)d->actor;
    PeFoe *f;
    (void)user;
    if (a && s_here_hidden_n && a != g_PlayerEntity) {
        if (pe_in_battle()) s_here_seen = 1;
        else if (s_here_seen) s_here_hidden_n = 0;         /* the battle is over: the room's own again */
        if (s_here_hidden_n && pe_in_battle()) for (int i = 0; i < s_here_hidden_n; ++i) if (s_here_hidden[i] == a) return 1;
    }
    if (!a || a->type_id != PE_FOE_TYPE) return 0;
    if (s.active && (f = pe_foe_of(a))) pe_draw(f, (const u32 *)d->view);
    return 1;   /* an imported enemy's actor never goes through the game's model draw */
}

/* The HP Aya loses to an imported enemy's blow, for the status line (before cheats change it). */
static int pe_on_hp_loss(void *ctx, void *user) {
    PeHookHpLoss *h = (PeHookHpLoss *)ctx;
    (void)user;
    if (s.active && s.last_blow && h->loss && h->source == 1) {
        s.last_blow->damage_done += *h->loss;
        s.last_blow = NULL;
    }
    return 0;
}

static int pe_on_enemy_damage(void *ctx, void *user) {
    PeHookEnemyDamage *hit = ctx;
    (void)user;
    if (!s.active || !s.foes || hit->target != s.foe[0].actor || *hit->damage <= 0) return 0;
    const EnemyCombatant *enemy = pe_core(&s.foe[0]);
    if (!enemy || enemy->hpAlive <= 0) return 0;
    if (s.launcher >= LAUNCHER_WAITING && s.launcher < LAUNCHER_FIRED && *hit->damage >= enemy->hpAlive)
        *hit->damage = enemy->hpAlive - 1;
    if (!s.canister_exposed || !g_PlayerEntity || hit->source != g_PlayerEntity->state || !hit->ordinary) return 0;
    const Combatant *aya = hit->source;
    const ItemDataRecord *weapon = pe_equipped(0);
    if (!weapon || weapon->kind < 1 || weapon->kind >= 8 || !aya->action ||
        aya->action->actionCode.actionId == 8) return 0;
    if (*hit->damage < enemy->hpAlive) *hit->damage = enemy->hpAlive;
    s.canister_exposed = 0;
    s.canister_detonated = 1;
    pe_toast("The gas canister exploded.");
    pe_mod_log(MOD_ID, "battle: Aya shot the Alligator's gas canister");
    return 0;
}

void pe_enemy_status_text(char *out, unsigned size) {
    const PeFoe *f = &s.foe[0];
    char seen[64] = "";
    EnemyCombatant *core;
    int i, dist = -1;
    if (!s.active) { snprintf(out, size, "%s", s.result[0] ? s.result : s_armed ? "ready for the next fight" : "none"); return; }
    if (!s.foes) { snprintf(out, size, "%s joining", s_roster[0]->pack.name); return; }
    const PeBattlePack *resource = f->resource;
    for (i = 0; i <= ROLE_SUMMON; ++i)
        if (f->seen & (1u << i)) snprintf(seen + strlen(seen), sizeof seen - strlen(seen), "%s%s", seen[0] ? "+" : "", k_role_names[i]);
    core = pe_core(f);
    if (core && g_PlayerEntity) dist = (int)pe_dist(f->actor, g_PlayerEntity);
    snprintf(out, size, "%s hp=%d/%d anim=%s lift=%d frames=%d tris=%d seen=%s hits=%d damage=%d dist=%d charge=%d foes=%d room=%d sent=%d sounds=%d vram=%d,%d rot=%d to_aya=%d",
             resource->pack.name, core ? core->hpAlive : 0, resource->pack.hp, f->role >= 0 ? k_role_names[f->role] : "-",
             f->lift, f->drawn_frames, f->tris, seen, f->hits, f->damage_done, dist, core ? (int)core->curHP : 0, s.foes,
             s.room_enemies, s.sent_away, f->sounds, resource->texture.x, resource->texture.y, f->actor ? f->actor->rot_y & 0xFFF : -1,
             f->actor && g_PlayerEntity ? pe_angle_to(f->actor, g_PlayerEntity) : -1);
}

void pe_enemy_battle_init(void) {
    pe_scan();
    pe_hook_add(PE_HOOK_BATTLE_UPDATE, pe_on_battle, NULL, PE_PRIORITY_EARLY, MOD_ID);
    pe_hook_add(PE_HOOK_FRAME, pe_on_frame, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_ACTOR_RENDER, pe_on_render, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_FIELD_BACKGROUND, pe_on_background, NULL, PE_PRIORITY_LATE, MOD_ID);
    pe_hook_add(PE_HOOK_HP_LOSS, pe_on_hp_loss, NULL, PE_PRIORITY_EARLY, MOD_ID);
    pe_hook_add(PE_HOOK_ENEMY_DAMAGE, pe_on_enemy_damage, NULL, PE_PRIORITY_LATE + 50, MOD_ID);
}
