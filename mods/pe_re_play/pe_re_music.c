#include "pe_re_music.h"

#include <stdio.h>
#include <string.h>

#include "pe_mod_api.h"
#include "re_field_audio.h"
#include "re_host.h"

/* PS1 RE2 INIT_TBL.DAT, the room cue map at offset 0x22c in each scenario
 * record. Low byte is the main bank, high byte is the two-sequence sub bank. */
static const uint8_t k_stage_base[] = {0x00, 0x1f, 0x3d, 0x4d, 0x60, 0x6c, 0x86, 142};
static const uint16_t k_room_music[5][142] = {
    { /* Leon A */
        0x010b, 0xc041, 0xff00, 0x0500, 0x010b, 0x0500, 0xc3ff, 0x0500, 0xc200, 0xc308,
        0xff04, 0xff08, 0xff08, 0xffff, 0xff08, 0xff0c, 0xff05, 0xcbff, 0xff02, 0xff08,
        0xffff, 0xff4d, 0xff2f, 0xff05, 0xff00, 0xff00, 0xff29, 0xff00, 0xffff, 0xffff,
        0xffff, 0xff05, 0xffff, 0xff06, 0xff07, 0xc449, 0xff07, 0xc807, 0xff07, 0xff0c,
        0xffff, 0xff07, 0xff19, 0xff07, 0xff07, 0xc4ff, 0xff07, 0xffff, 0xff03, 0xff00,
        0x0dff, 0xff03, 0xc041, 0xceff, 0xffff, 0xffff, 0xff03, 0xc3ff, 0xff08, 0xffff,
        0xffff, 0xffff, 0xc9ff, 0xff0f, 0xff55, 0xc90f, 0xff2e, 0xff14, 0x14ff, 0xff04,
        0x2eff, 0xff0f, 0xff0f, 0xff0c, 0xff0c, 0xffff, 0xffff, 0x990f, 0xff4c, 0xff0f,
        0xff0c, 0xff58, 0x980f, 0xffff, 0x990f, 0x1814, 0x9a14, 0xff5a, 0xc31b, 0xffff,
        0xff17, 0xff17, 0xc31b, 0xe214, 0xffff, 0xffff, 0xde5c, 0xff0e, 0xff0e, 0xff13,
        0x1f13, 0xc920, 0x1f13, 0x21ff, 0xff0e, 0xe420, 0xffff, 0xffff, 0x94ff, 0xffff,
        0xa05e, 0xa05e, 0xa55d, 0xa5ff, 0xff22, 0xd5ff, 0xa722, 0xffff, 0xffff, 0xff22,
        0xff22, 0xffff, 0x94ff, 0xff24, 0xffff, 0xa811, 0xff22, 0xff25, 0xff22, 0xadff,
        0xff22, 0xb452, 0xffff, 0xffff, 0xff11, 0xff11, 0xaf11, 0xff11, 0xff11, 0xffff,
        0xffff, 0x0000,
    },
    { /* Leon B */
        0x010b, 0xc041, 0xff00, 0x0500, 0x010b, 0x0500, 0xc3ff, 0x0500, 0xc200, 0x0708,
        0xff04, 0xc456, 0xff08, 0xffff, 0xff08, 0xff0c, 0xff05, 0xcbff, 0xff02, 0xcaff,
        0xffff, 0xff31, 0xff2f, 0xff05, 0xff00, 0xff00, 0xff29, 0xff00, 0xffff, 0xffff,
        0xffff, 0xff05, 0xff07, 0xff06, 0xff07, 0xff07, 0xff07, 0xc807, 0xff07, 0xff0c,
        0xffff, 0xff07, 0xff19, 0xff07, 0xff5e, 0xc4ff, 0xff07, 0xffff, 0xff03, 0xff00,
        0x0dff, 0xff03, 0xc041, 0xceff, 0xffff, 0xffff, 0xff03, 0xc3ff, 0xff08, 0xffff,
        0xffff, 0xffff, 0xc9ff, 0xff0f, 0xff50, 0xc90f, 0xff2e, 0xff14, 0x14ff, 0xff04,
        0x2eff, 0xff0f, 0xc90f, 0xff0c, 0xff0c, 0xffff, 0xffff, 0x990f, 0xff4c, 0xff0f,
        0xff0c, 0xff58, 0x980f, 0xffff, 0x990f, 0x1814, 0x9a14, 0xff5a, 0xc31b, 0xffff,
        0xff17, 0xff17, 0xc31b, 0xe214, 0xffff, 0xffff, 0xde5c, 0xff0e, 0xff0e, 0xff13,
        0x1f13, 0xc920, 0x1f13, 0x215e, 0xff0e, 0xe420, 0xffff, 0xffff, 0x94ff, 0xffff,
        0xa05e, 0xa05e, 0xa55d, 0xa5ff, 0xff22, 0xff0c, 0xa722, 0xffff, 0xffff, 0xff22,
        0xff22, 0xffff, 0x94ff, 0xff24, 0xffff, 0xa811, 0xff22, 0xff25, 0xff22, 0xadff,
        0xff22, 0xb452, 0xffff, 0xffff, 0xff11, 0xff11, 0xaf11, 0xff11, 0xff11, 0xffff,
        0xffff, 0x0000,
    },
    { /* Claire A */
        0x010b, 0xc041, 0xff00, 0x0500, 0x010b, 0x0500, 0xc3ff, 0x0500, 0xc200, 0xc308,
        0xff04, 0xff08, 0xff08, 0xffff, 0xc94a, 0xff0c, 0xff05, 0xcbff, 0xff02, 0xcaff,
        0xcaff, 0xcc31, 0xff2f, 0xff05, 0xff00, 0xff00, 0xff29, 0xff00, 0xffff, 0xffff,
        0xffff, 0xff05, 0xffff, 0xff06, 0xff07, 0xc449, 0xff07, 0xc807, 0xff07, 0xff0c,
        0xffff, 0xff07, 0xff19, 0xff07, 0xff07, 0xc4ff, 0xffff, 0xffff, 0xff03, 0xff00,
        0x0dff, 0xff03, 0xc041, 0xff03, 0xffff, 0xffff, 0xff03, 0xc3ff, 0xd1ff, 0xffff,
        0xffff, 0xd3ff, 0xffff, 0xff0f, 0xffff, 0xff0f, 0xff2e, 0xff14, 0x14ff, 0xd204,
        0x2e55, 0xd00f, 0xff0f, 0xff0c, 0xff0c, 0xffff, 0xffff, 0x965f, 0xff1f, 0xff0f,
        0xff0c, 0xffff, 0x980f, 0xffff, 0x990f, 0x9a14, 0x9814, 0xff5a, 0xc31b, 0x571f,
        0xff17, 0xff17, 0xa31f, 0xf614, 0xffff, 0xffff, 0xff1c, 0xff0e, 0xff0e, 0xff13,
        0x1f13, 0xc920, 0x1f13, 0x21ff, 0xff0e, 0xe420, 0xffff, 0xffff, 0x94ff, 0xffff,
        0xa0ff, 0xa0ff, 0xa55d, 0xa5ff, 0xff22, 0xe6ff, 0xa722, 0xffff, 0xffff, 0xff22,
        0xff22, 0xffff, 0x94ff, 0xff24, 0xffff, 0xff22, 0xff22, 0xff25, 0xff22, 0xadff,
        0xff22, 0xb452, 0xffff, 0xffff, 0xff11, 0xff11, 0xaf11, 0xff11, 0xff11, 0xffff,
        0xffff, 0x0000,
    },
    { /* Claire B */
        0x010b, 0xc041, 0xff00, 0x0500, 0x010b, 0x0500, 0xc3ff, 0x0500, 0xc200, 0x8708,
        0xff04, 0xc456, 0xff08, 0xffff, 0xc94a, 0xff0c, 0xff05, 0xcbff, 0xff02, 0xff08,
        0xcaff, 0xcc4d, 0xff2f, 0xff05, 0xff00, 0xff00, 0xff29, 0xff00, 0xffff, 0xffff,
        0xffff, 0xff05, 0xff07, 0xff06, 0xff07, 0xff07, 0xff07, 0xc807, 0xff07, 0xff0c,
        0xffff, 0xff07, 0xff19, 0xff07, 0xff5e, 0xc4ff, 0xffff, 0xffff, 0xff03, 0xff00,
        0x0dff, 0xff03, 0xc041, 0xff03, 0xffff, 0xffff, 0xff03, 0xc3ff, 0xd1ff, 0xffff,
        0xffff, 0xf261, 0xffff, 0xff0f, 0xffff, 0xff0f, 0xff2e, 0xff14, 0x14ff, 0xff04,
        0x2e50, 0xd00f, 0xff0f, 0xff0c, 0xff0c, 0xffff, 0xffff, 0x96ff, 0xff1f, 0xff0f,
        0xff0c, 0xffff, 0x980f, 0xffff, 0x990f, 0x9a14, 0x9814, 0xff5a, 0xc31b, 0x571f,
        0xff17, 0xdc17, 0xa31f, 0xff14, 0xffff, 0xffff, 0xff1c, 0xff0e, 0xff0e, 0xff13,
        0x1f13, 0xc920, 0x1f13, 0x215e, 0xff0e, 0xe420, 0xffff, 0xffff, 0x94ff, 0xffff,
        0xa0ff, 0xa0ff, 0xa55d, 0xa5ff, 0xff22, 0xff0c, 0xa722, 0xffff, 0xffff, 0xff22,
        0xff22, 0xffff, 0x94ff, 0xff24, 0xffff, 0xff22, 0xff22, 0xff25, 0xff22, 0xadff,
        0xff22, 0xb452, 0xffff, 0xffff, 0xff11, 0xff11, 0xaf11, 0xff11, 0xff11, 0xffff,
        0xffff, 0x0000,
    },
    { /* HUNK */
        0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xff28,
        0xff28, 0xffff, 0xff28, 0xffff, 0xffff, 0xff28, 0xff28, 0xffff, 0xff28, 0xff28,
        0xff28, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xf12d, 0xffff,
        0xffff, 0xff05, 0xff28, 0xffff, 0xffff, 0xff28, 0xff28, 0xffff, 0xff28, 0xffff,
        0xffff, 0xff27, 0xff27, 0xffff, 0xffff, 0xffff, 0xff27, 0xffff, 0xff27, 0xffff,
        0xffff, 0xffff, 0xffff, 0xff27, 0xffff, 0xffff, 0xff27, 0xff27, 0xffff, 0xffff,
        0xffff, 0xffff, 0xffff, 0xff27, 0xff27, 0xff67, 0xffff, 0xffff, 0xffff, 0xffff,
        0xffff, 0xffff, 0xff27, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
        0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
        0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
        0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
        0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
        0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
        0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
        0xffff, 0x0000,
    },
};

