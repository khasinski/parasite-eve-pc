/* The phone's own sounds on a call: picking up and the tone twice ("ring... ring...") on the ring
 * page, putting the phone down on "*click*".
 *
 * Every save phone's script plays four sounds from its area's sound bank, from a base id A
 * (script opcode 234, Akao command 0x15F: Asset_Find08w at the phone's position; 0x12D stops one
 * by its handle, Akao_Cmd_21(handle, 0)):
 *   after Yes to "Will you save your progress?":  A for 30 frames, then A+1 for 30   (pick up, dial)
 *   after the save screen:                        A+3 for 35 frames, then A+2 for 20 (put it down)
 * A call plays the same pairs with the same timing, at Aya's position. The ids belong to each
 * area's bank, so they come from a table of the phone rooms, read from both US discs' room scripts
 * (pe-native-debug tools/phone_rooms.py). The table is also the list of save phones: every other
 * "Will you save your progress?" is not a phone (a new day's save, the story save, the debug room)
 * and gets no call (pe_phone_spot.c). */
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_phone.h"
#include "pe1/asset_find.h"
#include "pe1/fixed_point.h"
#include "pe1/player_entity.h"
#include "pe1/field_actor.h"

void Akao_Cmd_21(int handle, int arg);

/* Phone rooms and their sound base A (USA discs). */
static const struct { short room; short base; } k_phones[] = {
    {20, 0x7E5},
    {42, 0x87E}, {59, 0x87E}, {97, 0x87E}, {99, 0x87E}, {112, 0x87E},
    {64, 0x904},
    {75, 0x908}, {87, 0x908},
    {126, 0x9BA},
    {138, 0x9A3}, {163, 0x9A3}, {325, 0x9A3},
    {154, 0x9C0},
    {176, 0x9FA},
    {184, 0xA0D},
    {195, 0x8CC},
    {196, 0xA85}, {237, 0xA85}, {238, 0xA85}, {340, 0xA85},   /* M238 plays A+1, A+2 another way (0x12C) */
    {266, 0xA6B},
    {276, 0xA4C},
};

/* Heard headless (--dump-audio, sounds on minus sounds off): A a short pick-up, A+1 a steady tone,
 * A+3 a steady tone, A+2 a louder put-down. Frames are game frames (30 a second, as the script's
 * waits). The game has no ringing sound, so "ring... ring..." is the tone twice with a pause;
 * it stops when the other side picks up (pe_phone_flow.c). offset -1: silence. */
typedef struct { int offset, frames; } PeStep;
static const PeStep k_dial[] = {{0, 30}, {1, 30}, {-1, 30}, {1, 30}};
static const PeStep k_hang_up[] = {{3, 35}, {2, 20}};

static const PeStep *s_steps;
static int s_count, s_at, s_left, s_base, s_handle;

int pe_phone_sound_base(int room) {
    unsigned i;
    for (i = 0; i < sizeof k_phones / sizeof k_phones[0]; ++i)
        if (k_phones[i].room == room) return k_phones[i].base;
    return 0;
}

static void pe_stop_one(void) {
    if (s_handle) Akao_Cmd_21(s_handle, 0);
    s_handle = 0;
}

static void pe_start_step(void) {
    const FieldActor *aya = g_PlayerEntity;
    const int id = s_base + s_steps[s_at].offset;
    s_left = s_steps[s_at].frames;
    if (s_steps[s_at].offset < 0) return;
    s_handle = aya ? Asset_Find08w(id, 0, Pe1Fixed_Integer(&aya->pos_x), Pe1Fixed_Integer(&aya->pos_y),
                                   Pe1Fixed_Integer(&aya->pos_z)) : 0;
    pe_log("phone_sound", "id=0x%X handle=%d", id, s_handle);
}

void pe_phone_sound_stop(void) {
    pe_stop_one();
    s_steps = NULL;
    s_count = s_at = 0;
}

void pe_phone_sound_play(int cue, int room, int on_phone) {
    pe_phone_sound_stop();
    if (!pe_phone_sounds_on() || !(s_base = pe_phone_sound_base(room)) || !g_PlayerEntity) return;
    if (cue != PE_PHONE_SOUND_DIAL && on_phone) return;   /* the phone's own put-down plays */
    if (cue == PE_PHONE_SOUND_DIAL) { s_steps = k_dial; s_count = (int)(sizeof k_dial / sizeof k_dial[0]); }
    else { s_steps = k_hang_up; s_count = (int)(sizeof k_hang_up / sizeof k_hang_up[0]); }
    s_at = cue == PE_PHONE_SOUND_DIAL && on_phone ? 1 : 0;   /* still holding it: no pick-up */
    pe_start_step();
}

void pe_phone_sound_tick(void) {
    if (!s_steps || --s_left > 0) return;
    pe_stop_one();
    if (++s_at < s_count) pe_start_step();
    else pe_phone_sound_stop();
}
