#include "pe1/dialogue_alpha.h"
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "common.h"
#include "pe1/asset_find.h"
#include <psyz/audio.h>
#include <psyz/cd.h>
#include "pe1/akao.h"
#include "pe1/akao/commands.h"
#include "pe1/akao/pan3d.h"
#include "pe1/asset_key_record.h"
#include "pe1/cdrom.h"
#include "pe1/cdrom_buffers.h"
#include "pe1/entity_frame_update.h"
#include "pe1/game_state.h"
#include "pe1/game_state_types.h"
#include "pe1/geom_state.h"
#include "pe1/gpu_image.h"
#include "pe1/gte.h"
#include "pe1/pe_image.h"
#include "pe1/psyq_cd.h"
#include "pe1/psyq_ds_queue.h"
#include "pe1/psyq_gpu.h"
#include "pe1/psyq_tim.h"
#include "pe1/render_buffers.h"
#include "pe1/render_camera.h"
#include "pe1/render_object.h"
#include "pe1/scene_assets.h"
#include "pe1/scene_room.h"

SceneRoomSectorRange g_RoomSectorTable[SCENE_ROOM_COUNT];
u16 g_PeImageSectorTable[81] = {
    0,71,72,87,121,126,131,153,156,157,180,197,
    200,203,220,237,254,271,288,316,340,368,396,428,
    434,442,448,456,464,472,478,483,489,497,503,510,
    519,526,532,537,539,541,544,547,550,553,570,587,
    604,621,638,655,672,689,706,723,740,757,774,791,
    808,825,842,859,876,893,910,927,965,969,978,1111,
    1276,1302,1547,1792,1975,2016,2105,2205,2224,
};

/* Resident USA key alphabet; room identifiers encode six five-bit digits. */
u8 g_Base32CharTable[32] = "0123456789abcdefghiklmnoprstuvwy";

/* Returns whether the TIM image of `type` placed at VRAM x = 832 sits off
 * row 256, or -1 when the scene has no such image. */
int Asset_SearchByKeyType(int type)
{
    void *blob;
    u32 key;
    TimPackedImage *image;
    u32 xMask, x832, yMask, y256;

    blob = g_LoadedSceneAssetBlock[0];
    key = 0x73DECD80;
    xMask = 0x7FFu << 10;
    x832 = 832 << 10;
    yMask = 0x7FFu << 21;
    y256 = 256 << 21;
    for (;;) {
        image = Asset_FindTable08ByU32Key(blob, key);
        if (image == 0) {
            return -1;
        }
        if (image->key.bytes.type == type && (image->geometry & xMask) == x832) {
            return (image->geometry & yMask) != y256;
        }
        key += 4;
    }
}

typedef struct AkaoPosPanWork {
    AkaoPackedRect3 rect;
    s32 out1;
    s32 out2;
} AkaoPosPanWork;

s32 Asset_Find08w(s32 arg0, s32 arg1, s16 arg2, s16 arg3, s16 arg4) {
    return Akao_SendPositionalCmd((void *)g_LoadedSceneAssetBlock[0], arg0, arg1, arg2, arg3, arg4);
}

s32 Akao_SetPos3D(s32 arg0, s32 arg1, u16 arg2, u16 arg3, u16 arg4) {
    AkaoPosPanWork local;
    s32 ret = 0;

    local.rect.x = arg2;
    local.rect.y = arg3;
    local.rect.z = arg4;
    Akao_Calc3DPan(&local.rect, &local.out1, &local.out2);

    {
        if (g_GameState.cd_range_read_mode != 0) {
            void **table = g_GameState.scene_object_tables;

            ret = Akao_Cmd_24(table[arg0], arg1, local.out1, local.out2);
        }
    }

    return ret;
}

s32 Akao_SendPositionalCmdStereo(s32 arg0, s32 arg1, s16 arg2, s16 arg3, s16 arg4) {
    s32 ret;
    void **basep = g_AkaoBgmHandle;

    ret = Akao_SendPositionalCmd(*basep, arg0, arg1, arg2, arg3, arg4);
    Akao_SendPositionalCmd(*basep, arg0 + 1, arg1, arg2, arg3, arg4);
    return ret;
}

