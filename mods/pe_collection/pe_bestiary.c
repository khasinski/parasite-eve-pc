/*
 * Bestiary: a registry of creatures (`bestiary` service, host/pe_services.h). Packs add creatures
 * and map their enemies to them; resolvers (a randomizer's shuffled enemies) are asked first. Per
 * creature:
 *   basics   (name, HP, defense, where)  first kill, or any purchase;
 *   weakness (weak / strong elements)    casting Scan on it, 5 kills (bosses: defeat), or Junk;
 *   loot     (drop and steal odds)       15 kills, or Junk (bosses: Junk only).
 * Kills: every battle frame (PE_HOOK_BATTLE_UPDATE) the enemy slots are watched; an enemy whose HP
 * goes from above 0 to 0 or less is a kill of the creature it resolves to. Scan: power 7 on an
 * enemy (PE_HOOK_SPELL). Unlocks are saved by the creature's key. F1 pages: area and progress
 * filters, a creature row browsed with left / right, its details, one purchase.
 */
#include <stdio.h>
#include <string.h>

#include "pe_collection.h"
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"

#define PE_SCAN           7
#define PE_ENEMY_SLOTS    7
#define PE_MAX_CREATURES  512
#define PE_MAX_MAPPINGS   2048
#define PE_MAX_RESOLVERS  8
#define PE_MAX_SECTIONS   32
#define PE_KILLS_WEAK     5
#define PE_KILLS_LOOT     15

/* ---- the registry ---- */
static const PeCreatureInfo *s_creature[PE_MAX_CREATURES];
static int s_creatures;
static struct { uint16_t room, identity; int16_t handle; } s_map[PE_MAX_MAPPINGS];
static int s_maps;
static PeBestiaryResolver s_resolver[PE_MAX_RESOLVERS];
static int s_resolvers;
static int s_event = -1;
static uint32_t s_checkpoint_epoch=1;
static void pe_checkpoint_changed(void){if(s_checkpoint_epoch<UINT32_MAX)++s_checkpoint_epoch;}


static int pe_valid(int h) { return h >= 0 && h < s_creatures; }

static int pe_find(const char *key) {
    int h;
    if (!key) return -1;
    for (h = 0; h < s_creatures; ++h) if (strcmp(s_creature[h]->key, key) == 0) return h;
    return -1;
}

static int pe_add(const PeCreatureInfo *info) {
    if (!info || !info->key || !*info->key || !info->name || s_creatures == PE_MAX_CREATURES || pe_find(info->key) >= 0) {
        pe_mod_log("pe_collection", "bestiary: creature %s refused", info && info->key ? info->key : "?");
        return -1;
    }
    pe_checkpoint_changed();
    s_creature[s_creatures] = info;
    return s_creatures++;
}

static const PeCreatureInfo *pe_info(int h) { return pe_valid(h) ? s_creature[h] : NULL; }

static int pe_replace(int h, const PeCreatureInfo *info) {
    if (!pe_valid(h) || !info || !info->key || strcmp(info->key, s_creature[h]->key) != 0) return 0;
    pe_checkpoint_changed();
    s_creature[h] = info;
    return 1;
}

static int pe_map(int room, unsigned identity, int h) {
    int i;
    if (!pe_valid(h) || room < 0) return 0;
    for (i = 0; i < s_maps; ++i)
        if (s_map[i].room == room && s_map[i].identity == identity) { pe_checkpoint_changed(); s_map[i].handle = (int16_t)h; return 1; }
    if (s_maps == PE_MAX_MAPPINGS) return 0;
    pe_checkpoint_changed();
    s_map[s_maps].room = (uint16_t)room;
    s_map[s_maps].identity = (uint16_t)identity;
    s_map[s_maps++].handle = (int16_t)h;
    return 1;
}

static int pe_add_resolver(PeBestiaryResolver fn) {
    if (!fn || s_resolvers == PE_MAX_RESOLVERS) return 0;
    pe_checkpoint_changed();
    s_resolver[s_resolvers++] = fn;
    return 1;
}

