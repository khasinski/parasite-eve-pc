/* Collection fixture: the production registry, profile and PE pack (each its own translation
 * unit), reached only the way another mod would (services, hooks, F1 rows), with the host and
 * game isolated. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pe_collection.h"
#include "pe_collection_data.h"
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe1/game_state.h"

extern const PeMod pe_mod_pe_collection;

Pe1GameState g_GameState;
GameStateBlock g_GameStateBlock;
BattleEnemySlot g_BattleEnemySlots[7];
struct FieldActor *g_FieldActorListHead;
static int s_room = 374;
static char s_dir[512];
static uint32_t s_junk = 1000;

int pe_game_loaded(void) { return 1; }
int pe_free_field(void) { return !(g_GameStateFlags & 0x2006u) && !(g_GameStateFlagsWord & 0xB600u); }   /* as host/pe_game.c */
int pe_save_loaded(void) { return 1; }
int pe_in_battle(void) { return 0; }
int pe_room_number(void) { return s_room; }
BattleEntity *pe_player(void) { return NULL; }

static PeHookFn s_hook[PE_HOOK_COUNT];
int pe_hook_add(int i, PeHookFn f, void *u, int p, const char *m) { (void)u; (void)p; (void)m; if (i >= 0 && i < PE_HOOK_COUNT) s_hook[i] = f; return 0; }
static int s_events, s_unlock_events;
int pe_event_id(const char *name) { return strcmp(name, "bestiary.unlock") == 0 ? 100 : 101 + s_events++; }
int pe_event_emit(int id, void *ctx) { (void)ctx; if (id == 100) ++s_unlock_events; return 0; }
static struct { const char *name; const void *api; } s_svc[8];
static int s_nsvc;
int pe_service_publish(const char *name, int v, const void *api, const char *m) { (void)v; (void)m; s_svc[s_nsvc].name = name; s_svc[s_nsvc++].api = api; return 0; }
static int junk_enabled(void) { return 1; }
static uint32_t junk_count(void) { return s_junk; }
static int junk_spend(uint32_t n) { if (n > s_junk) return 0; s_junk -= n; return 1; }
static const PeJunkApi s_junk_api = { junk_enabled, junk_count, junk_spend };
const void *pe_service_get(const char *name, int v) {
    int i;
    (void)v;
    if (strcmp(name, "junk") == 0) return &s_junk_api;
    for (i = 0; i < s_nsvc; ++i) if (strcmp(s_svc[i].name, name) == 0) return s_svc[i].api;
    return NULL;
}
int pe_mod_path(char *out, size_t size, const char *file) { snprintf(out, size, "%s/%s", s_dir, file); return 0; }
void pe_mod_log(const char *m, const char *f, ...) { (void)m; (void)f; }
void pe_log(const char *t, const char *f, ...) { (void)t; (void)f; }
void pe_toast(const char *t) { (void)t; }
int pe_menu_register_toggle(const char *s, const char *l, const char *d, int *f, PeMenuPerFrame p) { (void)s; (void)l; (void)d; (void)f; (void)p; return 0; }
static PeMenuAction unlock_costumes, lock_costumes, unlock_bestiary, reset_bestiary;
int pe_menu_register_action(const char *s, const char *l, const char *d, PeMenuAction a) {
    (void)s; (void)d;
    if (!strcmp(l, "Unlock all Bestiary")) unlock_bestiary = a;
    if (!strcmp(l, "Reset Bestiary")) reset_bestiary = a;
    if (!strcmp(l, "Unlock all costumes")) unlock_costumes = a;
    if (!strcmp(l, "Lock all costumes")) lock_costumes = a;
    return 0;
}
int pe_menu_register_choice(const char *s, const char *l, const char *d, int *i, const char *const *n, unsigned c) { (void)s; (void)l; (void)d; (void)i; (void)n; (void)c; return 0; }
static struct { const char *label; PeMenuChoiceCount count; PeMenuChoiceName name; } s_dyn[8];
static int s_ndyn;
int pe_menu_register_dynamic_choice(const char *s, const char *l, const char *d, int *i, unsigned m, PeMenuChoiceCount c, PeMenuChoiceName n) {
    (void)s; (void)d; (void)i; (void)m; s_dyn[s_ndyn].label = l; s_dyn[s_ndyn].count = c; s_dyn[s_ndyn++].name = n; return 0;
}
int pe_menu_register_info(const char *s, const char *l, const char *d, PeMenuText t) { (void)s; (void)l; (void)d; (void)t; return 0; }
int pe_menu_register_tick(PeMenuPerFrame f) { (void)f; return 0; }
int pe_menu_register_live_int(const char *s, const char *l, const char *d, int lo, int hi, int st, PeMenuGet g, PeMenuSet t) {
    (void)s; (void)l; (void)d; (void)lo; (void)hi; (void)st; (void)g; (void)t; return 0;
}
void pe_wardrobe_menu_register(const char *m) { (void)m; }   /* the menu screen: drawing only */
void pe_bestiary_menu_register(const char *m) { (void)m; }   /* the Bestiary screen: drawing only */
void pe_skin_register(const char *m) { (void)m; }            /* the loader: game buffers, VRAM */
int pe_skin_available(const char *folder) { return strcmp(folder, "test_skin") == 0; }
int pe_skin_instant(void) { return 0; }

