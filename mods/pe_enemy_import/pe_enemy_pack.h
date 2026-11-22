/* Imported enemy packs: a folder the player's own tool writes (tools in the enemy importer), read
 * here with no game state, so the loader and the posing are unit-tested on their own.
 *
 *   <prefs>/pe_enemies/<folder>/enemy.json   name, hp, exp, fps, yaw, anims {role: file}, attacks
 *                               model.bin    "PEEM": bones, vertices bound to bones, triangles
 *                               texture.bin  "PEET": 8-bit texture and its palettes (PSX colours)
 *                               anims/<move>.bin "PEEA": one move, a pose per game frame
 *                               sounds/<nn>.wav  what the moves play (optional)
 *
 * All little-endian. model.bin (version 1):
 *   char[4] "PEEM", u16 version, bones, vertices, triangles, tex width, tex height, palettes, 0
 *   bones      8 bytes: s16 parent (-1 root, else an earlier bone), s16 x, y, z from the parent
 *   vertices   8 bytes: s16 x, y, z in the rest pose's model frame (floor y 0, -y up), u8 bone,
 *              u8 second bone (0xFF none: else the vertex follows both, half and half)
 *   triangles 16 bytes: u16 a, b, c; u16 u0, u1, u2 (texels); u8 v0, v1, v2, palette
 * texture.bin: char[4] "PEET", u16 version 1, width, height, palettes, 256 u16 colours per
 *   palette (PSX 15-bit, 0 = see-through), then width * height u8 colour indices.
 * enemy.json may add "behavior" {speed, charge, radius, stagger, altitude} and per attack "power" and
 *   "hit" (see PeEnemyPack, PeEnemyAttack); the imported enemy's own fight logic reads them.
 * anims (version 1): char[4] "PEEA", u16 version, bones, frames, 0; per frame s16 root x, y, z,
 *   then s16 x, y, z angles per bone (4096 = a full turn; rotation = X * Y * Z, parent first).
 *   Version 2 adds a u16 event per frame after the frames: low byte = the sound it plays (an
 *   index into enemy.json's "sounds", 0 none), 0x100 = a blow can land on this frame.
 * enemy.json may list "sounds": [file, ...]: PCM WAV files, mono, 16-bit, 8000-48000 Hz, at most
 *   10 seconds each; an event's sound number k plays sounds[k]. */
#ifndef PE_ENEMY_PACK_H
#define PE_ENEMY_PACK_H

#include <stdint.h>

#define PE_ENEMY_MAX_BONES 32
#define PE_ENEMY_MAX_VERTICES 4096
#define PE_ENEMY_MAX_TRIANGLES 4096
#define PE_ENEMY_MAX_ANIMS 16
#define PE_ENEMY_MAX_ATTACKS 4
#define PE_ENEMY_NO_BONE 0xFF
#define PE_ENEMY_MAX_SOUNDS 32
enum { PE_ENEMY_EVENT_SOUND = 0x00FF, PE_ENEMY_EVENT_HIT = 0x0100 };

enum { PE_ANIM_IDLE, PE_ANIM_WALK, PE_ANIM_HURT, PE_ANIM_DIE, PE_ANIM_ROLES };

typedef struct { int16_t parent, x, y, z; } PeEnemyBone;
typedef struct { int16_t x, y, z; uint8_t bone, bone2; } PeEnemyVertex;
typedef struct { uint16_t v[3], u[3]; uint8_t t[3], palette; } PeEnemyTriangle;

typedef struct {
    char name[24];
    int frames;
    int16_t *data;          /* frames * (3 + 3 * bones) */
    uint16_t *event;        /* frames (version 2), else NULL */
} PeEnemyAnim;

typedef struct { int rate, frames; int16_t *pcm; } PeEnemySound;

typedef struct {
    int anim;               /* index into anims */
    int damage, range;      /* (old packs) HP taken from Aya; reach in PE field units (0: any) */
    int power;              /* attack power for the game's hit rules (Aya's defense and armor
                             * come off it); "power", else "damage" */
    int hit;                /* frame of the anim the blow lands on; "hit", else half way */
} PeEnemyAttack;

typedef struct {
    char folder[64], name[32];
    char source[128], next_form[128];  /* catalog asset IDs, optional in installed packs */
    int hp, exp, defense, fps, yaw;
    /* "behavior": speed (field units per game frame while walking), charge (its time bar: added
     * per frame, full at 9000), radius (body size for Aya's aim and reach), stagger (1: shots
     * stop it for its hurt anim, unless it is mid-attack), altitude (flight height above the floor) */
    int speed, charge, radius, stagger, altitude;
    int bones, vertices, triangles;
    PeEnemyBone bone[PE_ENEMY_MAX_BONES];
    PeEnemyVertex *vertex;
    PeEnemyTriangle *triangle;
    int tex_w, tex_h, palettes;
    uint16_t *palette;      /* palettes * 256 */
    uint8_t *pixels;        /* tex_w * tex_h */
    int anims;
    PeEnemyAnim anim[PE_ENEMY_MAX_ANIMS];
    int role[PE_ANIM_ROLES];   /* anim index per role, -1 none */
    int attacks;
    PeEnemyAttack attack[PE_ENEMY_MAX_ATTACKS];
    int sounds;
    int hurt_sound, death_sound;  /* optional cue when the corresponding animation is absent */
    PeEnemySound sound[PE_ENEMY_MAX_SOUNDS];
} PeEnemyPack;

/* A PCM WAV (mono, 16-bit) from memory; 0, or -1. s->pcm is malloc'd. */
int pe_enemy_wav_parse(const uint8_t *data, unsigned size, PeEnemySound *s);

/* Loads <dir>/enemy.json and the files it names. 0 on success; else -1 and why in err. */
int pe_enemy_pack_load(PeEnemyPack *pack, const char *dir, char *err, unsigned err_size);
void pe_enemy_pack_free(PeEnemyPack *pack);
int pe_enemy_pack_anim(const PeEnemyPack *pack, const char *name);
/* Animation roles missing from the built pack. Stationary enemies do not need a walk. */
int pe_enemy_pack_coverage(const PeEnemyPack *pack, int stationary, char *out, unsigned size);
enum { PE_AUDIO_ATTACK = 1, PE_AUDIO_HURT = 2, PE_AUDIO_DEATH = 4, PE_AUDIO_OTHER = 8 };
unsigned pe_enemy_pack_audio_roles(const PeEnemyPack *pack);

/* Every vertex posed for frame `frame` of anim `anim` (clamped to its last frame), into out
 * (pack->vertices entries, model frame: floor y 0). Returns 0, -1 for a bad anim. */
int pe_enemy_pose(const PeEnemyPack *pack, int anim, int frame, int16_t (*out)[3]);

#endif