/* Which creature an enemy is: resolvers (newest first), then the mapped table. */
static int pe_resolve(int room, unsigned identity, const EnemyCombatant *enemy) {
    int i;
    for (i = s_resolvers - 1; i >= 0; --i) {
        const int h = s_resolver[i](room, identity, enemy);
        if (pe_valid(h)) return h;
    }
    for (i = 0; i < s_maps; ++i)
        if (s_map[i].room == room && s_map[i].identity == identity) return s_map[i].handle;
    return -1;
}

static unsigned pe_state(int h) { return pe_valid(h) ? pe_profile_creature(s_creature[h]->key, NULL) : 0u; }
static unsigned pe_kills(int h) { unsigned k = 0; if (pe_valid(h)) (void)pe_profile_creature(s_creature[h]->key, &k); return k; }

static void pe_unlock_why(int h, unsigned bits, const char *why) {
    const unsigned old = pe_state(h), now = (old | bits | PE_BESTIARY_MET) & 7u, gained = now & ~old;
    const PeCreatureInfo *c = pe_info(h);
    char msg[96];
    if (!c || now == old) return;
    pe_profile_set_creature(c->key, now, pe_kills(h));
    snprintf(msg, sizeof msg, "Bestiary: %s%s%s%s", c->name, gained & PE_BESTIARY_WEAK ? " - weaknesses" : "",
             gained & PE_BESTIARY_LOOT ? " - loot" : "", gained == PE_BESTIARY_MET ? " - new entry" : "");
    pe_toast(msg);
    pe_log("bestiary", "creature=%s bits=0x%02X why=%s", c->key, now, why);
    {
        PeBestiaryEvent e = { h, gained, why };
        pe_event_emit(s_event, &e);
    }
}

static void pe_unlock(int h, unsigned bits) { pe_unlock_why(h, bits, "mod"); }

static void pe_kill(int h) {
    const PeCreatureInfo *c = pe_info(h);
    unsigned k = pe_kills(h) + 1u, bits = PE_BESTIARY_MET;
    if (!c) return;
    pe_profile_set_creature(c->key, pe_state(h), k);
    if (c->boss) bits |= PE_BESTIARY_WEAK;                 /* defeated: its weaknesses are known */
    else {
        if (k >= PE_KILLS_WEAK) bits |= PE_BESTIARY_WEAK;
        if (k >= PE_KILLS_LOOT) bits |= PE_BESTIARY_LOOT;
    }
    pe_unlock_why(h, bits, "kill");
}

/* ---- model forms (for the viewer) ---- */
static PeModelSource s_model[PE_MAX_CREATURES][PE_BESTIARY_MAX_FORMS];

static int pe_set_model(int h, int form, const PeModelSource *src) {
    if (!pe_valid(h) || form < 0 || form >= PE_BESTIARY_MAX_FORMS || !src || src->kind == PE_MODEL_NONE ||
        src->kind > PE_MODEL_CALLBACK || (src->kind == PE_MODEL_SKIN && (!src->folder || !*src->folder)) ||
        (src->kind == PE_MODEL_CALLBACK && !src->load)) return 0;
    pe_checkpoint_changed();
    s_model[h][form] = *src;
    return 1;
}

static int pe_model(int h, int form, PeModelSource *out) {
    if (!pe_valid(h) || form < 0 || form >= PE_BESTIARY_MAX_FORMS || s_model[h][form].kind == PE_MODEL_NONE) return 0;
    if (out) *out = s_model[h][form];
    return 1;
}

const PeBestiaryApi g_pe_bestiary_api = {
    pe_add, pe_find, pe_info, pe_replace, pe_map, pe_add_resolver, pe_state, pe_kills, pe_unlock,
    pe_set_model, pe_model,
};

/* ---- kills and Scan ---- */
static struct { int tracking; uint8_t entity_id; uint16_t identity; int32_t hp; } s_slot[PE_ENEMY_SLOTS];

