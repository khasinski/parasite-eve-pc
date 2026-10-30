/* Compile the actual backend with only SDL failure boundaries intercepted.
 * Normal calls still use the real hidden GPU device and resource lifetimes. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <SDL3/SDL.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static bool fail_map, fail_wait, fail_submit, fail_acquire;
static unsigned map_calls, texture_fail_after;
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
#define SDL_CreateGPUTexture test_texture
#define SDL_AcquireGPUCommandBuffer test_acquire
#define SDL_MapGPUTransferBuffer test_map
#define SDL_WaitForGPUFences test_wait
#define SDL_SubmitGPUCommandBufferAndAcquireFence test_submit
#include "../third_party/psyz/psyz/src/platform/sdl3_gpu.c"
#undef SDL_CreateGPUTexture
#undef SDL_AcquireGPUCommandBuffer
#undef SDL_MapGPUTransferBuffer
#undef SDL_WaitForGPUFences
#undef SDL_SubmitGPUCommandBufferAndAcquireFence
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
        }
        texture_fail_after=2;
        Psyz_VideoSetAntialiasing(1);
        assert(!Psyz_VideoGetAntialiasing() && Psyz_VideoGetInternalResolution()==2);
        Psyz_VideoSetInternalResolution(4);
        assert(internal_res==4 && scaled_vram_render!=old_render);
        puts("Failed first/second target allocations retain working pixels, AA and scale; retry succeeds");
    }else if(!strcmp(argv[1],"--swapchain-resize")){
        /* Claim the existing hidden window; no test may show it. */
        assert(SDL_GetWindowFlags(sdl3_window) & SDL_WINDOW_HIDDEN);
        swapchain_ok=SDL_ClaimWindowForGPUDevice(device,sdl3_window);
        assert(swapchain_ok);
        SDL_GetWindowSizeInPixels(sdl3_window,&swapchain_window_w,&swapchain_window_h);
        for(unsigned i=0;i<8;++i){
            PS1_RECT region={0,0,8,8};Draw_ClearImage(&region,255,0,0);
            assert(SDL_SetWindowSize(sdl3_window,640+(i%2)*160,480+(i%2)*120));
            SDL_SyncWindow(sdl3_window);
            assert(RefreshWindowSwapchain());
            /* SDL also invalidates its views on a repeated size event. */
            SDL_Event event={0};event.type=SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
            event.window.windowID=SDL_GetWindowID(sdl3_window);
            assert(WatchSwapchainResize(NULL,&event));
            assert(SDL_GetAtomicInt(&swapchain_resize_pending));
            assert(RefreshWindowSwapchain());
            assert(swapchain_ok && !pending_cmd);
            int w,h;SDL_GetWindowSizeInPixels(sdl3_window,&w,&h);
            assert(w==swapchain_window_w && h==swapchain_window_h);
            unsigned char pixel[4]={0};
            assert(DownloadVramRegionAsRGBA8888(0,0,1,1,pixel));
            assert(pixel[0]>200 && !pixel[1] && !pixel[2]);
            assert(SDL_GetWindowFlags(sdl3_window)&SDL_WINDOW_HIDDEN);
        }
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
