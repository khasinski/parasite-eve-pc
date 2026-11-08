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
 * them, as blanks: with Game text the page, widths and metrics are put back as the game had them.
 *
 * A pack can also have a letter table (localization/petext/letters.py): the codes its text doesn't
 * use, given to letters it needs. Those letters go on the page after the rest, widths and all, so
 * they are one byte each and everything downstream treats them as any other letter. Such a code
 * means something only in that pack, which is why none may reach a save. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "pe1/psyq_gpu.h"
#include "pe1/psyq_tim.h"
#include "pe1/resident_tables.h"
#include "pe1/textbox.h"
#include "pe1/text_render.h"
#include "pe1/menu_inventory.h"
#include "pe1/save.h"
#include "pe1/save_blob.h"
#include "pe_hooks.h"
#include "pe_lang_font.h"
#include "pe_lang_cyrillic.h"
#include "pe_lang_cjk.h"
#include "pe1/mod_text.h"
#include "pe_lang_pack.h"
#include "pe_lang_bundle.h"
#include "pe_services.h"
#include "pe_lang_selection.h"
#include "pe_lang_text.h"
#include "pe_lang_text_data.h"
#include "pe_lang_names.h"
#include "pe_save.h"
#include "pe_game.h"
#include "pe_translate.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"

#if !defined(PE_PS5) && !defined(PE_PS5_PLUGINS)
#include <psyz/overlay_sdl3.h>
#define PE_LANG_PICKER 1
#else
#define PE_LANG_PICKER 0
#endif

#define MOD_ID "pe_lang"
#define MAX_PACKS 64

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
            if (Pe_ExtendedGlyph(p, NULL) || pe_lang_pack_has_letter(active_pack(), *p)) return 0;
    }
    if (lookup->table == 4 && lookup->index >= 0x73 && lookup->index <= 0x75 && active_pack()) {
        /* A name entry grid: its cells on codes a pack draws as its own letters stay empty, as in
         * English, so none of those codes shows or reaches a save (pe_lang_grid_blank). */
        static unsigned char grids[3][512];
        static const PeLangPack *logged;
        unsigned char *grid = grids[lookup->index - 0x73];
        const unsigned char *from = text ? text : *lookup->text;
        int blanked = from ? pe_lang_grid_blank(from, grid, sizeof grids[0]) : -1;
        if (blanked > 0) {
            text = grid;
            if (logged != active_pack()) {
                logged = active_pack();
                pe_mod_log(MOD_ID, "names: %s: %d name entry grid cells kept empty, as in English (sys/%03u)",
                           s_labels[active_pack() - s_packs], blanked, lookup->index);
            }
        }
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
    const PeLangPack *pack = active_pack();
    const unsigned letters = pack ? pack->letter_count : 0;
    unsigned char advance[78], *metrics;
    for (unsigned i = 0; i < 78; ++i) advance[i] = g_TextboxGlyphSpacing[i].right;
    /* The pack's own pictures of the game's letters bring their widths, with no left gap; letters
     * built on those take their widths from them. */
    for (unsigned k = 0; k < letters; ++k) {
        const unsigned char *r = pe_lang_pack_letter(pack, k);
        if (r[4] < 78 && pe_lang_letter_redraws(r)) advance[r[4]] = r[5];
    }
    memset(s_widths, 0, sizeof s_widths);
    memcpy(s_widths, g_TextboxGlyphSpacing, sizeof g_TextboxGlyphSpacing);
    for (unsigned i = 0; i < 78; ++i) s_widths[i].right = advance[i];
    for (unsigned k = 0; k < letters; ++k)
        if (pe_lang_letter_redraws(pe_lang_pack_letter(pack, k))) s_widths[pe_lang_pack_letter(pack, k)[4]].left = 0;
    for (unsigned k = 0; k < PE_LATIN_COUNT; ++k)
        s_widths[kPeLatinGlyphs[k].code].right = (u8)pe_lang_font_advance(&kPeLatinGlyphs[k], advance);
    if (s_font_cyrillic)
        for (unsigned k = 0; k < PE_CYRILLIC_COUNT; ++k)
            s_widths[kPeCyrillicGlyphs[k].code].right = kPeCyrillicGlyphs[k].advance;
    for (unsigned k = 0; k < letters; ++k)
        s_widths[pe_lang_pack_letter(pack, k)[4]].right = (u8)pe_lang_letter_advance(pe_lang_pack_letter(pack, k), advance);
    g_TextboxGlyphWidths = s_widths;
    g_TextboxGlyphCount = FONT_CODES;
    if (!g_ResidentDataPack.directory.glyphMetrics) return;     /* not loaded yet: next scene */
    metrics = &g_ResidentDataPack.bytes[g_ResidentDataPack.directory.glyphMetrics];
    if (!s_metrics_saved) memcpy(s_metrics_original, metrics, sizeof s_metrics_original);
    for (unsigned k = 0; k < letters; ++k) {                 /* the redrawn game letters first */
        const unsigned char *r = pe_lang_pack_letter(pack, k);
        if (r[4] < PE_LATIN_FIRST_CODE) metrics[r[4]] = pe_lang_letter_metric(r, metrics);
    }
    for (unsigned k = 0; k < PE_LATIN_COUNT; ++k)
        metrics[kPeLatinGlyphs[k].code] = pe_lang_font_metric(&kPeLatinGlyphs[k], metrics);
    if (s_font_cyrillic)
        for (unsigned k = 0; k < PE_CYRILLIC_COUNT; ++k)
            metrics[kPeCyrillicGlyphs[k].code] = (unsigned char)(kPeCyrillicGlyphs[k].advance << 4);
    for (unsigned k = 0; k < letters; ++k)
        metrics[pe_lang_pack_letter(pack, k)[4]] = pe_lang_letter_metric(pe_lang_pack_letter(pack, k), metrics);
    s_metrics_saved = 1;
}

