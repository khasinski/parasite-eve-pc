/* The shared registry: collect, seal, publish. See pe_registry.h and bridge findings D6 Ã‚Â§11.
 *
 * Staging is bounded: every submission is counted against its mod's and its domain's limits before
 * anything is copied, and nothing more is copied once a limit is passed (only the counts go on, so
 * the outcome doesn't depend on order). The seal then works on the whole declared set:
 *   1. too many mods taking part: nothing is published;
 *   2. a domain over its limits: unavailable, with every batch holding one of its entries;
 *   3. a mod over its limits in a domain: every one of its batches holding such an entry fails;
 *   4. a mod not running (off, failed or missing what it needs): all its batches fail;
 *   5. a key claimed twice in a domain: every claimant's batch fails;
 *   6. batch requirements that are unknown, or cycles, fail;
 *   7. then, until nothing changes: a batch fails if a batch it requires failed, if an entry's
 *      reference names a missing or failed entry, or if its mod's base (a required batch) failed.
 * What is left is published in one step, in canonical order. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include "pe_mod_host.h"
#include "pe_registry.h"

const PeRegLimits g_pe_reg_limits[PE_REG_DOMAINS] = {
    {256, 64u << 10, 1024, 256u << 10},     /* content definitions */
    {64, 16u << 10, 256, 64u << 10},        /* equipment effects */
    {128, 16u << 10, 512, 64u << 10},       /* Catalog entries */
    {256, 32u << 10, 1024, 128u << 10},     /* shop offers */
};

#define PE_REG_MAX_ENTRIES (1024 + 256 + 512 + 1024)
#define PE_REG_MAX_ALL_BATCHES (PE_REG_MAX_OWNERS * PE_REG_MAX_BATCHES)

static const char *const s_domain_name[PE_REG_DOMAINS] = {"content", "effect", "catalog", "shop"};

typedef struct {
    char id[PE_REG_KEY_MAX + 1];
    uint32_t count[PE_REG_DOMAINS], bytes[PE_REG_DOMAINS];
    int over[PE_REG_DOMAINS];
    int nbatches;
} PeOwner;

typedef struct {
    int owner, required, nreq;
    char name[PE_REG_BATCH_MAX + 1];
    char req[PE_REG_MAX_REQUIRES][PE_REG_KEY_MAX + PE_REG_BATCH_MAX + 2];
    int req_at[PE_REG_MAX_REQUIRES];
    int tried[PE_REG_DOMAINS];           /* submitted to (copied or not): limits judge these */
    PeRegResult result;
    char why[96];
} PeBatch;

typedef struct {
    int batch;
    uint8_t domain, nrefs;
    char key[PE_REG_KEY_MAX + 1];
    void *data;
    uint32_t size, schema, unique_slot;
    PeRegRef *refs;
    char (*ref_key)[PE_REG_KEY_MAX + 1];
    int *ref_at;         /* the target entry, -1 if none */
} PeEntryRec;

enum { PHASE_COLLECT, PHASE_SEAL, PHASE_RUN };
static int s_phase = PHASE_COLLECT;
static uint32_t s_generation;
static int s_readonly;
static int s_unavailable;                /* the whole registry, this start */

static PeOwner s_owner[PE_REG_MAX_OWNERS];
static int s_owners, s_owner_overflow;
static PeBatch *s_batch[PE_REG_MAX_ALL_BATCHES];
static int s_batches;
static PeEntryRec *s_entry[PE_REG_MAX_ENTRIES];
static int s_entries;
static uint32_t s_dom_count[PE_REG_DOMAINS], s_dom_bytes[PE_REG_DOMAINS];
static int s_dom_over[PE_REG_DOMAINS];

/* Affinity is claimed during host bootstrap/reset, before any provider init. A TLS address is
 * portable across supported C11 platforms; atomics keep a rejected worker off mutable state. */
static _Thread_local unsigned char s_thread_marker;
static atomic_uintptr_t s_thread;
static int pe_registry_thread(void) {
    uintptr_t current = (uintptr_t)&s_thread_marker, expected = 0;
    atomic_compare_exchange_strong(&s_thread, &expected, current);
    return atomic_load(&s_thread) == current;
}

/* Include record/batch/index storage, not only payload bytes. Reserve 64 KiB for static tables
 * and bounded graph scratch; heap plus that reserve never exceeds the 1 MiB technical ceiling. */
