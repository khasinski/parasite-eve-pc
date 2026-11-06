#!/usr/bin/env python3
"""Compile actual PsyZ VSync policy and clocks against controlled displays.

SDL's dummy driver cannot emulate two independently refreshed displays or an
early-returning swapchain. Control display queries and time; execute the actual
policy, frame-wait and SDK VSync bodies without opening any windows.
Long runs retain the 59.94 Hz clock across display changes and late presents.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

source_path = Path(sys.argv[1])
source = source_path.read_text()
def production_function(signature, source=source):
    start = source.index(signature)
    brace = source.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

policy = production_function('static void ConfigureVSync(double target_fps)')
wait = production_function('static void WaitForNextFrame(void)')
elapsed = production_function('static double GetElapsedMicroseconds(Uint64 start, Uint64 end)')
clock = production_function('static Uint32 WallVblankCount(void)')
present = production_function('int Psyz_VideoVSync(int mode)')
skip = production_function('static bool SkipPresent(void)')
api = (source_path.parent.parent/'psyz/libapi.c').read_text()
vsync = production_function('int VSync(int mode)', api)
stubs = r'''
#include <assert.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
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
typedef uint64_t Uint64;
typedef uint32_t Uint32;
static Uint64 clock_us, perf_frequency=1000000, last_frame_time, finish_time;
static double drift_compensation, driver_frame_time_us, seed_drift;
static double target_frame_rate=59.94;
static double target_frame_time_us=1000000.0/59.94;
static unsigned game_speed=1;
static bool frame_presented;
static struct {
    double last_frame_time_us, last_draw_time_us, target_frame_time_us;
    unsigned long long total_frames;
    bool using_driver_vsync;
} gpu_stats;
static Uint64 SDL_GetPerformanceCounter(void) { return ++clock_us; }
static void SDL_DelayNS(Uint64 ns) { clock_us+=ns/1000; }
'''
clock_stubs = r'''
static Uint32 last_vsync_us, vblank_clock_offset, host_clock_pause_count;
static unsigned host_clock_pause_depth;
static Uint32 limitless_vblank_base;
static Uint64 limitless_frame_base, last_present_time;
static bool present_skip, is_pal;
static unsigned last_vsync_count, pad_count, kernel_count, callback_count;
static void (*g_PsyzVsyncCb)(void);
static Uint64 SDL_GetTicksNS(void) { return clock_us*1000; }
static void Sdl3Common_ApplyPendingTimingReset(void) {}
static bool periodic_stalls;
static Uint64 pending_present_work_us;
static void PlatformBackend_Present(void) {
    frame_presented=true;
    clock_us+=pending_present_work_us;pending_present_work_us=0;
}
static void PollEvents(void) {}
static void Sdl3Audio_VBlank(bool pal) { (void)pal; }
static void Psyz_KernelPoll(void) {}
static void ReadPadsOnVsync(void) { ++pad_count; }
static void Psyz_KernelVBlank(void) { ++kernel_count; }
static void counted_callback(void) { ++callback_count; }
'''
cases = r'''
static void pace(double refresh, unsigned speed, bool presented, int mode,
                 double work_us, double expected_us) {
    modes[2].refresh_rate=refresh;vsync_mode=mode;game_speed=speed;
    frame_presented=presented;ConfigureVSync(59.94);
    drift_compensation=seed_drift;last_frame_time=1000000;
    finish_time=last_frame_time+(Uint64)work_us;clock_us=finish_time;
    WaitForNextFrame();
    double interval=gpu_stats.last_frame_time_us;
    printf("refresh=%.5f speed=%u presented=%d mode=%d work=%.1f interval=%.1f\n",
           refresh,speed,presented,mode,work_us,interval);
    assert(interval>=expected_us-2 && interval<expected_us+10);
    assert(target_frame_time_us==1000000.0/59.94);
}

static void clock_run(double refresh, int mode, unsigned frames) {
    modes[2].refresh_rate=refresh;vsync_mode=mode;game_speed=1;
    ConfigureVSync(59.94);
    Uint32 start=WallVblankCount();
    unsigned start_callbacks=callback_count;
    Uint64 start_us=clock_us;
    unsigned long long start_presents=gpu_stats.total_frames;
    for(unsigned i=0;i<frames;++i) {
        unsigned periodic_origin=last_vsync_count;
        unsigned queries=(unsigned)VSync(-1);
        (void)VSync(1);
        assert(last_vsync_count==periodic_origin);
        assert((unsigned)VSync(-1)==queries);
        /* A late frame must not become the origin for every later wait. */
        if(periodic_stalls && i%100==0)
            pending_present_work_us=(Uint64)(target_frame_time_us*2.2);
        VSync(2);
    }
    unsigned blanks=WallVblankCount()-start;
    assert(callback_count-start_callbacks==blanks);
    assert(pad_count==callback_count && kernel_count==callback_count);
    assert(target_frame_rate==59.94);
    printf("long-run blanks=%u expected=%u stalled=%d\n",
           blanks,frames*2,periodic_stalls);
    fflush(stdout);
    assert(blanks>=frames*2 && blanks<=frames*2+2);
    double elapsed=(double)(clock_us-start_us);
    double nominal=frames*2*1000000.0/59.94;
    assert(fabs(elapsed-nominal)<2*1000000.0/59.94);
    printf("clock refresh=%.5f mode=%d calls=%u blanks=%u presents=%llu elapsed_us=%.0f nominal_us=%.0f\n",
           refresh,mode,frames,blanks,gpu_stats.total_frames-start_presents,elapsed,nominal);
}

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
    assert(use_driver_vsync && requested_vsync && queries==before+1);
    before=queries;
    vsync_mode=PSYZ_VSYNC_OFF;ConfigureVSync(59.94);
    assert(!use_driver_vsync && !requested_vsync && queries==before);
    vsync_mode=PSYZ_VSYNC_LIMITLESS;ConfigureVSync(59.94);
    assert(!use_driver_vsync && !requested_vsync && queries==before);
    puts("VSync policy: window monitor, fallback, PAL, rejected driver and explicit modes passed");
    pace(60,1,true,PSYZ_VSYNC_AUTO,14300,1000000.0/60);
    pace(60,1,true,PSYZ_VSYNC_AUTO,16620,1000000.0/60);
    pace(59.94,1,true,PSYZ_VSYNC_AUTO,14300,1000000.0/59.94);
    pace(120,1,true,PSYZ_VSYNC_ON,4000,1000000.0/120);
    pace(144,1,true,PSYZ_VSYNC_AUTO,4000,1000000.0/59.94);
    pace(60,2,true,PSYZ_VSYNC_AUTO,4000,1000000.0/59.94/2);
    pace(60,1,false,PSYZ_VSYNC_AUTO,4000,1000000.0/59.94);
    pace(60,1,true,PSYZ_VSYNC_OFF,4000,1000000.0/59.94);
    pace(60,1,true,PSYZ_VSYNC_LIMITLESS,4000,4000);
    pace(60,1,true,PSYZ_VSYNC_AUTO,20000,20000);
    driver_accepts=false;
    pace(60,1,true,PSYZ_VSYNC_AUTO,4000,1000000.0/59.94);
    pace(120,1,true,PSYZ_VSYNC_ON,4000,1000000.0/59.94);
    driver_accepts=true;
    /* Switching from a late software-paced frame must not cause a burst. */
    seed_drift=-5000;
    pace(60,1,true,PSYZ_VSYNC_AUTO,4000,1000000.0/60);
    seed_drift=-1000;
    pace(60,1,true,PSYZ_VSYNC_OFF,4000,1000000.0/59.94-1000);
    seed_drift=0;
    /* Three virtual minutes per display, then live display changes without
     * resetting the original VSync(n) schedule or callback counters. */
    clock_us=last_frame_time=finish_time=0;last_vsync_count=0;
    drift_compensation=0;g_PsyzVsyncCb=counted_callback;
    clock_run(60,PSYZ_VSYNC_AUTO,5400);
    clock_run(59.94,PSYZ_VSYNC_AUTO,5400);
    clock_run(120,PSYZ_VSYNC_ON,5400);
    clock_run(144,PSYZ_VSYNC_AUTO,5400);
    periodic_stalls=true;
    clock_run(60,PSYZ_VSYNC_AUTO,5400);
    puts("VSync queue pacing: early driver return, refresh changes, late frames, fast-forward, minimized and limitless passed");
}
'''
with tempfile.TemporaryDirectory(prefix='pe-vsync-displays-') as tmp:
    path = Path(tmp)
    (path/'policy.c').write_text(stubs+elapsed+policy+wait+clock_stubs+clock+skip+present+vsync+cases)
    binary = path/'policy-test'
    if sys.platform == 'win32':
        binary = binary.with_suffix('.exe')
    subprocess.run([sys.argv[2], '-std=c11', str(path/'policy.c'), '-o', str(binary), '-lm'], check=True)
    subprocess.run([str(binary)], check=True)
