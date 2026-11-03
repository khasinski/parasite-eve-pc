/*
 * Character packs (bridge findings D8 §3, format v1): costumes a player drops into
 * <preferences>/packs/, either as a plain zip or as the same layout unzipped in a folder:
 *   pack.json                       {"format": 1, "id": "re-classics", "name", "version", "author",
 *                                    "license", "kind": "assets", "min_wardrobe": 1}
 *   LICENSE                         required: the author's terms for redistribution
 *   characters/<folder>/            one costume each, e.g. claire.re2, claire.re2$biker
 *       character.json              {"name", "group", "tier": extra|outfit|supporting|main|special}
 *       texture.bin model.bin day1.bin ... day8.bin   the skin loader's files, same formats
 *       rooms.bin meta.bin          optional, as in pe_skins/
 * Read once at start-up; packs are data only and nothing in the game's files is replaced. Each
 * costume's key, saved in profiles, is "<pack id>/<folder>" (at most 63 bytes) and must never
 * change; the old pe_skins/ folders keep their own keys (pe_pack_pe1_costumes.c).
 *
 * A "recipe" pack (characters from the Parasite Eve disc, shared as instructions only) is built
 * into an assets pack in this folder by the bridge's tools/pe_assets/pe_pack_build.py, from the
 * player's own disc; the recipe itself is listed as needing that.
 *
 * Refused, with the reason on F1 > Collection > Wardrobe > Packs: a pack whose id appears twice
 * (every copy: the author field can't prove which is genuine, so the player removes one), a bad
 * or reserved id, a missing LICENSE, another format or kind (recipe packs need the builder), and
 * zips with unsafe or duplicate names, encryption, links or sizes past the limits below. A
 * character missing a file, or not shaped like Aya (host pe_model_check_skin: the bridge's
 * check_skins_struct.py), is refused alone; the pack's others load.
 *
 * Prices: packs state a tier, never Junk. Until Economy sets prices itself (D8 §4.3, `wardrobe`
 * v2), the tier maps to the kept curve 25 / 50 / 100 / 250 / 400 Junk, charged only while
 * Economy's Costume unlock rules apply.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "cJSON.h"
#include "miniz.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_model.h"
#include "pe_pages.h"
#include "pe_wardrobe.h"

#define PE_PACKS_DIR      "packs"
#define PE_MAX_PACKS      16
#define PE_MAX_CHARS      64            /* per pack */
#define PE_ID_MAX         31
#define PE_KEY_MAX        63
#define PE_FOLDER_MAX     47
#define PE_PACK_BYTES     (64u << 20)   /* a whole pack, unpacked */
#define PE_FILE_BYTES     (4u << 20)    /* one file (the skin loader's PE_FILE_MAX) */
#define PE_MAX_ENTRIES    4096
#define PE_MAX_RATIO      200u          /* unpacked / packed, per file */
#define PE_JSON_BYTES     16384u

typedef struct {
    char folder[PE_FOLDER_MAX + 1];
    char key[PE_KEY_MAX + 1];
    char bundle[PE_KEY_MAX + 2];        /* "@" + key: the skin loader asks this file */
    char name[48], group[48];
    PeCostume costume;
} PePackChar;

typedef struct {
    char id[PE_ID_MAX + 1], name[48], version[16];
    char path[1100];
    int zip;
    int nchars, refused_chars;
    char refused[96];                   /* empty: loaded */
    PePackChar ch[PE_MAX_CHARS];
} PePack;

static PePack *s_pack[PE_MAX_PACKS];
static int s_packs, s_skipped;          /* skipped: more than PE_MAX_PACKS */
static char s_dir[1100];

/* ---- reading a pack's files, from its zip or its folder ---- */

