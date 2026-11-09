#pragma once
#include <stdbool.h>
#include <stdint.h>
#define BFONT_THIN_WIDTH 12
#define BFONT_HEIGHT 24
void bfont_draw_str_ex(void *b, uint32_t width, uint32_t fg, uint32_t bg,
                       uint8_t bpp, bool opaque, const char *str);
