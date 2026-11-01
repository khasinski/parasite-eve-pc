/* The Collection's profile: unlocks by stable key, kept across games. See pe_collection.h. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "pe_collection.h"
#include "pe_mod_host.h"

#define PE_FILE      "pe_collection.ini"
#define PE_V1_FILE   "pe_collection.dat"   /* the psxrecomp build's */
#define PE_MAX       1024
#define PE_KEY_MAX   64

enum { R_CREATURE, R_COSTUME };

typedef struct { uint8_t type; char key[PE_KEY_MAX]; uint8_t bits, kills, seen; } PeRecord;   /* seen: tiers looked at */

static PeRecord s_rec[PE_MAX];
static int s_count, s_dirty;
static char s_worn[PE_KEY_MAX];

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

int pe_profile_owned(const char *key) { return pe_find(R_COSTUME, key, 0) != NULL; }
void pe_profile_own(const char *key) { if (pe_find(R_COSTUME, key, 1)) s_dirty = 1; }
/* Forget one costume only: Bestiary and unloaded packs' records stay intact. */
int pe_profile_forget_costume(const char *key) {
    PeRecord *r = pe_find(R_COSTUME, key, 0);
    if (!r) return 0;
    int at = (int)(r - s_rec);
    memmove(r, r + 1, (size_t)(s_count - at - 1) * sizeof *r);
    --s_count;
    s_dirty = 1;
    return 1;
}
const char *pe_profile_worn(void) { return s_worn; }
void pe_profile_set_worn(const char *key) { snprintf(s_worn, sizeof s_worn, "%s", key ? key : ""); s_dirty = 1; }

void pe_profile_clear(void) { s_count = 0; s_worn[0] = 0; s_dirty = 1; }
void pe_profile_changed(void) { s_dirty = 1; }
void pe_profile_tick(void) { if (s_dirty) pe_profile_save(); }

void pe_profile_save(void) {
    char path[1100];
    FILE *f;
    int i;
    s_dirty = 0;
    if (pe_mod_path(path, sizeof path, PE_FILE) != 0 || !(f = fopen(path, "w"))) return;
    fprintf(f, "# Parasite Eve Collection: unlocks kept across every game.\n"
               "# creature <key> <bits: 1 met, 2 weaknesses, 4 loot> <kills> <bits looked at>; costume <key> = owned; worn <key>\n");
    if (s_worn[0]) fprintf(f, "worn %s\n", s_worn);
    for (i = 0; i < s_count; ++i) {
        if (s_rec[i].type == R_CREATURE) fprintf(f, "creature %s %u %u %u\n", s_rec[i].key, s_rec[i].bits, s_rec[i].kills, s_rec[i].seen);
        else fprintf(f, "costume %s\n", s_rec[i].key);
    }
    fclose(f);
}

/* ---- the psxrecomp build's pe_collection.dat, imported once ---- */
typedef struct PeProfileV1 {
    uint32_t magic, version;
    uint64_t skins;                 /* bit i: skin i owned */
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
    for (i = 1; i < 64; ++i)
        if (((v1.skins >> i) & 1u) && (key = pe_pack_pe1_costume_key((int)i))) pe_profile_own(key);
    if ((key = pe_pack_pe1_costume_key(v1.worn)) && v1.worn) pe_profile_set_worn(key);
    pe_mod_log("pe_collection", "imported the recomp's %s", PE_V1_FILE);
    return 1;
}

void pe_profile_load(void) {
    char path[1100], line[256], type[16], key[PE_KEY_MAX];
    unsigned bits, kills, seen;
    FILE *f;
    s_count = 0;
    s_worn[0] = 0;
    if (pe_mod_path(path, sizeof path, PE_FILE) != 0 || !(f = fopen(path, "r"))) {
        if (pe_import_v1()) pe_profile_save();
        s_dirty = 0;
        return;
    }
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || sscanf(line, "%15s %63s", type, key) != 2) continue;
        if (strcmp(type, "worn") == 0) snprintf(s_worn, sizeof s_worn, "%s", key);
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

#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe_checkpoint_collection.h"
/* Unlocks and purchases belong to the global account profile, not the room snapshot.
 * Keep progress/dirty state; never run profile callbacks or write a file here. The
 * worn selection must be identical, as a different outfit changes resource owners. */
int pe_profile_checkpoint_walk(const unsigned char*in,unsigned char*out,int apply){(void)apply;if(!memchr(s_worn,0,sizeof s_worn))return 0;unsigned char canonical[64]={0};memcpy(canonical,s_worn,strlen(s_worn));if(in&&memcmp(in,canonical,64))return 0;if(out)memcpy(out,canonical,64);return 1;}
#endif
