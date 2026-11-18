/* Independent public-service consumer. The Import mod is absent from this executable. */
#include <SDL3/SDL.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pe_mod_host.h"
#include "pe_services.h"

extern const PeMod pe_mod_pe_assets;
extern int pe_imp_mkdirs(const char *path);

#define STORE "pe-assets-test/import/store"
#define HASH "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"

static const void *s_assets, *s_audio;

int pe_mod_path(char *out, size_t n, const char *file)
{
    return snprintf(out, n, "pe-assets-test/%s", file) >= (int)n;
}

int pe_mod_active(const char *owner) { return !strcmp(owner, "pe_assets"); }

int pe_service_publish(const char *name, int version, const void *api, const char *owner)
{
    assert(version == 1 && pe_mod_active(owner));
    if (!strcmp(name, "assets")) s_assets = api;
    else if (!strcmp(name, "audio")) s_audio = api;
    else return -1;
    return 0;
}

const void *pe_service_get(const char *name, int version)
{
    if (version != 1) return NULL;
    if (!strcmp(name, "assets")) return s_assets;
    if (!strcmp(name, "audio")) return s_audio;
    return NULL;
}

static void write_fixture(const char *path, const void *data, size_t size)
{
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(data, 1, size, file) == size);
    assert(!fclose(file));
}

int main(void)
{
    assert(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    assert(SDL_Init(SDL_INIT_AUDIO));
    assert(!pe_imp_mkdirs(STORE "/sample/" HASH "/0"));
    static const char catalog[] =
        "{\"sample:sound/test\":{\"name\":\"Generated pulse\",\"type\":\"Sound\",\"tags\":\"test\","
        "\"sources\":[{\"sha256\":\"" HASH "\",\"item\":\"0\",\"platform\":\"pc\"}]}}";
    static const unsigned char pcm[] = {'P','C','M','1',0x44,0xac,0,0,4,0,0,0,0,0,0,0x20,0,0xe0,0,0};
    write_fixture(STORE "/sample/catalog.json", catalog, sizeof catalog - 1);
    write_fixture(STORE "/sample/" HASH "/0/sound.bin", pcm, sizeof pcm);

    assert(!pe_mod_pe_assets.init());
    const PeAssetsApi *assets = pe_service_get("assets", 1);
    const PeAudioApi *audio = pe_service_get("audio", 1);
    assert(assets && assets->struct_size >= sizeof *assets && audio && audio->struct_size >= sizeof *audio);
    PeAssetInfo info;
    assert(assets->list_sounds("sample", 0, &info, 1) == 1);
    assert(assets->list_sounds("sample", 1, &info, 1) == 0);
    assert(!strcmp(info.id, "sample:sound/test"));

    PeAssetHandle first = assets->acquire_sound(info.id);
    PeAssetHandle second = assets->acquire_sound(info.id);
    assert(first && first == second);
    assets->release(first);
    uint64_t deadline = SDL_GetTicks() + 2000;
    int rc;
    while ((rc = assets->status(second)) == 0 && SDL_GetTicks() < deadline) SDL_Delay(1);
    assert(rc == 1 && audio->preview_clip(second) == 1);
    assets->release(second);
    assert(assets->status(second) == -1);

    PeAssetHandle missing = assets->acquire_sound("sample:sound/missing");
    assert(missing);
    deadline = SDL_GetTicks() + 2000;
    while ((rc = assets->status(missing)) == 0 && SDL_GetTicks() < deadline) SDL_Delay(1);
    assert(rc == -1 && audio->preview_clip(missing) == -1);
    assets->release(missing);
    pe_mod_pe_assets.shutdown();
    SDL_Quit();
    return 0;
}
