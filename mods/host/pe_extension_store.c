#include <stdio.h>
#include <string.h>
#include <stdint.h>
#ifdef _WIN32
#include <direct.h>
#define PE_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define PE_MKDIR(p) mkdir(p, 0700)
#endif
#include "pe_file_io.h"
#include "pe_mod_host.h"
#include "pe_extension_store.h"
#include "pe_profile_transactions.h"
#include "pe_save.h"
#include "pe_save_emission.h"
#define HEADER 96u
#define DIRECTORY 64u
typedef struct {
    char key[48];
    uint16_t version, min_reader, size;
    uint32_t offset;
} Entry;
static struct {
    const PeExtensionSection *d;
    char key[48], owner[48];
    int enabled, locked;
} s_sections[PE_EXT_SECTION_LIMIT];
static int s_count, s_source, s_pending_valid, s_staged;
static unsigned char s_link[20], s_disk[PE_EXT_FILE_LIMIT], s_pending[PE_EXT_FILE_LIMIT],
    s_build[PE_EXT_FILE_LIMIT];
static unsigned s_disk_size, s_pending_size;
_Alignas(max_align_t) static unsigned char s_candidates[PE_EXT_SECTION_LIMIT][2048];
static int s_candidate_status[PE_EXT_SECTION_LIMIT];
static uint32_t s_generation, s_staged_generation;
static char s_path[4096], s_staged_path[4224], s_committed_path[4096], s_pending_path[4096],
    s_previous_file[4224];
