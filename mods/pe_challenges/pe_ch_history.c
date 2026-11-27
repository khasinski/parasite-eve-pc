/* Challenges: the per-run file, pe_challenges.ini beside the preferences.
 *
 * One line per run that had a challenge on, newest last:
 *   <run id> ko=<knockouts> st=<playing|cleared|fallen> on=<hex> ever=<hex> drop=<hex> fl=<hex>
 *            b=<battles> t=<seconds> lv=<hex, 2 bits per challenge id> at=<Unix seconds>
 * Files from before the history (<run id> <knockouts> [dead]) still read: "dead" is fallen.
 *
 * Why a file and not the save: a knockout or a Hardcore death is followed by a reload, which would
 * undo anything kept in the save; and the history lists runs whose saves are gone or overwritten.
 * Kept per user, like pe_collection.ini, so the save editor, save states and companion files never
 * see it. Unknown keys on a line are ignored, so later builds can add some.
 *
 * Room: the newest 128 runs. When full, the oldest run still playing goes first, then the oldest
 * cleared one, and a fallen run only when nothing else is left (it keeps its saves refused).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "pe_challenges.h"
#include "pe_mod_host.h"

#define CH_FILE "pe_challenges.ini"
#define CH_RUNS_MAX 128

static PeChRunEntry s_runs[CH_RUNS_MAX];
static int s_count, s_loaded;

static const char *const kStates[] = {"playing", "cleared", "fallen"};

/* ---- the file ----------------------------------------------------------------------------- */

static void pe_parse_pair(PeChRunEntry *e, const char *key, const char *value) {
    const unsigned long long v = strtoull(value, NULL, 16);
    if (!strcmp(key, "ko")) e->knockouts = (uint32_t)strtoul(value, NULL, 10);
    else if (!strcmp(key, "st")) {
        for (uint32_t s = 0; s < 3; ++s) if (!strcmp(value, kStates[s])) e->state = s;
    }
    else if (!strcmp(key, "on")) e->active = (uint32_t)v;
    else if (!strcmp(key, "ever")) e->ever = (uint32_t)v;
    else if (!strcmp(key, "drop")) e->dropped = (uint32_t)v;
    else if (!strcmp(key, "fl")) e->flags = (uint32_t)v;
    else if (!strcmp(key, "lv")) e->levels = (uint32_t)v;
    else if (!strcmp(key, "b")) e->battles = (uint32_t)strtoul(value, NULL, 10);
    else if (!strcmp(key, "t")) e->seconds = (uint32_t)strtoul(value, NULL, 10);
    else if (!strcmp(key, "at")) e->at = (int64_t)strtoll(value, NULL, 10);
}

/* The next space-separated word of *cursor (cut in place), or NULL. */
static char *pe_word(char **cursor) {
    char *p = *cursor, *start;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (!*p) return NULL;
    start = p;
    while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') ++p;
    if (*p) *p++ = 0;
    *cursor = p;
    return start;
}

static void pe_parse_line(char *line) {
    PeChRunEntry e;
    char *cursor = line, *tok;
    int field = 0;
    memset(&e, 0, sizeof e);
    if (line[0] == '#') return;
    for (; (tok = pe_word(&cursor)) != NULL; ++field) {
        char *eq = strchr(tok, '=');
        if (field == 0) { e.id = (uint32_t)strtoul(tok, NULL, 16); continue; }
        if (eq) { *eq = 0; pe_parse_pair(&e, tok, eq + 1); }
        else if (field == 1) e.knockouts = (uint32_t)strtoul(tok, NULL, 10);   /* the older format */
        else if (!strcmp(tok, "dead")) e.state = CH_RUN_FALLEN;
    }
    if (e.id && s_count < CH_RUNS_MAX) s_runs[s_count++] = e;
}

static void pe_load(void) {
    char path[512], line[256];
    FILE *f;
    if (s_loaded) return;
    s_loaded = 1;
    if (pe_mod_path(path, sizeof path, CH_FILE) != 0 || !(f = fopen(path, "r"))) return;
    while (fgets(line, sizeof line, f)) pe_parse_line(line);
    fclose(f);
}

