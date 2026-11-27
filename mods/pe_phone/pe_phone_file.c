/* Reading calls written as text (mods/pe_phone/calls/pe1.txt explains the format for writers).
 * Pure: bytes in, calls out, mistakes reported through a callback with the file and line.
 *
 *   # a note                     (a line starting with #; blank lines are ignored)
 *   [pack/name]                  a new call: its key
 *   setting = value              caller, tone, day, weight, once, rooms, priority, after, story, needs
 *   them: text / aya: text       a line of the call, in order
 *
 * A call is kept only if it has a key, a caller and at least one line, and all of it is valid;
 * otherwise every mistake in it is reported and the call is left out. Unknown settings are
 * reported but don't drop the call. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_phone_core.h"

typedef struct PeParse {
    PePhoneStore *store;
    const char *file;
    PePhoneProblem problem;
    void *user;
    PePhoneCall *call;              /* the call being read, or NULL */
    int call_line, bad;
    int lines_at, rooms_at, text_at; /* store marks: undone if the call is dropped */
    int added;
} PeParse;

static void pe_report(PeParse *p, int line, const char *fmt, const char *arg) {
    char what[200];
    snprintf(what, sizeof what, fmt, arg ? arg : "");
    if (p->problem) p->problem(p->user, p->file, line, what);
}

static const char *pe_keep(PeParse *p, const char *s, int n) {
    PePhoneStore *st = p->store;
    char *out;
    if (st->text_used + n + 1 > PE_PHONE_STORE_TEXT) return NULL;
    out = st->text + st->text_used;
    memcpy(out, s, (size_t)n);
    out[n] = 0;
    st->text_used += n + 1;
    return out;
}

static char *pe_trim(char *s) {
    char *e;
    while (*s == ' ' || *s == '\t') ++s;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
    return s;
}

static int pe_eq(const char *a, const char *b) {
    for (; *a && *b; ++a, ++b) if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return !*a && !*b;
}

static int pe_number(const char *s, long *out) {
    char *end;
    long v;
    if (!*s) return 0;
    v = strtol(s, &end, 0);
    if (*pe_trim(end)) return 0;
    *out = v;
    return 1;
}

/* "1", "2-5", "any" (or empty): from..to, 0 0 for any. */
static int pe_range(const char *s, long *from, long *to) {
    char buf[64], *dash;
    if (!*s || pe_eq(s, "any")) { *from = *to = 0; return 1; }
    snprintf(buf, sizeof buf, "%s", s);
    dash = strchr(buf + 1, '-');
    if (!dash) return pe_number(buf, from) && (*to = *from, 1);
    *dash = 0;
    return pe_number(pe_trim(buf), from) && pe_number(pe_trim(dash + 1), to) && *from <= *to;
}

static void pe_finish(PeParse *p) {
    PePhoneStore *st = p->store;
    PePhoneCall *c = p->call;
    if (!c) return;
    if (!c->caller) { pe_report(p, p->call_line, "call %s has no caller = ...", c->key); p->bad = 1; }
    if (c->line_count == 0 && !p->bad) { pe_report(p, p->call_line, "call %s has no lines (them: ... / aya: ...)", c->key); p->bad = 1; }
    if (p->bad) {                                    /* left out: give its space back */
        pe_report(p, p->call_line, "call %s left out (see the lines above)", c->key);
        st->line_count = p->lines_at;
        st->room_count = p->rooms_at;
        st->text_used = p->text_at;
        --st->call_count;
    } else {
        ++p->added;
    }
    p->call = NULL;
}

static void pe_start(PeParse *p, int line, char *key) {
    PePhoneStore *st = p->store;
    int i;
    pe_finish(p);
    p->bad = 0;
    p->call_line = line;
    if (!*key || strchr(key, ' ') || !strchr(key, '/')) {
        pe_report(p, line, "[%s]: a key is pack/name, without spaces, like [mine/pizza2]", key);
        p->bad = 1;
    }
    for (i = 0; i < st->call_count; ++i)
        if (!strcmp(st->calls[i].key, key)) { pe_report(p, line, "[%s] is already used by another call", key); p->bad = 1; }
    if (st->call_count >= PE_PHONE_STORE_CALLS) { pe_report(p, line, "too many calls: [%s] and the rest left out", key); return; }
    p->lines_at = st->line_count;
    p->rooms_at = st->room_count;
    p->text_at = st->text_used;
    p->call = &st->calls[st->call_count++];
    memset(p->call, 0, sizeof *p->call);
    p->call->weight = 1;
    p->call->tone = PE_CALL_SERIOUS;
    p->call->lines = &st->lines[st->line_count];
    if (!(p->call->key = pe_keep(p, key, (int)strlen(key)))) { p->call->key = ""; p->bad = 1; }
}