static int pe_on_battle(void *ctx, void *user) {
    const int room = pe_room_number();
    int s;
    (void)ctx;
    (void)user;
    if (!pe_game_loaded()) return 0;
    for (s = 0; s < PE_ENEMY_SLOTS; ++s) {
        const EnemyCombatant *core = &g_BattleEnemySlots[s].combatant;
        const uint8_t eid = core->lowAndEntityId.bytes.entityId;
        const int32_t hp = core->hpAlive;
        const uint16_t id = (uint16_t)core->field04.fieldId04;
        if (!g_BattleEnemySlots[s].active) { s_slot[s].tracking = 0; continue; }
        if (!s_slot[s].tracking || s_slot[s].entity_id != eid) {
            s_slot[s].tracking = 1;
            s_slot[s].entity_id = eid;
            s_slot[s].hp = hp;
            s_slot[s].identity = id;
            continue;
        }
        if (s_slot[s].hp > 0 && hp <= 0 && room > 0) {
            const int h = pe_resolve(room, s_slot[s].identity ? s_slot[s].identity : id, core);
            if (h >= 0) pe_kill(h);
        }
        s_slot[s].hp = hp;
        if (id) s_slot[s].identity = id;
    }
    return 0;
}

static int pe_on_spell(void *ctx, void *user) {
    const PeHookSpell *spell = (const PeHookSpell *)ctx;
    const EnemyCombatant *core;
    int h;
    (void)user;
    if (spell->spell != PE_SCAN || !spell->target || spell->target == pe_player() || !pe_game_loaded()) return 0;
    if (!(core = (const EnemyCombatant *)spell->target->core)) return 0;
    if ((h = pe_resolve(pe_room_number(), (uint16_t)core->field04.fieldId04, core)) >= 0) pe_unlock_why(h, PE_BESTIARY_WEAK, "scan");
    return 0;
}

static void pe_field_tick(void) { if (!pe_in_battle()) memset(s_slot, 0, sizeof s_slot); }   /* each battle starts fresh */

/* ---- F1 pages ---- */
static const char *s_sections[PE_MAX_SECTIONS + 1];
static unsigned s_nsections;
static int s_area, s_show, s_sel, s_list[PE_MAX_CREATURES], s_count;
static const char *const s_show_names[] = { "All", "Met", "Not complete" };
static char s_name[80];

/* Areas in the order packs registered them. */
static void pe_sections(void) {
    int h;
    s_sections[0] = "All areas";
    s_nsections = 1;
    for (h = 0; h < s_creatures; ++h) {
        const char *sec = s_creature[h]->section ? s_creature[h]->section : "Other";
        unsigned j = 1;
        while (j < s_nsections && strcmp(s_sections[j], sec)) ++j;
        if (j == s_nsections && s_nsections <= PE_MAX_SECTIONS) s_sections[s_nsections++] = sec;
    }
}
static unsigned pe_area_count(void) { pe_sections(); return s_nsections; }
static const char *pe_area_name(unsigned i) { pe_sections(); return i < s_nsections ? s_sections[i] : ""; }

static void pe_filter(void) {
    int h;
    pe_sections();
    s_count = 0;
    for (h = 0; h < s_creatures; ++h) {
        const unsigned s = pe_state(h);
        const char *sec = s_creature[h]->section ? s_creature[h]->section : "Other";
        if (s_area > 0 && (unsigned)s_area < s_nsections && strcmp(sec, s_sections[s_area])) continue;
        if (s_show == 1 && !(s & PE_BESTIARY_MET)) continue;
        if (s_show == 2 && (s & PE_BESTIARY_WEAK) && (s & PE_BESTIARY_LOOT)) continue;
        s_list[s_count++] = h;
    }
    if (s_sel >= s_count) s_sel = 0;
}

static int pe_current(void) { pe_filter(); return s_count ? s_list[s_sel] : -1; }
static unsigned pe_count(void) { pe_filter(); return s_count ? (unsigned)s_count : 1u; }

