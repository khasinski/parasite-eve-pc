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
typedef struct {
    const char *key;
    uint16_t version, min_reader, size;
    void *data;
} PeExtensionSection;
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
