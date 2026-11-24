#ifndef PE1_AKAO_SPU_VOICE_H
#define PE1_AKAO_SPU_VOICE_H

/* Gate sequenced BGM at the output while retaining native playback state. */
void Akao_SetMusicMuted(int muted);
void AkaoSpuVoice_SetVolume(unsigned int index, unsigned int left, unsigned int right);
void AkaoSpuVoice_SetPitch(unsigned int index, unsigned int value);
void AkaoSpuVoice_SetStartAddress(unsigned int index, unsigned int value);
void AkaoSpuVoice_SetRepeatAddress(unsigned int index, unsigned int value);
void AkaoSpuVoice_SetAdsrAttack(unsigned int index, unsigned int left, unsigned int right);
void AkaoSpuVoice_SetAdsrDecayRate(unsigned int index, unsigned int value);
void AkaoSpuVoice_SetAdsrSustainLevel(unsigned int index, unsigned int value);
void AkaoSpuVoice_SetAdsrSustainRate(unsigned int index, unsigned int left, unsigned int right);
void AkaoSpuVoice_SetAdsrReleaseRate(unsigned int index, unsigned int left, unsigned int right);

#endif
