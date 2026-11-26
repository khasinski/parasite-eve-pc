/* Mods' per-save data kept by the host (mods/host/pe_save.h): saved and loaded for every mod the
 * same way; a mod that is off, or a mod not in this build, keeps its record; a newer layout survives
 * an older build; saves from before still load; New Game clears; a damaged tail resets; and the
 * crash copy holds what a save would, without calling any mod code. The game's own save path is
 * modelled as it is: a cleared 8 KB buffer, then PE_HOOK_SAVE_WRITE. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "pe_mod_host.h"
#include "pe_save.h"

static PeHookFn s_frame;
int pe_hook_add(int id, PeHookFn fn, void *user, int priority, const char *mod_id) {
    (void)user; (void)priority; (void)mod_id;
    assert(id == PE_HOOK_FRAME);
    s_frame = fn;
    return 0;
}
int pe_free_field(void) { return 1; }
static const char *s_current = "junk_mod";
static int s_junk_on = 1, s_keys_on = 1, s_pocket_on = 1;
const char *pe_mod_current(void) { return s_current; }
int pe_mod_active(const char *mod_id) {
    return !mod_id || (!strcmp(mod_id, "junk_mod") ? s_junk_on : !strcmp(mod_id, "keys_mod") ? s_keys_on :
                       !strcmp(mod_id, "pocket_mod") ? s_pocket_on : 1);
}
void pe_mod_log(const char *mod_id, const char *fmt, ...) { (void)mod_id; (void)fmt; }
void pe_toast(const char *text) { (void)text; }

/* Two stand-in mods. "junk" counts its callbacks, so the crash copy can be shown to call none. */
static struct { unsigned junk, pity; } s_junk;
static unsigned short s_keys[4];
static int s_calls;
static int junk_check(void *d) { (void)d; ++s_calls; return s_junk.junk > 99999; }
static void junk_reset(void *d) { (void)d; ++s_calls; s_junk.junk = 0; s_junk.pity = 7; }
static int junk_upgrade(void *d, const unsigned char *old, unsigned size, unsigned version) {
    (void)d; ++s_calls;
    if (version != 0 || size < 12 || memcmp(old, "PELT", 4)) return 1;
    memcpy(&s_junk.junk, old + 8, 4);
    s_junk.pity = 0;
    return 0;
}
static const PeSaveRecord s_junk_rec = { "pe_economy.loot", 2, &s_junk, sizeof s_junk, junk_check, junk_upgrade, junk_reset };
/* Named like pe_qol's pocket, so its old block lives in the window (pe_save.h). */
static int keys_upgrade(void *d, const unsigned char *old, unsigned size, unsigned version) {
    (void)d;
    if (version != 0 || size < 16 + sizeof s_keys || memcmp(old, "PKEY", 4)) return 1;
    memcpy(s_keys, old + 16, sizeof s_keys);
    return 0;
}
static const PeSaveRecord s_keys_rec = { "pe_qol.keys", 1, s_keys, sizeof s_keys, NULL, keys_upgrade, NULL };

/* A third mod whose record holds things the game needs back when the mod is off (pe_qol's keys):
 * release() "returns" all but the last to the game. */
static unsigned short s_pocket[3], s_returned;
static int s_releases;
static void pocket_release(void *d) {
    (void)d; ++s_releases;
    s_returned = s_pocket[0];
    s_pocket[0] = s_pocket[1]; s_pocket[1] = 0;
}
static const PeSaveRecord s_pocket_rec = { "test.pocket", 1, s_pocket, sizeof s_pocket, NULL, NULL, NULL, pocket_release };

static unsigned char s_card[0x2000];
static void game_save(void) { memset(s_card, 0, sizeof s_card); pe_save_on_write(s_card); }   /* Save_BuildCardFile */
/* The load, then the room's first free field frame (where release() runs). */
static void game_load(const unsigned char *card) { pe_save_on_read(card); s_frame(NULL, NULL); }

