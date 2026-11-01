/* One textured quad over the game (the same SDL_GPU pipeline as the game's settings panel),
 * drawn from the PsyZ overlay render callback. The init / render / destroy callbacks each chain
 * to the one installed before, so the game's own overlay keeps working. */
#include <stdint.h>
#include <string.h>
#include <SDL3/SDL.h>
#include <psyz/overlay.h>
#include <psyz/overlay_sdl3_gpu.h>

#include "../../../../sdl3-src/src/render/gpu/shaders/tri_texture.vert.spv.h"
#include "../../../../sdl3-src/src/render/gpu/shaders/tri_texture.vert.msl.h"
#include "../../../../sdl3-src/src/render/gpu/shaders/tri_texture.vert.dxil.h"
#include "../../../../sdl3-src/src/render/gpu/shaders/texture_rgba.frag.spv.h"
#include "../../../../sdl3-src/src/render/gpu/shaders/texture_rgba.frag.msl.h"
#include "../../../../sdl3-src/src/render/gpu/shaders/texture_rgba.frag.dxil.h"

#include "pe_ui.h"

typedef struct { float x, y, r, g, b, a, u, v; } Vertex;

static SDL_Window *s_window;
static SDL_GPUDevice *s_device;
static SDL_GPUGraphicsPipeline *s_pipeline;
static SDL_GPUSampler *s_sampler;
static SDL_GPUTexture *s_texture;
static SDL_GPUBuffer *s_vertices;
static int s_tex_w, s_tex_h, s_win_w, s_win_h, s_visible;
static PsyzOverlayInitCB_SDL3GPU s_prev_init;
static PsyzOverlayRenderCB_SDL3GPU s_prev_render;
static PsyzOverlayDestroyCB s_prev_destroy;

static void pe_release(void) {
    if (s_device) {
        if (s_texture) SDL_ReleaseGPUTexture(s_device, s_texture);
        if (s_vertices) SDL_ReleaseGPUBuffer(s_device, s_vertices);
        if (s_sampler) SDL_ReleaseGPUSampler(s_device, s_sampler);
        if (s_pipeline) SDL_ReleaseGPUGraphicsPipeline(s_device, s_pipeline);
    }
    s_texture = NULL;
    s_vertices = NULL;
    s_sampler = NULL;
    s_pipeline = NULL;
    s_tex_w = s_tex_h = 0;
}

static SDL_GPUShader *pe_shader(SDL_GPUShaderStage stage, const unsigned char *spv, size_t spv_size,
                                const unsigned char *msl, size_t msl_size, const unsigned char *dxil,
                                size_t dxil_size, Uint32 samplers) {
    const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(s_device);
    SDL_GPUShaderCreateInfo info;
    SDL_zero(info);
    info.stage = stage;
    info.num_samplers = samplers;
    info.num_uniform_buffers = 1;
    info.entrypoint = "main";
    if (formats & SDL_GPU_SHADERFORMAT_SPIRV) {
        info.code = spv; info.code_size = spv_size; info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    } else if (formats & SDL_GPU_SHADERFORMAT_MSL) {
        info.code = msl; info.code_size = msl_size; info.format = SDL_GPU_SHADERFORMAT_MSL; info.entrypoint = "main0";
    } else if (formats & SDL_GPU_SHADERFORMAT_DXIL) {
        info.code = dxil; info.code_size = dxil_size; info.format = SDL_GPU_SHADERFORMAT_DXIL;
    } else {
        return NULL;
    }
    return SDL_CreateGPUShader(s_device, &info);
}

