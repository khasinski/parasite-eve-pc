#ifndef PE_BP_API_H
#define PE_BP_API_H
/* `bp` v1, published by pe_bp: the BP overhaul's read-only snapshot and its one transaction
 * entry point. Screens (the BP UI) only read snapshots and send request IDs; every price,
 * check and write happens behind execute(), which revalidates and commits atomically. */
#include "bp_txn.h"

typedef struct PeBpApi {
    uint32_t struct_size;                       /* sizeof(PeBpApi) */
    int (*snapshot)(BpSnapshot *out);           /* 0 = no game loaded / not now */
    unsigned (*price)(unsigned base);           /* Junk fee after the Economy preset */
    BpResult (*execute)(const char *request);   /* never partial */
} PeBpApi;
#endif