static unsigned fnv(const char *name) {
    unsigned h = 0x811C9DC5u;
    for (; *name; ++name) h = (h ^ (unsigned char)*name) * 0x01000193u;
    return h;
}
/* Byte-level writer for tails the build under test didn't make (other builds, damage). */
static unsigned crc32(const unsigned char *p, unsigned n) {
    unsigned c = 0xFFFFFFFFu, i, b;
    for (i = 0; i < n; ++i) for (c ^= p[i], b = 0; b < 8; ++b) c = c & 1 ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    return ~c;
}
static unsigned s_at;
static void tail_begin(unsigned char *card) { memset(card, 0, 0x2000); memcpy(card + 0x1C90, "PEMC\1\0", 6); s_at = 20; }
static void tail_add(unsigned char *card, unsigned id, unsigned version, const void *data, unsigned size) {
    unsigned char *t = card + 0x1C90 + s_at;
    memcpy(t, &id, 4); t[4] = (unsigned char)version; t[5] = (unsigned char)(version >> 8);
    t[6] = (unsigned char)size; t[7] = (unsigned char)(size >> 8); memcpy(t + 12, data, size);
    {   /* the record's own CRC: its first 8 bytes, then its data */
        unsigned char buf[8 + 0x400]; unsigned c;
        memcpy(buf, t, 8); memcpy(buf + 8, data, size); c = crc32(buf, 8 + size); memcpy(t + 8, &c, 4);
    }
    s_at += 12 + size;
}
static void tail_end(unsigned char *card) {
    unsigned char *t = card + 0x1C90;
    unsigned c;
    t[6] = (unsigned char)s_at; t[7] = (unsigned char)(s_at >> 8); memset(t + 8, 0, 4);
    c = crc32(t + 0x200, 0x88); memcpy(t + 12, &c, 4);                 /* the window as written */
    c = crc32(t, s_at); memcpy(t + 8, &c, 4);
}
/* A record in a saved tail, NULL if absent. */
static const unsigned char *find(const unsigned char *card, const char *name, unsigned *version, unsigned *size) {
    const unsigned char *t = card + 0x1C90;
    unsigned at, total = t[6] | t[7] << 8, id;
    if (memcmp(t, "PEMC", 4)) return NULL;
    for (at = 20; at < total; at += 12 + (t[at + 6] | t[at + 7] << 8)) {   /* records short of the window */
        memcpy(&id, t + at, 4);
        if (id == fnv(name)) {
            if (version) *version = t[at + 4] | t[at + 5] << 8;
            if (size) *size = t[at + 6] | t[at + 7] << 8;
            return t + at + 12;
        }
    }
    return NULL;
}

