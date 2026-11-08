/* POSIX stdio locking that the KallistiOS newlib declares but does not
 * build; scene_assets.c holds a FILE across seek+read pairs. Newlib's own
 * recursive per-FILE lock does the work. */
#include <stdio.h>
#include <sys/stdio.h>

void flockfile(FILE *file) { _flockfile(file); }

void funlockfile(FILE *file) { _funlockfile(file); }

int ftrylockfile(FILE *file) {
    _flockfile(file);
    return 0;
}
