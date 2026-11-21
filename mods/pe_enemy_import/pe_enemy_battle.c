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
#include "pe_enemy_pack.h"
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_services.h"
#include "pe1/battle.h"
#include "pe1/battle_entity_anim.h"
#include "pe1/battle_runtime.h"
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
#define PE_PACKS_MAX 16
#define PE_FOES_MAX 3
#define PE_TEX_X 320          /* the texture's first choice when free (Central Park leaves it free) */
#define PE_TEX_Y 256
#define PE_TEX_W 192          /* VRAM pixels: 384 8-bit texels */
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

typedef struct {
    RenderNativeTag tag;
    u32 color, xy0, uv0, xy1, uv1, xy2, uv2;
} PeFt3;

enum { ROLE_ATTACK = PE_ANIM_ROLES };

typedef struct {
    FieldActor *actor;
    unsigned serial;                 /* the actor's field_sfx_id: a reused actor has another */
    int anim, role, attack;          /* attack: index + 1 while attacking */
    double frame;
    int blow_done, stagger, last_hp, dead;
    int drawn_frames, tris, hits, damage_done, attacks_started;
    int event_frame, sounds;         /* last frame whose events ran; sounds played */
    unsigned seen;                   /* bit per role: idle walk hurt die, attack = 1 << 4 */
} PeFoe;

static struct { char folder[64], name[32]; } s_list[PE_PACKS_MAX];
static int s_listed;
static char s_dir[1024];

static PeEnemyPack s_pack;
static int s_loaded = -1;            /* list index of s_pack, -1 none */
static int s_armed;                  /* list index + 1 for the next battle, 0 none */
static int s_send_away, s_count = 1;

static struct {
    int active, uploaded, frames, open_frames, joined, sent_away, room_enemies;
    int away_later;                  /* joined during the opening: send the room's enemies away
                                      * once the clock runs */
    int tex_x, tex_y, clut_x, clut_y; /* where this battle's texture and palettes went */
    int height;                      /* the pack's standing height, field units */
    PeFoe foe[PE_FOES_MAX];
    int foes;
    PeFoe *last_blow;                /* the foe whose blow the game is taking now */
    int last_blow_frames;
    char result[96];
} s;

static u16 s_saved_tex[PE_TEX_W * PE_TEX_H], s_saved_clut[PE_CLUTS_MAX][256];
static int16_t s_posed[PE_ENEMY_MAX_VERTICES][3];
static PeFt3 s_prims[PE_FOES_MAX][2][PE_ENEMY_MAX_TRIANGLES];

static const char *const k_role_names[] = {"idle", "walk", "hurt", "die", "attack"};

/* ---- the list of packs ---- */

