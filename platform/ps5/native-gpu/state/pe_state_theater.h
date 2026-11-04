#ifndef PE_STATE_THEATER_H
#define PE_STATE_THEATER_H
#include <stddef.h>
#include <stdint.h>
size_t PeStateTheater_Bytes(void);
int PeStateTheater_Walk(const unsigned char *,unsigned char *,int);
int PeStateTheater_RoomSupported(uint32_t,uint32_t);
#endif
