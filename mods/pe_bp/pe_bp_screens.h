#ifndef PE_BP_SCREENS_H
#define PE_BP_SCREENS_H
/* "bp_screens" v1, published by pe_bp for its own BP screens (prototypes/bp-ui): the full
 * read-only snapshot the screens draw from, and the one transaction entry point. Not for other
 * mods: they use the public "bp" service (host/pe_services.h), whose layout is stable. Every
 * price, check and write happens behind execute(), which revalidates and commits atomically
 * (the same path as the public service, so "bp.changed" fires for screen purchases too). */
#include "bp_txn.h"

typedef struct PeBpScreensApi {
    uint32_t struct_size;                       /* sizeof(PeBpScreensApi) */
    int (*snapshot)(BpSnapshot *out);           /* 0 = no game loaded / not now */
    unsigned (*price)(unsigned base);           /* Junk fee after the Economy preset */
    BpResult (*execute)(const char *request);   /* never partial */
} PeBpScreensApi;
#endif
