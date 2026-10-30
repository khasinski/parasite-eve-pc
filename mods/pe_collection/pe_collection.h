#ifndef PE_COLLECTION_H
#define PE_COLLECTION_H

/* The Collection's private interface, shared by its files:
 *   pe_collection.c   the mod, Junk, messages
 *   pe_profile.c      the profile: unlocks by stable key, kept across games
 *   pe_bestiary.c     the creature registry (`bestiary` service), kills, Scan; F1 pages
 *   pe_wardrobe.c     the costume registry (`costumes` and `wardrobe` services); F1 pages
 *   pe_pack_pe1.c     Parasite Eve's own creatures and costumes, registered like any other pack
 * Everything is cosmetic or information: it never changes a fight. Other mods add creatures and
 * costumes through the services (host/pe_services.h), never through these files. */

#include <stdint.h>

#include "pe_services.h"

/* ---- profile (pe_profile.c) ----
 * pe_collection.ini beside the preferences, a line per record:
 *   creature <key> <bits> <kills> <seen>   (seen: tiers looked at in the Bestiary; optional)
 *   costume <key>            (owned)
 *   worn <key>
 * Records of packs that aren't loaded are kept as read. The psxrecomp build's pe_collection.dat
 * (creatures and skins by index) is imported once through the PE pack's keys. */
void pe_profile_load(void);
void pe_profile_save(void);               /* now */
void pe_profile_changed(void);            /* at the next field frame */
void pe_profile_tick(void);
void pe_profile_clear(void);
unsigned pe_profile_creature(const char *key, unsigned *kills);   /* bits */
void pe_profile_set_creature(const char *key, unsigned bits, unsigned kills);   /* keeps seen within bits */
unsigned pe_profile_creature_seen(const char *key);
void pe_profile_set_creature_seen(const char *key, unsigned seen);
int pe_profile_owned(const char *key);
void pe_profile_own(const char *key);
int pe_profile_forget_costume(const char *key); /* costume ownership only */
const char *pe_profile_worn(void);        /* "" = Aya's own */
void pe_profile_set_worn(const char *key);

/* The v1 import needs the PE pack's keys by the recomp's indexes. */
const char *pe_pack_pe1_creature_key(int index);
const char *pe_pack_pe1_costume_key(int index);
void pe_pack_pe1_register(void);

/* ---- Junk and messages (pe_collection.c) ---- */
int pe_col_junk_ready(char *why, unsigned size);   /* 0 with a reason when Junk can't be spent */
unsigned pe_col_junk(void);
int pe_col_spend(unsigned n);
void pe_col_say(const char *text);        /* a toast, also kept for the pages' Result rows */
const char *pe_col_last_said(void);

/* ---- registries ---- */
extern const PeBestiaryApi g_pe_bestiary_api;
extern const PeCostumesApi g_pe_costumes_api;
void pe_bestiary_register(const char *mod_id);
void pe_wardrobe_register(const char *mod_id);
void pe_bestiary_reset(void);
void pe_wardrobe_reset(void);

/* Bestiary purchases and NEW markers, shared by the F1 page and the screen in Aya's menu. */
unsigned pe_bestiary_next_price(int h, unsigned *bit);   /* bit 0: nothing left to buy */
int pe_bestiary_buy(int h);                              /* says why not; 1 bought */
unsigned pe_bestiary_new(int h);                         /* tiers known but not looked at yet */
void pe_bestiary_seen(int h);                            /* looked at: no longer NEW */

/* For the Wardrobe screen in Aya's menu (pe_wardrobe_menu.c): costumes owned and wearable now. */
int pe_wardrobe_story_bank(void);             /* original room outfit, before costume overrides */
int pe_wardrobe_owned_count(void);
const PeCostume *pe_wardrobe_owned(int k);
const PeCostume *pe_wardrobe_worn(void);         /* NULL: Aya's own clothes */
void pe_wardrobe_wear_later(const PeCostume *c); /* worn from the next room load (the menu's close) */
void pe_wardrobe_menu_register(const char *mod_id);
void pe_bestiary_menu_register(const char *mod_id);   /* pe_bestiary_menu.c: the Bestiary in Aya's menu */

/* ---- skins (pe_skin_loader.c): costumes with a `bundle` folder ---- */
int pe_skin_available(const char *folder);       /* its files are there */
int pe_skin_instant(void);                       /* skins swap in place (F1), else the room reloads */
void pe_skin_register(const char *mod_id);

#endif