s32 Asset_Find08Alt(s32 arg0, s32 arg1, s16 arg2, s16 arg3, s16 arg4) {
    return Akao_SendPositionalCmd(g_AkaoBgmHandle[0], arg0, arg1, arg2, arg3, arg4);
}

s32 Akao_SendPositionalCmd(void *arg0, s32 arg1, s32 arg2, AkaoPosCoord arg3, AkaoPosCoord arg4, AkaoPosCoord arg5) {
    AkaoPackedRect3 rect;
    s32 out1;
    s32 out2;

    rect.x = arg3;
    rect.y = arg4;
    rect.z = arg5;

    Akao_Calc3DPan(&rect, &out1, &out2);
    return Akao_SendTableCommand(arg0, arg1, arg2, out1, out2);
}


int Akao_Calc3DPan(AkaoPackedRect3 *pos, int *pan, int *volume)
{
    Pe1GameState *state;
    s32 sxy;
    s32 x;
    s32 y;
    GteShortVector position = {pos->x, pos->y, pos->z, 0};
    int depth;
    int far;
    int span;
    int low;

    Render_SetGteScreenOffset();
    state = &g_GameState;
    gte_ldrotmatrix(g_ViewMatrixSlot.value);
    gte_ldtransmatrix(g_ViewMatrixSlot.value);
    gte_ctc2_26(*g_GeomScroll.state.position.projectionDistance);
    depth = RotTransPers(&position, &sxy, &x, &y);
    x = (short)sxy;
    y = sxy >> 16;
    Render_ResetGteScreenOffset();

    *pan = ((x + 40) * 128) / 400 + 64;
    if ((unsigned int)*pan >= 256) {
        *pan = 255;
    }

    if (depth < g_GameState.bank_value_f8) {
        depth = g_GameState.bank_value_f8;
    } else if (g_GameState.bank_value_fa < depth) {
        depth = g_GameState.bank_value_fa;
    }

    far = state->bank_value_fa;
    depth = far - depth;
    depth = (s32)((u32)depth * (u32)depth) / (far - state->bank_value_f8);
    span = state->bank_value_f7;
    low = state->bank_value_f6;
    span -= low;
    *volume = depth * span / (far - state->bank_value_f8) + low;
    if ((unsigned int)*volume >= 128) {
        *volume = 127;
    }
    return 0;
}

/* Contiguous scene-container helpers: TIM upload, key encoding and lookup. */

int Text_EncodeBase32(char *out, unsigned int value) {
    int i;
    int last;
    char *dst;

    i = 0;
    last = 5;
    dst = out;
    while (i < 6) {
        int shift = ((last - i) * 5) + 2;
        u8 c = g_Base32CharTable[(value >> shift) & 0x1F];

        *dst = c;
        if (c - 0x61U < 0x1AU) {
            *dst = c - 0x20;
        }
        i++;
        dst++;
    }

    out[6] = 0;
    return 0;
}

int Text_DecodeBase32(unsigned int a0) {
    int total = 0;
    int i;

    for (i = 1; i < 5; i++) {
        int factor = 1;
        int j;
        s8 digit;

        for (j = 0; j < 4 - i; j++) {
            factor *= 10;
        }

        digit = g_Base32CharTable[(a0 >> ((5 * (5 - i)) + 2)) & 0x1F];
        total += (digit - '0') * factor;
    }

    return total;
}

int Text_ParseBase32Id(signed char *arg0) {
    unsigned int result = 0;
    int index = 0;

    while (index < 6) {
        signed char c = *arg0;
        int value = ((unsigned int)(c - '0') < 10U)
            ? (c - '0')
            : (c - 'W') - (c >= 'k') - (c >= 'r') - (c >= 'y');

        result |= (unsigned int)value << ((5 - index) * 5 + 2);
        index++;
        arg0++;
    }

    return result;
}

int Text_ParseMapNumber(signed char *arg0) {
    int hundreds;
    int tens;
    int result;

    hundreds = arg0[2] - 0x30;
    result = hundreds * 3;
    result <<= 3;
    result += hundreds;
    result <<= 2;

    tens = arg0[3] - 0x30;
    result += tens * 10;
    result -= 0x30;

    return result + arg0[4];
}

/* Scene display clearing and CD read flow share the scene read flags. */

