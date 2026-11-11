/* Phone Calls' pure core (mods/pe_phone/pe_phone_core.h) and the game's own call pack, without the
 * game: laying calls out as pages, spotting the save question, the heard record, picking. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pe_phone.h"

#ifndef PE_PHONE_CALLS_FILE   /* CMake passes the absolute path; by hand, run from the checkout */
#define PE_PHONE_CALLS_FILE "mods/pe_phone/calls/pe1.txt"
#endif

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

    n = pe_phone_encode(m, 100, "{name}: ...There's a switch...");                   /* the sewer's emergency phone */
    m[n++] = 0xF8; m[n++] = 0xFB; m[n++] = 0x09; m[n++] = 3;
    n += pe_phone_encode(m + n, 30, "    Press the switch.");
    m[n++] = 0xF7;
    n += pe_phone_encode(m + n, 30, "    Use the phone.");
    m[n++] = 0xF7;
    n += pe_phone_encode(m + n, 30, "    Cancel");
    m[n] = 0xFF;
    assert(pe_phone_use_option(m) == 1);
    assert(pe_phone_save_question(m) == 0);
    n = pe_phone_encode(m, 100, "Use the phone.");                                    /* no choice */
    m[n] = 0xFF;
    assert(pe_phone_use_option(m) == -1);
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

static int s_warner_on;   /* the one condition these tests offer, as pe_shops would */
static int pe_holds(const char *name) { return !strcmp(name, "warner.supplies") && s_warner_on; }

static void test_needs(void) {
    PePhoneCall needy = CALL("t/needy", PE_CALL_SERIOUS, 1, 0, 0, 0, 9, NULL);
    PePhoneContext x = {87, 0xF8, 2, 3};
    PePhoneRecord r;
    memset(&r, 0, sizeof r);
    assert(pe_phone_condition_name_ok("warner.supplies") && pe_phone_condition_name_ok("a_b-1"));
    assert(!pe_phone_condition_name_ok("") && !pe_phone_condition_name_ok("two words") && !pe_phone_condition_name_ok("x;y"));
    assert(!pe_phone_condition_name_ok("abcdefghijklmnopqrstuvwxyz0123456"));   /* 33: too long */
    assert(pe_phone_needs_hold(NULL, NULL) && pe_phone_needs_hold("", NULL) && pe_phone_needs_hold("  ", pe_holds));
    s_warner_on = 0;
    needy.needs = "warner.supplies";
    assert(!pe_phone_fits(&needy, &x, &r, pe_holds) && !pe_phone_fits(&needy, &x, &r, NULL));
    s_warner_on = 1;
    assert(pe_phone_fits(&needy, &x, &r, pe_holds) && !pe_phone_fits(&needy, &x, &r, NULL));
    needy.needs = "warner.supplies nobody.offers";                             /* all must hold */
    assert(!pe_phone_fits(&needy, &x, &r, pe_holds));
    s_warner_on = 0;
}

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

    assert(pe_phone_pick(all, 5, &x, &r, pe_holds, 0) == &set_call);                           /* a set call first */
    pe_phone_record_played(&r, &set_call);
    assert(pe_phone_fits(&chained, &x, &r, pe_holds));                                          /* `after` heard */
    x.day = 4;
    assert(!pe_phone_fits(&serious_day2, &x, &r, pe_holds));
    x.day = 2;
    x.tone_mask = 1 << PE_CALL_FUNNY;
    for (roll = 0; roll < 4; ++roll)                                                  /* weights 3 + 1 */
        assert(pe_phone_pick(all, 5, &x, &r, pe_holds, roll) == (roll < 3 ? &funny_any : &funny_once));
    pe_phone_record_played(&r, &funny_once);
    assert(!pe_phone_fits(&funny_once, &x, &r, pe_holds));                                      /* once: heard */
    assert(pe_phone_pick(all, 5, &x, &r, pe_holds, 7) == &funny_any);
    pe_phone_record_played(&r, &funny_any);
    assert(pe_phone_pick(all, 5, &x, &r, pe_holds, 7) == &funny_any);                           /* the only one left */
    x.tone_mask = 1 << PE_CALL_SERIOUS;
    assert(pe_phone_pick(all, 5, &x, &r, pe_holds, 0) == &serious_day2 || pe_phone_pick(all, 5, &x, &r, pe_holds, 0) == &chained);
    pe_phone_record_played(&r, &serious_day2);
    assert(pe_phone_pick(all, 5, &x, &r, pe_holds, 0) == &chained);                             /* not the last five */
    x.tone_mask = 0;
    assert(pe_phone_pick(all, 5, &x, &r, pe_holds, 0) == NULL);
}