/* The game's own widths and metrics back. */
static void remove_widths(void) {
    g_TextboxGlyphWidths = g_TextboxGlyphSpacing;
    g_TextboxGlyphCount = sizeof g_TextboxGlyphSpacing / sizeof *g_TextboxGlyphSpacing;
    if (s_metrics_saved && g_ResidentDataPack.directory.glyphMetrics) {
        unsigned char *metrics = &g_ResidentDataPack.bytes[g_ResidentDataPack.directory.glyphMetrics];
        memcpy(metrics, s_metrics_original, FONT_CODES);   /* a pack may redraw the game's letters too */
    }
    s_metrics_saved = 0;
}

/* Put the glyphs into `pixels` (the page on its way to VRAM, or a copy) when a pack is in use. */
static void add_glyphs(unsigned char *pixels) {
    const PeLangPack *pack = active_pack();
    const unsigned char *records = pack ? pack->data + pack->letters_offset : NULL;
    unsigned low = 0, redrawn = 0;
    /* First the pack's own pictures of the game's letters (they come first, by rising code), so the
     * accented letters made from the game's letters are made from them. */
    while (pack && low < pack->letter_count && records[low * PE_LANG_LETTER_SIZE + 4] < PE_LATIN_FIRST_CODE) ++low;
    int own = low ? pe_lang_letters_fill(records, low, pixels, 128, 256) : 0;
    int added = pe_lang_font_fill(pixels, 128, 256);
    s_font_cyrillic = pe_lang_pack_uses_glyphs(pack, PE_CYRILLIC_FIRST_CODE, PE_CYRILLIC_LAST_CODE);
    int extra = s_font_cyrillic ? pe_lang_cyrillic_fill(pixels, 128, 256) : 0;
    /* After the others: a table's letter takes over its code's cell. */
    own += pack && pack->letter_count > low
               ? pe_lang_letters_fill(records + low * PE_LANG_LETTER_SIZE, pack->letter_count - low, pixels, 128, 256) : 0;
    for (unsigned k = 0; pack && k < pack->letter_count; ++k) redrawn += pe_lang_letter_redraws(pe_lang_pack_letter(pack, k));
    if (!s_font_added && added + extra + own) {
        pe_mod_log(MOD_ID, "added %d Latin Extended, %d Cyrillic and %d of the pack's own letters to the dialogue font",
                   added, extra, own - (int)redrawn);
        if (redrawn)
            pe_mod_log(MOD_ID, "text: %s draws %u letters with its own pictures and widths", s_labels[pack - s_packs], redrawn);
    }
    added += extra + own;
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
/* What the log says about the text drawn with the pack in use, once each (the disc tests read
 * these): stretches and name letters in use, and each letter the pack lacks, which shows '?'. */
#define MISSING_LOGGED 32u
static const PeLangPack *s_text_logged;
static int s_stretch_logged, s_name_letter_logged;
static unsigned s_uncoded_logged;           /* a bit per PeTextStyle */
static unsigned s_missing[MISSING_LOGGED], s_missing_count;
static void text_log_follow(const PeLangPack *pack) {
    if (s_text_logged == pack) return;
    s_text_logged = pack;
    s_stretch_logged = s_name_letter_logged = 0;
    s_uncoded_logged = 0;
    s_missing_count = 0;
}
/* Each missing thing once: a letter (Unicode), a name letter (paged) or a stretch. */
static int missing_once(const PeLangPack *pack, unsigned key) {
    text_log_follow(pack);
    for (unsigned i = 0; i < s_missing_count; ++i)
        if (s_missing[i] == key) return 0;
    if (s_missing_count == MISSING_LOGGED) return 0;
    s_missing[s_missing_count++] = key;
    return 1;
}
static int letter_missing(const PeLangPack *pack, unsigned letter, unsigned paged) {
    if (!missing_once(pack, paged ? paged | 0x80000000u : letter)) return 0;
    if (paged) pe_mod_log(MOD_ID, "text: %s has no name letter %02X %02X; it shows '?'",
                          pack ? s_labels[pack - s_packs] : "no pack chosen", 0xFAu + (paged >> 8), paged & 0xFFu);
    else pe_mod_log(MOD_ID, "text: %s has no letter U+%04X; it shows '?'", s_labels[pack - s_packs], letter);
    return 0;
}
/* A name letter no pack in use names (a name saved with another pack, or with no pack chosen):
 * '?', in menus as in dialogue, where the game's cell for that code is blank and the name would
 * vanish. Not FB 00 and the reserved numbers (FD 04, FE 04): the game's own menu text draws those,
 * and they keep the game's cell. */
static int name_letter_missing(PeHookTextLetter *l, const PeLangPack *pack) {
    unsigned page = l->paged >> 8, byte = l->paged & 0xFFu, id = (page - 1u) * 250u + byte;
    if (page < 1 || page > 4 || byte >= 0xFA || !id || PE_LANG_NAME_RESERVED(id)) return 0;
    letter_missing(pack, 0, l->paged);
    l->code = 0x2C;
    return 1;
}

/* The text renderer (pe1/text_render.h): a stretch's letters, and how a letter draws. The
 * renderer asks only for letters the game's English set lacks or a pack overrides. */
static int on_text_stretch(void *ctx, void *user) {
    PeHookTextStretch *s = ctx;
    unsigned count = 0;
    const PeLangPack *pack = active_pack();
    const uint32_t *letters = pe_lang_pack_stretch(pack, s->id, &count);
    (void)user;
    if (!letters) {
        if (pack && missing_once(pack, s->id | 0x40000000u))
            pe_mod_log(MOD_ID, "text: %s has no stretch %u", s_labels[pack - s_packs], s->id);
        return 0;
    }
    text_log_follow(pack);
    if (!s_stretch_logged) {
        s_stretch_logged = 1;
        pe_mod_log(MOD_ID, "text: %s's stretches in use (first: %u, %u letters)", s_labels[pack - s_packs], s->id, count);
    }
    s->letters = (const unsigned *)letters;
    s->count = count;
    return 1;
}
static int dictionary_index(const PeLangPack *pack, unsigned letter) {
    unsigned lo = 0, hi = pack->cjk_count;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2, at = pack->cjk_offset + mid * 76;
        unsigned cp = pack->data[at] | pack->data[at+1] << 8 | pack->data[at+2] << 16 | (unsigned)pack->data[at+3] << 24;
        if (cp == letter) return (int)mid;
        if (cp < letter) lo = mid + 1; else hi = mid;
    }
    return -1;
}
/* How far the pen moves after the table's letter with no code `k` in the dialogue box, as
 * petext/letters.py measures it. */
