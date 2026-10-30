/* Language packs: the game's text from a .pelang pack (docs/LOCALIZATION.md).
 *
 * Packs are read from lang/ beside the game and lang/ in the user folder (beside pe_mods.ini);
 * F1 > Language picks one, or the game's own text. Anything a pack doesn't translate keeps the
 * game's text, so a half-finished pack is still playable.
 *
 * The resident text tables (system messages, item names and descriptions, the rest) come through
 * PE_HOOK_TEXT_LOOKUP. Room dialogue comes through PE_HOOK_TEXT_PAGE: the pack names each English
 * message bank by its CRC32 ("bank/<crc32>"), so the bank a box reads from is measured once per
 * room and its pages looked up as dlg/<bank>/<page>. A bank the pack doesn't know (another disc
 * version, a modded room) keeps the game's text.
 *
 * Packs may use Latin Extended and Russian/Ukrainian/Belarusian Cyrillic. Cyrillic art is
 * installed only for packs that draw its new cells; Latin codes remain stable. Accented letters
 * (é, ñ, ß, « ...): while a pack is in use they are
 * composed from the game's own letters into the dialogue font's empty cells, codes 0x4D on (as
 * the font goes to VRAM, PE_HOOK_TIM_UPLOAD, or at once when F1 picks a pack); the dialogue box
 * gets their widths through g_TextboxGlyphWidths, and the menus through the resident glyph
 * metrics. The game's text never uses those codes, but its rename grids (sys/115-117) draw
 * them, as blanks: with Game text the page, widths and metrics are put back as the game had them. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "pe1/psyq_gpu.h"
#include "pe1/psyq_tim.h"
#include "pe1/resident_tables.h"
#include "pe1/textbox.h"
#include "pe_hooks.h"
#include "pe_lang_font.h"
#include "pe_lang_cyrillic.h"
#include "pe_lang_cjk.h"
#include "pe1/mod_text.h"
#include "pe_lang_pack.h"
#include "pe_lang_selection.h"
#include "pe_lang_text.h"
#include "pe_translate.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"

#define MOD_ID "pe_lang"
#define MAX_PACKS 16

static PeLangPack s_packs[MAX_PACKS];
static unsigned s_pack_count;
static char s_labels[MAX_PACKS][64];
static const char *s_names[MAX_PACKS + 1];
static int s_choice;                    /* 0: the game's own text, else s_packs[s_choice - 1] */

static const PeLangPack *active_pack(void) {
    return s_choice > 0 && (unsigned)s_choice <= s_pack_count ? &s_packs[s_choice - 1] : NULL;
}

static int on_text_lookup(void *ctx, void *user) {
    PeHookTextLookup *lookup = ctx;
    const unsigned char *text = pe_lang_pack_table(active_pack(), lookup->table, lookup->index);
    (void)user;
    if (text && lookup->persistent) {
        /* IDs in the CJK dictionary belong to this pack, not to the save format.
         * Rename defaults must remain readable after switching/removing a pack. */
        for (const unsigned char *p = text; *p != 0xFF; ++p)
            if (Pe_ExtendedGlyph(p, NULL)) return 0;
    }
    if (text) *lookup->text = (void *)text;
    return 0;
}

/* The bank the last box read from and its CRC32, measured once per room. */
#define MAX_BANK_BYTES 0x20000u
static const unsigned char *s_bank;
static uint32_t s_bank_crc;
static int s_bank_known;

static int on_text_page(void *ctx, void *user) {
    PeHookTextPage *box = ctx;
    const unsigned char *text;
    (void)user;
    if (!active_pack() || !box->bank) return 0;
    if (box->bank != s_bank) {
        size_t size = pe_lang_bank_size(box->bank, MAX_BANK_BYTES);
        s_bank = box->bank;
        s_bank_known = size != 0;
        s_bank_crc = s_bank_known ? pe_lang_crc32(box->bank, size) : 0;
    }
    if (s_bank_known && (text = pe_lang_pack_page(active_pack(), s_bank_crc, box->page)))
        *box->message = (unsigned char *)text;
    return 0;
}