void Draw_ClearOnFlag(void) {
    u32 *state = &g_GameState.flags;

    if (*state & 0x08000000) {
        RECT rect;
        unsigned int value;
        unsigned char byte;

        rect.x = 0;
        rect.y = 0;
        rect.w = 0x140;
        rect.h = 0x1C0;
        ClearImage(&rect, 0, 0, 1);
        DrawSync(0);
        if (Render_PrepareFrame() < 0) return;
        byte = g_DiscChangeFlags;
        value = *state;
        g_DiscChangeFlags = byte | 2;
        *state = value & 0xF7FFFDFF;
    }
}

/* Image access belongs to the original archive loader, shared by native callers. */

#include <ctype.h>
#include <stdarg.h>
#include <string.h>
#include <SDL3/SDL_stdinc.h>
FILE *Disc_Utf8File(const char *path,const char *mode) {
#ifdef _WIN32
    wchar_t *wide_path=(wchar_t *)SDL_iconv_string("UTF-16LE","UTF-8",path,strlen(path)+1);
    wchar_t *wide_mode=(wchar_t *)SDL_iconv_string("UTF-16LE","UTF-8",mode,strlen(mode)+1);
    FILE *file=wide_path&&wide_mode?_wfopen(wide_path,wide_mode):NULL;
    SDL_free(wide_path);SDL_free(wide_mode);return file;
#else
    return fopen(path,mode);
#endif
}


#include <stdlib.h>

#ifdef _WIN32
#define pe_seek _fseeki64
#define pe_tell _ftelli64
#else
#define pe_seek fseek
#define pe_tell ftell
#endif

static int fail(PeDisc *disc, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(disc->error, sizeof(disc->error), format, args);
    va_end(args);
    return 0;
}

static uint32_t le32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int read_image(PeDisc *disc, uint64_t offset, void *data, size_t size) {
    if (!disc->stream)
        return fail(disc, "Disc image is not mounted");
    if (offset > disc->image_size || size > disc->image_size - offset)
        return fail(disc, "Read exceeds the disc image");
    /* XA sample pulls and game asset reads share the mounted FILE. Keep
     * each seek/read pair atomic without depending on the audio backend. */
#ifdef _WIN32
    _lock_file(disc->stream);
#else
    flockfile(disc->stream);
#endif
    int read_ok=pe_seek(disc->stream,(int64_t)offset,SEEK_SET)==0 &&
                fread(data,1,size,disc->stream)==size;
#ifdef _WIN32
    _unlock_file(disc->stream);
#else
    funlockfile(disc->stream);
#endif
    if (!read_ok)
        return fail(disc, "Cannot read the disc image");
    return 1;
}

static int sector_payload(PeDisc *disc, uint32_t sector, void *data) {
    return read_image(disc, disc->track_offset + (uint64_t)sector * disc->sector_size +
                      disc->payload_offset, data, 2048);
}

static int decode_record(PeDisc *disc, const unsigned char *record,
                         size_t available, PeDiscFile *file) {
    unsigned size = record[0];
    if (size < 34 || size > available || record[32] > size - 33)
        return fail(disc, "Malformed ISO9660 directory record");
    if (record[25] & 0x80)
        return fail(disc, "Multi-extent ISO9660 files are not supported");
    file->sector = le32(record + 2);
    file->size = le32(record + 10);
    file->directory = (record[25] & 2) != 0;
    uint64_t end = (uint64_t)file->sector + ((uint64_t)file->size + 2047) / 2048;
    if (end > (disc->image_size - disc->track_offset) / disc->sector_size)
        return fail(disc, "ISO9660 file extent exceeds the disc image");
    return 1;
}

/* Deliberately reject layouts we cannot interpret instead of silently reading
 * the wrong track. The USA game uses one MODE2/2352 data track per disc. */
