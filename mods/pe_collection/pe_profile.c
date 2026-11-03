/* The Collection's profile: Bestiary unlocks by stable key, kept across games. See pe_collection.h. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_collection.h"
#include "pe_mod_host.h"
#include "pe_file_io.h"
#include "pe_services.h"

#define PE_FILE      "pe_collection.ini"
#define PE_V1_FILE   "pe_collection.dat"   /* the psxrecomp build's */
#define PE_WARDROBE_FILE "pe_wardrobe.ini"  /* the Wardrobe's, since its split */
#define PE_MAX       1024
#define PE_KEY_MAX   64
#define PE_FORMAT    1                      /* the first line, "format 1" */

enum { R_CREATURE, R_COSTUME };

typedef struct { uint8_t type; char key[PE_KEY_MAX]; uint8_t bits, kills, seen; } PeRecord;   /* seen: tiers looked at */

static PeRecord s_rec[PE_MAX];
static int s_count, s_dirty;
static unsigned s_retry_frames;
static char s_worn[PE_KEY_MAX];
static int s_newer;                         /* written by a newer format: read, never overwritten */

static PeRecord *pe_find(int type, const char *key, int create) {
    int i;
    if (!key || !*key) return NULL;
    for (i = 0; i < s_count; ++i)
        if (s_rec[i].type == type && strcmp(s_rec[i].key, key) == 0) return &s_rec[i];
    if (!create || s_count == PE_MAX || strlen(key) >= PE_KEY_MAX) return NULL;
    memset(&s_rec[s_count], 0, sizeof s_rec[0]);
    s_rec[s_count].type = (uint8_t)type;
    snprintf(s_rec[s_count].key, PE_KEY_MAX, "%s", key);
    return &s_rec[s_count++];
}

unsigned pe_profile_creature(const char *key, unsigned *kills) {
    const PeRecord *r = pe_find(R_CREATURE, key, 0);
    if (kills) *kills = r ? r->kills : 0;
    return r ? r->bits : 0;
}

void pe_profile_set_creature(const char *key, unsigned bits, unsigned kills) {
    PeRecord *r = pe_find(R_CREATURE, key, 1);
    if (!r) return;
    r->bits = (uint8_t)(bits & 7u);
    r->kills = (uint8_t)(kills > 255 ? 255 : kills);
    r->seen &= r->bits;
    s_dirty = 1;
}

unsigned pe_profile_creature_seen(const char *key) {
    const PeRecord *r = pe_find(R_CREATURE, key, 0);
    return r ? r->seen : 0;
}

void pe_profile_set_creature_seen(const char *key, unsigned seen) {
    PeRecord *r = pe_find(R_CREATURE, key, 0);
    if (!r || r->seen == (seen & r->bits)) return;
    r->seen = (uint8_t)(seen & r->bits);
    s_dirty = 1;
}

/* Has the Wardrobe imported this file's costume lines? Its profile says so once it has. */
static int pe_wardrobe_imported(void) {
    char path[1100], line[256];
    FILE *f;
    int yes = 0;
    if (pe_mod_path(path, sizeof path, PE_WARDROBE_FILE) != 0 || !(f = fopen(path, "r"))) return 0;
    while (!yes && fgets(line, sizeof line, f)) yes = strncmp(line, "imported ", 9) == 0;
    fclose(f);
    return yes;
}

/* The Bestiary's records only: costume lines still waiting for the Wardrobe stay. */
void pe_profile_clear(void) {
    int i, n = 0;
    for (i = 0; i < s_count; ++i) if (s_rec[i].type != R_CREATURE) s_rec[n++] = s_rec[i];
    s_count = n;
    s_dirty = 1;
}
void pe_profile_changed(void) { s_dirty = 1; }
void pe_profile_tick(void) { if(s_retry_frames){--s_retry_frames;return;} if (s_dirty) pe_profile_save(); }

