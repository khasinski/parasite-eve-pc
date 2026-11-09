/* The Wardrobe's profile: costumes owned and the one worn, by stable key, kept across games. See
 * pe_wardrobe.h.
 *
 * Until the Wardrobe became its own mod, these records lived in pe_collection.ini beside the
 * Bestiary's. The first time pe_wardrobe.ini is missing, its records are imported once, from the
 * first of:
 *   pe_collection.ini    its costume and worn lines, if it has any (read only: the Collection owns
 *                        that file, and writes one with creatures only when it imports the .dat)
 *   pe_collection.dat    the psxrecomp build's profile, skins by index
 * and the new file records where they came from ("imported <source>", or "imported none" for a
 * fresh profile). The Collection drops its costume lines once it sees that line, so a later
 * pe_wardrobe.ini lost or deleted can't bring back costumes cleared since (bridge findings D9
 * SPL-02); only the recomp's .dat, if still there, is imported again. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pe_profile_io.h"

#include "pe_mod_host.h"
#include "pe_wardrobe.h"
#include "pe_services.h"

#define PE_FILE         "pe_wardrobe.ini"
#define PE_LEGACY_FILE  "pe_collection.ini"   /* the Collection's, before the split */
#define PE_V1_FILE      "pe_collection.dat"   /* the psxrecomp build's */
#define PE_MAX          512
#define PE_KEY_MAX      64
#define PE_FORMAT       1                     /* the first line, "format 1" */

static char s_owned[PE_MAX][PE_KEY_MAX];
static int s_count, s_dirty;
static unsigned s_retry_frames;
static char s_worn[PE_KEY_MAX];
static char s_imported[24];            /* where the records first came from */
static char s_staged_key[PE_KEY_MAX], s_temporary[1200];
static unsigned s_revision, s_stage_revision;
static int s_staged;
static int s_newer;                    /* written by a newer format: read, never overwritten */

static int pe_index(const char *key) {
    int i;
    if (!key || !*key) return -1;
    for (i = 0; i < s_count; ++i) if (strcmp(s_owned[i], key) == 0) return i;
    return -1;
}

int pe_ward_profile_owned(const char *key) {
    const PeProfileTransactionsApi*t=pe_service_get("profile_transactions",1);
    return pe_index(key)>=0 || (t && t->struct_size>=sizeof *t && t->pending("pe_wardrobe/ownership",key));
}

void pe_ward_profile_own(const char *key) {
    if (!key || !*key || pe_index(key) >= 0 || s_count == PE_MAX || strlen(key) >= PE_KEY_MAX) return;
    snprintf(s_owned[s_count++], PE_KEY_MAX, "%s", key);
    s_dirty = 1;
    ++s_revision;
}

int pe_ward_profile_forget(const char *key) {
    const int at = pe_index(key);
    if (at < 0) return 0;
    memmove(s_owned[at], s_owned[at + 1], (size_t)(s_count - at - 1) * sizeof s_owned[0]);
    --s_count;
    s_dirty = 1;
    ++s_revision;
    return 1;
}

const char *pe_ward_profile_worn(void) { return s_worn; }
void pe_ward_profile_set_worn(const char *key) { snprintf(s_worn, sizeof s_worn, "%s", key ? key : ""); s_dirty = 1; ++s_revision; }

void pe_ward_profile_clear(void) { s_count = 0; s_worn[0] = 0; s_dirty = 1; ++s_revision; }
void pe_ward_profile_tick(void) {
    if (s_retry_frames) { --s_retry_frames; return; }
    if (s_dirty && !s_newer && !s_staged) (void)pe_ward_profile_save();
}

/* Preparation is private to the acquisition owner. A failed stage never truncates the profile. */
int pe_ward_profile_prepare_owned(const char *key) {
    char path[1100];
    FILE *f;
    int i, ok;
    if (s_staged || s_newer || (key && (!*key || strlen(key) >= PE_KEY_MAX ||
        (pe_index(key)<0 && s_count == PE_MAX)))) return 0;
    if (pe_mod_path(path, sizeof path, PE_FILE) != 0 || !pe_profile_target_ok(path)) return 0;
    if (snprintf(s_temporary, sizeof s_temporary, "%s.tmp.%lu", path, pe_profile_process()) >= (int)sizeof s_temporary) return 0;
    if (!(f = fopen(s_temporary, "w"))) return 0;
    fprintf(f, "format %d\n", PE_FORMAT);
    fprintf(f, "# Parasite Eve Wardrobe: costumes kept across every game.\n"
               "# imported <where these first came from>; costume <key> = owned; worn <key>\n");
    fprintf(f, "imported %s\n", s_imported[0] ? s_imported : "none");
    if (s_worn[0]) fprintf(f, "worn %s\n", s_worn);
    for (i = 0; i < s_count; ++i) fprintf(f, "costume %s\n", s_owned[i]);
    if (key && pe_index(key)<0) fprintf(f, "costume %s\n", key);
    ok = pe_profile_flush(f);
    if (fclose(f)) ok = 0;
    if (!ok) { remove(s_temporary); return 0; }
    snprintf(s_staged_key, sizeof s_staged_key, "%s", key ? key : "");
    s_stage_revision = s_revision;
    s_staged = 1;
    return 1;
}

void pe_ward_profile_cancel_owned(void) {
    if (s_staged) remove(s_temporary);
    s_staged = 0;
}