static int cue_image(PeDisc *disc, const char *path, char *image, size_t capacity) {
    FILE *cue = Disc_Utf8File(path, "rb");
    if (!cue) return fail(disc, "Cannot open CUE: %s", path);
    char line[4096], filename[4096] = {0};
    int have_file = 0, have_track = 0, have_index = 0, valid = 1;
    while (valid && fgets(line, sizeof(line), cue)) {
        if (!strchr(line, '\n') && !feof(cue)) { valid = 0; break; }
        char *p = line;
        while (isspace((unsigned char)*p)) ++p;
        char extra;
        if (!*p || strncmp(p, "REM ", 4) == 0) continue;
        if (strncmp(p, "FILE ", 5) == 0) {
            p += 5;
            while (isspace((unsigned char)*p)) ++p;
            char *start = p;
            if (*p == '"') { start = ++p; while (*p && *p != '"') ++p; }
            else while (*p && !isspace((unsigned char)*p)) ++p;
            size_t length = (size_t)(p - start);
            if (!length || length >= sizeof(filename) || have_file || have_track) {
                valid = 0; break;
            }
            memcpy(filename, start, length);
            filename[length] = 0;
            if (*p == '"') ++p;
            char type[16];
            if (sscanf(p, " %15s %c", type, &extra) != 1 || strcmp(type, "BINARY"))
                valid = 0;
            have_file = 1;
        } else if (strncmp(p, "TRACK ", 6) == 0) {
            unsigned number; char mode[32];
            if (!have_file || have_track ||
                sscanf(p + 6, "%2u %31s %c", &number, mode, &extra) != 2 ||
                number != 1 || strcmp(mode, "MODE2/2352")) valid = 0;
            have_track = 1;
        } else if (strncmp(p, "INDEX ", 6) == 0) {
            unsigned index, minutes, seconds, frames;
            if (!have_track || sscanf(p + 6, "%2u %2u:%2u:%2u %c", &index,
                    &minutes, &seconds, &frames, &extra) != 4 ||
                minutes > 99 || seconds >= 60 || frames >= 75 || index > 1 || have_index) {
                valid = 0; break;
            }
            if (index == 1) {
                disc->track_offset = ((uint64_t)minutes * 60 * 75 +
                                      seconds * 75 + frames) * 2352;
                have_index = 1;
            }
        } else valid = 0;
    }
    if (ferror(cue)) valid = 0;
    fclose(cue);
    if (!valid || !have_file || !have_track || !have_index)
        return fail(disc, "Expected single-track BINARY MODE2/2352 CUE with INDEX 01");
    size_t prefix = 0;
    if (filename[0] != '/' && filename[0] != '\\' &&
        !(isalpha((unsigned char)filename[0]) && filename[1] == ':')) {
        for (const char *p = path; *p; ++p)
            if (*p == '/' || *p == '\\') prefix = (size_t)(p - path + 1);
    }
    if (prefix + strlen(filename) + 1 > capacity)
        return fail(disc, "CUE image path is too long");
    memcpy(image, path, prefix);
    strcpy(image + prefix, filename);
    return 1;
}

int pe_disc_open(PeDisc *disc, const char *path) {
    unsigned char sector[2048];
    memset(disc, 0, sizeof(*disc));
    char image_path[8192];
    size_t length = strlen(path);
    int cue = length >= 4 && path[length - 4] == '.' &&
              tolower((unsigned char)path[length - 3]) == 'c' &&
              tolower((unsigned char)path[length - 2]) == 'u' &&
              tolower((unsigned char)path[length - 1]) == 'e';
    if (cue) {
        if (!cue_image(disc, path, image_path, sizeof(image_path))) return 0;
        path = image_path;
    }
    disc->stream = Disc_Utf8File(path, "rb");
    if (!disc->stream) return fail(disc, "Cannot open disc image: %s", path);
    if (pe_seek(disc->stream, 0, SEEK_END) != 0 || pe_tell(disc->stream) < 0) {
        pe_disc_close(disc);
        return fail(disc, "Cannot measure disc image");
    }
    disc->image_size = (uint64_t)pe_tell(disc->stream);
    if (disc->track_offset > disc->image_size) {
        pe_disc_close(disc);
        return fail(disc, "CUE INDEX 01 exceeds the disc image");
    }
    const uint32_t sizes[] = {2352, 2048};
    for (unsigned kind = 0; kind < (cue ? 1u : 2u); ++kind) {
        disc->sector_size = sizes[kind];
        disc->payload_offset = kind == 0 ? 24 : 0;
        if (!sector_payload(disc, 16, sector)) continue;
        if (sector[0] != 1 || memcmp(sector + 1, "CD001", 5) != 0 || sector[6] != 1)
            continue;
        if (sector[128] != 0 || sector[129] != 8) break;
        if (!decode_record(disc, sector + 156, sizeof(sector) - 156, &disc->root))
            break;
        if (!disc->root.directory) break;
        disc->error[0] = 0;
        return 1;
    }
    pe_disc_close(disc);
    return fail(disc, "Expected a MODE2/2352 BIN or 2048-byte ISO9660 image");
}

