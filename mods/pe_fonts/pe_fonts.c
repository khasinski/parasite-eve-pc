/* Fonts: F1 > Font picks the letters the dialogue boxes and menus draw. See "Fonts (pe_fonts)" in
 * mods/README.md.
 *
 * Original keeps the game's own letters. Every picture in mods/fonts/ (beside the game, or in the
 * user folder beside pe_mods.ini) is another choice, named after its file, with no code to add; the
 * release brings Pixel Sans. The letters go into the dialogue font page as it goes to VRAM
 * (PE_HOOK_TIM_UPLOAD, or at once when F1 picks one) and the widths into the game's own tables, so
 * pe_lang builds its accents on the font in use. The rest of the page stays as it is. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "pe1/pe_image.h"
#include "pe1/psyq_gpu.h"
#include "pe1/psyq_tim.h"
#include "pe1/resident_tables.h"
#include "pe1/textbox.h"
#include "pe_fonts_sheet.h"
#include "pe_hooks.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"

#define MOD_ID "pe_fonts"
#define MAX_FONTS 32
#define NAME_SIZE 64
#define PAGE_BYTES (128u * 256u)        /* 64 words x 256 rows of 4-bit pixels */
#define CHOICE_FILE "pe_fonts_selection.txt"

typedef struct Font {
    char file[NAME_SIZE];               /* what the choice is saved as */
    char label[NAME_SIZE];
    PeFontSheet sheet;
} Font;

static Font *s_fonts[MAX_FONTS];
static unsigned s_font_count;
static const char *s_names[MAX_FONTS + 1];
static int s_choice;                    /* 0: the game's letters, else s_fonts[s_choice - 1] */

static const RECT kPageRect = {0x140, 0, 64, 256};
static PeFontSheet s_game_sheet;        /* the game's own letters, from the page as it first loads */
static int s_page_saved;
static unsigned char s_page_work[PAGE_BYTES];

static TextboxGlyphSpacing s_game_widths[78];
static unsigned char s_game_metrics[PE_FONT_CODES];
static int s_metrics_saved;
static int s_widths_touched;            /* a font's widths went in since the start (or Original's back) */

static const Font *active_font(void) {
    return s_choice > 0 && (unsigned)s_choice <= s_font_count ? s_fonts[s_choice - 1] : NULL;
}

/* ---- Widths ---- */

/* The menus' metrics live in the resident pack. The disc's bytes are the same every time it is
 * read, so the first look is the game's own: taken before pe_lang (a later hook) changes any. */
static void install_metrics(const PeFontSheet *sheet) {
    if (!g_ResidentDataPack.directory.glyphMetrics) return;          /* not loaded yet: next scene */
    unsigned char *metrics = &g_ResidentDataPack.bytes[g_ResidentDataPack.directory.glyphMetrics];
    if (!s_metrics_saved) {
        memcpy(s_game_metrics, metrics, sizeof s_game_metrics);
        s_metrics_saved = 1;
    }
    for (unsigned i = 0; i < PE_FONT_CODES; ++i)
        metrics[i] = sheet && sheet->metric[i] ? sheet->metric[i] : s_game_metrics[i];
}

/* Original from the start writes nothing: the game's own tables stay as the disc and the game left
 * them (a saved copy could be stale after a disc change). Only after a font's widths went in are
 * the saved ones put back. */
static void install_widths(void) {
    const Font *font = active_font();
    if (!font && !s_widths_touched) return;
    s_widths_touched = font != NULL;
    memcpy(g_TextboxGlyphSpacing, s_game_widths, sizeof s_game_widths);
    if (font)
        for (unsigned i = 0; i < PE_FONT_CODES; ++i)
            if (font->sheet.advance[i]) {
                g_TextboxGlyphSpacing[i].left = 0;
                g_TextboxGlyphSpacing[i].right = font->sheet.advance[i];
            }
    install_metrics(font ? &font->sheet : NULL);
}

/* ---- The font page ---- */

/* The US dialogue font page: 4-bit, its image at VRAM 0x140,0 and its palette at 0x140,0xFC. */
static unsigned char *font_pixels(TimFile *tim) {
    if ((tim->flags & 7) != 0 || !(tim->flags & 8)) return NULL;
    TimBlock *clut = &tim->first_block;
    TimBlock *image = (TimBlock *)&clut->bytes[clut->header.length];
    const RECT *c = &clut->header.rect, *r = &image->header.rect;
    if (c->x != 0x140 || c->y != 0xFC || r->x != 0x140 || r->y != 0 || r->w != 64 || r->h != 256) return NULL;
    return (unsigned char *)image->header.pixels;
}

static int on_tim_upload(void *ctx, void *user) {
    (void)user;
    if (PE_JAPAN) return 0;             /* the Japanese font page is laid out differently */
    unsigned char *pixels = font_pixels(ctx);
    if (!pixels) return 0;
    if (!s_page_saved) {
        pe_font_sheet_take(pixels, 128, &s_game_sheet);
        s_page_saved = 1;
    }
    if (active_font()) pe_font_sheet_write(&active_font()->sheet, pixels, 128);
    install_widths();
    return 0;
}

/* The resident pack can be read again with a room, the game's own bytes back in it. */
static int on_scene_start(void *ctx, void *user) {
    (void)ctx;
    (void)user;
    if (PE_JAPAN) return 0;
    install_widths();
    return 0;
}

