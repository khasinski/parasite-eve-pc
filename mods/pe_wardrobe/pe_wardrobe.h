#ifndef PE_WARDROBE_H
#define PE_WARDROBE_H

/* The Wardrobe's private interface, shared by its files:
 *   pe_wardrobe_mod.c          the mod, Junk, messages
 *   pe_wardrobe_profile.c      the profile: costumes owned and the one worn, kept across games
 *   pe_wardrobe.c              the costume registry (`costumes` and `wardrobe` services); F1 pages
 *   pe_wardrobe_menu.c         the Wardrobe screen in Aya's menu
 *   pe_skin_loader.c           character skins (costumes with a `bundle` folder)
 *   pe_pack_pe1_costumes.c     Parasite Eve's own costumes, registered like any other pack
 *   pe_packs.c                 character packs from <preferences>/packs/ (zip or folder)
 * The Wardrobe is a mod of its own (bridge findings D8 §12, D9): it needs no other mod. Junk is
 * reached through the `junk` service when a mod publishes it. Other mods add costumes through the
 * services (host/pe_services.h), never through these files. */

#include <stdint.h>

#include "pe_services.h"

/* ---- profile (pe_wardrobe_profile.c) ----
 * pe_wardrobe.ini beside the preferences, a line per record:
 *   format 1                 (first; a newer format is read but never rewritten)
 *   imported <source>        (where its records first came from; see the profile's import)
 *   costume <key>            (owned)
 *   worn <key>
 * Records of packs that aren't loaded are kept as read. */
void pe_ward_profile_load(void);
void pe_ward_profile_transactions_init(void);
int pe_ward_profile_save(void);              /* checked staged write; 0 keeps dirty state */
/* Acquisition staging is private, single-owner and process-local; not a two-file crash journal. */
int pe_ward_profile_prepare_owned(const char *key);
int pe_ward_profile_commit_owned(void);
void pe_ward_profile_cancel_owned(void);
void pe_ward_profile_tick(void);              /* saves if changed */
void pe_ward_profile_clear(void);
int pe_ward_profile_owned(const char *key);
void pe_ward_profile_own(const char *key);
int pe_ward_profile_forget(const char *key);  /* 1 if it was owned */
const char *pe_ward_profile_worn(void);       /* "" = Aya's own */
void pe_ward_profile_set_worn(const char *key);

/* The recomp's pe_collection.dat import needs the PE pack's costume keys by the recomp's indexes. */
const char *pe_pack_pe1_costume_key(int index);
void pe_pack_pe1_costumes_register(void);

/* ---- Junk and messages (pe_wardrobe_mod.c) ---- */
int pe_ward_junk_ready(char *why, unsigned size);   /* 0 with a reason when Junk can't be spent */
unsigned pe_ward_junk(void);
int pe_ward_spend(unsigned n);
int pe_ward_refund(unsigned n);
void pe_ward_say(const char *text);         /* a toast, also kept for the pages' Result rows */
const char *pe_ward_last_said(void);

/* ---- the registry (pe_wardrobe.c) ---- */
extern const PeCostumesApi g_pe_costumes_api;
void pe_wardrobe_register(const char *mod_id);
void pe_wardrobe_reset(void);

/* For the Wardrobe screen in Aya's menu (pe_wardrobe_menu.c): costumes owned and wearable now. */
int pe_wardrobe_story_bank(void);             /* original room outfit, before costume overrides */
int pe_wardrobe_owned_count(void);
const PeCostume *pe_wardrobe_owned(int k);
const PeCostume *pe_wardrobe_worn(void);         /* NULL: Aya's own clothes */
void pe_wardrobe_wear_later(const PeCostume *c); /* worn from the next room load (the menu's close) */
void pe_wardrobe_menu_register(const char *mod_id);

/* ---- character packs (pe_packs.c) ----
 * A pack costume's bundle is "@<pack id>/<folder>"; the skin loader reads its files through here. */
void pe_packs_scan(void);                        /* at start-up */
void pe_packs_register(void);                    /* their costumes into the registry */
void pe_packs_page_register(void);               /* F1 > Collection > Wardrobe > Packs */
int pe_pack_read(const char *ref, const char *name, unsigned char **data, unsigned *size, unsigned max);
int pe_pack_has(const char *ref, const char *name);

/* ---- skins (pe_skin_loader.c): costumes with a `bundle` folder ---- */
int pe_skin_available(const char *folder);       /* its files are there */
int pe_skin_instant(void);                       /* skins swap in place (F1), else the room reloads */
void pe_skin_register(const char *mod_id);

#endif
