/* Striker, Revision 6.4 (build spec 4.2). Pure rules: no game globals.
 *
 * Every gun action snapshots each possible recipient into one zone:
 *   point-blank  surface gap <= striker.point_blank_gap (same for every gun)
 *   falloff      native distance in the outer falloff_band_percent of the
 *                gun's native range, or beyond it
 *   native       everything between
 * Point-blank wins where the two overlap. Numbers live in PeStrikerTuning
 * (pe_style_catalog.c). */
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

int pe_striker_point_blank(const PeStyleSession *s, int dx, int dz,
                           unsigned radius) {
  uint64_t reach;
  int64_t x = dx, z = dz;
  if (!on(s) || !radius || radius > 1048576u)
    return 0;
  /* Horizontal surface gap: Aya's native footprint, never the costume's. */
  reach = (uint64_t)radius + PE_ST_AYA_FOOTPRINT + tuning(s)->point_blank_gap;
  return (uint64_t)(x * x) + (uint64_t)(z * z) <= reach * reach;
}

int pe_striker_zone(const PeStyleSession *s, int close, int dx, int dz,
                    unsigned range) {
  const PeStrikerTuning *t;
  int64_t x = dx, z = dz;
  uint64_t dist;
  if (!on(s))
    return PE_ST_NATIVE;
  if (close)
    return PE_ST_POINT_BLANK; /* wins over an overlapping falloff band */
  t = tuning(s);
  if (!t->falloff_band_percent || t->falloff_band_percent > 100 || !range)
    return PE_ST_NATIVE; /* unknown range: no falloff, never a guess */
  /* The native distance the gun's accuracy and range check use. */
  dist = isqrt((uint64_t)(x * x) + (uint64_t)(z * z));
  return dist * 100u >= (uint64_t)range * (100u - t->falloff_band_percent)
             ? PE_ST_FALLOFF
             : PE_ST_NATIVE;
}

void pe_striker_action_begin(PeStyleSession *s) {
  memset(s->st.zone, PE_ST_NATIVE, sizeof s->st.zone);
}
void pe_striker_target(PeStyleSession *s, unsigned index, int zone) {
  if (index < PE_STYLE_TARGETS && zone >= PE_ST_NATIVE && zone <= PE_ST_FALLOFF)
    s->st.zone[index] = (unsigned char)zone;
}
int pe_striker_target_zone(const PeStyleSession *s, unsigned index) {
  return index < PE_STYLE_TARGETS ? s->st.zone[index] : PE_ST_NATIVE;
}

unsigned pe_striker_percent(const PeStyleSession *s, int zone) {
  if (!on(s))
    return 100;
  if (zone == PE_ST_POINT_BLANK)
    return tuning(s)->point_blank_percent;
  if (zone == PE_ST_FALLOFF)
    return tuning(s)->falloff_percent;
  return 100;
}

int pe_striker_damage(const PeStyleSession *s, unsigned index, int damage,
                      int ordinary) {
  uint64_t scaled;
  unsigned percent;
  if (!on(s) || !s->gun_action || !ordinary || damage <= 0 ||
      index >= PE_STYLE_TARGETS)
    return damage;
  percent = pe_striker_percent(s, s->st.zone[index]);
  if (percent == 100)
    return damage;
  scaled = ((uint64_t)(unsigned)damage * percent + 50u) / 100u;
  if (!scaled)
    return 1; /* falloff never turns a hit into a miss */
  return scaled > INT_MAX ? INT_MAX : (int)scaled;
}
