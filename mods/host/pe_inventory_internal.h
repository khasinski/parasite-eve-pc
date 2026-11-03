#ifndef PE_INVENTORY_INTERNAL_H
#define PE_INVENTORY_INTERNAL_H
#include "pe_inventory_api.h"
/* A quote's preparation validates every fallible step, including save admission. The no-fail
 * commit is private, and only valid under the shop's reentry guard in the same synchronous call.
 */
typedef struct {
    int bag, pool, instance, dict;
    uint32_t id;
    struct ItemDataRecord *unused;
    unsigned char record[32];
    char key[48];
    uint16_t revision;
} PeInventoryGrantPlan;
PeRegResult pe_inventory_prepare_grant(const char *, PeInventoryGrantPlan *);
uint32_t pe_inventory_commit_grant(const PeInventoryGrantPlan *);
#endif
