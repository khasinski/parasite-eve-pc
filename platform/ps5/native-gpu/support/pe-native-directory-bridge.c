/* Independent ABI boundary to the already-linked public ps5platform provider.
 * Compile with the retained RADV SDK headers; never expose its DIR/dirent. */
#include <ps5platform/libc.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
void *pe_native_directory_open(const char *path, int *error) {
    DIR *p = ps5_opendir(path);
    *error = p ? 0 : errno;
    return p;
}
int pe_native_directory_next(void *opaque, char name[256], unsigned *type,
                             uint32_t *inode, int *error) {
    errno = 0;
    struct dirent *e = ps5_readdir((DIR *)opaque);
    if (!e) { *error = errno; return 0; }
    /* The pinned public provider validates raw getdents records, then
     * returns only d_type and d_name in its zeroed dirent. d_namlen is not
     * populated: reading it rejects every nonempty valid name as EIO. */
    const char *end = memchr(e->d_name, 0, sizeof e->d_name);
    if (!end) { *error = EIO; return 0; }
    size_t n = (size_t)(end - e->d_name);
    if (n > 255) { *error = EIO; return 0; }
    memcpy(name,e->d_name,n+1); *type=e->d_type; *inode=e->d_fileno; *error=0;
    return 1;
}
int pe_native_directory_close(void *opaque, int *error) {
    int r = ps5_closedir((DIR *)opaque); *error = r < 0 ? errno : 0; return r;
}
