/* Compile the actual backend with only SDL failure boundaries intercepted.
 * Normal calls still use the real hidden GPU device and resource lifetimes. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <SDL3/SDL.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static bool fail_map, fail_wait, fail_submit, fail_acquire, fail_swapchain_params;
static unsigned map_calls, texture_fail_after, swapchain_param_calls, overlay_frames;
static void count_overlay_frame(void) { ++overlay_frames; }
static SDL_GPUTexture *test_texture(SDL_GPUDevice *device, const SDL_GPUTextureCreateInfo *info) {
    if (texture_fail_after && !--texture_fail_after) {
        SDL_SetError("Injected GPU allocation failure");
        return NULL;
    }
    return SDL_CreateGPUTexture(device, info);
}
static SDL_GPUCommandBuffer *test_acquire(SDL_GPUDevice *device) {
    if(fail_acquire)return NULL;
    return SDL_AcquireGPUCommandBuffer(device);
}
static void *test_map(SDL_GPUDevice *device,SDL_GPUTransferBuffer *buffer,bool cycle) {
    ++map_calls;
    if(fail_map)return NULL;
    return SDL_MapGPUTransferBuffer(device,buffer,cycle);
}
static bool test_wait(SDL_GPUDevice *device,bool all,SDL_GPUFence *const *fences,Uint32 count) {
    bool done=SDL_WaitForGPUFences(device,all,fences,count);
    return fail_wait?false:done;
}
static SDL_GPUFence *test_submit(SDL_GPUCommandBuffer *cmd) {
    if(fail_submit){SDL_CancelGPUCommandBuffer(cmd);return NULL;}
    return SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
}
static bool test_swapchain_params(
    SDL_GPUDevice *device, SDL_Window *window,
    SDL_GPUSwapchainComposition composition, SDL_GPUPresentMode mode) {
    ++swapchain_param_calls;
    if (fail_swapchain_params) {
        SDL_SetError("Injected swapchain parameter failure");
        return false;
    }
    return SDL_SetGPUSwapchainParameters(device, window, composition, mode);
}
#define SDL_CreateGPUTexture test_texture
#define SDL_AcquireGPUCommandBuffer test_acquire
#define SDL_MapGPUTransferBuffer test_map
#define SDL_WaitForGPUFences test_wait
#define SDL_SubmitGPUCommandBufferAndAcquireFence test_submit
#define SDL_SetGPUSwapchainParameters test_swapchain_params
#include "../third_party/psyz/psyz/src/platform/sdl3_gpu.c"
#undef SDL_CreateGPUTexture
#undef SDL_AcquireGPUCommandBuffer
#undef SDL_MapGPUTransferBuffer
#undef SDL_WaitForGPUFences
#undef SDL_SubmitGPUCommandBufferAndAcquireFence
#undef SDL_SetGPUSwapchainParameters
int main(int argc,char **argv) {
    assert(argc==2);
    if(!InitPlatform())return 77;
    Draw_ResetBuffer();
    if(!strcmp(argv[1],"--upload")){
        for(unsigned acquire=0;acquire<2;++acquire){
            assert(SubmitCmdAndWait());
            n_vertices=MAX_VERTEX_COUNT-4;vertex_cur=vertex_buf+n_vertices;
            fail_map=!acquire;fail_acquire=acquire;
            Draw_EnsureBufferWillNotOverflow(6,6);
            assert(!n_vertices && !n_indices && vertex_cur==vertex_buf && index_cur==index_buf);
            fail_map=fail_acquire=false;
        }
        puts("Failed upload cannot leave a full CPU batch for the next primitive");
    }else if(!strcmp(argv[1],"--readback")){
        unsigned char out[4]={0x5a,0x5a,0x5a,0x5a};
        for(unsigned submit=0;submit<2;++submit){
            fail_submit=submit;fail_wait=!submit;map_calls=0;
            assert(!DownloadVramRegionAsRGBA8888(0,0,1,1,out));
            assert(!map_calls && !pending_cmd);
            for(unsigned i=0;i<sizeof out;++i)assert(out[i]==0x5a);
        }
        fail_submit=fail_wait=false;
        puts("Failed submission/wait never maps or exposes unfinished VRAM data");
    }else if(!strcmp(argv[1],"--resize")){
        /* Initial high-res failure with PGXP enabled falls back to 1x scaled target. */
        set_geometry_correction=true;
        texture_fail_after=1;
        Psyz_VideoSetInternalResolution(4);
        assert(internal_res==1 && set_internal_res==1 && Psyz_VideoGetGeometryCorrection());
        assert(scaled_vram_render && scaled_blend_sample);

        Psyz_VideoSetInternalResolution(2);
        SDL_GPUTexture *old_render=scaled_vram_render,*old_sample=scaled_blend_sample;
        assert(old_render && old_sample);
        for(unsigned fail=1;fail<=2;++fail){
            texture_fail_after=fail;
            Psyz_VideoSetInternalResolution(4);
            assert(internal_res==2 && set_internal_res==2);
            assert(scaled_vram_render==old_render && scaled_blend_sample==old_sample);
            PS1_RECT region={0,0,8,8};Draw_ClearImage(&region,255,0,0);
            unsigned char pixel[4]={0};
            assert(DownloadVramRegionAsRGBA8888(0,0,1,1,pixel));
            assert(pixel[0]>200 && pixel[1]==0 && pixel[2]==0);
            int rw=0,rh=0;
            unsigned char *rendered=Psyz_VideoAllocRenderedFrame(&rw,&rh);
            assert(rendered && rw==display_size.x*2 && rh==display_size.y*2);
            assert(rendered[0]>200 && rendered[1]==0 && rendered[2]==0);
            free(rendered);
        }
        texture_fail_after=2;
        Psyz_VideoSetAntialiasing(1);
        assert(!Psyz_VideoGetAntialiasing() && Psyz_VideoGetInternalResolution()==2);
        Psyz_VideoSetAntialiasing(1);
        assert(Psyz_VideoGetAntialiasing() && Psyz_VideoGetInternalResolution()==2 && internal_res==4);
        /* Failed SSAA resolve target allocation must not return an un-downsampled quarter image. */
        for(unsigned fail=1;fail<=2;++fail){
            int rw=1,rh=1;
            texture_fail_after=fail;
            assert(!Psyz_VideoAllocRenderedFrame(&rw,&rh) && !rw && !rh);
        }
        {
            int rw=0,rh=0;
            unsigned char *rendered=Psyz_VideoAllocRenderedFrame(&rw,&rh);
            assert(rendered && rw==display_size.x*2 && rh==display_size.y*2);
            free(rendered);
        }
        Psyz_VideoSetAntialiasing(0);
        Psyz_VideoSetInternalResolution(4);
        assert(internal_res==4 && scaled_vram_render!=old_render);
        puts("Failed first/second target allocations retain working pixels, AA and scale; retry succeeds");
    }else if(!strcmp(argv[1],"--swapchain-resize")){
        /* Claim the existing hidden window; no test may show it. */
        assert(SDL_GetWindowFlags(sdl3_window) & SDL_WINDOW_HIDDEN);
        swapchain_ok=SDL_ClaimWindowForGPUDevice(device,sdl3_window);
        assert(swapchain_ok);
        swapchain_present_mode=SDL_GPU_PRESENTMODE_VSYNC;
        use_driver_vsync=true;
        SDL_GetWindowSizeInPixels(sdl3_window,&swapchain_window_w,&swapchain_window_h);
        for(unsigned i=0;i<8;++i){
            PS1_RECT region={0,0,8,8};Draw_ClearImage(&region,255,0,0);
            assert(SDL_SetWindowSize(sdl3_window,640+(i%2)*160,480+(i%2)*120));
            SDL_SyncWindow(sdl3_window);
            swapchain_param_calls=0;
            assert(RefreshWindowSwapchain());
            assert(swapchain_param_calls==0);
            /* SDL also invalidates its views on a repeated size event. */
            SDL_Event event={0};event.type=SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
            event.window.windowID=SDL_GetWindowID(sdl3_window);
            assert(WatchSwapchainResize(NULL,&event));
            assert(SDL_GetAtomicInt(&swapchain_resize_pending));
            assert(RefreshWindowSwapchain());
            /* Simulate a fullscreen flag change. */
            swapchain_window_fullscreen^=SDL_WINDOW_FULLSCREEN;
            assert(RefreshWindowSwapchain());
            assert(swapchain_ok && !pending_cmd);
            int w,h;SDL_GetWindowSizeInPixels(sdl3_window,&w,&h);
            assert(w==swapchain_window_w && h==swapchain_window_h);
            unsigned char pixel[4]={0};
            assert(DownloadVramRegionAsRGBA8888(0,0,1,1,pixel));
            assert(pixel[0]>200 && !pixel[1] && !pixel[2]);
            assert(SDL_GetWindowFlags(sdl3_window)&SDL_WINDOW_HIDDEN);
        }
        /* Failing present-mode reconfiguration must release the swapchain claim
         * and Present must still flush draws, run overlay_frame_cb, and update finish_time. */
        use_driver_vsync=false;
        fail_swapchain_params=true;
        SDL_SetAtomicInt(&swapchain_resize_pending,1);
        assert(!RefreshWindowSwapchain());
        assert(!swapchain_ok);
        Psyz_OverlayFrameCB(count_overlay_frame);
        overlay_frames=0;
        last_frame_time=SDL_GetPerformanceCounter();
        PS1_RECT region={0,0,8,8};Draw_ClearImage(&region,0,255,0);
        PlatformBackend_Present();
        assert(!pending_cmd && overlay_frames==1 && finish_time>=last_frame_time && !frame_presented);
        fail_swapchain_params=false;
        assert(RefreshWindowSwapchain() && swapchain_ok);
        puts("Hidden window swapchain recreation retires pending work and preserves game VRAM");
    }else if(!strcmp(argv[1],"--reset")){
        PS1_RECT region={0,0,8,8};
        Draw_ClearImage(&region,255,0,0);assert(pending_cmd);
        ResetPlatform();assert(!pending_cmd);
        assert(InitPlatform());
        Draw_ClearImage(&region,255,0,0);
        unsigned char pixel[4]={0};
        assert(DownloadVramRegionAsRGBA8888(0,0,1,1,pixel));
        assert(pixel[0]>200 && pixel[1]==0 && pixel[2]==0 && !pending_cmd);
        puts("Device reset forgets old command buffers and permits reinitialization");
    }else assert(0);
    QuitPlatform();return 0;
}
