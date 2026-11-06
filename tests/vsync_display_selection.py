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
present_pace = production_function('static void PaceDriverPresent(void)')
elapsed = production_function('static double GetElapsedMicroseconds(Uint64 start, Uint64 end)')
clock = production_function('static Uint32 WallVblankCount(void)')
present = production_function('int Psyz_VideoVSync(int mode)')
skip = production_function('static bool SkipPresent(void)')
api = (source_path.parent.parent/'psyz/libapi.c').read_text()
vsync = production_function('int VSync(int mode)', api)
gpu = source_path.with_name('sdl3_gpu.c').read_text()
submit = production_function('static bool SubmitCmd(void) {', gpu)
submit_frame = production_function('static void SubmitFrame(void) {', gpu)
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
static Uint64 last_driver_submit;
static bool driver_submit_valid, driver_present_pending;
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
static bool periodic_stalls, exercise_submit_clock;
static void reset_submit(void);
static void SubmitFrame(void);
static Uint64 pending_present_work_us;
static void PlatformBackend_Present(void) {
    frame_presented=true;
    clock_us+=pending_present_work_us;pending_present_work_us=0;
    if(exercise_submit_clock) {
        clock_us+=(gpu_stats.total_frames%2)?4000:1000;
        reset_submit();SubmitFrame();
    }
}
static void PollEvents(void) {}
static void Sdl3Audio_VBlank(bool pal) { (void)pal; }
static void Psyz_KernelPoll(void) {}
static void ReadPadsOnVsync(void) { ++pad_count; }
static void Psyz_KernelVBlank(void) { ++kernel_count; }
static void counted_callback(void) { ++callback_count; }
'''
submit_stubs = r'''
typedef struct { int id; } SDL_GPUCommandBuffer;
typedef struct { int id; } SDL_GPUFence;
static SDL_GPUCommandBuffer command, *pending_cmd, *quarantined_cmd;
static SDL_GPUFence fence, *frame_fences[2];
#define MAX_FRAMES_IN_FLIGHT 2
static unsigned frame_fence_idx, submit_calls, commit_count, released_fences;
static bool gpu_failed, swapchain_ok, present_commit_pending;
static bool submit_ok, wait_ok, fence_ok;
static Uint64 actual_submit;
static void *device;
static bool GpuFail(const char *stage, bool uncertain) {
    (void)stage;assert(uncertain);gpu_failed=true;return false;
}
static bool SDL_SubmitGPUCommandBuffer(SDL_GPUCommandBuffer *cmd) {
    assert(cmd==&command);++submit_calls;actual_submit=clock_us;return submit_ok;
}
static SDL_GPUFence *SDL_SubmitGPUCommandBufferAndAcquireFence(SDL_GPUCommandBuffer *cmd) {
    assert(cmd==&command);++submit_calls;actual_submit=clock_us;return fence_ok?&fence:NULL;
}
static bool SDL_WaitForGPUFences(void *dev, bool all, SDL_GPUFence **f, int count) {
    (void)dev;assert(all && count==1 && *f==&fence);return wait_ok;
}
static void SDL_ReleaseGPUFence(void *dev, SDL_GPUFence *f) {
    (void)dev;assert(f==&fence);++released_fences;
}
static void RecordPresentedCommit(void) {
    if(present_commit_pending){++commit_count;present_commit_pending=false;}
}
static void reset_submit(void) {
    pending_cmd=&command;quarantined_cmd=NULL;gpu_failed=false;
    swapchain_ok=frame_presented=present_commit_pending=driver_present_pending=true;
    frame_fence_idx=submit_calls=commit_count=released_fences=0;
    frame_fences[0]=frame_fences[1]=NULL;submit_ok=wait_ok=fence_ok=true;
}
'''
submit_cases = r'''
static void check_submit_spacing(void) {
    modes[2].refresh_rate=60;vsync_mode=PSYZ_VSYNC_AUTO;game_speed=1;
    ConfigureVSync(59.94);clock_us=last_frame_time=finish_time=1000000;
    Uint64 previous=0;
    double minimum=1e9;
    for(unsigned i=0;i<30;++i) {
        clock_us+=(i%2)?4000:1000;
        reset_submit();SubmitFrame();
        assert(!driver_present_pending);
        if(i) {
            double delta=(double)(actual_submit-previous);
            if(delta<minimum)minimum=delta;
            assert(delta>=driver_frame_time_us);
        }
        previous=actual_submit;WaitForNextFrame();
    }
    Uint64 last=last_driver_submit,start=clock_us;
    reset_submit();driver_present_pending=false;
    swapchain_ok=frame_presented=false;SubmitFrame();
    assert(clock_us==start && last_driver_submit==last);
    game_speed=2;start=clock_us;reset_submit();SubmitFrame();
    assert(clock_us==start && !driver_submit_valid);
    game_speed=1;ConfigureVSync(59.94);
    assert(!driver_submit_valid);
    start=clock_us;reset_submit();SubmitFrame();
    assert(clock_us==start+1); /* first submit only records its timestamp */
    vsync_mode=PSYZ_VSYNC_LIMITLESS;start=clock_us;
    reset_submit();SubmitFrame();assert(clock_us==start && !driver_submit_valid);
    printf("GPU submissions: alternating CPU work minimum %.0f us; offscreen, fast-forward, reset and limitless passed\n",minimum);
}
static void check_submit_state(void) {
    reset_submit();SubmitFrame();
    assert(submit_calls==1 && commit_count==1 && !pending_cmd && !gpu_failed && frame_presented && !present_commit_pending);
    reset_submit();submit_ok=false;SubmitFrame();
    assert(submit_calls==1 && !commit_count && !pending_cmd && quarantined_cmd==&command && gpu_failed && !present_commit_pending);
    assert(!frame_presented && "A failed GPU submit must not count as a presented frame");
    reset_submit();swapchain_ok=frame_presented=present_commit_pending=driver_present_pending=false;SubmitFrame();
    assert(submit_calls==1 && !commit_count && !gpu_failed && frame_fences[0]==&fence && frame_fence_idx==1 && !frame_presented);
    reset_submit();swapchain_ok=frame_presented=present_commit_pending=driver_present_pending=false;
    frame_fences[0]=&fence;wait_ok=false;SubmitFrame();
    assert(!submit_calls && !released_fences && gpu_failed && pending_cmd==&command && frame_fences[0]==&fence);
    reset_submit();gpu_failed=true;SubmitFrame();assert(!submit_calls && pending_cmd==&command);
    puts("GPU submit: failed present cleared; success/fence/failed-wait ownership preserved");
}
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
    check_submit_state();
    check_submit_spacing();
    exercise_submit_clock=true;periodic_stalls=false;
    /* The isolated spacing case resets virtual time; rebase its next wait. */
    last_vsync_count=WallVblankCount()+vblank_clock_offset;
    clock_run(60,PSYZ_VSYNC_AUTO,5400);
    clock_run(59.94,PSYZ_VSYNC_AUTO,5400);
    clock_run(120,PSYZ_VSYNC_ON,5400);
    clock_run(144,PSYZ_VSYNC_AUTO,5400);
    puts("VSync queue pacing: early driver return, refresh changes, late frames, fast-forward, minimized and limitless passed");
}
'''
with tempfile.TemporaryDirectory(prefix='pe-vsync-displays-') as tmp:
    path = Path(tmp)
    (path/'policy.c').write_text(stubs+elapsed+policy+present_pace+wait+clock_stubs+clock+skip+present+vsync+
                               submit_stubs+submit+submit_frame+submit_cases+cases)
    binary = path/'policy-test'
    if sys.platform == 'win32':
        binary = binary.with_suffix('.exe')
    subprocess.run([sys.argv[2], '-std=c11', str(path/'policy.c'), '-o', str(binary), '-lm'], check=True)
    subprocess.run([str(binary)], check=True)
