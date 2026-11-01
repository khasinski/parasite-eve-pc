/*
 * Requisition's F1 page: F1 > PE Enhancements > Economy > Requisition. Presentation and
 * confirmation only; the goods, prices and rules are pe_requisition.c.
 *
 *   Your Junk / Goods / Details / Status   pick goods and see why they can or can't be bought
 *   Buy                                    press twice; the second press rechecks the goods,
 *                                          price and bag, and asks again if anything changed
 *   Spare Club                             free Club 1, only when Aya has no usable weapon
 *   Result                                 the last action or the pending confirmation
 *
 * A pending confirmation is cancelled whenever the game runs a frame (leaving F1), on a scene
 * change, a load or a new game. When Aya has no usable weapon, a one-time message points to
 * the Spare Club.
 */
#include <stdio.h>
#include <string.h>

#include "pe_economy.h"
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe_requisition.h"

#define PE_BAG_SLOTS 50
#define PE_GOODS_ROWS_MAX 19      /* goods plus the four economy effects */
#define PE_RESCUE_CHECK_FRAMES 60

enum { ARMED_NONE, ARMED_BUY, ARMED_CLUB };

static int s_selected;            /* the Goods row */
static struct {
    int what;                     /* ARMED_* */
    uint16_t item;
    uint32_t price;
    s16 bag[PE_BAG_SLOTS];        /* the bag when armed: a change asks again */
} s_armed;
static char s_result[96], s_label[80];
static unsigned s_frames;
static int s_rescue_warned;


static void pe_result(const char *text) {
    snprintf(s_result, sizeof s_result, "%s", text);
    pe_toast(text);
}

static void pe_arm(int what, uint16_t item, uint32_t price) {
    s_armed.what = what;
    s_armed.item = item;
    s_armed.price = price;
    memcpy(s_armed.bag, g_AyaInventoryItems, sizeof s_armed.bag);
}

static int pe_bag_unchanged(void) { return memcmp(s_armed.bag, g_AyaInventoryItems, sizeof s_armed.bag) == 0; }

/* ---- rows ---- */

static unsigned pe_goods_count(void) { return (unsigned)pe_shop_count(); }

static const char *pe_goods_name(unsigned row) {
    const uint16_t id = pe_shop_item((int)row);
    snprintf(s_label, sizeof s_label, "%s - %u Junk", pe_shop_name(id), (unsigned)pe_shop_price(id));
    return s_label;
}

static void pe_junk_text(char *out, unsigned size) { snprintf(out, size, "%u", (unsigned)pe_loot_junk()); }
static void pe_details_text(char *out, unsigned size) { pe_shop_details(pe_shop_item(s_selected), out, size); }

static void pe_status_text(char *out, unsigned size) {
    const char *why = pe_shop_blocked(pe_shop_item(s_selected));
    snprintf(out, size, "%s", why ? why : "Ready to buy");
}

static void pe_result_text(char *out, unsigned size) {
    snprintf(out, size, "%s", s_result[0] ? s_result : "Choose goods, then press Buy twice");
}

/* ---- actions ---- */

static void pe_buy(void) {
    const uint16_t id = pe_shop_item(s_selected);
    const char *why = pe_shop_blocked(id);
    uint32_t price;
    char msg[96];
    if (why) {
        s_armed.what = ARMED_NONE;
        pe_result(why);
        return;
    }
    price = pe_shop_price(id);
    if (s_armed.what != ARMED_BUY || s_armed.item != id || s_armed.price != price || !pe_bag_unchanged()) {
        pe_arm(ARMED_BUY, id, price);
        snprintf(s_result, sizeof s_result, "Press Buy again: %s for %u Junk", pe_shop_name(id), (unsigned)price);
        pe_toast(s_result);
        return;
    }
    s_armed.what = ARMED_NONE;
    pe_shop_buy(id, msg, sizeof msg);
    pe_result(msg);
}