static const char *pe_name(unsigned i) {
    const PeCreatureInfo *c;
    unsigned s;
    pe_filter();
    if (!s_count) return "None";
    if (i >= (unsigned)s_count) i = 0;
    c = s_creature[s_list[i]];
    s = pe_state(s_list[i]);
    if (!(s & PE_BESTIARY_MET)) snprintf(s_name, sizeof s_name, "???  (%u/%d)", i + 1, s_count);
    else snprintf(s_name, sizeof s_name, "%s%s%s  (%u/%d)", c->ex ? "EX " : "", c->name,
                  (s & PE_BESTIARY_WEAK) && (s & PE_BESTIARY_LOOT) ? " *" : c->boss ? " (boss)" : "", i + 1, s_count);
    return s_name;
}

static const char *pe_elems(uint8_t bits, char *buf, unsigned size) {
    static const char *const n[5] = { "Heat", "Frost", "Cyanide", "Acid", "Tranq" };
    int i;
    buf[0] = 0;
    for (i = 0; i < 5; ++i)
        if (bits & (1u << i)) snprintf(buf + strlen(buf), size - strlen(buf), "%s%s", buf[0] ? ", " : "", n[i]);
    return buf[0] ? buf : "none";
}

static void pe_info_row(char *out, unsigned size) {
    const int h = pe_current();
    const PeCreatureInfo *c = pe_info(h);
    if (!c) { snprintf(out, size, "-"); return; }
    if (pe_state(h) & PE_BESTIARY_MET)
        snprintf(out, size, "HP %u  Def %u  %s  kills %u", c->hp, c->defense, c->section ? c->section : "", pe_kills(h));
    else snprintf(out, size, "Not met yet  (%s)", c->section ? c->section : "");
}

static void pe_elements_row(char *out, unsigned size) {
    const int h = pe_current();
    const PeCreatureInfo *c = pe_info(h);
    char weak[48], strong[48];
    if (!c) { snprintf(out, size, "-"); return; }
    if (pe_state(h) & PE_BESTIARY_WEAK)
        snprintf(out, size, "Weak: %s  Strong: %s", pe_elems(c->weak, weak, sizeof weak), pe_elems(c->strong, strong, sizeof strong));
    else snprintf(out, size, "Scan, %s or %u Junk", c->boss ? "defeat" : "5 kills", c->price_weak);
}

static void pe_drop_row(char *out, unsigned size) {
    const int h = pe_current();
    const PeCreatureInfo *c = pe_info(h);
    if (!c) snprintf(out, size, "-");
    else if (pe_state(h) & PE_BESTIARY_LOOT) snprintf(out, size, "%s", c->drop ? c->drop : "nothing");
    else snprintf(out, size, "%s%u Junk", c->boss ? "" : "15 kills or ", c->price_loot);
}

static void pe_steal_row(char *out, unsigned size) {
    const int h = pe_current();
    const PeCreatureInfo *c = pe_info(h);
    snprintf(out, size, "%s", c && (pe_state(h) & PE_BESTIARY_LOOT) ? (c->steal ? c->steal : "nothing") : "-");
}

/* Buying: the next locked tier, weaknesses then loot, of a met creature. */
unsigned pe_bestiary_next_price(int h, unsigned *bit) {
    const unsigned s = pe_state(h);
    const PeCreatureInfo *c = pe_info(h);
    *bit = 0;
    if (!c) return 0;
    if (!(s & PE_BESTIARY_WEAK)) { *bit = PE_BESTIARY_WEAK; return c->price_weak; }
    if (!(s & PE_BESTIARY_LOOT)) { *bit = PE_BESTIARY_LOOT; return c->price_loot; }
    return 0;
}

int pe_bestiary_buy(int h) {
    char msg[96];
    unsigned bit, price;
    if (!pe_valid(h)) return 0;
    if (!(pe_state(h) & PE_BESTIARY_MET)) { pe_col_say("Meet it first"); return 0; }
    price = pe_bestiary_next_price(h, &bit);
    if (!bit) { pe_col_say("Already complete"); return 0; }
    if (!pe_col_junk_ready(msg, sizeof msg)) { pe_col_say(msg); return 0; }
    if (!pe_col_spend(price)) {
        snprintf(msg, sizeof msg, "Need %u Junk (have %u)", price, pe_col_junk());
        pe_col_say(msg);
        return 0;
    }
    pe_unlock_why(h, bit, "junk");
    pe_bestiary_seen(h);                                   /* bought while looking at it */
    pe_profile_save();
    return 1;
}

