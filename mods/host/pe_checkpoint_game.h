#ifndef PE_CHECKPOINT_GAME_H
#define PE_CHECKPOINT_GAME_H
int pe_checkpoint_build_card(void);
int pe_checkpoint_native_valid(const unsigned char *);
int pe_checkpoint_capture_portable(unsigned char *,unsigned,unsigned *);
int pe_checkpoint_prepare_portable(const unsigned char *,unsigned);
int pe_checkpoint_commit_portable(void);
#endif
