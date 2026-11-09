/* Challenges: Hardcore saves marked in the game's own save / load lists (the save phone's card
 * screen and the title's Load screen are the same menu: Menu_DrawSaveSlotEntry draws each slot).
 *
 *   The row     each slot is drawn in its save's window colour (slot->blendColor, the colour the
 *               player picked, which the game keeps per save). A Hardcore save is shown in a dark
 *               red window, a fallen one in grey: only the menu's copy (g_MemCardPortStates) is
 *               changed, never the card. A rescan of the card puts the save's own colour back and it
 *               is marked again.
 *   Title bar   with a Hardcore slot highlighted, the menu's title bar (PE_HOOK_HELP_TEXT, screens
 *               37 / 38: card 1 / 2) says so: its challenges, or that it fell and can't be played;
 *               when saving, that the slot holds a Hardcore run.
 *   Which saves each occupied slot's file (bu<port>0:BASLUS-00662000000<flag><slot>, mapped by the
 *               game's own Psyz_AdjustPath) is read once and its "pe_challenges.run" record peeked
 *               (pe_save_peek: read-only, no mod code runs). It is read again when the slot's preview
 *               changes (a save over it). Fallen = its run id is fallen in the run history.
 * Reading only: no save, companion or record is written. Works for any card file, whatever build
 * wrote it; a save without the record is just not Hardcore.
 */
#include <stdio.h>
#include <string.h>
#include <psyz/system.h>

#include "pe_challenges.h"
#include "pe_gametext.h"
#include "pe_mod_host.h"
#include "pe_save.h"
#include "pe1/memcard.h"
#include "pe1/menu_context_help.h"
#include "pe1/menu_widget.h"

#define CH_PORTS          2
#define CH_SLOTS          15
#define CH_LIST_CARD1     0x25        /* the slot list's selected_base: 0x25 + port */
#define CH_CARD_BYTES     0x2000
#define CH_RED_WINDOW     0x14148Cu   /* the game's colour order (B G R): dark red */
#define CH_FALLEN_WINDOW  0x303030u   /* grey */

enum { CH_MARK_NONE, CH_MARK_HARDCORE, CH_MARK_FALLEN };

typedef struct PeSlotMark {
    int checked, mark;
    uint32_t run_id, active;
    int32_t key_play, key_game;       /* the preview the mark was made for */
    int16_t key_hp, key_max;
    uint8_t key_flag;
} PeSlotMark;

static PeSlotMark s_marks[CH_PORTS][CH_SLOTS];
static u8 s_text[96];
static uint8_t s_card[CH_CARD_BYTES];

static int pe_preview_changed(const PeSlotMark *m, const MemCardSaveSlot *slot) {
    return !m->checked || m->key_play != slot->playTimeMinutes || m->key_game != slot->gameTimeMinutes ||
           m->key_hp != slot->currentHp || m->key_max != slot->maxHp || m->key_flag != slot->titleStyleFlag;
}

/* Read the slot's file and peek its run record. */
static void pe_check_slot(int port, int index, const MemCardSaveSlot *slot, PeSlotMark *m) {
    char card_name[32], path[512];
    PeChallengeRun run;
    unsigned version = 0, size = 0;
    FILE *f;
    memset(m, 0, sizeof *m);
    m->checked = 1;
    m->key_play = slot->playTimeMinutes;
    m->key_game = slot->gameTimeMinutes;
    m->key_hp = slot->currentHp;
    m->key_max = slot->maxHp;
    m->key_flag = slot->titleStyleFlag;
    snprintf(card_name, sizeof card_name, "bu%d0:BASLUS-00662000000%c%c", port, slot->titleStyleFlag + '0', index + 'A');
    Psyz_AdjustPath(path, card_name, (int)sizeof path);
    if (!(f = fopen(path, "rb"))) { pe_log("challenge_card", "card=%d slot=%d unreadable path=%s", port + 1, index + 1, path); return; }
    if (fread(s_card, 1, sizeof s_card, f) != sizeof s_card) {
        fclose(f);
        pe_log("challenge_card", "card=%d slot=%d short file", port + 1, index + 1);
        return;
    }
    fclose(f);
    memset(&run, 0, sizeof run);
    if (pe_save_peek(s_card, "pe_challenges.run", &version, &run, sizeof run, &size) != 1) return;   /* no record: not a challenge save */
    if (version != 2 || size != sizeof run || !(run.active & CH_BIT(CH_HARDCORE))) return;
    m->run_id = run.run_id;
    m->active = run.active;
    m->mark = pe_ch_run_is_dead(run.run_id) ? CH_MARK_FALLEN : CH_MARK_HARDCORE;
    pe_log("challenge_card", "card=%d slot=%d run=%08x %s", port + 1, index + 1, (unsigned)run.run_id,
           m->mark == CH_MARK_FALLEN ? "fallen" : "hardcore");
}

