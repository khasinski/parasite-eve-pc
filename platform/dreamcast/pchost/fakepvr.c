/* Software stand-in for the PVR for the PC harness: rasterises the strips of
 * the scenes it is asked to keep (DCHOST_EVERY / DCHOST_SAVE) with the
 * header's texture, modulation and blend factors into a 640x480 image and
 * writes DCHOST_DIR/vbNNNNNN.ppm. A virtual clock moves one frame per
 * vid_waitvbl(), so the game runs as fast as the host allows. With
 * PE_MEMORY_REPORT=1 it also logs heap peaks (main RAM only: PVR texture
 * memory is left out). Edges shared by two triangles of a strip are shaded
 * twice (no fill rule): thin seams inside translucent quads that the PVR
 * does not draw. */
#include <dc/pvr.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
static size_t vertex_buf_size = 1024 * 1024; /* set by pvr_init */
#define W 640
#define H 480
static float fb[H][W][3];
static float bg[3];
static pvr_poly_hdr_t cur;
static pvr_vertex_t strip[64];
static int nstrip, frame, prims, headers;
int g_fake_capture; /* read by dc_gpu.c built with DCGPU_TRACE */
#define capture g_fake_capture
static uint64_t vclock_us;
static unsigned long long total_prims;
static int scenes;
/* DCHOST_SAVE="vblank,vblank,..." or DCHOST_EVERY=N: which scenes to keep,
 * within DCHOST_FROM..DCHOST_TO when set; DCHOST_NOWRITE keeps them for
 * tracing (DCGPU_TRACE) without writing files */
static int WantFrame(unsigned vb) {
    static unsigned last_saved;
    const char *from = getenv("DCHOST_FROM"), *to = getenv("DCHOST_TO");
    if ((from && vb < (unsigned)atoi(from)) || (to && vb > (unsigned)atoi(to))) return 0;
    const char *every = getenv("DCHOST_EVERY");
    if (every && atoi(every) > 0 && vb >= last_saved + (unsigned)atoi(every)) { last_saved = vb; return 1; }
    const char *list = getenv("DCHOST_SAVE");
    if (!list) return 0;
    for (const char *p = list; *p;) {
        unsigned v = (unsigned)strtoul(p, (char **)&p, 10);
        if (v && vb >= v && last_saved < v) { last_saved = vb; return 1; }
        while (*p == ',') p++;
    }
    return 0;
}
uint64_t timer_us_gettime64(void) { return __atomic_add_fetch(&vclock_us, 1, __ATOMIC_RELAXED); }
uint64_t timer_ns_gettime64(void) { return ++vclock_us * 1000u; }
static void (*vblank_hnd)(uint32_t, void *);
int vblank_handler_add(void (*hnd)(uint32_t, void *), void *data) { (void)data; vblank_hnd = hnd; return 1; }
void host_audio_vblank(void);
void vid_waitvbl(void) {
    vclock_us = (vclock_us / 16683u + 1) * 16683u;
    if (vblank_hnd) vblank_hnd(0, NULL);
    host_audio_vblank();
}
int thd_pass(void) { vid_waitvbl(); return 0; }
int pvr_init(const pvr_init_params_t *p) { vertex_buf_size = (size_t)p->vertex_buf_size; return 0; }
void pvr_set_bg_color(float r, float g, float b) { bg[0] = r; bg[1] = g; bg[2] = b; }

static size_t pvr_bytes, heap_peak;
static unsigned heap_peak_vb;
/* Texture memory as KallistiOS leaves it: 8 MB, less 1591 KB for the
 * 640x480 framebuffers, OPBs and tables, less the double-buffered vertex
 * buffer (calibrated on Flycast: 4553 KB free with a 1 MB vertex buffer). */