void pe_disc_close(PeDisc *disc) {
    if (disc->stream) fclose(disc->stream);
    disc->stream = NULL;
}

static int same_name(const unsigned char *name, size_t length,
                     const char *wanted, size_t wanted_length) {
    size_t stem = 0;
    while (stem < length && name[stem] != ';') ++stem;
    if (stem != wanted_length) return 0;
    for (size_t i = 0; i < stem; ++i)
        if (toupper(name[i]) != toupper((unsigned char)wanted[i])) return 0;
    return 1;
}

static int find_child(PeDisc *disc, const PeDiscFile *directory,
                      const char *name, size_t name_length, PeDiscFile *file) {
    unsigned char sector[2048];
    for (uint64_t offset = 0; offset < directory->size; offset += 2048) {
        if (!sector_payload(disc, directory->sector + (uint32_t)(offset / 2048), sector))
            return 0;
        size_t available = directory->size - offset < 2048 ?
                           (size_t)(directory->size - offset) : 2048;
        size_t cursor = 0;
        while (cursor < available && sector[cursor]) {
            const unsigned char *entry = sector + cursor;
            PeDiscFile candidate;
            if (!decode_record(disc, entry, available - cursor, &candidate)) return 0;
            if (same_name(entry + 33, entry[32], name, name_length)) {
                *file = candidate;
                return 1;
            }
            cursor += entry[0];
        }
    }
    return fail(disc, "Disc file not found: %.*s", (int)name_length, name);
}

int pe_disc_find(PeDisc *disc, const char *path, PeDiscFile *file) {
    PeDiscFile current = disc->root;
    while (*path == '/' || *path == '\\') ++path;
    while (*path) {
        const char *end = path;
        while (*end && *end != '/' && *end != '\\') ++end;
        const char *version = memchr(path, ';', (size_t)(end - path));
        size_t length = (size_t)((version ? version : end) - path);
        if (!current.directory) return fail(disc, "Path crosses a regular file");
        if (!length || !find_child(disc, &current, path, length, &current)) return 0;
        path = end;
        while (*path == '/' || *path == '\\') ++path;
    }
    *file = current;
    return 1;
}

int pe_disc_read(PeDisc *disc, const PeDiscFile *file, uint64_t offset,
                 void *data, size_t size) {
    unsigned char sector[2048];
    unsigned char *output = data;
    if (offset > file->size || size > file->size - offset)
        return fail(disc, "Read exceeds the file extent");
    while (size) {
        size_t start = (size_t)(offset % 2048);
        size_t count = size < 2048 - start ? size : 2048 - start;
        if (!sector_payload(disc, file->sector + (uint32_t)(offset / 2048), sector))
            return 0;
        memcpy(output, sector + start, count);
        offset += count;
        output += count;
        size -= count;
    }
    return 1;
}

int pe_disc_read_raw(PeDisc *disc, const PeDiscFile *file, uint32_t sector,
                     void *data) {
    if (disc->sector_size != 2352)
        return fail(disc, "Movie audio requires a raw MODE2/2352 disc image");
    if (sector >= ((uint64_t)file->size + 2047) / 2048)
        return fail(disc, "Raw sector exceeds the file extent");
    return read_image(disc, disc->track_offset +
                      ((uint64_t)file->sector + sector) * 2352, data, 2352);
}

PeDisc *g_CdImage;
static PeDiscFile mounted_archive;
static int completion;
void CdRom_BindImage(PeDisc *disc, const PeDiscFile *archive)
{
    if (disc != g_CdImage) {
        /* A disc change retires decoded XA as well as future sector reads.
         * Do not hold audio around the CD command (kernel/IRQ lock order). */
        CdControlB(8, NULL, NULL);
        Psyz_AudioLock();
        Psyz_CdSetReadCB(NULL);
        g_CdImage = disc;
        Psyz_AudioUnlock();
    }
    if (archive) mounted_archive = *archive;
    else memset(&mounted_archive, 0, sizeof(mounted_archive));
    completion = 0;
    g_GameState.flags &= ~0x1004000u;
}