/* `name` inside the pack; *data is malloc'd. 0 if missing, empty or larger than `max`. */
static int pe_pack_file(const PePack *p, const char *name, unsigned char **data, unsigned *size, unsigned max) {
    *data = NULL;
    *size = 0;
    if (p->zip) {
        mz_zip_archive z;
        mz_zip_archive_file_stat st;
        size_t n = 0;
        int i;
        memset(&z, 0, sizeof z);
        if (!mz_zip_reader_init_file(&z, p->path, 0)) return 0;
        i = mz_zip_reader_locate_file(&z, name, NULL, 0);
        if (i >= 0 && mz_zip_reader_file_stat(&z, (mz_uint)i, &st) && st.m_uncomp_size > 0 && st.m_uncomp_size <= max)
            *data = mz_zip_reader_extract_to_heap(&z, (mz_uint)i, &n, 0);
        mz_zip_reader_end(&z);
        if (!*data) return 0;
        if (n == 0 || n > max) { mz_free(*data); *data = NULL; return 0; }
        {   /* hand back plain malloc memory, as the folder path does */
            unsigned char *copy = malloc(n);
            if (copy) memcpy(copy, *data, n);
            mz_free(*data);
            *data = copy;
        }
        *size = (unsigned)n;
        return *data != NULL;
    } else {
        char path[1300];
        FILE *f;
        long n;
        snprintf(path, sizeof path, "%s/%s", p->path, name);
        if (!(f = fopen(path, "rb"))) return 0;
        fseek(f, 0, SEEK_END);
        n = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (n <= 0 || (unsigned long)n > max || !(*data = malloc((size_t)n))) { fclose(f); return 0; }
        if (fread(*data, 1, (size_t)n, f) != (size_t)n) { free(*data); *data = NULL; fclose(f); return 0; }
        fclose(f);
        *size = (unsigned)n;
        return 1;
    }
}

static int pe_pack_exists(const PePack *p, const char *name) {
    if (p->zip) {
        mz_zip_archive z;
        int i;
        memset(&z, 0, sizeof z);
        if (!mz_zip_reader_init_file(&z, p->path, 0)) return 0;
        i = mz_zip_reader_locate_file(&z, name, NULL, 0);
        mz_zip_reader_end(&z);
        return i >= 0;
    } else {
        char path[1300];
        FILE *f;
        snprintf(path, sizeof path, "%s/%s", p->path, name);
        if (!(f = fopen(path, "rb"))) return 0;
        fclose(f);
        return 1;
    }
}

/* "@<pack id>/<folder>" to its pack and character. */
static const PePack *pe_ref(const char *ref, const PePackChar **out) {
    int i, j;
    if (!ref || ref[0] != '@') return NULL;
    for (i = 0; i < s_packs; ++i) {
        const PePack *p = s_pack[i];
        if (p->refused[0]) continue;
        for (j = 0; j < p->nchars; ++j)
            if (strcmp(p->ch[j].bundle, ref) == 0) { *out = &p->ch[j]; return p; }
    }
    return NULL;
}

int pe_pack_read(const char *ref, const char *name, unsigned char **data, unsigned *size, unsigned max) {
    const PePackChar *c;
    const PePack *p = pe_ref(ref, &c);
    char rel[160];
    *data = NULL;
    *size = 0;
    if (!p) return 0;
    snprintf(rel, sizeof rel, "characters/%s/%s", c->folder, name);
    return pe_pack_file(p, rel, data, size, max);
}

int pe_pack_has(const char *ref, const char *name) {
    const PePackChar *c;
    const PePack *p = pe_ref(ref, &c);
    char rel[160];
    if (!p) return 0;
    snprintf(rel, sizeof rel, "characters/%s/%s", c->folder, name);
    return pe_pack_exists(p, rel);
}

/* ---- checks ---- */

static int pe_valid_id(const char *id) {
    size_t n = strlen(id), i;
    if (!n || n > PE_ID_MAX || id[0] == '-') return 0;
    for (i = 0; i < n; ++i)
        if (!((id[i] >= 'a' && id[i] <= 'z') || (id[i] >= '0' && id[i] <= '9') || id[i] == '-')) return 0;
    return strcmp(id, "pe1") != 0 && strcmp(id, "local") != 0;   /* the game's own and pe_skins */
}

/* claire.re2, claire.re2$biker: letters, digits, . _ - $ */
static int pe_valid_folder(const char *f) {
    size_t n = strlen(f), i;
    if (!n || n > PE_FOLDER_MAX || f[0] == '.') return 0;
    for (i = 0; i < n; ++i) {
        const char c = f[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
              c == '-' || c == '$')) return 0;
    }
    return 1;
}