enum { MUSIC_SLOTS = PE_FIELD_DIALOGUE, VOICE_SLOT = PE_FIELD_DIALOGUE, PENDING_EFFECTS = 16 };
static PeAssetHandle s_voice;
static PeAssetHandle s_effects[PENDING_EFFECTS];
typedef struct {
    char id[32];
    PeAssetHandle handle;
    uint32_t revision;
    unsigned loops;
    int wanted, attempted;
} MusicSlot;

static MusicSlot s_slots[MUSIC_SLOTS];
static unsigned s_stage, s_room, s_scenario;
static int s_selected, s_muted, s_suspended, s_results;

static const PeFieldAudioApi *audio_api(void)
{
    const PeFieldAudioApi *audio = pe_service_get(PE_FIELD_AUDIO_SERVICE, PE_FIELD_AUDIO_VERSION);
    return audio && audio->struct_size >= offsetof(PeFieldAudioApi, pause_songs) + sizeof audio->pause_songs &&
        audio->play_song && audio->stop_song && audio->pause_songs ? audio : NULL;
}

static void mute_native(int muted)
{
    if (s_muted == muted) return;
    const ReHost *host = re_host();
    if (host->struct_size >= offsetof(ReHost, set_native_music_muted) + sizeof host->set_native_music_muted &&
        host->set_native_music_muted) host->set_native_music_muted(muted);
    s_muted = muted;
}