/* ---- Latin Extended and opt-in Cyrillic glyphs ---- */

#define FONT_CODES 0xF1u                 /* single-byte glyph codes the box can draw: 0x00-0xF0 */
#define FONT_BYTES (128u * 256u)         /* the page's 4-bit pixels: 64 words x 256 rows */
static TextboxGlyphSpacing s_widths[FONT_CODES];
static int s_font_cyrillic;              /* installed only when the selected pack uses new Cyrillic cells */
static int s_font_added;                 /* the glyphs are in VRAM and their widths installed */
/* The page as the game uploaded it. The glyphs go only into a copy, and only while a pack is in
 * use: the empty cells they take are drawn by the game too (the rename grids), so with Game text
 * the font, widths and menu metrics must stay exactly the game's. */
static unsigned char s_font_original[FONT_BYTES], s_font_work[FONT_BYTES];
static unsigned char s_metrics_original[FONT_CODES];
static int s_font_saved, s_metrics_saved;
static PeCjkCache s_cjk;
static const PeLangPack *s_font_pack;
static int s_cjk_warned;
static const RECT kFontRect = {0x140, 0, 64, 256};

/* The US dialogue font page: 4-bit, its image at VRAM 0x140,0 (64 x 256 words), its palette at
 * 0x140,0xFC (src/main/text/tables.c, g_FieldFontTextureState). */
static unsigned char *font_pixels(TimFile *tim) {
    if ((tim->flags & 7) != 0 || !(tim->flags & 8)) return NULL;
    TimBlock *clut = &tim->first_block;
    TimBlock *image = (TimBlock *)&clut->bytes[clut->header.length];
    const RECT *c = &clut->header.rect, *r = &image->header.rect;
    if (c->x != 0x140 || c->y != 0xFC || r->x != 0x140 || r->y != 0 || r->w != 64 || r->h != 256) return NULL;
    return (unsigned char *)image->header.pixels;
}

/* The widths of the new codes, for the dialogue box and for the menus. */
static void install_widths(void) {
    unsigned char advance[78], *metrics;
    for (unsigned i = 0; i < 78; ++i) advance[i] = g_TextboxGlyphSpacing[i].right;
    memset(s_widths, 0, sizeof s_widths);
    memcpy(s_widths, g_TextboxGlyphSpacing, sizeof g_TextboxGlyphSpacing);
    for (unsigned k = 0; k < PE_LATIN_COUNT; ++k)
        s_widths[kPeLatinGlyphs[k].code].right = (u8)pe_lang_font_advance(&kPeLatinGlyphs[k], advance);
    if (s_font_cyrillic)
        for (unsigned k = 0; k < PE_CYRILLIC_COUNT; ++k)
            s_widths[kPeCyrillicGlyphs[k].code].right = kPeCyrillicGlyphs[k].advance;
    g_TextboxGlyphWidths = s_widths;
    g_TextboxGlyphCount = FONT_CODES;
    if (!g_ResidentDataPack.directory.glyphMetrics) return;     /* not loaded yet: next scene */
    metrics = &g_ResidentDataPack.bytes[g_ResidentDataPack.directory.glyphMetrics];
    for (unsigned k = 0; k < PE_LATIN_COUNT; ++k) {
        if (!s_metrics_saved) s_metrics_original[kPeLatinGlyphs[k].code] = metrics[kPeLatinGlyphs[k].code];
        metrics[kPeLatinGlyphs[k].code] = pe_lang_font_metric(&kPeLatinGlyphs[k], metrics);
    }
    if (s_font_cyrillic)
        for (unsigned k = 0; k < PE_CYRILLIC_COUNT; ++k) {
            unsigned code = kPeCyrillicGlyphs[k].code;
            if (!s_metrics_saved) s_metrics_original[code] = metrics[code];
            metrics[code] = (unsigned char)(kPeCyrillicGlyphs[k].advance << 4);
        }
    s_metrics_saved = 1;
}