/* A zip entry name a pack may hold: relative, forward slashes, no "..", no drive or stream. */
static int pe_safe_name(const char *name) {
    const char *seg = name;
    if (!*name || name[0] == '/' || strchr(name, '\\') || strchr(name, ':')) return 0;
    while (*seg) {
        const char *end = strchr(seg, '/');
        const size_t len = end ? (size_t)(end - seg) : strlen(seg);
        if ((len == 2 && seg[0] == '.' && seg[1] == '.') || (len == 1 && seg[0] == '.')) return 0;
        if (!end) break;
        seg = end + 1;
    }
    return 1;
}

/* The zip as a whole: every entry safe, no duplicates, no encryption or links, within the limits. */
static int pe_check_zip(const char *path, char *why, size_t size) {
    mz_zip_archive z;
    mz_uint n, i, j;
    unsigned long long total = 0;
    int ok = 1;
    memset(&z, 0, sizeof z);
    if (!mz_zip_reader_init_file(&z, path, 0)) { snprintf(why, size, "not a readable zip"); return 0; }
    n = mz_zip_reader_get_num_files(&z);
    if (n > PE_MAX_ENTRIES) { snprintf(why, size, "too many files (%u)", n); ok = 0; }
    for (i = 0; ok && i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&z, i, &st)) { snprintf(why, size, "unreadable entry %u", i); ok = 0; break; }
        if (!pe_safe_name(st.m_filename)) { snprintf(why, size, "unsafe name %.40s", st.m_filename); ok = 0; break; }
        if (st.m_is_encrypted || !st.m_is_supported) { snprintf(why, size, "encrypted or unsupported %.40s", st.m_filename); ok = 0; break; }
        if (((st.m_external_attr >> 16) & 0xF000u) == 0xA000u) { snprintf(why, size, "link %.40s", st.m_filename); ok = 0; break; }
        if (st.m_is_directory) continue;
        if (st.m_uncomp_size > PE_FILE_BYTES) { snprintf(why, size, "%.40s is over 4 MiB", st.m_filename); ok = 0; break; }
        if (st.m_comp_size && st.m_uncomp_size / st.m_comp_size > PE_MAX_RATIO) {
            snprintf(why, size, "%.40s compresses suspiciously well", st.m_filename); ok = 0; break;
        }
        total += st.m_uncomp_size;
        if (total > PE_PACK_BYTES) { snprintf(why, size, "over 64 MiB unpacked"); ok = 0; break; }
        for (j = 0; j < i; ++j) {
            char other[512];
            if (mz_zip_reader_get_filename(&z, j, other, sizeof other) && strcmp(other, st.m_filename) == 0) {
                snprintf(why, size, "%.40s appears twice", st.m_filename); ok = 0; break;
            }
        }
    }
    mz_zip_reader_end(&z);
    return ok;
}

static const char *pe_json_str(const cJSON *o, const char *k) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : NULL;
}

static cJSON *pe_json(const PePack *p, const char *name) {
    unsigned char *data;
    unsigned size;
    cJSON *j;
    if (!pe_pack_file(p, name, &data, &size, PE_JSON_BYTES)) return NULL;
    j = cJSON_ParseWithLength((const char *)data, size);
    free(data);
    return j;
}

/* The kept curve, by tier (D8 §3.3, §4.3); 0 for an unknown tier. */
static unsigned pe_tier_price(const char *tier) {
    static const char *const names[] = {"extra", "outfit", "supporting", "main", "special"};
    static const unsigned price[] = {25, 50, 100, 250, 400};
    unsigned i;
    for (i = 0; tier && i < 5; ++i) if (strcmp(tier, names[i]) == 0) return price[i];
    return 0;
}

static const char *const s_required[] = {"texture.bin", "model.bin", "day1.bin", "day2.bin", "day3.bin", "day4.bin",
                                         "day5.bin", "day6.bin", "day7.bin", "day8.bin"};

