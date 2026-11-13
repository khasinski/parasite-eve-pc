#include "pe_style.h"
#include <limits.h>
#include <string.h>

void pe_style_end(PeStyleSession *s) { memset(s, 0, sizeof *s); }
void pe_style_begin(PeStyleSession *s, PeStyleId id) {
  const PeStyleDef *d = pe_style_definition(id);
  pe_style_end(s);
  s->style = d->implemented ? d->id : PE_STYLE_ORIGINAL;
  s->active = 1;
}
static int is(const PeStyleSession *s, PeStyleId id) {
  return s->active && s->style == id;
}
static int saturate(uint64_t v) { return v > INT_MAX ? INT_MAX : (int)v; }

/* Quickstep lives in pe_style_quickstep.c. */

/* ---- Shared AT scaling (native 100% for every current style) ------------ */
int pe_style_charge(PeStyleSession *s, int increment) {
  uint64_t scaled;
  if (!s->active || increment <= 0)
    return increment;
  scaled = (uint64_t)(unsigned)increment *
               pe_style_definition(s->style)->charge_percent +
           s->charge_remainder;
  s->charge_remainder = (unsigned)(scaled % 100);
  return saturate(scaled / 100);
}

/* ---- Bulwark: guard and Retaliate --------------------------------------- */
int pe_style_guard(const PeStyleSession *s, unsigned gauge, unsigned full,
                   int controllable) {
  return is(s, PE_STYLE_BULWARK) && gauge < full && controllable;
}
/* Every incoming-damage multiplier, once per eligible hit: Bulwark's guard or
 * Quickstep's Overextended (the two never share an encounter). */
int pe_style_damage(const PeStyleSession *s, int loss, int guarded) {
  uint64_t scaled;
  unsigned percent = 100;
  if (loss <= 0)
    return loss;
  if (guarded && is(s, PE_STYLE_BULWARK))
    percent = pe_style_definition(s->style)->damage_percent;
  else
    percent = pe_quickstep_exposed_percent(s);
  if (percent == 100)
    return loss;
  scaled = ((uint64_t)(unsigned)loss * percent + 50) / 100;
  return scaled ? saturate(scaled) : 1;
}
void pe_style_absorb(PeStyleSession *s, int taken, int guarded) {
  uint64_t total;
  if (!guarded || !is(s, PE_STYLE_BULWARK) || taken <= 0)
    return;
  total = (uint64_t)s->counter_store + (unsigned)taken;
  s->counter_store = total > INT_MAX ? INT_MAX : (unsigned)total;
}
int pe_style_counter(PeStyleSession *s, int damage, int ordinary) {
  uint64_t cap;
  unsigned bonus;
  if (!is(s, PE_STYLE_BULWARK) || !s->weapon_action || !s->action_open ||
      !ordinary || damage <= 0 || !s->counter_store)
    return damage;
  cap = ((uint64_t)(unsigned)damage *
             pe_style_definition(s->style)->counter_percent +
         99) /
        100;
  bonus = cap < s->counter_store ? (unsigned)cap : s->counter_store;
  s->counter_store -= bonus;
  s->counter_spent = 1;
  return saturate((uint64_t)(unsigned)damage + bonus);
}
void pe_style_counter_resolve(PeStyleSession *s) {
  if (s->counter_spent)
    s->counter_store = 0; /* one counter per weapon action that landed */
  s->counter_spent = 0;
}

/* ---- Striker ------------------------------------------------------------ */
int pe_style_point_blank(const PeStyleSession *s, int dx, int dz,
                         unsigned radius) {
  uint64_t reach;
  int64_t x = dx, z = dz;
  if (!is(s, PE_STYLE_STRIKER) || !radius || radius > 1048576u)
    return 0;
  /* Horizontal surface gap: Aya's native footprint, never the costume's size.
   */
  reach =
      (uint64_t)radius + 63u + pe_style_definition(s->style)->point_blank_gap;
  return (uint64_t)(x * x) + (uint64_t)(z * z) <= reach * reach;
}
void pe_style_action_begin(PeStyleSession *s, int gun, int weapon) {
  pe_quickstep_action_begin(s);
  s->close_targets = 0;
  s->gun_action = gun != 0;
  s->weapon_action = weapon != 0 || gun != 0;
  s->action_open = 1;
  s->action_canceled = 0;
  s->pending_damage = 0;
  s->counter_spent = 0;
}
void pe_style_target(PeStyleSession *s, unsigned index, int close) {
  if (index < PE_STYLE_TARGETS && close)
    s->close_targets |= UINT64_C(1) << index;
}
int pe_style_gun_damage(const PeStyleSession *s, unsigned index, int damage,
                        int ordinary) {
  uint64_t scaled;
  if (!is(s, PE_STYLE_STRIKER) || !s->gun_action || !ordinary || damage <= 0 ||
      index >= PE_STYLE_TARGETS ||
      !(s->close_targets & (UINT64_C(1) << index)))
    return damage;
  scaled = ((uint64_t)(unsigned)damage *
                pe_style_definition(s->style)->damage_percent +
            50u) /
           100u;
  return saturate(scaled);
}

/* ---- Channeler: action bookkeeping (rules in pe_style_channeler.c) ------ */
void pe_style_weapon_damage(PeStyleSession *s, int before, int after,
                            int ordinary) {
  if (is(s, PE_STYLE_CHANNELER) && s->action_open && !s->action_canceled &&
      ordinary && before > 0 && after < before)
    s->pending_damage = 1;
}
void pe_style_action_cancel(PeStyleSession *s) {
  if (s->action_open) {
    s->action_canceled = 1;
    s->pending_damage = 0;
  }
}
