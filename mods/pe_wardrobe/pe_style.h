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
typedef struct {
  PeStyleId id;
  const char *key, *name, *benefit, *limitation;
  unsigned implemented, refund_percent, charge_percent, damage_percent,
      point_blank_gap, pe_recovery_percent;
  /* Revision 5: Quickstep movement burst, Retaliate per-hit cap, Spellweave
   * charge limit and Overchannel heal bonus. */
  unsigned move_percent, counter_percent, weave_charges, overchannel_percent;
} PeStyleDef;
typedef struct {
  PeStyleId style;
  unsigned active, pending_kill, charge_remainder;
  uint64_t close_targets;
  unsigned gun_action, weapon_action;
  unsigned action_open, action_canceled, pending_damage;
  unsigned pe_remainder;
  unsigned move_boost;                 /* Quickstep, until the next action */
  unsigned counter_store, counter_spent; /* Retaliate */
  unsigned weave_charges;              /* Spellweave */
} PeStyleSession;

const PeStyleDef *pe_style_definition(PeStyleId id);
PeStyleId pe_style_for_costume(const char *key);
void pe_style_begin(PeStyleSession *s, PeStyleId id);
void pe_style_end(PeStyleSession *s);
void pe_style_kill(PeStyleSession *s, int before, int after, int weapon);
unsigned pe_style_resolve(PeStyleSession *s, unsigned gauge, unsigned full,
                          int continues);
int pe_style_move(const PeStyleSession *s, int factor);
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