/* The last entry of an F1 filter row (Area, Group). */
static const char *last_filter(const char *label) {
    int i;
    for (i = 0; i < s_ndyn; ++i)
        if (strcmp(s_dyn[i].label, label) == 0) return s_dyn[i].name(s_dyn[i].count() - 1);
    return "";
}

/* ---- what another mod would do ---- */
static const PeCreatureInfo s_zombie = { "re2/zombie", "Zombie", "Resident Evil 2", 0, 0, 60, 10, 1, 0, "Handgun ammo 50%", "nothing", 5, 10 };
static struct PeModel *zombie_model(void *user) { (void)user; return NULL; }   /* an import's own decoder */
static const PeCreatureInfo s_zombie_rnd = { "re2/zombie", "Zombie", "Resident Evil 2", 0, 0, 60, 10, 2, 0, "Medicine 1 100%", "nothing", 5, 10 };
static const PeCreatureInfo s_impostor = { "re2/licker", "Licker", "Resident Evil 2", 0, 0, 60, 10, 0, 0, "", "", 5, 10 };
static int s_rnd_handle = -1, s_rnd_native;
static int shuffle(int room, unsigned identity, const struct EnemyCombatant *e) { (void)e; return (room == 374 && identity == 0x99) || (s_rnd_native && room == 263 && identity == 0x1A) ? s_rnd_handle : -1; }
static int s_loads[2];
static void claire_load(int wearing) { ++s_loads[wearing ? 1 : 0]; }
static const PeCostume s_claire = { "re2/claire", "Claire (RE2)", "Resident Evil", 25, -1, NULL, claire_load };
static const PeCostume s_bad_bank = { "x/bad", "Bad", "X", 0, 11, NULL, NULL };
static const PeCostume s_skin = { "x/skin", "Skin", "Skins", 10, -1, NULL, NULL, "test_skin" };
static const PeCostume s_skin_missing = { "x/nofiles", "No files", "Skins", 0, -1, NULL, NULL, "missing_skin" };
static const PeCostume s_nothing = { "x/nothing", "Nothing", "Skins", 0, -1, NULL, NULL, NULL };

static void kill_once(int slot, unsigned identity) {
    BattleEnemySlot *e = &g_BattleEnemySlots[slot];
    e->active = 1;
    e->combatant.field04.fieldId04 = (s16)identity;
    e->combatant.lowAndEntityId.bytes.entityId = (u8)(slot + 1);
    e->combatant.hpAlive = 10;
    s_hook[PE_HOOK_BATTLE_UPDATE](NULL, NULL);      /* tracked */
    e->combatant.hpAlive = 0;
    s_hook[PE_HOOK_BATTLE_UPDATE](NULL, NULL);      /* killed */
    e->active = 0;
    s_hook[PE_HOOK_BATTLE_UPDATE](NULL, NULL);
}

/* Exercise the production service/hook path for every extracted native actor
 * key. Deliberately use identical HP/defense: identity must survive scaling and
 * combat-stat changes, including multiple species sharing rank/kind. */