static int pe_cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void pe_scan(void) {
    char **list;
    int count, i;
    s_listed = 0;
    if (pe_mod_path(s_dir, sizeof s_dir, "pe_enemies") != 0) return;
    if (!(list = SDL_GlobDirectory(s_dir, "*", 0, &count))) return;
    qsort(list, (size_t)count, sizeof *list, pe_cmp);
    for (i = 0; i < count && s_listed < PE_PACKS_MAX; ++i) {
        char path[1300];
        SDL_PathInfo info;
        FILE *f;
        if (list[i][0] == '.' || strlen(list[i]) >= sizeof s_list[0].folder) continue;
        snprintf(path, sizeof path, "%s/%s/enemy.json", s_dir, list[i]);
        if (!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_FILE) continue;
        snprintf(s_list[s_listed].folder, sizeof s_list[0].folder, "%s", list[i]);
        snprintf(s_list[s_listed].name, sizeof s_list[0].name, "%s", list[i]);
        if ((f = fopen(path, "rb"))) {   /* the name, for the list; the full check is at load */
            char text[512], *p;
            size_t n = fread(text, 1, sizeof text - 1, f);
            text[n] = 0;
            if ((p = strstr(text, "\"name\"")) && (p = strchr(p + 6, '"'))) {
                char *e = strchr(++p, '"');
                if (e && e - p > 0 && e - p < (long)sizeof s_list[0].name)
                    snprintf(s_list[s_listed].name, sizeof s_list[0].name, "%.*s", (int)(e - p), p);
            }
            fclose(f);
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

static int pe_load(int index) {
    char dir[1200], err[200];
    if (s_loaded == index) return 1;
    if (s_loaded >= 0) pe_enemy_pack_free(&s_pack);
    s_loaded = -1;
    snprintf(dir, sizeof dir, "%s/%s", s_dir, s_list[index].folder);
    if (pe_enemy_pack_load(&s_pack, dir, err, sizeof err)) {
        pe_mod_log(MOD_ID, "enemy pack %s refused: %s", s_list[index].folder, err);
        pe_toast("Fights: that enemy pack can't be used (see pe_mods.log)");
        return 0;
    }
    if (s_pack.tex_w > 2 * PE_TEX_W || (s_pack.tex_w & 1) || s_pack.palettes > PE_CLUTS_MAX) {
        pe_mod_log(MOD_ID, "enemy pack %s refused: texture wider than %d or more than %d palettes",
                   s_list[index].folder, 2 * PE_TEX_W, PE_CLUTS_MAX);
        pe_enemy_pack_free(&s_pack);
        return 0;
    }
    s_loaded = index;
    pe_mod_log(MOD_ID, "enemy pack %s loaded: %s, %d bones, %d vertices, %d triangles, %d anims, %d attacks, hp %d",
               s_pack.folder, s_pack.name, s_pack.bones, s_pack.vertices, s_pack.triangles, s_pack.anims,
               s_pack.attacks, s_pack.hp);
    return 1;
}

void pe_enemy_battle_arm(int choice, int count, int send_away) {
    s_armed = 0;
    if (choice <= 0 || choice > s_listed) return;
    s_count = count < 1 ? 1 : count > PE_FOES_MAX ? PE_FOES_MAX : count;
    s_send_away = send_away;
    if (pe_load(choice - 1)) s_armed = choice;
}

int pe_enemy_battle_arm_pack(const char *pack, int count, int send_away) {
    s_armed = 0;
    if (s.active) return 0;                    /* not mid-battle: the list is the battle's */
    if (s_loaded >= 0) pe_enemy_pack_free(&s_pack), s_loaded = -1;   /* it may have been rebuilt */
    pe_scan();
    for (int i = 0; i < s_listed; i++)
        if (!strcmp(s_list[i].folder, pack)) {
            pe_enemy_battle_arm(i + 1, count, send_away);
            return s_armed != 0;
        }
    return 0;
}

void pe_enemy_battle_disarm(void) { s_armed = 0; }

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
    if (anim < 0) anim = s_pack.role[PE_ANIM_IDLE];
    if (f->role == role && f->anim == anim) return;
    f->role = role;
    f->anim = anim;
    f->frame = 0;
    f->event_frame = -1;
    f->seen |= 1u << role;
}

/* The anim's sound events from the last frame run up to `to` (wrapping when it loops), on the
 * audio service's effect voices (pe_assets): footsteps, swings. Silent without the service. */
static void pe_play_events(PeFoe *f, int to) {
    const PeEnemyAnim *an = &s_pack.anim[f->anim];
    static const PeAudioApi *audio;
    if (!an->event || to == f->event_frame) return;
    if (!audio) {
        const PeAudioApi *a = pe_service_get("audio", 1);
        if (a && a->struct_size >= sizeof *a && a->play_effect) audio = a;
    }
    for (int guard = 0, k = f->event_frame; guard < an->frames && k != to; guard++) {
        k = k + 1 >= an->frames ? 0 : k + 1;
        unsigned snd = an->event[k] & PE_ENEMY_EVENT_SOUND;
        if (snd && (int)snd < s_pack.sounds && s_pack.sound[snd].pcm) {
            f->sounds++;
            if (audio) audio->play_effect(s_pack.sound[snd].pcm, (uint32_t)s_pack.sound[snd].frames, (uint32_t)s_pack.sound[snd].rate, 1.0f);
        }
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

static int pe_reach(int attack) {
    return s_pack.attack[attack].range ? s_pack.attack[attack].range : s_pack.radius * 4;
}

/* Where Aya aims and where hits and numbers show: its middle, at its body size. */
static void pe_place_targets(FieldActor *a) {
    RenderObjectEntity *o = &a->render_object;
    const int x = a->pos_x >> 16, y = a->pos_y >> 16, z = a->pos_z >> 16;
    o->target_x = (s16)x;
    o->target_y = (s16)(y - s.height * 6 / 10);
    o->target_z = (s16)z;
    o->hit_cylinder.value0 = (s16)x;
    o->hit_cylinder.value1 = (s16)y;
    o->hit_cylinder.value2 = (s16)z;
    o->hit_cylinder.radius = (s16)s_pack.radius;
    o->hit_body.value0 = (s16)x;
    o->hit_body.value1 = (s16)(y - s.height / 2);
    o->hit_body.value2 = (s16)z;
    o->hit_body.radius = (s16)s_pack.radius;
}

static int pe_spawn(int x, int y, int z) {
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
    core->hpAlive = core->hpAliveCap = core->previousHP = s_pack.hp;
    core->defense = (u16)(s_pack.defense >= 0 ? s_pack.defense : 0);
    core->chargeStep = (u16)s_pack.charge;
    core->hpMirror = (u16)(s_pack.exp > 0xFFFF ? 0xFFFF : s_pack.exp);   /* the EXP it gives */
    core->deathAssetEnabled = 0;
    core->deathAnimFrame = 0;
    core->effect = &core->records[0];
    core->effect->state = 0;
    memset(f, 0, sizeof *f);
    f->actor = a;
    f->serial = a->field_sfx_id;
    f->role = -1;
    f->last_hp = s_pack.hp;
    pe_set_anim(f, PE_ANIM_IDLE, s_pack.role[PE_ANIM_IDLE]);
    pe_place_targets(a);
    s.foes++;
    pe_mod_log(MOD_ID, "battle: %s joins at %d,%d (enemy place bit %d, hp %d%s)", s_pack.name, x >> 16, z >> 16,
               core->slotBit, s_pack.hp, a->collision_face ? "" : ", no floor found");
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

static void pe_vram(int put_back) {
    RECT tex = {(short)s.tex_x, (short)s.tex_y, (short)(s_pack.tex_w / 2), (short)s_pack.tex_h};
    int i;
    if (!put_back) {   /* save what is there, then ours */
        StoreImage(&tex, s_saved_tex);
        for (i = 0; i < s_pack.palettes; ++i) {
            RECT c = {(short)s.clut_x, (short)(s.clut_y + i), 256, 1};
            StoreImage(&c, s_saved_clut[i]);
        }
        DrawSync(0);
        LoadImage(&tex, s_pack.pixels);
        for (i = 0; i < s_pack.palettes; ++i) {
            RECT c = {(short)s.clut_x, (short)(s.clut_y + i), 256, 1};
            LoadImage(&c, s_pack.palette + 256 * i);
        }
    } else {
        LoadImage(&tex, s_saved_tex);
        for (i = 0; i < s_pack.palettes; ++i) {
            RECT c = {(short)s.clut_x, (short)(s.clut_y + i), 256, 1};
            LoadImage(&c, s_saved_clut[i]);
        }
    }
    DrawSync(0);
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

static void pe_place(void) {
    const int tw = s_pack.tex_w / 2, th = s_pack.tex_h, rows = s_pack.palettes > 0 ? s_pack.palettes : 1;
    RECT all = {0, 0, 1024, 512};
    long long best, cost;
    for (int i = 0; i < 2; ++i) {
        pe_mark_rect(&g_RenderDrawEnvArray[i].clip);
        pe_mark_rect(&g_RenderDispEnvArray[i].disp);
    }
    StoreImage(&all, s_vram);
    DrawSync(0);
    /* the texture: a texture page column (x a multiple of 64), its lines inside one page row */
    s.tex_x = PE_TEX_X;
    s.tex_y = PE_TEX_Y;
    best = pe_block_cost(s.tex_x, s.tex_y, tw, th);
    for (int page = 0; page < 2; ++page)
        for (int dy = 0; dy + th <= 256; dy += 16)
            for (int x = 0; x + tw <= 1024; x += 64)
                if ((cost = pe_block_cost(x, page * 256 + dy, tw, th)) < best) {
                    best = cost;
                    s.tex_x = x;
                    s.tex_y = page * 256 + dy;
                }
    pe_mod_log(MOD_ID, "battle: %s texture at %d,%d (%lld cells read by the room, %lld pixels not black; %d frames looked at)",
               s_pack.name, s.tex_x, s.tex_y, best / 1000000LL, best % 1000000LL, s_used_frames);
    /* the palettes: 256 wide (x a multiple of 256), clear of the texture */
    s.clut_x = PE_CLUT_X;
    s.clut_y = PE_CLUT_Y;
    best = pe_overlap(s.clut_x, s.clut_y, 256, rows, s.tex_x, s.tex_y, tw, th) ? -1
                                                                               : pe_block_cost(s.clut_x, s.clut_y, 256, rows);
    for (int x = 0; x < 1024; x += 256)
        for (int y = 0; y + rows <= 512; ++y)
            if (!pe_overlap(x, y, 256, rows, s.tex_x, s.tex_y, tw, th) &&
                ((cost = pe_block_cost(x, y, 256, rows)) < best || best < 0)) {
                best = cost;
                s.clut_x = x;
                s.clut_y = y;
            }
    pe_mod_log(MOD_ID, "battle: %s palettes at %d,%d (%lld cells read by the room, %lld pixels not black)",
               s_pack.name, s.clut_x, s.clut_y, best / 1000000LL, best % 1000000LL);
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
    for (i = 0; i < s_count; ++i) {
        /* more than one: side by side across Aya's view of the spot; beside the room's enemy
         * when it stays */
        const double ang = atan2((x - aya->pos_x) / 65536.0, (z - aya->pos_z) / 65536.0) + PE_PI / 2;
        double side = (i - (s_count - 1) / 2.0) * (s_pack.radius * 3.0);
        if ((!s_send_away || s.away_later) && first) side += s_pack.radius * 3.0;
        if (!pe_spawn(x + (int)(sin(ang) * side * 65536.0), y, z + (int)(cos(ang) * side * 65536.0))) break;
    }
    if (!s.foes) {
        pe_mod_log(MOD_ID, "battle: no free actor or enemy place for %s", s_pack.name);
        return;
    }
    pe_place();
    pe_vram(0);
    s.uploaded = 1;
}

static void pe_begin(void) {
    int top = 0;
    memset(&s, 0, sizeof s);
    memset(s_used, 0, sizeof s_used);
    s_used_frames = 0;
    s.active = 1;
    for (int i = 0; i < s_pack.vertices; ++i)   /* standing height: the highest rest vertex (-y up) */
        if (-s_pack.vertex[i].y > top) top = -s_pack.vertex[i].y;
    s.height = top > 50 ? top : 500;
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
    if (s.uploaded) pe_vram(1);
    snprintf(s.result, sizeof s.result, "%s %s (beaten %d of %d)", s_pack.name, why, beaten, s.foes);
    pe_mod_log(MOD_ID, "battle over: %s", s.result);
    s.active = 0;
}

static void pe_foe_think(PeFoe *f) {
    FieldActor *a = f->actor, *aya = g_PlayerEntity;
    EnemyCombatant *core = pe_core(f);
    const PeEnemyAnim *cur = &s_pack.anim[f->anim];
    int hp, loop, done, want, diff;
    double dist;

    a->flags |= 0x800000;   /* the game's anim step leaves it alone (it has no game anim) */
    a->anim_step = 0;
    if (!core) return;
    hp = core->hpAlive;
    if (g_GameStateFlags & 0x100) { pe_place_targets(a); return; }   /* enemies stopped */

    /* the frame: idle and walk loop, the rest hold their last frame */
    loop = f->role == PE_ANIM_IDLE || f->role == PE_ANIM_WALK;
    f->frame += s_pack.fps / 30.0;
    if (f->frame >= cur->frames) f->frame = loop ? fmod(f->frame, cur->frames) : cur->frames - 1;
    done = f->frame >= cur->frames - 1;
    pe_play_events(f, (int)f->frame);

    if (hp <= 0) {   /* death: its anim plays, then the game fades and frees it */
        if (!f->dead) {
            const int die = s_pack.role[PE_ANIM_DIE];
            const int frames = die >= 0 ? (int)(s_pack.anim[die].frames * 30.0 / s_pack.fps) : 1;
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
        a->anim.fixed = done ? core->deathAnimFrame << 16 : (int)(f->frame * 30.0 / s_pack.fps) << 16;
        pe_place_targets(a);
        return;
    }

    /* hurt: a shot stops it for its hurt anim, unless it is mid-attack */
    if (hp < f->last_hp && !f->attack && s_pack.stagger && s_pack.role[PE_ANIM_HURT] >= 0) {
        f->role = -1;
        pe_set_anim(f, PE_ANIM_HURT, s_pack.role[PE_ANIM_HURT]);
        f->stagger = 1;
        done = 0;
    }
    f->last_hp = hp;
    if (f->stagger) {
        if (!done) { pe_place_targets(a); return; }
        f->stagger = 0;
    }

    dist = aya ? pe_dist(a, aya) : 1e9;
    want = aya ? pe_angle_to(a, aya) : a->rot_y;

    if (f->attack) {
        const PeEnemyAttack *atk = &s_pack.attack[f->attack - 1];
        if (!f->blow_done && f->frame >= atk->hit) {
            f->blow_done = 1;
            diff = pe_turn_diff(a->rot_y, want);
            if (aya && aya->state && dist <= pe_reach(f->attack - 1) * 1.15 && abs(diff) < 0x300) {
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
        if (!done) { pe_place_targets(a); return; }
        f->attack = 0;
        core->coreFlags &= 0x7FFFFFFFu;   /* a blow the game did not take is over */
    }

    /* turn toward Aya */
    diff = pe_turn_diff(a->rot_y, want);
    a->rot_y = (short)((a->rot_y + (diff > PE_TURN_STEP ? PE_TURN_STEP : diff < -PE_TURN_STEP ? -PE_TURN_STEP : diff)) & 0xFFF);

    /* attack when its time bar is full and Aya is in reach of one of its attacks */
    if (s_pack.attacks && core->curHP >= PE_CHARGE_FULL && abs(diff) < 0x200) {
        for (int k = 0; k < s_pack.attacks; ++k) {
            const int c = (f->attacks_started + k) % s_pack.attacks;
            if (dist <= pe_reach(c)) {
                f->attack = c + 1;
                f->blow_done = 0;
                f->attacks_started++;
                core->curHP = 0;   /* its turn spent */
                f->role = -1;
                pe_set_anim(f, ROLE_ATTACK, s_pack.attack[c].anim);
                pe_place_targets(a);
                return;
            }
        }
    }

    {   /* walk to Aya, stopping a little inside its shortest reach */
        int reach = 0;
        for (int k = 0; k < s_pack.attacks; ++k)
            if (!reach || pe_reach(k) < reach) reach = pe_reach(k);
        if (!reach) reach = s_pack.radius * 3;
        const int moving = f->role == PE_ANIM_WALK;   /* starts a little further out than it stops */
        /* (at least a unit to go: steps shrink as it closes in, and would never quite end) */
        if (aya && dist > reach * (moving ? 0.8 : 0.9) && dist - reach * 0.8 >= 1.0 && s_pack.speed > 0 &&
            abs(diff) < 0x400) {
            const double left = dist - reach * 0.8;
            const double step = s_pack.speed < left ? s_pack.speed : left;
            const double ang = a->rot_y * 2 * PE_PI / 4096.0;
            a->base_x = a->pos_x;
            a->base_y = a->pos_y;
            a->base_z = a->pos_z;
            a->pos_x += (int)(sin(ang) * step * 65536.0);
            a->pos_z += (int)(cos(ang) * step * 65536.0);
            pe_set_anim(f, PE_ANIM_WALK, s_pack.role[PE_ANIM_WALK]);
        } else {
            pe_set_anim(f, PE_ANIM_IDLE, s_pack.role[PE_ANIM_IDLE]);
        }
    }
    pe_place_targets(a);
}

static int pe_on_battle(void *ctx, void *user) {
    (void)ctx; (void)user;
    if (!s.active && s_armed && s_loaded == s_armed - 1) {
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
            pe_mod_log(MOD_ID, "battle: the opening is still on; %s joins%s", s_pack.name,
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
        if (s_loaded >= 0) pe_enemy_pack_free(&s_pack), s_loaded = -1;   /* list indexes may move */
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
    EnemyCombatant *core = (EnemyCombatant *)a->state;
    const GteMatrix *view = (const GteMatrix *)view_words;
    PeFt3 *prims = s_prims[f - s.foe][g_ActiveDrawSlot & 1];
    GteMatrix eye;
    int i, r, c, n = 0, yaw, shade = 0x80;
    double cy, sy;

    pe_enemy_pose(&s_pack, f->anim, (int)f->frame, s_posed);
    yaw = (a->rot_y + s_pack.yaw) & 0xFFF;
    cy = cos(yaw * 2 * PE_PI / 4096.0);
    sy = sin(yaw * 2 * PE_PI / 4096.0);
    {
        const double m[3][3] = {{cy, 0, sy}, {0, 1, 0}, {-sy, 0, cy}};
        const double t[3] = {a->pos_x / 65536.0, a->pos_y / 65536.0, a->pos_z / 65536.0};
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
        GteShortVector feet = {0, 0, 0, 0}, mid = {0, (s16)(-s.height * 6 / 10), 0, 0};
        int sxy = 0, p, flag;
        RotTransPers(&feet, &sxy, &p, &flag);
        a->render_object.projected_x = (s16)(sxy & 0xFFFF);
        a->render_object.projected_y = (s16)((unsigned)sxy >> 16);
        RotTransPers(&mid, &sxy, &p, &flag);
        a->render_object.projected_target_x = (s16)(sxy & 0xFFFF);
        a->render_object.projected_target_y = (s16)((unsigned)sxy >> 16);
    }

    for (i = 0; i < s_pack.triangles; ++i) {
        const PeEnemyTriangle *t = &s_pack.triangle[i];
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
            const int px = s.tex_x + base / 2, dv = s.tex_y & 255;
            const u32 tpage = 0x80u | ((unsigned)(s.tex_y / 256) << 4) | (unsigned)(px >> 6);
            const u32 clut = ((unsigned)(s.clut_y + t->palette) << 6) | ((unsigned)s.clut_x >> 4);
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

void pe_enemy_status_text(char *out, unsigned size) {
    const PeFoe *f = &s.foe[0];
    char seen[64] = "";
    EnemyCombatant *core;
    int i, dist = -1;
    if (!s.active) { snprintf(out, size, "%s", s.result[0] ? s.result : s_armed ? "ready for the next fight" : "none"); return; }
    if (!s.foes) { snprintf(out, size, "%s joining", s_pack.name); return; }
    for (i = 0; i <= ROLE_ATTACK; ++i)
        if (f->seen & (1u << i)) snprintf(seen + strlen(seen), sizeof seen - strlen(seen), "%s%s", seen[0] ? "+" : "", k_role_names[i]);
    core = pe_core(f);
    if (core && g_PlayerEntity) dist = (int)pe_dist(f->actor, g_PlayerEntity);
    snprintf(out, size, "%s hp=%d/%d anim=%s frames=%d tris=%d seen=%s hits=%d damage=%d dist=%d charge=%d foes=%d room=%d sent=%d sounds=%d vram=%d,%d rot=%d to_aya=%d",
             s_pack.name, core ? core->hpAlive : 0, s_pack.hp, f->role >= 0 ? k_role_names[f->role] : "-",
             f->drawn_frames, f->tris, seen, f->hits, f->damage_done, dist, core ? (int)core->curHP : 0, s.foes,
             s.room_enemies, s.sent_away, f->sounds, s.tex_x, s.tex_y, f->actor ? f->actor->rot_y & 0xFFF : -1,
             f->actor && g_PlayerEntity ? pe_angle_to(f->actor, g_PlayerEntity) : -1);
}

void pe_enemy_battle_init(void) {
    pe_scan();
    pe_hook_add(PE_HOOK_BATTLE_UPDATE, pe_on_battle, NULL, PE_PRIORITY_EARLY, MOD_ID);
    pe_hook_add(PE_HOOK_FRAME, pe_on_frame, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_ACTOR_RENDER, pe_on_render, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_FIELD_BACKGROUND, pe_on_background, NULL, PE_PRIORITY_LATE, MOD_ID);
    pe_hook_add(PE_HOOK_HP_LOSS, pe_on_hp_loss, NULL, PE_PRIORITY_EARLY, MOD_ID);
}
