#include "pe_style.h"
#include <string.h>

static const PeStyleDef definitions[PE_STYLE_COUNT] = {
    {.id = PE_STYLE_ORIGINAL,
     .key = "original",
     .name = "Original",
     .benefit = "Native combat",
     .limitation = "No costume ability",
     .implemented = 1,
     .charge_percent = 100,
     .damage_percent = 100},
    {.id = PE_STYLE_QUICKSTEP,
     .key = "quickstep",
     .name = "Quickstep",
     .benefit = "Weapon kill: +25% AT, speed burst",
     .limitation = "No refund on PE or final enemy",
     .implemented = 1,
     .refund_percent = 25,
     .charge_percent = 100,
     .damage_percent = 100,
     .move_percent = 125},
    {.id = PE_STYLE_BULWARK,
     .key = "bulwark",
     .name = "Bulwark",
     .benefit = "Guarded hits load a counter attack",
     .limitation = "Must take hits to power a counter",
     .implemented = 1,
     .charge_percent = 100,
     .damage_percent = 75,
     .counter_percent = 50},
    {.id = PE_STYLE_STRIKER,
     .key = "striker",
     .name = "Striker",
     .benefit = "Point-blank guns: +25% hit damage",
     .limitation = "Get close; no club or PE bonus",
     .implemented = 1,
     .charge_percent = 100,
     .damage_percent = 125,
     .point_blank_gap = 96},
    {.id = PE_STYLE_CHANNELER,
     .key = "channeler",
     .name = "Channeler",
     .benefit = "Weapon hits charge next PE power",
     .limitation = "Passive PE recovery at half speed",
     .implemented = 1,
     .charge_percent = 100,
     .damage_percent = 100,
     .pe_recovery_percent = 50,
     .weave_charges = 3,
     .overchannel_percent = 25}};
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
