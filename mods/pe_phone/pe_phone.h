#ifndef PE_PHONE_H
#define PE_PHONE_H

/* Phone Calls (pe_phone): after Aya saves at a phone, "Stay on the line?" and, on Yes, a short
 * call in the game's own dialogue boxes. Bridge findings pe-phone-calls-plan-2026-11-08.md.
 *
 *   pe_phone_mod.c     the mod: descriptor, F1 rows, settings
 *   pe_phone_calls.c   the call registry ("phonecalls" v1), picking, the per-save record
 *   pe_phone_field.c   spotting the phone, the question, playing a call (field lock, boxes)
 *   pe_calls_pe1.c     Parasite Eve's own calls: a pack like any other */

#include "pe_services.h"

#define PE_PHONE_MOD_ID "pe_phone"

/* Settings (pe_phone_mod.c). */
enum { PE_PHONE_ASK = 0, PE_PHONE_ALWAYS = 1, PE_PHONE_OFF = 2 };
enum { PE_TONE_MIXED = 0, PE_TONE_SERIOUS = 1, PE_TONE_FUNNY = 2 };
int pe_phone_mode(void);
int pe_phone_tone_mask(void);

/* pe_phone_calls.c */
void pe_phone_calls_register(const char *mod_id);
const PePhoneCallsApi *pe_phone_calls(void);
void pe_phone_calls_played(const PePhoneCall *call);   /* mark heard (once calls, last five) */
int pe_phone_calls_count(void);
const PePhoneCall *pe_phone_call_at(int index);
unsigned pe_phone_heard_count(void);

/* pe_phone_field.c */
void pe_phone_field_register(const char *mod_id);
void pe_phone_field_debug_ask(void);    /* show the question at the next free field frame */
void pe_phone_field_debug_call(void);   /* play a call at the next free field frame */
void pe_phone_field_status(char *out, unsigned size);   /* last phone seen, for F1 */
/* Measure every registered call's boxes with the game's font, logging any that don't fit. */
int pe_phone_field_check_calls(void);

/* pe_calls_pe1.c */
void pe_calls_pe1_register(const PePhoneCallsApi *api);

/* The story day the save screen shows (1..6) for script variable 74. */
int pe_phone_story_day(unsigned story);

#endif
