/* Debug-control UI state: the native menu layer, open dialogue boxes and the pages dialogue
 * recently showed. See docs/DEBUG_CONTROL.md. Reads only. */
#include "pe_debug_internal.h"
#include <stdint.h>
#include <string.h>
#include "pe_game.h"
#include "pe_hooks.h"
#include "pe_mod_host.h"
#include "pe_gametext.h"
#include "pe1/menu_dialog.h"
#include "pe1/menu_state.h"
#include "pe1/save.h"
#include "pe1/textbox.h"
#include "pe1/textbox_open.h"

#define PE_UI_PAGES 8
#define PE_UI_TEXT 400

/* The last pages dialogue boxes opened, as the box draws them (after language packs and mods
 * replaced the text), so a test can read a page that has already closed. */
static struct { unsigned frame; int map; unsigned page; char text[PE_UI_TEXT]; } s_pages[PE_UI_PAGES];
static unsigned s_page_next;

static int pe_ui_text_page(void *ctx, void *user) {
    PeHookTextPage *q = ctx;
    unsigned at = s_page_next++ % PE_UI_PAGES;
    (void)user;
    s_pages[at].frame = pe_debug_frame();
    s_pages[at].map = pe_room_number();
    s_pages[at].page = q ? q->page : 0;
    s_pages[at].text[0] = 0;
    if (q && q->message && *q->message) pe_text_to_ascii(s_pages[at].text, sizeof s_pages[at].text, *q->message);
    return 0;
}

static void pe_ui_install(void) {
    /* Late: language packs and mods replace the text first. */
    pe_hook_add(PE_HOOK_TEXT_PAGE, pe_ui_text_page, NULL, 10000, "host");
}

/* Some mods rewrite a box's text after it opened (Wayne's menu rows): while a box is open, its
 * page's newest entry follows the text the box draws, so it still holds that after it closes. */
static void pe_ui_frame(void) {
    unsigned i, k;
    for (i = 0; i < 4; ++i) {
        const TextboxEntry *box = &g_TextboxEntries[i];
        if (!box->state || !box->message) continue;
        for (k = 0; k < PE_UI_PAGES && k < s_page_next; ++k) {
            unsigned at = (s_page_next - 1 - k) % PE_UI_PAGES;
            if (s_pages[at].page != (unsigned)(unsigned short)box->page_id) continue;
            pe_text_to_ascii(s_pages[at].text, sizeof s_pages[at].text, box->message);
            break;
        }
    }
}

void pe_debug_ui_write(FILE *f) {
    char text[PE_UI_TEXT];
    unsigned i, first = 1, start = s_page_next > PE_UI_PAGES ? s_page_next - PE_UI_PAGES : 0;
    fprintf(f, "{\"menu\":{\"open\":%s,\"mode\":%u,\"item\":%d,\"action_open\":%s,\"action\":%u,"
            "\"memcard_dialog\":%d,\"save_slot\":%d},\"dialogue\":[",
            (g_GameStateFlags & 4) ? "true" : "false", (unsigned)g_MenuActiveMode, g_InvSelectedItemIndex,
            g_MenuActionSubmenuOpen ? "true" : "false", g_MenuActionSubmenuSel, g_MemCardDialogState,
            g_SaveSelectedSlot);
    for (i = 0; i < 4; ++i) {
        const TextboxEntry *box = &g_TextboxEntries[i];
        int choices = box->control.flags & 0x200000 ? (box->control.flags >> 0x16) & 7 : 0;
        if (!box->state) continue;
        text[0] = 0;
        if (box->message) pe_text_to_ascii(text, sizeof text, box->message);
        fprintf(f, "%s{\"box\":%u,\"state\":\"%s\",\"page\":%d,\"style\":%u,\"rect\":[%u,%u,%u,%u],\"text\":",
                first ? "" : ",", i, box->state == 1 ? "typing" : box->state == 2 ? "waiting" : "other",
                box->page_id, box->style, box->x, box->y, box->width, box->height);
        pe_debug_json_string(f, text);
        fprintf(f, ",\"choices\":%d,\"choice\":%d}", choices, choices ? (int)(signed char)g_DialogueChoiceCursor : -1);
        first = 0;
    }
    fputs("],\"recent_pages\":[", f);
    for (i = start; i < s_page_next; ++i) {
        fprintf(f, "%s{\"frame\":%u,\"map\":%d,\"page\":%u,\"text\":", i != start ? "," : "",
                s_pages[i % PE_UI_PAGES].frame, s_pages[i % PE_UI_PAGES].map, s_pages[i % PE_UI_PAGES].page);
        pe_debug_json_string(f, s_pages[i % PE_UI_PAGES].text);
        fputc('}', f);
    }
    fputs("]}", f);
}

static void pe_ctl_ui_state(const char *args) {
    char path[1100];
    FILE *f;
    (void)args;
    pe_debug_path(path, sizeof path, "out");
    if (!(f = fopen(path, "a"))) return;
    fputs("ok ui-state ", f);
    pe_debug_ui_write(f);
    fputc('\n', f);
    fclose(f);
}

static const PeDebugCommand s_commands[] = {
    {"ui-state", PE_DEBUG_EXACT, pe_ctl_ui_state},
};

const PeDebugModule pe_debug_ui_module = {
    "ui", s_commands, sizeof s_commands / sizeof *s_commands, pe_ui_install, pe_ui_frame
};
