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
void pe_style_kill(PeStyleSession *s, int before, int after, int weapon) {
  if (s->active && s->style == PE_STYLE_QUICKSTEP && weapon && before > 0 &&
      after <= 0)
    s->pending_kill = 1;
}
unsigned pe_style_resolve(PeStyleSession *s, unsigned gauge, unsigned full,
                          int continues) {
  unsigned pending = s->pending_kill;
  uint64_t total;
  s->pending_kill =
      0; /* resolve once, including final kill / duplicate callbacks */
  if (!s->active || s->style != PE_STYLE_QUICKSTEP || !pending || !continues)
    return gauge;
  total = (uint64_t)gauge +
          (uint64_t)full * pe_style_definition(s->style)->refund_percent / 100;
  return total > full ? full : (unsigned)total;
}
int pe_style_charge(PeStyleSession *s, int increment) {
  uint64_t scaled;
  if (!s->active || increment <= 0)
    return increment;
  scaled = (uint64_t)(unsigned)increment *
               pe_style_definition(s->style)->charge_percent +
           s->charge_remainder;
  s->charge_remainder = (unsigned)(scaled % 100);
  return scaled / 100 > INT_MAX ? INT_MAX : (int)(scaled / 100);
}
int pe_style_guard(const PeStyleSession *s, unsigned gauge, unsigned full,
                   int controllable) {
  return s->active && s->style == PE_STYLE_BULWARK && gauge < full &&
         controllable;
}
int pe_style_damage(const PeStyleSession *s, int loss, int guarded) {
  uint64_t scaled;
  if (!guarded || !s->active || s->style != PE_STYLE_BULWARK || loss <= 0)
    return loss;
  scaled = ((uint64_t)(unsigned)loss *
                pe_style_definition(s->style)->damage_percent +
            50) /
           100;
  return scaled ? (int)scaled : 1;
}

int pe_style_point_blank(const PeStyleSession *s, int dx, int dz,
                         unsigned radius) {
  uint64_t reach;
  int64_t x = dx, z = dz;
  if (!s->active || s->style != PE_STYLE_STRIKER || !radius ||
      radius > 1048576u)
    return 0;
  /* Horizontal surface gap: Aya's native footprint, never the costume's size.
   */
  reach =
      (uint64_t)radius + 63u + pe_style_definition(s->style)->point_blank_gap;
  return (uint64_t)(x * x) + (uint64_t)(z * z) <= reach * reach;
}
void pe_style_action_begin(PeStyleSession *s, int gun) {
  s->pending_kill = 0;
  s->close_targets = 0;
  s->gun_action = gun != 0;
  s->action_open = 1;
  s->action_canceled = 0;
  s->pending_damage = 0;
}
void pe_style_target(PeStyleSession *s, unsigned index, int close) {
  if (index < PE_STYLE_TARGETS && close)
    s->close_targets |= UINT64_C(1) << index;
}
int pe_style_gun_damage(const PeStyleSession *s, unsigned index, int damage,
                        int ordinary) {
  uint64_t scaled;
  if (!s->active || s->style != PE_STYLE_STRIKER || !s->gun_action ||
      !ordinary || damage <= 0 || index >= PE_STYLE_TARGETS ||
      !(s->close_targets & (UINT64_C(1) << index)))
    return damage;
  scaled = ((uint64_t)(unsigned)damage *
                pe_style_definition(s->style)->damage_percent +
            50u) /
           100u;
  return scaled > INT_MAX ? INT_MAX : (int)scaled;
}

int pe_style_pe_recovery(PeStyleSession *s, int increment) {
  if (!s->active || s->style != PE_STYLE_CHANNELER || increment <= 0)
    return increment;
  uint64_t scaled = (uint64_t)(unsigned)increment *
                        pe_style_definition(s->style)->pe_recovery_percent +
                    s->pe_remainder;
  s->pe_remainder = (unsigned)(scaled % 100);
  return scaled / 100 > INT_MAX ? INT_MAX : (int)(scaled / 100);
}
void pe_style_weapon_damage(PeStyleSession *s, int before, int after,
                            int ordinary) {
  if (s->active && s->style == PE_STYLE_CHANNELER && s->action_open &&
      !s->action_canceled && ordinary && before > 0 && after < before)
    s->pending_damage = 1;
}
void pe_style_action_cancel(PeStyleSession *s) {
  if (s->action_open) {
    s->action_canceled = 1;
    s->pending_damage = 0;
  }
}
unsigned pe_style_pe_resolve(PeStyleSession *s, unsigned current,
                             unsigned maximum, int continues) {
  unsigned earned = s->action_open && !s->action_canceled && s->pending_damage;
  s->pending_damage = 0;
  s->action_open = 0;
  if (!s->active || s->style != PE_STYLE_CHANNELER || !earned || !continues ||
      !maximum)
    return current;
  if (current >= maximum) {
    s->pe_refund_remainder = 0;
    s->pe_remainder = 0;
    return maximum;
  }
  uint64_t raw =
      (uint64_t)maximum * pe_style_definition(s->style)->pe_refund_percent +
      s->pe_refund_remainder;
  uint64_t total = (uint64_t)current + raw / 100;
  s->pe_refund_remainder = total >= maximum ? 0 : (unsigned)(raw % 100);
  if (total >= maximum)
    s->pe_remainder = 0;
  return total >= maximum ? maximum : (unsigned)total;
}
