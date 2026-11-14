/* Striker (build spec 4.2, falloff reworked 2026-11-13 after playtest). Pure
 * rules: no game globals.
 *
 * Every gun action snapshots each possible recipient's damage percent:
 *   close range  its body reaches the ring (surface gap <= point_blank_gap,
 *                the same for every gun): point_blank_percent (125)
 *   falloff      past the ring, damage drops steadily with the enemy's edge
 *                distance, from 100% at the ring to 100 - falloff_max_penalty
 *                (75%) at the gun's native range, and stays there beyond it
 * Numbers live in PeStrikerTuning (pe_style_catalog.c). */
#include "pe_style.h"
#include <limits.h>
#include <string.h>

static const PeStrikerTuning *tuning(const PeStyleSession *s) {
  return &pe_style_definition(s->style)->striker;
}
static int on(const PeStyleSession *s) {
  return s->active && s->style == PE_STYLE_STRIKER;
}
/* Floor square root, as the native Math_IntSqrt rounds. */
static uint64_t isqrt(uint64_t v) {
  uint64_t r = 0, bit = UINT64_C(1) << 62;
  while (bit > v)
    bit >>= 2;
  while (bit) {
    if (v >= r + bit) {
      v -= r + bit;
      r = (r >> 1) + bit;
    } else {
      r >>= 1;
    }
    bit >>= 2;
  }
  return r;
}

/* Aya's native footprint plus the close-range gap: an enemy whose body
 * reaches this circle around her centre is close range. The aiming ring is
 * drawn at this radius. */
unsigned pe_striker_ring_radius(const PeStyleSession *s) {
  return on(s) ? PE_ST_AYA_FOOTPRINT + tuning(s)->point_blank_gap : 0u;
}

int pe_striker_point_blank(const PeStyleSession *s, int dx, int dz,
                           unsigned radius) {
  uint64_t reach;
  int64_t x = dx, z = dz;
  if (!on(s) || !radius || radius > 1048576u)
    return 0;
  /* Horizontal surface gap: Aya's native footprint, never the costume's. */
  reach = (uint64_t)radius + pe_striker_ring_radius(s);
  return (uint64_t)(x * x) + (uint64_t)(z * z) <= reach * reach;
}

unsigned pe_striker_shot_percent(const PeStyleSession *s, int close,
                                 unsigned radius, int dx, int dz,
                                 unsigned range) {
  const PeStrikerTuning *t;
  int64_t x = dx, z = dz;
  uint64_t dist, edge, start;
  unsigned max_penalty;
  if (!on(s))
    return 100;
  t = tuning(s);
  if (close)
    return t->point_blank_percent;
  max_penalty = t->falloff_max_penalty > 99 ? 99 : t->falloff_max_penalty;
  if (!max_penalty || !range)
    return 100; /* unknown range: no falloff, never a guess */
  /* The native distance the gun's accuracy and range check use. */
  dist = isqrt((uint64_t)(x * x) + (uint64_t)(z * z));
  if (dist >= range)
    return 100 - max_penalty; /* beyond the dome */
  start = pe_striker_ring_radius(s);
  if (range <= start)
    return 100; /* the whole dome is inside the ring: no room to fall off */
  edge = dist > radius ? dist - radius : 0; /* the enemy's near edge */
  if (edge <= start)
    return 100;
  return 100 - (unsigned)((uint64_t)max_penalty * (edge - start) /
                          (range - start));
}

void pe_striker_action_begin(PeStyleSession *s) {
  memset(s->st.percent, 0, sizeof s->st.percent); /* 0 = not recorded */
}
void pe_striker_target(PeStyleSession *s, unsigned index, unsigned percent) {
  if (index < PE_STYLE_TARGETS && percent && percent <= 255)
    s->st.percent[index] = (unsigned char)percent;
}
unsigned pe_striker_target_percent(const PeStyleSession *s, unsigned index) {
  unsigned p = index < PE_STYLE_TARGETS ? s->st.percent[index] : 0;
  return p ? p : 100;
}

int pe_striker_damage(const PeStyleSession *s, unsigned index, int damage,
                      int ordinary) {
  uint64_t scaled;
  unsigned percent;
  if (!on(s) || !s->gun_action || !ordinary || damage <= 0 ||
      index >= PE_STYLE_TARGETS)
    return damage;
  percent = pe_striker_target_percent(s, index);
  if (percent == 100)
    return damage;
  scaled = ((uint64_t)(unsigned)damage * percent + 50u) / 100u;
  if (!scaled)
    return 1; /* falloff never turns a hit into a miss */
  return scaled > INT_MAX ? INT_MAX : (int)scaled;
}
