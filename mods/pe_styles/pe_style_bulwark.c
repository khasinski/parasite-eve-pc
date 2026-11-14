/* Bulwark, Revision 6.4 (build spec 4.3), single-guard build. Pure rules: no
 * game globals.
 *
 * Phase, judged per resolved hit:
 *   guarded  AT below full while controllable: incoming x guard_percent;
 *            the HP actually lost goes to the counter store and Rally pool
 *   exposed  AT full while controllable: incoming x exposed_percent; feeds
 *            nothing
 *   off      actions, recovery, hit reactions, forced control: native
 * Counter: the next weapon action's ordinary hits each gain
 * min(store, ceil(hit x counter_percent)). Rally: each boosted hit heals its
 * bonus until rally_percent of the pool is won back. A heal (any HP gain we did
 * not cause) empties the pool but keeps the store. Numbers live in
 * PeBulwarkTuning (pe_style_catalog.c). Brace (two-level guard) comes later. */
#include "pe_style.h"
#include <limits.h>

static const PeBulwarkTuning *tuning(const PeStyleSession *s) {
  return &pe_style_definition(s->style)->bulwark;
}
static int on(const PeStyleSession *s) {
  return s->active && s->style == PE_STYLE_BULWARK;
}
static unsigned add_capped(unsigned a, unsigned b) {
  return b > (unsigned)INT_MAX - a ? (unsigned)INT_MAX : a + b;
}

int pe_bulwark_phase(const PeStyleSession *s, unsigned gauge, unsigned full,
                     int controllable) {
  if (!on(s) || !controllable)
    return PE_BW_OFF;
  return gauge < full ? PE_BW_GUARDED : PE_BW_EXPOSED;
}
unsigned pe_bulwark_percent(const PeStyleSession *s, int phase) {
  if (!on(s))
    return 100;
  if (phase == PE_BW_GUARDED)
    return tuning(s)->guard_percent;
  if (phase == PE_BW_EXPOSED)
    return tuning(s)->exposed_percent;
  return 100;
}

void pe_bulwark_absorb(PeStyleSession *s, int taken, int phase) {
  if (!on(s) || phase != PE_BW_GUARDED || taken <= 0)
    return;
  s->bw.store = add_capped(s->bw.store, (unsigned)taken);
  s->bw.pool = add_capped(s->bw.pool, (unsigned)taken);
}

int pe_bulwark_counter(PeStyleSession *s, int damage, int ordinary,
                       unsigned *bonus_out) {
  uint64_t cap, total;
  unsigned bonus;
  if (bonus_out)
    *bonus_out = 0;
  if (!on(s) || !s->weapon_action || !s->action_open || !ordinary ||
      damage <= 0 || !s->bw.store)
    return damage;
  cap = ((uint64_t)(unsigned)damage * tuning(s)->counter_percent + 99) / 100;
  bonus = cap < s->bw.store ? (unsigned)cap : s->bw.store;
  s->bw.store -= bonus;
  s->bw.spent = 1;
  if (bonus_out)
    *bonus_out = bonus;
  total = (uint64_t)(unsigned)damage + bonus;
  return total > INT_MAX ? INT_MAX : (int)total;
}

unsigned pe_bulwark_rally_left(const PeStyleSession *s) {
  uint64_t allowed;
  if (!on(s) || !s->bw.pool)
    return 0;
  allowed = ((uint64_t)s->bw.pool * tuning(s)->rally_percent + 99) / 100;
  return allowed > s->bw.regained ? (unsigned)(allowed - s->bw.regained) : 0;
}

unsigned pe_bulwark_rally(PeStyleSession *s, unsigned bonus, int hp,
                          int max_hp) {
  unsigned heal = pe_bulwark_rally_left(s);
  if (bonus < heal)
    heal = bonus;
  if (max_hp <= hp || hp < 0)
    heal = 0;
  else if (heal > (unsigned)(max_hp - hp))
    heal = (unsigned)(max_hp - hp);
  s->bw.regained = add_capped(s->bw.regained, heal);
  if (heal)
    s->bw.last_hp = hp + (int)heal; /* our own gain is not a heal */
  return heal;
}

int pe_bulwark_observe_hp(PeStyleSession *s, int hp) {
  int healed = 0;
  if (!on(s))
    return 0;
  if (s->bw.hp_seen && hp > s->bw.last_hp && s->bw.pool) {
    s->bw.pool = 0; /* healing first empties the Rally pool */
    s->bw.regained = 0;
    healed = 1;
  }
  s->bw.last_hp = hp;
  s->bw.hp_seen = 1;
  return healed;
}

void pe_bulwark_resolve(PeStyleSession *s) {
  if (s->bw.spent) { /* the action that landed a counter uses both up */
    s->bw.store = 0;
    s->bw.pool = 0;
    s->bw.regained = 0;
  }
  s->bw.spent = 0;
}
