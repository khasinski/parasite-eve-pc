#include "pe_fonts_sheet.h"

#include <string.h>

#define CELL PE_FONT_CELL

int pe_font_sheet_read(const unsigned char *rgba, unsigned width, unsigned height, unsigned pitch,
                       PeFontSheet *out) {
    int any = 0;
    if ((width != 252 && width != 256) || height < 48 || height > 256 || pitch < width * 4) return -1;
    memset(out, 0, sizeof *out);
    for (unsigned code = 0; code < PE_FONT_CODES; ++code) {
        const unsigned cx = code % PE_FONT_COLUMNS * CELL, cy = code / PE_FONT_COLUMNS * CELL;
        int first = -1, last = -1, marker = -1;
        for (unsigned y = 0; y < CELL; ++y) {
            const unsigned char *row = rgba + (cy + y) * pitch + cx * 4;
            for (unsigned x = 0; x < CELL; ++x) {
                const unsigned char *p = row + x * 4;
                if (p[3] < 128) continue;
                if (p[0] == 255 && p[1] == 0 && p[2] == 255) {
                    marker = (int)x;
                    continue;
                }
                out->cells[code][y * CELL + x] = (p[0] * 3u + p[1] * 6u + p[2]) / 10u >= 128 ? 1 : 2;
                if (first < 0 || (int)x < first) first = (int)x;
                if ((int)x > last) last = (int)x;
            }
        }
        if (last >= 0) {
            any = 1;
            out->metric[code] = (unsigned char)((unsigned)(last - first + 1) << 4 | (unsigned)first);
        }
        if (marker >= 0) out->advance[code] = (unsigned char)(marker + 1);
        else if (last >= 0) out->advance[code] = (unsigned char)(last + 1);
        else if (code == PE_FONT_SPACE) out->advance[code] = PE_FONT_SPACE_ADVANCE;
    }
    return any ? 0 : -1;
}

void pe_font_sheet_write(const PeFontSheet *sheet, unsigned char *pixels, unsigned stride) {
    for (unsigned code = 0; code < PE_FONT_CODES; ++code) {
        const unsigned cx = code % PE_FONT_COLUMNS * CELL, cy = code / PE_FONT_COLUMNS * CELL;
        for (unsigned i = 0; i < CELL * CELL; ++i) {
            const unsigned x = cx + i % CELL, y = cy + i / CELL;
            unsigned char *b = &pixels[y * stride + x / 2];
            const unsigned v = sheet->cells[code][i];
            *b = x & 1 ? (unsigned char)((*b & 0x0F) | v << 4) : (unsigned char)((*b & 0xF0) | v);
        }
    }
}