static void clear_slot(unsigned slot, const PeFieldAudioApi *audio, const PeAssetsApi *assets)
{
    MusicSlot *song = &s_slots[slot];
    if (song->attempted && audio) audio->stop_song(slot);
    if (song->handle && assets) assets->release(song->handle);
    song->handle = 0;
    song->attempted = 0;
}

static void clear_scene_audio(const PeFieldAudioApi *audio, const PeAssetsApi *assets)
{
    if (audio) audio->stop_song(VOICE_SLOT);
    if (assets) {
        if (s_voice) assets->release(s_voice);
        for (unsigned i = 0; i < PENDING_EFFECTS; ++i)
            if (s_effects[i]) assets->release(s_effects[i]);
    }
    s_voice = 0;
    memset(s_effects, 0, sizeof s_effects);
}

static void script_audio(PeReSession *session, const PeFieldAudioApi *audio, const PeAssetsApi *assets)
{
    PeReScdRun *run = &session->run;
    run->voice_managed = 1;
    if (run->voice_pending) {
        run->voice_pending = 0;
        if (s_voice) assets->release(s_voice);
        audio->stop_song(VOICE_SLOT);
        char id[40];
        snprintf(id, sizeof id, "re2:voice/%u/%u/%u", session->player,
                 run->voice_mode ? 0 : session->stage + 1, run->voice_track);
        s_voice = assets->acquire_song(id);
        pe_mod_log("pe_re_play", "script voice cue %s", id);
    }
    if (s_voice) {
        const int status = audio->play_song(VOICE_SLOT, s_voice, 0, 1);
        if (status) {
            pe_mod_log("pe_re_play", "script voice %s", status > 0 ? "playing" : "unavailable");
            assets->release(s_voice);
            s_voice = 0;
        }
    }
    if (audio->struct_size >= offsetof(PeFieldAudioApi, set_song_gain) + sizeof audio->set_song_gain &&
        audio->song_active && audio->set_song_gain) {
        run->voice_busy = s_voice || audio->song_active(VOICE_SLOT);
        if (run->voice_busy && run->voice_volume_set)
            audio->set_song_gain(VOICE_SLOT, run->voice_volume / 127.0f);
    } else run->voice_busy = s_voice != 0;
    if (audio->struct_size >= offsetof(PeFieldAudioApi, play_clip) + sizeof audio->play_clip &&
        audio->play_clip && assets->acquire_sound) {
        unsigned free_slot = 0;
        for (unsigned cue = 0; cue < run->sound_count; ++cue) {
            while (free_slot < PENDING_EFFECTS && s_effects[free_slot]) ++free_slot;
            if (free_slot == PENDING_EFFECTS) break;
            char id[56];
            snprintf(id, sizeof id, "re2:sound/room%x%02x%u/wave%02u",
                     session->stage + 1, session->room, session->player, run->sound_cues[cue]);
            s_effects[free_slot++] = assets->acquire_sound(id);
            pe_mod_log("pe_re_play", "script sound cue %s", id);
        }
        for (unsigned i = 0; i < PENDING_EFFECTS; ++i) {
            if (!s_effects[i]) continue;
            const int status = audio->play_clip(s_effects[i], 1.0f);
            if (!status) continue;
            pe_mod_log("pe_re_play", "script sound %s", status > 0 ? "playing" : "unavailable");
            assets->release(s_effects[i]);
            s_effects[i] = 0;
        }
    }
    run->sound_count = 0;
}