static void pe_save(void) {
    char path[512], temp[520];
    FILE *f;
    if (pe_mod_path(path, sizeof path, CH_FILE) != 0) return;
    snprintf(temp, sizeof temp, "%s.tmp", path);
    if (!(f = fopen(temp, "w"))) return;
    fputs("# Challenges: one line per run, newest last (see mods/pe_challenges/pe_ch_history.c)\n", f);
    for (int i = 0; i < s_count; ++i) {
        const PeChRunEntry *e = &s_runs[i];
        fprintf(f, "%08x ko=%u st=%s on=%x ever=%x drop=%x fl=%x b=%u t=%u lv=%x at=%lld\n",
                (unsigned)e->id, (unsigned)e->knockouts, kStates[e->state < 3 ? e->state : 0], (unsigned)e->active,
                (unsigned)e->ever, (unsigned)e->dropped, (unsigned)e->flags, (unsigned)e->battles,
                (unsigned)e->seconds, (unsigned)e->levels, (long long)e->at);
    }
    if (fclose(f) != 0) { remove(temp); return; }
    remove(path);
    if (rename(temp, path) != 0) remove(temp);
}

/* ---- entries ------------------------------------------------------------------------------ */

static int pe_drop_one(void) {
    static const uint32_t order[] = {CH_RUN_PLAYING, CH_RUN_CLEARED, CH_RUN_FALLEN};
    for (unsigned o = 0; o < 3; ++o)
        for (int i = 0; i < s_count; ++i)
            if (s_runs[i].state == order[o]) {
                memmove(&s_runs[i], &s_runs[i + 1], sizeof s_runs[0] * (size_t)(s_count - i - 1));
                return --s_count, 1;
            }
    return 0;
}

static PeChRunEntry *pe_entry(uint32_t id, int create) {
    pe_load();
    if (!id) return NULL;
    for (int i = 0; i < s_count; ++i)
        if (s_runs[i].id == id) return &s_runs[i];
    if (!create) return NULL;
    if (s_count == CH_RUNS_MAX && !pe_drop_one()) return NULL;
    memset(&s_runs[s_count], 0, sizeof s_runs[0]);
    s_runs[s_count].id = id;
    return &s_runs[s_count++];
}

int pe_ch_history_count(void) { pe_load(); return s_count; }

const PeChRunEntry *pe_ch_history_newest(int index) {
    pe_load();
    return index >= 0 && index < s_count ? &s_runs[s_count - 1 - index] : NULL;
}

const PeChRunEntry *pe_ch_history_find(uint32_t run_id) { return pe_entry(run_id, 0); }

void pe_ch_history_note(int state) {
    PeChRunEntry *e;
    uint32_t levels = 0;
    if (!g_ch_run.run_id || !g_ch_run.ever || !(e = pe_entry(g_ch_run.run_id, 1))) return;
    for (int id = 0; id < CH_COUNT; ++id) levels |= (uint32_t)(g_ch_run.level[id] & 3u) << (2 * id);
    e->active = g_ch_run.active;
    e->ever = g_ch_run.ever;
    e->dropped = g_ch_run.dropped;
    e->flags = g_ch_run.flags;
    e->battles = g_ch_run.battles;
    e->seconds = g_ch_run.seconds;
    e->levels = levels;
    e->at = (int64_t)time(NULL);
    if ((uint32_t)state > e->state) e->state = (uint32_t)state;   /* playing < cleared < fallen */
    pe_save();
}

void pe_ch_history_knockout(void) {
    PeChRunEntry *e = pe_entry(g_ch_run.run_id, 1);
    if (!e) return;
    e->knockouts++;
    pe_save();
    pe_log("challenge_ko", "run=%08x knockouts=%u", (unsigned)g_ch_run.run_id, (unsigned)e->knockouts);
}

unsigned pe_ch_knockouts(void) {
    const PeChRunEntry *e = pe_entry(g_ch_run.run_id, 0);
    return e ? e->knockouts : 0;
}

int pe_ch_run_is_dead(uint32_t run_id) {
    const PeChRunEntry *e = pe_entry(run_id, 0);
    return e && e->state == CH_RUN_FALLEN;
}