#define PVR_TEXTURE_BYTES ((8192u - 1591u) * 1024u - 2u * vertex_buf_size)
size_t pvr_mem_available(void) { return PVR_TEXTURE_BYTES - pvr_bytes; }
void pvr_txr_load(const void *src, pvr_ptr_t dst, size_t count) { memcpy(dst, src, count); }
pvr_ptr_t pvr_mem_malloc(size_t n) {
    if (n > pvr_mem_available()) return NULL;
    pvr_bytes += n; return calloc(1, n);
}
void pvr_poly_cxt_txr(pvr_poly_cxt_t *d, pvr_list_t l, int f, int w, int h, pvr_ptr_t t, int fl) {
    memset(d, 0, sizeof(*d)); d->list_type = l; d->txr.enable = 1; d->txr.format = f;
    d->txr.width = w; d->txr.height = h; d->txr.base = t; d->txr.filter = fl;
    d->blend.src = PVR_BLEND_SRCALPHA; d->blend.dst = PVR_BLEND_INVSRCALPHA; d->txr.env = PVR_TXRENV_MODULATE;
}
void pvr_poly_cxt_col(pvr_poly_cxt_t *d, pvr_list_t l) {
    memset(d, 0, sizeof(*d)); d->list_type = l;
    d->blend.src = PVR_BLEND_SRCALPHA; d->blend.dst = PVR_BLEND_INVSRCALPHA;
}
void pvr_poly_compile(pvr_poly_hdr_t *d, const pvr_poly_cxt_t *s) { d->magic = 0x48445221; d->cxt = *s; }
int pvr_wait_ready(void) { return 0; }
void pvr_scene_begin(void) {
    capture = WantFrame((unsigned)(vclock_us / 16683u));
    if (capture) for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) memcpy(fb[y][x], bg, sizeof bg);
    prims = headers = 0;
}
int pvr_list_begin(pvr_list_t l) { (void)l; return 0; }
int pvr_list_finish(void) { return 0; }
static float Factor(int f, const float *s, float sa, const float *d, int c, int is_dst) {
    switch (f) {
    case PVR_BLEND_ZERO: return 0;
    case PVR_BLEND_ONE: return 1;
    case PVR_BLEND_DESTCOLOR: return is_dst ? s[c] : d[c];   /* "other" colour */
    case PVR_BLEND_INVDESTCOLOR: return is_dst ? 1 - s[c] : 1 - d[c];
    case PVR_BLEND_SRCALPHA: return sa;
    case PVR_BLEND_INVSRCALPHA: return 1 - sa;
    default: return 1;
    }
}
static void Unpack(uint32_t c, float *o) {
    o[0] = ((c >> 24) & 255) / 255.f; o[1] = ((c >> 16) & 255) / 255.f;
    o[2] = ((c >> 8) & 255) / 255.f; o[3] = (c & 255) / 255.f;
}
static void Tri(const pvr_vertex_t *a, const pvr_vertex_t *b, const pvr_vertex_t *c) {
    const pvr_poly_cxt_t *x = &cur.cxt;
    float area = (b->x - a->x) * (c->y - a->y) - (c->x - a->x) * (b->y - a->y);
    if (fabsf(area) < 1e-6f) return;
    int x0 = (int)floorf(fminf(a->x, fminf(b->x, c->x))), x1 = (int)ceilf(fmaxf(a->x, fmaxf(b->x, c->x)));
    int y0 = (int)floorf(fminf(a->y, fminf(b->y, c->y))), y1 = (int)ceilf(fmaxf(a->y, fmaxf(b->y, c->y)));
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > W) x1 = W; if (y1 > H) y1 = H;
    float ca[4], cb[4], cc[4];
    Unpack(a->argb, ca); Unpack(b->argb, cb); Unpack(c->argb, cc);
    for (int py = y0; py < y1; py++) for (int px = x0; px < x1; px++) {
        float sx = px + 0.5f, sy = py + 0.5f;
        float w0 = ((b->x - sx) * (c->y - sy) - (c->x - sx) * (b->y - sy)) / area;
        float w1 = ((c->x - sx) * (a->y - sy) - (a->x - sx) * (c->y - sy)) / area;
        float w2 = 1 - w0 - w1;
        if (w0 < 0 || w1 < 0 || w2 < 0) continue;
        float col[4];
        for (int k = 0; k < 4; k++) col[k] = x->gen.shading ? w0 * ca[k] + w1 * cb[k] + w2 * cc[k] : cc[k];
        float sa = col[0], s[3] = {col[1], col[2], col[3]};
        if (x->txr.enable) {
            /* z is 1/w: interpolate u/w, v/w and 1/w, as the PVR does */
            float iz = w0 * a->z + w1 * b->z + w2 * c->z;
            float u = (w0 * a->u * a->z + w1 * b->u * b->z + w2 * c->u * c->z) / iz;
            float v = (w0 * a->v * a->z + w1 * b->v * b->z + w2 * c->v * c->z) / iz;
            int tw = x->txr.width, th = x->txr.height;
            float tex[4] = {0, 0, 0, 0}; /* a r g b */
            float fu = u * tw, fv = v * th;
            int bilinear = x->txr.filter == PVR_FILTER_BILINEAR;
            if (bilinear) { fu -= 0.5f; fv -= 0.5f; }
            int bu = (int)floorf(fu), bv = (int)floorf(fv);
            float ax = bilinear ? fu - bu : 0, ay = bilinear ? fv - bv : 0;
            for (int k = 0; k < (bilinear ? 4 : 1); k++) {
                int tu = bu + (k & 1), tv = bv + (k >> 1);
                float wt = (k & 1 ? ax : 1 - ax) * (k >> 1 ? ay : 1 - ay);
                if (x->txr.uv_clamp == PVR_UVCLAMP_UV) {
                    tu = tu < 0 ? 0 : tu >= tw ? tw - 1 : tu;
                    tv = tv < 0 ? 0 : tv >= th ? th - 1 : tv;
                }
                tu &= tw - 1; tv &= th - 1;
                uint16_t t = ((uint16_t *)x->txr.base)[tv * tw + tu];
                if (x->txr.format & PVR_TXRFMT_RGB565) {
                    tex[0] += wt; tex[1] += wt * ((t >> 11) & 31) / 31.f;
                    tex[2] += wt * ((t >> 5) & 63) / 63.f; tex[3] += wt * (t & 31) / 31.f;
                } else {
                    tex[0] += wt * ((t >> 15) & 1); tex[1] += wt * ((t >> 10) & 31) / 31.f;
                    tex[2] += wt * ((t >> 5) & 31) / 31.f; tex[3] += wt * (t & 31) / 31.f;
                }
            }
            s[0] *= tex[1]; s[1] *= tex[2]; s[2] *= tex[3];
            if (!x->txr.alpha) sa *= tex[0];
        }
        float *d = fb[py][px];
        float out[3];
        for (int k = 0; k < 3; k++) {
            float v = s[k] * Factor(x->blend.src, s, sa, d, k, 0) + d[k] * Factor(x->blend.dst, s, sa, d, k, 1);
            out[k] = v > 1 ? 1 : v < 0 ? 0 : v;
        }
        memcpy(d, out, sizeof out);
    }
}
/* Like the PVR, draw nothing until the scene ends: strips are recorded with
 * their header and rasterised in pvr_scene_finish(), reading textures then.
 * A texture overwritten after use in the same frame shows up as on hardware. */