static int pe_profile_save_checked(void) {
    char path[1100],temp[1200];
    FILE *f;
    int i;
    const int carry = !pe_wardrobe_imported();     /* costume lines the Wardrobe hasn't taken yet */
    if (s_newer) return 0;
    if (pe_mod_path(path,sizeof path,PE_FILE)!=0 || !pe_profile_target_ok(path))return 0;
    if(snprintf(temp,sizeof temp,"%s.tmp.%lu",path,pe_profile_process())>=(int)sizeof temp)return 0;
    if(!(f=fopen(temp,"w")))return 0;
    fprintf(f, "format %d\n", PE_FORMAT);
    fprintf(f, "# Parasite Eve Collection: Bestiary unlocks kept across every game.\n"
               "# creature <key> <bits: 1 met, 2 weaknesses, 4 loot> <kills> <bits looked at>\n");
    if (carry && s_worn[0]) fprintf(f, "worn %s\n", s_worn);
    for (i = 0; i < s_count; ++i) {
        if (s_rec[i].type == R_CREATURE) fprintf(f, "creature %s %u %u %u\n", s_rec[i].key, s_rec[i].bits, s_rec[i].kills, s_rec[i].seen);
        else if (carry) fprintf(f, "costume %s\n", s_rec[i].key);
    }
    int ok=pe_profile_flush(f);if(fclose(f))ok=0;
    if(!ok||!pe_profile_replace(temp,path)){remove(temp);return 0;}
    s_dirty=0;return 1;
}

void pe_profile_save(void){s_retry_frames=pe_profile_save_checked()?0:60;}

/* ---- the psxrecomp build's pe_collection.dat, imported once ---- */
typedef struct PeProfileV1 {
    uint32_t magic, version;
    uint64_t skins;                 /* the Wardrobe's */
    uint8_t worn, reserved[7];
    uint8_t creature[128];          /* bits 0-2, kills in 3-7 */
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
    for (i = 0; i < 128; ++i)
        if (v1.creature[i] && (key = pe_pack_pe1_creature_key((int)i)))
            pe_profile_set_creature(key, v1.creature[i] & 7u, v1.creature[i] >> 3);
    pe_mod_log("pe_collection", "imported the recomp's %s", PE_V1_FILE);
    return 1;
}

void pe_profile_load(void) {
    char path[1100], line[256], type[16], key[PE_KEY_MAX];
    unsigned bits, kills, seen;
    FILE *f;
    s_count = 0;
    s_worn[0] = 0;
    s_newer = 0;
    if (pe_mod_path(path, sizeof path, PE_FILE) != 0 || !(f = fopen(path, "r"))) {
        if (pe_import_v1()) pe_profile_save();
        s_dirty = 0;
        return;
    }
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || sscanf(line, "%15s %63s", type, key) != 2) continue;
        if (strcmp(type, "format") == 0 && atoi(key) > PE_FORMAT) s_newer = 1;
        else if (strcmp(type, "worn") == 0) snprintf(s_worn, sizeof s_worn, "%s", key);
        else if (strcmp(type, "costume") == 0) (void)pe_find(R_COSTUME, key, 1);
        else if (strcmp(type, "creature") == 0 && sscanf(line, "%*s %*s %u %u", &bits, &kills) == 2) {
            PeRecord *r = pe_find(R_CREATURE, key, 1);
            if (sscanf(line, "%*s %*s %*u %*u %u", &seen) != 1) seen = bits;   /* older files: nothing new */
            if (r) {
                r->bits = (uint8_t)(bits & 7u);
                r->kills = (uint8_t)(kills > 255 ? 255 : kills);
                r->seen = (uint8_t)(seen & r->bits);
            }
        }
    }
    fclose(f);
    s_dirty = 0;
}

static int pe_transaction_admit(const char*key,unsigned bits){
    return bits && !(bits&~7u) && !s_newer && key && *key && strlen(key)<PE_KEY_MAX && (pe_find(R_CREATURE,key,0)||s_count<PE_MAX);
}
static unsigned pe_transaction_owned(const char*key){return pe_profile_creature(key,NULL);}
static int pe_transaction_promote(const char*key,unsigned bits){
    if(!pe_transaction_admit(key,bits))return 0;unsigned kills;
    unsigned old=pe_profile_creature(key,&kills);pe_profile_set_creature(key,old|bits,kills);return pe_profile_save_checked();
}
void pe_profile_transactions_init(void){
    static const PeProfileTransactionAdapter a={sizeof a,"pe_collection/knowledge",7,pe_transaction_admit,pe_transaction_owned,pe_transaction_promote};
    const PeProfileTransactionsApi*t=pe_service_get("profile_transactions",1);
    if(t&&t->struct_size>=sizeof *t&&!t->add(&a))pe_mod_log("pe_collection","profile transaction adapter unavailable");
}