static int pe_pipeline(void) {
    const SDL_GPUTextureFormat format = SDL_GetGPUSwapchainTextureFormat(s_device, s_window);
    SDL_GPUShader *vs, *fs;
    SDL_GPUVertexBufferDescription vbd;
    SDL_GPUVertexAttribute attr[3];
    SDL_GPUColorTargetDescription color;
    SDL_GPUGraphicsPipelineCreateInfo info;
    SDL_GPUSamplerCreateInfo sampler;
    SDL_GPUBufferCreateInfo buffer;

    if (s_pipeline && s_sampler && s_vertices) return 1;
    if (format == SDL_GPU_TEXTUREFORMAT_INVALID) return 0;
    vs = pe_shader(SDL_GPU_SHADERSTAGE_VERTEX, tri_texture_vert_spv, sizeof tri_texture_vert_spv, tri_texture_vert_msl,
                   sizeof tri_texture_vert_msl, tri_texture_vert_dxil, sizeof tri_texture_vert_dxil, 0);
    fs = pe_shader(SDL_GPU_SHADERSTAGE_FRAGMENT, texture_rgba_frag_spv, sizeof texture_rgba_frag_spv, texture_rgba_frag_msl,
                   sizeof texture_rgba_frag_msl, texture_rgba_frag_dxil, sizeof texture_rgba_frag_dxil, 1);
    if (!vs || !fs) {
        if (vs) SDL_ReleaseGPUShader(s_device, vs);
        if (fs) SDL_ReleaseGPUShader(s_device, fs);
        return 0;
    }
    SDL_zero(vbd);
    vbd.pitch = sizeof(Vertex);
    vbd.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    attr[0] = (SDL_GPUVertexAttribute){0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 0};
    attr[1] = (SDL_GPUVertexAttribute){1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 2 * sizeof(float)};
    attr[2] = (SDL_GPUVertexAttribute){2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 6 * sizeof(float)};
    SDL_zero(color);
    color.format = format;
    color.blend_state.enable_blend = true;
    color.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    color.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    color.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    color.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    SDL_zero(info);
    info.vertex_shader = vs;
    info.fragment_shader = fs;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_buffer_descriptions = &vbd;
    info.vertex_input_state.num_vertex_attributes = 3;
    info.vertex_input_state.vertex_attributes = attr;
    info.target_info.color_target_descriptions = &color;
    info.target_info.num_color_targets = 1;
    s_pipeline = SDL_CreateGPUGraphicsPipeline(s_device, &info);
    SDL_ReleaseGPUShader(s_device, vs);
    SDL_ReleaseGPUShader(s_device, fs);
    SDL_zero(sampler);
    sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
    sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    s_sampler = SDL_CreateGPUSampler(s_device, &sampler);
    SDL_zero(buffer);
    buffer.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    buffer.size = sizeof(Vertex) * 12;
    s_vertices = SDL_CreateGPUBuffer(s_device, &buffer);
    if (!s_pipeline || !s_sampler || !s_vertices) {
        pe_release();
        return 0;
    }
    return 1;
}

static void pe_quad(Vertex *v, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, float a) {
    v[0] = (Vertex){x0, y0, 1, 1, 1, a, u0, v0};
    v[1] = (Vertex){x1, y0, 1, 1, 1, a, u1, v0};
    v[2] = (Vertex){x0, y1, 1, 1, 1, a, u0, v1};
    v[3] = v[2];
    v[4] = v[1];
    v[5] = (Vertex){x1, y1, 1, 1, 1, a, u1, v1};
}

int pe_ui_gpu_ready(void) {
    if (!s_device || !s_window) return 0;
    /* Confirm actual GPU pipeline resources before a modal panel pauses
     * the game and captures its controller. No invisible modal on failure. */
    if (!pe_pipeline()) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Native GPU mod overlay unavailable: %s", SDL_GetError());
        return 0;
    }
    return 1;
}

int pe_ui_gpu_window_size(int *w, int *h) {
    *w = *h = 0;
    if (!s_window) return 0;
    SDL_GetWindowSizeInPixels(s_window, w, h);
    return *w > 0 && *h > 0;
}