static uint32_t rd32(const unsigned char *p) {
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static unsigned rd16(const unsigned char *p) {
    return p[0] | (unsigned)p[1] << 8;
}
static void wr16(unsigned char *p, unsigned v) {
    p[0] = v;
    p[1] = v >> 8;
}
static void wr32(unsigned char *p, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        p[i] = v >> (8 * i);
}
static uint32_t crc(const void *data, unsigned n) {
    const unsigned char *p = data;
    uint32_t c = ~0u;
    for (unsigned i = 0; i < n; ++i) {
        c ^= p[i];
        for (int j = 0; j < 8; ++j)
            c = c & 1 ? (c >> 1) ^ 0xedb88320u : c >> 1;
    }
    return ~c;
}
static int key_ok(const char *p) {
    if (!p || !*p || strlen(p) > 47) return 0;
    for (; *p; ++p)
        if (!(*p >= 'a' && *p <= 'z') && !(*p >= '0' && *p <= '9') && !strchr("_-/.", *p)) return 0;
    return 1;
}
static int index_of(const PeExtensionSection *d) {
    for (int i = 0; i < s_count; ++i)
        if (s_sections[i].d == d) return i;
    return -1;
}
static int parse(const unsigned char *b, unsigned n, Entry *entries) {
    if (n < HEADER || memcmp(b, "PEEX", 4) || rd16(b + 4) != 1 || rd16(b + 6) != HEADER ||
        rd32(b + 12) != n || !memchr(b + 20, 0, 64))
        return -1;
    unsigned count = rd16(b + 16), next = HEADER + count * DIRECTORY;
    if (count > PE_EXT_SECTION_LIMIT || next > n || rd16(b + 18)) return -1;
    for (unsigned i = 0; i < count; ++i) {
        const unsigned char *p = b + HEADER + i * DIRECTORY;
        Entry *e = &entries[i];
        if (!memchr(p, 0, 48) || !key_ok((const char *)p)) return -1;
        memcpy(e->key, p, 48);
        e->version = rd16(p + 48);
        e->min_reader = rd16(p + 50);
        e->offset = rd32(p + 52);
        if (rd32(p + 56) > PE_EXT_FILE_LIMIT) return -1;
        e->size = rd32(p + 56);
        if (!e->version || !e->min_reader || !e->size || e->offset != next || e->size > n - next ||
            rd32(p + 60) != crc(b + next, e->size) || (i && strcmp(entries[i - 1].key, e->key) >= 0))
            return -1;
        next += e->size;
    }
    return next == n ? (int)count : -1;
}
static int paths(const char *path, uint32_t generation, char *dir, size_t ds, char *file, size_t fs,
                 const char **basename) {
    if (!path || !*path) return 0;
    const char *a = strrchr(path, '/'), *b = strrchr(path, '\\');
    const char *slash = !a ? b : !b ? a : a > b ? a : b;
    const char *name = slash ? slash + 1 : path;
    size_t prefix = slash ? (size_t)(slash - path + 1) : 0;
    if (!*name || strlen(name) > 63) return 0;
    for (const char *p = name; *p; ++p)
        if (!(*p >= 'A' && *p <= 'Z') && !(*p >= 'a' && *p <= 'z') && !(*p >= '0' && *p <= '9') && *p != '-')
            return 0;
    int n = snprintf(dir, ds, "%.*s.pe-ext", (int)prefix, path);
    if (n < 0 || (size_t)n >= ds) return 0;
    n = snprintf(file, fs, "%s/%s.%08x.pex", dir, name, generation);
    if (n < 0 || (size_t)n >= fs) return 0;
    if (basename) *basename = name;
    return 1;
}
static PeSaveRecord s_record = {"pe_host.extlink", 1, s_link, sizeof s_link, NULL, NULL, NULL, NULL};
void pe_extension_install(void) {
    (void)pe_save_register_conditional(&s_record);
    (void)pe_save_host_stack_record();
}
int pe_extension_register(const PeExtensionSection *d) {
    const char *owner = pe_mod_current();
    if (!d || !key_ok(d->key) || !d->data || !d->version || !d->min_reader || d->size == 0 ||
        d->size > 2048 || s_count == PE_EXT_SECTION_LIMIT)
        return -1;
    /* Core setup runs before provider init. Runtime registration is never allowed. */
    if (pe_reg_registration_access() != PE_REG_OK) return -1;
    if (owner) {
        size_t n = strlen(owner);
        if (strncmp(owner, d->key, n) || d->key[n] != '/') return -1;
    } else if (strncmp(d->key, "pe_host/", 8)) return -1;
    for (int i = 0; i < s_count; ++i)
        if (!strcmp(s_sections[i].key, d->key)) return -1;
    s_sections[s_count].d = d;
    if (owner) snprintf(s_sections[s_count].owner, 48, "%s", owner);
    snprintf(s_sections[s_count].key, 48, "%s", d->key);
    ++s_count;
    return 0;
}
static int plan(Entry *entries, const unsigned char **bytes, int extra) {
    Entry disk[PE_EXT_SECTION_LIMIT];
    int n = s_disk_size ? parse(s_disk, s_disk_size, disk) : 0, count = 0;
    if (n < 0) return -1;
    for (int i = 0; i < n; ++i) {
        entries[count] = disk[i];
        bytes[count++] = s_disk + disk[i].offset;
    }
    for (int i = 0; i < s_count; ++i)
        if ((s_sections[i].enabled || i == extra) &&
            (!s_sections[i].owner[0] || pe_mod_is_running(s_sections[i].owner))) {
            if (s_sections[i].locked) return -1;
            const PeExtensionSection *d = s_sections[i].d;
            int j;
            for (j = 0; j < count; ++j)
                if (!strcmp(entries[j].key, s_sections[i].key)) break;
            if (j == count) {
                if (count == PE_EXT_SECTION_LIMIT) return -1;
                ++count;
            }
            snprintf(entries[j].key, 48, "%s", s_sections[i].key);
            entries[j].version = d->version;
            entries[j].min_reader = d->min_reader;
            entries[j].size = d->size;
            bytes[j] = d->data;
        }
    for (int i = 1; i < count; ++i) {
        Entry e = entries[i];
        const unsigned char *p = bytes[i];
        int j = i;
        while (j > 0 && strcmp(entries[j - 1].key, e.key) > 0) {
            entries[j] = entries[j - 1];
            bytes[j] = bytes[j - 1];
            --j;
        }
        entries[j] = e;
        bytes[j] = p;
    }
    unsigned total = HEADER + count * DIRECTORY;
    for (int i = 0; i < count; ++i)
        total += entries[i].size;
    return total <= PE_EXT_FILE_LIMIT ? count : -1;
}
PeRegResult pe_extension_enable(const PeExtensionSection *d) {
    PeRegResult r = pe_reg_access();
    if (r != PE_REG_OK) return r;
    int at = index_of(d);
    if (at < 0) return PE_REG_BAD_ARGUMENT;
    if (s_sections[at].owner[0] && !pe_mod_is_running(s_sections[at].owner)) return PE_REG_INACTIVE;
    if (!s_source || s_sections[at].locked || s_staged) return PE_REG_UNSAVEABLE;
    Entry entries[PE_EXT_SECTION_LIMIT];
    const unsigned char *bytes[PE_EXT_SECTION_LIMIT];
    if (plan(entries, bytes, at) < 0) return PE_REG_CAPACITY;
    if (pe_save_enable_record(&s_record) != PE_SAVE_EMISSION_OK) return PE_REG_UNSAVEABLE;
    s_sections[at].enabled = 1;
    return PE_REG_OK;
}
int pe_extension_available(const PeExtensionSection *d) {
    return pe_reg_access() == PE_REG_OK && s_source && index_of(d) >= 0 && !s_sections[index_of(d)].locked &&
           !s_staged &&
           (!s_sections[index_of(d)].owner[0] || pe_mod_is_running(s_sections[index_of(d)].owner));
}
void pe_extension_new_game(void) {
    s_source = 1;
    s_disk_size = s_pending_size = 0;
    s_pending_valid = s_staged = 0;
    s_generation = 0;
    s_committed_path[0] = s_previous_file[0] = 0;
    memset(s_link, 0, sizeof s_link);
    for (int i = 0; i < s_count; ++i) {
        s_sections[i].enabled = s_sections[i].locked = 0;
        memset(s_sections[i].d->data, 0, s_sections[i].d->size);
    }
}
int pe_extension_set_save_path(const char *path) {
    char dir[4160], file[4224];
    if (!paths(path, 1, dir, sizeof dir, file, sizeof file, NULL) || strlen(path) >= sizeof s_path) return -1;
    strcpy(s_path, path);
    return 0;
}
static unsigned encode(unsigned char*out,const Entry*entries,const unsigned char*const*bytes,int n,
                       uint32_t generation,const char*name,uint32_t previous){
    unsigned total = HEADER + n * DIRECTORY;
    memset(out, 0, PE_EXT_FILE_LIMIT);
    memcpy(out, "PEEX", 4);
    wr16(out + 4, 1);
    wr16(out + 6, HEADER);
    wr32(out + 8, generation);
    wr16(out + 16, n);
    snprintf((char *)out + 20, 64, "%s", name);
    for (int i = 0; i < n; ++i) {
        unsigned char *p = out + HEADER + i * DIRECTORY;
        memcpy(p, entries[i].key, 48);
        wr16(p + 48, entries[i].version);
        wr16(p + 50, entries[i].min_reader);
        wr32(p + 52, total);
        wr32(p + 56, entries[i].size);
        wr32(p + 60, crc(bytes[i], entries[i].size));
        memcpy(out + total, bytes[i], entries[i].size);
        total += entries[i].size;
    }
    wr32(out + 12, total);
    wr32(out+84,previous);
    wr32(out + 88, crc(out, total));
    return total;
}
int pe_extension_stage(void) {
    Entry entries[PE_EXT_SECTION_LIMIT];
    const unsigned char *bytes[PE_EXT_SECTION_LIMIT];
    int n = plan(entries, bytes, -1);
    if (n < 0) return -1;
    if (!n) return 0;
    if (!s_source || !s_path[0] || s_generation == UINT32_MAX) return -1;
    char dir[4160], file[4224], temp[4288];
    const char *name;
    uint32_t gen = s_generation + 1;
    FILE *f = NULL;
    /* Never overwrite an existing generation, including an orphan from an interrupted save. */
    int free_generation = 0;
    for (int tries = 0; tries < 256; ++tries) {
        if (!gen || !paths(s_path, gen, dir, sizeof dir, file, sizeof file, &name)) return -1;
        PE_MKDIR(dir);
        if ((f = fopen(file, "rb"))) {
            fclose(f);
            ++gen;
            continue;
        }
        free_generation = 1;
        break;
    }
    if (!gen || !free_generation) return -1;
    unsigned total=encode(s_build,entries,bytes,n,gen,name,
        !strcmp(s_committed_path,s_path)?s_generation:0);
    if (snprintf(temp, sizeof temp, "%s.tmp.%lu", file, pe_profile_process()) >= (int)sizeof temp) return -1;
    f = fopen(temp, "wbx");
    if (!f) return -1;
    int ok = fwrite(s_build, 1, total, f) == total && pe_profile_flush(f);
    if (fclose(f)) ok = 0;
    if (!ok || !pe_file_publish_new(temp, file)) {
        remove(temp);
        return -1;
    }
    memset(s_link, 0, sizeof s_link);
    s_link[0] = 1;
    s_link[1] = 1;
    wr32(s_link + 4, gen);
    wr32(s_link + 8, total);
    wr32(s_link + 12, crc(s_build, total));
    s_link[16] = n;
    s_staged_generation = gen;
    s_staged = 1;
    snprintf(s_staged_path, sizeof s_staged_path, "%s", file);
    return 0;
}
static int validate_load(const char *path, const unsigned char *block, const unsigned char *embedded, unsigned embedded_size) {
    unsigned char link[20];
    unsigned version = 0, size = 0;
    s_pending_valid = 0;
    s_pending_size = 0;
    if (!path || strlen(path) >= sizeof s_pending_path) return -1;
    strcpy(s_pending_path, path);
    int found = pe_save_peek(block, "pe_host.extlink", &version, link, sizeof link, &size);
    if (found < 0) return -1;
    if (!found) {
        if (embedded_size) return -1;
        if(!pe_reg_readonly_begin())return -1;
        int prepared=pe_save_prepare_load(block);pe_reg_readonly_end();
        if(prepared)return -1;
        s_pending_valid = 1;
        return 0;
    }
    if (version != 1 || size != 20 || link[0] != 1 || link[1] != 1 || link[2] || link[3] || link[17] ||
        link[18] || link[19])
        return -1;
    uint32_t gen = rd32(link + 4), n = rd32(link + 8);
    if (!gen || n < HEADER || n > PE_EXT_FILE_LIMIT) return -1;
    char dir[4160], file[4224];
    const char *name;
    if (!paths(path, gen, dir, sizeof dir, file, sizeof file, &name)) return -1;
    int ok;
    if (embedded) {
        if (embedded_size != n) return -1;
        memcpy(s_pending, embedded, n);
        ok = 1;
    } else {
        FILE *f = fopen(file, "rb");
        if (!f) return -1;
        ok = fread(s_pending, 1, n, f) == n && fgetc(f) == EOF;
        fclose(f);
    }
    if (!ok || !memchr(s_pending + 20, 0, 64) || crc(s_pending, n) != rd32(link + 12) ||
        rd32(s_pending + 8) != gen || strcmp((char *)s_pending + 20, name))
        return -1;
    uint32_t checksum = rd32(s_pending + 88);
    wr32(s_pending + 88, 0);
    ok = crc(s_pending, n) == checksum;
    wr32(s_pending + 88, checksum);
    Entry entries[PE_EXT_SECTION_LIMIT];
    int count = ok ? parse(s_pending, n, entries) : -1;
    if (count < 0 || count != link[16]) return -1;
    /* Identity data is required to interpret the native pool safely. Unknown provider sections
     * remain opaque, but an unreadable core identity ledger must refuse the outer load. */
    for (int i = 0; i < count; ++i)
        if (!strcmp(entries[i].key, "pe_host/content") &&
            (entries[i].version != 1 || entries[i].min_reader > 1 || entries[i].size != 2048))
            return -1;
    if(!pe_reg_readonly_begin())return -1;
    memset(s_candidate_status, 0, sizeof s_candidate_status);
    for (int i = 0; i < s_count; ++i) {
        const PeExtensionSection *d = s_sections[i].d;
        for (int j = 0; j < count; ++j) if (!strcmp(s_sections[i].key, entries[j].key)) {
            Entry *e = &entries[j];
            unsigned char *candidate = s_candidates[i];
            memset(candidate, 0, d->size);
            int ok = 0;
            if (e->version == d->version && e->min_reader <= 1 && e->size == d->size) {
                memcpy(candidate, s_pending + e->offset, d->size); ok = 1;
            } else if (e->version < d->version && e->min_reader <= 1 && d->upgrade) {
                ok = !d->upgrade(candidate, s_pending + e->offset, e->size, e->version);
            }
            if (ok && d->check) ok = !d->check(candidate);
            if (ok && d->check_native) ok = !d->check_native(candidate, block);
            s_candidate_status[i] = ok ? 1 : -1;
            break;
        }
    }
    int prepared=pe_save_prepare_load(block);
    pe_reg_readonly_end();
    if(prepared)return -1;
    for(int i=0;i<s_count;i++)if(!strcmp(s_sections[i].key,"pe_host/content")&&s_candidate_status[i]<0)return -1;
    s_pending_size = n;
    s_pending_valid = 1;
    return 0;
}
int pe_extension_validate_load(const char *path, const unsigned char *block) {
    return validate_load(path, block, NULL, 0);
}
void pe_extension_apply_load(void) {
    if (!s_pending_valid) {
        s_source = 0;
        return;
    }
    /* A companion wins over a stale legacy record, including with its provider absent. */
    if (s_pending_size) {
        Entry es[PE_EXT_SECTION_LIMIT];int count=parse(s_pending,s_pending_size,es);
        for(int i=0;i<count;i++)if(!strcmp(es[i].key,"pe_stacks/counts"))
            pe_save_retire_record(pe_save_host_stack_record());
    }
    s_disk_size = s_pending_size;
    if (s_disk_size) memcpy(s_disk, s_pending, s_disk_size);
    strcpy(s_committed_path, s_pending_path);
    s_previous_file[0] = 0;
    if (s_disk_size && rd32(s_disk + 84) && rd32(s_disk + 84) < rd32(s_disk + 8)) {
        char dir[4160];
        (void)paths(s_committed_path, rd32(s_disk + 84), dir, sizeof dir, s_previous_file,
                    sizeof s_previous_file, NULL);
    }
    s_generation = s_disk_size ? rd32(s_disk + 8) : 0;
    s_source = 1;
    s_staged = 0;
    s_pending_valid = 0;
    Entry entries[PE_EXT_SECTION_LIMIT];
    int n = s_disk_size ? parse(s_disk, s_disk_size, entries) : 0;
    for (int i = 0; i < s_count; ++i) {
        const PeExtensionSection *d = s_sections[i].d;
        s_sections[i].enabled = s_sections[i].locked = 0;
        memset(d->data, 0, d->size);
        for (int j = 0; j < n; ++j)
            if (!strcmp(s_sections[i].key, entries[j].key)) {
                if (s_candidate_status[i] != 1) {
                    s_sections[i].locked = 1;
                    break;
                }
                memcpy(d->data, s_candidates[i], d->size);
                s_sections[i].enabled = 1;
                break;
            }
    }
}
void pe_extension_committed(void) {
    if (s_staged) {
        if (!strcmp(s_committed_path, s_path)) {
            if (s_previous_file[0]) remove(s_previous_file);
            if (s_generation) {
                char dir[4160];
                (void)paths(s_path, s_generation, dir, sizeof dir, s_previous_file, sizeof s_previous_file,
                            NULL);
            }
        } else s_previous_file[0] = 0;
        strcpy(s_committed_path, s_path);
        s_disk_size = rd32(s_build + 12);
        memcpy(s_disk, s_build, s_disk_size);
        s_generation = s_staged_generation;
        s_staged = 0;
    }
}
void pe_extension_cancel(void) {
    if (s_staged && s_staged_path[0]) remove(s_staged_path);
    s_staged = 0;
    s_staged_path[0] = 0;
}

int pe_mod_save_path(const char *path) {
    return pe_extension_set_save_path(path);
}
int pe_mod_load_admit(const char *path, const unsigned char *block) {
    return pe_extension_validate_load(path, block);
}
void pe_mod_save_committed(const char *path) {
    if (path && !strcmp(path, s_path)) {
        pe_extension_committed();
        pe_profile_transactions_committed();
    }
}
void pe_mod_save_cancelled(void) {
    pe_extension_cancel();
}

int pe_mod_save_publish(const char *temporary, const char *target) {
    /* A staged companion belongs to s_path; a save with none has nothing to keep in step. */
    if (!temporary || !target || (s_staged && strcmp(target, s_path))) return 0;
    FILE *f = fopen(temporary, "r+b");
    if (!f) return 0;
    int ok = pe_profile_flush(f);
    if (fclose(f)) ok = 0;
    return ok && pe_profile_replace(temporary, target);
}

int pe_extension_snapshot(unsigned char*out,unsigned capacity,unsigned*size,unsigned char link[20],
                          const char*patch_key,const void*patch,unsigned patch_size){
    if(pe_reg_access()!=PE_REG_OK||!s_source||s_staged||!out||capacity<PE_EXT_FILE_LIMIT||!size||!link)return -1;
    Entry entries[PE_EXT_SECTION_LIMIT];const unsigned char*bytes[PE_EXT_SECTION_LIMIT];
    int n=plan(entries,bytes,-1);if(n<0)return -1;
    if(!n){*size=0;memset(link,0,20);return 0;}
    for(int i=0;i<n;++i)if(patch_key&&!strcmp(entries[i].key,patch_key)){
        if(!patch||entries[i].version!=1||entries[i].min_reader>1||entries[i].size!=patch_size)return -1;
        bytes[i]=patch;
    }
    unsigned total=encode(out,entries,bytes,n,1,"pe-crash",0);
    memset(link,0,20);link[0]=1;link[1]=1;wr32(link+4,1);wr32(link+8,total);wr32(link+12,crc(out,total));link[16]=n;
    *size=total;return 0;
}
/* PECP v1: native US save-v1 plus companion, entirely self-contained. */

int pe_checkpoint_pack(unsigned char *bytes,unsigned cap,unsigned *used,const unsigned char *block) {
    unsigned char link[20];unsigned version=0,size=0,n=s_staged?rd32(s_build+12):0;
    if(!bytes||!used||!block||cap<32+8192+n)return -1;
    int found=pe_save_peek(block,"pe_host.extlink",&version,link,sizeof link,&size);
    if(found<0 || (!!found != !!n))return -1;
    if(found&&(version!=1||size!=20||rd32(link+8)!=n||rd32(link+12)!=crc(s_build,n)))return -1;
    memset(bytes,0,32);memcpy(bytes,"PECP",4);wr32(bytes+4,1);wr32(bytes+8,8192);
    wr32(bytes+12,n);wr32(bytes+16,0x50455531u);
    memcpy(bytes+32,block,8192);if(n)memcpy(bytes+32+8192,s_build,n);
    wr32(bytes+20,crc(bytes,32+8192+n));*used=32+8192+n;return 0;
}
int pe_checkpoint_unpack(const unsigned char *bytes,unsigned total,const char *binding,unsigned char *block) {
    s_pending_valid=0;s_pending_size=0;
    if(!bytes||!binding||!block||total<32+8192||total>32+8192+PE_EXT_FILE_LIMIT||
       memcmp(bytes,"PECP",4)||rd32(bytes+4)!=1||rd32(bytes+8)!=8192||
       rd32(bytes+12)>PE_EXT_FILE_LIMIT||total!=32+8192+rd32(bytes+12)||
       rd32(bytes+16)!=0x50455531u||rd32(bytes+24)||rd32(bytes+28))return -1;
    unsigned char copy[32+8192+PE_EXT_FILE_LIMIT];memcpy(copy,bytes,total);
    uint32_t checksum=rd32(copy+20);wr32(copy+20,0);
    if(crc(copy,total)!=checksum)return -1;
    if(validate_load(binding,bytes+32,bytes+32+8192,rd32(bytes+12)))return -1;
    memcpy(block,bytes+32,8192);return 0;
}
int pe_checkpoint_write(const char *path,const unsigned char *block) {
    unsigned char bytes[32+8192+PE_EXT_FILE_LIMIT];unsigned total;
    if(!path||pe_checkpoint_pack(bytes,sizeof bytes,&total,block))return -1;
    char temp[4224];
    if(snprintf(temp,sizeof temp,"%s.tmp.%lu",path,pe_profile_process()) >= (int)sizeof temp)return -1;
    FILE *f=fopen(temp,"wbx");if(!f)return -1;
    int ok=fwrite(bytes,1,total,f)==total&&pe_profile_flush(f);if(fclose(f))ok=0;
    if(ok)ok=pe_profile_replace(temp,path);if(!ok)remove(temp);
    if(ok)pe_profile_transactions_committed();return ok?0:-1;
}
int pe_checkpoint_read(const char *path,const char *binding,unsigned char *block) {
    unsigned char bytes[32+8192+PE_EXT_FILE_LIMIT];
    s_pending_valid=0;s_pending_size=0;
    if(!path||!binding||!block)return -1;
    FILE *f=fopen(path,"rb");if(!f)return -1;
    unsigned total=(unsigned)fread(bytes,1,sizeof bytes,f);
    int ok=!ferror(f)&&fgetc(f)==EOF;fclose(f);
    return ok?pe_checkpoint_unpack(bytes,total,binding,block):-1;
}
static int file_exists(const char *path) {
    FILE *f=path?fopen(path,"rb"):NULL;
    if(f)fclose(f);
    return f!=NULL;
}
int pe_checkpoint_slot_state(const char *checkpoint,const char *legacy) {
    if(file_exists(checkpoint))return PE_CHECKPOINT_SLOT_CURRENT;
    return file_exists(legacy)?PE_CHECKPOINT_SLOT_LEGACY:PE_CHECKPOINT_SLOT_EMPTY;
}
int pe_checkpoint_read_legacy(const char *path,unsigned char *block) {
    unsigned char bytes[8192];
    FILE *f=path&&block?fopen(path,"rb"):NULL;
    if(!f)return -1;
    int ok=fread(bytes,1,sizeof bytes,f)==sizeof bytes&&fgetc(f)==EOF&&!ferror(f);
    fclose(f);
    if(!ok)return -1;
    memcpy(block,bytes,sizeof bytes);
    return 0;
}
/* Host-owned v1 stack reserve. A missing provider never owns the only copy.
 * Materialize from reserve only: consuming native slots cannot recreate items. */
void pe_extension_refill_stacks(short *lists[3],const unsigned sizes[3]) {
    if(!s_source||s_staged||pe_mod_is_running("pe_stacks")||!s_disk_size)return;
    Entry entries[PE_EXT_SECTION_LIMIT];int n=parse(s_disk,s_disk_size,entries);
    for(int j=0;j<n;++j)if(!strcmp(entries[j].key,"pe_stacks/counts")) {
        Entry *e=&entries[j];if(e->version!=1||e->min_reader>1||e->size!=204)return;
        unsigned char *b=s_disk+e->offset;unsigned total=b[0]+b[1]+b[2];
        if(total>100||b[3])return;
        unsigned k=0;
        for(int l=0;l<3;++l) {
            unsigned char seen[256]={0};
            for(unsigned i=0;i<b[l];++i,++k) {
                unsigned id=b[4+k*2],qty=b[5+k*2];
                if(!id||qty>98||seen[id])return;
                seen[id]=1;
            }
        }
        k=0;
        for(int l=0;l<3;++l)for(unsigned i=0;i<b[l];++i,++k) {
            unsigned char *pair=b+4+k*2;
            for(unsigned slot=0;slot<sizes[l]&&pair[1];++slot)
                if(!lists[l][slot]){lists[l][slot]=pair[0];--pair[1];}
        }
        unsigned char packed[204]={0}; unsigned out=0; k=0;
        for(int l=0;l<3;++l)for(unsigned i=0;i<b[l];++i,++k)
            if(b[5+k*2]) {
                packed[l]++;packed[4+out*2]=b[4+k*2];
                packed[5+out*2]=b[5+k*2];++out;
            }
        memcpy(b,packed,sizeof packed);
        wr32(s_disk+HEADER+j*DIRECTORY+60,crc(b,e->size));
        wr32(s_disk+88,0);wr32(s_disk+88,crc(s_disk,s_disk_size));
        return;
    }
}

int pe_extension_owner_available(const char *owner) {
    if(!owner)return 1;
    for(int i=0;i<s_count;i++)if(!strcmp(owner,s_sections[i].owner)&&s_sections[i].locked)return 0;
    return 1;
}
