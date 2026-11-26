/* Auto Medicine order (issue 238): the Auto Medicine armor effect heals Aya below a fifth of her
 * HP by trying Full Recover first, then Medicine 4 down to Medicine 1, so it spends the strongest
 * item on a small top-up. "Weakest first" tries Medicine 1 up to Full Recover instead; Auto
 * Medicine still uses as many as it needs to get above a fifth, as it does natively. Only the
 * order changes (PE_HOOK_AUTO_MEDICINE); which items count and when it heals stay the game's. */
#include "pe_hooks.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe1/item_ids.h"

enum { PE_MEDICINE_STRONGEST, PE_MEDICINE_WEAKEST };
static int s_order;
static const char *const s_order_names[] = { "Strongest first", "Weakest first" };

static int pe_on_auto_medicine(void *ctx, void *user) {
    static const int weakest[] = { ITEM_MEDICINE_1, ITEM_MEDICINE_2, ITEM_MEDICINE_3,
                                   ITEM_MEDICINE_4, ITEM_FULL_RECOVER };
    PeHookAutoMedicine *query = ctx;
    int i;
    (void)user;
    if (s_order != PE_MEDICINE_WEAKEST || !query || !query->order || query->count != 5) return 0;
    for (i = 0; i < 5; ++i) query->order[i] = weakest[i];
    return 0;
}

void pe_auto_medicine_register(const char *mod_id) {
    pe_menu_register_choice(PE_PAGE_COMBAT, "Auto Medicine order",
        "Which medicine the Auto Medicine armor effect uses first.",
        &s_order, s_order_names, 2);
    pe_hook_add(PE_HOOK_AUTO_MEDICINE, pe_on_auto_medicine, NULL, PE_PRIORITY_NORMAL, mod_id);
}