static unsigned extra_advance(const PeLangPack *pack, unsigned k) {
    unsigned char advance[78];
    for (unsigned i = 0; i < 78; ++i) advance[i] = g_TextboxGlyphSpacing[i].right;
    return pe_lang_letter_advance(pe_lang_pack_extra(pack, k), advance);
}
/* Its menus' metrics byte (drawn width << 4 | first column), 0 before the game loads its own. */
static unsigned extra_metric(const PeLangPack *pack, unsigned k) {
    if (!g_ResidentDataPack.directory.glyphMetrics) return 0;
    return pe_lang_letter_metric(pe_lang_pack_extra(pack, k),
                                 &g_ResidentDataPack.bytes[g_ResidentDataPack.directory.glyphMetrics]);
}

/* Whether the pack in use reads right to left (from its language code), and which Unicode letter
 * its one-byte codes draw, for the renderer's right-to-left lines. */
static const PeLangPack *s_rtl_logged;
static int on_text_script(void *ctx, void *user) {
    PeHookTextScript *t = ctx;
    const PeLangPack *pack = active_pack();
    (void)user;
    if (!pack) return 0;
    t->rtl = pe_lang_code_rtl(pack->lang);
    if (t->rtl && s_rtl_logged != pack) {
        s_rtl_logged = pack;
        pe_mod_log(MOD_ID, "text: %s reads right to left (%s)", s_labels[pack - s_packs], pack->lang);
    }
    if (t->code < 0xF1) t->letter = (pack->unicode ? pack->unicode : kPeTextUnicode)[t->code];
    return 1;
}

