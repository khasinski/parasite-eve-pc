/* Platform boundary declarations used by the host-only session test. */
#ifndef PE_TEST_STATE_AUDIO_H
#define PE_TEST_STATE_AUDIO_H
int Psyz_StateKernelBegin(void);
void Psyz_StateKernelEnd(void);
int Psyz_StateAudioBegin(void);
void Psyz_StateAudioEnd(void);
int Psyz_StateAudioOwned(void);
#endif