/* Host I/O replaces drive negotiation and queues. Data is read immediately;
 * the original completion poll still releases the game's read-in-progress flags. */
int CdRom_ReadSectorsFromLba(u32 lba, void *destination, u32 sectors)
{
    if (g_GameState.flags & 0x1000000u) return -1;
    completion = -1;
    if (!g_CdImage || !destination || lba < mounted_archive.sector) return -1;
    uint64_t offset = (uint64_t)(lba - mounted_archive.sector) * 2048u;
    uint64_t bytes = (uint64_t)sectors * 2048u;
    if (offset > mounted_archive.size || bytes > mounted_archive.size - offset || bytes > SIZE_MAX) return -1;
    if (!pe_disc_read(g_CdImage, &mounted_archive, offset, destination, (size_t)bytes)) return -1;
    g_GameState.flags |= 0x1004000u;
    completion = 0;
    return 1;
}

int CdRom_PollReady(void)
{
    g_GameState.flags &= ~0x1004000u;
    return completion;
}

/* The original callers address an image sector plus an optional offset. */
int CdRom_ReadSectors(u32 lba, u32 offset, void *destination, u32 size)
{
    if (offset > UINT32_MAX - lba) return -1;
    return CdRom_ReadSectorsFromLba(lba + offset, destination, size);
}

s32 CdRom_LoadInitialImage(void) {
    s32 status;
    const u16 *range = g_StrFileDirLba;
    DISPENV sp18;

    if (!g_StrFileDirBuffer || range[1] <= range[0] ||
        (u32)(range[1] - range[0]) * 2048 > 0x80000 ||
        g_PeImageBaseLba > UINT32_MAX - range[0]) return -3;

    g_GameState.scene_audio.tracks.keys[0][1] = -1;
    g_GameState.scene_audio.tracks.keys[0][0] = -1;
    g_GameState.scene_audio.tracks.keys[1][1] = -1;
    g_GameState.scene_audio.tracks.keys[1][0] = -1;
    g_GameState.scene_audio.tracks.banks[1] = -1;
    g_GameState.scene_audio.tracks.banks[0] = -1;
    g_GameState.flags &= 0xFFFFFF0F;
    Akao_Cmd_F0();

    status = CdRom_ReadSectors(g_PeImageBaseLba + range[0], 0, g_StrFileDirBuffer, range[1] - range[0]);
    if (status < 0) return -1;

    while ((status = CdRom_PollReady()) != 0) {
        if (status < 0) return -2;
    }

    VSync(0);
    SetDispMask(0);
    SetDefDispEnv(&sp18, 0, 0, 0x140, 0xF0);
    sp18.isrgb24 = 1;
    PutDispEnv(&sp18);
    return 0;
}

int Akao_SendTableCommand(void *arg0, int arg1, int arg2, int arg3, int arg4) {
    void *entry = Asset_FindTable2CByU16Key(arg0, arg1);
    int ret;

    if (entry != 0) {
        ret = Akao_Cmd_24(entry, arg2, arg3, arg4);
    } else {
        ret = -1;
    }
    return ret;
}

/* Keyed record tables: SceneAssetDirectory word = 22-bit byte offset of the
 * records from the blob plus a 10-bit record count; each record holds the
 * 24-bit byte offset of its payload. Retail reserves an 8-byte frame for an
 * unused local in each lookup. */
#define DEFINE_ASSET_SEARCHER(name, base_type, key_type, table_member, record_type, offset_member) \
void *name(base_type base, key_type key) { \
    SceneAssetDirectory *directory; \
    record_type *records; \
    u32 descriptor; \
    u32 count; \
    u32 i; \
    void *found; \
    char unused[1]; \
 \
    directory = SceneAsset_ResolveOffset(base, ((SceneAssetBlob *)base)->directoryOffset); \
    descriptor = directory->table_member; \
    records = SceneAsset_ResolveOffset(base, descriptor & 0x3FFFFF); \
    count = descriptor >> 22; \
    found = 0; \
    for (i = 0; i < count; i++) { \
        if (records[i].key == key) { \
            found = SceneAsset_ResolveOffset(base, records[i].offset_member & 0xFFFFFF); \
            return found; \
        } \
    } \
    return found; \
}