void pe_re_music_suspend(void)
{
    if (!s_muted) return;
    const PeFieldAudioApi *audio = audio_api();
    if (audio) audio->pause_songs(1);
    s_suspended = 1;
    mute_native(0);
}

void pe_re_music_stop(void)
{
    const PeFieldAudioApi *audio = audio_api();
    const PeAssetsApi *assets = pe_service_get("assets", 1);
    for (unsigned slot = 0; slot < MUSIC_SLOTS; ++slot) clear_slot(slot, audio, assets);
    clear_scene_audio(audio, assets);
    mute_native(0);
    s_selected = 0;
    s_suspended = 0;
    s_results = 0;
}

static void select_room(unsigned stage, unsigned room, unsigned scenario)
{
    uint16_t cue = 0xffff;
    if (scenario < 5 && stage < sizeof k_stage_base - 1 &&
        room < (unsigned)(k_stage_base[stage + 1] - k_stage_base[stage]))
        cue = k_room_music[scenario][k_stage_base[stage] + room];

    s_stage = stage;
    s_room = room;
    s_scenario = scenario;
    s_selected = 1;
    memset(s_slots, 0, sizeof s_slots);
    for (unsigned slot = 0; slot < MUSIC_SLOTS; ++slot) {
        const unsigned bank = slot ? cue >> 8 : cue & 0xff;
        if (bank == 0xff) continue;
        unsigned track = bank & 0x3f;
        if (slot ? track >= 0x38 : (track == 0x32 || track >= 0x3b)) track = 0;
        snprintf(s_slots[slot].id, sizeof s_slots[slot].id,
                 slot ? "re2:music/sub_%02x" : "re2:music/main%02x", track);
        const unsigned suppress = slot == 0 ? 0xc0 : slot == 1 ? 0x40 : 0x80;
        s_slots[slot].wanted = !(bank & suppress);
    }
}