static int on_text_letter(void *ctx, void *user) {
    PeHookTextLetter *l = ctx;
    const PeLangPack *pack = active_pack();
    const unsigned *codes;
    unsigned cell = 0x2C, metric = 0;
    int index;
    (void)user;
    if (!pack) return l->paged ? name_letter_missing(l, NULL) : 0;
    if (l->paged) {                              /* a two-byte name letter: its Unicode letter */
        int code;
        l->letter = pe_lang_pack_name_letter(pack, l->paged);
        if (!l->letter) return name_letter_missing(l, pack);
        text_log_follow(pack);
        if (l->draw && !s_name_letter_logged) {
            s_name_letter_logged = 1;
            pe_mod_log(MOD_ID, "text: name letters in use (first drawn: %02X %02X, U+%04X)", 0xFAu + (l->paged >> 8),
                       l->paged & 0xFFu, l->letter);
        }
        code = PeText_GameCode(l->letter);
        if (code >= 0) { l->code = code; return 1; }
    }
    /* a one-byte letter on the page: the pack's table, or the Latin Extended and Cyrillic set */
    codes = pack->unicode ? pack->unicode : kPeTextUnicode;
    for (unsigned code = 0x4D; code < 0xF1; ++code)
        if (codes[code] == l->letter && s_font_added) { l->code = (int)code; return 1; }
    /* a table letter with no code, made in the letter cache like a dictionary letter; menus cut it
     * as they cut the game's own letters: from its first inked column, its drawn width and a pixel */
    index = pe_lang_pack_extra_index(pack, l->letter);
    if (index >= 0) {
        unsigned first, width;
        if (PE_LANG_JOINING(l->letter) && pe_lang_letter_cut(pe_lang_pack_extra(pack, (unsigned)index), &first, &width)) {
            metric = width << 4 | first;            /* joined letters touch: cut to the strokes */
            l->advance = width + 1;
        } else if (l->style == PE_TEXT_MENU) {
            metric = extra_metric(pack, (unsigned)index);
            if ((metric & 15) >= 12 || !(metric >> 4)) metric = 0;
            l->advance = metric ? (metric >> 4) + 1 : 12;
        } else {
            l->advance = extra_advance(pack, (unsigned)index);
        }
        text_log_follow(pack);
        if (l->draw && !(s_uncoded_logged & 1u << l->style)) {
            s_uncoded_logged |= 1u << l->style;
            pe_mod_log(MOD_ID, "text: %s's letters with no code in use in %s (first drawn: U+%04X, advance %u)",
                       s_labels[pack - s_packs], l->style == PE_TEXT_MENU ? "menus" : "dialogue", l->letter,
                       l->advance);
        }
        index += (int)pack->cjk_count;
    } else {
        index = dictionary_index(pack, l->letter);
        if (index < 0) return PeText_GameCode(l->letter) < 0 ? letter_missing(pack, l->letter, 0) : 0;
        l->advance = pe_lang_pack_glyph_width(pack, (unsigned)index);
    }
    l->code = -1;
    if (l->draw && (!s_font_saved || !pe_cjk_resolve(&s_cjk, (unsigned)index, &cell, cjk_sync, cjk_upload, NULL))) {
        if (!s_cjk_warned) {
            pe_mod_log(MOD_ID, "letter cache unavailable/full (%u cells); showing ?", s_cjk.count);
            s_cjk_warned = 1;
        }
        cell = 0x2C;
        metric = 0;
    }
    l->u = (unsigned char)(cell % 21 * 12);
    l->v = (unsigned char)(cell / 21 * 12);
    if (metric) {                                /* the cut, kept inside the cell */
        unsigned first = metric & 15, width = metric >> 4;
        l->u = (unsigned char)(l->u + first);
        l->w = (unsigned char)(width < 12 - first ? width : 12 - first);
    }
    return 1;
}


