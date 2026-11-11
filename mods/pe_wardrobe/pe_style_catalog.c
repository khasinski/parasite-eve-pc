#include "pe_style.h"
#include <string.h>

static const PeStyleDef definitions[PE_STYLE_COUNT] = {
    {PE_STYLE_ORIGINAL, "original", "Original", "Native combat",
     "No costume ability", 1, 0, 100, 100},
    {PE_STYLE_QUICKSTEP, "quickstep", "Quickstep",
     "Weapon kill: +25% AT once per action",
     "No refund on PE or final enemy", 1, 25, 100, 100},
    {PE_STYLE_BULWARK, "bulwark", "Bulwark",
     "25% less damage while freely charging",
     "90% AT speed; guard ends at full AT", 1, 0, 90, 75},
    {PE_STYLE_STRIKER, "striker", "Striker", "Point-blank guns: +25% hit damage",
     "Get close; no club or PE bonus", 1, 0, 100, 125, 96},
    {PE_STYLE_CHANNELER, "channeler", "Channeler", "Damaging weapon action: +5% max PE",
     "Passive PE recovery at half speed", 1, 0, 100, 100, 0, 5, 50}};
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
