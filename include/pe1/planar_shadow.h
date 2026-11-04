/* SPDX-License-Identifier: MIT. Optional narrow player-shadow prototype. */
#ifndef PE_PLANAR_SHADOW_H
#define PE_PLANAR_SHADOW_H
void PeShadow_SetStyle(int); /* 0 Original(default),1 planar player mesh */
int PeShadow_GetStyle(void);
void PeShadow_BeginFrame(void);
void PeShadow_EndFrame(void);
int PeShadow_StateIdle(void); /* derived work only; selector needs readonly identity */
#endif
