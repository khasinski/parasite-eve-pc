#ifndef PE_IMP_FIT_H
#define PE_IMP_FIT_H
/* The fit layer: an imported Model made fit for one use (docs/IMPORT_MODELS.md). Readers stay
 * faithful and the stored Model is never changed; mods take a fitted copy from here instead of
 * loading a Model raw, and get a reason when it can't serve that use.
 *
 *   classify   can it serve the use? A costume (or a held weapon, a player holding it) needs the
 *              RE human skeleton (RE2's 15-bone tree, which RE1's and RE3's players are read into)
 *              standing at a person's height, rigid parts, and a texture on at most two pages with
 *              every face inside it. An enemy or a prop takes any model with faces.
 *   normalize  on the copy, for that use only: zero-area faces dropped (all uses); placeholder parts
 *              (one face within a 16-unit box) emptied for costumes and weapons, their bones kept;
 *              faces outside the texture dropped for props (refused for costumes).
 *   report     what it is and what was dropped (PeImpFitInfo). Height and facing are measured, not
 *              applied: each builder fits size and direction its own way.
 * Fitting reads a stored Model (no disc) and takes milliseconds, so it isn't cached: change a rule
 * here and every build after it uses the new rule. */
#include "pe_imp.h"

typedef enum { PE_IMP_USE_COSTUME = 1, PE_IMP_USE_ENEMY, PE_IMP_USE_WEAPON, PE_IMP_USE_PROP } PeImpUse;

#define PE_IMP_FIT_VERSION 1

typedef struct {
    int person;                    /* the RE human skeleton at a standing height */
    int skinned;                   /* has skinned parts */
    int32_t height;                /* the skeleton's rest-pose height (joint y extent), model units */
    int facing;                    /* -1: faces -z (the RE games), 0: not known */
    uint32_t page_first, pages;    /* the texture pages its faces use */
    uint32_t faces;                /* faces kept */
    uint32_t dropped_degenerate, dropped_stub, dropped_off_texture;
} PeImpFitInfo;

/* Fit `src` for `use` into `out` (a copy; free it with pe_imp_model_free). `asset_id` (may be NULL)
 * is checked against the deny list. 0, or -1 with the reason in `why` (nothing to free). */
int pe_imp_fit_model(const PeImpModel *src, const char *asset_id, PeImpUse use, PeImpModel *out, PeImpFitInfo *info,
                     char *why, size_t n);
/* The same for a catalogued Model "<game>:<id>" in the store at `root`. */
int pe_imp_fit(const char *root, const char *asset_id, PeImpUse use, PeImpModel *out, PeImpFitInfo *info, char *why,
               size_t n);
/* A person's call, kept as data: never fit `asset_id` for the uses in `uses` (1 << PeImpUse), with
 * this reason. Game modules register theirs at start. 0, or -1 (full, bad arguments). */
int pe_imp_fit_deny(const char *asset_id, unsigned uses, const char *reason);
void pe_imp_fit_reset(void);                     /* tests: forget the deny list */

#endif
