/* Registry fixture: the production registry (mods/host/pe_registry.c) driven through the bridge
 * findings' D6 §11.6 worked examples and its limits, with the mod host isolated. Order-sensitive
 * cases run both ways round and must come out the same. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_mod_host.h"
#include "pe_registry.h"

static const char *s_current;                      /* the mod whose init is "running" */
static const char *s_stopped[4];                   /* mods not running */
const char *pe_mod_current(void) { return s_current; }
int pe_mod_is_running(const char *id) {
    int i;
    for (i = 0; i < 4; ++i) if (s_stopped[i] && !strcmp(s_stopped[i], id)) return 0;
    return 1;
}
void pe_mod_log(const char *m, const char *f, ...) { (void)m; (void)f; }

static void as(const char *mod) { s_current = mod; }

static PeRegResult add(int domain, const char *batch, const char *key, const PeRegRef *refs, int nrefs) {
    PeRegEntry e = {(uint8_t)domain, key, key, (uint32_t)strlen(key) + 1, refs, (uint8_t)nrefs};
    return pe_reg_submit(batch, &e);
}

static PeRegResult result(const char *owner, const char *batch) { return pe_reg_batch_result(owner, batch, NULL, 0); }

/* A registers 20 definitions, B 30, in either order: one ready set, one canonical order. */
static void many(int b_first, char out[64][48]) {
    char key[48];
    int i, pass;
    pe_registry_reset();
    for (pass = 0; pass < 2; ++pass) {
        const int b = pass == !b_first;
        as(b ? "mod_b" : "mod_a");
        assert(pe_reg_batch("base", 1, NULL) == PE_REG_OK);
        for (i = b ? 29 : 19; i >= 0; --i) {             /* reversed within each mod too */
            snprintf(key, sizeof key, "%s/item%02d", b ? "mod_b" : "mod_a", i);
            assert(add(PE_REG_CONTENT, "base", key, NULL, 0) == PE_REG_PENDING);
        }
    }
    as(NULL);
    pe_registry_seal();
    assert(pe_reg_count(PE_REG_CONTENT) == 50);
    for (i = 0; i < 50; ++i) snprintf(out[i], 48, "%s", pe_reg_key(PE_REG_CONTENT, i));
}

/* The Shop domain over its whole limit while Content fits, mods in either order. */
static void shop_overflow(int reversed) {
    char key[48];
    int m, i;
    pe_registry_reset();
    for (m = 0; m < 5; ++m) {
        char mod[16];
        const int which = reversed ? 4 - m : m;
        snprintf(mod, sizeof mod, "shop%d", which);
        as(mod);
        assert(pe_reg_batch("items", 1, NULL) == PE_REG_OK);
        assert(pe_reg_batch("offers", 0, NULL) == PE_REG_OK);
        snprintf(key, sizeof key, "%s/thing", mod);
        assert(add(PE_REG_CONTENT, "items", key, NULL, 0) == PE_REG_PENDING);
        for (i = 0; i < 250; ++i) {                      /* 5 x 250 = 1250 offers: over the 1024 limit */
            snprintf(key, sizeof key, "%s/offer%03d", mod, i);
            (void)add(PE_REG_SHOP, "offers", key, NULL, 0);
        }
    }
    as(NULL);
    pe_registry_seal();
    assert(!pe_reg_domain_ready(PE_REG_SHOP) && pe_reg_count(PE_REG_SHOP) == 0);
    assert(pe_reg_domain_ready(PE_REG_CONTENT) && pe_reg_count(PE_REG_CONTENT) == 5);
    for (m = 0; m < 5; ++m) {
        char mod[16];
        snprintf(mod, sizeof mod, "shop%d", m);
        assert(result(mod, "items") == PE_REG_OK && result(mod, "offers") == PE_REG_CAPACITY);
    }
}