DEFINE_ASSET_SEARCHER(Asset_FindTable08ByU32Key, void *, int, keyEntries, AssetU32KeyRecord, packedOffset)
void *Asset_FindTable2CByU16Key(char *base, int key) {
    /* Room buffers are reused while queued and active SFX still refer to their
     * programs. Keep the serialized programs alive, sharing identical bytes
     * across rooms; sequencer PCs and loop targets then remain valid. */
    struct StreamCopy {
        struct StreamCopy *next;
        size_t size;
        u8 bytes[];
    };
    static struct StreamCopy *copies;
    SceneAssetDirectory *directory = SceneAsset_ResolveOffset(
        base, ((SceneAssetBlob *)base)->directoryOffset);
    u32 packed = directory->streamEntries;
    SceneCdStreamRecord *records = SceneAsset_ResolveOffset(base, packed & 0x3FFFFF);

    for (u32 i = 0; i < packed >> 22; ++i) {
        if (records[i].key == key) {
            size_t size = records[i].reserved & 0xFFFFFF;
            const u8 *program = SceneAsset_ResolveOffset(base, records[i].offset & 0xFFFFFF);
            struct StreamCopy *copy;
            if (size < 8) return NULL;
            for (copy = copies; copy; copy = copy->next) {
                if (copy->size == size && !memcmp(copy->bytes, program, size))
                    return copy->bytes;
            }
            copy = malloc(sizeof(*copy) + size);
            if (!copy) return NULL;
            copy->size = size;
            memcpy(copy->bytes, program, size);
            copy->next = copies;
            copies = copy;
            return copy->bytes;
        }
    }
    return NULL;
}
DEFINE_ASSET_SEARCHER(Asset_FindTable30ByU16Key, char *, int, trackEntries, SceneTrackRecord, offset)


int Draw_LoadTimAsset(TimUploadRecord *asset, void *base) {
    RECT rect;
    int mask;
    u8 *saved_base;
    unsigned int packed;
    int h;
    unsigned int offset;
    int secondary;
    unsigned int image_offset;
    u8 *image_address;

    packed = asset->image_rectangle;
    rect.x = (packed >> 10) & 0x7FF;
    packed = asset->image_rectangle;
    rect.y = packed >> 21;
    packed = asset->image_rectangle;
    rect.w = packed & 0x3FF;
    saved_base = base;
    packed = asset->image_source.bytes.height;
    if (packed != 0) {
        h = packed & 0xFF;
    } else {
        h = 0x100;
    }

    mask = 0xFFFFFF;
    rect.h = h;
#ifdef PE_PS5_GPU_NATIVE
    PeMenuFont_SourceUpload(&rect,(void *)(saved_base + (asset->image_source.offsetAndHeight & mask)));
#endif
    LoadImage(&rect, (void *)(saved_base + (asset->image_source.offsetAndHeight & mask)));

    secondary = asset->secondary_source.offsetAndHeight & mask;
    if (secondary != 0) {
        packed = asset->secondary_rectangle;
        rect.x = (packed >> 10) & 0x7FF;
        packed = asset->secondary_rectangle;
        rect.y = packed >> 21;
        packed = asset->secondary_rectangle;
        rect.w = packed & 0x3FF;
        rect.h = asset->secondary_source.bytes.height;
        image_offset = asset->image_source.offsetAndHeight & mask;
        image_address = saved_base + image_offset;
        offset = asset->secondary_source.offsetAndHeight & mask;
#ifdef PE_PS5_GPU_NATIVE
        PeMenuFont_SourceUpload(&rect,(void *)(offset + image_address));
#endif
        LoadImage(&rect, (void *)(offset + image_address));
    }

    return 0;
}

/* USA resident audio bank directory: archive-relative base and 251 bounds.
 * The mounted EXE supplies these serialized data values before bank I/O. */
_Alignas(u32) u8 g_SceneSectorDirectory[4 + 2 * 251];
