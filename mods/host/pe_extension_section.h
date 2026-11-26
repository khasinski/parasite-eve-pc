#ifndef PE_EXTENSION_SECTION_H
#define PE_EXTENSION_SECTION_H

#include "pe_registry.h"

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

#endif