/* The character's files read and checked for Aya's shape; 0 with the reason if not. */
static int pe_check_char(const PePack *p, const char *folder, char *why, size_t size) {
    static const char *const optional[2] = {"rooms.bin", "meta.bin"};
    unsigned char *data[12] = {0};
    unsigned bytes[12] = {0}, i;
    char rel[160];
    PeSkinFiles f;
    int ok = 1;
    for (i = 0; ok && i < 12; ++i) {
        const char *name = i < 10 ? s_required[i] : optional[i - 10];
        snprintf(rel, sizeof rel, "characters/%s/%s", folder, name);
        if (!pe_pack_file(p, rel, &data[i], &bytes[i], PE_FILE_BYTES) && i < 10) { snprintf(why, size, "%s unreadable", name); ok = 0; }
    }
    if (ok) {
        memset(&f, 0, sizeof f);
        f.texture = data[0]; f.texture_size = bytes[0];
        f.model = data[1]; f.model_size = bytes[1];
        for (i = 0; i < 8; ++i) { f.day[i] = data[2 + i]; f.day_size[i] = bytes[2 + i]; }
        f.rooms = data[10]; f.rooms_size = bytes[10];
        f.meta = data[11]; f.meta_size = bytes[11];
        ok = pe_model_check_skin(&f, why, size);
    }
    for (i = 0; i < 12; ++i) free(data[i]);
    return ok;
}

/* One character: its character.json, every required file, and their shape. */
static int pe_read_char(PePack *p, const char *folder) {
    PePackChar *c;
    char rel[160];
    cJSON *j;
    const char *name, *group, *tier;
    unsigned price, i;
    if (p->nchars == PE_MAX_CHARS) { ++p->refused_chars; return 0; }
    if (!pe_valid_folder(folder) || strlen(p->id) + 1 + strlen(folder) > PE_KEY_MAX) {
        pe_mod_log("pe_wardrobe", "pack %s: character %s refused (folder name)", p->id, folder);
        ++p->refused_chars;
        return 0;
    }
    snprintf(rel, sizeof rel, "characters/%s/character.json", folder);
    j = pe_json(p, rel);
    name = j ? pe_json_str(j, "name") : NULL;
    group = j ? pe_json_str(j, "group") : NULL;
    tier = j ? pe_json_str(j, "tier") : NULL;
    price = pe_tier_price(tier);
    if (!name || !*name || !price) {
        pe_mod_log("pe_wardrobe", "pack %s: character %s refused (character.json: name and tier)", p->id, folder);
        ++p->refused_chars;
        cJSON_Delete(j);
        return 0;
    }
    for (i = 0; i < sizeof s_required / sizeof s_required[0]; ++i) {
        snprintf(rel, sizeof rel, "characters/%s/%s", folder, s_required[i]);
        if (!pe_pack_exists(p, rel)) {
            pe_mod_log("pe_wardrobe", "pack %s: character %s refused (%s missing)", p->id, folder, s_required[i]);
            ++p->refused_chars;
            cJSON_Delete(j);
            return 0;
        }
    }
    {
        char why[96];
        if (!pe_check_char(p, folder, why, sizeof why)) {
            pe_mod_log("pe_wardrobe", "pack %s: character %s refused (%s)", p->id, folder, why);
            ++p->refused_chars;
            cJSON_Delete(j);
            return 0;
        }
    }
    c = &p->ch[p->nchars++];
    memset(c, 0, sizeof *c);
    snprintf(c->folder, sizeof c->folder, "%s", folder);
    snprintf(c->key, sizeof c->key, "%s/%s", p->id, folder);
    snprintf(c->bundle, sizeof c->bundle, "@%s", c->key);
    snprintf(c->name, sizeof c->name, "%s", name);
    snprintf(c->group, sizeof c->group, "%s", group && *group ? group : p->name);
    c->costume = (PeCostume){ c->key, c->name, c->group, (uint16_t)price, -1, NULL, NULL, c->bundle };
    cJSON_Delete(j);
    return 1;
}

