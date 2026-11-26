/* Hold to skip dialogue (issue 246). A dialogue box shows its whole page at once and waits for
 * the action button (Cross) to turn the page or close; holding the chosen button presses it for
 * the player every few frames, as mashing Cross would. It never answers a question: a page that
 * shows choices (its FB 09 command) is left for the player, and so is every box on screen with
 * it. Off by default. */
#include <stdint.h>
#include "pe_hooks.h"
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe1/field_pad.h"
#include "pe1/textbox.h"

#define PE_SKIP_ACTION   0x100u   /* the cooked field action edge the boxes read (Cross) */
#define PE_SKIP_INTERVAL 3        /* frames between presses: each page is drawn before the next */
#define PE_SKIP_SCAN     1024     /* page bytes looked at for a choice before giving up (no skip) */

static int s_button;
static unsigned s_held, s_ticks;
static const char *const s_button_names[] = { "Off", "Square", "R1", "L1" };
/* The decoded PS1 pad bits PE_HOOK_PAD_MAP reports (Cross and Circle already swapped). */
static const unsigned s_button_bits[] = { 0, 0x8000u, 0x0800u, 0x0400u };

/* 1 when the page box->message points at may ask a question (or can't be read to its end). */
static int pe_skip_page_asks(const TextboxEntry *box) {
    const uint8_t *at = box->message;
    unsigned i;
    if (box->control.flags & 0x200000) return 1;     /* the box drew its choices */
    if (!at) return 1;
    for (i = 0; i + 1 < PE_SKIP_SCAN; ++i) {
        uint8_t c = at[i];
        if (c == 0xF8 || c == 0xF9 || c == 0xFF) return 0;
        if (c == 0xFB && at[i + 1] == 9) return 1;
    }
    return 1;
}

static int pe_skip_on_pad_map(void *ctx, void *user) {
    const PeHookPadMap *pad = ctx;
    (void)user;
    s_held = pad ? pad->buttons : 0;
    return 0;
}

static int pe_skip_on_textboxes(void *ctx, void *user) {
    unsigned i, waiting = 0;
    unsigned bit;
    (void)ctx; (void)user;
    bit = s_button > 0 && s_button < (int)(sizeof s_button_bits / sizeof s_button_bits[0]) ? s_button_bits[s_button] : 0;
    if (!bit || !(s_held & bit) || !pe_game_loaded()) {
        s_ticks = 0;
        return 0;
    }
    for (i = 0; i < 4; ++i) {
        const TextboxEntry *box = &g_TextboxEntries[i];
        if (!box->state) continue;
        if (box->state != 2 || pe_skip_page_asks(box)) {   /* opening, or a question: wait */
            s_ticks = 0;
            return 0;
        }
        waiting = 1;
    }
    if (!waiting) {
        s_ticks = 0;
        return 0;
    }
    if (++s_ticks >= PE_SKIP_INTERVAL) {
        g_FieldPadPressed |= PE_SKIP_ACTION;
        s_ticks = 0;
    }
    return 0;
}

void pe_dialogue_skip_register(const char *mod_id) {
    pe_menu_register_choice(PE_PAGE_MOVEMENT, "Hold to skip dialogue",
        "Hold this button to turn dialogue pages quickly. Questions still wait for you.",
        &s_button, s_button_names, sizeof s_button_names / sizeof s_button_names[0]);
    pe_hook_add(PE_HOOK_PAD_MAP, pe_skip_on_pad_map, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_TEXTBOXES, pe_skip_on_textboxes, NULL, PE_PRIORITY_LATE, mod_id);
}