/* ---- call files ---- */

static PePhoneStore s_store;
static int s_problems, s_problem_line;
static char s_problem[256];

static void on_problem(void *user, const char *file, int line, const char *what) {
    (void)user;
    (void)file;
    if (!s_problems++) { s_problem_line = line; snprintf(s_problem, sizeof s_problem, "%s", what); }
    printf("  (expected) %s line %d: %s\n", file, line, what);
}

static int parse(const char *text) {
    s_problems = 0;
    s_problem[0] = 0;
    return pe_phone_parse_calls(&s_store, "test.txt", text, (unsigned)strlen(text), on_problem, NULL);
}

static void test_call_file(void) {
    const PePhoneCall *c;
    pe_phone_store_clear(&s_store);
    assert(parse("# a note\n\n[mine/a]\ncaller = Bob\ntone = funny\nday = 2-4\nrooms = 32, 87\n"
                 "priority = 5\nonce = yes\nweight = 3\nafter = pe1/pizza\nstory = 0x80-0xE8\n"
                 "them: Hi, {name}!\naya: Hello.\r\n") == 1 && s_problems == 0);
    c = &s_store.calls[0];
    assert(!strcmp(c->key, "mine/a") && !strcmp(c->caller, "Bob") && c->tone == PE_CALL_FUNNY);
    assert(c->day_from == 2 && c->day_to == 4 && c->room_count == 2 && c->rooms[0] == 32 && c->rooms[1] == 87);
    assert(c->priority == 5 && c->once && c->weight == 3 && !strcmp(c->after, "pe1/pizza"));
    assert(c->story_from == 0x80 && c->story_to == 0xE8 && c->line_count == 2);
    assert(c->lines[0].speaker == PE_CALL_CALLER && !strcmp(c->lines[0].text, "Hi, {name}!"));
    assert(c->lines[1].speaker == PE_CALL_AYA && !strcmp(c->lines[1].text, "Hello."));

    /* defaults: serious, any day, any room, weight 1 */
    assert(parse("[mine/b]\ncaller = X\nthem: Hi.\n") == 1);
    c = &s_store.calls[1];
    assert(c->tone == PE_CALL_SERIOUS && !c->day_from && !c->rooms && c->weight == 1 && !c->once);

    /* mistakes: reported with the line, the call left out, the next one still loads */
    assert(parse("[mine/bad]\ncaller = X\nthem: semi; colon\n\n[mine/c]\ncaller = Y\naya: Fine.\n") == 1);
    assert(s_problems == 2 && s_problem_line == 3 && strstr(s_problem, "semi; colon"));
    assert(!strcmp(s_store.calls[s_store.call_count - 1].key, "mine/c"));
    assert(parse("[mine/d]\ncaller = X\ntone = loud\nthem: Hi.\n") == 0 && s_problem_line == 3);
    assert(parse("[mine/e]\nthem: Hi.\n") == 0 && strstr(s_problem, "no caller"));
    assert(parse("[mine/f]\ncaller = X\n") == 0 && strstr(s_problem, "no lines"));
    assert(parse("[mine/a]\ncaller = X\nthem: Hi.\n") == 0 && strstr(s_problem, "already used"));
    assert(parse("[no slash]\ncaller = X\nthem: Hi.\n") == 0);
    assert(parse("them: Hi.\n") == 0 && strstr(s_problem, "outside any call"));
    assert(parse("[mine/g]\ncaller = X\nday = 9\nthem: Hi.\n") == 0);
    assert(parse("[mine/h]\ncaller = X\nshout: Hi.\n") == 0);
    assert(parse("[mine/i]\ncaller = X\ncolour = red\nthem: Hi.\n") == 1 && strstr(s_problem, "unknown setting"));   /* kept */
    assert(parse("[mine/n]\ncaller = X\nneeds = warner.supplies, other.thing\nthem: Hi.\n") == 1 && s_problems == 0);
    assert(!strcmp(s_store.calls[s_store.call_count - 1].needs, "warner.supplies  other.thing"));
    assert(parse("[mine/n2]\ncaller = X\nneeds = no;way\nthem: Hi.\n") == 0 && strstr(s_problem, "condition name"));
}