/* ---- Names in the pack's letters, kept with the save (pe_lang_names.h) ----
 * The three name windows: Aya's name (metadata window 1), then the weapon's and armor's being
 * renamed. Each keeps room for its end byte: 13, 16 and 16 bytes. Metadata window 0 is never read
 * or written: the USA game leaves it unused and keeps the play time right after its 8 bytes. */
#define NAME_WINDOWS 3
static unsigned char *name_window(unsigned w) {
    return w == 0 ? g_SaveRuntimeState.windows.metadata[1].text : g_SaveRuntimeState.cursorMetadata[w - 1].text;
}
static const unsigned kNameRoom[NAME_WINDOWS] = {12, 15, 15};
static const char *const kNameWindow[NAME_WINDOWS] = {"Aya's name", "the weapon's name", "the armor's name"};
/* Each name in Unicode while the game runs; the save holds the window's bytes as they are. */
typedef struct KeptNames {
    uint8_t count[NAME_WINDOWS];             /* 0: nothing kept, the window's bytes are the name */
    uint32_t letters[NAME_WINDOWS][PE_LANG_NAME_MAX];
} KeptNames;
static KeptNames s_kept_names;
static int s_names_pending;

/* The window's name in Unicode, from `pack`'s letters, unless the window still holds what was put
 * there from the kept letters (a pack that lacks some shows '?', which must not replace them). */
static void names_capture(const PeLangPack *pack) {
    for (unsigned w = 0; w < NAME_WINDOWS; ++w) {
        unsigned char *window = name_window(w), expected[16];
        uint32_t letters[PE_LANG_NAME_MAX];
        int n;
        if (s_kept_names.count[w]) {
            pe_lang_name_encode(pack, s_kept_names.letters[w], s_kept_names.count[w], expected, kNameRoom[w]);
            if (pe_lang_name_same(window, expected, kNameRoom[w])) continue;
        }
        n = pe_lang_name_decode(pack, window, kNameRoom[w], letters);
        if (n < 0) continue;                 /* not a name the pack can read: left alone */
        s_kept_names.count[w] = (uint8_t)n;
        memcpy(s_kept_names.letters[w], letters, sizeof letters);
    }
}
/* The kept names into the windows, in `pack`'s letters. */
static void names_put(const PeLangPack *pack) {
    int changed = 0;
    for (unsigned w = 0; w < NAME_WINDOWS; ++w) {
        if (!s_kept_names.count[w]) continue;
        pe_lang_name_encode(pack, s_kept_names.letters[w], s_kept_names.count[w], name_window(w), kNameRoom[w]);
        changed = 1;
        pe_mod_log(MOD_ID, "names: %s put in %s's letters (%u letters)", kNameWindow[w],
                   pack ? pack->name : "the game's own text", s_kept_names.count[w]);
    }
    if (changed) Menu_CopyPromptCodes((u8 *)Save_GetActiveMetadataBuffer());   /* the dialogue's {name} */
}
/* Saving: the save's copy keeps the window's bytes, the pack in use's letters. Where that pack
 * lacks some of a name's letters (the window shows '?'), the copy gets the letters of a pack that
 * has them all, so the name comes back whole after a load. */