int pe_ui_gpu_show(const PeSurface *s, int x, int y, float dim) {
    Vertex verts[12];
    float w, h;
    const Uint32 tex_bytes = (Uint32)((size_t)s->w * (size_t)s->h * 4u), vtx_bytes = sizeof verts;
    SDL_GPUTransferBufferCreateInfo ti;
    SDL_GPUTransferBuffer *transfer;
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUCopyPass *copy;
    SDL_GPUTextureTransferInfo tsrc;
    SDL_GPUTextureRegion tdst;
    SDL_GPUTransferBufferLocation vsrc;
    SDL_GPUBufferRegion vdst;
    uint8_t *mapped;

    if (!pe_ui_gpu_ready() || !s->pixels || !pe_ui_gpu_window_size(&s_win_w, &s_win_h) || !pe_pipeline()) return 0;
    if (!s_texture || s_tex_w != s->w || s_tex_h != s->h) {
        SDL_GPUTextureCreateInfo info;
        if (s_texture) SDL_ReleaseGPUTexture(s_device, s_texture);
        SDL_zero(info);
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = (Uint32)s->w;
        info.height = (Uint32)s->h;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        s_texture = SDL_CreateGPUTexture(s_device, &info);
        s_tex_w = s->w;
        s_tex_h = s->h;
        if (!s_texture) return 0;
    }
    w = (float)s_win_w;
    h = (float)s_win_h;
    pe_quad(verts, -1, 1, 1, -1, 0.5f / (float)s->w, ((float)s->h - 0.5f) / (float)s->h,
            0.5f / (float)s->w, ((float)s->h - 0.5f) / (float)s->h, dim);          /* dimming: the solid texel */
    pe_quad(verts + 6, (float)x / w * 2 - 1, 1 - (float)y / h * 2, (float)(x + s->w) / w * 2 - 1,
            1 - (float)(y + s->h - 1) / h * 2, 0, 0, 1, (float)(s->h - 1) / (float)s->h, 1);

    SDL_zero(ti);
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size = tex_bytes + vtx_bytes;
    if (!(transfer = SDL_CreateGPUTransferBuffer(s_device, &ti))) return 0;
    if (!(mapped = (uint8_t *)SDL_MapGPUTransferBuffer(s_device, transfer, false))) {
        SDL_ReleaseGPUTransferBuffer(s_device, transfer);
        return 0;
    }
    memcpy(mapped, s->pixels, tex_bytes);
    memcpy(mapped + tex_bytes, verts, vtx_bytes);
    SDL_UnmapGPUTransferBuffer(s_device, transfer);
    if (!(cmd = SDL_AcquireGPUCommandBuffer(s_device))) {
        SDL_ReleaseGPUTransferBuffer(s_device, transfer);
        return 0;
    }
    copy = SDL_BeginGPUCopyPass(cmd);
    SDL_zero(tsrc);
    tsrc.transfer_buffer = transfer;
    tsrc.pixels_per_row = (Uint32)s->w;
    tsrc.rows_per_layer = (Uint32)s->h;
    SDL_zero(tdst);
    tdst.texture = s_texture;
    tdst.w = (Uint32)s->w;
    tdst.h = (Uint32)s->h;
    tdst.d = 1;
    SDL_UploadToGPUTexture(copy, &tsrc, &tdst, false);
    SDL_zero(vsrc);
    vsrc.transfer_buffer = transfer;
    vsrc.offset = tex_bytes;
    SDL_zero(vdst);
    vdst.buffer = s_vertices;
    vdst.size = vtx_bytes;
    SDL_UploadToGPUBuffer(copy, &vsrc, &vdst, false);
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(s_device, transfer);
    s_visible = 1;
    return 1;
}

void pe_ui_gpu_hide(void) { s_visible = 0; }

static void pe_render(SDL_GPUCommandBuffer *cmd, SDL_GPURenderPass *pass) {
    static const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    static const float kColorScale[4] = {1, 0, 0, 0};
    SDL_GPUViewport viewport;
    SDL_Rect scissor;
    SDL_GPUTextureSamplerBinding tex;
    SDL_GPUBufferBinding vtx;
    if (s_prev_render) s_prev_render(cmd, pass);
    if (!s_visible || !s_pipeline || !s_texture || !s_vertices || s_win_w <= 0 || s_win_h <= 0) return;
    viewport = (SDL_GPUViewport){0, 0, (float)s_win_w, (float)s_win_h, 0, 1};
    scissor = (SDL_Rect){0, 0, s_win_w, s_win_h};
    SDL_SetGPUViewport(pass, &viewport);
    SDL_SetGPUScissor(pass, &scissor);
    SDL_BindGPUGraphicsPipeline(pass, s_pipeline);
    SDL_zero(tex);
    tex.texture = s_texture;
    tex.sampler = s_sampler;
    SDL_BindGPUFragmentSamplers(pass, 0, &tex, 1);
    SDL_zero(vtx);
    vtx.buffer = s_vertices;
    SDL_BindGPUVertexBuffers(pass, 0, &vtx, 1);
    SDL_PushGPUVertexUniformData(cmd, 0, kIdentity, sizeof kIdentity);
    SDL_PushGPUFragmentUniformData(cmd, 0, kColorScale, sizeof kColorScale);
    SDL_DrawGPUPrimitives(pass, 12, 1, 0, 0);
}

static void pe_init(SDL_Window *window, SDL_GPUDevice *device) {
    if (s_prev_init) s_prev_init(window, device);
    if (s_device && s_device != device) pe_release();
    s_window = window;
    s_device = device;
    if (window) SDL_SetWindowTitle(window, "Parasite Eve (Mods) - F1: Mods");
}

static void pe_destroy(void) {
    if (s_prev_destroy) s_prev_destroy();
    pe_release();
    s_device = NULL;
    s_window = NULL;
    s_visible = 0;
}

void pe_ui_gpu_install(void) {
    s_prev_init = Psyz_OverlayInit_SDL3GPU(pe_init);
    s_prev_render = Psyz_OverlayRender_SDL3GPU(pe_render);
    s_prev_destroy = Psyz_OverlayDestroyCB(pe_destroy);
}
