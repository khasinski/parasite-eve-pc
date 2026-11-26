/* pe_qol: hold to skip dialogue (issue 246) and the Auto Medicine order (issue 238). */
#ifdef NDEBUG
#undef NDEBUG /* These runtime checks must also execute in release CI builds. */
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pe_hooks.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe1/field_pad.h"
#include "pe1/item_ids.h"
#include "pe1/textbox.h"

u32 g_FieldPadBits, g_FieldPadPressed;
TextboxEntry g_TextboxEntries[4];
static int loaded = 1, *skip_button, *medicine_order;
static PeHookFn on_pad_map, on_textboxes, on_medicine;

int pe_game_loaded(void) { return loaded; }
int pe_menu_register_choice(const char *page, const char *label, const char *description,
                            int *index, const char *const *names, unsigned count) {
    (void)page; (void)description; (void)names;
    if (!strcmp(label, "Hold to skip dialogue")) { assert(count == 4); skip_button = index; }
    else { assert(!strcmp(label, "Auto Medicine order") && count == 2); medicine_order = index; }
    return 0;
}
int pe_hook_add(int id, PeHookFn fn, void *user, int priority, const char *mod_id) {
    (void)user; (void)priority; (void)mod_id;
    if (id == PE_HOOK_PAD_MAP) on_pad_map = fn;
    else if (id == PE_HOOK_TEXTBOXES) on_textboxes = fn;
    else { assert(id == PE_HOOK_AUTO_MEDICINE); on_medicine = fn; }
    return 0;
}
void pe_dialogue_skip_register(const char *mod_id);
void pe_auto_medicine_register(const char *mod_id);

/* One frame: the pad poll with these decoded buttons, then the boxes; 1 if Cross was pressed. */
static int frame(unsigned buttons) {
    int run_mask = 0;
    PeHookPadMap pad = {buttons, &run_mask};
    g_FieldPadPressed = 0;
    on_pad_map(&pad, NULL);
    on_textboxes(NULL, NULL);
    return (g_FieldPadPressed & 0x100) != 0;
}
static int presses(unsigned buttons, int frames) {
    int n = 0;
    while (frames-- > 0) n += frame(buttons);
    return n;
}

int main(void) {
    static unsigned char plain[] = {0x10, 0x11, 0xF8, 0x12, 0xFF};
    static unsigned char question[] = {0x10, 0xF7, 0xFB, 0x09, 0x02, 0x13, 0xF7, 0x14, 0xFF};
    static unsigned char unended[2048];
    pe_dialogue_skip_register("pe_qol");
    pe_auto_medicine_register("pe_qol");
    assert(skip_button && medicine_order && on_pad_map && on_textboxes && on_medicine);

    /* Off by default: holding anything presses nothing. */
    g_TextboxEntries[0].state = 2; g_TextboxEntries[0].message = plain;
    assert(presses(0x8000u | 0x0800u | 0x0400u, 30) == 0);

    /* Square held on a waiting page: Cross every third frame; released, nothing. */
    *skip_button = 1;
    assert(presses(0x8000u, 9) == 3);
    assert(presses(0, 9) == 0);
    assert(presses(0x0800u, 9) == 0);          /* R1 isn't the chosen button */
    *skip_button = 2;
    assert(presses(0x0800u, 9) == 3);
    *skip_button = 1;

    /* A page with a question, or a box that drew its choices, waits for the player. */
    g_TextboxEntries[0].message = question;
    assert(presses(0x8000u, 30) == 0);
    g_TextboxEntries[0].message = plain; g_TextboxEntries[0].control.flags = 0x200000;
    assert(presses(0x8000u, 30) == 0);
    g_TextboxEntries[0].control.flags = 0;
    /* ... and so does every other box on screen with it. */
    g_TextboxEntries[1].state = 2; g_TextboxEntries[1].message = question;
    assert(presses(0x8000u, 30) == 0);
    g_TextboxEntries[1].state = 0;
    /* A page that can't be read to its end is treated as a question. */
    memset(unended, 0x10, sizeof unended);
    g_TextboxEntries[0].message = unended;
    assert(presses(0x8000u, 30) == 0);
    g_TextboxEntries[0].message = plain;

    /* A box still opening (state 1) restarts the count: the page is drawn before a press. */
    assert(presses(0x8000u, 2) == 0);
    g_TextboxEntries[0].state = 1;
    assert(frame(0x8000u) == 0);
    g_TextboxEntries[0].state = 2;
    assert(presses(0x8000u, 2) == 0 && frame(0x8000u) == 1);

    /* No box, or no game: nothing. */
    g_TextboxEntries[0].state = 0;
    assert(presses(0x8000u, 30) == 0);
    g_TextboxEntries[0].state = 2; loaded = 0;
    assert(presses(0x8000u, 30) == 0);
    loaded = 1;

    /* Auto Medicine: the native order stays unless Weakest first is chosen. */
    {
        const int native[5] = {ITEM_FULL_RECOVER, ITEM_MEDICINE_4, ITEM_MEDICINE_3, ITEM_MEDICINE_2, ITEM_MEDICINE_1};
        const int weakest[5] = {ITEM_MEDICINE_1, ITEM_MEDICINE_2, ITEM_MEDICINE_3, ITEM_MEDICINE_4, ITEM_FULL_RECOVER};
        int order[5];
        PeHookAutoMedicine query = {order, 5};
        memcpy(order, native, sizeof order);
        assert(on_medicine(&query, NULL) == 0 && !memcmp(order, native, sizeof order));
        *medicine_order = 1;
        assert(on_medicine(&query, NULL) == 0 && !memcmp(order, weakest, sizeof order));
        memcpy(order, native, sizeof order); query.count = 4;   /* an order it doesn't know: kept */
        assert(on_medicine(&query, NULL) == 0 && !memcmp(order, native, sizeof order));
    }
    puts("pe_qol dialogue skip and Auto Medicine order passed");
    return 0;
}