static void native_species_regressions(const PeBestiaryApi *b) {
    BattleEntity actor = {0};
    unsigned before[PE_CREATURES];
    int handles[PE_CREATURES];
    for (int i = 0; i < PE_CREATURES; ++i) {
        handles[i] = b->find(pe_pack_pe1_creature_key(i));
        assert(handles[i] >= 0);
    }
    actor.core = &g_BattleEnemySlots[0].combatant;
    g_FieldActorListHead = (struct FieldActor *)&actor;
    for (int n = 0; n < PE_CREATURE_KEYS; ++n) {
        const PeCreatureKey *k = &g_pe_creature_keys[n];
        s_room = k->map; actor.modelIndex = k->actor;
        for (int i = 0; i < PE_CREATURES; ++i) before[i] = b->kills(handles[i]);
        kill_once(0, k->id);
        for (int i = 0; i < PE_CREATURES; ++i)
            assert(b->kills(handles[i]) == before[i] + (unsigned)(i == k->creature));
        /* EX/progression can change rank; the room/model still identifies
         * this species even when the original rank/kind is no longer present. */
        kill_once(0, k->id ^ 0x0080u);
        for (int i = 0; i < PE_CREATURES; ++i)
            assert(b->kills(handles[i]) == before[i] + 2u * (unsigned)(i == k->creature));
        /* Scan must resolve the same actor without granting another species
         * weaknesses. Existing progress is restored through public profile IO. */
        for (int i = 0; i < PE_CREATURES; ++i)
            pe_profile_set_creature(pe_pack_pe1_creature_key(i), 0, b->kills(handles[i]));
        PeHookSpell scan = {0}; scan.spell = 7; scan.target = &actor;
        s_hook[PE_HOOK_SPELL](&scan, NULL);
        for (int i = 0; i < PE_CREATURES; ++i)
            assert(((b->state(handles[i]) & PE_BESTIARY_WEAK) != 0) == (i == k->creature));
    }
    /* Without an actor, a shared rank/kind cannot identify the species. Do
     * not turn a missing model into the last registered creature's kill. */
    g_FieldActorListHead = NULL; s_room = 263;
    for (int i = 0; i < PE_CREATURES; ++i) before[i] = b->kills(handles[i]);
    kill_once(0, 0x001A);
    for (int i = 0; i < PE_CREATURES; ++i) assert(b->kills(handles[i]) == before[i]);
    /* A later randomizer resolver must still override a real native model. */
    g_FieldActorListHead = (struct FieldActor *)&actor; actor.modelIndex = 7;
    s_rnd_native = 1;
    unsigned imported = b->kills(s_rnd_handle);
    kill_once(0, 0x001A);
    assert(b->kills(s_rnd_handle) == imported + 1);
    for (int i = 0; i < PE_CREATURES; ++i) assert(b->kills(handles[i]) == before[i]);
    s_rnd_native = 0; g_FieldActorListHead = NULL;
    printf("Native Bestiary: %d model-aware kill/Scan mappings; shared species isolated; randomizer precedence retained\n", PE_CREATURE_KEYS);
}

static int find_shop(const PeWardrobeApi *shop, const char *part) {
    int i;
    for (i = 0; i < shop->count(); ++i) if (strstr(shop->name(i), part)) return i;
    return -1;
}

