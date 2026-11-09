#pragma once
#include <stdint.h>
typedef void (*asic_evt_handler)(uint32_t code, void *data);
int vblank_handler_add(asic_evt_handler hnd, void *data);
