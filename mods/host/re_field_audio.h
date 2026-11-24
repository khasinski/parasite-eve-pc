#ifndef RE_FIELD_AUDIO_H
#define RE_FIELD_AUDIO_H

#include "pe_services.h"

#define PE_FIELD_AUDIO_SERVICE "re_field_audio"
#define PE_FIELD_AUDIO_VERSION 1

enum {
    PE_FIELD_MUSIC_MAIN, PE_FIELD_MUSIC_SUB_FIRST, PE_FIELD_MUSIC_SUB_SECOND,
    PE_FIELD_DIALOGUE, PE_FIELD_AUDIO_SLOTS
};

/* Replaceable output for imported play. Handles come from the assets service.
 * Play callbacks return 1 once started, 0 while the asset is pending, and -1
 * for unavailable audio. The caller may release the handle after either
 * terminal result. Pausing preserves stream positions and leaves native audio
 * running. Each one-shot effect may play alongside the current sequences. */
typedef struct PeFieldAudioApi {
    uint32_t struct_size;
    int (*play_song)(unsigned slot, PeAssetHandle handle, unsigned sequence, unsigned loops);
    void (*stop_song)(unsigned slot);
    void (*pause_songs)(int paused);
    int (*play_clip)(PeAssetHandle handle, float gain);
    int (*song_active)(unsigned slot);
    void (*set_song_gain)(unsigned slot, float gain);
} PeFieldAudioApi;

#endif