static void pe_spare_club(void) {
    char msg[96];
    if (!pe_game_loaded() || !pe_loot_enabled() || !pe_free_field()) {
        s_armed.what = ARMED_NONE;
        pe_result("Use Requisition in the field with Junk economy on");
        return;
    }
    if (!pe_shop_rescue_needed()) {
        s_armed.what = ARMED_NONE;
        pe_result("Aya can still fight: no spare Club");
        return;
    }
    if (s_armed.what != ARMED_CLUB || !pe_bag_unchanged()) {
        pe_arm(ARMED_CLUB, 0, 0);
        pe_result("Press Spare Club again to receive a free Club 1");
        return;
    }
    s_armed.what = ARMED_NONE;
    pe_shop_spare_club(msg, sizeof msg);
    pe_result(msg);
}

/* ---- per frame and resets ---- */

static void pe_tick(void) {
    s_armed.what = ARMED_NONE;    /* the game ran a frame: F1 was left, so the confirmation lapses */
    if (++s_frames % PE_RESCUE_CHECK_FRAMES || !pe_game_loaded() || !pe_loot_enabled() || !pe_free_field()) return;
    if (!pe_shop_rescue_needed()) {
        s_rescue_warned = 0;
        return;
    }
    if (!s_rescue_warned) {
        s_rescue_warned = 1;
        pe_toast("Aya cannot attack: F1 / Economy / Requisition has a spare Club");
    }
}

static int pe_on_reset(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    s_armed.what = ARMED_NONE;
    s_rescue_warned = 0;
    s_frames = 0;
    s_result[0] = '\0';
    return 0;
}

void pe_requisition_menu_register(const char *mod_id) {
    pe_menu_register_info(PE_PAGE_REQUISITION, "Your Junk", "The live Junk balance.", pe_junk_text);
    pe_menu_register_dynamic_choice(PE_PAGE_REQUISITION, "Goods",
                                    "Tools disappear with a Tool Kit; effects disappear once unlocked.", &s_selected,
                                    PE_GOODS_ROWS_MAX, pe_goods_count, pe_goods_name);
    pe_menu_register_info(PE_PAGE_REQUISITION, "Details", "Price, carried amount and bag space.", pe_details_text);
    pe_menu_register_info(PE_PAGE_REQUISITION, "Status", "Why this purchase is available or blocked.", pe_status_text);
    pe_menu_register_action(PE_PAGE_REQUISITION, "Buy", "Press twice. Rechecks goods, price, balance and bag space.", pe_buy);
    pe_menu_register_action(PE_PAGE_REQUISITION, "Spare Club",
                            "Free Club 1 only if no usable weapon exists in bag or storage. Press twice.", pe_spare_club);
    pe_menu_register_info(PE_PAGE_REQUISITION, "Result", "The last action or pending confirmation.", pe_result_text);
    pe_menu_register_tick(pe_tick);
    pe_hook_add(PE_HOOK_SAVE_READ, pe_on_reset, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_SCENE_START, pe_on_reset, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_NEW_GAME, pe_on_reset, NULL, PE_PRIORITY_NORMAL, mod_id);
}

#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe_checkpoint_wire.h"
#include "pe_checkpoint_economy.h"
int pe_requisition_menu_checkpoint_walk(const unsigned char *in,unsigned char *out,int apply){if((!in&&!out)||(0))return 0;if(apply&&(!in||!pe_requisition_menu_checkpoint_walk(in,0,0)))return 0;PeCheckpointWire w={in,out,0,apply,1};
PE_CK_S(w,s_selected,0,255,0);
PE_CK_S(w,s_armed.what,0,255,0);
PE_CK_U(w,s_armed.item,0,65535,0);
PE_CK_U(w,s_armed.price,0,UINT32_MAX,0);
for(unsigned i=0;i<50;i++){PE_CK_S(w,s_armed.bag[i],-32768,32767,0);}
PE_CK_U(w,s_frames,0,UINT32_MAX,0);
PE_CK_U(w,s_rescue_warned,0,1,0);

if(apply&&w.ok){memset(s_result,0,sizeof s_result);memset(s_label,0,sizeof s_label);}return w.ok&&w.at==224u;}
#endif
