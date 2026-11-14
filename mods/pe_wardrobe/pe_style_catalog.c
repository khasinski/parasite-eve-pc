#include "pe_style.h"
#include <string.h>

static const PeStyleDef definitions[PE_STYLE_COUNT] = {
    {.id = PE_STYLE_ORIGINAL,
     .key = "original",
     .name = "Original",
     .benefit = "Native combat",
     .limitation = "No costume ability",
     .implemented = 1,
     .charge_percent = 100},
    {.id = PE_STYLE_QUICKSTEP,
     .key = "quickstep",
     .name = "Quickstep",
     .benefit = "Break an enemy below half or kill: +20% AT, 3 s speed",
     .limitation = "While rushing you take 20% more damage",
     .implemented = 1,
     .charge_percent = 100,
     /* Build spec 4.1 and 5. Tune here, one value at a time. */
     .quickstep = {.triggers = PE_QS_TRIGGER_BREAKPOINT | PE_QS_TRIGGER_KILL,
                   .breakpoints = {50},
                   .refund_percent = 20,
                   .burst_frames = 90, /* 3 s at 30 battle frames a second */
                   .move_percent = 125,
                   .exposed_percent = 120,
                   .exposed_grace_frames = 0}},
    {.id = PE_STYLE_BULWARK,
     .key = "bulwark",
     .name = "Bulwark",
     .benefit = "Guard below full AT; hit back to win HP back",
     .limitation = "Sitting on a full AT bar: +15% damage taken",
     .implemented = 1,
     .charge_percent = 100,
     /* Build spec 4.3 and 5, single-guard build. Tune here, one at a time. */
     .bulwark = {.guard_percent = 75,
                 .counter_percent = 50,
                 .rally_percent = 50, /* 0 turns Rally off */
                 .exposed_percent = 115}}, /* 100 turns Exposed off */
    {.id = PE_STYLE_STRIKER,
     .key = "striker",
     .name = "Striker",
     .benefit = "Point-blank gun hits deal +25% damage",
     .limitation = "Shots from your gun's outer range deal -20%",
     .implemented = 1,
     .charge_percent = 100,
     /* Build spec 4.2 and 5. Tune here, one value at a time. */
     /* Gap 200 (spec said 96): 96 was nearly touching in playtest, too
      * small a ring to aim for. Still calibrating (spec 8). */
     .striker = {.point_blank_gap = 200,
                 .point_blank_percent = 125,
                 .falloff_band_percent = 30, /* 0 turns falloff off */
                 .falloff_percent = 80}},
    {.id = PE_STYLE_CHANNELER,
     .key = "channeler",
     .name = "Channeler",
     .benefit = "Hits charge your next support PE: 1 = half, 2 = free",
     .limitation = "At full charge, a hit cracks it; a 2nd breaks one",
     .implemented = 1,
     .charge_percent = 100,
     /* Build spec 4.4 and 5. Tune here, one value at a time. */
     .channeler = {.max_charges = 2,
                   .discount = {0, 50, 100},
                   .overchannel_percent = 25,
                   .backlash = 1,
                   .backlash_group_frames = 30, /* 1 s: one enemy combo */
                   .pe_recovery_percent = 100}}};
typedef struct {
  const char *key;
  PeStyleId style;
} Mapping;
static const Mapping costumes[] = {{"pe1/t_shirt", PE_STYLE_QUICKSTEP},
                                   {"pe1/date", PE_STYLE_QUICKSTEP},
                                   {"pe1/eddie", PE_STYLE_QUICKSTEP},
                                   {"pe1/eddie_gun", PE_STYLE_QUICKSTEP},
                                   {"pe1/harlequin", PE_STYLE_QUICKSTEP},
                                   {"pe1/ben", PE_STYLE_QUICKSTEP},
                                   {"pe1/cameraman", PE_STYLE_QUICKSTEP},
                                   {"pe1/usher", PE_STYLE_QUICKSTEP},
                                   {"pe1/police_light", PE_STYLE_QUICKSTEP},
                                   {"pe1/opera_doublet", PE_STYLE_QUICKSTEP},
                                   {"pe1/baker", PE_STYLE_BULWARK},
                                   {"pe1/warner", PE_STYLE_BULWARK},
                                   {"pe1/opera_king", PE_STYLE_BULWARK},
                                   {"pe1/guard", PE_STYLE_BULWARK},
                                   {"pe1/admiral", PE_STYLE_BULWARK},
                                   {"pe1/policewoman", PE_STYLE_BULWARK},
                                   {"pe1/charred", PE_STYLE_BULWARK},
                                   {"pe1/opera_knight", PE_STYLE_BULWARK},
                                   {"pe1/daniel", PE_STYLE_STRIKER},
                                   {"pe1/daniel_gun", PE_STYLE_STRIKER},
                                   {"pe1/wayne", PE_STYLE_STRIKER},
                                   {"pe1/torres", PE_STYLE_STRIKER},
                                   {"pe1/nix", PE_STYLE_STRIKER},
                                   {"pe1/nix_gun", PE_STYLE_STRIKER},
                                   {"pe1/crew", PE_STYLE_STRIKER},
                                   {"pe1/soldier", PE_STYLE_STRIKER},
                                   {"pe1/captain", PE_STYLE_STRIKER},
                                   {"pe1/police_cap", PE_STYLE_STRIKER},
                                   {"pe1/evening_dress", PE_STYLE_CHANNELER},
                                   {"pe1/melissa", PE_STYLE_CHANNELER},
                                   {"pe1/eve", PE_STYLE_CHANNELER},
                                   {"pe1/maeda", PE_STYLE_CHANNELER},
                                   {"pe1/maeda_pack", PE_STYLE_CHANNELER},
                                   {"pe1/maya", PE_STYLE_CHANNELER},
                                   {"pe1/cathy", PE_STYLE_CHANNELER},
                                   {"pe1/scientist", PE_STYLE_CHANNELER},
                                   {"pe1/klamp", PE_STYLE_CHANNELER},
                                   {"pe1/klamp_coat", PE_STYLE_CHANNELER},
                                   {"pe1/nurse", PE_STYLE_CHANNELER}};

const PeStyleDef *pe_style_definition(PeStyleId id) {
  return &definitions[(unsigned)id < PE_STYLE_COUNT ? id : PE_STYLE_ORIGINAL];
}
PeStyleId pe_style_for_costume(const char *key) {
  unsigned i;
  if (key)
    for (i = 0; i < sizeof costumes / sizeof costumes[0]; ++i)
      if (!strcmp(key, costumes[i].key))
        return costumes[i].style;
  /* Unknown packs and deferred rigs never inherit abilities by display name. */
  return PE_STYLE_ORIGINAL;
}