/* The pack's character folders, in name order (the registry keeps that order). */
static int pe_cmp(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }
static int pe_cmp_ptr(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void pe_read_chars(PePack *p) {
    static char names[PE_MAX_CHARS * 2][PE_FOLDER_MAX + 8];
    int n = 0, i;
    if (p->zip) {
        mz_zip_archive z;
        mz_uint count, k;
        memset(&z, 0, sizeof z);
        if (!mz_zip_reader_init_file(&z, p->path, 0)) return;
        count = mz_zip_reader_get_num_files(&z);
        for (k = 0; k < count && n < PE_MAX_CHARS * 2; ++k) {
            char file[512], *rest, *slash;
            if (!mz_zip_reader_get_filename(&z, k, file, sizeof file) || strncmp(file, "characters/", 11) != 0) continue;
            rest = file + 11;
            if (!(slash = strchr(rest, '/')) || strcmp(slash + 1, "character.json") != 0) continue;
            *slash = 0;
            if (strlen(rest) < sizeof names[0]) snprintf(names[n++], sizeof names[0], "%s", rest);
        }
        mz_zip_reader_end(&z);
    } else {
        char path[1200];
        char **list;
        int count;
        snprintf(path, sizeof path, "%s/characters", p->path);
        if ((list = SDL_GlobDirectory(path, "*", 0, &count))) {
            for (i = 0; i < count && n < PE_MAX_CHARS * 2; ++i)
                if (strlen(list[i]) < sizeof names[0]) snprintf(names[n++], sizeof names[0], "%s", list[i]);
            SDL_free(list);
        }
    }
    qsort(names, (size_t)n, sizeof names[0], pe_cmp);
    for (i = 0; i < n; ++i) pe_read_char(p, names[i]);
}

/* One entry of packs/: a .zip or a folder. */
static void pe_read_pack(const char *entry, int zip) {
    PePack *p;
    cJSON *j;
    const cJSON *format, *minw;
    const char *id, *kind;
    if (s_packs == PE_MAX_PACKS) { ++s_skipped; return; }
    if (!(p = calloc(1, sizeof *p))) return;
    s_pack[s_packs++] = p;
    snprintf(p->path, sizeof p->path, "%s/%s", s_dir, entry);
    p->zip = zip;
    snprintf(p->name, sizeof p->name, "%s", entry);
    if (zip && !pe_check_zip(p->path, p->refused, sizeof p->refused)) return;
    if (!(j = pe_json(p, "pack.json"))) { snprintf(p->refused, sizeof p->refused, "no readable pack.json"); return; }
    format = cJSON_GetObjectItemCaseSensitive(j, "format");
    minw = cJSON_GetObjectItemCaseSensitive(j, "min_wardrobe");
    id = pe_json_str(j, "id");
    kind = pe_json_str(j, "kind");
    if (pe_json_str(j, "name")) snprintf(p->name, sizeof p->name, "%s", pe_json_str(j, "name"));
    if (pe_json_str(j, "version")) snprintf(p->version, sizeof p->version, "%s", pe_json_str(j, "version"));
    if (id) snprintf(p->id, sizeof p->id, "%.*s", PE_ID_MAX, id);
    if (!cJSON_IsNumber(format) || format->valueint != 1) snprintf(p->refused, sizeof p->refused, "pack format isn't 1");
    else if (!id || !pe_valid_id(id)) snprintf(p->refused, sizeof p->refused, "bad id (a-z, 0-9, -; at most 31)");
    else if (cJSON_IsNumber(minw) && minw->valueint > 1) snprintf(p->refused, sizeof p->refused, "needs a newer Wardrobe");
    else if (kind && strcmp(kind, "assets") != 0)
        snprintf(p->refused, sizeof p->refused, strcmp(kind, "recipe") == 0
                 ? "recipe: build it from your disc with pe_pack_build.py" : "unknown kind");
    else if (!pe_pack_exists(p, "LICENSE")) snprintf(p->refused, sizeof p->refused, "no LICENSE");
    cJSON_Delete(j);
    if (!p->refused[0]) pe_read_chars(p);
}

/* ---- the scan, at start-up ---- */
static int pe_ends_zip(const char *s) {
    const size_t n = strlen(s);
    return n > 4 && SDL_strcasecmp(s + n - 4, ".zip") == 0;
}

void pe_packs_scan(void) {
    char **list;
    int count, i, k;
    while (s_packs > 0) free(s_pack[--s_packs]);
    s_skipped = 0;
    if (pe_mod_path(s_dir, sizeof s_dir, PE_PACKS_DIR) != 0) return;
    if (!(list = SDL_GlobDirectory(s_dir, "*", 0, &count))) return;
    qsort(list, (size_t)count, sizeof *list, pe_cmp_ptr);          /* the same order on every system */
    for (i = 0; i < count; ++i) {
        char path[1300];
        SDL_PathInfo info;
        if (list[i][0] == '.') continue;
        snprintf(path, sizeof path, "%s/%s", s_dir, list[i]);
        if (!SDL_GetPathInfo(path, &info)) continue;
        if (info.type == SDL_PATHTYPE_FILE && pe_ends_zip(list[i])) pe_read_pack(list[i], 1);
        else if (info.type == SDL_PATHTYPE_DIRECTORY) pe_read_pack(list[i], 0);
    }
    SDL_free(list);
    /* The same id twice: every copy refused. */
    for (i = 0; i < s_packs; ++i)
        for (k = 0; k < s_packs; ++k)
            if (i != k && s_pack[i]->id[0] && strcmp(s_pack[i]->id, s_pack[k]->id) == 0)
                snprintf(s_pack[i]->refused, sizeof s_pack[i]->refused, "id %s installed twice: remove one copy", s_pack[i]->id);
    for (i = 0; i < s_packs; ++i)
        pe_mod_log("pe_wardrobe", "pack %s (%s): %s%s", s_pack[i]->name, s_pack[i]->id[0] ? s_pack[i]->id : "?",
                   s_pack[i]->refused[0] ? "refused: " : "loaded", s_pack[i]->refused);
}

void pe_packs_register(void) {
    int i, j;
    for (i = 0; i < s_packs; ++i) {
        PePack *p = s_pack[i];
        if (p->refused[0]) continue;
        for (j = 0; j < p->nchars; ++j)
            if (g_pe_costumes_api.add(&p->ch[j].costume) < 0) ++p->refused_chars;   /* a taken key or a full registry */
    }
}

/* ---- F1 > Collection > Wardrobe > Packs ---- */
static int s_sel;
static char s_line[160];

static unsigned pe_pack_count(void) { return s_packs ? (unsigned)s_packs : 1u; }
static const char *pe_pack_name(unsigned i) {
    if (!s_packs) return "None installed";
    if (i >= (unsigned)s_packs) i = 0;
    snprintf(s_line, sizeof s_line, "%s %s  (%u/%d)", s_pack[i]->name, s_pack[i]->version, i + 1, s_packs);
    return s_line;
}
static void pe_pack_status(char *out, unsigned size) {
    const PePack *p = s_packs && s_sel < s_packs ? s_pack[s_sel] : NULL;
    if (!p) snprintf(out, size, "Drop pack .zip files or folders in the folder below, then restart");
    else if (p->refused[0]) snprintf(out, size, "Refused: %s", p->refused);
    else if (p->refused_chars) snprintf(out, size, "%d characters; %d refused (see pe_mods.log)", p->nchars, p->refused_chars);
    else snprintf(out, size, "%d characters loaded (%s)", p->nchars, p->id);
}
static void pe_pack_folder(char *out, unsigned size) {
    snprintf(out, size, "%s%s", s_dir[0] ? s_dir : PE_PACKS_DIR, s_skipped ? "  (more than 16 packs: some not read)" : "");
}

void pe_packs_page_register(void) {
    pe_menu_register_dynamic_choice(PE_PAGE_WARDROBE_PACKS, "Pack", "Packs found at start-up.", &s_sel, PE_MAX_PACKS,
                                    pe_pack_count, pe_pack_name);
    pe_menu_register_info(PE_PAGE_WARDROBE_PACKS, "Status", "Loaded, or why it was refused.", pe_pack_status);
    pe_menu_register_info(PE_PAGE_WARDROBE_PACKS, "Folder", "Where packs go. Changes apply the next time the game starts.",
                          pe_pack_folder);
}