int main(void) {
    const PeBestiaryApi *b;
    const PeCostumesApi *w;
    const PeWardrobeApi *shop;
    char msg[96], path[600];
    unsigned kills;
    int zombie, rat, i, j;
    FILE *f;

    snprintf(s_dir, sizeof s_dir, "%s", getenv("PE_TEST_DIR") ? getenv("PE_TEST_DIR") : ".");
    pe_mod_path(path, sizeof path, "pe_collection.ini");
    remove(path);
    f = fopen(path, "w");                            /* records of a pack that isn't loaded */
    fprintf(f, "creature ff7/cactuar 7 3\ncostume ff7/cloud\nworn ff7/cloud\n");
    fclose(f);

    assert(pe_mod_pe_collection.init() == 0);
    b = pe_service_get("bestiary", 1);
    w = pe_service_get("costumes", 1);
    shop = pe_service_get("wardrobe", 1);
    assert(b && w && shop);

    /* The PE pack: stable, unique keys. */
    assert((rat = b->find("pe1/rat")) >= 0 && strcmp(b->info(rat)->name, "Rat") == 0);
    assert(b->find("pe1/ex_rat") >= 0 || b->find("pe1/rat_2") >= 0);
    for (i = 0; pe_pack_pe1_creature_key(i); ++i)
        for (j = 0; j < i; ++j) assert(strcmp(pe_pack_pe1_creature_key(i), pe_pack_pe1_creature_key(j)) != 0);
    assert(i == 75);

    /* The PE pack's model forms: every creature has a main body (form 0); Eve has three bodies, the
     * Crab's body comes before its claws, and Ultimate Being's 5-vertex script helper is left out. */
    {
        PeModelSource src;
        int eve = b->find("pe1/eve"), crab = b->find("pe1/crab"), ub = b->find("pe1/ultimate_being");
        for (i = 0; pe_pack_pe1_creature_key(i); ++i) assert(b->model(b->find(pe_pack_pe1_creature_key(i)), 0, NULL));
        assert(eve >= 0 && b->model(eve, 0, &src) && src.kind == PE_MODEL_ROOM && src.map == 23 && src.actor == 2);
        assert(b->model(eve, 2, NULL) && !b->model(eve, 3, NULL));
        assert(crab >= 0 && b->model(crab, 0, &src) && src.map == 269 && src.actor == 3);
        assert(ub >= 0 && b->model(ub, 4, NULL) && !b->model(ub, 5, NULL));
    }

    /* An import pack: its own creature, area and mapping; a taken key is refused. */
    assert((zombie = b->add(&s_zombie)) >= 0);
    assert(b->add(&s_zombie) < 0);
    {   /* its own model: another game's decoder; bad sources are refused */
        PeModelSource own = {PE_MODEL_CALLBACK, 0, 0, NULL, zombie_model, NULL}, got;
        PeModelSource no_load = {PE_MODEL_CALLBACK, 0, 0, NULL, NULL, NULL};
        PeModelSource no_folder = {PE_MODEL_SKIN, 0, 0, NULL, NULL, NULL};
        assert(!b->model(zombie, 0, NULL));
        assert(b->set_model(zombie, 0, &own) && b->model(zombie, 0, &got) && got.load == zombie_model);
        assert(!b->set_model(zombie, 1, &no_load) && !b->set_model(zombie, 1, &no_folder));
        assert(!b->set_model(zombie, PE_BESTIARY_MAX_FORMS, &own) && !b->set_model(9999, 0, &own));
    }
    assert(b->map(999, 0x42, zombie));
    s_room = 999;
    for (i = 0; i < 5; ++i) kill_once(0, 0x42);
    assert(b->kills(zombie) == 5 && (b->state(zombie) & 3u) == 3u);
    assert(s_unlock_events == 2);                    /* new entry, then weaknesses */
    assert(strcmp(last_filter("Area"), "Resident Evil 2") == 0);

    /* A randomizer: new info under the same key, and its own enemy lookup asked first. */
    assert(b->replace(zombie, &s_zombie_rnd) && b->info(zombie)->weak == 2);
    assert(!b->replace(zombie, &s_impostor));
    s_rnd_handle = zombie;
    assert(b->add_resolver(shuffle));
    s_room = 374;
    kill_once(1, 0x99);
    assert(b->kills(zombie) == 6);

    /* Costumes: a provider's model gets load(1) at the player load while worn, load(0) once after. */
    assert(w->add(&s_bad_bank) < 0);
    assert(w->add(&s_nothing) < 0);                  /* bank -1 needs a bundle or load() */
    assert(w->add(&s_skin) >= 0 && w->add(&s_skin_missing) >= 0);
    assert(find_shop(shop, "Skin") >= 0 && find_shop(shop, "No files") < 0);   /* no files: hidden */
    assert(w->add(&s_claire) >= 0);
    assert(strcmp(last_filter("Group"), "Resident Evil") == 0);
    assert(w->worn() == NULL);                       /* ff7/cloud isn't loaded: Aya's own clothes */
    assert((i = find_shop(shop, "Claire")) >= 0 && shop->pick(i, msg, sizeof msg) && s_junk == 1000 - 25);
    assert(w->worn() == &s_claire);
    g_GameState.requested_entity_bank = 12;
    s_hook[PE_HOOK_PLAYER_BANK](NULL, NULL);
    assert(s_loads[1] == 1 && g_GameState.requested_entity_bank == 12);
    assert(shop->pick(0, msg, sizeof msg) && w->worn() == NULL);
    s_hook[PE_HOOK_PLAYER_BANK](NULL, NULL);
    s_hook[PE_HOOK_PLAYER_BANK](NULL, NULL);
    assert(s_loads[0] == 1 && s_loads[1] == 1);

    /* Aya's menu lists what is owned and wearable: Claire (bought above), not the unbought outfits. */
    assert(pe_wardrobe_owned_count() == 1 && pe_wardrobe_owned(0) == &s_claire && !pe_wardrobe_owned(1));
    pe_wardrobe_wear_later(&s_claire);
    assert(pe_wardrobe_worn() == &s_claire);
    pe_wardrobe_wear_later(NULL);
    assert(pe_wardrobe_worn() == NULL);

    /* The game's own outfit: the bank is rewritten at the player load. */
    assert((i = find_shop(shop, "T-shirt")) >= 0 && shop->pick(i, msg, sizeof msg));
    g_GameState.loaded_entity_bank = 12;
    s_hook[PE_HOOK_PLAYER_BANK](NULL, NULL);
    assert(g_GameState.requested_entity_bank == 13 && (g_GameState.flags & 0x200000u));

    /* The profile keeps the records of packs that aren't loaded, and reads back. */
    pe_profile_save();
    pe_profile_load();
    assert(pe_profile_creature("ff7/cactuar", &kills) == 7 && kills == 3);
    assert(pe_profile_owned("ff7/cloud") && pe_profile_owned("re2/claire"));
    assert(pe_profile_creature("re2/zombie", &kills) == 3 && kills == 6);
    assert(strcmp(pe_profile_worn(), w->worn()->key) == 0);
    remove(path);

    /* Testing buttons affect registered costume ownership only, persist it,
     * and never spend Junk or bypass missing-asset checks. */
    assert(unlock_costumes && lock_costumes);
    uint32_t junk_before = s_junk;
    unlock_costumes();
    assert(pe_profile_owned("re2/claire") && pe_profile_owned("x/skin") && pe_profile_owned("x/nofiles"));
    assert(find_shop(shop, "No files") < 0 && s_junk == junk_before);
    int owned_before = pe_wardrobe_owned_count();
    unlock_costumes();
    assert(pe_wardrobe_owned_count() == owned_before && s_junk == junk_before);
    pe_profile_load();
    assert(pe_profile_owned("x/skin") && pe_profile_owned("x/nofiles"));
    assert(pe_profile_creature("re2/zombie", &kills) == 3 && kills == 6);
    lock_costumes();
    assert(!*pe_profile_worn() && !w->worn());
    assert(!pe_profile_owned("re2/claire") && !pe_profile_owned("x/skin") && !pe_profile_owned("x/nofiles"));
    assert(pe_profile_owned("ff7/cloud")); /* unloaded pack is preserved */
    assert(pe_profile_creature("re2/zombie", &kills) == 3 && kills == 6 && s_junk == junk_before);
    pe_profile_load();
    assert(!pe_profile_owned("re2/claire") && !*pe_profile_worn());
    lock_costumes(); /* idempotent */
    assert(s_junk == junk_before && pe_profile_owned("ff7/cloud"));
    remove(path);
    /* Bestiary testing actions persist discovery independently of costumes,
     * keep true kills on unlock, and clear stale battle tracking on reset. */
    assert(unlock_bestiary && reset_bestiary);
    pe_profile_own("re2/claire");
    pe_profile_set_worn("re2/claire");
    unsigned kills_before = b->kills(zombie);
    junk_before = s_junk;
    unlock_bestiary();
    assert(b->state(zombie) == 7 && b->kills(zombie) == kills_before && b->state(rat) == 7);
    int events_before = s_unlock_events;
    unlock_bestiary();
    assert(s_unlock_events == events_before);
    pe_profile_load();
    assert(b->state(zombie) == 7 && b->kills(zombie) == kills_before);
    assert(pe_profile_owned("re2/claire") && !strcmp(pe_profile_worn(), "re2/claire"));
    g_BattleEnemySlots[0].active = 1;
    g_BattleEnemySlots[0].combatant.lowAndEntityId.bytes.entityId = 1;
    g_BattleEnemySlots[0].combatant.field04.fieldId04 = 0x99;
    g_BattleEnemySlots[0].combatant.hpAlive = 10;
    s_hook[PE_HOOK_BATTLE_UPDATE](NULL, NULL);
    reset_bestiary();
    assert(b->state(zombie) == 0 && b->kills(zombie) == 0 && b->state(rat) == 0 && b->kills(rat) == 0);
    g_BattleEnemySlots[0].combatant.hpAlive = 0;
    s_hook[PE_HOOK_BATTLE_UPDATE](NULL, NULL);
    assert(b->kills(zombie) == 0); /* death started before reset is not credited */
    reset_bestiary();
    pe_profile_load();
    assert(b->state(zombie) == 0 && b->kills(zombie) == 0);
    assert(pe_profile_owned("re2/claire") && !strcmp(pe_profile_worn(), "re2/claire"));
    assert(pe_profile_creature("ff7/cactuar", &kills) == 7 && kills == 3 && s_junk == junk_before);
    kill_once(0, 0x99);
    assert(b->state(zombie) == 1 && b->kills(zombie) == 1);

    /* NEW marks: an older line (no seen number) is all looked at; a new tier is NEW until looked at,
     * and kept across a save. Buying: met creatures only, the next tier, paid in Junk. */
    assert(pe_profile_creature_seen("ff7/cactuar") == 7);
    assert(pe_bestiary_new(zombie) == PE_BESTIARY_MET);
    pe_bestiary_seen(zombie);
    assert(pe_bestiary_new(zombie) == 0);
    {
        unsigned bit, price = pe_bestiary_next_price(zombie, &bit), junk = s_junk = 5000;
        assert(bit == PE_BESTIARY_WEAK && price == b->info(zombie)->price_weak);
        assert(pe_bestiary_buy(zombie) && s_junk == junk - price);
        assert(b->state(zombie) == 3 && pe_bestiary_new(zombie) == 0);   /* bought while looking */
        pe_profile_set_creature_seen("re2/zombie", 1);
        assert(pe_bestiary_new(zombie) == PE_BESTIARY_WEAK);
        pe_profile_save();
        pe_profile_load();
        assert(pe_bestiary_new(zombie) == PE_BESTIARY_WEAK);
        pe_profile_set_creature("re2/zombie", 0, 0);
        assert(!pe_bestiary_buy(zombie) && s_junk == junk - price);       /* never met: no sale */
    }
    remove(path);
    /* The psxrecomp build's pe_collection.dat (by index) is imported once through the PE keys. */
    {
        typedef struct { uint32_t magic, version; uint64_t skins; uint8_t worn, reserved[7]; uint8_t creature[128]; uint32_t checksum; } PeProfile;
        PeProfile v1;
        char old[600];
        uint32_t sum = 0x5A17C0DEu;
        size_t k;
        memset(&v1, 0, sizeof v1);
        v1.magic = 0x4C434550u;
        v1.version = 1u;
        v1.skins = 1u | (1u << 2);                   /* Aya, T-shirt */
        v1.worn = 2;
        v1.creature[1] = (uint8_t)(3u | (6u << 3));  /* the Rat: met, weaknesses, 6 kills */
        assert(sizeof v1 == 160);
        for (k = 0; k < offsetof(PeProfile, checksum); ++k)   /* everything before the checksum */ sum = (sum << 5) + sum + ((uint8_t *)&v1)[k];
        v1.checksum = sum;
        pe_mod_path(old, sizeof old, "pe_collection.dat");
        f = fopen(old, "wb");
        fwrite(&v1, 1, sizeof v1, f);
        fclose(f);
        pe_profile_load();
        assert(pe_profile_creature(pe_pack_pe1_creature_key(1), &kills) == 3 && kills == 6);
        assert(pe_profile_owned(pe_pack_pe1_costume_key(2)) && strcmp(pe_profile_worn(), pe_pack_pe1_costume_key(2)) == 0);
        remove(old);
        remove(path);
    }
    native_species_regressions(b);
    puts("Collection checks passed: PE pack keys, import creatures and costumes, resolvers, replace, events, profile, recomp import.");
    return 0;
}