int main(void) {
    static char order1[64][48], order2[64][48];
    char why[128];
    int i;

    /* 1. Order doesn't matter. */
    many(0, order1);
    many(1, order2);
    for (i = 0; i < 50; ++i) assert(!strcmp(order1[i], order2[i]));
    assert(!strcmp(order1[0], "mod_a/item00") && !strcmp(order1[49], "mod_b/item29"));

    /* 2. A key declared twice fails every claimant's batch: nothing wins. */
    pe_registry_reset();
    as("mod_a");
    assert(pe_reg_batch("one", 0, NULL) == PE_REG_OK && pe_reg_batch("two", 0, NULL) == PE_REG_OK && pe_reg_batch("other", 0, NULL) == PE_REG_OK);
    assert(add(PE_REG_CONTENT, "one", "mod_a/sword", NULL, 0) == PE_REG_PENDING);
    assert(add(PE_REG_CONTENT, "two", "mod_a/sword", NULL, 0) == PE_REG_PENDING);   /* a different payload, too */
    assert(add(PE_REG_CONTENT, "other", "mod_a/shield", NULL, 0) == PE_REG_PENDING);
    assert(add(PE_REG_EFFECT, "other", "mod_a/sword", NULL, 0) == PE_REG_PENDING);  /* another domain: no clash */
    /* 3. B can't claim A's namespace, nor reach for an owner by name. */
    as("mod_b");
    assert(pe_reg_batch("base", 1, NULL) == PE_REG_OK);
    assert(add(PE_REG_CONTENT, "base", "mod_a/sword", NULL, 0) == PE_REG_BAD_ARGUMENT);
    assert(add(PE_REG_CONTENT, "base", "Mod_B/Upper", NULL, 0) == PE_REG_BAD_ARGUMENT);       /* not canonical */
    assert(add(PE_REG_CONTENT, "base", "mod_b/a_name_far_too_long_for_the_registry_limit", NULL, 0) == PE_REG_BAD_ARGUMENT);
    assert(add(PE_REG_CONTENT, "nobatch", "mod_b/x", NULL, 0) == PE_REG_BAD_ARGUMENT);
    assert(add(PE_REG_CONTENT, "base", "mod_b/ok", NULL, 0) == PE_REG_PENDING);
    as(NULL);
    assert(add(PE_REG_CONTENT, "base", "mod_b/late", NULL, 0) == PE_REG_WRONG_PHASE);      /* outside an init */
    assert(result("mod_a", "one") == PE_REG_PENDING);                                         /* a receipt, not ready */
    assert(!pe_reg_find(PE_REG_CONTENT, "mod_a/shield", NULL, NULL));                          /* nothing before the seal */
    pe_registry_seal();
    assert(pe_reg_batch_result("mod_a", "one", why, sizeof why) == PE_REG_DUPLICATE_ID && strstr(why, "mod_a/sword"));
    assert(result("mod_a", "two") == PE_REG_DUPLICATE_ID && result("mod_a", "other") == PE_REG_OK);
    assert(!pe_reg_find(PE_REG_CONTENT, "mod_a/sword", NULL, NULL) && pe_reg_find(PE_REG_EFFECT, "mod_a/sword", NULL, NULL));
    {
        uint32_t size;
        const char *owner;
        const char *data = pe_reg_find(PE_REG_CONTENT, "mod_b/ok", &size, &owner);
        assert(data && !strcmp(data, "mod_b/ok") && size == 9 && !strcmp(owner, "mod_b"));   /* the payload, copied */
    }
    as("mod_b");
    assert(pe_reg_batch("late", 0, NULL) == PE_REG_WRONG_PHASE);                             /* after the seal */
    assert(add(PE_REG_CONTENT, "base", "mod_b/late", NULL, 0) == PE_REG_WRONG_PHASE);
    as(NULL);
    s_stopped[0] = "mod_b";                                                                   /* checked at use */
    assert(!pe_reg_find(PE_REG_CONTENT, "mod_b/ok", NULL, NULL));
    s_stopped[0] = NULL;

    /* 4. Content is ready; its optional Shop adapter referring to Economy, which isn't there, isn't. */
    pe_registry_reset();
    as("packs");
    {
        static const PeRegRef economy[] = {{PE_REG_CONTENT, "pe_economy/junk"}};
        assert(pe_reg_batch("content", 1, NULL) == PE_REG_OK);
        assert(pe_reg_batch("shop", 0, (const char *const[]){"content", NULL}) == PE_REG_OK);
        assert(add(PE_REG_CONTENT, "content", "packs/coat", NULL, 0) == PE_REG_PENDING);
        assert(add(PE_REG_SHOP, "shop", "packs/coat_offer", economy, 1) == PE_REG_PENDING);
    }
    as(NULL);
    pe_registry_seal();
    assert(result("packs", "content") == PE_REG_OK && result("packs", "shop") == PE_REG_MISSING_REFERENCE);
    assert(pe_reg_find(PE_REG_CONTENT, "packs/coat", NULL, NULL) && !pe_reg_find(PE_REG_SHOP, "packs/coat_offer", NULL, NULL));

    /* 5. Passive cross-references between two mods resolve together; a missing external one fails
     *    its batch, and what requires that batch; a base failure takes the mod's other batches. */
    pe_registry_reset();
    {
        static const PeRegRef to_b[] = {{PE_REG_CONTENT, "mod_b/gun"}}, to_a[] = {{PE_REG_CONTENT, "mod_a/ammo"}};
        static const PeRegRef ghost[] = {{PE_REG_EFFECT, "nobody/ghost"}};
        as("mod_a");
        assert(pe_reg_batch("base", 1, NULL) == PE_REG_OK);
        assert(add(PE_REG_CONTENT, "base", "mod_a/ammo", to_b, 1) == PE_REG_PENDING);
        as("mod_b");
        assert(pe_reg_batch("base", 1, NULL) == PE_REG_OK);
        assert(add(PE_REG_CONTENT, "base", "mod_b/gun", to_a, 1) == PE_REG_PENDING);
        as("mod_c");
        assert(pe_reg_batch("extra", 0, NULL) == PE_REG_OK);
        assert(pe_reg_batch("needs_extra", 0, (const char *const[]){"extra", NULL}) == PE_REG_OK);
        assert(pe_reg_batch("alone", 0, NULL) == PE_REG_OK);
        assert(add(PE_REG_CONTENT, "extra", "mod_c/x", ghost, 1) == PE_REG_PENDING);
        assert(add(PE_REG_CONTENT, "needs_extra", "mod_c/y", NULL, 0) == PE_REG_PENDING);
        assert(add(PE_REG_CONTENT, "alone", "mod_c/z", NULL, 0) == PE_REG_PENDING);
        as("mod_d");
        assert(pe_reg_batch("base", 1, NULL) == PE_REG_OK);
        assert(pe_reg_batch("more", 0, NULL) == PE_REG_OK);
        assert(add(PE_REG_CONTENT, "base", "mod_d/broken", ghost, 1) == PE_REG_PENDING);
        assert(add(PE_REG_CONTENT, "more", "mod_d/fine", NULL, 0) == PE_REG_PENDING);
    }
    as(NULL);
    pe_registry_seal();
    assert(result("mod_a", "base") == PE_REG_OK && result("mod_b", "base") == PE_REG_OK);
    assert(result("mod_c", "extra") == PE_REG_MISSING_REFERENCE && result("mod_c", "needs_extra") == PE_REG_REQUIRES_FAILED);
    assert(result("mod_c", "alone") == PE_REG_OK);
    assert(result("mod_d", "base") == PE_REG_MISSING_REFERENCE && result("mod_d", "more") == PE_REG_REQUIRES_FAILED);

    /* 6. Batches requiring each other: a cycle, refused; an independent batch stays ready. */
    pe_registry_reset();
    as("mod_a");
    assert(pe_reg_batch("p", 0, (const char *const[]){"mod_b:q", NULL}) == PE_REG_OK);
    assert(pe_reg_batch("free", 0, NULL) == PE_REG_OK);
    assert(add(PE_REG_CONTENT, "p", "mod_a/p", NULL, 0) == PE_REG_PENDING);
    assert(add(PE_REG_CONTENT, "free", "mod_a/free", NULL, 0) == PE_REG_PENDING);
    as("mod_b");
    assert(pe_reg_batch("q", 0, (const char *const[]){"mod_a:p", NULL}) == PE_REG_OK);
    assert(pe_reg_batch("unknown", 0, (const char *const[]){"mod_z:nothing", NULL}) == PE_REG_OK);
    assert(add(PE_REG_CONTENT, "q", "mod_b/q", NULL, 0) == PE_REG_PENDING);
    as(NULL);
    pe_registry_seal();
    assert(result("mod_a", "p") == PE_REG_CYCLE && result("mod_b", "q") == PE_REG_CYCLE);
    assert(result("mod_a", "free") == PE_REG_OK && result("mod_b", "unknown") == PE_REG_REQUIRES_FAILED);

    /* 7. Shop over its whole limit, Content within: Shop unavailable, Content ready, either order. */
    shop_overflow(0);
    shop_overflow(1);

    /* 8. A mod over its own limit in a domain loses its whole contribution there, not all but the
     *    last; another domain of the same mod is unaffected. Effects allow 64 per mod. */
    pe_registry_reset();
    as("greedy");
    assert(pe_reg_batch("first", 0, NULL) == PE_REG_OK);
    assert(pe_reg_batch("second", 0, NULL) == PE_REG_OK);
    assert(pe_reg_batch("content", 0, NULL) == PE_REG_OK);
    for (i = 0; i < 65; ++i) {
        char key[48];
        snprintf(key, sizeof key, "greedy/e%02d", i);
        assert(add(PE_REG_EFFECT, i < 40 ? "first" : "second", key, NULL, 0) == (i < 64 ? PE_REG_PENDING : PE_REG_CAPACITY));
    }
    assert(add(PE_REG_CONTENT, "content", "greedy/ok", NULL, 0) == PE_REG_PENDING);
    as(NULL);
    pe_registry_seal();
    assert(result("greedy", "first") == PE_REG_CAPACITY && result("greedy", "second") == PE_REG_CAPACITY);
    assert(pe_reg_count(PE_REG_EFFECT) == 0 && pe_reg_domain_ready(PE_REG_EFFECT));
    assert(result("greedy", "content") == PE_REG_OK);

    /* 9. A mod that isn't running (off, or its init failed) publishes nothing. */
    pe_registry_reset();
    as("sleepy");
    assert(pe_reg_batch("base", 1, NULL) == PE_REG_OK);
    assert(add(PE_REG_CONTENT, "base", "sleepy/x", NULL, 0) == PE_REG_PENDING);
    as(NULL);
    s_stopped[0] = "sleepy";
    pe_registry_seal();
    assert(result("sleepy", "base") == PE_REG_INACTIVE && pe_reg_count(PE_REG_CONTENT) == 0);
    s_stopped[0] = NULL;

    /* 10. More than 32 mods taking part: nothing this start, rather than the first 32. */
    pe_registry_reset();
    for (i = 0; i < 33; ++i) {
        static char mods[33][16];
        char key[48];
        snprintf(mods[i], sizeof mods[i], "m%02d", i);
        as(mods[i]);
        if (i < 32) {
            assert(pe_reg_batch("base", 1, NULL) == PE_REG_OK);
            snprintf(key, sizeof key, "m%02d/x", i);
            assert(add(PE_REG_CONTENT, "base", key, NULL, 0) == PE_REG_PENDING);
        } else assert(pe_reg_batch("base", 1, NULL) == PE_REG_CAPACITY);
    }
    as(NULL);
    pe_registry_seal();
    assert(pe_reg_count(PE_REG_CONTENT) == 0 && !pe_reg_domain_ready(PE_REG_CONTENT));
    assert(result("m00", "base") == PE_REG_CAPACITY);
    assert(pe_reg_generation() > 0);

    pe_registry_reset();
    puts("Registry checks passed: order, duplicates, namespaces, phases, optional adapters, references, cycles, limits, inactive mods, owner cap.");
    return 0;
}