/* NEW: tiers known but not looked at in the Bestiary yet. */
unsigned pe_bestiary_new(int h) { return pe_valid(h) ? pe_state(h) & ~pe_profile_creature_seen(s_creature[h]->key) & 7u : 0u; }
void pe_bestiary_seen(int h) { if (pe_valid(h)) pe_profile_set_creature_seen(s_creature[h]->key, pe_state(h)); }

static void pe_buy(void) { const int h = pe_current(); if (h >= 0) (void)pe_bestiary_buy(h); }

static void pe_buy_label(char *out, unsigned size) {
    const int h = pe_current();
    unsigned bit = 0;
    const unsigned price = h >= 0 ? pe_bestiary_next_price(h, &bit) : 0;
    if (!bit) snprintf(out, size, "Complete");
    else snprintf(out, size, "%s for %u Junk (have %u)", bit == PE_BESTIARY_WEAK ? "Weaknesses" : "Loot", price, pe_col_junk());
}

static void pe_progress(char *out, unsigned size) {
    int h, met = 0, done = 0;
    for (h = 0; h < s_creatures; ++h) {
        const unsigned s = pe_state(h);
        met += (s & PE_BESTIARY_MET) != 0;
        done += (s & (PE_BESTIARY_WEAK | PE_BESTIARY_LOOT)) == (PE_BESTIARY_WEAK | PE_BESTIARY_LOOT);
    }
    snprintf(out, size, "Met %d/%d  Complete %d", met, s_creatures, done);
}

static void pe_result(char *out, unsigned size) { snprintf(out, size, "%s", pe_col_last_said()); }

void pe_bestiary_reset(void) { memset(s_slot, 0, sizeof s_slot); s_sel = 0; }

/* Testing actions change only creatures registered by the loaded packs.
 * Keep real kill counts on unlock; reset clears both discovery and kills. */
static void pe_unlock_all(void) {
    int changed = 0;
    for (int h = 0; h < s_creatures; ++h) {
        unsigned gained = 7u & ~pe_state(h);
        if (!gained) continue;
        pe_profile_set_creature(s_creature[h]->key, 7u, pe_kills(h));
        PeBestiaryEvent e = { h, gained, "debug" };
        pe_event_emit(s_event, &e);
        ++changed;
    }
    pe_profile_save();
    char msg[80];
    snprintf(msg, sizeof msg, "Unlocked %d Bestiary entries", changed);
    pe_col_say(msg);
}
static void pe_reset_all(void) {
    for (int h = 0; h < s_creatures; ++h)
        if (pe_state(h) || pe_kills(h)) pe_profile_set_creature(s_creature[h]->key, 0, 0);
    pe_bestiary_reset(); /* discard pre-reset enemy HP tracking */
    pe_profile_save();
    pe_col_say("Bestiary reset: discoveries, kill counts and NEW marks cleared");
}

/* One creature at a time, for trying the screen's tiers, buying and NEW marks (any creature,
 * met or not, whatever the F1 Bestiary page's filters are). */
static int s_test;
static char s_test_name[80];

static int pe_test(void) { return pe_valid(s_test) ? s_test : -1; }
static unsigned pe_test_count(void) { return s_creatures ? (unsigned)s_creatures : 1u; }
static const char *pe_test_name(unsigned i) {
    const PeCreatureInfo *c = pe_info((int)i);
    if (!c) return "None";
    snprintf(s_test_name, sizeof s_test_name, "%s%s  (%u/%d)", c->ex ? "EX " : "", c->name, i + 1, s_creatures);
    return s_test_name;
}

