/* SPDX-License-Identifier: MIT
 * Application-only, opt-in linker --wrap diagnostics. Link together with
 * application-startup-log.c and --wrap=malloc,--wrap=calloc,--wrap=realloc.
 * No allocator replacement: forward once, retain the real return and errno.
 * Requires the application's initialized libc errno ABI. Does not diagnose
 * allocations internal to shared libraries or calls bypassing linker wrapping.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <errno.h>

extern void pe_application_stage(const char *message);
extern void *cpe_heap_malloc(size_t);
extern void *cpe_heap_calloc(size_t, size_t);
extern void *cpe_heap_realloc(void *, size_t);

/* Lock-free x86 byte flag: skip recursive/concurrent logging, never wait for
 * another thread which might itself be waiting inside the allocator/logger.
 * Budget covers ALL attempted messages, including unavailable log files.
 */
static atomic_flag emitting = ATOMIC_FLAG_INIT;
static unsigned messages;
enum { MESSAGE_LIMIT = 128, LARGE_ALLOCATION = 256 * 1024 };

static char *literal(char *p, const char *s) {
    while (*s) *p++ = *s++;
    return p;
}
static char *number(char *p, size_t value) {
    char digits[3 * sizeof(size_t)];
    unsigned n = 0;
    do { digits[n++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (n) *p++ = digits[--n];
    return p;
}
static void observe(const char *op, size_t count, size_t size, void *result,
                    int saved_errno) {
    int overflow = count != 0 && size > SIZE_MAX / count;
    /* A null return for a zero-sized request is not an allocation failure. */
    int failed = !result && count != 0 && size != 0;
    int large = overflow || (count != 0 && size >=
        (LARGE_ALLOCATION / count + (LARGE_ALLOCATION % count != 0)));
    if ((!failed && !large) ||
        atomic_flag_test_and_set_explicit(&emitting, memory_order_acquire)) return;
    if (messages < MESSAGE_LIMIT) {
        char line[256]; /* Maximum: literals + 2x20-digit sizes + 11-digit int. */
        char *p = literal(line, "allocation ");
        ++messages;
        p = literal(p, op);
        p = literal(p, " count="); p = number(p, count);
        p = literal(p, " size="); p = number(p, size);
        p = literal(p, " overflow="); *p++ = (char)('0' + overflow);
        p = literal(p, " null="); *p++ = result ? '0' : '1';
        p = literal(p, " failed="); *p++ = (char)('0' + failed);
        p = literal(p, " errno=");
        /* Unsigned subtraction also handles INT_MIN without signed overflow. */
        unsigned magnitude = (unsigned)saved_errno;
        if (saved_errno < 0) { *p++ = '-'; magnitude = 0u - magnitude; }
        p = number(p, magnitude);
        if (messages == MESSAGE_LIMIT) p = literal(p, " limit=reached");
        *p++ = '\n'; *p = '\0';
        pe_application_stage(line);
    }
    atomic_flag_clear_explicit(&emitting, memory_order_release);
}

void *__wrap_malloc(size_t size) {
    void *result = cpe_heap_malloc(size);
    int saved_errno = errno;
    observe("malloc", 1, size, result, saved_errno);
    errno = saved_errno;
    return result;
}
void *__wrap_calloc(size_t count, size_t size) {
    void *result = cpe_heap_calloc(count, size);
    int saved_errno = errno;
    observe("calloc", count, size, result, saved_errno);
    errno = saved_errno;
    return result;
}
void *__wrap_realloc(void *ptr, size_t size) {
    void *result = cpe_heap_realloc(ptr, size);
    int saved_errno = errno;
    observe("realloc", 1, size, result, saved_errno);
    errno = saved_errno;
    return result;
}
