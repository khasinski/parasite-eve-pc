/* Catalog preview service. Source parsing stays on the import worker. */
#include <SDL3/SDL.h>
#include <psyz/audio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pe_imp.h"
#include "pe_mod_host.h"
#include "pe_services.h"

#define MOD_ID "pe_assets"
#define SLOTS 4

typedef struct {
    char id[PE_IMP_ID_MAX];
    uint32_t type;
    void *data;
    SDL_Thread *worker;
    SDL_AtomicInt result;
    unsigned refs;
    PeAssetHandle handle;
} Clip;

static Clip s_clips[SLOTS];
static PeAssetHandle s_next;
static SDL_AudioStream *s_preview;
static unsigned s_rate;
static char s_store[PE_IMP_PATH_MAX];

static void reap(void)
{
    for (unsigned i = 0; i < SLOTS; ++i) {
        Clip *clip = &s_clips[i];
        if (clip->worker && SDL_GetAtomicInt(&clip->result)) {
            SDL_WaitThread(clip->worker, NULL);
            clip->worker = NULL;
        }
        if (!clip->refs && !clip->worker && clip->handle) {
            free(clip->data);
            memset(clip, 0, sizeof *clip);
        }
    }
}

static Clip *lookup(PeAssetHandle handle)
{
    if (!handle) return NULL;
    for (unsigned i = 0; i < SLOTS; ++i)
        if (s_clips[i].handle == handle && s_clips[i].refs) return &s_clips[i];
    return NULL;
}

static int load_clip(void *arg)
{
    Clip *clip = arg;
    int rc;
    if (clip->type == PE_IMP_SONG) {
        PeImpSong *song;
        rc = pe_imp_catalog_song(s_store, clip->id, &song, NULL, 0);
        clip->data = song;
    } else {
        PeImpSound *sound;
        rc = pe_imp_catalog_sound(s_store, clip->id, &sound, NULL, 0);
        clip->data = sound;
    }
    SDL_SetAtomicInt(&clip->result, rc ? -1 : 1);
    return 0;
}

static PeAssetHandle acquire(const char *id, uint32_t type)
{
    if (!pe_mod_active(MOD_ID) || !id || !*id || strlen(id) >= PE_IMP_ID_MAX) return 0;
    reap();
    Clip *empty = NULL;
    for (unsigned i = 0; i < SLOTS; ++i) {
        Clip *clip = &s_clips[i];
        if (clip->handle && clip->type == type && !strcmp(clip->id, id)) {
            if (clip->refs == UINT32_MAX) return 0;
            ++clip->refs;
            return clip->handle;
        }
        if (!clip->handle && !empty) empty = clip;
    }
    if (!empty) return 0;
    snprintf(empty->id, sizeof empty->id, "%s", id);
    if (!++s_next) ++s_next;
    empty->handle = s_next;
    empty->refs = 1;
    empty->type = type;
    empty->worker = SDL_CreateThread(load_clip, "pe_asset_clip", empty);
    if (!empty->worker) {
        memset(empty, 0, sizeof *empty);
        return 0;
    }
    return empty->handle;
}

static PeAssetHandle acquire_sound(const char *id) { return acquire(id, PE_IMP_SOUND); }
static PeAssetHandle acquire_song(const char *id) { return acquire(id, PE_IMP_SONG); }

static int status(PeAssetHandle handle)
{
    if (!pe_mod_active(MOD_ID)) return -1;
    reap();
    Clip *clip = lookup(handle);
    return clip ? SDL_GetAtomicInt(&clip->result) : -1;
}

static void release(PeAssetHandle handle)
{
    Clip *clip = lookup(handle);
    if (clip) --clip->refs;
    reap();
}

