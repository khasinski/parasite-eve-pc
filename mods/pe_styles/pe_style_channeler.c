/* Channeler, Revision 6.4: "Weave attacks into spells".
 *
 * Pro (Spellweave): a completed weapon action that damaged a living enemy adds
 * one charge, up to max_charges. The next support PE power spends them all:
 * discount[charges] % off, and at max charges Heal 1-3 also Overchannel.
 * Con (Backlash, warning first): at max charges the first eligible incoming
 * hit cracks the weave; a second one before casting breaks one charge. Hits
 * within backlash_group_frames of each other count as one enemy attack. No PE
 * is ever lost.
 *
 * Pure logic, no game globals. Every number is PeChannelerTuning in
 * pe_style_catalog.c:
 *   - max_charges and discount[] (index = charges held);
 *   - overchannel_percent 0 turns Overchannel off;
 *   - backlash 0 turns the con off;
 *   - backlash_group_frames sets how close hits must be to count as one;
 *   - pe_recovery_percent scales passive regen (100 = native). */
#include "pe_style.h"
#include <limits.h>

static const PeChannelerTuning *tuning(void) {
  return &pe_style_definition(PE_STYLE_CHANNELER)->channeler;
}
static int on(const PeStyleSession *s) {
  return s->active && s->style == PE_STYLE_CHANNELER;
}
static unsigned max_charges(void) {
  unsigned m = tuning()->max_charges;
  return m > PE_CH_MAX_CHARGES ? PE_CH_MAX_CHARGES : m;
}

int pe_channeler_recovery(PeStyleSession *s, int increment) {
  uint64_t scaled;
  if (!on(s) || increment <= 0)
    return increment;
  scaled = (uint64_t)(unsigned)increment * tuning()->pe_recovery_percent +
           s->pe_remainder;
  s->pe_remainder = (unsigned)(scaled % 100);
  scaled /= 100;
  return scaled > INT_MAX ? INT_MAX : (int)scaled;
}

int pe_channeler_resolve(PeStyleSession *s, int continues) {
  unsigned earned = s->action_open && !s->action_canceled && s->pending_damage;
  s->pending_damage = 0;
  s->action_open = 0;
  if (!on(s) || !earned || !continues || s->ch.charges >= max_charges())
    return 0;
  s->ch.charges++;
  return 1;
}

unsigned pe_channeler_discount(const PeStyleSession *s) {
  unsigned c;
  if (!on(s) || !s->ch.charges)
    return 0;
  c = s->ch.charges > PE_CH_MAX_CHARGES ? PE_CH_MAX_CHARGES : s->ch.charges;
  return tuning()->discount[c] > 100 ? 100 : tuning()->discount[c];
}

unsigned pe_channeler_cast(PeStyleSession *s, int *overchannel) {
  unsigned percent = pe_channeler_discount(s);
  if (overchannel)
    *overchannel = 0;
  if (!on(s) || !s->ch.charges)
    return 0;
  if (overchannel && s->ch.charges >= max_charges() &&
      tuning()->overchannel_percent)
    *overchannel = 1;
  s->ch.charges = 0;
  s->ch.cracked = 0; /* casting clears the crack */
  return percent;
}

int pe_channeler_full(const PeStyleSession *s) {
  return on(s) && max_charges() && s->ch.charges >= max_charges();
}

int pe_channeler_hit(PeStyleSession *s) {
  const PeChannelerTuning *t = tuning();
  if (!pe_channeler_full(s) || !t->backlash)
    return PE_CH_NONE;
  if (s->ch.group) {
    s->ch.group = t->backlash_group_frames; /* same attack, still going */
    return PE_CH_NONE;
  }
  s->ch.group = t->backlash_group_frames;
  if (!s->ch.cracked) {
    s->ch.cracked = 1; /* warning: nothing lost */
    return PE_CH_CRACK;
  }
  s->ch.cracked = 0;
  s->ch.charges--; /* never any PE */
  return PE_CH_BREAK;
}

void pe_channeler_tick(PeStyleSession *s) {
  if (on(s) && s->ch.group)
    s->ch.group--;
}
