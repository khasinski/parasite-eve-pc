/* Registration: source-game mods add their readers and naming tables at init. Nothing here
 * knows a game by name; a new game is a new mod. */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "pe_imp.h"
#include "pe_imp_internal.h"

#define MAX_READERS 128
#define MAX_NAMING 64

static const PeImpReader *s_reader[MAX_READERS];
static int s_readers;
static struct { char game[16]; const char *json; PjNode *root; } s_naming[MAX_NAMING];
static int s_namings;

const char *pe_imp_type_name(uint32_t type) {
    switch (type) {
    case PE_IMP_MODEL: return "Model";
    case PE_IMP_ANIMATION: return "Animation";
    case PE_IMP_IMAGESET: return "ImageSet";
    case PE_IMP_SOUND: return "Sound";
    case PE_IMP_SONG: return "Song";
    case PE_IMP_TEXTSET: return "TextSet";
    default: return "?";
    }
}

static int word_ok(const char *s, size_t max) {
    if (!s || !*s || strlen(s) > max) return 0;
    for (; *s; s++)
        if (!(islower((unsigned char)*s) || isdigit((unsigned char)*s) || *s == '-' || *s == '_')) return 0;
    return 1;
}

int pe_imp_add_reader(const PeImpReader *r) {
    if (!r) return -1;
    int paired = r->companion || r->find_companion;
    if (r->api.version != PE_IMP_API_VERSION || r->api.size != sizeof *r || !r->match ||
        (r->companion && r->find_companion) || (r->read ? (paired || r->read_pair) : (!paired || !r->read_pair)) ||
        !word_ok(r->game, 15) || !word_ok(r->platform, 15) || !word_ok(r->format, 15) || !r->produces)
        return -1;
    for (int i = 0; i < s_readers; i++)
        if (s_reader[i] == r || (!strcmp(s_reader[i]->game, r->game) && !strcmp(s_reader[i]->platform, r->platform) &&
                                 !strcmp(s_reader[i]->format, r->format)))
            return -1;
    if (s_readers == MAX_READERS) return -1;
    s_reader[s_readers++] = r;
    return 0;
}

int pe_imp_readers(void) { return s_readers; }
const PeImpReader *pe_imp_reader(int i) { return i >= 0 && i < s_readers ? s_reader[i] : NULL; }

/* '?' one character, '*' any run within one folder; case-insensitive. */
int pe_imp_match(const char *p, const char *s) {
    for (; *p; p++, s++) {
        if (*p == '*') {
            for (const char *t = s;; t++) {
                if (pe_imp_match(p + 1, t)) return 1;
                if (!*t || *t == '/') return 0;
            }
        }
        if (!*s || (*p == '?' ? *s == '/' : toupper((unsigned char)*p) != toupper((unsigned char)*s))) return 0;
    }
    return !*s;
}

int pe_imp_set_naming(const char *game, const char *json) {
    if (!word_ok(game, 15) || !json || s_namings == MAX_NAMING) return -1;
    char why[160];
    PjNode *root = pj_parse(json, strlen(json), why, sizeof why);
    if (!root || root->kind != PJ_OBJECT) { pj_free(root); return -1; }
    for (int i = 0; i < s_namings; i++)
        if (!strcmp(s_naming[i].game, game)) { pj_free(s_naming[i].root); s_naming[i].root = root; s_naming[i].json = json; return 0; }
    snprintf(s_naming[s_namings].game, sizeof s_naming[0].game, "%s", game);
    s_naming[s_namings].json = json, s_naming[s_namings].root = root;
    s_namings++;
    return 0;
}

static uint32_t disc_id_hash(uint32_t h);

uint32_t pe_imp_naming_hash(void) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < s_namings; i++)
        for (int part = 0; part < 2; part++) {
            for (const char *c = part ? s_naming[i].json : s_naming[i].game; *c; c++) h = (h ^ (uint8_t)*c) * 16777619u;
            h = (h ^ 0xffu) * 16777619u;
        }
    return disc_id_hash(h);
}

/* The naming table's {id, name} for a proposed ID, if any. */
void pe_imp_rename(const char *game, const char *proposed, const char **id, const char **name) {
    *id = proposed;
    for (int i = 0; i < s_namings; i++) {
        if (strcmp(s_naming[i].game, game)) continue;
        const PjNode *e = pj_get(s_naming[i].root, proposed);
        if (e) {
            *id = pj_str(pj_get(e, "id"), proposed);
            const char *nm = pj_str(pj_get(e, "name"), NULL);
            if (nm) *name = nm;
        }
    }
}

/* ---- source games, rigs, builder targets ---- */
#define MAX_GAMES 32
#define MAX_RIGS 64
#define MAX_TARGETS 32
#define MAX_DISC_IDS 8
static struct { char game[16], title[48], group[48]; const char *disc_id[MAX_DISC_IDS]; int disc_ids; } s_game[MAX_GAMES];
static struct { char game[16], prefix[48]; const struct PeImpRig *rig; } s_rig[MAX_RIGS];
static const PeImpTarget *s_target[MAX_TARGETS];
static int s_games, s_rigs, s_targets;