static int on_names_write(void *ctx, void *user) {
    unsigned char *block = ctx;
    const PeLangPack *pack = active_pack();
    (void)user;
    names_capture(pack);
    for (unsigned w = 0; w < NAME_WINDOWS; ++w) {
        unsigned char *copy = block + 0x100 + (name_window(w) - (unsigned char *)&g_SaveRuntimeState);
        int i = pe_lang_name_save_pack(s_packs, s_pack_count, pack, s_kept_names.letters[w], s_kept_names.count[w],
                                       kNameRoom[w]);
        if (i < 0) continue;                 /* the window's bytes as they are */
        pe_lang_name_encode(&s_packs[i], s_kept_names.letters[w], s_kept_names.count[w], copy, kNameRoom[w]);
        pe_mod_log(MOD_ID, "names: %s saved in %s's letters (%s lacks some)", kNameWindow[w], s_labels[i],
                   pack ? pack->name : "the game's own text");
    }
    return 0;
}
static int on_names_read(void *ctx, void *user) {
    (void)ctx; (void)user;
    s_names_pending = 1;                     /* once the game has the save's windows */
    return 0;
}
static int on_names_new_game(void *ctx, void *user) {
    (void)ctx; (void)user;
    memset(&s_kept_names, 0, sizeof s_kept_names);
    return 0;
}
/* After a load: a name with two-byte letters is read with the pack that knows them all (the one
 * in use first), then shown in the pack in use's letters. */
static void names_frame(void) {
    const PeLangPack *pack = active_pack();
    int changed = 0;
    if (!s_names_pending) return;
    s_names_pending = 0;
    memset(&s_kept_names, 0, sizeof s_kept_names);
    for (unsigned w = 0; w < NAME_WINDOWS; ++w) {
        uint32_t letters[PE_LANG_NAME_MAX];
        unsigned count = 0;
        int found;
        if (!pe_lang_name_paged(name_window(w), kNameRoom[w])) continue;
        found = pe_lang_name_find_pack(s_packs, s_pack_count, pack ? (int)(pack - s_packs) : -1, name_window(w),
                                       kNameRoom[w], letters, &count);
        if (found < 0) {
            pe_mod_log(MOD_ID, "names: %s has name letters no loaded pack knows; left as saved", kNameWindow[w]);
            continue;
        }
        s_kept_names.count[w] = (uint8_t)count;
        memcpy(s_kept_names.letters[w], letters, sizeof letters);
        pe_mod_log(MOD_ID, "names: %s read with %s's letters (%u letters)", kNameWindow[w], s_labels[found], count);
        if (&s_packs[found] != pack) changed = 1;
    }
    if (changed) names_put(pack);
}

static int on_cjk_frame(void *ctx, void *user) {
    (void)ctx; (void)user;
    names_frame();
    pe_cjk_frame(&s_cjk);
    return 0;
}

