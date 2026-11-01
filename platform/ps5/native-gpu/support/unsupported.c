/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <sys/types.h>
#include <stddef.h>
/* Mesa ac_print_gpu_info optionally reads Linux /proc/self/fd. Native PS5
 * has no such filesystem, and its title ABI has no readlink export. Fail
 * honestly; do not pull a libkernel_sys stub or emit an inline syscall.
 * The pinned archive references this only in optional diagnostic printing. */
ssize_t cpe_readlink(const char *path, char *buffer, size_t size)
{
    (void)path; (void)buffer; (void)size;
    errno = ENOSYS;
    return -1;
}
extern int ps5_mkstemps(char *path, int suffix_length);
int cpe_mkstemp(char *path) { return ps5_mkstemps(path, 0); }
