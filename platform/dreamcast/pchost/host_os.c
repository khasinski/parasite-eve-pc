/* pthreads in place of dc_os.c's KallistiOS threads, for the host harness. */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include "psyz/os.h" /* psyz/src on the include path */
/* Kernel time follows the virtual vblank clock (fakepvr.c), as on the
 * Dreamcast it follows the real one, so root counters (the AKAO sequencer)
 * keep pace with the frames however fast the host runs. DCHOST_WALL_CLOCK=1
 * uses the host's clock instead. */
uint64_t timer_us_gettime64(void);
uint64_t Psyz_OsNowUs(void) {
    static int wall = -1;
    if (wall < 0)
        wall = getenv("DCHOST_WALL_CLOCK") != NULL;
    if (!wall) /* each read moves it 1 us, as polling loops need */
        return timer_us_gettime64();
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}
uintptr_t Psyz_OsThreadSelf(void) { return (uintptr_t)pthread_self(); }
void *Psyz_OsLockCreate(void) {
    pthread_mutex_t *m = malloc(sizeof(*m)); pthread_mutex_init(m, NULL); return m;
}
void Psyz_OsLock(void *l) { pthread_mutex_lock(l); }
void Psyz_OsUnlock(void *l) { pthread_mutex_unlock(l); }
typedef struct { pthread_mutex_t m; pthread_cond_t c; int rung; } Bell;
void *Psyz_OsBellCreate(void) {
    Bell *b = calloc(1, sizeof(*b)); pthread_mutex_init(&b->m, NULL);
    pthread_condattr_t a; pthread_condattr_init(&a); pthread_condattr_setclock(&a, CLOCK_MONOTONIC);
    pthread_cond_init(&b->c, &a); return b;
}
void Psyz_OsBellRing(void *p) {
    Bell *b = p; pthread_mutex_lock(&b->m); b->rung = 1; pthread_cond_signal(&b->c); pthread_mutex_unlock(&b->m);
}
void Psyz_OsBellWait(void *p, uint32_t timeout_us) {
    Bell *b = p; pthread_mutex_lock(&b->m);
    if (timeout_us == PSYZ_OS_FOREVER) {
        while (!b->rung) pthread_cond_wait(&b->c, &b->m);
    } else {
        struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t ns = (uint64_t)ts.tv_nsec + (uint64_t)timeout_us * 1000u;
        ts.tv_sec += (time_t)(ns / 1000000000u); ts.tv_nsec = (long)(ns % 1000000000u);
        while (!b->rung && pthread_cond_timedwait(&b->c, &b->m, &ts) != ETIMEDOUT) {}
    }
    b->rung = 0; pthread_mutex_unlock(&b->m);
}
static void *Entry(void *fn) { ((void (*)(void))fn)(); return NULL; }
int Psyz_OsThreadStart(void (*fn)(void), const char *name) {
    (void)name; pthread_t t;
    if (pthread_create(&t, NULL, Entry, (void *)fn)) return -1;
    pthread_detach(t); return 0;
}
