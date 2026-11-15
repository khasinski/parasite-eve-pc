#ifndef PE_WORKSHOP_MODEL_H
#define PE_WORKSHOP_MODEL_H

/* What the Workshop screen shows and what Cross does, without drawing (pe_workshop_model.c), so
 * it can be tested on its own. The rules (Workshop Overhaul, decisions of November 3):
 *   - a list that scrolls and wraps, no Next page / Back rows; Circle goes back;
 *   - rows that can't be chosen now are gray, never red; they stay selectable and Cross on one
 *     plays the error sound and says why;
 *   - a one-of-a-kind good already bought keeps its row, gray, with "owned" where its price was;
 *   - every purchase goes through the game's Yes / No (cursor on No) and plays the confirm sound.
 * Buying itself stays in pe_requisition.c. */
#include <stddef.h>
#include <stdint.h>

enum { PE_WS_REQUISITION, PE_WS_REBUILD, PE_WS_WARDROBE, PE_WS_PERMIT, PE_WS_SERVICES };

/* The Workshop's rows: the services there now, in order. Returns how many. */
enum { PE_WS_EXTRA_SERVICES = 8, PE_WS_SERVICE_CAPACITY = PE_WS_SERVICES + PE_WS_EXTRA_SERVICES };
struct MenuWidgetNode;
typedef struct PeWsService {
    const char *id, *name, *help;
    int (*available)(void);
    int (*open)(struct MenuWidgetNode *from);
} PeWsService;
/* Caller-owned definitions outlive the registry. Stable IDs reject duplicates. */
int pe_ws_register_service(const PeWsService *service);
int pe_ws_open_service(int service, struct MenuWidgetNode *from);
/* out has PE_WS_SERVICE_CAPACITY entries. Built-ins keep their original order. */
int pe_ws_services(int *out);
const char *pe_ws_service_name(int service);
const char *pe_ws_service_help(int service);

typedef struct PeWsGoods {
    uint16_t id;                 /* goods id; economy effects are 0xF000.. */
    const char *name;            /* an economy effect's name; NULL: the game's item name for id */
    uint32_t price;
    int owned;                   /* a one-of-a-kind good already bought: "owned", gray */
    const char *blocked;         /* why it can't be bought now (gray), or NULL */
} PeWsGoods;

/* Requisition row `row`; 0 if there is no such row. */
int pe_ws_goods(int row, PeWsGoods *out);

/* Cross on Requisition row `row`: PE_WS_ASK with the Yes / No question in `msg`, or PE_WS_REFUSE
 * with the reason. */
enum { PE_WS_REFUSE, PE_WS_ASK };
int pe_ws_cross(int row, char *msg, size_t size);

/* An item's name in ASCII for questions (pe_item_table.h); "it" for one it doesn't know. */
const char *pe_ws_item_name(unsigned id);

/* ---- Rebuild: the Mod Catalog's `rebuild` service; no rows while the Catalog isn't running ---- */
struct ItemDataRecord;
typedef struct PeWsRebuild {
    uint8_t id;                  /* base item */
    uint32_t price;
    unsigned bought;             /* slots added with Mod Permits */
    const char *blocked;         /* why it can't be rebuilt now (gray), or NULL */
} PeWsRebuild;

int pe_ws_rebuild_count(void);
/* Rebuild row `row`, and into *rec (if not NULL) the item it makes; 0 if there is no such row. */
int pe_ws_rebuild(int row, PeWsRebuild *out, struct ItemDataRecord *rec);
int pe_ws_rebuild_cross(int row, char *msg, size_t size);   /* PE_WS_ASK / PE_WS_REFUSE */
int pe_ws_rebuild_do(int row, char *msg, size_t size);      /* 1 if rebuilt */

#endif