typedef struct { pvr_poly_hdr_t hdr; int first, count; } Strip;
static Strip *rec_strips; static int rec_nstrips, rec_cap_strips;
static pvr_vertex_t *rec_verts; static int rec_nverts, rec_cap_verts;
pvr_vertex_t fake_dr_vertex;
int pvr_prim(const void *data, size_t size) {
    const pvr_poly_hdr_t *h = data;
    if (size == sizeof(pvr_poly_hdr_t) && h->magic == 0x48445221) { cur = *h; headers++; return 0; }
    const pvr_vertex_t *v = data;
    strip[nstrip++] = *v;
    if (v->flags == PVR_CMD_VERTEX_EOL) {
        prims++;
        if (capture) {
            if (rec_nstrips == rec_cap_strips) { rec_cap_strips = rec_cap_strips ? rec_cap_strips * 2 : 1024; rec_strips = realloc(rec_strips, rec_cap_strips * sizeof(*rec_strips)); }
            if (rec_nverts + nstrip > rec_cap_verts) { rec_cap_verts = (rec_cap_verts + nstrip) * 2; rec_verts = realloc(rec_verts, rec_cap_verts * sizeof(*rec_verts)); }
            rec_strips[rec_nstrips++] = (Strip){cur, rec_nverts, nstrip};
            memcpy(rec_verts + rec_nverts, strip, nstrip * sizeof(*strip));
            rec_nverts += nstrip;
        }
        nstrip = 0;
    }
    return 0;
}
static void RasteriseRecorded(void) {
    for (int i = 0; i < rec_nstrips; i++) {
        cur = rec_strips[i].hdr;
        const pvr_vertex_t *v = rec_verts + rec_strips[i].first;
        for (int k = 2; k < rec_strips[i].count; k++) Tri(&v[k - 2], &v[k - 1], &v[k]);
    }
    rec_nstrips = rec_nverts = 0;
}

int pvr_scene_finish(void) {
    RasteriseRecorded();
    scenes++; total_prims += (unsigned)prims;
    {
        struct mallinfo2 mi = mallinfo2();
        size_t used = mi.uordblks + mi.hblkhd - pvr_bytes; /* PVR memory is not main RAM */
        if (used > heap_peak) {
            heap_peak = used; heap_peak_vb = (unsigned)(vclock_us / 16683u);
            if (getenv("PE_MEMORY_REPORT")) fprintf(stderr, "[heap] new peak %zu bytes at vblank %u\n", heap_peak, heap_peak_vb);
        }
    }
    if (!capture) return 0;
    if (getenv("DCHOST_NOWRITE")) return 0; /* trace only */
    const char *dir = getenv("DCHOST_DIR");
    char name[512]; snprintf(name, sizeof name, "%s/vb%06u.ppm", dir ? dir : ".", (unsigned)(vclock_us / 16683u)); frame++;
    FILE *f = fopen(name, "wb"); fprintf(f, "P6 %d %d 255\n", W, H);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++)
        for (int k = 0; k < 3; k++) fputc((int)(fb[y][x][k] * 255 + 0.5f), f);
    fclose(f);
    printf("%s: %d strips, %d header changes (scene %d, avg %.0f strips)\n", name, prims, headers, scenes, (double)total_prims / scenes);
    fflush(stdout);
    return 0;
}