/* The player's copy of pe1.txt: kept up to date while unedited, never overwritten once edited. */
static void test_copy(void) {
    static const char v1[] = "[pe1/a]\ncaller = A\nthem: One.\n";
    static const char v2[] = "[pe1/a]\ncaller = A\nthem: One.\n\n[pe1/b]\ncaller = B\nthem: Two.\n";
    char file[512], stamp[256];
    int n;
    const unsigned s1 = (unsigned)strlen(v1), s2 = (unsigned)strlen(v2);
    /* written by the game from v1 */
    n = pe_phone_copy_stamp(stamp, (int)sizeof stamp, v1, s1);
    assert(n > 20 && stamp[n - 1] == '\n' && !strncmp(stamp, "# pe1 copy ", 11));
    snprintf(file, sizeof file, "%s%s", stamp, v1);
    assert(pe_phone_copy_state(file, (unsigned)strlen(file), v1, s1) == PE_COPY_CURRENT);
    assert(pe_phone_copy_state(file, (unsigned)strlen(file), v2, s2) == PE_COPY_UPDATE);        /* a newer game */
    /* the player changes a line */
    snprintf(file, sizeof file, "%s%s", stamp, "[pe1/a]\ncaller = A\nthem: Mine now.\n");
    assert(pe_phone_copy_state(file, (unsigned)strlen(file), v1, s1) == PE_COPY_EDITED);
    assert(pe_phone_copy_state(file, (unsigned)strlen(file), v2, s2) == PE_COPY_EDITED_NEWER);
    /* no stamp (a copy from before stamps): the same calls are adopted, anything else is the player's */
    assert(pe_phone_copy_state(v2, s2, v2, s2) == PE_COPY_UPDATE);
    assert(pe_phone_copy_state(v1, s1, v2, s2) == PE_COPY_EDITED_NEWER);
    assert(pe_phone_copy_state("", 0, v2, s2) == PE_COPY_EDITED_NEWER);
    /* the stamp is a comment: the copy reads as the same calls */
    snprintf(file, sizeof file, "%s%s", stamp, v1);
    pe_phone_store_clear(&s_store);
    assert(pe_phone_parse_calls(&s_store, "pe1.txt", file, (unsigned)strlen(file), on_problem, NULL) == 1);
}

/* ---- the game's own pack (calls/pe1.txt) ---- */

static const PePhoneCall *s_pack[PE_PHONE_STORE_CALLS];
static int s_pack_count;

static void load_pe1(void) {
    static char text[256 * 1024];
    FILE *f = fopen(PE_PHONE_CALLS_FILE, "rb");
    size_t n;
    int i;
    assert(f);
    n = fread(text, 1, sizeof text, f);
    fclose(f);
    pe_phone_store_clear(&s_store);
    s_problems = 0;
    assert(pe_phone_parse_calls(&s_store, "pe1.txt", text, (unsigned)n, on_problem, NULL) == s_store.call_count);
    assert(s_problems == 0);
    for (i = 0; i < s_store.call_count; ++i) s_pack[i] = &s_store.calls[i];
    s_pack_count = s_store.call_count;
}

static void test_pe1_pack(void) {
    static PePhoneScript s;
    PePhoneRecord r;
    PePhoneContext sewer = {32, 0x68, 1, 3}, park = {87, 0xF8, 2, 3}, carnegie = {1, 0x40, 1, 3};
    int i, k, funny = 0;
    load_pe1();
    assert(s_pack_count == 54);
    memset(&r, 0, sizeof r);
    assert(!strcmp(pe_phone_pick(s_pack, s_pack_count, &sewer, &r, pe_holds, 0)->key, "pe1/sewer_emergency"));
    for (i = 0; i < 64; ++i) {                                                        /* never elsewhere */
        assert(strncmp(pe_phone_pick(s_pack, s_pack_count, &park, &r, pe_holds, (uint32_t)i * 7919u)->key, "pe1/sewer", 9));
        assert(strncmp(pe_phone_pick(s_pack, s_pack_count, &carnegie, &r, pe_holds, (uint32_t)i * 7919u)->key, "pe1/sewer", 9));
    }
    pe_phone_record_played(&r, pe_phone_pick(s_pack, s_pack_count, &sewer, &r, pe_holds, 0));
    assert(strcmp(pe_phone_pick(s_pack, s_pack_count, &sewer, &r, pe_holds, 0)->key, "pe1/sewer_emergency"));   /* once */
    for (i = 0; i < s_pack_count; ++i) {
        const PePhoneCall *c = s_pack[i];
        assert(!strncmp(c->key, "pe1/", 4) && pe_phone_text_ok(c->caller));
        for (k = 0; k < i; ++k) assert(strcmp(c->key, s_pack[k]->key));
        for (k = 0; k < c->line_count; ++k) assert(pe_phone_text_ok(c->lines[k].text));
        assert(c->day_from >= 0 && c->day_to <= 6 && (!c->day_to || c->day_to >= c->day_from));
        pe_phone_script_build(&s, c, adv);
        assert(s.count >= c->line_count + 2 && s.click == s.count - 1);
        funny += c->tone == PE_CALL_FUNNY;
        if (c->priority >= 10) {                     /* set calls: one day, own phones, once */
            assert(c->day_from >= 1 && (!c->day_to || c->day_to == c->day_from));
            assert(c->room_count > 0 && c->once);
            if (c->after) {
                for (k = 0; k < s_pack_count && strcmp(s_pack[k]->key, c->after); ++k) {}
                assert(k < s_pack_count && s_pack[k]->priority > c->priority);   /* chain runs downhill */
            }
        }
    }
    assert(funny == 20);
}

