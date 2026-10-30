/*
 * Wardrobe: a registry of costumes (`costumes` service, host/pe_services.h), bought once with Junk
 * and kept in every game. A costume is one of the game's player bundles or a model its provider
 * loads itself.
 *
 * The game's bundles are picked by g_SceneAreaType (g_GameState.requested_entity_bank): the room
 * sets it when it loads, and Aya's model, textures and per-day animation rows all follow it. A worn
 * bundle costume rewrites it from 10 / 12 / 13 (the gameplay outfits) at the start of the room
 * load's model step (PE_HOOK_PLAYER_BANK, phase 0) and asks for a reload when the loaded bundle
 * differs; taking it off needs nothing, the game reloads its own when the room's bank differs from
 * the loaded one. A provider's costume gets load(1) at that same moment while worn, load(0) once
 * when it comes off. Wearing from F1 asks for the game's same-room reload (g_GameStateFlags 0x2000
 * and flags 0x800 end the frame loop and the room loads again), so it shows as soon as the panel
 * closes.
 *
 * Also publishes `wardrobe` v1 (what can be worn now, for shops: Wayne's Workshop) and announces
 * changes as the "wardrobe.worn" event.
 */
#include <stdio.h>
#include <string.h>

#include "pe_collection.h"
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_translate.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe1/game_state.h"

#define PE_MAX_COSTUMES  256
#define PE_MAX_GROUPS    16
#define PE_RELOAD_ROOM   0x2000u   /* g_GameStateFlags: end the frame loop */
#define PE_RELOAD_SAME   0x800u    /* g_GameState.flags: the same room again */
#define PE_NEW_BUNDLE    0x200000u /* g_GameState.flags: load the player bundle */

/* ---- the registry ---- */
static const PeCostume *s_costume[PE_MAX_COSTUMES];
static int s_costumes;
static const PeCostume *s_loaded;      /* the provider costume whose load(1) ran last */
static int s_event = -1;

static int pe_gameplay_bank(int b) { return b == 10 || b == 12 || b == 13; }

static int pe_find(const char *key) {
    int i;
    if (!key || !*key) return -1;
    for (i = 0; i < s_costumes; ++i) if (strcmp(s_costume[i]->key, key) == 0) return i;
    return -1;
}

static int pe_add(const PeCostume *c) {
    if (!c || !c->key || !*c->key || !c->name || s_costumes == PE_MAX_COSTUMES || pe_find(c->key) >= 0 ||
        (c->bank >= 0 && !pe_gameplay_bank(c->bank)) || (c->bank < 0 && !c->load && !(c->bundle && *c->bundle))) {
        pe_mod_log("pe_collection", "wardrobe: costume %s refused", c && c->key ? c->key : "?");
        return -1;
    }
    s_costume[s_costumes] = c;
    return s_costumes++;
}

static int pe_is_skin(const PeCostume *c) { return c && c->bank < 0 && !c->load; }   /* the skin loader's */

static int pe_wearable(const PeCostume *c) {
    return c && (!c->available || c->available()) && (!pe_is_skin(c) || pe_skin_available(c->bundle));
}

static const PeCostume *pe_worn(void) {
    const int i = pe_find(pe_profile_worn());
    return i >= 0 && pe_wearable(s_costume[i]) ? s_costume[i] : NULL;
}

/* The game's same-room reload, so a new costume shows when F1 closes. */
static int pe_reload(void) {
    if (!pe_game_loaded() || pe_in_battle() || (unsigned)(g_SceneAreaType - 10u) >= 5u) return 0;
    g_GameStateFlags |= PE_RELOAD_ROOM;
    g_GameState.flags |= PE_RELOAD_SAME;
    return 1;
}

const PeCostumesApi g_pe_costumes_api = { pe_add, pe_worn, pe_reload };

/* ---- the costume at room load ---- */
static int s_room_bank = -1;            /* the bank the room itself asked for, before any costume */

