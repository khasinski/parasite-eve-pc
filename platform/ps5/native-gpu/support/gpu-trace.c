/* SPDX-License-Identifier: MIT. Bounded observations, no GPU ownership changes. */
#include <stdatomic.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <string.h>
extern void pe_application_stage(const char *);
static atomic_flag busy = ATOMIC_FLAG_INIT;
static unsigned messages, queue_seen;
static atomic_flag error_claim = ATOMIC_FLAG_INIT;
static char first_error[512];
static atomic_ullong direct_allocated, direct_released;
void CPE_GPUTrace(const char *name, long long result, unsigned long long a, unsigned long long b) {
    int saved = errno;
    /* Reserve the first error independently of the normal log budget and
     * nonblocking serialization. A concurrent ordinary log cannot swallow it. */
    int first = result < 0 && !atomic_flag_test_and_set_explicit(&error_claim, memory_order_acquire);
    if (first) {
        char line[640];
        snprintf(first_error, sizeof first_error, "%s result=%lld", name, result);
        snprintf(line, sizeof line, "GPU FIRST_ERROR=%s rendered_output=unobserved\n", first_error);
        pe_application_stage(line);
    }
    if (atomic_flag_test_and_set_explicit(&busy, memory_order_acquire)) { errno=saved; return; }
    /* First queue admission is also reserved after budget exhaustion. */
    int queue = strcmp(name, "queue_admitted") == 0;
    int first_queue = queue && !queue_seen;
    if (queue) queue_seen = 1;
    if ((!queue || first_queue) && (messages < 192 || first_queue)) {
        char line[1024];
        snprintf(line, sizeof line, "GPU stage=%s result=%lld a=%llu b=%llu direct_allocated_cumulative=%llu direct_released_cumulative=%llu rendered_output=unobserved\n",
            name, result, a, b, atomic_load(&direct_allocated), atomic_load(&direct_released));
        pe_application_stage(line); ++messages;
        if (messages == 192) pe_application_stage("GPU normal log budget exhausted; first error/admission still reserved\n");
    }
    atomic_flag_clear_explicit(&busy, memory_order_release);
    errno = saved;
}
int __real_sceKernelAllocateDirectMemory(int64_t, int64_t, size_t, size_t, int, int64_t *);
int __wrap_sceKernelAllocateDirectMemory(int64_t low, int64_t high, size_t size, size_t align, int type, int64_t *out) {
    int r = __real_sceKernelAllocateDirectMemory(low,high,size,align,type,out); int saved=errno;
    if (!r) atomic_fetch_add(&direct_allocated,size);
    CPE_GPUTrace("direct_heap_allocate",r,size,align); errno=saved; return r;
}
int __real_sceKernelReleaseDirectMemory(int64_t, size_t);
int __wrap_sceKernelReleaseDirectMemory(int64_t address, size_t size) {
    int r=__real_sceKernelReleaseDirectMemory(address,size); int saved=errno;
    if (!r) atomic_fetch_add(&direct_released,size);
    CPE_GPUTrace("direct_heap_release",r,size,0); errno=saved; return r;
}