int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--opaque")) {
        unsigned char input[0x2000]={0},output[0x2000]={0},snapshot[0x2000]={0};
        s_junk_on=s_keys_on=s_pocket_on=0;s_current="pocket_mod";
        assert(pe_save_register(&s_pocket_rec)==0);pe_save_install();
        for(unsigned i=PE_SAVE_TAIL_OFFSET;i<sizeof input;++i)input[i]=(unsigned char)(i*13+7);
        pe_save_on_read(input);s_frame(NULL,NULL);
        assert(s_releases==0);pe_save_on_write(output);pe_save_crash_tail(snapshot);
        assert(!memcmp(input+PE_SAVE_TAIL_OFFSET,output+PE_SAVE_TAIL_OFFSET,PE_SAVE_TAIL_SIZE));
        assert(!memcmp(input+PE_SAVE_TAIL_OFFSET,snapshot+PE_SAVE_TAIL_OFFSET,PE_SAVE_TAIL_SIZE));
        puts("Mods off: all 880 opaque tail bytes survive saving and room snapshots");return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--overflow")) {
        static unsigned char byte;
        static const PeSaveRecord bad={"test.overflow",1,&byte,UINT_MAX,NULL,NULL,NULL};
        assert(pe_save_register(&bad)==-1);return 0;
    }
    unsigned char other[0x2000], crash[0x2000];
    unsigned v, n;
    const unsigned char *p;
    char text[512];
    size_t used = 0;

    pe_save_install();
    assert(pe_save_register(&s_junk_rec) == 0);
    s_current = "keys_mod";
    assert(pe_save_register(&s_keys_rec) == 0);
    assert(pe_save_register(&s_keys_rec) == -1);                      /* same name twice */
    s_current = "pocket_mod";
    assert(pe_save_register(&s_pocket_rec) == 0);
    s_current = "keys_mod";
    {
        static unsigned char big[0x370];
        static const PeSaveRecord too_big = { "test.big", 1, big, sizeof big, NULL, NULL, NULL };
        assert(pe_save_register(&too_big) == -1);                      /* no room: refused at start-up */
    }

    /* Save and load: every mod's state, the same way. */
    s_junk.junk = 120; s_junk.pity = 3; s_keys[0] = 0xC8;
    game_save();
    assert(!memcmp(s_card + 0x1C90, "PEMC", 4) && s_card[0x1C8F] == 0);
    assert(s_card[0x1C90 + 16] == PE_MOD_API_VERSION && s_card[0x1C90 + 18] == 1);   /* the writer marker */
    assert((p = find(s_card, "pe_economy.loot", &v, &n)) && v == 2 && n == sizeof s_junk);
    assert((p = find(s_card, "pe_qol.keys", &v, &n)) && v == 1 && p[0] == 0xC8);
    memset(&s_junk, 0, sizeof s_junk); memset(s_keys, 0, sizeof s_keys);
    game_load(s_card);
    assert(s_junk.junk == 120 && s_junk.pity == 3 && s_keys[0] == 0xC8);

    /* Saving twice gives the same bytes. */
    memcpy(other, s_card, sizeof other);
    game_save();
    assert(!memcmp(other, s_card, sizeof other));

    /* A mod that's off keeps its record through saves; turned back on it reads it. */
    s_keys_on = 0;
    s_keys[0] = 0;                                                     /* not running: its memory doesn't count */
    s_junk.junk = 130;
    game_save();
    assert((p = find(s_card, "pe_qol.keys", NULL, NULL)) && p[0] == 0xC8);
    assert((p = find(s_card, "pe_economy.loot", NULL, NULL)) && p[0] == 130);
    game_load(s_card);
    s_keys_on = 1;
    game_load(s_card);
    assert(s_keys[0] == 0xC8);

    /* A record nobody here knows (a mod not in this build) and a newer layout both survive. */
    {
        unsigned char extra[3] = {1, 2, 3}, newer[12] = {9, 9, 9};
        tail_begin(other);
        tail_add(other, fnv("someone.else"), 4, extra, sizeof extra);
        tail_add(other, fnv("pe_economy.loot"), 3, newer, sizeof newer);
        tail_end(other);
        s_junk.junk = 55;
        game_load(other);
        assert(s_junk.junk == 0 && s_junk.pity == 7);                  /* can't read v3: defaults */
        s_junk.junk = 77;                                              /* played on */
        game_save();
        assert((p = find(s_card, "someone.else", &v, &n)) && v == 4 && n == 3 && p[2] == 3);
        assert((p = find(s_card, "pe_economy.loot", &v, &n)) && v == 3 && n == 12 && p[0] == 9);   /* not overwritten */
    }

    /* A container format newer than this build: kept as it is through saves, mods from defaults;
     * New Game or a normal save loaded afterwards clears it. */
    {
        tail_begin(other);
        other[0x1C90 + 4] = 2;
        tail_add(other, fnv("pe_economy.loot"), 9, "future", 6);
        tail_end(other);
        s_junk.junk = 55;
        game_load(other);
        assert(s_junk.junk == 0 && s_junk.pity == 7);
        s_junk.junk = 66;
        game_save();
        assert(!memcmp(s_card + 0x1C90, other + 0x1C90, 0x370));
        game_save();
        assert(!memcmp(s_card + 0x1C90, other + 0x1C90, 0x370));
        memset(crash, 0, sizeof crash);
        pe_save_crash_tail(crash);
        assert(!memcmp(crash + 0x1C90, other + 0x1C90, 0x370));
        used = 0;
        pe_save_crash_describe(text, sizeof text, &used);
        assert(strstr(text, "format 2 from a newer build"));
        used = 0;
        memset(other, 0, sizeof other);
        game_load(other);                                              /* a vanilla save */
        s_junk.junk = 12;
        game_save();
        assert((p = find(s_card, "pe_economy.loot", &v, NULL)) && v == 2 && p[0] == 12);
    }

    /* A save from before: the old fixed blocks go to their owners' upgrade(). */
    memset(other, 0, sizeof other);
    memcpy(other + 0x1C90, "PELT", 4);
    other[0x1C90 + 8] = 42;
    game_load(other);
    assert(s_junk.junk == 42 && s_junk.pity == 0);
    game_save();
    assert((p = find(s_card, "pe_economy.loot", &v, NULL)) && v == 2 && p[0] == 42);   /* now in the new layout */

    /* A record that fails its owner's checks resets. Damage to one record (another build wrote over
     * it) resets only that record; damage to the container's header resets everything. */
    {
        struct { unsigned junk, pity; } bad = {100000, 1};
        tail_begin(other);
        tail_add(other, fnv("pe_economy.loot"), 2, &bad, sizeof bad);
        tail_end(other);
        game_load(other);
        assert(s_junk.junk == 0 && s_junk.pity == 7);
        s_junk.junk = 5; s_keys[0] = 0xC8;
        game_save();
        memcpy(other, s_card, sizeof other);
        p = find(other, "pe_economy.loot", NULL, NULL);
        other[p - other] ^= 1;
        s_junk.junk = 6; s_keys[0] = 0;
        game_load(other);
        assert(s_junk.junk == 0 && s_junk.pity == 7 && s_keys[0] == 0xC8);
        game_save();                                                   /* the lost record is gone, the rest kept */
        assert(!find(s_card, "pe_economy.loot", NULL, NULL) || find(s_card, "pe_economy.loot", NULL, NULL)[0] == 0);
        assert((p = find(s_card, "pe_qol.keys", NULL, NULL)) && p[0] == 0xC8);
        memcpy(other, s_card, sizeof other);
        other[0x1C90 + 6] = 0xFF; other[0x1C90 + 7] = 0xFF;          /* total: past the room */
        s_junk.junk = 5;
        game_load(other);
        assert(s_junk.junk == 0 && s_keys[0] == 0);
    }

    /* Admission (the card load asks pe_save_peek for the companion link, then pe_save_prepare_load):
     * a partly overwritten container is admitted and recovered, not refused. A record still there
     * peeks fine; one absent from it is unknown (it may have been lost), not "none". A damaged or
     * newer header is refused. */
    {
        unsigned char link[8], got[16];
        unsigned gv = 0, gn = 0;
        memset(link, 0x5A, sizeof link);
        tail_begin(other);
        tail_add(other, fnv("pe_qol.keys"), 1, "\xC8\0\0\0\0\0\0\0", 8);
        tail_add(other, fnv("pe_economy.loot"), 2, "\x2A\0\0\0\0\0\0\0", 8);
        tail_add(other, fnv("pe_host.extlink"), 1, link, sizeof link);
        tail_end(other);
        assert(pe_save_peek(other, "pe_host.extlink", &gv, got, sizeof got, &gn) == 1 && gv == 1 && gn == 8);
        assert(pe_save_peek(other, "nobody.here", &gv, got, sizeof got, &gn) == 0);
        assert(pe_save_prepare_load(other) == 0);
        p = find(other, "pe_economy.loot", NULL, NULL);
        other[p - other] ^= 1;                                         /* another build wrote over it */
        assert(pe_save_peek(other, "pe_host.extlink", &gv, got, sizeof got, &gn) == 1 && got[0] == 0x5A);
        assert(pe_save_peek(other, "pe_economy.loot", &gv, got, sizeof got, &gn) == -1);   /* lost */
        assert(pe_save_peek(other, "nobody.here", &gv, got, sizeof got, &gn) == -1);       /* unknown */
        assert(pe_save_prepare_load(other) == 0);                      /* admitted, not "unreadable" */
        s_junk.junk = 6; s_keys[0] = 0;
        game_load(other);
        assert(s_junk.junk == 0 && s_junk.pity == 7 && s_keys[0] == 0xC8);   /* lost reset, kept loaded */
        memcpy(crash, other, sizeof crash);
        crash[0x1C90 + 4] = 2;                                         /* a newer container format */
        assert(pe_save_peek(crash, "pe_host.extlink", &gv, got, sizeof got, &gn) == -1);
        assert(pe_save_prepare_load(crash) == -1);
        crash[0x1C90 + 4] = 1;
        crash[0x1C90 + 6] = 0xFF; crash[0x1C90 + 7] = 0xFF;            /* a damaged header */
        assert(pe_save_peek(crash, "pe_host.extlink", &gv, got, sizeof got, &gn) == -1);
        assert(pe_save_prepare_load(crash) == -1);
        s_keys[0] = 0;
    }

    /* New Game: everything resets and nothing carries over. */
    s_junk.junk = 300; s_keys[0] = 0xC9;
    game_save();
    game_load(s_card);
    s_keys_on = 0;
    pe_save_new_game();                                          /* as the host on PE_HOOK_NEW_GAME */
    assert(s_junk.junk == 0);
    game_save();
    assert(!find(s_card, "pe_qol.keys", NULL, NULL));
    s_keys_on = 1;

    /* The crash copy: what a save made now holds, with no mod code called and nothing changed. */
    s_junk.junk = 900; s_keys[0] = 0xCA;
    s_calls = 0;
    memset(crash, 0, sizeof crash);
    pe_save_crash_tail(crash);
    assert(s_calls == 0);
    game_save();
    assert(!memcmp(crash + 0x1C90, s_card + 0x1C90, 0x370));
    pe_save_crash_describe(text, sizeof text, &used);
    assert(strstr(text, "3 record(s)") && strstr(text, "writer mod API 1 / save writer 1") &&
           strstr(text, "pe_economy.loot (junk_mod) v2, 8 bytes, live"));

    /* An off mod with release(): its record loads, release() gives back what it can, and the save
     * holds what is left (from memory), not the carried record. Running, release() never runs. */
    {
        s_pocket[0] = 0xC8; s_pocket[1] = 0xC9;
        game_save();
        game_load(s_card);
        assert(s_releases == 0);
        s_pocket_on = 0;
        memset(s_pocket, 0, sizeof s_pocket);
        pe_save_on_read(s_card);                                       /* loaded, no free field yet */
        assert(s_releases == 0 && s_pocket[0] == 0xC8 && s_pocket[1] == 0xC9);
        s_frame(NULL, NULL);
        assert(s_releases == 1 && s_returned == 0xC8 && s_pocket[0] == 0xC9);
        game_save();
        assert((p = find(s_card, "test.pocket", NULL, NULL)) && p[0] == 0xC9 && p[2] == 0);
        game_load(s_card);                                             /* released again: the rest */
        assert(s_releases == 2 && s_returned == 0xC9 && s_pocket[0] == 0);
        s_pocket_on = 1;
        game_load(s_card);
        assert(s_releases == 2 && s_pocket[0] == 0xC9);                /* on again: the record as saved */
        memset(s_pocket, 0, sizeof s_pocket);
    }

    /* The window (0x200..0x287): the old PKEY/SORT blocks. A build without the records (an
     * official release) rewrites them; the container doesn't break, and the newer pocket wins. */
    {
        unsigned char *w;
        s_junk.junk = 410; s_keys[0] = 0xC8;
        game_save();
        assert(s_card[0x1C90 + 0x200] == 0);                           /* no owner wrote a block: none */
        memcpy(other, s_card, sizeof other);
        w = other + 0x1C90 + 0x200;                                    /* the official build saves */
        memcpy(w, "PKEY", 4); w[16] = 0xD1;
        memcpy(other + 0x1C90 + 0x260, "SORT", 4);
        memset(&s_junk, 0, sizeof s_junk); memset(s_keys, 0, sizeof s_keys);
        game_load(other);
        assert(s_junk.junk == 410 && s_keys[0] == 0xD1);
        /* Saved here again: the pocket's record follows, and the window as this save holds it. */
        game_save();
        assert((p = find(s_card, "pe_qol.keys", NULL, NULL)) && p[0] == 0xD1);
        /* The owner off: its block in the window carries forward, still newer than the record. */
        memcpy(s_card + 0x1C90 + 0x200, w, 0x54);
        s_keys_on = 0;
        game_load(s_card);
        game_save();
        assert(!memcmp(s_card + 0x1C90 + 0x200, w, 0x54));
        assert(!memcmp(s_card + 0x1C90 + 0x260, "SORT", 4));
        s_keys_on = 1;
    }

    /* A container longer than the window runs around it, and a block written into the window
     * afterwards doesn't damage it. */
    {
        static unsigned char big[0x220];
        static const PeSaveRecord big_rec = { "test.big2", 1, big, sizeof big, NULL, NULL, NULL };
        unsigned i;
        for (i = 0; i < sizeof big; ++i) big[i] = (unsigned char)(i * 7 + 1);
        assert(pe_save_register(&big_rec) == 0);
        unsigned char before[0x88];
        memcpy(before, s_card + 0x1C90 + 0x200, sizeof before);
        s_junk.junk = 515;
        game_save();
        /* The container skipped it: the pocket's block as its running owner wrote it (nothing, here),
         * the sort block carried. */
        for (i = 0x200; i < 0x260; ++i) assert(s_card[0x1C90 + i] == 0);
        assert(!memcmp(s_card + 0x1C90 + 0x260, before + 0x60, 0x28));
        assert(s_card[0x1C90 + 0x288] != 0);                               /* and went on after it */
        memcpy(other, s_card, sizeof other);
        memcpy(other + 0x1C90 + 0x200, "PKEY", 4);
        memset(big, 0, sizeof big); s_junk.junk = 0;
        game_load(other);
        assert(s_junk.junk == 515);
        for (i = 0; i < sizeof big; ++i) assert(big[i] == (unsigned char)(i * 7 + 1));
    }

    puts("Save record checks passed: one way for every mod, mods off and unknown mods keep theirs, newer "
         "layouts survive, old saves load, damage resets, New Game clears, crash copy runs no mod code, "
         "the old blocks' window stays readable by older builds.");
    return 0;
}