/* The game's own widths and metrics back. */
static void remove_widths(void) {
    g_TextboxGlyphWidths = g_TextboxGlyphSpacing;
    g_TextboxGlyphCount = sizeof g_TextboxGlyphSpacing / sizeof *g_TextboxGlyphSpacing;
    if (s_metrics_saved && g_ResidentDataPack.directory.glyphMetrics) {
        unsigned char *metrics = &g_ResidentDataPack.bytes[g_ResidentDataPack.directory.glyphMetrics];
        for (unsigned k = 0; k < PE_LATIN_COUNT; ++k) metrics[kPeLatinGlyphs[k].code] = s_metrics_original[kPeLatinGlyphs[k].code];
        if (s_font_cyrillic)
            for (unsigned k = 0; k < PE_CYRILLIC_COUNT; ++k) {
                unsigned code = kPeCyrillicGlyphs[k].code;
                metrics[code] = s_metrics_original[code];
            }
    }
    s_metrics_saved = 0;
}

/* Put the glyphs into `pixels` (the page on its way to VRAM, or a copy) when a pack is in use. */
static void add_glyphs(unsigned char *pixels) {
    int added = pe_lang_font_fill(pixels, 128, 256);
    s_font_cyrillic = pe_lang_pack_uses_glyphs(active_pack(), PE_CYRILLIC_FIRST_CODE, PE_CYRILLIC_LAST_CODE);
    int extra = s_font_cyrillic ? pe_lang_cyrillic_fill(pixels, 128, 256) : 0;
    if (!s_font_added && added + extra)
        pe_mod_log(MOD_ID, "added %d Latin Extended and %d Cyrillic glyphs to the dialogue font", added, extra);
    added += extra;
    if (added) {
        s_font_added = 1;
        install_widths();
    }
}

static int on_tim_upload(void *ctx, void *user) {
    unsigned char *pixels = font_pixels(ctx);
    (void)user;
    if (!pixels) return 0;
    memcpy(s_font_original, pixels, FONT_BYTES);
    s_font_saved = 1;
    s_font_added = 0;
    if (active_pack()) add_glyphs(pixels);
    s_font_pack = active_pack();
    pe_cjk_reset(&s_cjk, s_font_pack, s_font_original);
    s_cjk_warned = 0;
    return 0;
}

/* After the choice changes in F1: the page in VRAM and the widths follow it now. */
static void font_follow_choice(void) {
    if (!s_font_saved) return;                       /* not uploaded yet: on_tim_upload decides */
    int want_cyrillic = pe_lang_pack_uses_glyphs(active_pack(), PE_CYRILLIC_FIRST_CODE, PE_CYRILLIC_LAST_CODE);
    if (active_pack() && (!s_font_added || want_cyrillic != s_font_cyrillic || s_font_pack != active_pack())) {
        if (s_font_added) remove_widths();
        s_font_added = 0;
        memcpy(s_font_work, s_font_original, FONT_BYTES);
        add_glyphs(s_font_work);
        DrawSync(0);
        LoadImage((RECT *)&kFontRect, (u_long *)s_font_work);
    } else if (!active_pack() && s_font_added) {
        DrawSync(0);
        LoadImage((RECT *)&kFontRect, (u_long *)s_font_original);
        remove_widths();
        s_font_added = 0;
        s_font_cyrillic = 0;
    }
    if (s_font_pack != active_pack()) {
        s_font_pack = active_pack();
        pe_cjk_reset(&s_cjk, s_font_pack, s_font_original);
        s_cjk_warned = 0;
    }
}

/* A new room can load its bank where the last one was: measure again. The resident pack can be
 * read again too, so the menu widths go back in. */
static int on_scene_start(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    s_bank = NULL;
    if (s_font_added) install_widths();
    return 0;
}

