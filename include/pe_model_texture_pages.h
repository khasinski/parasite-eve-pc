/* MPL-2.0. Original player-bank source ownership adapter. */
#ifndef PE_MODEL_TEXTURE_PAGES_H
#define PE_MODEL_TEXTURE_PAGES_H
#ifdef PE_PS5
extern void Psyz_GpuBeginPlayerTextureSet(int bank);
extern void Psyz_GpuRegisterPlayerTexturePage(unsigned page,unsigned clut);
static void PeModelTextures_Player(RenderObjectEntity *obj,int bank){
    RenderPacket34 *q;RenderPacket28 *t;unsigned i,j;
    Psyz_GpuBeginPlayerTextureSet(bank);
    if(!obj||!obj->header||!obj->primitive_buffer||obj->header->packet34_count<0||obj->header->packet34_count>256||obj->header->packet28_count<0||obj->header->packet28_count>256)return;
    q=(RenderPacket34 *)obj->primitive_buffer;
    for(i=0;i<(unsigned)obj->header->packet34_count;i++)for(j=0;j<2;j++,q++)Psyz_GpuRegisterPlayerTexturePage(q->page_bits,q->clut);
    t=(RenderPacket28 *)q;
    for(i=0;i<(unsigned)obj->header->packet28_count;i++)for(j=0;j<2;j++,t++)Psyz_GpuRegisterPlayerTexturePage(t->page_bits,t->clut);
}
#else
#define PeModelTextures_Player(object,bank) ((void)0)
#endif
#endif

