/* Model decode and model view, on a synthetic room (no disc, no game): one textured quad standing in
 * the z-y plane, a 16-bit texture half red and half see-through, an idle row. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pe_disc_assets.h"
#include "pe_model.h"
#include "pe_model_view.h"

void pe_mod_log(const char *mod, const char *fmt, ...) { (void)mod; (void)fmt; }
int pe_disc_room(int map, PeRoomBlobs *out) { (void)map; memset(out, 0, sizeof *out); return 0; }
static int player_reads;
int pe_disc_player(int bank, PeRoomBlobs *r) { (void)bank; ++player_reads; memset(r, 0, sizeof *r); return 0; }
void pe_disc_room_free(PeRoomBlobs *r) { free(r->data); free(r->texture); free(r->scratch); memset(r,0,sizeof *r); }
static int real_assets;
int pe_mod_path(char *out, size_t size, const char *file) {
    const char *root = getenv(real_assets ? "PE_MODEL_SKIN_ROOT" : "PE_MODEL_TEST_DIR");
    const char *name = strrchr(file, '/');
    if (!root) return -1;
    if (real_assets) return snprintf(out,size,"%s/%s",root,file) < (int)size ? 0 : -1;
    return snprintf(out,size,"%s/model-fixture-%s",root,name ? name+1 : file) < (int)size ? 0 : -1;
}

static void w16(uint8_t *b, unsigned at, unsigned v) { b[at] = (uint8_t)v; b[at + 1] = (uint8_t)(v >> 8); }
static void w32(uint8_t *b, unsigned at, unsigned v) { w16(b, at, v & 0xFFFF); w16(b, at + 2, v >> 16); }

enum { SLOT = 3, MODEL = 0x200, ROW = 0x400 };

static void build_data(uint8_t *d) {
    static const short v[4][3] = {{0, -10, -10}, {0, -10, 10}, {0, 10, -10}, {0, 10, 10}};
    unsigned at = MODEL + 0x1C, i, k;
    w32(d, 4, 0x100);                                         /* directory */
    w32(d, 0x100 + 0x0C, 0x140 | 1u << 22);                   /* one model record */
    w32(d, 0x100 + 0x10, 0x160 | 1u << 22);                   /* one animation row */
    w32(d, 0x140 + 4, MODEL | (unsigned)SLOT << 24);
    w32(d, 0x160 + 4, ROW | 0x15u << 24); d[0x160 + 11] = SLOT;
    d[MODEL + 2] = 1; w16(d, MODEL + 6, 4); w16(d, MODEL + 8, 1); w16(d, MODEL + 0x18, 1);
    w16(d, at, 0); w16(d, at + 2, 4); d[at + 4] = 1; at += 12;              /* the part */
    for (i = 0; i < 4; ++i, at += 8) for (k = 0; k < 3; ++k) w16(d, at + 2 * k, (unsigned)(v[i][k] & 0xFFFF));
    at += 4 * 4;
    for (k = 0; k < 4; ++k) w16(d, at + 4 + 2 * k, k);                      /* the quad: 0 1 2 3 */
    at += 12 + (2 + 2 * 1 + 1) * 8;
    d[at] = 0; at += 4;                                                      /* hierarchy: part 0 */
    d[at] = 0; d[at + 1] = 0; w16(d, at + 2, 480u << 6);                     /* uv 0,0, a palette */
    d[at + 4] = 15; d[at + 5] = 0; w16(d, at + 6, 2u << 7 | 10u);            /* 16-bit page at x 640 */
    d[at + 8] = 0; d[at + 9] = 15; d[at + 12] = 15; d[at + 13] = 15;
    d[ROW] = 0; d[ROW + 1] = 0; d[ROW + 2] = 1;                              /* byte angles, 1 part, 1 frame */
    for (i = 0; i < 6; ++i) { d[ROW + 12 + 4 * i] = 1; d[ROW + 13 + 4 * i] = 0; }   /* all constant, 0 */
}

static void build_texture(uint8_t *t) {
    unsigned x, y;
    w32(t, 4, 0x40);
    w32(t, 0x40 + 0x28, 0x80 | 1u << 22);                     /* one TIM record */
    w32(t, 0x80 + 4, 0x100 | 16u << 24);                       /* pixels at 0x100, 16 rows */
    w32(t, 0x80 + 8, 16u | 640u << 10 | 0u << 21);             /* 16 wide at 640, 0 */
    for (y = 0; y < 16; ++y) for (x = 0; x < 16; ++x) w16(t, 0x100 + 2 * (y * 16 + x), x < 8 ? 0x001F : 0);
}