static void cjk_sync(void *user) { (void)user; DrawSync(0); }
static int cjk_upload(unsigned cell, const unsigned char *pixels, void *user) {
    (void)user;
    RECT r = {(short)(0x140 + cell%21*3), (short)(cell/21*12), 3, 12};
    /* LoadImage's source is word-aligned even when pack text has odd byte length. */
    unsigned int aligned[18];
    memcpy(aligned, pixels, sizeof aligned);
    return LoadImage(&r, aligned);
}
static int on_text_glyph(void *ctx, void *user) {
    PeHookTextGlyph *g = ctx;
    unsigned id, cell = 0x2C; /* visible '?' if the frame exhausts its cache */
    const PeLangPack *pack = active_pack();
    (void)user;
    if (!pack || !Pe_ExtendedGlyph(g->text, &id) || id >= pack->cjk_count) return 0;
    g->bytes = 4; g->advance = 12;
    if (g->draw && (!s_font_saved || !pe_cjk_resolve(&s_cjk,id,&cell,cjk_sync,cjk_upload,NULL))) {
        if (!s_cjk_warned) {
            pe_mod_log(MOD_ID,"CJK glyph cache unavailable/full (%u eligible cells); showing ?",s_cjk.count);
            s_cjk_warned = 1;
        }
    }
    g->u = (unsigned char)(cell%21*12); g->v = (unsigned char)(cell/21*12);
    return 1;
}
static int on_cjk_frame(void *ctx, void *user) {
    (void)ctx; (void)user;
    pe_cjk_frame(&s_cjk);
    return 0;
}

