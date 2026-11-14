/* Quickstep, Revision 6.4: "Break through, keep moving".
 *
 * Pro: a weapon action that knocks an enemy past a breakpoint for the first
 * time, or kills one while others remain, refunds part of the AT bar once and
 * starts a movement burst. Con (Overextended): incoming damage is raised while
 * the burst lasts.
 *
 * Pure logic, no game globals. Every number and switch is in
 * PeQuickstepTuning (pe_style_catalog.c), so tuning never touches this file:
 *   - triggers: drop PE_QS_TRIGGER_KILL or _BREAKPOINT to test either alone;
 *   - breakpoints: {50} today, {50, 25} if boss fights feel flat;
 *   - burst_frames 0 turns off both the burst and Overextended;
 *   - exposed_percent 100 turns off Overextended alone;
 *   - exposed_grace_frames delays Overextended inside the burst. */
#include "pe_style.h"
#include <limits.h>
#include <stddef.h>

static const PeQuickstepTuning *tuning(void) {
  return &pe_style_definition(PE_STYLE_QUICKSTEP)->quickstep;
}
static int on(const PeStyleSession *s) {
  return s->active && s->style == PE_STYLE_QUICKSTEP;
}
static int threshold(int max_hp, unsigned percent) {
  return (int)((int64_t)max_hp * percent / 100);
}
static const PeQuickstepMark *find(const PeStyleSession *s,
                                   const void *enemy) {
  for (unsigned i = 0; i < s->qs.marks; i++)
    if (s->qs.mark[i].enemy == enemy)
      return &s->qs.mark[i];
  return NULL;
}
static PeQuickstepMark *track(PeStyleSession *s, const void *enemy,
                              int max_hp) {
  PeQuickstepMark *m = (PeQuickstepMark *)find(s, enemy);
  if (!m) {
    if (s->qs.marks >= PE_QS_TRACKED)
      return NULL; /* full: no breakpoint refunds for further enemies */
    m = &s->qs.mark[s->qs.marks++];
    m->enemy = enemy;
    m->max_hp = max_hp;
    m->crossed = 0;
  } else if (m->max_hp != max_hp) {
    m->max_hp = max_hp; /* the game rewrote max HP: a fresh boss phase */
    m->crossed = 0;
  }
  return m;
}

void pe_quickstep_hit(PeStyleSession *s, const void *enemy, int before,
                      int after, int max_hp) {
  const PeQuickstepTuning *t = tuning();
  PeQuickstepMark *m;
  if (!on(s) || !enemy || before <= 0 || after >= before)
    return;
  if ((t->triggers & PE_QS_TRIGGER_KILL) && after <= 0)
    s->qs.pending = 1; /* "while others remain" is judged at resolve */
  if (!(t->triggers & PE_QS_TRIGGER_BREAKPOINT) || max_hp <= 0 ||
      !(m = track(s, enemy, max_hp)))
    return;
  for (unsigned i = 0; i < PE_QS_BREAKPOINTS && t->breakpoints[i]; i++) {
    int line = threshold(max_hp, t->breakpoints[i]);
    if (m->crossed & (1u << i) || after > line)
      continue;
    m->crossed |= 1u << i; /* healing never re-arms it */
    if (before > line)
      s->qs.pending = 1; /* this hit crossed it */
  }
}

unsigned pe_quickstep_resolve(PeStyleSession *s, unsigned gauge, unsigned full,
                              int continues) {
  const PeQuickstepTuning *t = tuning();
  unsigned pending = s->qs.pending;
  uint64_t total;
  s->qs.pending = 0; /* resolve once, including duplicate end callbacks */
  if (!on(s) || !pending || !continues)
    return gauge;
  s->qs.burst = t->burst_frames;
  s->qs.burst_age = 0;
  total = (uint64_t)gauge + (uint64_t)full * t->refund_percent / 100;
  return total > full ? full : (unsigned)total;
}

void pe_quickstep_action_begin(PeStyleSession *s) {
  s->qs.pending = 0;
  s->qs.burst = 0; /* the burst ends when Aya's next action begins */
  s->qs.burst_age = 0;
}

void pe_quickstep_tick(PeStyleSession *s, int controllable) {
  if (!on(s) || !s->qs.burst || !controllable)
    return; /* menus, pauses, actions and hit reactions don't count */
  s->qs.burst--;
  s->qs.burst_age++;
}

int pe_quickstep_burst(const PeStyleSession *s) {
  return on(s) && s->qs.burst > 0;
}

int pe_quickstep_move(const PeStyleSession *s, int factor) {
  uint64_t scaled;
  if (!pe_quickstep_burst(s) || factor <= 0)
    return factor;
  scaled = (uint64_t)(unsigned)factor * tuning()->move_percent / 100;
  return scaled > INT_MAX ? INT_MAX : (int)scaled;
}

unsigned pe_quickstep_exposed_percent(const PeStyleSession *s) {
  const PeQuickstepTuning *t = tuning();
  if (!pe_quickstep_burst(s) || s->qs.burst_age < t->exposed_grace_frames ||
      !t->exposed_percent)
    return 100;
  return t->exposed_percent;
}

int pe_quickstep_spent(const PeStyleSession *s, const void *enemy, int hp,
                       int max_hp) {
  const PeQuickstepTuning *t = tuning();
  const PeQuickstepMark *m;
  if (!on(s) || !(t->triggers & PE_QS_TRIGGER_BREAKPOINT) ||
      !t->breakpoints[0] || max_hp <= 0)
    return 0;
  m = find(s, enemy);
  if (m && m->max_hp == max_hp && (m->crossed & 1u))
    return 1;
  return hp <= threshold(max_hp, t->breakpoints[0]);
}

int pe_quickstep_available(const PeStyleSession *s, const void *enemy, int hp,
                           int max_hp) {
  const PeQuickstepTuning *t = tuning();
  if (!on(s) || !(t->triggers & PE_QS_TRIGGER_BREAKPOINT) ||
      !t->breakpoints[0] || max_hp <= 0 || hp <= 0)
    return 0;
  return !pe_quickstep_spent(s, enemy, hp, max_hp);
}
