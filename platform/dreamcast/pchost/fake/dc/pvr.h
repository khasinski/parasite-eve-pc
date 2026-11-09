/* Host stand-in for <dc/pvr.h>: just what dc_gpu.c uses, recorded and
 * rasterised in software by fakepvr.c. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef void *pvr_ptr_t;
typedef int pvr_list_t;
enum { PVR_LIST_OP_POLY, PVR_LIST_OP_MOD, PVR_LIST_TR_POLY };
enum { PVR_BINSIZE_0 = 0, PVR_BINSIZE_8 = 8, PVR_BINSIZE_16 = 16, PVR_BINSIZE_32 = 32 };
enum { PVR_BLEND_ZERO, PVR_BLEND_ONE, PVR_BLEND_DESTCOLOR, PVR_BLEND_INVDESTCOLOR,
       PVR_BLEND_SRCALPHA, PVR_BLEND_INVSRCALPHA, PVR_BLEND_DESTALPHA, PVR_BLEND_INVDESTALPHA };
enum { PVR_TXRENV_REPLACE, PVR_TXRENV_MODULATE, PVR_TXRENV_DECAL, PVR_TXRENV_MODULATEALPHA };
enum { PVR_CULLING_NONE };
enum { PVR_DEPTHCMP_ALWAYS = 7 };
enum { PVR_FILTER_NONE = 0, PVR_FILTER_BILINEAR = 2 };
enum { PVR_UVCLAMP_NONE, PVR_UVCLAMP_V, PVR_UVCLAMP_U, PVR_UVCLAMP_UV };
#define PVR_TXRFMT_ARGB1555 0
#define PVR_TXRFMT_RGB565 (1 << 27)
#define PVR_TXRFMT_NONTWIDDLED (1 << 26)
#define PVR_CMD_VERTEX 0xe0000000
#define PVR_CMD_VERTEX_EOL 0xf0000000
typedef struct {
    int opb_sizes[5];
    int vertex_buf_size, dma_enabled, fsaa_enabled, autosort_disabled,
        opb_overflow_count, vbuf_doublebuf_disabled;
} pvr_init_params_t;
typedef struct {
    pvr_list_t list_type;
    struct { bool alpha, shading; int culling; } gen;
    struct { int src, dst; } blend;
    struct { int comparison; bool write; } depth;
    struct { bool enable; int filter, uv_clamp; bool alpha; int env, width, height, format; pvr_ptr_t base; } txr;
} pvr_poly_cxt_t;
typedef struct { uint32_t magic; pvr_poly_cxt_t cxt; } pvr_poly_hdr_t;
typedef struct {
    uint32_t flags;
    float x, y, z, u, v;
    uint32_t argb, oargb;
} pvr_vertex_t;
int pvr_init(const pvr_init_params_t *params);
void pvr_set_bg_color(float r, float g, float b);
size_t pvr_mem_available(void);
pvr_ptr_t pvr_mem_malloc(size_t size);
void pvr_txr_load(const void *src, pvr_ptr_t dst, size_t count);
void pvr_poly_cxt_txr(pvr_poly_cxt_t *dst, pvr_list_t list, int fmt, int w, int h, pvr_ptr_t txr, int filter);
void pvr_poly_cxt_col(pvr_poly_cxt_t *dst, pvr_list_t list);
void pvr_poly_compile(pvr_poly_hdr_t *dst, const pvr_poly_cxt_t *src);
int pvr_wait_ready(void);
void pvr_scene_begin(void);
int pvr_list_begin(pvr_list_t list);
int pvr_list_finish(void);
int pvr_scene_finish(void);
int pvr_prim(const void *data, size_t size);
/* Direct rendering: a vertex written to the target goes in on commit. */
extern pvr_vertex_t fake_dr_vertex;
#define pvr_dr_target(...) ((void *)&fake_dr_vertex)
#define pvr_dr_commit(addr) pvr_prim((addr), sizeof(pvr_vertex_t))