static void pe_setting(PeParse *p, int line, char *name, char *value) {
    PePhoneCall *c = p->call;
    long a, b;
    if (pe_eq(name, "caller")) {
        if (!pe_phone_text_ok(value) || !*value) { pe_report(p, line, "caller = %s: only letters, digits, spaces and . , : ! ? ' - + * %% /", value); p->bad = 1; return; }
        c->caller = pe_keep(p, value, (int)strlen(value));
    } else if (pe_eq(name, "tone")) {
        if (pe_eq(value, "serious")) c->tone = PE_CALL_SERIOUS;
        else if (pe_eq(value, "funny")) c->tone = PE_CALL_FUNNY;
        else { pe_report(p, line, "tone = %s: serious or funny", value); p->bad = 1; }
    } else if (pe_eq(name, "day") || pe_eq(name, "days")) {
        if (!pe_range(value, &a, &b) || a < 0 || b > 6) { pe_report(p, line, "day = %s: 1 to 6, a range like 2-5, or any", value); p->bad = 1; return; }
        c->day_from = (int)a;
        c->day_to = (int)b;
    } else if (pe_eq(name, "story")) {
        if (!pe_range(value, &a, &b) || a < 0) { pe_report(p, line, "story = %s: a value or range like 0x80-0xE8, or any", value); p->bad = 1; return; }
        c->story_from = (unsigned)a;
        c->story_to = (unsigned)b;
    } else if (pe_eq(name, "weight")) {
        if (!pe_number(value, &a) || a < 1 || a > 1000) { pe_report(p, line, "weight = %s: a number from 1", value); p->bad = 1; return; }
        c->weight = (int)a;
    } else if (pe_eq(name, "priority")) {
        if (!pe_number(value, &a) || a < 0 || a > 1000) { pe_report(p, line, "priority = %s: 0 or more", value); p->bad = 1; return; }
        c->priority = (int)a;
    } else if (pe_eq(name, "once")) {
        if (pe_eq(value, "yes")) c->once = 1;
        else if (pe_eq(value, "no")) c->once = 0;
        else { pe_report(p, line, "once = %s: yes or no", value); p->bad = 1; }
    } else if (pe_eq(name, "after")) {
        c->after = *value && !pe_eq(value, "-") ? pe_keep(p, value, (int)strlen(value)) : NULL;
    } else if (pe_eq(name, "needs")) {
        char names[256], *tok;
        snprintf(names, sizeof names, "%s", value);
        c->needs = NULL;
        if (!*value || pe_eq(value, "-")) return;
        for (tok = strtok(names, " ,"); tok; tok = strtok(NULL, " ,"))
            if (!pe_phone_condition_name_ok(tok)) {
                pe_report(p, line, "needs: %s is not a condition name (letters, digits, . _ -)", tok);
                p->bad = 1;
                return;
            }
        for (tok = value; *tok; ++tok) if (*tok == ',') *tok = ' ';
        c->needs = pe_keep(p, value, (int)strlen(value));
    } else if (pe_eq(name, "rooms") || pe_eq(name, "room")) {
        PePhoneStore *st = p->store;
        char *tok;
        c->rooms = NULL;
        c->room_count = 0;
        if (!*value || pe_eq(value, "any")) return;
        c->rooms = &st->rooms[st->room_count];
        for (tok = strtok(value, " ,"); tok; tok = strtok(NULL, " ,")) {
            if (!pe_number(tok, &a) || a < 1 || a > 999) { pe_report(p, line, "rooms: %s is not a room number", tok); p->bad = 1; continue; }
            if (st->room_count >= PE_PHONE_STORE_ROOMS) { pe_report(p, line, "too many rooms in all the calls%s", ""); p->bad = 1; return; }
            st->rooms[st->room_count++] = (short)a;
            ++c->room_count;
        }
    } else {
        pe_report(p, line, "unknown setting \"%s\" (ignored)", name);
    }
}

static void pe_say(PeParse *p, int line, int speaker, char *text) {
    PePhoneStore *st = p->store;
    if (!*text) { pe_report(p, line, "an empty line%s", ""); p->bad = 1; return; }
    if (!pe_phone_text_ok(text)) {
        pe_report(p, line, "\"%s\": only letters, digits, spaces, {name} and . , : ! ? ' - + * %% /", text);
        p->bad = 1;
        return;
    }
    if (st->line_count >= PE_PHONE_STORE_LINES) { pe_report(p, line, "too many lines in all the calls%s", ""); p->bad = 1; return; }
    st->lines[st->line_count].speaker = speaker;
    st->lines[st->line_count].text = pe_keep(p, text, (int)strlen(text));
    if (!st->lines[st->line_count].text) { pe_report(p, line, "out of room for text%s", ""); p->bad = 1; return; }
    ++st->line_count;
    ++p->call->line_count;
}

