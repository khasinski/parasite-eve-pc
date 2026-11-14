/* New Game setup, independent of combat styles. Work on a draft until Start
 * game; headless/unavailable questions leave the F1 picks untouched. */
#include "pe_challenges.h"
#include "pe_mod_menu.h"
#include <stdio.h>
#include <string.h>

int pe_ch_start_pick(int levels[CH_COUNT]) {
  int draft[CH_COUNT], cursor = 0;
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
    names[0] = "Start game";
    descriptions[0] = summary;
    names[1] = "Clear all challenges";
    descriptions[1] = "Switch every challenge off for this new game. Your "
                      "combat style is unchanged.";
    for (unsigned i = 0; i < g_ch_def_count; ++i) {
      const PeChallengeDef *d = g_ch_defs[i];
      const int level = draft[d->id];
      const char *value = !level ? "Off" : d->levels ? d->levels[level] : "On";
      snprintf(labels[i + 2], sizeof labels[i + 2], "%s: %s", d->short_name,
               value);
      names[i + 2] = labels[i + 2];
      descriptions[i + 2] = d->description;
    }
    int choice = pe_menu_choose_title(
        "Choose your challenges", "Up/Down select   Cross/Enter choose", names,
        descriptions, g_ch_def_count + 2, cursor);
    if (choice < 0)
      return 0;
    if (!choice) {
      memcpy(levels, draft, sizeof draft);
      return 1;
    }
    cursor = choice;
    if (choice == 1) {
      memset(draft, 0, sizeof draft);
      continue;
    }
    if (choice >= (int)g_ch_def_count + 2)
      return 0;
    const PeChallengeDef *d = g_ch_defs[choice - 2];
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
