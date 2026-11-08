#ifndef PE_PHONE_H
#define PE_PHONE_H

/* Phone Calls (pe_phone): after Aya saves at a phone, "Stay on the line?" and, on Yes, a short
 * call in the game's own dialogue boxes. Bridge findings pe-phone-calls-plan-2026-11-08.md.
 *
 * Each file does one job and talks to the others only through this header:
 *
 *   pe_phone_mod.c      the mod: descriptor, F1 rows, settings
 *   pe_phone_service.c  the call registry ("phonecalls" v1) and this save's heard record
 *   pe_phone_spot.c     spotting a phone save: the phone's question, then save.committed
 *   pe_phone_flow.c     after the save: the question, then the call, page by page
 *   pe_phone_box.c      the game side of showing it: our dialogue box, field lock, pad, font
 *   pe_phone_sound.c    the phone's own sounds: pick up and dial, put down
 *   pe_calls_pe1.c      Parasite Eve's own calls: a pack like any other
 *
 * and the pure core (pe_phone_core.h: text and pages, the record, picking), which tests link alone. */

#include "pe_phone_core.h"

#define PE_PHONE_MOD_ID "pe_phone"

/* Field action bits (Boot_BuildRenderFlagTable): confirm, cancel, the choice cursor's up and down. */
#define PE_PHONE_PAD_CROSS  0x0100u
#define PE_PHONE_PAD_CIRCLE 0x0400u
#define PE_PHONE_PAD_UP     0x0008u
#define PE_PHONE_PAD_DOWN   0x0020u

/* ---- settings (pe_phone_mod.c) ---- */
enum { PE_PHONE_ASK = 0, PE_PHONE_ALWAYS = 1, PE_PHONE_OFF = 2 };
enum { PE_TONE_MIXED = 0, PE_TONE_SERIOUS = 1, PE_TONE_FUNNY = 2 };
int pe_phone_mode(void);
int pe_phone_tone_mask(void);

/* ---- the registry and the record (pe_phone_service.c) ---- */
void pe_phone_service_register(const char *mod_id);
const PePhoneCallsApi *pe_phone_calls(void);
int pe_phone_calls_count(void);
const PePhoneCall *pe_phone_call_at(int index);
void pe_phone_calls_played(const PePhoneCall *call);   /* into this save's heard record */
unsigned pe_phone_heard_count(void);
int pe_phone_story_day(unsigned story);                /* the save screen's Day (1..6) for variable 74 */

/* ---- spotting (pe_phone_spot.c) ---- */
typedef struct PePhoneSpot {
    int room;                          /* map number, -1 unknown */
    unsigned story;                    /* script variable 74 */
} PePhoneSpot;
void pe_phone_spot_register(const char *mod_id);
void pe_phone_spot_tick(int settled);                  /* every idle frame: room changes, unsaved questions */
int pe_phone_spot_ready(PePhoneSpot *out);             /* a committed phone save is waiting */
void pe_phone_spot_clear(void);
void pe_phone_spot_quiet(int on);                      /* ignore the phone's pages while a call is up */
void pe_phone_spot_inject(void);                       /* debug: as if saved at a phone here */
void pe_phone_spot_status(char *out, unsigned size);   /* the last phone seen, for F1 */

/* ---- the flow (pe_phone_flow.c) ---- */
void pe_phone_flow_register(const char *mod_id);
void pe_phone_flow_debug(int mode);                    /* PE_PHONE_ASK or PE_PHONE_ALWAYS at the next settled frame */
int pe_phone_flow_check_calls(void);                   /* every call laid out with the game's font; problems logged */

/* ---- the game side (pe_phone_box.c) ---- */
int pe_phone_font_ready(void);
int pe_phone_font_advance(uint8_t glyph);              /* a PePhoneAdvance with the game's font and name */
int pe_phone_box_show(const uint8_t *text, int style, int lines, int width);   /* 0: no box free */
int pe_phone_box_open(void);                           /* ours, and still showing */
const uint8_t *pe_phone_box_message(void);
int pe_phone_box_any(void);                            /* any dialogue box at all */
int pe_phone_box_choice(void);                         /* the choice cursor's row */
void pe_phone_box_close(void);
void pe_phone_box_forget(void);                        /* room load / loaded save: no box, no lock owed */
void pe_phone_lock(void);
void pe_phone_unlock(void);
void pe_phone_pad_take(void);                          /* PE_HOOK_FRAME: the pad, away from the field */
uint32_t pe_phone_pad_pressed(void);                   /* this frame's presses (ours) */
void pe_phone_pad_give(int cursor);                    /* after the field logic; cursor: up/down for a choice */

/* ---- the phone's own sounds (pe_phone_sound.c) ---- */
enum { PE_PHONE_SOUND_DIAL = 0, PE_PHONE_SOUND_HANG_UP = 1 };
int pe_phone_sounds_on(void);                          /* setting (pe_phone_mod.c) */
int pe_phone_sound_base(int room);                     /* the room's phone sound base id; 0: not a save phone's room */
void pe_phone_sound_play(int cue, int room);           /* pick up + dial, or put down */
void pe_phone_sound_tick(void);                        /* every frame: steps and stops */
void pe_phone_sound_stop(void);

/* ---- the game's own calls (pe_calls_pe1.c) ---- */
void pe_calls_pe1_register(const PePhoneCallsApi *api);

#endif
