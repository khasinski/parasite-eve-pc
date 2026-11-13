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

/* ---- Channeler (Revision 6.4) -------------------------------------------
 * Numbers and switches: PeChannelerTuning (pe_style_catalog.c). Rules:
 * pe_style_channeler.c. */
enum { PE_CH_MAX_CHARGES = 4 };
typedef struct {
  unsigned max_charges;                         /* 2 */
  unsigned discount[PE_CH_MAX_CHARGES + 1];     /* % off by charges held */
  unsigned overchannel_percent;                 /* extra Heal 1-3 at max */
  unsigned backlash;                            /* 0 turns the con off */
  unsigned backlash_group_frames; /* hits this close count as one attack */
  unsigned pe_recovery_percent;   /* passive PE regen, 100 = native */
} PeChannelerTuning;
typedef struct {
  unsigned charges;
  unsigned cracked;     /* Backlash warning given at full charge */
  unsigned group;       /* frames left in the current enemy attack */
} PeChannelerState;
enum { PE_CH_NONE, PE_CH_CRACK, PE_CH_BREAK }; /* pe_channeler_hit result */

/* ---- Striker (Revision 6.4) ---------------------------------------------
 * Numbers and switches: PeStrikerTuning (pe_style_catalog.c). Rules:
 * pe_style_striker.c. */
enum {
  PE_STYLE_TARGETS = 64,     /* recipients snapshotted per gun action */
  PE_ST_AYA_FOOTPRINT = 63   /* Aya's native radius, never the costume's */
};
enum { PE_ST_NATIVE, PE_ST_POINT_BLANK, PE_ST_FALLOFF }; /* zones */
typedef struct {
  unsigned point_blank_gap;      /* surface gap in world units, every gun */
  unsigned point_blank_percent;  /* gun hit damage at point-blank */
  unsigned falloff_band_percent; /* outer % of native gun range; 0 = off */
  unsigned falloff_percent;      /* gun hit damage in the falloff band */
} PeStrikerTuning;
typedef struct {
  unsigned char zone[PE_STYLE_TARGETS]; /* PE_ST_* per snapshotted recipient */
} PeStrikerState;

/* ---- Bulwark (Revision 6.4, single-guard build) -------------------------
 * Numbers and switches: PeBulwarkTuning (pe_style_catalog.c). Rules:
 * pe_style_bulwark.c. */
enum { PE_BW_OFF, PE_BW_GUARDED, PE_BW_EXPOSED }; /* phase per resolved hit */
typedef struct {
  unsigned guard_percent;   /* incoming damage while guarded (below full AT) */
  unsigned counter_percent; /* per-hit counter bonus cap, % of the hit */
  unsigned rally_percent;   /* share of the Rally pool won back; 0 = off */
  unsigned exposed_percent; /* incoming damage on a full bar; 100 = off */
} PeBulwarkTuning;
typedef struct {
  unsigned store, spent;   /* counter */
  unsigned pool, regained; /* Rally */
  int last_hp;             /* heal detection */
  unsigned hp_seen;
} PeBulwarkState;

typedef struct {
  PeStyleId id;
  const char *key, *name, *benefit, *limitation;
  unsigned implemented, charge_percent;
  PeQuickstepTuning quickstep;
  PeStrikerTuning striker;
  PeBulwarkTuning bulwark;
  PeChannelerTuning channeler;
} PeStyleDef;
typedef struct {
  PeStyleId style;
  unsigned active, charge_remainder;
  PeStrikerState st;                     /* Striker */
  unsigned gun_action, weapon_action;
  unsigned action_open, action_canceled, pending_damage;
  unsigned pe_remainder;
  PeQuickstepState qs;                   /* Quickstep */
  PeBulwarkState bw;                     /* Bulwark */
  PeChannelerState ch;                   /* Channeler */
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
/* Every incoming-damage multiplier, once per eligible hit. phase is Bulwark's
 * (PE_BW_*); other styles pass PE_BW_OFF. */
int pe_style_damage(const PeStyleSession *s, int loss, int phase);
/* Bulwark. */
int pe_bulwark_phase(const PeStyleSession *s, unsigned gauge, unsigned full,
                     int controllable);
unsigned pe_bulwark_percent(const PeStyleSession *s, int phase);
/* HP actually lost from a hit; only guarded hits feed store and pool. */
void pe_bulwark_absorb(PeStyleSession *s, int taken, int phase);
/* An outgoing hit of the open weapon action: returns the boosted damage and
 * the bonus in *bonus_out (for Rally). */
int pe_bulwark_counter(PeStyleSession *s, int damage, int ordinary,
                       unsigned *bonus_out);
/* HP to restore for a counter bonus, capped by the pool and max HP. */
unsigned pe_bulwark_rally(PeStyleSession *s, unsigned bonus, int hp,
                          int max_hp);
unsigned pe_bulwark_rally_left(const PeStyleSession *s); /* the "+N" */
/* Each frame: an HP gain we did not cause empties the pool. 1 if it did. */
int pe_bulwark_observe_hp(PeStyleSession *s, int hp);
void pe_bulwark_resolve(PeStyleSession *s); /* weapon action end */
void pe_style_action_begin(PeStyleSession *s, int gun, int weapon);
/* Striker. Point-blank: horizontal offset to the recipient's hit cylinder
 * centre and its scaled radius. */
int pe_striker_point_blank(const PeStyleSession *s, int dx, int dz,
                           unsigned radius);
/* Zone for one recipient: close from pe_striker_point_blank, dx/dz the native
 * targeting offset, range the gun's native range (0 = unknown, no falloff). */
int pe_striker_zone(const PeStyleSession *s, int close, int dx, int dz,
                    unsigned range);
void pe_striker_action_begin(PeStyleSession *s); /* clears the snapshot */
void pe_striker_target(PeStyleSession *s, unsigned index, int zone);
int pe_striker_target_zone(const PeStyleSession *s, unsigned index);
unsigned pe_striker_percent(const PeStyleSession *s, int zone);
/* An ordinary gun hit on snapshot recipient index. */
int pe_striker_damage(const PeStyleSession *s, unsigned index, int damage,
                      int ordinary);
void pe_style_weapon_damage(PeStyleSession *s, int before, int after,
                            int ordinary);
void pe_style_action_cancel(PeStyleSession *s);
/* Channeler. Passive PE regen scaling (native at 100%). */
int pe_channeler_recovery(PeStyleSession *s, int increment);
/* 1 when the completed action earned a charge. */
int pe_channeler_resolve(PeStyleSession *s, int continues);
/* % off a support power right now (menu cost, affordability). */
unsigned pe_channeler_discount(const PeStyleSession *s);
/* Spends every charge on a support PE power; returns the refunded share of its
 * cost in percent and sets *overchannel at max charges. Clears the crack. */
unsigned pe_channeler_cast(PeStyleSession *s, int *overchannel);
/* Backlash: an eligible incoming hit. Returns PE_CH_CRACK / _BREAK / _NONE. */
int pe_channeler_hit(PeStyleSession *s);
void pe_channeler_tick(PeStyleSession *s); /* every battle frame */
int pe_channeler_full(const PeStyleSession *s);

/* Wardrobe adapter, intentionally separate from the testable style engine. */
void pe_styles_register(const char *mod_id);
int pe_styles_enabled(void);
/* Shared F1/native-menu preference; rejects all in-battle changes. */
int pe_styles_set_enabled(int value);
void pe_styles_describe(const char *costume_key, char *out, unsigned size);
#endif