/* ---- The folder and the F1 page ---- */

static int compare_fonts(const void *a, const void *b) {
    return SDL_strcasecmp((*(Font *const *)a)->label, (*(Font *const *)b)->label);
}

static void load_font(const char *dir, const char *file) {
    char path[4096];
    if (s_font_count >= MAX_FONTS || strlen(file) >= NAME_SIZE) return;
    for (unsigned i = 0; i < s_font_count; ++i)
        if (!SDL_strcasecmp(s_fonts[i]->file, file)) return;       /* the one beside the game wins */
    snprintf(path, sizeof path, "%s%s", dir, file);
    SDL_Surface *loaded = SDL_LoadPNG(path);
    SDL_Surface *image = loaded ? SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32) : NULL;
    Font *font = image ? calloc(1, sizeof *font) : NULL;
    if (font && !pe_font_sheet_read(image->pixels, (unsigned)image->w, (unsigned)image->h,
                                    (unsigned)image->pitch, &font->sheet)) {
        snprintf(font->file, sizeof font->file, "%s", file);
        snprintf(font->label, sizeof font->label, "%s", file);
        char *dot = strrchr(font->label, '.');
        if (dot) *dot = 0;
        for (char *p = font->label; *p; ++p)
            if (*p == '_') *p = ' ';
        s_fonts[s_font_count++] = font;
        pe_mod_log(MOD_ID, "font \"%s\" from %s", font->label, path);
    } else {
        free(font);
        pe_mod_log(MOD_ID, "skipped %s: not a font picture (PNG, 252 or 256 wide, at least 48 tall)", path);
    }
    SDL_DestroySurface(image);
    SDL_DestroySurface(loaded);
}

static void load_dir(const char *dir) {
    int count = 0;
    char **files = SDL_GlobDirectory(dir, "*.png", SDL_GLOB_CASEINSENSITIVE, &count);
    for (int i = 0; files && i < count; ++i) load_font(dir, files[i]);
    SDL_free(files);
}

static void choice_save(void) {
    char path[4096];
    FILE *f;
    if (pe_mod_path(path, sizeof path, CHOICE_FILE) || !(f = fopen(path, "w"))) return;
    if (active_font()) fputs(active_font()->file, f);
    fclose(f);
}

static void choice_load(void) {
    char path[4096], file[NAME_SIZE] = "";
    FILE *f;
    s_choice = 0;
    if (pe_mod_path(path, sizeof path, CHOICE_FILE) || !(f = fopen(path, "r"))) return;
    if (fgets(file, sizeof file, f)) {
        file[strcspn(file, "\r\n")] = 0;
        for (unsigned i = 0; i < s_font_count; ++i)
            if (!SDL_strcasecmp(s_fonts[i]->file, file)) s_choice = (int)i + 1;
    }
    fclose(f);
}

static int font_get(void) { return s_choice; }

/* A pick in F1 shows at once: the letter cells of the page in VRAM are rewritten and the rest of
 * it (pe_lang's accents) is left as it is. */
static int font_set(int choice) {
    if (choice < 0 || (unsigned)choice > s_font_count) return 0;
    s_choice = choice;
    choice_save();
    if (PE_JAPAN) return 1;
    if (s_page_saved) {
        DrawSync(0);
        StoreImage((RECT *)&kPageRect, s_page_work);
        DrawSync(0);
        pe_font_sheet_write(active_font() ? &active_font()->sheet : &s_game_sheet, s_page_work, 128);
        LoadImage((RECT *)&kPageRect, s_page_work);
    }
    install_widths();
    return 1;
}

static int fonts_init(void) {
    char dir[4096];
    memcpy(s_game_widths, g_TextboxGlyphSpacing, sizeof s_game_widths);
    const char *base = SDL_GetBasePath();
    if (base) {
        snprintf(dir, sizeof dir, "%smods/fonts/", base);
        load_dir(dir);
    }
    if (!pe_mod_path(dir, sizeof dir, "mods/fonts/")) load_dir(dir);
    qsort(s_fonts, s_font_count, sizeof *s_fonts, compare_fonts);
    choice_load();
    s_names[0] = "Original";
    for (unsigned i = 0; i < s_font_count; ++i) s_names[i + 1] = s_fonts[i]->label;

    pe_hook_add(PE_HOOK_TIM_UPLOAD, on_tim_upload, NULL, PE_PRIORITY_EARLY, MOD_ID);
    pe_hook_add(PE_HOOK_SCENE_START, on_scene_start, NULL, PE_PRIORITY_EARLY, MOD_ID);

    pe_menu_begin("Font");
    pe_menu_top_page("Font", "Change the letters of the game's text.", 0);
    pe_menu_register_live_choice(0, "Font", "Original is the game's own; the others come from mods/fonts/.",
                                 s_names, s_font_count + 1, font_get, font_set);
    pe_menu_begin(0);
    return 0;
}

static void fonts_shutdown(void) {
    s_choice = 0;
    install_widths();
    for (unsigned i = 0; i < s_font_count; ++i) free(s_fonts[i]);
    memset(s_fonts, 0, sizeof s_fonts);
    s_font_count = 0;
    s_page_saved = 0;
    s_metrics_saved = 0;
}

const PeMod pe_mod_pe_fonts = {MOD_ID, "Font", "0.1", NULL, fonts_init, fonts_shutdown};
