/* Challenges: records, one per challenge, kept across every game.
 *
 * A challenge is completed when a run is cleared (the Clear Data save after the credits) with it on
 * at the end and never lowered. The record keeps the best of every such clear:
 *   clean / assisted   how many clears, by whether the run was assisted
 *   best level         the hardest level cleared (levels go up with difficulty: Tough crowd 2x HP,
 *                      Club only 1.5x ...); 1 for an on / off challenge
 *   fastest            the shortest time with challenges, in seconds
 *   hardcore           ever cleared in a Hardcore run
 *   first              when it was first cleared (Unix seconds)
 *
 * Fed like any other mod would be: by listening to "challenges.run_cleared" (pe_ch_events.c), then
 * reading that run's line in the history (pe_ch_history.c, written just before the event). The
 * first time the file is missing, the cleared runs already in the history are counted once.
 *
 * The file: pe_challenges_records.ini beside the preferences, per user like the history, one line
 * per challenge id: "<id> clean= assisted= best= fastest= hardcore= first=". Ids are the stable
 * challenge ids; unknown keys are ignored, records of ids this build doesn't know are kept.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_challenges.h"
#include "pe_mod_host.h"

#define CH_FILE "pe_challenges_records.ini"
#define CH_IDS  32

static PeChRecord s_rec[CH_IDS];
static int s_loaded;

/* ---- the file ----------------------------------------------------------------------------- */

static void pe_save(void) {
    char path[512], temp[520];
    FILE *f;
    if (pe_mod_path(path, sizeof path, CH_FILE) != 0) return;
    snprintf(temp, sizeof temp, "%s.tmp", path);
    if (!(f = fopen(temp, "w"))) return;
    fputs("# Challenges: one line per challenge id (see mods/pe_challenges/pe_ch_records.c)\n", f);
    for (int id = 0; id < CH_IDS; ++id) {
        const PeChRecord *r = &s_rec[id];
        if (!r->clean && !r->assisted) continue;
        fprintf(f, "%d clean=%u assisted=%u best=%u fastest=%u hardcore=%u first=%lld\n", id, (unsigned)r->clean,
                (unsigned)r->assisted, (unsigned)r->best_level, (unsigned)r->fastest, (unsigned)r->hardcore,
                (long long)r->first);
    }
    if (fclose(f) != 0) { remove(temp); return; }
    remove(path);
    if (rename(temp, path) != 0) remove(temp);
}

static void pe_parse(char *line) {
    char *p = line, *word;
    int id;
    PeChRecord r;
    if (line[0] == '#' || sscanf(line, "%d", &id) != 1 || id < 0 || id >= CH_IDS) return;
    memset(&r, 0, sizeof r);
    while (*p && *p != ' ') ++p;
    while ((word = strchr(p, '=')) != NULL) {
        char *key = word, *value = word + 1;
        while (key > p && key[-1] != ' ') --key;
        *word = 0;
        if (!strcmp(key, "clean")) r.clean = (uint32_t)strtoul(value, NULL, 10);
        else if (!strcmp(key, "assisted")) r.assisted = (uint32_t)strtoul(value, NULL, 10);
        else if (!strcmp(key, "best")) r.best_level = (uint32_t)strtoul(value, NULL, 10);
        else if (!strcmp(key, "fastest")) r.fastest = (uint32_t)strtoul(value, NULL, 10);
        else if (!strcmp(key, "hardcore")) r.hardcore = (uint32_t)strtoul(value, NULL, 10);
        else if (!strcmp(key, "first")) r.first = (int64_t)strtoll(value, NULL, 10);
        p = value;
    }
    s_rec[id] = r;
}

/* One clear, from its history line: every challenge on at the end and never lowered. */
static int pe_apply(const PeChRunEntry *e) {
    const int assisted = e->dropped || (e->flags & CH_FLAG_CHEATS);
    int changed = 0;
    for (int id = 0; id < CH_COUNT && id < CH_IDS; ++id) {
        PeChRecord *r = &s_rec[id];
        const uint32_t level = (uint32_t)pe_ch_entry_level(e, id);
        if (!(e->active & CH_BIT(id)) || (e->dropped & CH_BIT(id))) continue;
        if (assisted) r->assisted++; else r->clean++;
        if (level > r->best_level) r->best_level = level ? level : 1;
        if (!r->best_level) r->best_level = 1;
        if (e->seconds && (!r->fastest || e->seconds < r->fastest)) r->fastest = e->seconds;
        if (e->active & CH_BIT(CH_HARDCORE)) r->hardcore = 1;
        if (!r->first || (e->at && e->at < r->first)) r->first = e->at;
        changed = 1;
    }
    return changed;
}

static void pe_load(void) {
    char path[512], line[256];
    FILE *f;
    if (s_loaded) return;
    s_loaded = 1;
    if (pe_mod_path(path, sizeof path, CH_FILE) == 0 && (f = fopen(path, "r")) != NULL) {
        while (fgets(line, sizeof line, f)) pe_parse(line);
        fclose(f);
        return;
    }
    /* No file yet: the clears already in the history, once. */
    {
        int changed = 0;
        for (int i = 0; i < pe_ch_history_count(); ++i) {
            const PeChRunEntry *e = pe_ch_history_newest(i);
            if (e && e->state == CH_RUN_CLEARED) changed |= pe_apply(e);
        }
        if (changed) pe_save();
    }
}

/* ---- for the other files ------------------------------------------------------------------ */

const PeChRecord *pe_ch_record(int id) {
    pe_load();
    return id >= 0 && id < CH_IDS ? &s_rec[id] : NULL;
}

int pe_ch_records_cleared(void) {
    int n = 0;
    pe_load();
    for (unsigned i = 0; i < g_ch_def_count; ++i) {
        const PeChRecord *r = &s_rec[g_ch_defs[i]->id];
        n += r->clean || r->assisted;
    }
    return n;
}

/* ---- the event ---------------------------------------------------------------------------- */

static int pe_records_on_cleared(void *ctx, void *user) {
    const uint32_t *run_id = ctx;
    const PeChRunEntry *e = run_id ? pe_ch_history_find(*run_id) : NULL;
    (void)user;
    pe_load();
    if (e && e->state == CH_RUN_CLEARED && pe_apply(e)) {
        pe_save();
        pe_log("challenge_records", "run=%08x cleared: %d of %u challenges completed so far", (unsigned)e->id,
               pe_ch_records_cleared(), g_ch_def_count);
    }
    return 0;
}

void pe_ch_records_register(const char *mod_id) {
    const int cleared = pe_event_id(CH_EVENT_CLEARED);
    if (cleared >= 0) pe_hook_add(cleared, pe_records_on_cleared, NULL, PE_PRIORITY_NORMAL, mod_id);
}