static int pe_on_player_bank(void *ctx, void *user) {
    const PeCostume *c = pe_worn();
    (void)ctx;
    (void)user;
    if (!pe_save_loaded() || g_GameState.entity_texture_phase != 0) return 0;   /* the actor is rebuilt meanwhile */
    s_room_bank = g_SceneAreaType;                            /* phase 0 comes once a load */
    if (s_loaded && s_loaded != c) { s_loaded->load(0); s_loaded = NULL; }   /* a provider costume comes off */
    if (!c || !pe_gameplay_bank(g_SceneAreaType)) return 0;                /* not in Liberate, story scenes */
    if (pe_is_skin(c)) return 0;                                /* the skin loader puts it on */
    if (c->bank < 0) {
        c->load(1);
        s_loaded = c;
        return 0;
    }
    if (g_SceneAreaType == c->bank) return 0;
    g_SceneAreaType = (u8)c->bank;                              /* one of the game's own bundles */
    if (c->bank != g_GameState.loaded_entity_bank) g_GameState.flags |= PE_NEW_BUNDLE;
    g_GameState.story_day_flags |= 1u;                          /* and its day rows */
    pe_log("skin", "costume=%s bank=%d", c->key, c->bank);
    return 0;
}

/* Does wearing `c` (instead of what is worn now) need the room reloaded? Skins swap in place with
 * Instant swap on (pe_skin_loader.c), otherwise the room reloads to put one on or take it off; the game's
 * own bundles need loading unless the right one is loaded already; a provider's model goes in at
 * the room load. */
static int pe_needs_reload(const PeCostume *c) {
    int bank;
    if (pe_is_skin(c) || pe_is_skin(pe_worn())) return !pe_skin_instant();   /* on or off */
    if (c && c->bank < 0) return 1;
    bank = c ? c->bank : s_room_bank;
    return bank < 0 || bank != g_GameState.loaded_entity_bank;
}

static void pe_set_worn(const PeCostume *c, int now) {
    char msg[96];
    const int reload = pe_needs_reload(c);
    pe_profile_set_worn(c ? c->key : "");
    pe_profile_save();
    pe_log("wardrobe", "wear=%s reload=%d", c ? c->key : "own", reload);
    pe_event_emit(s_event, (void *)c);
    if (!now) return;
    snprintf(msg, sizeof msg, !reload || pe_reload() ? pe_l10n_ui("Wearing %s") : pe_l10n_ui("Wearing %s from the next room"), c ? pe_l10n_ui(c->name) : pe_l10n_ui("Aya's own clothes"));
    pe_col_say(msg);
}

/* Buy (if needed); 0 with a reason in msg if it can't. */
static int pe_buy(const PeCostume *c, char *msg, size_t size) {
    if (pe_profile_owned(c->key) || !c->price) return 1;
    if (!pe_col_junk_ready(msg, (unsigned)size)) return 0;
    if (!pe_col_spend(c->price)) { snprintf(msg, size, pe_l10n_ui("Need %u Junk (have %u)"), (unsigned)c->price, pe_col_junk()); return 0; }
    pe_profile_own(c->key);
    pe_log("wardrobe", "bought=%s price=%u", c->key, (unsigned)c->price);
    return 1;
}

/* ---- F1 pages: a group row, a costume row browsed with left / right, buy / wear ---- */
static const char *s_groups[PE_MAX_GROUPS];
static unsigned s_ngroups;
static int s_group, s_sel, s_list[PE_MAX_COSTUMES], s_count;
static char s_name[80];

static const char *pe_group_of(const PeCostume *c) { return c->group ? c->group : "Other"; }

/* Groups in the order packs registered them; costumes that can't be worn yet (available() 0, e.g.
 * their files are missing) are hidden, as are groups with nothing else. */
static void pe_groups(void) {
    int i;
    s_ngroups = 0;
    for (i = 0; i < s_costumes; ++i) {
        const char *g = pe_group_of(s_costume[i]);
        if (!pe_wearable(s_costume[i])) continue;
        unsigned j = 0;
        while (j < s_ngroups && strcmp(s_groups[j], g)) ++j;
        if (j == s_ngroups && s_ngroups < PE_MAX_GROUPS) s_groups[s_ngroups++] = g;
    }
}
static unsigned pe_group_count(void) { pe_groups(); return s_ngroups ? s_ngroups : 1u; }
static const char *pe_group_name(unsigned i) { pe_groups(); return i < s_ngroups ? s_groups[i] : "None"; }