int pe_imp_add_game(const char *game, const char *title, const char *group) {
    if (!word_ok(game, 15) || !title || !group || s_games == MAX_GAMES) return -1;
    for (int i = 0; i < s_games; i++) if (!strcmp(s_game[i].game, game)) return -1;
    snprintf(s_game[s_games].game, sizeof s_game[0].game, "%s", game);
    snprintf(s_game[s_games].title, sizeof s_game[0].title, "%s", title);
    snprintf(s_game[s_games].group, sizeof s_game[0].group, "%s", group);
    s_game[s_games].disc_ids = 0;
    s_games++;
    return 0;
}

int pe_imp_add_disc_id(const char *game, const char *pattern) {
    for (int i = 0; i < s_games; i++)
        if (!strcmp(s_game[i].game, game)) {
            if (!pattern || !*pattern || s_game[i].disc_ids == MAX_DISC_IDS) return -1;
            s_game[i].disc_id[s_game[i].disc_ids++] = pattern;
            return 0;
        }
    return -1;                                       /* add the game first */
}

static uint32_t disc_id_hash(uint32_t h) {
    for (int g = 0; g < s_games; g++)
        for (int i = 0; i < s_game[g].disc_ids; i++) {
            for (const char *c = s_game[g].disc_id[i]; *c; c++) h = (h ^ (uint8_t)*c) * 16777619u;
            h = (h ^ 0xfeu) * 16777619u;
        }
    return h;
}

int pe_imp_disc_id_count(const char *game) {
    for (int i = 0; i < s_games; i++) if (!strcmp(s_game[i].game, game)) return s_game[i].disc_ids;
    return 0;
}

const char *pe_imp_disc_id(const char *game, int k) {
    for (int i = 0; i < s_games; i++)
        if (!strcmp(s_game[i].game, game)) return k >= 0 && k < s_game[i].disc_ids ? s_game[i].disc_id[k] : NULL;
    return NULL;
}

const char *pe_imp_game_title(const char *game) {
    for (int i = 0; i < s_games; i++) if (!strcmp(s_game[i].game, game)) return s_game[i].title;
    return game;
}

const char *pe_imp_game_group(const char *game) {
    for (int i = 0; i < s_games; i++) if (!strcmp(s_game[i].game, game)) return s_game[i].group;
    return "Imported";
}

int pe_imp_add_rig(const char *game, const char *id_prefix, const struct PeImpRig *rig) {
    if (!word_ok(game, 15) || !id_prefix || strlen(id_prefix) >= sizeof s_rig[0].prefix || !rig || s_rigs == MAX_RIGS) return -1;
    snprintf(s_rig[s_rigs].game, sizeof s_rig[0].game, "%s", game);
    snprintf(s_rig[s_rigs].prefix, sizeof s_rig[0].prefix, "%s", id_prefix);
    s_rig[s_rigs++].rig = rig;
    return 0;
}

const struct PeImpRig *pe_imp_rig_for(const char *asset_id) {
    const char *colon = strchr(asset_id, ':');
    if (!colon) return NULL;
    size_t gl = (size_t)(colon - asset_id);
    for (int i = 0; i < s_rigs; i++)
        if (strlen(s_rig[i].game) == gl && !strncmp(s_rig[i].game, asset_id, gl) &&
            !strncmp(colon + 1, s_rig[i].prefix, strlen(s_rig[i].prefix)))
            return s_rig[i].rig;
    return NULL;
}

int pe_imp_add_target(const PeImpTarget *t) {
    if (!t || t->api.version != PE_IMP_API_VERSION || t->api.size != sizeof *t || !t->build || !t->module ||
        !t->verb || s_targets == MAX_TARGETS)
        return -1;
    for (int i = 0; i < s_targets; i++) if (s_target[i] == t || !strcmp(s_target[i]->module, t->module)) return -1;
    s_target[s_targets++] = t;
    return 0;
}

#define MAX_AUTOS 128
static const PeImpAuto *s_auto[MAX_AUTOS];
static int s_autos;

int pe_imp_add_auto(const PeImpAuto *a) {
    if (!a || !a->asset_id || !strchr(a->asset_id, ':') || !a->module || !a->disc || s_autos == MAX_AUTOS) return -1;
    for (int i = 0; i < s_autos; i++) if (!strcmp(s_auto[i]->asset_id, a->asset_id) && !strcmp(s_auto[i]->module, a->module)) return -1;
    s_auto[s_autos++] = a;
    return 0;
}
int pe_imp_autos(void) { return s_autos; }
const PeImpAuto *pe_imp_auto(int i) { return i >= 0 && i < s_autos ? s_auto[i] : NULL; }

int pe_imp_targets(void) { return s_targets; }
const PeImpTarget *pe_imp_target(int i) { return i >= 0 && i < s_targets ? s_target[i] : NULL; }

void pe_imp_registry_reset(void) {
    for (int i = 0; i < s_namings; i++) pj_free(s_naming[i].root);
    s_readers = s_namings = s_games = s_rigs = s_targets = s_autos = 0;
}
