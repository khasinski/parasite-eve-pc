#include "pe_state.h"
#include "pe_state_boundary.h"
#include "psyz/state_audio.h"
#include "pe1/akao/globals.h"
#include <string.h>
#include <stdint.h>
static void put(unsigned char *o,uint32_t v){for(unsigned j=0;j<4;j++)o[j]=(unsigned char)(v>>(8*j));}
static uint32_t get(const unsigned char *p){return(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int validate(void *ctx,const unsigned char *in,size_t bytes){(void)ctx;size_t n=Psyz_StateSpuBytes();if(!in||bytes!=16+n+20+26*4||memcmp(in,"PEAUDIO2",8)||get(in+8)!=n||get(in+12)!=26||(!Psyz_StateSpuValidate(in+16,n)||!Psyz_StateAudioClockValidate(in+16+n,20)))return 0;size_t at=16+n+20;uint32_t v;
v=get(in+at);at+=4;if(0)return 0;
v=get(in+at);at+=4;if(v>65535)return 0;
v=get(in+at);at+=4;if((int32_t)v< -32768||(int32_t)v>32767)return 0;
v=get(in+at);at+=4;if((int32_t)v< -32768||(int32_t)v>32767)return 0;
v=get(in+at);at+=4;if((int32_t)v< -32768||(int32_t)v>32767)return 0;
v=get(in+at);at+=4;if(0)return 0;
v=get(in+at);at+=4;if(0)return 0;
v=get(in+at);at+=4;if(0)return 0;
v=get(in+at);at+=4;if(0)return 0;
v=get(in+at);at+=4;if(0)return 0;
v=get(in+at);at+=4;if(0)return 0;
v=get(in+at);at+=4;if(0)return 0;
v=get(in+at);at+=4;if(0)return 0;
v=get(in+at);at+=4;if(0)return 0;
v=get(in+at);at+=4;if(0)return 0;
v=get(in+at);at+=4;if(0)return 0;
v=get(in+at);at+=4;if(0)return 0;
v=get(in+at);at+=4;if(0)return 0;
v=get(in+at);at+=4;if(0)return 0;
v=get(in+at);at+=4;if(0)return 0;
v=get(in+at);at+=4;if(0)return 0;
v=get(in+at);at+=4;if(0)return 0;
v=get(in+at);at+=4;if(0)return 0;
v=get(in+at);at+=4;if(v>65535)return 0;
v=get(in+at);at+=4;if(0)return 0;
v=get(in+at);at+=4;if(0)return 0;
(void)v;return at==bytes;}
static int capture(void *ctx,unsigned char *out,size_t cap,size_t *used){(void)ctx;if(used)*used=0;size_t n=Psyz_StateSpuBytes(),total=16+n+20+26*4;if(!out||!used||cap<total||!Psyz_StateAudioOwned()||g_AkaoSeqBusyFlags)return 0;memcpy(out,"PEAUDIO2",8);put(out+8,n);put(out+12,26);if(!Psyz_StateSpuCapture(out+16,n)||!Psyz_StateAudioClockCapture(out+16+n,20))return 0;size_t at=16+n+20;
put(out+at,(uint32_t)g_AkaoGlobalPitchDepth);at+=4;
put(out+at,(uint32_t)g_AkaoGlobalSlideTick);at+=4;
put(out+at,(uint32_t)g_AkaoGlobalPitchSlideCounter);at+=4;
put(out+at,(uint32_t)g_AkaoGlobalD2D0SlideCounter);at+=4;
put(out+at,(uint32_t)g_AkaoGlobalD2CCSlideCounter);at+=4;
put(out+at,(uint32_t)g_AkaoGlobalPitchSlideStep);at+=4;
put(out+at,(uint32_t)g_AkaoGlobalD2D0SlideStep);at+=4;
put(out+at,(uint32_t)g_AkaoGlobalD2CCSlideStep);at+=4;
put(out+at,(uint32_t)g_AkaoTimerDeltaHist0);at+=4;
put(out+at,(uint32_t)g_AkaoTimerDeltaHist1);at+=4;
put(out+at,(uint32_t)g_AkaoTimerDeltaHist2);at+=4;
put(out+at,(uint32_t)g_AkaoTimerDeltaHist3);at+=4;
put(out+at,(uint32_t)g_AkaoTimerDeltaSum);at+=4;
put(out+at,(uint32_t)g_SpuActiveVoiceMask);at+=4;
put(out+at,(uint32_t)g_SpuPendingKeyOffMask);at+=4;
put(out+at,(uint32_t)g_SpuStoppedVoiceMask);at+=4;
put(out+at,(uint32_t)g_AkaoSeqPendingFlags);at+=4;
put(out+at,(uint32_t)g_AkaoPlaybackMode);at+=4;
put(out+at,(uint32_t)g_AkaoTrack34Mask);at+=4;
put(out+at,(uint32_t)g_AkaoTrack38Mask);at+=4;
put(out+at,(uint32_t)g_AkaoTrack3CMask);at+=4;
put(out+at,(uint32_t)g_AkaoVoiceUpdateFlags);at+=4;
put(out+at,(uint32_t)g_AkaoVoiceMaskScratch);at+=4;
put(out+at,(uint32_t)g_AkaoTrack5ATransposeValue);at+=4;
put(out+at,(uint32_t)g_AkaoSeqLoopCounter);at+=4;
put(out+at,(uint32_t)g_AkaoSfxAllocatedVoiceMask);at+=4;
*used=at;return validate(0,out,at);}
static int restore(void *ctx,const unsigned char *in,size_t bytes){if(!Psyz_StateAudioOwned()||g_AkaoSeqBusyFlags||!validate(ctx,in,bytes))return 0;size_t n=Psyz_StateSpuBytes();if(!Psyz_StateSpuRestore(in+16,n)||!Psyz_StateAudioClockRestore(in+16+n,20))return 0;size_t at=16+n+20;
g_AkaoGlobalPitchDepth=(int)get(in+at);at+=4;
g_AkaoGlobalSlideTick=(u16)get(in+at);at+=4;
g_AkaoGlobalPitchSlideCounter=(short)get(in+at);at+=4;
g_AkaoGlobalD2D0SlideCounter=(short)get(in+at);at+=4;
g_AkaoGlobalD2CCSlideCounter=(short)get(in+at);at+=4;
g_AkaoGlobalPitchSlideStep=(int)get(in+at);at+=4;
g_AkaoGlobalD2D0SlideStep=(int)get(in+at);at+=4;
g_AkaoGlobalD2CCSlideStep=(int)get(in+at);at+=4;
g_AkaoTimerDeltaHist0=(s32)get(in+at);at+=4;
g_AkaoTimerDeltaHist1=(s32)get(in+at);at+=4;
g_AkaoTimerDeltaHist2=(s32)get(in+at);at+=4;
g_AkaoTimerDeltaHist3=(s32)get(in+at);at+=4;
g_AkaoTimerDeltaSum=(s32)get(in+at);at+=4;
g_SpuActiveVoiceMask=(unsigned int)get(in+at);at+=4;
g_SpuPendingKeyOffMask=(unsigned int)get(in+at);at+=4;
g_SpuStoppedVoiceMask=(unsigned int)get(in+at);at+=4;
g_AkaoSeqPendingFlags=(unsigned int)get(in+at);at+=4;
g_AkaoPlaybackMode=(int)get(in+at);at+=4;
g_AkaoTrack34Mask=(unsigned int)get(in+at);at+=4;
g_AkaoTrack38Mask=(unsigned int)get(in+at);at+=4;
g_AkaoTrack3CMask=(unsigned int)get(in+at);at+=4;
g_AkaoVoiceUpdateFlags=(unsigned int)get(in+at);at+=4;
g_AkaoVoiceMaskScratch=(unsigned int)get(in+at);at+=4;
g_AkaoTrack5ATransposeValue=(u16)get(in+at);at+=4;
g_AkaoSeqLoopCounter=(int)get(in+at);at+=4;
g_AkaoSfxAllocatedVoiceMask=(unsigned int)get(in+at);at+=4;
return at==bytes;}
int PeState_RegisterAudioSubset(PeStateRegistry *r){PeStateModule m={PE_STATE_AUDIO,2,600000,capture,0,validate,restore,0};return PeState_Register(r,m);}