int main(void) {
    static uint8_t data[0x800], tex[0x400], rgba[32 * 32 * 4];
    PeRoomBlobs room = {NULL, tex, data, 0, sizeof tex, sizeof data};
    PeModel m;
    PeViewFit fit;
    PeImage img = {32, 32, rgba};
    PeView view = {32, 32, 0, 0, 0, 0};
    int i, red = 0, clear = 0, drawn;
    build_data(data);
    build_texture(tex);
    assert(pe_model_decode(&room, SLOT, &m));
    assert(m.parts == 1 && m.verts == 4 && m.faces == 1 && m.face[0].quad && m.face[0].textured);
    assert(m.anim && pe_model_frames(&m) == 1);
    assert(!pe_model_decode(&room, SLOT + 1, &(PeModel){0}));             /* no model in that slot */
    assert(pe_model_fit(&m, &fit) && fit.radius > 9.9f && fit.radius < 10.1f && fit.y0 < -9.9f && fit.y1 > 9.9f);
    assert(pe_model_render(&m, &fit, &view, &img));
    for (i = 0; i < 32 * 32; ++i) {
        if (rgba[4 * i + 3] == 255 && rgba[4 * i] > 200 && rgba[4 * i + 1] == 0) ++red;
        if (rgba[4 * i + 3] == 0) ++clear;
    }
    assert(red > 150 && clear > 150);                       /* half the quad red, half see-through */
    view.yaw = 1024;                                        /* a quarter turn: the quad edge-on */
    assert(pe_model_render(&m, &fit, &view, &img));
    for (i = 0, drawn = 0; i < 32 * 32; ++i) drawn += rgba[4 * i + 3] != 0;
    assert(drawn < 40);
    view.yaw = 0; view.silhouette = 1; view.silhouette_rgb = 0x101018;   /* the shape only */
    assert(pe_model_render(&m, &fit, &view, &img));
    for (i = 0, drawn = 0; i < 32 * 32; ++i)
        if (rgba[4 * i + 3]) { assert(rgba[4 * i] == 0x10 && rgba[4 * i + 2] == 0x18); ++drawn; }
    assert(drawn == red);
    pe_model_free(&m);
    /* Player idle is 0x15, not enemy idle 2. Player rows do not use enemy group IDs. */
    w32(data,0x144,MODEL); data[0x16B]=99;
    w32(data,0x110,0x160 | 2u<<22);
    w32(data,0x170,0x500 | 2u<<24); data[0x177]=0;
    memcpy(data+0x500,data+ROW,36); data[0x500+13]=7;
    assert(pe_model_decode_player(&room,&m));
    assert(m.anim && m.anim[13]==0); pe_model_free(&m);
    assert(pe_model_decode(&room,0,&m)); /* enemy slot grouping and idle 2 remain unchanged */
    assert(m.anim && m.anim[13]==7); pe_model_free(&m);
    /* Cache keys include folder contents and retain their own folder string. */
    char path[1200], folder[32]="first";
    FILE *file;
    assert(!pe_mod_path(path,sizeof path,"model.bin"));
    file=fopen(path,"wb"); assert(file); assert(fwrite(data,1,sizeof data,file)==sizeof data); fclose(file);
    assert(!pe_mod_path(path,sizeof path,"texture.bin"));
    file=fopen(path,"wb"); assert(file); assert(fwrite(tex,1,sizeof tex,file)==sizeof tex); fclose(file);
    PeModelSource src={.kind=PE_MODEL_SKIN,.folder=folder};
    const PeModel *first=pe_model_load(&src); assert(first && first->anim && first->vram);
    strcpy(folder,"second"); const PeModel *second=pe_model_load(&src); assert(second && second!=first);
    src.folder="first"; assert(pe_model_load(&src)==first);
    pe_model_release_all();
    pe_mod_path(path,sizeof path,"model.bin"); remove(path);
    pe_mod_path(path,sizeof path,"texture.bin"); remove(path);
    assert(!pe_model_load(&src));             /* missing assets fail cleanly */
    src.folder=NULL; src.map=12;
    assert(!pe_model_load(&src) && player_reads==1); /* no disc: no live actor fallback */
    if (getenv("PE_MODEL_SKIN_ROOT")) {
        static const char *folders[]={"harlequin","baker","maya"};
        real_assets=1;
        for (i=0;i<3;++i) {
            src.folder=folders[i];
            const PeModel *skin=pe_model_load(&src); assert(skin && skin->anim && pe_model_fit(skin,&fit));
            assert(pe_model_frames(skin)>=1); /* player idle may intentionally be a single pose */
            printf("Real skin preview decoded: %s (%d parts, %d idle frames)\n",folders[i],skin->parts,pe_model_frames(skin));
        }
        pe_model_release_all();
    }
    puts("Model checks passed: decode, slot lookup, idle row, fit, texture, see-through texels, turning, silhouette.");
    return 0;
}
