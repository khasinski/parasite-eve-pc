#ifndef PE_IMP_GUESS_H
#define PE_IMP_GUESS_H
/* An enemy profile guessed from a model and its moves (pe_imp_guess.c): any imported monster can
 * become an enemy in the host game without a hand-written profile (game-neutral: a PE2 host reuses it). Each move is measured (does it loop,
 * how much moves, how far it reaches out, where the body ends up), and idle, walk, hurt, die and up
 * to two attacks are picked from that. No game state: unit-tested on made-up and real moves. */
#include <stddef.h>

#include "pe_imp.h"
#include "pe_imp_enemy.h"

/* What one move does, in the model's own height (1.0 = how tall it stands). */
typedef struct {
    int frames;
    double loop;                              /* how far the last frame's pose is from the first */
    double motion;                            /* how much the body moves per frame */
    double start, low, end;                   /* height on the first frame, its lowest, the last */
    double reach[4];                          /* how far it gets out past its first frame:
                                               * +x, -x, +z, -z (yaw 3072, 1024, 0, 2048) */
    double travel;                            /* the root's way from first to last frame */
    int hits, sounds;                         /* frames marked by the source */
} PeImpMoveLook;

/* Every move of `a` posed on `m`: look[i] for move i (frames 0: an empty move). `height`: how tall
 * the model stands (source units). 0, or -1 with a reason. */
int pe_imp_enemy_look(const PeImpModel *m, const PeImpAnimation *a, PeImpMoveLook *look, double *height,
                  char *why, size_t n);

/* A profile with room for its strings: guessed, or read from a profile file. */
typedef struct {
    PeImpEnemyProfile p;
    char name[48], moves[PE_IMP_ID_MAX], sounds[PE_IMP_ID_MAX];
    char anim[PE_IMP_ENEMY_ANIMS][24];
    char next_form[PE_IMP_ID_MAX];
} PeImpEnemyGuess;

/* The profile for a model named `name` whose moves are `moves_id` (the local ID): idle, walk, hurt,
 * die and up to two attacks ("attack", "attack2") picked from what the moves do; hp, power, size
 * and speed from how big it is. `yaw` turns the model to face Aya; `scale` (per mille) takes its
 * units to PE field units (0: made 500 tall). Roles it finds no move for are left out (only idle
 * is needed). 0, or -1 with a reason (every move empty). */
int pe_imp_enemy_guess(const PeImpModel *m, const PeImpAnimation *a, const char *name, const char *moves_id,
                   int yaw, int scale, PeImpEnemyGuess *g, char *why, size_t n);
int pe_imp_enemy_guess_body(const PeImpModel *m, const PeImpAnimation *a, const char *name, const char *moves_id,
                            int yaw, int scale, PeImpEnemyBody body, PeImpEnemyGuess *g, char *why, size_t n);

/* The source game's own strength for `asset_id` (pe_imp_enemy_strength), if it registered one, over
 * a guessed profile: hp (and exp from it) and, when given, every attack's power. 1 applied, 0 none. */
int pe_imp_enemy_known_strength(const char *asset_id, PeImpEnemyProfile *p);

/* A profile as a file the player can change (profile.json in the enemy's pack folder): its moves by
 * number, sizes, attacks and behaviour; `note` is written first, for whoever opens it. */
int pe_imp_enemy_profile_save(const PeImpEnemyProfile *p, const char *note, const char *path, char *why, size_t n);
/* Read back (a changed one too): 0, or -1 with a reason (missing, malformed, no idle, an attack
 * naming an anim the file doesn't list). */
int pe_imp_enemy_profile_load(const char *path, PeImpEnemyGuess *g, char *why, size_t n);

#endif
