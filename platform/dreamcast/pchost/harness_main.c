/* Host harness: runs the Dreamcast build of the game (dc_main.c, the PsyZ
 * Dreamcast GPU backend) on a 32-bit PC with a software PVR.
 * usage: dchost LIMIT [VBLANK:BUTTONS ...]  (PSY-Q bits: 8 Start, 16 Up,
 * 16384 Cross). Frames: DCHOST_DIR, DCHOST_EVERY=N or DCHOST_SAVE=a,b,c */
#include <stdio.h>
#include <stdlib.h>
#include "common.h"
#include "pe1/boot.h"
int dc_main(int argc, char **argv);
int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s LIMIT [VBLANK:BUTTONS ...]\n", argv[0]); return 2; }
    g_BootHostVBlankLimit = (u32)strtoul(argv[1], NULL, 10);
    g_BootHostPadEvents = calloc((size_t)argc, sizeof(*g_BootHostPadEvents));
    for (int i = 2; i < argc; i++) {
        char *end;
        unsigned long vb = strtoul(argv[i], &end, 10);
        unsigned long b = strtoul(end + 1, NULL, 0);
        g_BootHostPadEvents[g_BootHostPadEventCount++] = (BootHostPadEvent){(u32)vb, (u16)b};
    }
    return dc_main(argc, argv);
}
