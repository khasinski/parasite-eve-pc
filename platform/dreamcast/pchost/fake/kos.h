#pragma once
#define KOS_INIT_FLAGS(flags) static const int kos_init_flags_unused __attribute__((unused)) = 0
#define INIT_DEFAULT 0
#include <dc/video.h>
/* GD-ROM status: the harness has no drive (both images come from /pc) */
enum { CD_STATUS_BUSY = 0, CD_STATUS_PAUSED = 1, CD_STATUS_OPEN = 6, CD_STATUS_NO_DISC = 7 };
static inline int cdrom_get_status(int *status, int *disc_type) {
    *status = CD_STATUS_NO_DISC;
    *disc_type = 0;
    return 0;
}
