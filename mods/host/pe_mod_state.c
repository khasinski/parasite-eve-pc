/* Mods' live-state providers (pe_mod_state.h) and the JSON writer debug control reads them with.
 * Registration is always on (it costs a table entry); providers run only from debug control. */
#include "pe_mod_state.h"
#include "debug/pe_debug_internal.h"
#include "pe_mod_host.h"
#include <string.h>

#define PE_STATE_PROVIDERS 32
#define PE_STATE_DEPTH 7

struct PeStateWriter {
    FILE *f;
    int depth;                       /* 0: the provider's own object */
    int dropped;                     /* levels opened past PE_STATE_DEPTH, not written */
    unsigned char comma[PE_STATE_DEPTH], list[PE_STATE_DEPTH];
};

typedef struct {
    const char *owner, *name;
    PeModStateFn fn;
    void *user;
} PeStateProvider;

static PeStateProvider s_providers[PE_STATE_PROVIDERS];
static int s_count;

int pe_mod_state_provider(const char *name, PeModStateFn fn, void *user) {
    const char *owner = pe_mod_current();
    int i;
    if (!owner || !name || !*name || !fn || s_count >= PE_STATE_PROVIDERS) return -1;
    for (i = 0; i < s_count; ++i)
        if (!strcmp(s_providers[i].owner, owner) && !strcmp(s_providers[i].name, name)) return -1;
    s_providers[s_count].owner = owner;
    s_providers[s_count].name = name;
    s_providers[s_count].fn = fn;
    s_providers[s_count].user = user;
    ++s_count;
    return 0;
}

/* The separator and key of the next value; 0 when it must not be written. */
static int pe_state_key(PeStateWriter *w, const char *key) {
    if (!w || w->dropped) return 0;
    if (w->comma[w->depth]) fputc(',', w->f);
    w->comma[w->depth] = 1;
    if (!w->list[w->depth]) {
        pe_debug_json_string(w->f, key ? key : "");
        fputc(':', w->f);
    }
    return 1;
}

void pe_state_int(PeStateWriter *w, const char *key, long long value) {
    if (pe_state_key(w, key)) fprintf(w->f, "%lld", value);
}

void pe_state_bool(PeStateWriter *w, const char *key, int value) {
    if (pe_state_key(w, key)) fputs(value ? "true" : "false", w->f);
}

void pe_state_text(PeStateWriter *w, const char *key, const char *value) {
    if (!pe_state_key(w, key)) return;
    if (value) pe_debug_json_string(w->f, value);
    else fputs("null", w->f);
}

static void pe_state_open(PeStateWriter *w, const char *key, int list) {
    if (!w) return;
    if (w->dropped || w->depth + 1 >= PE_STATE_DEPTH) { ++w->dropped; return; }
    pe_state_key(w, key);
    fputc(list ? '[' : '{', w->f);
    ++w->depth;
    w->comma[w->depth] = 0;
    w->list[w->depth] = (unsigned char)list;
}

void pe_state_begin_object(PeStateWriter *w, const char *key) { pe_state_open(w, key, 0); }
void pe_state_begin_list(PeStateWriter *w, const char *key) { pe_state_open(w, key, 1); }

void pe_state_end(PeStateWriter *w) {
    if (!w) return;
    if (w->dropped) { --w->dropped; return; }
    if (w->depth <= 0) return;     /* the provider's own object is closed by the reader */
    fputc(w->list[w->depth] ? ']' : '}', w->f);
    --w->depth;
}

int pe_mod_state_count(const char *owner) {
    int i, n = 0;
    for (i = 0; i < s_count; ++i) n += !owner || !strcmp(s_providers[i].owner, owner);
    return n;
}

/* {"<name>":{...},...}: every provider of a running `owner`, called around pe_mod_call_begin so
 * a crash inside one names it. */
void pe_mod_state_write(FILE *f, const char *owner) {
    int i, first = 1;
    fputc('{', f);
    for (i = 0; i < s_count; ++i) {
        PeStateWriter w;
        if (strcmp(s_providers[i].owner, owner)) continue;
        if (!first) fputc(',', f);
        first = 0;
        pe_debug_json_string(f, s_providers[i].name);
        fputs(":{", f);
        memset(&w, 0, sizeof w);
        w.f = f;
        pe_mod_call_begin(owner, "state provider", s_providers[i].name);
        s_providers[i].fn(&w, s_providers[i].user);
        pe_mod_call_end();
        w.dropped = 0;
        while (w.depth > 0) pe_state_end(&w);
        fputc('}', f);
    }
    fputc('}', f);
}

/* {"<owner>":{...},...}: every running mod that has providers. */
void pe_mod_state_write_running(FILE *f) {
    int i, j, first = 1;
    fputc('{', f);
    for (i = 0; i < s_count; ++i) {
        const char *owner = s_providers[i].owner;
        for (j = 0; j < i && strcmp(s_providers[j].owner, owner); ++j) {}
        if (j < i || !pe_mod_is_running(owner)) continue;    /* one entry per owner */
        if (!first) fputc(',', f);
        first = 0;
        pe_debug_json_string(f, owner);
        fputc(':', f);
        pe_mod_state_write(f, owner);
    }
    fputc('}', f);
}