static void pe_filter(void) {
    int i;
    pe_groups();
    s_count = 0;
    for (i = 0; i < s_costumes; ++i)
        if (pe_wearable(s_costume[i]) && (unsigned)s_group < s_ngroups &&
            strcmp(pe_group_of(s_costume[i]), s_groups[s_group]) == 0) s_list[s_count++] = i;
    if (s_sel >= s_count) s_sel = 0;
}

static unsigned pe_count(void) { pe_filter(); return s_count ? (unsigned)s_count : 1u; }

static const char *pe_state_name(const PeCostume *c) {
    if (pe_worn() == c) return "worn";
    return pe_profile_owned(c->key) || !c->price ? "owned" : NULL;
}

static const char *pe_name(unsigned n) {
    const PeCostume *c;
    const char *state;
    pe_filter();
    if (!s_count) return pe_l10n_ui("None");
    if (n >= (unsigned)s_count) n = 0;
    c = s_costume[s_list[n]];
    if ((state = pe_state_name(c))) snprintf(s_name, sizeof s_name, "%s  [%s]  (%u/%d)", pe_l10n_ui(c->name), state, n + 1, s_count);
    else snprintf(s_name, sizeof s_name, "%s  %u Junk  (%u/%d)", pe_l10n_ui(c->name), (unsigned)c->price, n + 1, s_count);
    return s_name;
}

static void pe_accept(void) {
    const PeCostume *c;
    char msg[96];
    pe_filter();
    if (!s_count) return;
    c = s_costume[s_list[s_sel]];
    if (pe_worn() == c) { pe_col_say("Already wearing it"); return; }
    if (!pe_buy(c, msg, sizeof msg)) { pe_col_say(msg); return; }
    pe_set_worn(c, 1);
}

static void pe_default(void) {
    if (!pe_worn()) { pe_col_say("Already wearing Aya's own clothes"); return; }
    pe_set_worn(NULL, 1);
}

static void pe_status(char *out, unsigned size) {
    const PeCostume *c = pe_worn();
    snprintf(out, size, pe_l10n_ui("Wearing %s   Junk %u"), c ? pe_l10n_ui(c->name) : pe_l10n_ui("Aya's own"), pe_col_junk());
}

static void pe_result(char *out, unsigned size) { snprintf(out, size, "%s", pe_col_last_said()); }

/* ---- `wardrobe` v1 (shops): Aya's own clothes, then every costume that can be worn now ---- */
static const PeCostume *pe_ward_costume(int k, int *own) {
    int i;
    *own = k == 0;
    if (k-- == 0) return NULL;
    for (i = 0; i < s_costumes; ++i)
        if (pe_wearable(s_costume[i]) && k-- == 0) return s_costume[i];
    return NULL;
}
static int pe_ward_count(void) { int n = 1, i; for (i = 0; i < s_costumes; ++i) n += pe_wearable(s_costume[i]); return n; }
static const char *pe_ward_name(int k) {
    int own;
    const PeCostume *c = pe_ward_costume(k, &own);
    return own ? "Aya's own clothes" : c ? pe_l10n_ui(c->name) : "";
}
static unsigned pe_ward_price(int k) { int own; const PeCostume *c = pe_ward_costume(k, &own); return c ? c->price : 0u; }
static int pe_ward_state(int k) {
    int own;
    const PeCostume *c = pe_ward_costume(k, &own);
    if (own) return pe_worn() ? 1 : 2;
    if (!c) return 0;
    return pe_worn() == c ? 2 : pe_profile_owned(c->key) || !c->price;
}
static int pe_ward_pick(int k, char *msg, size_t size) {   /* buy if needed, then wear (from the next room) */
    int own;
    const PeCostume *c = pe_ward_costume(k, &own);
    if (!own && !c) return 0;
    if (c && !pe_buy(c, msg, size)) return 0;
    pe_set_worn(c, 0);
    snprintf(msg, size, pe_l10n_ui("Wearing %s when you leave"), c ? pe_l10n_ui(c->name) : pe_l10n_ui("Aya's own clothes"));
    return 1;
}
static const PeWardrobeApi s_wardrobe = { pe_ward_count, pe_ward_name, pe_ward_price, pe_ward_state, pe_ward_pick };