static int list_assets(const char *game, const char *type, unsigned offset,
                       PeAssetInfo *out, unsigned capacity)
{
    if (!pe_mod_active(MOD_ID) || !game || !*game || strlen(game) > 15 ||
        strlen(game) != strspn(game, "abcdefghijklmnopqrstuvwxyz0123456789-_") ||
        !out || !capacity || capacity > 256)
        return -1;
    PeImpAsset *assets;
    int count = pe_imp_catalog_assets(s_store, game, &assets);
    unsigned written = 0;
    for (int i = 0; i < count && written < capacity; ++i) {
        if (strcmp(assets[i].type, type)) continue;
        if (offset) { --offset; continue; }
        PeAssetInfo *info = &out[written++];
        snprintf(info->id, sizeof info->id, "%s", assets[i].id);
        snprintf(info->name, sizeof info->name, "%s", assets[i].name);
        snprintf(info->tags, sizeof info->tags, "%s", assets[i].tags);
    }
    free(assets);
    return count < 0 ? -1 : (int)written;
}

static int list_sounds(const char *game, unsigned offset, PeAssetInfo *out, unsigned capacity)
{
    return list_assets(game, "Sound", offset, out, capacity);
}

static int list_songs(const char *game, unsigned offset, PeAssetInfo *out, unsigned capacity)
{
    return list_assets(game, "Song", offset, out, capacity);
}

static int song_view(PeAssetHandle handle, PeSongView *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof *out);
    int ready = status(handle);
    if (ready != 1) return ready;
    const Clip *clip = lookup(handle);
    if (clip->type != PE_IMP_SONG) return -1;
    const PeImpSong *song = clip->data;
    const uint8_t *secondary = song->payload + song->seq_size[0];
    const uint8_t *instruments = secondary + song->seq_size[1];
    *out = (PeSongView){
        .format = PE_SONG_PSX_SEQ_VAB,
        .sequence = {song->payload, song->seq_size[1] ? secondary : NULL},
        .sequence_size = {song->seq_size[0], song->seq_size[1]},
        .instruments = instruments, .samples = instruments + song->instrument_size,
        .instrument_size = song->instrument_size, .sample_size = song->sample_size
    };
    return 1;
}

static int preview_clip(PeAssetHandle handle)
{
    int ready = status(handle);
    if (ready != 1) return ready;
    const Clip *clip = lookup(handle);
    if (clip->type != PE_IMP_SOUND) return -1;
    const PeImpSound *sound = clip->data;
    if (!s_preview || sound->rate != s_rate) {
        SDL_DestroyAudioStream(s_preview);
        SDL_AudioSpec spec = {.format = SDL_AUDIO_S16, .channels = 1, .freq = (int)sound->rate};
        s_preview = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
        if (!s_preview) return -1;
        s_rate = sound->rate;
    }
    if (!SDL_ClearAudioStream(s_preview) ||
        !SDL_SetAudioStreamGain(s_preview, (float)Psyz_AudioGetOutputVolume() / 100.0f) ||
        !SDL_PutAudioStreamData(s_preview, sound->pcm, (int)(sound->frames * sizeof *sound->pcm)) ||
        !SDL_FlushAudioStream(s_preview) || !SDL_ResumeAudioStreamDevice(s_preview)) return -1;
    return 1;
}

static const PeAssetsApi k_assets = {
    sizeof(PeAssetsApi), list_sounds, acquire_sound, status, release,
    list_songs, acquire_song, song_view
};
static const PeAudioApi k_audio = {sizeof(PeAudioApi), preview_clip};

static int init(void)
{
    if (pe_mod_path(s_store, sizeof s_store, "import/store")) return 1;
    return pe_service_publish("assets", 1, &k_assets, MOD_ID) ||
           pe_service_publish("audio", 1, &k_audio, MOD_ID);
}

static void shutdown(void)
{
    SDL_DestroyAudioStream(s_preview);
    s_preview = NULL;
    for (unsigned i = 0; i < SLOTS; ++i) {
        if (s_clips[i].worker) SDL_WaitThread(s_clips[i].worker, NULL);
        free(s_clips[i].data);
    }
    memset(s_clips, 0, sizeof s_clips);
}

const PeMod pe_mod_pe_assets = {MOD_ID, "Imported asset services", "0.1", NULL, init, shutdown};