static void pe_tiers(char *out, unsigned size, unsigned bits) {
    snprintf(out, size, "%s%s%s%s", bits & PE_BESTIARY_MET ? "met " : "", bits & PE_BESTIARY_WEAK ? "weak " : "",
             bits & PE_BESTIARY_LOOT ? "loot" : "", bits ? "" : "none");
}

static void pe_test_state(char *out, unsigned size) {
    const int h = pe_test();
    char known[24], fresh[24];
    if (h < 0) { snprintf(out, size, "-"); return; }
    pe_tiers(known, sizeof known, pe_state(h));
    pe_tiers(fresh, sizeof fresh, pe_bestiary_new(h));
    snprintf(out, size, "Known: %s  Kills %u  NEW: %s", known, pe_kills(h), fresh);
}

static void pe_test_tier(void) {
    const int h = pe_test();
    unsigned bit;
    if (h < 0) return;
    if (!(pe_state(h) & PE_BESTIARY_MET)) bit = PE_BESTIARY_MET;
    else (void)pe_bestiary_next_price(h, &bit);
    if (!bit) { pe_col_say("Already complete"); return; }
    pe_unlock_why(h, bit, "debug");
    pe_profile_save();
}

static void pe_test_kill(void) {
    const int h = pe_test();
    char msg[80];
    if (h < 0) return;
    pe_kill(h);                                            /* the real kill path: 5 and 15 unlock */
    pe_profile_save();
    snprintf(msg, sizeof msg, "%s: %u kills", s_creature[h]->name, pe_kills(h));
    pe_col_say(msg);
}

static void pe_test_reset(void) {
    const int h = pe_test();
    char msg[80];
    if (h < 0) return;
    pe_profile_set_creature(s_creature[h]->key, 0, 0);
    pe_profile_save();
    snprintf(msg, sizeof msg, "%s: back to never met", s_creature[h]->name);
    pe_col_say(msg);
}

static void pe_test_new(void) {
    const int h = pe_test();
    if (h < 0) return;
    if (!pe_state(h)) { pe_col_say("Nothing known to mark NEW"); return; }
    pe_profile_set_creature_seen(s_creature[h]->key, 0);
    pe_profile_save();
    pe_col_say("Marked NEW");
}

