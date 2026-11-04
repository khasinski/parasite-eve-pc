/* Early diagnostics: no libc, TLS, errno or heap before _init_env.
 * Shared allocator for application CRT and payload main. Public raw APIs only.
 * No automatic pruning, overwriting, or retries after bounded allocation fails. */
#include <stddef.h>
#include "pe-build-identity.h"
extern int sceKernelOpen(const char *, int, unsigned);
extern long sceKernelWrite(int, const void *, size_t);
extern int sceKernelFsync(int);
static int fd = -1, attempted;
static char session_path[240];
static const char *const roots[] = {
    "/data/parasite-eve/logs/startup-", "/data/parasite-eve/startup-", "/data/parasite-eve-startup-"
};
static size_t append(char *out, size_t at, const char *s) {
    while (*s) out[at++] = *s++;
    out[at] = 0;
    return at;
}
static void raw_write(int target, const char *message) {
    size_t length = 0;
    while (message[length]) ++length;
    while (length) {
        long n = sceKernelWrite(target, message, length);
        if (n <= 0 || (size_t)n > length) break;
        message += n; length -= (size_t)n;
    }
    (void)sceKernelFsync(target);
}
int pe_session_fd(void) {
    unsigned root, run;
    if (attempted) return fd;
    attempted = 1;
    for (root = 0; root < 3; ++root) {
        for (run = 0; run < 256; ++run) {
            static const char hex[] = "0123456789abcdef";
            size_t n = append(session_path, 0, roots[root]);
            n = append(session_path, n, PE_BUILD_ATTEMPT);
            session_path[n++] = '-';
            session_path[n++] = hex[run >> 4]; session_path[n++] = hex[run & 15];
            append(session_path, n, ".log");
            /* SDK fcntl.h: WRONLY|APPEND|CREAT|EXCL, never TRUNC. */
            fd = sceKernelOpen(session_path, 0xa09, 0666);
            if (fd >= 0) {
                /* Append-only discovery index; last complete line is newest session.
                 * One descriptor retained for process lifetime avoids new CRT APIs.
                 * Pre-main failures remain discoverable through this index or glob.
                 */
                int index = sceKernelOpen("/data/parasite-eve-latest-session.txt", 0x209, 0666);
                raw_write(fd, "PE build " PE_BUILD_IDENTITY "\n");
                if (index >= 0) {
                    char line[242];
                    size_t len = append(line, 0, session_path);
                    line[len++] = '\n'; line[len] = 0;
                    raw_write(index, line);
                }
                return fd;
            }
            /* EEXIST: public kernel encoding, also POSIX-negative for host fixture.
             * Missing/unwritable directories fall back immediately. */
            if (fd != -17 && (unsigned)fd != 0x80020011u) break;
        }
    }
    session_path[0] = 0;
    return -1;
}
const char *pe_session_path(void) { (void)pe_session_fd(); return session_path; }
extern void ps5_fp_ieee(void);
void pe_application_stage(const char *message) {
    static int fp_initialized;
    if (!fp_initialized) { fp_initialized = 1; ps5_fp_ieee(); }
    int target = pe_session_fd();
    if (target >= 0) raw_write(target, message);
}
