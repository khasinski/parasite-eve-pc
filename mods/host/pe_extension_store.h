#ifndef PE_EXTENSION_STORE_H
#define PE_EXTENSION_STORE_H
#include "pe_registry.h"
/* Bounded companion store, always owned by the host. Unknown/newer sections are preserved.
 * Registration is init-only, full names are copied, caller data must outlive the game.
 * Sections are dormant until enabled BEFORE mutation. A refused enable changes nothing.
 * No public filesystem paths. The native save bridge alone supplies the mapped card path.
 */
#define PE_EXT_FILE_LIMIT 8192u
#define PE_EXT_SECTION_LIMIT 16
int pe_checkpoint_pack(unsigned char *, unsigned, unsigned *, const unsigned char *);
int pe_checkpoint_unpack(const unsigned char *, unsigned, const char *, unsigned char *);
int pe_checkpoint_write(const char *path, const unsigned char *block);
int pe_checkpoint_read(const char *path, const char *binding, unsigned char *block);
/* A slot's file: a checkpoint wins over an old plain card file kept beside it. */
enum { PE_CHECKPOINT_SLOT_EMPTY, PE_CHECKPOINT_SLOT_CURRENT, PE_CHECKPOINT_SLOT_LEGACY };
int pe_checkpoint_slot_state(const char *checkpoint, const char *legacy);
/* Exactly one 8 KB card file; admit it with pe_mod_load_admit once its native checks pass. */
int pe_checkpoint_read_legacy(const char *path, unsigned char *block);
void pe_extension_refill_stacks(short *lists[3], const unsigned sizes[3]);
typedef struct {
    const char *key;
    uint16_t version, min_reader, size;
    void *data;
    /* Pure, bounded adapters: no gameplay, profile I/O, RNG or publication.
     * Return zero for success. Failure locks and preserves the original bytes. */
    int (*upgrade)(void *candidate, const void *old, unsigned size, unsigned version);
    int (*check)(const void *candidate);
    int (*check_native)(const void *candidate, const unsigned char *native_block);
} PeExtensionSection;
int pe_extension_owner_available(const char *);
int pe_extension_register(const PeExtensionSection *section);
PeRegResult pe_extension_enable(const PeExtensionSection *section);
int pe_extension_available(const PeExtensionSection *section);
/* Snapshot into caller memory only; patch replaces an existing v1 section in the copy.
 * Neutral filename/generation are rebound by Restore. No callbacks, allocation, I/O or live changes. */
int pe_extension_snapshot(unsigned char *out,unsigned capacity,unsigned *size,unsigned char link[20],
    const char *patch_key,const void *patch,unsigned patch_size);
/* Core initialization and native save boundary (not provider callbacks). */
void pe_extension_install(void);
void pe_extension_new_game(void);
int pe_extension_set_save_path(const char *native_path);
int pe_extension_stage(void);
int pe_extension_validate_load(const char *native_path, const unsigned char *block);
void pe_extension_apply_load(void);
void pe_extension_committed(void);
void pe_extension_cancel(void);
#endif