static int by_name(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Put a loaded pack in the list: a pack with the same identity (language code and name) is
 * replaced, so a user-folder copy wins over the one beside the game and a picked update wins over
 * the old one. Returns its slot, or -1 (the pack freed) when the list is full. */
static int add_pack(PeLangPack *loaded, const char *path) {
    PeLangSelection id;
    memcpy(id.lang, loaded->lang, sizeof id.lang);
    memcpy(id.name, loaded->name, sizeof id.name);
    int existing = pe_lang_selection_find(&id, s_packs, s_pack_count);
    unsigned slot = existing ? (unsigned)existing - 1 : s_pack_count;
    if (slot == MAX_PACKS) {
        pe_mod_log(MOD_ID, "skipped %s: pack limit reached", path);
        pe_lang_pack_free(loaded);
        return -1;
    }
    if (existing) pe_lang_pack_free(&s_packs[slot]);
    s_packs[slot] = *loaded;
    snprintf(s_labels[slot], sizeof s_labels[0], "%s (%s)", loaded->name, loaded->lang);
    s_names[slot + 1] = s_labels[slot];
    pe_mod_log(MOD_ID, "%s: %s, %u entries", path, s_labels[slot], loaded->count);
    if (!existing) ++s_pack_count;
    return (int)slot;
}

/* Load the .pelang files in `dir` (ending in a separator), in file-name order. */
static void load_dir(const char *dir) {
    int count = 0;
    char **files = SDL_GlobDirectory(dir, "*.pelang", 0, &count);
    if (!files) return;
    qsort(files, (size_t)count, sizeof *files, by_name);
    for (int i = 0; i < count; ++i) {
        char path[4096];
        PeLangPack loaded;
        PeLangOpen result;
        snprintf(path, sizeof path, "%s%s", dir, files[i]);
        if ((result = pe_lang_bundle_load(&loaded, path)) != PE_LANG_OPEN_OK) {
            pe_mod_log(MOD_ID, "skipped %s: %s", path, pe_lang_bundle_error(result));
            continue;
        }
        add_pack(&loaded, path);
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
    if (pe_save_loaded()) names_capture(active_pack());   /* names in Unicode, then the new pack's letters */
    s_choice = choice;
    if (pe_save_loaded()) names_put(active_pack());
    s_bank = NULL;                                   /* measure the room's bank again for the new pack */
    font_follow_choice();
    pe_mod_log(MOD_ID, "language: %s", choice ? s_labels[choice - 1] : "the game's own text");
    if (pe_mod_path(path, sizeof path, "pe_lang_selection.dat") || pe_lang_selection_save(path, &id)) {
        pe_mod_log(MOD_ID, "could not save language selection");
        pe_toast("Could not save language. Selection applies for this session.");
    }
    return 1;
}

#if PE_LANG_PICKER
/* ---- F1 > Language > Add a translation: a file picker for .pelang files ----
 * The file is checked first, then copied into lang/ in the user folder (so it is there at the next
 * start) and switched to. The panel is modal and the game paused, so the picker is waited on here,
 * as the disc picker is at start. */
typedef struct Picked {
    SDL_AtomicInt done;
    char path[4096];
    int failed;
} Picked;

static void SDLCALL on_picked(void *user, const char *const *files, int filter) {
    Picked *picked = user;
    (void)filter;
    if (!files || (files[0] && strlen(files[0]) >= sizeof picked->path)) picked->failed = 1;
    else if (files[0]) snprintf(picked->path, sizeof picked->path, "%s", files[0]);
    SDL_SetAtomicInt(&picked->done, 1);
}

static const char *file_name(const char *path) {
    const char *name = path;
    for (const char *p = path; *p; ++p)
        if (*p == '/' || *p == '\\') name = p + 1;
    return name;
}

static int same_file(const char *a, const char *b) {
#ifdef _WIN32
    for (; *a && *b; ++a, ++b) {
        char x = *a == '\\' ? '/' : (char)SDL_tolower((unsigned char)*a);
        char y = *b == '\\' ? '/' : (char)SDL_tolower((unsigned char)*b);
        if (x != y) return 0;
    }
    return !*a && !*b;
#else
    return !strcmp(a, b);
#endif
}

static void add_translation(const char *path) {
    char dir[4096], dest[4096], toast[160];
    const char *name = file_name(path);
    size_t len = strlen(name);
    PeLangPack loaded;
    PeLangOpen result = pe_lang_bundle_load(&loaded, path);
    if (result != PE_LANG_OPEN_OK) {
        pe_mod_log(MOD_ID, "could not add %s: %s", path, pe_lang_bundle_error(result));
        snprintf(toast, sizeof toast, "Not added: %s.", pe_lang_bundle_error(result));
        pe_toast(toast);
        return;
    }
    /* Copied under its own name, given the .pelang ending if it lacks it, so the folder scan finds it. */
    int has_ending = len >= 7 && !SDL_strcasecmp(name + len - 7, ".pelang");
    int copied = !pe_mod_path(dir, sizeof dir, "lang/") && SDL_CreateDirectory(dir);
    if (copied) {
        int n = snprintf(dest, sizeof dest, "%s%s%s", dir, name, has_ending ? "" : ".pelang");
        copied = n > 0 && (size_t)n < sizeof dest;
    }
    if (copied && !same_file(path, dest)) copied = SDL_CopyFile(path, dest);
    if (copied) pe_mod_log(MOD_ID, "added %s as %s", path, dest);
    else pe_mod_log(MOD_ID, "could not copy %s into the user folder: %s", path, SDL_GetError());

    /* The pack in use may be the one replaced: point the letters away from it first, and
     * language_set below puts them back from the new copy. */
    PeLangSelection id;
    memcpy(id.lang, loaded.lang, sizeof id.lang);
    memcpy(id.name, loaded.name, sizeof id.name);
    int existing = pe_lang_selection_find(&id, s_packs, s_pack_count);
    if (existing && existing == s_choice) language_set(0);

    int slot = add_pack(&loaded, copied ? dest : path);
    if (slot < 0) {
        pe_toast("Not added: the language list is full.");
        return;
    }
    pe_menu_live_choice_resize(s_names, s_pack_count + 1);
    language_set(slot + 1);
    if (copied) snprintf(toast, sizeof toast, "Added %s.", s_labels[slot]);
    else snprintf(toast, sizeof toast, "Using %s for now: it could not be copied.", s_labels[slot]);
    pe_toast(toast);
}

static void pick_translation(void) {
    static const SDL_DialogFileFilter filters[] = {{"Parasite Eve translation (*.pelang)", "pelang"}};
    Picked picked;
    memset(&picked, 0, sizeof picked);
    SDL_PropertiesID props = SDL_CreateProperties();
    if (!props) return;
    SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, (void *)filters);
    SDL_SetNumberProperty(props, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, 1);
    SDL_SetBooleanProperty(props, SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, false);
    SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING, "Add a translation");
    SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, Psyz_GetWindow_SDL3());
    SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFILE, on_picked, &picked, props);
    while (!SDL_GetAtomicInt(&picked.done)) {
        SDL_PumpEvents();
        SDL_Delay(10);
    }
    SDL_DestroyProperties(props);
    /* Keys pressed in the picker must not reach the panel. */
    SDL_FlushEvents(SDL_EVENT_KEY_DOWN, SDL_EVENT_TEXT_INPUT);
    SDL_ResetKeyboard();
    if (picked.failed) {
        pe_mod_log(MOD_ID, "file picker failed: %s", SDL_GetError());
        pe_toast("The file picker could not open.");
    } else if (picked.path[0]) {
        add_translation(picked.path);
    }
}
#endif

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
    pe_hook_add(PE_HOOK_TEXT_LOOKUP, on_text_lookup, NULL, PE_PRIORITY_EARLY, MOD_ID);
    pe_hook_add(PE_HOOK_TEXT_PAGE, on_text_page, NULL, PE_PRIORITY_EARLY, MOD_ID);
    pe_hook_add(PE_HOOK_SCENE_START, on_scene_start, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_TIM_UPLOAD, on_tim_upload, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_TEXT_GLYPH, on_text_glyph, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_TEXT_STRETCH, on_text_stretch, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_SAVE_WRITE, on_names_write, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_SAVE_READ, on_names_read, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_NEW_GAME, on_names_new_game, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_TEXT_LETTER, on_text_letter, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_TEXT_SCRIPT, on_text_script, NULL, PE_PRIORITY_NORMAL, MOD_ID);
    pe_hook_add(PE_HOOK_FRAME_DRAWN, on_cjk_frame, NULL, PE_PRIORITY_NORMAL, MOD_ID);

    pe_menu_begin("Language");
    pe_menu_top_page("Language", "Play with the game's text from a language pack.", 0);
    pe_menu_register_live_choice(0, "Language",
                            "A pack from lang/ beside the game or in the user folder; Game text turns it off.",
                            s_names, s_pack_count + 1, language_get, language_set);
    pe_menu_register_info(0, "In use", "Untranslated text stays as the game has it.", active_text);
#if PE_LANG_PICKER
    pe_menu_register_action(0, "Add a translation",
                            "Pick a .pelang file: it is checked, copied to lang/ in the user folder and used.",
                            pick_translation);
#endif
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
    s_text_logged = NULL;
    memset(&s_kept_names, 0, sizeof s_kept_names);
    s_names_pending = 0;
    pe_cjk_reset(&s_cjk, NULL, NULL);
}

const PeMod pe_mod_pe_lang = {MOD_ID, "Language packs", "0.1", NULL, lang_init, lang_shutdown};