/* ---- for the Wardrobe screen in Aya's menu ---- */
static int s_reload_after_menu;

static int pe_owned_wearable(const PeCostume *c) { return pe_wearable(c) && (pe_profile_owned(c->key) || !c->price); }

int pe_wardrobe_owned_count(void) {
    int n = 0, i;
    for (i = 0; i < s_costumes; ++i) n += pe_owned_wearable(s_costume[i]);
    return n;
}

const PeCostume *pe_wardrobe_owned(int k) {
    int i;
    for (i = 0; i < s_costumes; ++i)
        if (pe_owned_wearable(s_costume[i]) && k-- == 0) return s_costume[i];
    return NULL;
}

const PeCostume *pe_wardrobe_worn(void) { return pe_worn(); }

void pe_wardrobe_wear_later(const PeCostume *c) {
    s_reload_after_menu = pe_needs_reload(c);        /* a skin goes in by itself as the menu closes */
    pe_set_worn(c, 0);
}

/* Once Aya's menu has closed, the room reloads so the costume picked there shows. */
static void pe_reload_tick(void) {
    if (!s_reload_after_menu || !pe_game_loaded() || !pe_free_field()) return;
    s_reload_after_menu = 0;
    (void)pe_reload();
}

/* Testing actions operate on registered keys, including costumes whose assets
 * are currently missing. Availability checks still apply before wearing. */
static void pe_unlock_all(void) {
    int unlocked = 0;
    for (int i = 0; i < s_costumes; ++i) {
        const char *key = s_costume[i]->key;
        if (!pe_profile_owned(key)) {
            pe_profile_own(key);
            unlocked += pe_profile_owned(key);
        }
    }
    pe_profile_save();
    char msg[80];
    snprintf(msg, sizeof msg, "Unlocked %d costumes", unlocked);
    pe_col_say(msg);
}
static void pe_lock_all(void) {
    int locked = 0;
    if (pe_find(pe_profile_worn()) >= 0) pe_set_worn(NULL, 1);
    for (int i = 0; i < s_costumes; ++i)
        locked += pe_profile_forget_costume(s_costume[i]->key);
    pe_profile_save();
    char msg[80];
    snprintf(msg, sizeof msg, "Cleared %d costume unlocks; free outfits remain available", locked);
    pe_col_say(msg);
}
void pe_wardrobe_reset(void) { s_sel = 0; }

void pe_wardrobe_register(const char *mod_id) {
    s_event = pe_event_id("wardrobe.worn");
    pe_menu_register_action(PE_PAGE_DEBUG_COLLECTION, "Unlock all costumes",
        "Testing: own every registered costume for free, saved across games.", pe_unlock_all);
    pe_menu_register_action(PE_PAGE_DEBUG_COLLECTION, "Lock all costumes",
        "Testing: clear costume unlocks and return to story clothes. Free outfits stay available.", pe_lock_all);
    pe_menu_register_dynamic_choice(PE_PAGE_WARDROBE, "Group", "Which costumes to browse.", &s_group, PE_MAX_GROUPS,
                                    pe_group_count, pe_group_name);
    pe_menu_register_dynamic_choice(PE_PAGE_WARDROBE, "Costume", "Left / right to browse.", &s_sel, PE_MAX_COSTUMES, pe_count, pe_name);
    pe_menu_register_action(PE_PAGE_WARDROBE, "Buy / wear", "Buy once (every game), wear now: the room reloads.", pe_accept);
    pe_menu_register_action(PE_PAGE_WARDROBE, "Wear Aya's own", "Back to the clothes the story gives her.", pe_default);
    pe_menu_register_info(PE_PAGE_WARDROBE, "Status", "What Aya wears, and your Junk.", pe_status);
    pe_menu_register_info(PE_PAGE_WARDROBE, "Result", "What the last action did.", pe_result);
    pe_menu_register_tick(pe_reload_tick);
    pe_service_publish("wardrobe", 1, &s_wardrobe, mod_id);
    pe_hook_add(PE_HOOK_PLAYER_BANK, pe_on_player_bank, NULL, PE_PRIORITY_NORMAL, mod_id);
}