static void pe_all_seen(int seen) {
    for (int h = 0; h < s_creatures; ++h) pe_profile_set_creature_seen(s_creature[h]->key, seen ? pe_state(h) : 0);
    pe_profile_save();
    pe_col_say(seen ? "NEW marks cleared" : "Everything known is NEW again");
}
static void pe_clear_new(void) { pe_all_seen(1); }
static void pe_make_new(void) { pe_all_seen(0); }
void pe_bestiary_register(const char *mod_id) {
    s_event = pe_event_id("bestiary.unlock");
    pe_menu_register_action(PE_PAGE_DEBUG_COLLECTION, "Unlock all Bestiary",
        "Testing: reveal every registered creature, weakness and loot entry. Keep kill counts.", pe_unlock_all);
    pe_menu_register_action(PE_PAGE_DEBUG_COLLECTION, "Reset Bestiary",
        "Testing: clear discoveries, kill counts and NEW marks. Keep costumes and Junk.", pe_reset_all);
    pe_menu_register_dynamic_choice(PE_PAGE_DEBUG_COLLECTION, "Test creature",
        "Testing: the creature the actions below change. Left / right.", &s_test, PE_MAX_CREATURES, pe_test_count, pe_test_name);
    pe_menu_register_info(PE_PAGE_DEBUG_COLLECTION, "Test state", "Its tiers, kills and NEW marks.", pe_test_state);
    pe_menu_register_action(PE_PAGE_DEBUG_COLLECTION, "Unlock next tier",
        "Testing: met, then weaknesses, then loot. Free.", pe_test_tier);
    pe_menu_register_action(PE_PAGE_DEBUG_COLLECTION, "Add a kill",
        "Testing: count a kill as a battle would (5 kills: weaknesses, 15: loot).", pe_test_kill);
    pe_menu_register_action(PE_PAGE_DEBUG_COLLECTION, "Reset creature",
        "Testing: this creature back to never met: tiers, kills, NEW.", pe_test_reset);
    pe_menu_register_action(PE_PAGE_DEBUG_COLLECTION, "Mark creature NEW",
        "Testing: show what is known about it as NEW again.", pe_test_new);
    pe_menu_register_action(PE_PAGE_DEBUG_COLLECTION, "Clear all NEW", "Testing: mark everything known as looked at.", pe_clear_new);
    pe_menu_register_action(PE_PAGE_DEBUG_COLLECTION, "Make all NEW", "Testing: mark everything known as NEW.", pe_make_new);
    pe_menu_register_info(PE_PAGE_DEBUG_COLLECTION, "Result", "What the last action did.", pe_result);
    pe_menu_register_dynamic_choice(PE_PAGE_BESTIARY, "Area", "Which creatures to browse.", &s_area, PE_MAX_SECTIONS + 1,
                                    pe_area_count, pe_area_name);
    pe_menu_register_choice(PE_PAGE_BESTIARY, "Show", "All, met only, or not complete.", &s_show, s_show_names, 3);
    pe_menu_register_dynamic_choice(PE_PAGE_BESTIARY, "Creature", "Left / right to browse. * = complete.", &s_sel,
                                    PE_MAX_CREATURES, pe_count, pe_name);
    pe_menu_register_info(PE_PAGE_BESTIARY, "Info", "HP, defense, where, kills.", pe_info_row);
    pe_menu_register_info(PE_PAGE_BESTIARY, "Elements", "Weak and strong elements.", pe_elements_row);
    pe_menu_register_info(PE_PAGE_BESTIARY, "Drop", "What it drops (odds).", pe_drop_row);
    pe_menu_register_info(PE_PAGE_BESTIARY, "Steal", "What can be stolen (odds).", pe_steal_row);
    pe_menu_register_info(PE_PAGE_BESTIARY, "Next tier", "What Buy unlocks: weaknesses, then loot.", pe_buy_label);
    pe_menu_register_action(PE_PAGE_BESTIARY, "Buy next tier", "Spend Junk on the next tier. Kept in every game.", pe_buy);
    pe_menu_register_info(PE_PAGE_BESTIARY, "Progress", "Bestiary totals.", pe_progress);
    pe_menu_register_info(PE_PAGE_BESTIARY, "Result", "What the last action did.", pe_result);
    pe_menu_register_tick(pe_field_tick);
    pe_hook_add(PE_HOOK_BATTLE_UPDATE, pe_on_battle, NULL, PE_PRIORITY_LATE, mod_id);
    pe_hook_add(PE_HOOK_SPELL, pe_on_spell, NULL, PE_PRIORITY_NORMAL, mod_id);
}

#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe_checkpoint_collection.h"
int pe_bestiary_checkpoint_walk(const unsigned char*in,unsigned char*out,int apply){
if(s_checkpoint_epoch==UINT32_MAX)return 0;if(apply&&(!in||!pe_bestiary_checkpoint_walk(in,0,0)))return 0;
PeCheckpointWire w={in,out,0,apply,1};PE_CK_U(w,s_checkpoint_epoch,1,UINT32_MAX-1,1);PE_CK_S(w,s_creatures,0,PE_MAX_CREATURES,1);PE_CK_S(w,s_maps,0,PE_MAX_MAPPINGS,1);PE_CK_S(w,s_resolvers,0,PE_MAX_RESOLVERS,1);PE_CK_S(w,s_event,-1,INT_MAX,1);
for(unsigned i=0;i<PE_ENEMY_SLOTS;i++){PE_CK_S(w,s_slot[i].tracking,0,1,0);PE_CK_U(w,s_slot[i].entity_id,0,255,0);PE_CK_U(w,s_slot[i].identity,0,65535,0);PE_CK_S(w,s_slot[i].hp,INT_MIN,INT_MAX,0);}return w.ok&&w.at==132;
}
#endif
