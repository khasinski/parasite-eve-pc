#ifndef PE_BP_CATALOG_H
#define PE_BP_CATALOG_H
/* Builds the approved UI's catalog from a live snapshot and the rules' quotes. Pure: no game
 * globals. All figures shown come from bp_quote(); the renderer still calculates nothing.
 * The catalog lives in static storage that the next build overwrites. Page, row and request
 * IDs are interned (stable pointers for the whole session) so focus memory survives. */
#include "bp_rules.h"
#include "bp_ui.h"

const BpUiCatalog *bp_catalog_build(const BpSnapshot *s, BpPriceFn price);
/* Storage high-water marks of the last build, for tests and logs. */
typedef struct BpCatalogUsage { size_t pages, rows, lines, previews, text, interned; int overflow; } BpCatalogUsage;
BpCatalogUsage bp_catalog_usage(void);
#endif
