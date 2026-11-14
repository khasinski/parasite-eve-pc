/* New Game setup, independent of combat styles. Work on a draft until Start
 * game; headless/unavailable questions leave the F1 picks untouched. */
#include "pe_challenges.h"
#include "pe_mod_menu.h"
#include <stdio.h>
#include <string.h>

int pe_ch_start_pick(int levels[CH_COUNT]) {
  int draft[CH_COUNT], cursor = 0;
  const PeChallengeDef *rows[CH_COUNT];
  unsigned row_count = 0;
  /* Feature Hardcore first without changing the catalog or saved challenge ids.
   */
  rows[row_count++] = pe_ch_def(CH_HARDCORE);
  for (unsigned i = 0; i < g_ch_def_count; ++i)
    if (g_ch_defs[i]->id != CH_HARDCORE)
      rows[row_count++] = g_ch_defs[i];
  const int clear_row = (int)row_count, start_row = clear_row + 1;
  memcpy(draft, levels, sizeof draft);
  for (;;) {
    char labels[CH_COUNT + 2][96], summary[256];
    const char *names[CH_COUNT + 2], *descriptions[CH_COUNT + 2];
    unsigned selected = 0;
    for (int id = 0; id < CH_COUNT; ++id)
      selected += draft[id] != 0;
    snprintf(summary, sizeof summary, "%u challenge%s selected.%s", selected,
             selected == 1 ? "" : "s",
             draft[CH_HARDCORE]
                 ? " Hardcore: a game over ends this run permanently; "
                   "challenges cannot be lowered after starting."
                 : " Combine any challenges, or start with all of them off. "
                   "Combat style is chosen separately.");
    names[start_row] = "Start game";
    descriptions[start_row] = summary;
    names[clear_row] = "Clear all challenges";
    descriptions[clear_row] =
        "Switch every challenge off for this new game. Your "
        "combat style is unchanged.";
    for (unsigned i = 0; i < row_count; ++i) {
      const PeChallengeDef *d = rows[i];
      const int level = draft[d->id];
      const char *value = !level ? "Off" : d->levels ? d->levels[level] : "On";
      snprintf(labels[i], sizeof labels[i], "%s: %s", d->short_name, value);
      names[i] = labels[i];
      descriptions[i] = d->description;
    }
    int choice = pe_menu_choose_title(
        "Choose your challenges", "Up/Down select   Cross/Enter choose", names,
        descriptions, g_ch_def_count + 2, cursor);
    if (choice < 0)
      return 0;
    if (choice == start_row) {
      memcpy(levels, draft, sizeof draft);
      return 1;
    }
    cursor = choice;
    if (choice == clear_row) {
      memset(draft, 0, sizeof draft);
      continue;
    }
    if (choice >= (int)row_count)
      return 0;
    const PeChallengeDef *d = rows[choice];
    const char *options[CH_MAX_LEVELS + 1], *help[CH_MAX_LEVELS + 1];
    unsigned count = d->levels ? d->level_count : 2;
    for (unsigned i = 0; i < count; ++i) {
      options[i] = d->levels ? d->levels[i] : i ? "On" : "Off";
      help[i] =
          i ? d->description
            : "This challenge will be off. Other selections are unchanged.";
    }
    options[count] = "Back";
    help[count] = "Return to challenge setup without changing this option.";
    int level =
        pe_menu_choose_title(d->label, "Up/Down select   Cross/Enter choose",
                             options, help, count + 1, draft[d->id]);
    if (level < 0)
      return 0;
    if (level < (int)count)
      draft[d->id] = level;
  }
}
