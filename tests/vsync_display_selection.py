#!/usr/bin/env python3
"""Compile the actual PsyZ VSync policy against controlled monitor identities.

SDL's dummy driver cannot emulate two independently refreshed displays. Stub only
its display queries and the backend setter; execute the production policy body.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

source = Path(sys.argv[1]).read_text()
start = source.index('static void ConfigureVSync(double target_fps)')
brace = source.index('{', start)
end, depth = brace + 1, 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
policy = source[start:end]
stubs = r'''
#include <assert.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#define INFOF(...) ((void)0)
typedef unsigned SDL_DisplayID;
typedef struct { double refresh_rate; } SDL_DisplayMode;
typedef struct { int unused; } SDL_Window;
enum {PSYZ_VSYNC_AUTO, PSYZ_VSYNC_ON, PSYZ_VSYNC_OFF, PSYZ_VSYNC_LIMITLESS};
static SDL_Window window;
static SDL_Window *sdl3_window = &window;
static int vsync_mode = PSYZ_VSYNC_AUTO;
static bool use_driver_vsync, driver_accepts=true, requested_vsync;
static unsigned last_display, queries;
static SDL_DisplayID window_display=2;
static SDL_DisplayMode modes[3]={{0},{120},{59.94}};
static SDL_DisplayID SDL_GetPrimaryDisplay(void) { return 1; }
static SDL_DisplayID SDL_GetDisplayForWindow(SDL_Window *w) { assert(w==&window);return window_display; }
static const SDL_DisplayMode *SDL_GetCurrentDisplayMode(SDL_DisplayID id) {
    assert(id==1 || id==2);last_display=id;++queries;return &modes[id];
}
static bool PlatformBackend_SetDriverVsync(bool enabled) {
    requested_vsync=enabled;return enabled && driver_accepts;
}
'''
cases = r'''
int main(void) {
    ConfigureVSync(59.94);
    assert(last_display==2 && use_driver_vsync && requested_vsync);
    modes[1].refresh_rate=59.94;modes[2].refresh_rate=144;
    ConfigureVSync(59.94);
    assert(last_display==2 && !use_driver_vsync && !requested_vsync);
    window_display=0;ConfigureVSync(59.94);
    assert(last_display==1 && use_driver_vsync);
    sdl3_window=NULL;ConfigureVSync(59.94);
    assert(last_display==1 && use_driver_vsync);
    sdl3_window=&window;window_display=2;modes[2].refresh_rate=50;
    ConfigureVSync(50);assert(use_driver_vsync);
    ConfigureVSync(59.94);assert(!use_driver_vsync);
    modes[2].refresh_rate=59.94;driver_accepts=false;
    ConfigureVSync(59.94);assert(!use_driver_vsync && !requested_vsync);
    driver_accepts=true;unsigned before=queries;
    vsync_mode=PSYZ_VSYNC_ON;ConfigureVSync(59.94);
    assert(use_driver_vsync && requested_vsync && queries==before);
    vsync_mode=PSYZ_VSYNC_OFF;ConfigureVSync(59.94);
    assert(!use_driver_vsync && !requested_vsync && queries==before);
    vsync_mode=PSYZ_VSYNC_LIMITLESS;ConfigureVSync(59.94);
    assert(!use_driver_vsync && !requested_vsync && queries==before);
    puts("VSync policy: window monitor, fallback, PAL, rejected driver and explicit modes passed");
}
'''
with tempfile.TemporaryDirectory(prefix='pe-vsync-displays-') as tmp:
    path = Path(tmp)
    (path/'policy.c').write_text(stubs+policy+cases)
    binary = path/'policy-test'
    if sys.platform == 'win32':
        binary = binary.with_suffix('.exe')
    subprocess.run([sys.argv[2], '-std=c11', str(path/'policy.c'), '-o', str(binary), '-lm'], check=True)
    subprocess.run([str(binary)], check=True)
