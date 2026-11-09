/* Challenges: the info rows. On the Challenges page: Run (Clean / Assisted) and Warnings (other
 * mods that can work around a challenge; never blocked). On Run summary: the run's status,
 * challenges on / used / lowered, cheats, battles won, knockouts and time. */
#include <stdio.h>
#include <string.h>

#include "pe_challenges.h"
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"

static void pe_info_status(char *out, unsigned size) {
    const char *hardcore = pe_ch_on(CH_HARDCORE) ? ", Hardcore (locked)" : pe_ch_run_locked() ? ", locked" : "";
    if (!pe_save_loaded()) snprintf(out, size, "No game loaded");
    else if (!g_ch_run.ever) snprintf(out, size, "No challenges");
    else if (pe_ch_assisted()) snprintf(out, size, "Assisted%s", hardcore);
    else snprintf(out, size, "Clean%s", hardcore);
}

/* The challenges on now; a level row shows its level ("Tough crowd 2x HP"). */
static void pe_info_active(char *out, unsigned size) {
    size_t used = 0;
    out[0] = 0;
    for (unsigned i = 0; i < g_ch_def_count && used + 1 < size; ++i) {
        const PeChallengeDef *d = g_ch_defs[i];
        const int level = pe_ch_level(d->id);
        if (!level) continue;
        used += (size_t)snprintf(out + used, size - used, "%s%s", used ? ", " : "", d->short_name);
        if (used < size && d->levels && level < (int)d->level_count)
            used += (size_t)snprintf(out + used, size - used, " %s", d->levels[level]);
        if (used >= size) used = size - 1;
    }
    if (!out[0]) snprintf(out, size, "None");
}

static void pe_info_ever(char *out, unsigned size) { pe_ch_names(g_ch_run.ever, out, size); }
static void pe_info_dropped(char *out, unsigned size) { pe_ch_names(g_ch_run.dropped, out, size); }
static void pe_info_cheats(char *out, unsigned size) {
    snprintf(out, size, "%s", g_ch_run.flags & CH_FLAG_CHEATS ? "Yes" : "No");
}
static void pe_info_battles(char *out, unsigned size) { snprintf(out, size, "%u", (unsigned)g_ch_run.battles); }
static void pe_info_knockouts(char *out, unsigned size) { snprintf(out, size, "%u", pe_ch_knockouts()); }
static void pe_info_time(char *out, unsigned size) {
    const unsigned s = g_ch_run.seconds;
    snprintf(out, size, "%u:%02u:%02u", s / 3600, s / 60 % 60, s % 60);
}

/* A warning: the challenge, the mod that works around it, and what it does. */
typedef struct PeChallengeWarning {
    int challenge;     /* -1: any challenge */
    const char *mod;   /* NULL: a cheat row is on */
    const char *text;
} PeChallengeWarning;

static const PeChallengeWarning kWarnings[] = {
    {CH_NO_ITEMS, "pe_shops", "shops still sell items"},
    {CH_NO_ESCAPE, "pe_economy", "Smoke Grenades still escape"},
    {CH_NO_EXP, "pe_rewards", "battle Junk scales with EXP, so less Junk"},
    {-1, NULL, "a cheat is on (run assisted)"},
};

static void pe_info_warnings(char *out, unsigned size) {
    const unsigned want = pe_ch_rows_mask();
    size_t used = 0;
    out[0] = 0;
    for (size_t i = 0; i < sizeof kWarnings / sizeof kWarnings[0] && used + 1 < size; ++i) {
        const PeChallengeWarning *w = &kWarnings[i];
        const int wanted = w->challenge < 0 ? want != 0 : (want & CH_BIT(w->challenge)) != 0;
        const int applies = w->mod ? pe_mod_active(w->mod) : pe_ch_cheat_row_on() != NULL;
        if (!wanted || !applies) continue;
        used += (size_t)snprintf(out + used, size - used, "%s%s", used ? "; " : "", w->text);
        if (used >= size) used = size - 1;
    }
    if (!out[0]) snprintf(out, size, "None");
}

int pe_ch_summary_register(void) {
    int failed = 0;
    failed |= pe_menu_register_info(0, "Run", "Clean, or Assisted once a challenge was lowered or a cheat used.",
        pe_info_status);
    failed |= pe_menu_register_info(0, "Warnings", "Other mods that can work around a challenge (never blocked).",
        pe_info_warnings);

    pe_menu_page(CH_PAGE_RUN, "This game's run: challenges, battles won, knockouts and time.", 20);
    failed |= pe_menu_register_info(CH_PAGE_RUN, "Status", "Clean, Assisted, or No challenges.", pe_info_status);
    failed |= pe_menu_register_info(CH_PAGE_RUN, "On now", "The challenges on now.", pe_info_active);
    failed |= pe_menu_register_info(CH_PAGE_RUN, "Used this run", "Every challenge that was on at some point.",
        pe_info_ever);
    failed |= pe_menu_register_info(CH_PAGE_RUN, "Lowered mid-run",
        "Challenges turned off or lowered after the start.", pe_info_dropped);
    failed |= pe_menu_register_info(CH_PAGE_RUN, "Cheats used",
        "A cheat or playtest assist while a challenge was on.", pe_info_cheats);
    failed |= pe_menu_register_info(CH_PAGE_RUN, "Battles won", "Battles won with a challenge on.", pe_info_battles);
    failed |= pe_menu_register_info(CH_PAGE_RUN, "Knockouts",
        "Times Aya fell in battle this run (kept across reloads).", pe_info_knockouts);
    failed |= pe_menu_register_info(CH_PAGE_RUN, "Time", "Play time with a challenge on (pauses left out).",
        pe_info_time);
    return failed ? -1 : 0;
}
