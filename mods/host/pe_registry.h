#ifndef PE_REGISTRY_H
#define PE_REGISTRY_H

/* The shared registry for mods' definitions: custom content, equipment effects, Catalog entries and
 * shop offers (bridge findings D6 §3, §4 and §11, "the small shared collection / seal / readiness
 * facility"). Nothing here is gameplay: it decides, once per start, which declared definitions are
 * ready, the same way whatever order mods start in.
 *
 * Lifecycle (§11.5):
 *   COLLECT  during its init, a mod declares batches and submits entries. A submission returns
 *            PE_REG_PENDING: a receipt, not a usable definition.
 *   SEAL     once every mod has started (the host calls pe_registry_seal), the whole set is checked:
 *            limits, duplicate keys, references and batch requirements, to a fixed point.
 *   RUN      ready entries are published at once; lookups see only them, and only while their mod
 *            runs. Anything submitted now gets PE_REG_WRONG_PHASE: changes need a restart.
 *
 * Identity (§11.1): the owner is the mod whose init is running, never a string it passes; its
 * namespace is its mod id, so every key it submits is "<mod id>/<name>". Keys are lowercase ASCII
 * (a-z 0-9 _ - . /), at most 47 bytes, refused rather than rewritten. A key is unique within its
 * domain.
 *
 * Batches (§11.1): the smallest unit that becomes ready; every entry in one becomes ready, or none
 * does. A batch may require other batches (its own by name, another mod's as "mod_id:batch"). A
 * mod's required batches are its base: if one fails, all its batches fail. An optional batch fails
 * alone, with what requires it.
 *
 * Conflicts and failures (§11.2, §11.3), the same whatever the order:
 *   - a key submitted twice (by one mod or two) fails every claimant's batch; nothing wins;
 *   - a reference to a missing or failed entry fails the referring batch; requirement cycles fail;
 *   - a mod over its count or byte limit in a domain loses its whole contribution there;
 *   - declarations over a whole domain's limit make that domain unavailable this start;
 *   - more than PE_REG_MAX_OWNERS mods taking part make this start's registry unavailable.
 * Threading: bootstrap/reset binds the game thread before mod init; worker mutations are refused
 * with WRONG_THREAD and worker lookups return unavailable. Seal is private until publication.
 * Required staging/publication allocation failures refuse readiness instead of publishing a subset.
 * Reference arrays are sized to actual use. A separate checked heap+static envelope bounds real
 * registry metadata to 1 MiB, in addition to the per-domain logical key/payload/reference limits.
 * Exhausting that physical envelope makes the generation unavailable, without selecting a winner.
 * Payloads are copied; the registry owns its copies until the next start.
 */
#include <stddef.h>
#include <stdint.h>

typedef enum { PE_REG_CONTENT, PE_REG_EFFECT, PE_REG_CATALOG, PE_REG_SHOP, PE_REG_DOMAINS } PeRegDomain;

typedef enum {
    PE_REG_OK, PE_REG_PENDING, PE_REG_UNAVAILABLE, PE_REG_INACTIVE, PE_REG_WRONG_THREAD, PE_REG_WRONG_PHASE,
    PE_REG_BAD_VERSION, PE_REG_BAD_ARGUMENT, PE_REG_DUPLICATE_ID, PE_REG_CAPACITY, PE_REG_STALE_GENERATION,
    PE_REG_UNSUPPORTED, PE_REG_CONFLICT, PE_REG_UNSAVEABLE, PE_REG_MISSING_REFERENCE, PE_REG_REQUIRES_FAILED,
    PE_REG_CYCLE
} PeRegResult;

#define PE_REG_KEY_MAX        47     /* bytes, without the terminating zero */
#define PE_REG_BATCH_MAX      31
#define PE_REG_MAX_OWNERS     32
#define PE_REG_MAX_BATCHES    16     /* per mod */
#define PE_REG_MAX_REFS       8      /* per entry */
#define PE_REG_MAX_REQUIRES   16     /* per batch */

/* Per domain: entries and metadata bytes (key, payload and references), per mod and in total (§11.3). */
typedef struct { uint32_t owner_entries, owner_bytes, domain_entries, domain_bytes; } PeRegLimits;
extern const PeRegLimits g_pe_reg_limits[PE_REG_DOMAINS];

typedef struct PeRegRef {
    uint8_t domain;
    const char *key;
    uint32_t schema;              /* 0: untyped; otherwise target must have this schema */
} PeRegRef;

typedef struct PeRegEntry {
    uint8_t domain;
    const char *key;               /* "<your mod id>/<name>" */
    const void *data;              /* copied; may be NULL with size 0 */
    uint32_t size;
    const PeRegRef *refs;          /* definitions this one needs, any mod's; copied */
    uint8_t nrefs;
    uint32_t schema;              /* optional typed payload contract, compared at seal */
    uint32_t unique_slot;         /* nonzero: unique within owner/domain/schema */
} PeRegEntry;

/* ---- COLLECT: during your mod's init ---- */
/* Declare a batch before submitting to it. `required`: part of your base (see above). `requires`:
 * NULL-terminated, "batch" (yours) or "mod_id:batch"; copied. */
PeRegResult pe_reg_batch(const char *batch, int required, const char *const *requires);
PeRegResult pe_reg_submit(const char *batch, const PeRegEntry *entry);   /* PE_REG_PENDING if accepted */

/* ---- after the seal ---- */
uint32_t pe_reg_generation(void);           /* 0 until the first seal; new each start */
int pe_reg_domain_ready(int domain);
/* A ready entry of a running mod, or NULL. *size and *owner may be NULL. */
const void *pe_reg_find(int domain, const char *key, uint32_t *size, const char **owner);
/* Ready entries of a domain in canonical order (owner, batch, key): the same every start. */
int pe_reg_count(int domain);
const void *pe_reg_find_typed(int domain,const char *key,uint32_t schema,uint32_t expected_size);
const char *pe_reg_key(int domain, int index);
/* How a batch came out: PE_REG_OK, PE_REG_PENDING (not sealed yet) or why it failed, in `why`. */
PeRegResult pe_reg_batch_result(const char *owner, const char *batch, char *why, size_t size);
const char *pe_reg_result_name(PeRegResult r);

/* Thread/phase gate for core runtime APIs; never binds affinity from a worker. */
PeRegResult pe_reg_access(void);
PeRegResult pe_reg_registration_access(void);
/* Pure callback scope: core mutation APIs and nested evaluations must refuse. */
int pe_reg_readonly_begin(void);
void pe_reg_readonly_end(void);
int pe_reg_callback_active(void);

/* ---- the host ---- */
void pe_registry_seal(void);                /* once every mod has started */
void pe_registry_reset(void);               /* forget everything (a new start; tests) */

#endif