#define PE_REG_HEAP_LIMIT ((1u << 20) - (64u << 10))
_Static_assert(sizeof s_owner + sizeof s_batch + sizeof s_entry +
               3u * PE_REG_MAX_ALL_BATCHES * sizeof(int) + 4096u <= (64u << 10),
               "registry static tables exceed reserved metadata budget");
typedef union { max_align_t alignment; size_t bytes; } PeRegAllocation;
static size_t s_heap_bytes;
static void *pe_reg_alloc(size_t n, int zero) {
    PeRegAllocation *h;
    if (n > PE_REG_HEAP_LIMIT - sizeof *h || s_heap_bytes > PE_REG_HEAP_LIMIT - sizeof *h - n) { s_unavailable = 1; return NULL; }
    h = zero ? calloc(1, sizeof *h + n) : malloc(sizeof *h + n);
    if (!h) { s_unavailable = 1; return NULL; }
    h->bytes = sizeof *h + n;
    s_heap_bytes += h->bytes;
    return h + 1;
}
static void pe_reg_free(void *p) {
    PeRegAllocation *h;
    if (!p) return;
    h = (PeRegAllocation *)p - 1;
    s_heap_bytes -= h->bytes;
    free(h);
}
static void pe_fail(int b, PeRegResult why, const char *fmt, const char *a, const char *c);

/* Published: ready entries per domain, by key (lookups) and in canonical order (iteration). */
static int *s_by_key[PE_REG_DOMAINS], *s_canon[PE_REG_DOMAINS];
static int s_ready[PE_REG_DOMAINS];

const char *pe_reg_result_name(PeRegResult r) {
    static const char *const names[] = {"ok", "pending", "unavailable", "inactive", "wrong thread", "wrong phase",
        "bad version", "bad argument", "duplicate id", "capacity", "stale generation", "unsupported", "conflict",
        "unsaveable", "missing reference", "requires a failed batch", "requirement cycle"};
    return (unsigned)r < sizeof names / sizeof names[0] ? names[r] : "?";
}

/* ---- names ---- */
static int pe_key_ok(const char *key) {
    size_t n, i;
    if (!key || !(n = strlen(key)) || n > PE_REG_KEY_MAX) return 0;
    for (i = 0; i < n; ++i) {
        const char c = key[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == '/')) return 0;
    }
    return 1;
}

