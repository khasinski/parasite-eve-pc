#pragma once
#include <stdint.h>
/* virtual clock: advances one frame per vid_waitvbl() */
uint64_t timer_us_gettime64(void);
uint64_t timer_ns_gettime64(void);
