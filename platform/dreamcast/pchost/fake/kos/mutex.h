#pragma once
#define _GNU_SOURCE 1
#include <pthread.h>
typedef pthread_mutex_t mutex_t;
#define RECURSIVE_MUTEX_INITIALIZER PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP
static inline int mutex_lock(mutex_t *m) { return pthread_mutex_lock(m); }
static inline int mutex_unlock(mutex_t *m) { return pthread_mutex_unlock(m); }