/* ---- the player's copy of pe1.txt ---- */

#define PE_STAMP "# pe1 copy "

static uint32_t pe_fnv(const char *s, unsigned n) {
    uint32_t h = 2166136261u;
    unsigned i;
    for (i = 0; i < n; ++i) { h ^= (uint8_t)s[i]; h *= 16777619u; }
    return h;
}

int pe_phone_copy_stamp(char *out, int cap, const char *builtin, unsigned builtin_size) {
    return snprintf(out, (size_t)cap, PE_STAMP "%08x: the game keeps this file up to date while you leave it "
                    "as it is. Once you change it, it's yours: newer calls go to pe1.txt.new.\n",
                    (unsigned)pe_fnv(builtin, builtin_size));
}

int pe_phone_copy_state(const char *copy, unsigned size, const char *builtin, unsigned builtin_size) {
    const unsigned stamp_len = (unsigned)strlen(PE_STAMP);
    const uint32_t now = pe_fnv(builtin, builtin_size);
    unsigned body = 0;
    char hex[9];
    uint32_t was;
    if (size < stamp_len + 8 || memcmp(copy, PE_STAMP, stamp_len)) {        /* no stamp */
        return size == builtin_size && !memcmp(copy, builtin, size) ? PE_COPY_UPDATE : PE_COPY_EDITED_NEWER;
    }
    memcpy(hex, copy + stamp_len, 8);
    hex[8] = 0;
    was = (uint32_t)strtoul(hex, NULL, 16);
    while (body < size && copy[body] != '\n') ++body;
    if (body < size) ++body;                                               /* past the stamp line */
    if (pe_fnv(copy + body, size - body) == was)                           /* unedited */
        return was == now ? PE_COPY_CURRENT : PE_COPY_UPDATE;
    return was == now ? PE_COPY_EDITED : PE_COPY_EDITED_NEWER;
}

void pe_phone_store_clear(PePhoneStore *store) {
    store->call_count = store->line_count = store->room_count = store->text_used = 0;
}

int pe_phone_parse_calls(PePhoneStore *store, const char *file, const char *src, unsigned size,
                         PePhoneProblem problem, void *user) {
    PeParse p;
    unsigned at = 0;
    int line = 0;
    memset(&p, 0, sizeof p);
    p.store = store;
    p.file = file ? file : "?";
    p.problem = problem;
    p.user = user;
    if (size >= 3 && (unsigned char)src[0] == 0xEF && (unsigned char)src[1] == 0xBB && (unsigned char)src[2] == 0xBF) at = 3;   /* UTF-8 mark */
    while (at < size) {
        char buf[1024], *s, *colon, *eq;
        unsigned n = 0;
        ++line;
        while (at < size && src[at] != '\n') { if (n < sizeof buf - 1) buf[n++] = src[at]; ++at; }
        ++at;
        buf[n] = 0;
        s = pe_trim(buf);
        if (!*s || *s == '#') continue;
        if (*s == '[') {
            char *end = strchr(s, ']');
            if (!end) { pe_report(&p, line, "%s: a key ends with ]", s); continue; }
            *end = 0;
            pe_start(&p, line, pe_trim(s + 1));
            continue;
        }
        if (!p.call) { pe_report(&p, line, "\"%s\" is outside any call: start one with [pack/name]", s); continue; }
        colon = strchr(s, ':');
        eq = strchr(s, '=');
        if (colon && (!eq || colon < eq)) {
            *colon = 0;
            if (pe_eq(pe_trim(s), "them")) pe_say(&p, line, PE_CALL_CALLER, pe_trim(colon + 1));
            else if (pe_eq(pe_trim(s), "aya")) pe_say(&p, line, PE_CALL_AYA, pe_trim(colon + 1));
            else { pe_report(&p, line, "\"%s:\": a line starts with them: or aya:", s); p.bad = 1; }
        } else if (eq) {
            *eq = 0;
            pe_setting(&p, line, pe_trim(s), pe_trim(eq + 1));
        } else {
            pe_report(&p, line, "\"%s\": expected setting = value, them: text or aya: text", s);
            p.bad = 1;
        }
    }
    pe_finish(&p);
    return p.added;
}
