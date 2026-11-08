/* Phone Calls' pure core (mods/pe_phone/pe_phone_core.h) and the game's own call pack, without the
 * game: laying calls out as pages, spotting the save question, the heard record, picking. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pe_phone.h"

#define NAME_W 27   /* "Aya" */

static int adv(uint8_t g) {
    if (g == PE_PHONE_GLYPH_NAME) return NAME_W;
    if (g == PE_PHONE_GLYPH_SPACE) return 4;
    return 8;
}

/* ---- text ---- */

static void test_text_ok(void) {
    assert(pe_phone_text_ok("Hello, {name}! It's 9:30 - 50% off* / ok?"));
    assert(!pe_phone_text_ok("semi; colon"));
    assert(!pe_phone_text_ok("\"quoted\""));
    assert(!pe_phone_text_ok("{nam}"));
    assert(!pe_phone_text_ok(NULL));
}

/* Each line's width without the window margin, and the line count. */
static int page_lines(const PePhonePage *p, int *widest) {
    int lines = 1, w = 0, i, start = p->style == PE_PHONE_WINDOW;
    *widest = 0;
    for (i = start; p->text[i] != 0xFF; ++i) {
        if (p->text[i] == 0xF7) { ++lines; w = 0; if (p->style == PE_PHONE_WINDOW) ++i; continue; }
        w += adv(p->text[i]);
        if (w > *widest) *widest = w;
    }
    return lines;
}

static const PePhoneLine k_long[] = {
    {PE_CALL_AYA, "One two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen "
                  "seventeen eighteen nineteen twenty twenty-one twenty-two twenty-three twenty-four twenty-five "
                  "twenty-six twenty-seven twenty-eight twenty-nine thirty."},
    {PE_CALL_CALLER, "Short."},
};
static const PePhoneCall k_long_call = {"test/long", "Test Caller", PE_CALL_SERIOUS, k_long, 2, 1, 0, 0, 0, 0, 0, NULL, 0, 0, NULL};

static void test_script(void) {
    static PePhoneScript s;
    int i, w, wide, ran;
    pe_phone_script_build(&s, &k_long_call, adv);
    assert(s.count >= 5 && s.click == s.count - 1);
    assert(s.pages[0].style == PE_PHONE_BAND && s.pages[0].text[0] == 0x0D);          /* "*dial..." */
    for (i = 1; i < s.click - 1; ++i) {                                               /* Aya's boxes */
        const PePhonePage *p = &s.pages[i];
        assert(p->style == PE_PHONE_BAND);
        assert(p->text[0] == PE_PHONE_GLYPH_NAME);                                    /* the name on every box */
        assert(page_lines(p, &w) <= PE_PHONE_MAX_LINES && p->lines <= PE_PHONE_MAX_LINES);
        assert(w <= PE_PHONE_TEXT_MAX_W && p->width == w);
    }
    {
        const PePhonePage *c = &s.pages[s.click - 1];                                 /* the caller's window */
        uint8_t prefix[16];
        const int n = pe_phone_encode(prefix, sizeof prefix, "Test Caller:");
        assert(c->style == PE_PHONE_WINDOW && c->text[0] == PE_PHONE_GLYPH_SPACE);
        assert(!memcmp(c->text + 1, prefix, (size_t)n));
        assert(page_lines(c, &w) == 1 && c->width == w + 2 * adv(PE_PHONE_GLYPH_SPACE));
    }
    assert(pe_phone_script_problems(&s, &k_long_call, &wide, &ran) == 1 && wide == 0 && ran == s.count - 4);

    pe_phone_script_build(&s, NULL, adv);                                             /* nobody answers */
    assert(s.count == 4 && s.click == 3);
}

static void test_save_question(void) {
    uint8_t m[128];
    int n = pe_phone_encode(m, 100, "Will you save your progress?");
    m[n++] = 0xF7; m[n++] = 0xFB; m[n++] = 0x09; m[n++] = 2;
    n += pe_phone_encode(m + n, 20, "    Yes");
    m[n] = 0xFF;
    assert(pe_phone_save_question(m) == 1);
    memmove(m + 1, m, (size_t)n + 1);
    m[0] = PE_PHONE_GLYPH_NAME;                                                       /* the story save */
    assert(pe_phone_save_question(m) == 2);
    n = pe_phone_encode(m, 100, "Will you save your progress?");
    m[n] = 0xFF;
    assert(pe_phone_save_question(m) == 0);                                           /* no choice */
    assert(pe_phone_save_question(NULL) == 0);
}

/* ---- the record ---- */

static const PePhoneLine k_line[] = {{PE_CALL_CALLER, "Hi."}};
#define CALL(key, tone, w, once, df, dt, prio, after) {key, "X", tone, k_line, 1, w, once, df, dt, 0, 0, NULL, 0, prio, after}