int pe_ward_profile_commit_owned(void) {
    char path[1100];
    if (!s_staged) return 0;
    if (s_revision != s_stage_revision || pe_mod_path(path, sizeof path, PE_FILE) != 0 ||
        !pe_profile_replace(s_temporary, path)) { pe_ward_profile_cancel_owned(); return 0; }
    if (*s_staged_key) pe_ward_profile_own(s_staged_key);
    s_staged = 0;
    s_dirty = 0;
    return 1;
}

int pe_ward_profile_save(void) {
    int ok = pe_ward_profile_prepare_owned(NULL) && pe_ward_profile_commit_owned();
    /* Retain changes, but do not hammer a failing disk/permissions path every game frame. */
    s_retry_frames = ok ? 0 : 60;
    return ok;
}

/* Read `file`'s costume, worn and imported lines: -1 if it isn't there, else how many costume and
 * worn lines it had. */
static int pe_read(const char *file) {
    char path[1100], line[256], type[16], key[PE_KEY_MAX];
    FILE *f;
    int n = 0;
    if (pe_mod_path(path, sizeof path, file) != 0 || !(f = fopen(path, "r"))) return -1;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || sscanf(line, "%15s %63s", type, key) != 2) continue;
        if (strcmp(type, "worn") == 0) snprintf(s_worn, sizeof s_worn, "%s", key), ++n;
        else if (strcmp(type, "costume") == 0) pe_ward_profile_own(key), ++n;
        else if (strcmp(type, "imported") == 0) snprintf(s_imported, sizeof s_imported, "%s", key);
        else if (strcmp(type, "format") == 0 && atoi(key) > PE_FORMAT) s_newer = 1;
    }
    fclose(f);
    return n;
}

/* ---- the psxrecomp build's pe_collection.dat ---- */
typedef struct PeProfileV1 {
    uint32_t magic, version;
    uint64_t skins;                 /* bit i: skin i owned */
    uint8_t worn, reserved[7];
    uint8_t creature[128];          /* the Collection's */
    uint32_t checksum;
} PeProfileV1;
_Static_assert(sizeof(PeProfileV1) == 160, "the recomp's pe_collection.dat layout");

static int pe_import_v1(void) {
    char path[1100];
    PeProfileV1 v1;
    uint32_t sum = 0x5A17C0DEu;
    const char *key;
    size_t i;
    FILE *f;
    int ok;
    if (pe_mod_path(path, sizeof path, PE_V1_FILE) != 0 || !(f = fopen(path, "rb"))) return 0;
    ok = fread(&v1, 1, sizeof v1, f) == sizeof v1;
    fclose(f);
    for (i = 0; ok && i < offsetof(PeProfileV1, checksum); ++i) sum = (sum << 5) + sum + ((const uint8_t *)&v1)[i];
    if (!ok || v1.magic != 0x4C434550u || v1.version != 1u || sum != v1.checksum) return 0;
    for (i = 1; i < 64; ++i)
        if (((v1.skins >> i) & 1u) && (key = pe_pack_pe1_costume_key((int)i))) pe_ward_profile_own(key);
    if ((key = pe_pack_pe1_costume_key(v1.worn)) && v1.worn) pe_ward_profile_set_worn(key);
    return 1;
}

void pe_ward_profile_load(void) {
    pe_ward_profile_cancel_owned();
    ++s_revision;
    s_retry_frames = 0;
    s_count = 0;
    s_worn[0] = 0;
    s_imported[0] = 0;
    s_newer = 0;
    if (pe_read(PE_FILE) >= 0) {
        if (!s_imported[0]) snprintf(s_imported, sizeof s_imported, "none");
        if (s_newer) pe_mod_log("pe_wardrobe", "%s is from a newer Wardrobe: read only, never rewritten", PE_FILE);
        s_dirty = 0;
        return;
    }
    if (pe_read(PE_LEGACY_FILE) > 0) snprintf(s_imported, sizeof s_imported, "%s", PE_LEGACY_FILE);
    else if (pe_import_v1()) snprintf(s_imported, sizeof s_imported, "%s", PE_V1_FILE);
    else snprintf(s_imported, sizeof s_imported, "none");
    pe_mod_log("pe_wardrobe", "new %s: imported %s (%d costumes%s)", PE_FILE, s_imported, s_count, s_worn[0] ? ", one worn" : "");
    pe_ward_profile_save();
}

static int pe_transaction_admit(const char*key,unsigned bits){
    return bits==1 && !s_newer && key && *key && strlen(key)<PE_KEY_MAX && (pe_index(key)>=0 || s_count<PE_MAX);
}
static unsigned pe_transaction_owned(const char*key){return pe_index(key)>=0;}
static int pe_transaction_promote(const char*key,unsigned bits){
    if(!pe_transaction_admit(key,bits))return 0;
    pe_ward_profile_own(key);return pe_ward_profile_save();
}
void pe_ward_profile_transactions_init(void){
    static const PeProfileTransactionAdapter a={sizeof a,"pe_wardrobe/ownership",1,pe_transaction_admit,pe_transaction_owned,pe_transaction_promote};
    const PeProfileTransactionsApi*t=pe_service_get("profile_transactions",1);
    if(t&&t->struct_size>=sizeof *t&&!t->add(&a))pe_mod_log("pe_wardrobe","profile transaction adapter unavailable");
}
