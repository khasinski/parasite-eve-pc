/* SPDX-License-Identifier: MIT -- optional decoded dialogue ASCII only. */
#ifndef PE_DIALOGUE_ALPHA_H
#define PE_DIALOGUE_ALPHA_H
#include "pe1/textbox.h"
#include "pe1/render_prim.h"
void PeDialogue_SetEnabled(int enabled);
int PeDialogue_GetEnabled(void);
int PeDialogue_GetEffective(void);
/* Enabled is supported by explicit native font-reference ownership. */
int PeDialogue_StateSafe(void);
void PeDialogue_Prepare(void);
/* Source-owned field font-bank load notification; does not enable the option. */
void PeDialogue_FontBankLoaded(void);
void PeDialogue_Tag(RenderNativeTag *ordering,TextboxGlyphPacket *packet,unsigned code);
#if PE_NATIVE_STATE_CPU_COMPONENTS
#include <stddef.h>
#define PE_FONT_CONTEXT_STATE_BYTES (64u+64u*48u*2u)
#define PE_DIALOGUE_STATE_BYTES (16u+2u*PE_FONT_CONTEXT_STATE_BYTES)
int PeDialogue_StateCapture(unsigned char *,size_t);
int PeDialogue_StateValidate(const unsigned char *,size_t);
int PeDialogue_StateRestore(const unsigned char *,size_t);
#endif

/* Original Triangle-menu FT4 text only, independent default-Original setting. */
void PeMenuFont_SetEnabled(int);
int PeMenuFont_GetEnabled(void);
int PeMenuFont_GetEffective(void);
void PeMenuFont_Prepare(void);
void PeMenuFont_Tag(RenderNativeTag *,RenderTexturedQuad *,unsigned,unsigned,unsigned);
#if PE_NATIVE_STATE_CPU_COMPONENTS
int PeFont_StateCapture(unsigned,unsigned char *,size_t);
int PeFont_StateValidate(unsigned,const unsigned char *,size_t);
int PeFont_StateRestore(unsigned,const unsigned char *,size_t);
#endif

void PeMenuFont_SourceBegin(void);
void PeMenuFont_SourceUpload(const RECT *,const void *);

#endif /* PE_DIALOGUE_ALPHA_H */