/* A day's set calls play in order at whichever of its phones Aya uses, then the random pool. */
static void test_set_calls(void) {
    static const char *const park_order[] = {"pe1/park_wall", "pe1/park_zoo"};
    static const int park_rooms[] = {87, 59};
    PePhoneRecord r;
    PePhoneContext x = {0, 0xF8, 2, 3}, hall = {20, 0x40, 1, 3}, hall2 = {20, 0xE0, 2, 3};
    const PePhoneCall *c;
    int i;
    load_pe1();
    memset(&r, 0, sizeof r);
    for (i = 0; i < (int)(sizeof park_order / sizeof park_order[0]); ++i) {
        x.room = park_rooms[i];
        c = pe_phone_pick(s_pack, s_pack_count, &x, &r, pe_holds, 12345u);
        assert(c && !strcmp(c->key, park_order[i]));
        pe_phone_record_played(&r, c);
    }
    c = pe_phone_pick(s_pack, s_pack_count, &x, &r, pe_holds, 12345u);
    assert(c && strncmp(c->key, "pe1/park_", 9));                 /* the chain is done */
    /* Warner sent with supplies (pe_shops offers warner.supplies): Wayne calls first, then the wall update */
    memset(&r, 0, sizeof r);
    s_warner_on = 1;
    x.room = 64;
    c = pe_phone_pick(s_pack, s_pack_count, &x, &r, pe_holds, 12345u);
    assert(c && !strcmp(c->key, "pe1/park_wayne"));
    pe_phone_record_played(&r, c);
    assert(!strcmp(pe_phone_pick(s_pack, s_pack_count, &x, &r, pe_holds, 12345u)->key, "pe1/park_wall"));
    s_warner_on = 0;
    x.tone_mask = 2;                                                /* funny only: the zoo call's turn */
    memset(&r, 0, sizeof r);
    assert(strncmp(pe_phone_pick(s_pack, s_pack_count, &x, &r, pe_holds, 1u)->key, "pe1/park_", 9));   /* waits on the wall update */
    memset(&r, 0, sizeof r);
    assert(!strcmp(pe_phone_pick(s_pack, s_pack_count, &hall, &r, pe_holds, 7u)->key, "pe1/hall_report"));
    assert(strcmp(pe_phone_pick(s_pack, s_pack_count, &hall2, &r, pe_holds, 7u)->key, "pe1/hall_report"));   /* Day 1 only */
    /* a full once list forgets the oldest: still no repeat, the old day's call can't fit any more */
    memset(&r, 0, sizeof r);
    pe_phone_record_played(&r, pe_phone_pick(s_pack, s_pack_count, &hall, &r, pe_holds, 7u));
    for (i = 0; i < s_pack_count; ++i) if (s_pack[i]->once) pe_phone_record_played(&r, s_pack[i]);
    hall2.room = 20;
    for (i = 0; i < 64; ++i)
        assert(strcmp(pe_phone_pick(s_pack, s_pack_count, &hall2, &r, pe_holds, (uint32_t)i * 7919u)->key, "pe1/hall_report"));
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    test_text_ok();
    test_script();
    test_save_question();
    test_record();
    test_needs();
    test_copy();
    test_pick();
    test_call_file();
    test_pe1_pack();
    test_set_calls();
    puts("pe_phone: all checks passed");
    return 0;
}