static int pe_batch_name_ok(const char *b) {
    size_t n, i;
    if (!b || !(n = strlen(b)) || n > PE_REG_BATCH_MAX) return 0;
    for (i = 0; i < n; ++i) {
        const char c = b[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return 0;
    }
    return 1;
}

/* The running init's mod, as an owner (added on first use); -1 outside an init or past the cap. */
static int pe_owner_now(void) {
    const char *id = pe_mod_current();
    int i;
    if (!id) return -1;
    for (i = 0; i < s_owners; ++i) if (strcmp(s_owner[i].id, id) == 0) return i;
    if (s_owners == PE_REG_MAX_OWNERS) { s_owner_overflow = 1; return -2; }
    if (strlen(id) > PE_REG_KEY_MAX) return -1;
    memset(&s_owner[s_owners], 0, sizeof s_owner[0]);
    snprintf(s_owner[s_owners].id, sizeof s_owner[0].id, "%s", id);
    return s_owners++;
}

static int pe_find_batch(int owner, const char *name) {
    int i;
    for (i = 0; i < s_batches; ++i) if (s_batch[i]->owner == owner && strcmp(s_batch[i]->name, name) == 0) return i;
    return -1;
}

/* ---- COLLECT ---- */
PeRegResult pe_reg_batch(const char *batch, int required, const char *const *requires) {
    PeBatch *b;
    int owner, n = 0;
    if (!pe_registry_thread()) return PE_REG_WRONG_THREAD;
    if (s_readonly || s_phase != PHASE_COLLECT || !pe_mod_current()) return PE_REG_WRONG_PHASE;
    if ((owner = pe_owner_now()) == -2) return PE_REG_CAPACITY;
    if (owner < 0 || !pe_batch_name_ok(batch)) return PE_REG_BAD_ARGUMENT;
    if (pe_find_batch(owner, batch) >= 0) return PE_REG_DUPLICATE_ID;
    if (s_owner[owner].nbatches == PE_REG_MAX_BATCHES || s_batches == PE_REG_MAX_ALL_BATCHES) return PE_REG_CAPACITY;
    while (requires && requires[n]) {
        if (n == PE_REG_MAX_REQUIRES || strlen(requires[n]) >= sizeof b->req[0]) return PE_REG_BAD_ARGUMENT;
        ++n;
    }
    if (!(b = pe_reg_alloc(sizeof *b, 1))) { s_unavailable = 1; return PE_REG_CAPACITY; }
    b->owner = owner;
    b->required = required != 0;
    snprintf(b->name, sizeof b->name, "%s", batch);
    for (b->nreq = 0; b->nreq < n; ++b->nreq) snprintf(b->req[b->nreq], sizeof b->req[0], "%s", requires[b->nreq]);
    b->result = PE_REG_PENDING;
    s_batch[s_batches++] = b;
    ++s_owner[owner].nbatches;
    return PE_REG_OK;
}

static uint32_t pe_entry_bytes(const PeRegEntry *e) {
    uint32_t bytes = (uint32_t)strlen(e->key) + 1u + e->size;
    int i;
    for (i = 0; i < e->nrefs; ++i) bytes += (uint32_t)strlen(e->refs[i].key) + 2u;
    return bytes;
}

PeRegResult pe_reg_submit(const char *batch, const PeRegEntry *e) {
    PeOwner *o;
    PeEntryRec *r;
    size_t ns;
    uint32_t bytes;
    int owner, b, i;
    if (!pe_registry_thread()) return PE_REG_WRONG_THREAD;
    if (s_readonly || s_phase != PHASE_COLLECT || !pe_mod_current()) return PE_REG_WRONG_PHASE;
    if ((owner = pe_owner_now()) == -2) return PE_REG_CAPACITY;
    if (owner < 0 || !batch || !e || e->size > g_pe_reg_limits[e->domain < PE_REG_DOMAINS ? e->domain : 0].owner_bytes || e->domain >= PE_REG_DOMAINS || !pe_key_ok(e->key) || (e->size && !e->data) ||
        e->nrefs > PE_REG_MAX_REFS || (e->nrefs && !e->refs))
        return PE_REG_BAD_ARGUMENT;
    o = &s_owner[owner];
    ns = strlen(o->id);
    if (strncmp(e->key, o->id, ns) != 0 || e->key[ns] != '/' || !e->key[ns + 1]) return PE_REG_BAD_ARGUMENT;   /* your namespace */
    for (i = 0; i < e->nrefs; ++i)
        if (e->refs[i].domain >= PE_REG_DOMAINS || !pe_key_ok(e->refs[i].key)) return PE_REG_BAD_ARGUMENT;
    if ((b = pe_find_batch(owner, batch)) < 0) return PE_REG_BAD_ARGUMENT;
    /* Count first, whatever happens to the copy, so limits judge the whole declared set. */
    s_batch[b]->tried[e->domain] = 1;
    bytes = pe_entry_bytes(e);
    o->count[e->domain] += 1;
    o->bytes[e->domain] += bytes;
    s_dom_count[e->domain] += 1;
    s_dom_bytes[e->domain] += bytes;
    if (o->count[e->domain] > g_pe_reg_limits[e->domain].owner_entries || o->bytes[e->domain] > g_pe_reg_limits[e->domain].owner_bytes)
        o->over[e->domain] = 1;
    if (s_dom_count[e->domain] > g_pe_reg_limits[e->domain].domain_entries || s_dom_bytes[e->domain] > g_pe_reg_limits[e->domain].domain_bytes)
        s_dom_over[e->domain] = 1;
    if (o->over[e->domain] || s_dom_over[e->domain]) return PE_REG_CAPACITY;      /* known now; the seal says so too */
    /* Allocate only the declared references; zero-reference entries do not reserve eight keys. */
    size_t refs_size = (size_t)e->nrefs * (sizeof(PeRegRef) + sizeof(int) + PE_REG_KEY_MAX + 1u);
    if (s_entries == PE_REG_MAX_ENTRIES || !(r = pe_reg_alloc(sizeof *r + refs_size, 1))) {
        pe_fail(b, PE_REG_CAPACITY, "cannot stage required entry %s%s", e->key, NULL);
        return PE_REG_CAPACITY;
    }
    r->refs = (PeRegRef *)(r + 1);
    r->ref_at = (int *)(r->refs + e->nrefs);
    r->ref_key = (char (*)[PE_REG_KEY_MAX + 1])(r->ref_at + e->nrefs);
    if (e->size && !(r->data = pe_reg_alloc(e->size, 0))) {
        pe_reg_free(r);
        pe_fail(b, PE_REG_CAPACITY, "cannot copy required entry %s%s", e->key, NULL);
        return PE_REG_CAPACITY;
    }
    if (e->size) memcpy(r->data, e->data, e->size);
    r->batch = b;
    r->domain = e->domain;
    r->size = e->size;
    r->schema = e->schema;
    r->unique_slot = e->unique_slot;
    r->nrefs = e->nrefs;
    snprintf(r->key, sizeof r->key, "%s", e->key);
    for (i = 0; i < e->nrefs; ++i) {
        snprintf(r->ref_key[i], sizeof r->ref_key[0], "%s", e->refs[i].key);
        r->refs[i].schema = e->refs[i].schema;
        r->refs[i].domain = e->refs[i].domain;
        r->refs[i].key = r->ref_key[i];
    }
    s_entry[s_entries++] = r;
    return PE_REG_PENDING;
}

/* ---- SEAL ---- */
static void pe_fail(int b, PeRegResult why, const char *fmt, const char *a, const char *c) {
    PeBatch *batch = s_batch[b];
    if (batch->result != PE_REG_PENDING) return;           /* the first reason stays */
    batch->result = why;
    snprintf(batch->why, sizeof batch->why, fmt, a ? a : "", c ? c : "");
}

static int pe_cmp_key(const void *x, const void *y) {
    const PeEntryRec *a = s_entry[*(const int *)x], *b = s_entry[*(const int *)y];
    const int d = (int)a->domain - (int)b->domain;
    return d ? d : strcmp(a->key, b->key);
}

static int pe_cmp_canon(const void *x, const void *y) {
    const PeEntryRec *a = s_entry[*(const int *)x], *b = s_entry[*(const int *)y];
    const PeBatch *ba = s_batch[a->batch], *bb = s_batch[b->batch];
    int c = strcmp(s_owner[ba->owner].id, s_owner[bb->owner].id);
    if (!c) c = strcmp(ba->name, bb->name);
    return c ? c : strcmp(a->key, b->key);
}

/* The unique entry with this key, or -1 (none, or claimed twice). `order` is sorted by key. */
static int pe_lookup(const int *order, int n, int domain, const char *key) {
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2, d = (int)s_entry[order[mid]]->domain - domain;
        const int c = d ? d : strcmp(s_entry[order[mid]]->key, key);
        if (!c) {
            if ((mid > 0 && pe_cmp_key(&order[mid - 1], &order[mid]) == 0) ||
                (mid + 1 < n && pe_cmp_key(&order[mid + 1], &order[mid]) == 0)) return -1;
            return order[mid];
        }
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
}

/* Requirement cycles: what is left once batches nothing requires, and batches requiring nothing,
 * are peeled away repeatedly is on a cycle or between cycles. */
static void pe_cycles(void) {
    static int in[PE_REG_MAX_ALL_BATCHES], out[PE_REG_MAX_ALL_BATCHES], gone[PE_REG_MAX_ALL_BATCHES];
    int b, k, changed = 1;
    memset(gone, 0, sizeof gone);
    while (changed) {
        changed = 0;
        memset(in, 0, sizeof in);
        memset(out, 0, sizeof out);
        for (b = 0; b < s_batches; ++b) {
            if (gone[b]) continue;
            for (k = 0; k < s_batch[b]->nreq; ++k) {
                const int t = s_batch[b]->req_at[k];
                if (t < 0 || gone[t]) continue;
                ++out[b];
                ++in[t];
            }
        }
        for (b = 0; b < s_batches; ++b)
            if (!gone[b] && (!in[b] || !out[b])) { gone[b] = 1; changed = 1; }
    }
    for (b = 0; b < s_batches; ++b)
        if (!gone[b]) pe_fail(b, PE_REG_CYCLE, "batch requirements form a cycle%s%s", NULL, NULL);
}

static int pe_publish(void) {
    int d, i;
    for (d = 0; d < PE_REG_DOMAINS; ++d) {
        int n = 0;
        pe_reg_free(s_by_key[d]);
        pe_reg_free(s_canon[d]);
        s_by_key[d] = s_canon[d] = NULL;
        s_ready[d] = 0;
        for (i = 0; i < s_entries; ++i) n += s_entry[i]->domain == d && s_batch[s_entry[i]->batch]->result == PE_REG_OK;
        if (!n) continue;
        if (!(s_by_key[d] = pe_reg_alloc(sizeof(int) * (size_t)n, 0)) ||
            !(s_canon[d] = pe_reg_alloc(sizeof(int) * (size_t)n, 0))) goto failed;
        for (i = 0, n = 0; i < s_entries; ++i)
            if (s_entry[i]->domain == d && s_batch[s_entry[i]->batch]->result == PE_REG_OK) s_by_key[d][n] = s_canon[d][n] = i, ++n;
        qsort(s_by_key[d], (size_t)n, sizeof(int), pe_cmp_key);
        qsort(s_canon[d], (size_t)n, sizeof(int), pe_cmp_canon);
        s_ready[d] = n;
    }
    return 1;
failed:
    for (d = 0; d < PE_REG_DOMAINS; ++d) {
        pe_reg_free(s_by_key[d]); pe_reg_free(s_canon[d]);
        s_by_key[d] = s_canon[d] = NULL; s_ready[d] = 0;
    }
    return 0;
}

void pe_registry_seal(void) {
    int *order = NULL;
    int b, i, k, d, changed;
    if (!pe_registry_thread() || s_phase != PHASE_COLLECT) return;
    s_phase = PHASE_SEAL;
    ++s_generation;
    /* 1. too many mods: nothing this start */
    if (s_owner_overflow || s_unavailable) {
        s_unavailable = 1;
        for (b = 0; b < s_batches; ++b) pe_fail(b, PE_REG_CAPACITY, s_owner_overflow ? "more than 32 mods registered%s%s" : "registry staging unavailable%s%s", NULL, NULL);
    }
    /* 2, 3. limits */
    for (b = 0; b < s_batches; ++b)
        for (d = 0; d < PE_REG_DOMAINS; ++d) {
            if (!s_batch[b]->tried[d]) continue;
            if (s_dom_over[d]) pe_fail(b, PE_REG_CAPACITY, "the %s domain is over its limit%s", s_domain_name[d], NULL);
            else if (s_owner[s_batch[b]->owner].over[d]) pe_fail(b, PE_REG_CAPACITY, "its mod is over the %s limit%s", s_domain_name[d], NULL);
        }
    /* 4. mods not running */
    for (b = 0; b < s_batches; ++b)
        if (!pe_mod_is_running(s_owner[s_batch[b]->owner].id)) pe_fail(b, PE_REG_INACTIVE, "its mod is not running%s%s", NULL, NULL);
    /* 5. keys claimed twice */
    if (s_entries && (order = pe_reg_alloc(sizeof(int) * (size_t)s_entries, 0))) {
        for (i = 0; i < s_entries; ++i) order[i] = i;
        qsort(order, (size_t)s_entries, sizeof(int), pe_cmp_key);
        for (i = 0; i + 1 < s_entries; ++i)
            if (pe_cmp_key(&order[i], &order[i + 1]) == 0) {
                pe_fail(s_entry[order[i]]->batch, PE_REG_DUPLICATE_ID, "%s is declared twice%s", s_entry[order[i]]->key, NULL);
                pe_fail(s_entry[order[i + 1]]->batch, PE_REG_DUPLICATE_ID, "%s is declared twice%s", s_entry[order[i]]->key, NULL);
            }
        for (i = 0; i < s_entries; ++i)
            for (k = 0; k < s_entry[i]->nrefs; ++k)
                s_entry[i]->ref_at[k] = pe_lookup(order, s_entries, s_entry[i]->refs[k].domain, s_entry[i]->refs[k].key);
    } else if (s_entries) {
        s_unavailable = 1;
        for (b = 0; b < s_batches; ++b) pe_fail(b, PE_REG_CAPACITY, "out of memory sealing the registry%s%s", NULL, NULL);
    }
    /* 6. requirements: known batches, no cycles */
    for (b = 0; b < s_batches; ++b) {
        PeBatch *x = s_batch[b];
        for (k = 0; k < x->nreq; ++k) {
            const char *colon = strchr(x->req[k], ':');
            int owner = x->owner, o;
            char name[PE_REG_BATCH_MAX + 1];
            if (colon) {
                owner = -1;
                for (o = 0; o < s_owners; ++o)
                    if (strlen(s_owner[o].id) == (size_t)(colon - x->req[k]) && !strncmp(s_owner[o].id, x->req[k], (size_t)(colon - x->req[k]))) owner = o;
                snprintf(name, sizeof name, "%s", colon + 1);
            } else snprintf(name, sizeof name, "%s", x->req[k]);
            x->req_at[k] = owner >= 0 ? pe_find_batch(owner, name) : -1;
            if (x->req_at[k] < 0) pe_fail(b, PE_REG_REQUIRES_FAILED, "requires %s, which nobody declared%s", x->req[k], NULL);
        }
    }
    pe_cycles();
    /* 7. propagate to a fixed point */
    for(int a=0;a<s_entries;++a)if(s_entry[a]->unique_slot)
        for(int c=0;c<a;++c)if(s_entry[c]->unique_slot==s_entry[a]->unique_slot&&
            s_entry[c]->schema==s_entry[a]->schema&&s_entry[c]->domain==s_entry[a]->domain&&
            s_batch[s_entry[c]->batch]->owner==s_batch[s_entry[a]->batch]->owner){
            pe_fail(s_entry[c]->batch,PE_REG_DUPLICATE_ID,"duplicate stable ordinal %s%s",s_entry[c]->key,NULL);
            pe_fail(s_entry[a]->batch,PE_REG_DUPLICATE_ID,"duplicate stable ordinal %s%s",s_entry[a]->key,NULL);
        }
    for (changed = 1; changed;) {
        changed = 0;
        for (b = 0; b < s_batches; ++b) {
            PeBatch *x = s_batch[b];
            if (x->result != PE_REG_PENDING) continue;
            for (k = 0; k < x->nreq && x->result == PE_REG_PENDING; ++k)
                if (x->req_at[k] >= 0 && s_batch[x->req_at[k]]->result != PE_REG_PENDING)
                    pe_fail(b, PE_REG_REQUIRES_FAILED, "requires %s, which failed%s", x->req[k], NULL);
            for (i = 0; i < s_entries && x->result == PE_REG_PENDING; ++i) {
                const PeEntryRec *e = s_entry[i];
                if (e->batch != b) continue;
                for (k = 0; k < e->nrefs; ++k) {
                    const int t = e->ref_at[k];
                    if (t < 0 || s_batch[s_entry[t]->batch]->result != PE_REG_PENDING ||
                        (e->refs[k].schema && e->refs[k].schema != s_entry[t]->schema)) {
                        pe_fail(b, PE_REG_MISSING_REFERENCE, "%s needs %s, not ready", e->key, e->refs[k].key);
                        break;
                    }
                }
            }
            for (i = 0; i < s_batches && x->result == PE_REG_PENDING; ++i)
                if (s_batch[i]->owner == x->owner && s_batch[i]->required && s_batch[i]->result != PE_REG_PENDING)
                    pe_fail(b, PE_REG_REQUIRES_FAILED, "its mod's base batch %s failed%s", s_batch[i]->name, NULL);
            changed |= x->result != PE_REG_PENDING;
        }
    }
    for (b = 0; b < s_batches; ++b) {
        if (s_batch[b]->result == PE_REG_PENDING) s_batch[b]->result = PE_REG_OK;
        else pe_mod_log(s_owner[s_batch[b]->owner].id, "registry: batch %s not ready: %s", s_batch[b]->name, s_batch[b]->why);
    }
    pe_reg_free(order);
    if (!pe_publish()) {
        s_unavailable = 1;
        for (b = 0; b < s_batches; ++b) if (s_batch[b]->result == PE_REG_OK) {
            s_batch[b]->result = PE_REG_PENDING;
            pe_fail(b, PE_REG_CAPACITY, "cannot publish the complete registry%s%s", NULL, NULL);
        }
    }
    s_phase = PHASE_RUN;
    pe_mod_log(NULL, "registry generation %u: content %d, effects %d, catalog %d, shop %d ready", s_generation,
               s_ready[PE_REG_CONTENT], s_ready[PE_REG_EFFECT], s_ready[PE_REG_CATALOG], s_ready[PE_REG_SHOP]);
}

/* ---- RUN ---- */
uint32_t pe_reg_generation(void) { return pe_registry_thread() && s_phase == PHASE_RUN ? s_generation : 0; }

int pe_reg_domain_ready(int domain) {
    return pe_registry_thread() && s_phase == PHASE_RUN && !s_unavailable && domain >= 0 && domain < PE_REG_DOMAINS && !s_dom_over[domain];
}

const void *pe_reg_find(int domain, const char *key, uint32_t *size, const char **owner) {
    int lo = 0, hi;
    if (!pe_reg_domain_ready(domain) || !key) return NULL;
    hi = s_ready[domain] - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        const PeEntryRec *e = s_entry[s_by_key[domain][mid]];
        const int c = strcmp(e->key, key);
        if (!c) {
            const char *who = s_owner[s_batch[e->batch]->owner].id;
            if (!pe_mod_is_running(who)) return NULL;             /* checked at use, not just at seal */
            if (size) *size = e->size;
            if (owner) *owner = who;
            return e->data ? e->data : (const void *)e->key;     /* a definition with no payload still exists */
        }
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

int pe_reg_count(int domain) { return pe_reg_domain_ready(domain) ? s_ready[domain] : 0; }

const char *pe_reg_key(int domain, int index) {
    return pe_reg_domain_ready(domain) && index >= 0 && index < s_ready[domain] ? s_entry[s_canon[domain][index]]->key : NULL;
}

PeRegResult pe_reg_batch_result(const char *owner, const char *batch, char *why, size_t size) {
    int o, b;
    if (!pe_registry_thread()) return PE_REG_WRONG_THREAD;
    if (why && size) why[0] = 0;
    for (o = 0; o < s_owners; ++o)
        if (owner && strcmp(s_owner[o].id, owner) == 0 && batch && (b = pe_find_batch(o, batch)) >= 0) {
            if (why && size) snprintf(why, size, "%s", s_batch[b]->why);
            return s_batch[b]->result;
        }
    return PE_REG_BAD_ARGUMENT;
}

void pe_registry_reset(void) {
    int i;
    if (!pe_registry_thread() || s_readonly) return;
    for (i = 0; i < s_entries; ++i) { pe_reg_free(s_entry[i]->data); pe_reg_free(s_entry[i]); }
    for (i = 0; i < s_batches; ++i) pe_reg_free(s_batch[i]);
    for (i = 0; i < PE_REG_DOMAINS; ++i) { pe_reg_free(s_by_key[i]); pe_reg_free(s_canon[i]); s_by_key[i] = s_canon[i] = NULL; s_ready[i] = 0; }
    memset(s_owner, 0, sizeof s_owner);
    memset(s_dom_count, 0, sizeof s_dom_count);
    memset(s_dom_bytes, 0, sizeof s_dom_bytes);
    memset(s_dom_over, 0, sizeof s_dom_over);
    s_owners = s_batches = s_entries = s_owner_overflow = s_unavailable = 0;
    s_phase = PHASE_COLLECT;
}

PeRegResult pe_reg_access(void) {
    if (!pe_registry_thread()) return PE_REG_WRONG_THREAD;
    if (s_readonly || s_phase != PHASE_RUN) return PE_REG_WRONG_PHASE;
    return s_unavailable ? PE_REG_UNAVAILABLE : PE_REG_OK;
}

int pe_reg_readonly_begin(void) {
    if (pe_reg_access()!=PE_REG_OK) return 0;
    s_readonly=1;return 1;
}
void pe_reg_readonly_end(void) { if(pe_registry_thread())s_readonly=0; }

PeRegResult pe_reg_registration_access(void){
 if(!pe_registry_thread())return PE_REG_WRONG_THREAD;
 return !s_readonly&&s_phase==PHASE_COLLECT?PE_REG_OK:PE_REG_WRONG_PHASE;
}

int pe_reg_callback_active(void){return !pe_registry_thread()||s_readonly;}

const void *pe_reg_find_typed(int domain,const char*key,uint32_t schema,uint32_t expected_size){
    if(!pe_reg_domain_ready(domain)||!key||!schema)return NULL;
    int lo=0,hi=s_ready[domain]-1;
    while(lo<=hi){int mid=(lo+hi)/2;const PeEntryRec*e=s_entry[s_by_key[domain][mid]];int c=strcmp(e->key,key);
        if(!c)return e->schema==schema&&e->size==expected_size&&pe_mod_is_running(s_owner[s_batch[e->batch]->owner].id)?e->data:NULL;
        if(c<0)lo=mid+1;else hi=mid-1;
    }return NULL;
}