static const PeAssetsApi *song_assets(void)
{
    const PeAssetsApi *assets = pe_service_get("assets", 1);
    return assets && assets->struct_size >= offsetof(PeAssetsApi, acquire_song) + sizeof assets->acquire_song &&
        assets->acquire_song && assets->release ? assets : NULL;
}

static void poll_slot(unsigned slot, const PeFieldAudioApi *audio, const PeAssetsApi *assets, const char *context)
{
    MusicSlot *song = &s_slots[slot];
    if (song->wanted && *song->id && !song->attempted) {
        song->handle = assets->acquire_song(song->id);
        song->attempted = 1;
    }
    if (!song->handle) return;
    const unsigned sequence = slot == PE_FIELD_MUSIC_SUB_SECOND;
    const int status = audio->play_song(slot, song->handle, sequence, song->loops);
    if (!status) return;
    if (status > 0) pe_mod_log("pe_re_play", "%s music: %s sequence %u", context, song->id, sequence);
    assets->release(song->handle);
    song->handle = 0;
}

void pe_re_music_results(void)
{
    if (!s_results) {
        pe_re_music_stop();
        s_results = 1;
        MusicSlot *song = &s_slots[PE_FIELD_MUSIC_MAIN];
        *song = (MusicSlot){.id = "re2:music/main2a", .wanted = 1}; /* ENDING.C: results cue */
    }
    mute_native(1);
    const PeFieldAudioApi *audio = audio_api();
    const PeAssetsApi *assets = song_assets();
    if (audio && assets) poll_slot(PE_FIELD_MUSIC_MAIN, audio, assets, "results");
}

void pe_re_music_update(PeReSession *session)
{
    if (strcmp(session->game->game, "re2")) {
        pe_re_music_stop();
        return;
    }
    if (s_results) pe_re_music_stop();
    const unsigned scenario = session->player_id == 12 ? 4 :
        session->player * 2 + pe_re_flag(&session->progress.flags, PE_RE_FG_STATUS, 1);
    const PeFieldAudioApi *audio = audio_api();
    const PeAssetsApi *assets = song_assets();
    if (!s_selected || s_stage != session->stage || s_room != session->room || s_scenario != scenario) {
        for (unsigned slot = 0; slot < MUSIC_SLOTS; ++slot) clear_slot(slot, audio, assets);
        clear_scene_audio(audio, assets);
        s_suspended = 0;
        select_room(session->stage, session->room, scenario);
    }
    mute_native(1);
    if (s_suspended) {
        if (audio) audio->pause_songs(0);
        s_suspended = 0;
        pe_mod_log("pe_re_play", "field music: resumed after combat");
    }
    if (!audio || !assets) {
        session->run.voice_busy = 0;
        session->run.voice_pending = 0;
        session->run.sound_count = 0;
        return;
    }

    script_audio(session, audio, assets);
    for (unsigned slot = 0; slot < MUSIC_SLOTS; ++slot) {
        MusicSlot *song = &s_slots[slot];
        if (song->revision != session->run.bgm_revision[slot]) {
            const unsigned command = session->run.bgm_command[slot];
            song->revision = session->run.bgm_revision[slot];
            clear_slot(slot, audio, assets);
            song->wanted = command == 1 || command == 3;
            if (command == 1) song->loops = 1;
        }
        poll_slot(slot, audio, assets, "field");
    }
}
