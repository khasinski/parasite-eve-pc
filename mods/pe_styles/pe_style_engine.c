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

/* ---- Incoming damage ------------------------------------------------------
 * Bulwark's guard or Exposed (pe_style_bulwark.c), or Quickstep's
 * Overextended. The styles never share an encounter. */
int pe_style_damage(const PeStyleSession *s, int loss, int phase) {
  uint64_t scaled;
  unsigned percent;
  if (loss <= 0)
    return loss;
  percent = is(s, PE_STYLE_BULWARK) ? pe_bulwark_percent(s, phase)
                                    : pe_quickstep_exposed_percent(s);
  if (percent == 100)
    return loss;
  scaled = ((uint64_t)(unsigned)loss * percent + 50) / 100;
  return scaled ? saturate(scaled) : 1;
}

/* ---- Shared action bookkeeping (Striker rules: pe_style_striker.c) ------ */
void pe_style_action_begin(PeStyleSession *s, int gun, int weapon) {
  pe_quickstep_action_begin(s);
  pe_striker_action_begin(s);
  s->gun_action = gun != 0;
  s->weapon_action = weapon != 0 || gun != 0;
  s->action_open = 1;
  s->action_canceled = 0;
  s->pending_damage = 0;
  s->bw.spent = 0;
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
