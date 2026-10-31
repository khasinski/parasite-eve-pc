/* Static PS5 application policy: external plugins and SDL runtime replacement
 * are unsupported. Unlike SDK payload dlfcn, these functions require no
 * loader-injected callbacks. Failures are explicit; no module is loaded.
 * SPDX-License-Identifier: MIT
 */
#include <dlfcn.h>
#include <errno.h>

static _Thread_local int unavailable;

void *dlopen(const char *path, int mode)
{
    (void)path;
    (void)mode;
    unavailable = 1;
    errno = ENOSYS;
    return 0;
}

void *dlsym(void *handle, const char *name)
{
    (void)handle;
    (void)name;
    unavailable = 1;
    errno = ENOSYS;
    return 0;
}

int dlclose(void *handle)
{
    (void)handle;
    unavailable = 1;
    errno = ENOSYS;
    return -1;
}

char *dlerror(void)
{
    if (!unavailable) return 0;
    unavailable = 0;
    return "Dynamic library loading is unsupported in this static PS5 application";
}