/* Every listed slot: check new or changed ones, and keep marked ones in their window colour. A slot
 * counts once the game has listed it and read its preview (its own state and metadataReady): the
 * card manager's state keeps moving (3, 8, 10, 12 ...) while it lists a card, so it isn't used. */
static void pe_refresh(void) {
    for (int port = 0; port < CH_PORTS; ++port) {
        MemCardPortState *card = &g_MemCardPortStates[port];
        for (int i = 0; i < CH_SLOTS; ++i) {
            MemCardSaveSlot *slot = &card->slots[i];
            PeSlotMark *m = &s_marks[port][i];
            if (slot->state != MEMCARD_SLOT_OCCUPIED || !slot->metadataReady) { m->checked = 0; continue; }
            if (pe_preview_changed(m, slot)) pe_check_slot(port, i, slot, m);
            if (m->mark == CH_MARK_HARDCORE) slot->blendColor = (s32)CH_RED_WINDOW;
            else if (m->mark == CH_MARK_FALLEN) slot->blendColor = (s32)CH_FALLEN_WINDOW;
        }
    }
}

/* Before the slot list draws its rows. */
static int pe_marks_rows(void *ctx, void *user) {
    const PeHookListRows *rows = ctx;
    (void)user;
    if (rows && rows->node && (rows->node->selected_base == CH_LIST_CARD1 || rows->node->selected_base == CH_LIST_CARD1 + 1))
        pe_refresh();
    return 0;
}

/* The title bar over a highlighted Hardcore slot. */
static int pe_marks_help(void *ctx, void *user) {
    u8 **text = (u8 **)ctx;
    MenuWidgetNode *node = MenuWidget_GetCurrentNode();
    const PeSlotMark *m;
    char line[96], names[64];
    int port, index;
    (void)user;
    if (!node || (node->selected_base != CH_LIST_CARD1 && node->selected_base != CH_LIST_CARD1 + 1)) return 0;
    port = node->selected_base - CH_LIST_CARD1;
    index = MenuWidget_GridCellIndex(MenuWidget_FindByModeAndSelectedBase(2, node->selected_base));   /* as the game's title bar */
    if (index < 0 || index >= CH_SLOTS) return 0;
    pe_refresh();
    m = &s_marks[port][index];
    if (m->mark == CH_MARK_NONE) return 0;
    pe_ch_names(m->active & ~CH_BIT(CH_HARDCORE), names, sizeof names);
    if (m->mark == CH_MARK_FALLEN)
        snprintf(line, sizeof line, g_McDialogMode ? "Fallen Hardcore run: free to save over"
                                                   : "Fallen Hardcore run: it can't be played");
    else if (g_McDialogMode)
        snprintf(line, sizeof line, m->run_id == g_ch_run.run_id ? "Hardcore run: this run's save"
                                                                  : "Saving here replaces a Hardcore run");
    else
        snprintf(line, sizeof line, "Hardcore run   %s", strcmp(names, "None") ? names : "no other challenges");
    *text = pe_text_from_ascii(s_text, sizeof s_text, line);
    return 0;
}

void pe_ch_card_marks_register(const char *mod_id) {
    pe_hook_add(PE_HOOK_LIST_ROWS, pe_marks_rows, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_HELP_TEXT, pe_marks_help, NULL, PE_PRIORITY_LATE, mod_id);
}