static void test_record(void) {
    static PePhoneCall calls[20];
    static char keys[20][16];
    PePhoneRecord r;
    int i;
    memset(&r, 0, sizeof r);
    for (i = 0; i < 20; ++i) {
        snprintf(keys[i], sizeof keys[i], "t/c%d", i);
        calls[i] = (PePhoneCall)CALL(keys[i], PE_CALL_FUNNY, 1, 1, 0, 0, 0, NULL);
    }
    pe_phone_record_played(&r, &calls[0]);
    pe_phone_record_played(&r, &calls[1]);
    assert(pe_phone_record_recent(&r, "t/c1") == 1 && pe_phone_record_recent(&r, "t/c0") == 2);
    pe_phone_record_played(&r, &calls[0]);                                            /* back to the front */
    assert(pe_phone_record_recent(&r, "t/c0") == 1 && r.last_count == 2);
    for (i = 2; i < 7; ++i) pe_phone_record_played(&r, &calls[i]);
    assert(r.last_count == PE_PHONE_LAST && !pe_phone_record_recent(&r, "t/c1"));     /* the oldest left */
    assert(pe_phone_record_once(&r, "t/c1") && pe_phone_record_heard(&r, "t/c1"));
    for (i = 7; i < 20; ++i) pe_phone_record_played(&r, &calls[i]);
    assert(r.heard_count == PE_PHONE_HEARD && !pe_phone_record_once(&r, "t/c0") && pe_phone_record_once(&r, "t/c19"));
    assert(pe_phone_record_valid(&r));
    r.last_count = 9;
    assert(!pe_phone_record_valid(&r));
    assert(pe_phone_key_hash("") != 0);
}

/* ---- picking ---- */

static void test_pick(void) {
    static const PePhoneCall serious_day2 = CALL("t/s2", PE_CALL_SERIOUS, 1, 0, 2, 3, 0, NULL);
    static const PePhoneCall funny_any = CALL("t/fa", PE_CALL_FUNNY, 3, 0, 0, 0, 0, NULL);
    static const PePhoneCall funny_once = CALL("t/fo", PE_CALL_FUNNY, 1, 1, 0, 0, 0, NULL);
    static const PePhoneCall set_call = CALL("t/set", PE_CALL_SERIOUS, 1, 0, 0, 0, 5, NULL);
    static const PePhoneCall chained = CALL("t/next", PE_CALL_SERIOUS, 1, 0, 0, 0, 0, "t/set");
    const PePhoneCall *all[] = {&serious_day2, &funny_any, &funny_once, &set_call, &chained};
    PePhoneContext x = {87, 0xF8, 2, 3};
    PePhoneRecord r;
    uint32_t roll;
    memset(&r, 0, sizeof r);

    assert(pe_phone_pick(all, 5, &x, &r, 0) == &set_call);                           /* a set call first */
    pe_phone_record_played(&r, &set_call);
    assert(pe_phone_fits(&chained, &x, &r));                                          /* `after` heard */
    x.day = 4;
    assert(!pe_phone_fits(&serious_day2, &x, &r));
    x.day = 2;
    x.tone_mask = 1 << PE_CALL_FUNNY;
    for (roll = 0; roll < 4; ++roll)                                                  /* weights 3 + 1 */
        assert(pe_phone_pick(all, 5, &x, &r, roll) == (roll < 3 ? &funny_any : &funny_once));
    pe_phone_record_played(&r, &funny_once);
    assert(!pe_phone_fits(&funny_once, &x, &r));                                      /* once: heard */
    assert(pe_phone_pick(all, 5, &x, &r, 7) == &funny_any);
    pe_phone_record_played(&r, &funny_any);
    assert(pe_phone_pick(all, 5, &x, &r, 7) == &funny_any);                           /* the only one left */
    x.tone_mask = 1 << PE_CALL_SERIOUS;
    assert(pe_phone_pick(all, 5, &x, &r, 0) == &serious_day2 || pe_phone_pick(all, 5, &x, &r, 0) == &chained);
    pe_phone_record_played(&r, &serious_day2);
    assert(pe_phone_pick(all, 5, &x, &r, 0) == &chained);                             /* not the last five */
    x.tone_mask = 0;
    assert(pe_phone_pick(all, 5, &x, &r, 0) == NULL);
}

/* ---- the game's own pack ---- */

static const PePhoneCall *s_pack[64];
static int s_pack_count;
static int fake_add(const PePhoneCall *c) { s_pack[s_pack_count++] = c; return 0; }

static void test_pe1_pack(void) {
    static const PePhoneCallsApi api = {fake_add, NULL, NULL, NULL, NULL};
    static PePhoneScript s;
    int i, k, funny = 0;
    pe_calls_pe1_register(&api);
    assert(s_pack_count == 21);
    for (i = 0; i < s_pack_count; ++i) {
        const PePhoneCall *c = s_pack[i];
        assert(!strncmp(c->key, "pe1/", 4) && pe_phone_text_ok(c->caller));
        for (k = 0; k < i; ++k) assert(strcmp(c->key, s_pack[k]->key));
        for (k = 0; k < c->line_count; ++k) assert(pe_phone_text_ok(c->lines[k].text));
        assert(c->day_from >= 0 && c->day_to <= 6 && (!c->day_to || c->day_to >= c->day_from));
        pe_phone_script_build(&s, c, adv);
        assert(s.count >= c->line_count + 2 && s.click == s.count - 1);
        funny += c->tone == PE_CALL_FUNNY;
    }
    assert(funny == 9);
}

int main(void) {
    test_text_ok();
    test_script();
    test_save_question();
    test_record();
    test_pick();
    test_pe1_pack();
    puts("pe_phone: all checks passed");
    return 0;
}
