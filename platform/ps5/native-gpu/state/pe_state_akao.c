#include "pe_state.h"
#include "pe_state_boundary.h"
#include "psyz/state_audio.h"
#include "psyz/state_producers.h"
#include "pe1/akao/globals.h"
#include <string.h>
static uint64_t generation=1,boundGeneration;static const void *pointers[700];static unsigned char pointerRoles[700];static unsigned pointerCount;static uint64_t boundSession,boundScene;static const void *bases[2];
void PeStateAkao_Changed(void){if(++generation==0)generation=1;}
static uint32_t pointer_id(const void *p,unsigned role){if(!p)return 0;for(unsigned i=0;i<pointerCount;i++)if(pointers[i]==p&&pointerRoles[i]==role)return i+1;if(pointerCount==700)return UINT32_MAX;pointers[pointerCount]=p;pointerRoles[pointerCount++]=(unsigned char)role;return pointerCount;}
static void put(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static uint32_t get(const unsigned char *p){return(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int bound(void){return Psyz_StateAudioOwned()&&Psyz_StateKernelOwned()&&!g_AkaoSeqBusyFlags&&boundGeneration==generation&&boundSession==PeStateBoundary_Session()&&boundScene==PeStateBoundary_SceneEpoch()&&bases[0]==g_AkaoSequencerState.primary.sequence_base&&bases[1]==g_AkaoSequencerState.secondary.sequence_base;}
static int canonical(uint32_t v,unsigned k){if(k==4)return v<=3;return k==1?v<=255:k==2?v<=65535:k==3?((int32_t)v>=-32768&&(int32_t)v<=32767):1;}
static int walk(const unsigned char *in,unsigned char *out,int apply){size_t at=32;uint32_t v;
#define FIELD(value,kind) do{v=out?(uint32_t)(value):get(in+at);if(!canonical(v,kind))return 0;if(out)put(out+at,v);else if(apply)(value)=(__typeof__(value))v;at+=4;}while(0)
#define REF(value,role) do{v=out?pointer_id(value,role):get(in+at);if(v==UINT32_MAX||v>pointerCount||(v&&pointerRoles[v-1]!=(role)))return 0;if(out)put(out+at,v);else if(apply)(value)=(__typeof__(value))(v?pointers[v-1]:0);at+=4;}while(0)
for(unsigned i=0;i<2;i++){AkaoSequencerBank *bank=i?&g_AkaoSequencerState.secondary:&g_AkaoSequencerState.primary;REF(bank->sequence_base,5);if(!out&&((v?pointers[v-1]:0)!=bases[i]))return 0;
FIELD(bank->status_flags,0);
FIELD(bank->active_voice_mask,0);
FIELD(bank->pending_voice_mask,0);
FIELD(bank->yield_voice_mask,0);
FIELD(bank->key_on_request_mask,0);
FIELD(bank->allocated_voice_mask,0);
FIELD(bank->key_off_request_mask,0);
FIELD(bank->pending_restore_mask,0);
FIELD(bank->tempo,0);
FIELD(bank->tempo_delta,0);
FIELD(bank->tick_accumulator,0);
FIELD(bank->reserved_voice_mask,0);
FIELD(bank->key_off_dirty_mask,0);
FIELD(bank->volume_dirty_mask,0);
FIELD(bank->adsr_dirty_mask,0);
FIELD(bank->volume,0);
FIELD(bank->volume_delta,0);
FIELD(bank->pitch_current,0);
FIELD(bank->pitch_delta,0);
FIELD(bank->pitch_slide_duration,3);
FIELD(bank->tempo_slide_duration,2);
FIELD(bank->bank_id,2);
FIELD(bank->condition_value,2);
FIELD(bank->volume_slide_duration,2);
FIELD(bank->noise_clock,2);
FIELD(bank->beats_per_measure,2);
FIELD(bank->beat,2);
FIELD(bank->ticks_per_beat,2);
FIELD(bank->tick,2);
FIELD(bank->measure,2);
}
for(unsigned i=0;i<60;i++){AkaoTrack *track=&g_AkaoSequencerTracks[i].track;
REF(track->pc,1);
REF(track->call_stack[0],1);
REF(track->call_stack[1],1);
REF(track->call_stack[2],1);
REF(track->call_stack[3],1);
REF(track->branch_target,1);
REF(track->repeat_target,1);
REF(track->pitch_lfo_table,2);
REF(track->volume_lfo_table,3);
REF(track->aux_lfo_table,4);
FIELD(track->key_off_mask,0);
FIELD(track->key_on_mask,0);
FIELD(track->pitch_base,0);
FIELD(track->voice_mask_a,0);
FIELD(track->flags,0);
FIELD(track->pitch_offset,0);
FIELD(track->pitch_offset_step,0);
FIELD(track->expression_value,0);
FIELD(track->expression_delta,0);
FIELD(track->pitch_slide_step,0);
FIELD(track->field_50_duration,2);
FIELD(track->field_52_duration,2);
FIELD(track->parent_track_id,2);
FIELD(track->note_length,2);
FIELD(track->pan_duration,2);
FIELD(track->note_pitch,2);
FIELD(track->voice_index,2);
FIELD(track->pitch_slide_current,2);
FIELD(track->pitch_slide_duration,2);
FIELD(track->repeat_counters[0],2);
FIELD(track->repeat_counters[1],2);
FIELD(track->repeat_counters[2],2);
FIELD(track->repeat_counters[3],2);
FIELD(track->volume,3);
FIELD(track->volume_base,2);
FIELD(track->volume_duration,2);
FIELD(track->pitch_offset_duration,2);
FIELD(track->expression_duration,2);
FIELD(track->panpot_duration,2);
FIELD(track->panpot,2);
FIELD(track->panpot_slide_duration,2);
FIELD(track->pitch_slide_steps,2);
FIELD(track->panpot_step,2);
FIELD(track->vibrato_duration,2);
FIELD(track->tremolo_counter,2);
FIELD(track->tremolo_duration,2);
FIELD(track->tremolo_phase,2);
FIELD(track->pitch_lfo_delay,2);
FIELD(track->pitch_lfo_counter,2);
FIELD(track->pitch_lfo_duration,2);
FIELD(track->pitch_lfo_phase,2);
FIELD(track->pitch_lfo_selector,2);
FIELD(track->pitch_lfo_depth,2);
FIELD(track->pitch_lfo_target,2);
FIELD(track->pitch_lfo_slide_duration,2);
FIELD(track->pitch_lfo_delta,3);
FIELD(track->field_9A,3);
FIELD(track->volume_lfo_delay,2);
FIELD(track->volume_lfo_counter,2);
FIELD(track->volume_lfo_duration,2);
FIELD(track->volume_lfo_phase,2);
FIELD(track->volume_lfo_selector,2);
FIELD(track->volume_lfo_target,2);
FIELD(track->volume_lfo_slide_duration,2);
FIELD(track->volume_lfo_delta,3);
FIELD(track->field_AC,3);
FIELD(track->aux_lfo_duration,2);
FIELD(track->aux_lfo_phase,2);
FIELD(track->aux_lfo_selector,2);
FIELD(track->aux_lfo_target,2);
FIELD(track->aux_lfo_slide_duration,2);
FIELD(track->aux_lfo_delta,3);
FIELD(track->key_on_delay,2);
FIELD(track->key_off_delay,2);
FIELD(track->call_stack_index,4);
FIELD(track->default_note_length,3);
FIELD(track->fixed_note_length,3);
FIELD(track->volume_delta,3);
FIELD(track->pitch_slide_delta,3);
FIELD(track->pan_target,3);
FIELD(track->pan_delta,3);
FIELD(track->panpot_delta,3);
FIELD(track->expression,3);
FIELD(track->detune,3);
FIELD(track->current_note,3);
FIELD(track->vibrato_delta,3);
FIELD(track->tremolo_delta,3);
FIELD(track->pitch_lfo_value,3);
FIELD(track->volume_lfo_value,3);
FIELD(track->aux_lfo_value,3);
FIELD(track->assigned_voice_index,0);
FIELD(track->update_flags,0);
FIELD(track->sample_header,0);
FIELD(track->sample_data,0);
FIELD(track->adsr_attack,0);
FIELD(track->adsr_sustain,0);
FIELD(track->adsr_release,0);
FIELD(track->pitch,2);
FIELD(track->adsr_attack_rate,2);
FIELD(track->adsr_decay_rate,2);
FIELD(track->adsr_sustain_level,2);
FIELD(track->adsr_sustain_rate,2);
FIELD(track->adsr_release_rate,2);
FIELD(track->volume_left,3);
FIELD(track->volume_right,3);
}
#undef FIELD
#undef REF
return at==25480;}
static int capture(void *ctx,unsigned char *out,size_t cap,size_t *used){(void)ctx;if(used)*used=0;if(!out||!used||cap<25480||!Psyz_StateAudioOwned()||!Psyz_StateKernelOwned()||g_AkaoSeqBusyFlags)return 0;if(boundGeneration!=generation||boundScene!=PeStateBoundary_SceneEpoch()){pointerCount=0;boundGeneration=generation;boundSession=PeStateBoundary_Session();boundScene=PeStateBoundary_SceneEpoch();bases[0]=g_AkaoSequencerState.primary.sequence_base;bases[1]=g_AkaoSequencerState.secondary.sequence_base;}if(!bound())return 0;uint32_t current=g_AkaoCurTrack==&g_AkaoSequencerState.primary?1:g_AkaoCurTrack==&g_AkaoSequencerState.secondary?2:g_AkaoCurTrack==0?0:UINT32_MAX;if(current==UINT32_MAX)return 0;memcpy(out,"PEAKAO01",8);put(out+8,(uint32_t)generation);put(out+12,(uint32_t)(generation>>32));put(out+16,(uint32_t)boundScene);put(out+20,(uint32_t)(boundScene>>32));put(out+24,current);put(out+28,0);if(!walk(0,out,0))return 0;*used=25480;return 1;}
static int validate(void *ctx,const unsigned char *in,size_t bytes){(void)ctx;return in&&bytes==25480&&!memcmp(in,"PEAKAO01",8)&&bound()&&get(in+8)==(uint32_t)generation&&get(in+12)==(uint32_t)(generation>>32)&&get(in+16)==(uint32_t)boundScene&&get(in+20)==(uint32_t)(boundScene>>32)&&get(in+24)<=2&&!get(in+28)&&walk(in,0,0);}
static int restore(void *ctx,const unsigned char *in,size_t bytes){if(!validate(ctx,in,bytes))return 0;uint32_t current=get(in+24);if(!walk(in,0,1))return 0;g_AkaoCurTrack=current==1?&g_AkaoSequencerState.primary:current==2?&g_AkaoSequencerState.secondary:0;return 1;}
int PeState_RegisterAkaoTracks(PeStateRegistry *r){PeStateModule m={PE_STATE_AKAO,1,65536,capture,0,validate,restore,0};return PeState_Register(r,m);}