static int by_name(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Load the packs in `dir` (ending in a separator), in file-name order. */
static void load_dir(const char *dir) {
    int count = 0;
    char **files = SDL_GlobDirectory(dir, "*.pelang", 0, &count);
    if (!files) return;
    qsort(files, (size_t)count, sizeof *files, by_name);
    for (int i = 0; i < count; ++i) {
        char path[4096];
        PeLangPack loaded, *pack = &loaded;
        snprintf(path, sizeof path, "%s%s", dir, files[i]);
        if (pe_lang_pack_load(pack, path)) {
            pe_mod_log(MOD_ID, "skipped %s: invalid/unsafe pack or unsupported format (supported: v1/v2)", path);
            continue;
        }
        PeLangSelection id;
        memcpy(id.lang, pack->lang, sizeof id.lang);
        memcpy(id.name, pack->name, sizeof id.name);
        int existing = pe_lang_selection_find(&id, s_packs, s_pack_count);
        unsigned slot = existing ? (unsigned)existing - 1 : s_pack_count;
        if (slot == MAX_PACKS) {
            pe_mod_log(MOD_ID, "skipped %s: pack limit reached", path);
            pe_lang_pack_free(pack);
            continue;
        }
        if (existing) pe_lang_pack_free(&s_packs[slot]);
        s_packs[slot] = loaded; /* User-folder copies override the same identity beside the game. */
        snprintf(s_labels[slot], sizeof s_labels[0], "%s (%s)", pack->name, pack->lang);
        pe_mod_log(MOD_ID, "%s: %s, %u entries", path, s_labels[slot], pack->count);
        if (!existing) ++s_pack_count;
    }
    SDL_free(files);
}

static void active_text(char *out, unsigned size) {
    const PeLangPack *pack = active_pack();
    if (pack) snprintf(out, size, "%s, %u entries", pack->name, pack->count);
    else snprintf(out, size, s_pack_count ? "the game's own text" : "no packs found");
}

static int language_get(void) { return s_choice; }

static int language_set(int choice) {
    char path[4096];
    PeLangSelection id = {0};
    if (choice < 0 || (unsigned)choice > s_pack_count) return 0;
    if (choice) {
        memcpy(id.lang, s_packs[choice - 1].lang, sizeof id.lang);
        memcpy(id.name, s_packs[choice - 1].name, sizeof id.name);
    }
    s_choice = choice;
    s_bank = NULL;                                   /* measure the room's bank again for the new pack */
    font_follow_choice();
    if (pe_mod_path(path, sizeof path, "pe_lang_selection.dat") || pe_lang_selection_save(path, &id)) {
        pe_mod_log(MOD_ID, "could not save language selection");
        pe_toast("Could not save language. Selection applies for this session.");
    }
    return 1;
}

/* The host hides this service while pe_lang or All mods is off. Resolve the active pack
 * per lookup, so F1 changes and missing packs immediately use each caller's fallback. */
static const unsigned char *mod_text(const char *key) {
    return key && !strncmp(key, "mod/", 4) ? pe_lang_pack_find(active_pack(), key) : NULL;
}
static const PeTranslationsApi s_translations = {mod_text};
static const char *interface_text(const char *source) {
    char key[40];
    snprintf(key,sizeof key,"mod/ui/%08x",pe_lang_hash(source));
    return pe_lang_text_utf8((PeLangPack *)active_pack(),key);
}
static unsigned interface_encode(unsigned char *out,unsigned size,const char *text) {
    return pe_lang_text_encode(active_pack(),out,size,text);
}
static const char *interface_item(unsigned id) {
    char key[32];
    if (!id || id>255) return NULL;
    snprintf(key,sizeof key,"item/%03u",id-1);
    return pe_lang_text_utf8((PeLangPack *)active_pack(),key);
}
static const PeInterfaceTextApi s_interface_text = {interface_text,interface_encode,interface_item};

static int lang_init(void) {
    char dir[4096];
    pe_service_publish("translations", 1, &s_translations, MOD_ID);
    pe_service_publish("interface_text", 1, &s_interface_text, MOD_ID);
    const char *base = SDL_GetBasePath();
    if (base) {
        snprintf(dir, sizeof dir, "%slang/", base);
        load_dir(dir);
    }
    if (!pe_mod_path(dir, sizeof dir, "lang/")) load_dir(dir);

    PeLangSelection selected;
    s_choice = 0;
    if (!pe_mod_path(dir, sizeof dir, "pe_lang_selection.dat") && !pe_lang_selection_load(dir, &selected))
        s_choice = pe_lang_selection_find(&selected, s_packs, s_pack_count);
    s_names[0] = "Game text";
    for (unsigned i = 0; i < s_pack_count; ++i) s_names[i + 1] = s_labels[i];
    pe_hook_add(PE_HOOK_TEXT_LOOKUP, on_text_lookup, NULL, PE_PRIORITY_EARLY, MOD_ID);
    pe_hook_add(PE_HOOK_TEXT_PAGE, on_text_page, NULL, PE_PRIORITY_EARLY, MOD_ID);
    pe_hook_add(PE_HOOK_SCENE_START, on_scene_start, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_TIM_UPLOAD, on_tim_upload, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_TEXT_GLYPH, on_text_glyph, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_FRAME_DRAWN, on_cjk_frame, NULL, PE_PRIORITY_NORMAL, MOD_ID);

    pe_menu_begin("Language");
    pe_menu_top_page("Language", "Play with the game's text from a language pack.", 0);
    pe_menu_register_live_choice(0, "Language",
                            "A pack from lang/ beside the game or in the user folder; Game text turns it off.",
                            s_names, s_pack_count + 1, language_get, language_set);
    pe_menu_register_info(0, "In use", "Untranslated text stays as the game has it.", active_text);
    pe_menu_begin(0);
    return 0;
}

static void lang_shutdown(void) {
    for (unsigned i = 0; i < s_pack_count; ++i) pe_lang_pack_free(&s_packs[i]);
    s_pack_count = 0;
    s_choice = 0;
    s_bank = NULL;
    if (s_font_added) {
        remove_widths();
        s_font_added = 0;
        s_font_cyrillic = 0;
    }
    s_font_saved = 0;
    s_font_pack = NULL;
    pe_cjk_reset(&s_cjk, NULL, NULL);
}

const PeMod pe_mod_pe_lang = {MOD_ID, "Language packs", "0.1", NULL, lang_init, lang_shutdown};
