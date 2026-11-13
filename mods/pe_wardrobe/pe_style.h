#ifndef PE_STYLE_H
#define PE_STYLE_H
#include <stdint.h>

/* Stable identifiers. Definitions, mappings and simulation have no game
 * globals. */
typedef enum {
  PE_STYLE_ORIGINAL,
  PE_STYLE_QUICKSTEP,
  PE_STYLE_BULWARK,
  PE_STYLE_STRIKER,
  PE_STYLE_CHANNELER,
  PE_STYLE_COUNT
} PeStyleId;
/* ---- Quickstep (Revision 6.4) -------------------------------------------
 * Every number and switch lives in PeQuickstepTuning (pe_style_catalog.c);
 * the rules live in pe_style_quickstep.c. Change a value there, not here. */
enum {
  PE_QS_TRIGGER_BREAKPOINT = 1u, /* weapon hit knocks an enemy past a % */
  PE_QS_TRIGGER_KILL = 2u,       /* weapon kill while other enemies remain */
  PE_QS_BREAKPOINTS = 4,         /* thresholds per enemy, 0 ends the list */
  PE_QS_TRACKED = 32             /* enemies/parts remembered per encounter */
};
typedef struct {
  unsigned triggers;                       /* PE_QS_TRIGGER_* bits */
  unsigned breakpoints[PE_QS_BREAKPOINTS]; /* % of max HP, e.g. {50} */
  unsigned refund_percent;                 /* of a full AT bar, once/action */
  unsigned burst_frames;  /* controllable battle frames (30 a second); 0 off */
  unsigned move_percent;  /* movement during the burst */
  unsigned exposed_percent;      /* Overextended: incoming damage in burst */
  unsigned exposed_grace_frames; /* burst frames before Overextended starts */
} PeQuickstepTuning;
typedef struct {
  const void *enemy; /* the enemy or part's own combatant */
  int max_hp;        /* a changed max HP (new boss phase) re-arms it */
  unsigned crossed;  /* bit i: breakpoints[i] already used */
} PeQuickstepMark;
typedef struct {
  unsigned pending;           /* this action earned its one refund */
  unsigned burst, burst_age;  /* frames left / frames elapsed */
  unsigned marks;
  PeQuickstepMark mark[PE_QS_TRACKED];
} PeQuickstepState;

typedef struct {
  PeStyleId id;
  const char *key, *name, *benefit, *limitation;
  unsigned implemented, charge_percent, damage_percent, point_blank_gap,
      pe_recovery_percent;
  /* Revision 5: Retaliate per-hit cap, Spellweave charge limit and
   * Overchannel heal bonus. */
  unsigned counter_percent, weave_charges, overchannel_percent;
  PeQuickstepTuning quickstep;
} PeStyleDef;
typedef struct {
  PeStyleId style;
  unsigned active, charge_remainder;
  uint64_t close_targets;
  unsigned gun_action, weapon_action;
  unsigned action_open, action_canceled, pending_damage;
  unsigned pe_remainder;
  PeQuickstepState qs;                   /* Quickstep */
  unsigned counter_store, counter_spent; /* Retaliate */
  unsigned weave_charges;              /* Spellweave */
} PeStyleSession;

const PeStyleDef *pe_style_definition(PeStyleId id);
PeStyleId pe_style_for_costume(const char *key);
void pe_style_begin(PeStyleSession *s, PeStyleId id);
void pe_style_end(PeStyleSession *s);
/* Quickstep. A weapon hit on one enemy/part: HP before and after, its current
 * max HP. Marks breakpoints and queues the action's one refund. */
void pe_quickstep_hit(PeStyleSession *s, const void *enemy, int before,
                      int after, int max_hp);
/* At action end, after the native AT reset: returns the new gauge and starts
 * the burst when a refund lands. continues = enemies remain and Aya lives. */
unsigned pe_quickstep_resolve(PeStyleSession *s, unsigned gauge, unsigned full,
                              int continues);
void pe_quickstep_action_begin(PeStyleSession *s); /* ends the burst */
void pe_quickstep_tick(PeStyleSession *s, int controllable); /* each frame */
int pe_quickstep_burst(const PeStyleSession *s);
int pe_quickstep_move(const PeStyleSession *s, int factor);
unsigned pe_quickstep_exposed_percent(const PeStyleSession *s); /* 100 = off */
/* HP pip: 1 once this enemy is at or below its first breakpoint. */
int pe_quickstep_spent(const PeStyleSession *s, const void *enemy, int hp,
                       int max_hp);
int pe_style_charge(PeStyleSession *s, int increment);
int pe_style_guard(const PeStyleSession *s, unsigned gauge, unsigned full,
                   int controllable);
int pe_style_damage(const PeStyleSession *s, int loss, int guarded);
void pe_style_absorb(PeStyleSession *s, int taken, int guarded);
int pe_style_counter(PeStyleSession *s, int damage, int ordinary);
void pe_style_counter_resolve(PeStyleSession *s);
enum { PE_STYLE_TARGETS = 64 };
int pe_style_point_blank(const PeStyleSession *s, int dx, int dz,
                         unsigned radius);
void pe_style_action_begin(PeStyleSession *s, int gun, int weapon);
void pe_style_target(PeStyleSession *s, unsigned index, int close);
int pe_style_gun_damage(const PeStyleSession *s, unsigned index, int damage,
                        int ordinary);
int pe_style_pe_recovery(PeStyleSession *s, int increment);
void pe_style_weapon_damage(PeStyleSession *s, int before, int after,
                            int ordinary);
void pe_style_action_cancel(PeStyleSession *s);
/* Spellweave: 1 when the completed action earned a charge. */
int pe_style_weave_resolve(PeStyleSession *s, int continues);
/* Spends every charge on a support PE power; returns the refunded share of its
 * cost in percent and sets *overchannel at the full charge count. */
unsigned pe_style_weave_cast(PeStyleSession *s, int *overchannel);

/* Wardrobe adapter, intentionally separate from the testable style engine. */
void pe_styles_register(const char *mod_id);
int pe_styles_enabled(void);
/* Shared F1/native-menu preference; rejects all in-battle changes. */
int pe_styles_set_enabled(int value);
void pe_styles_describe(const char *costume_key, char *out, unsigned size);
#endif
