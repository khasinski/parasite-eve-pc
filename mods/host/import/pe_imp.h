#ifndef PE_IMP_H
#define PE_IMP_H
/*
 * The importer core (design: "Cross-Game Asset Import"): other PlayStation games the player owns,
 * read from disc images in import mode, into a store of neutral data and a catalog of stable IDs,
 * then built into Parasite Eve packs. Plain C with stdio only, for PC and PS5.
 *
 *   disc images   pe_imp_disc_*: a .cue (first data track), a raw .bin or an .iso; ISO 9660 files
 *   data types    Model and Sound, with bounded save and load
 *   readers       registered by source-game mods (pe_src_<game>): file pattern -> neutral data
 *   store         <root>/<game>/<sha256 of the file>/<item>/..., rebuilt when a reader's version changes
 *   catalog       <root>/<game>/catalog.json: "<game>:<kind>/<name>" -> its copies in the store
 *
 * Foreign files are hostile input: every reader bounds-checks every offset and count and refuses
 * with a reason instead of reading past its buffer. Nothing here runs during play.
 * Functions return 0 on success and -1 with a reason in `why` (may be NULL).
 */
#include <stddef.h>
#include <stdint.h>

#define PE_IMP_API_VERSION 1
#define PE_IMP_FILE_MAX    (64u << 20)      /* one file read from a disc image */
#define PE_IMP_PATH_MAX    1100
#define PE_IMP_ID_MAX      80               /* "re2:player/claire" */
#define PE_IMP_NAME_MAX    48

typedef struct { uint32_t version, size; } PeImpApi;

/* ---- data types (bits, so a reader or target can name several) ---- */
enum {
    PE_IMP_MODEL = 1u << 0, PE_IMP_ANIMATION = 1u << 1, PE_IMP_IMAGESET = 1u << 2,
    PE_IMP_SOUND = 1u << 3, PE_IMP_SONG = 1u << 4, PE_IMP_TEXTSET = 1u << 5
};
const char *pe_imp_type_name(uint32_t type);

/* ---- disc images ---- */
typedef struct PeImpDisc PeImpDisc;
PeImpDisc *pe_imp_disc_open(const char *path, char *why, size_t n);
void pe_imp_disc_close(PeImpDisc *d);
int pe_imp_disc_files(const PeImpDisc *d);
const char *pe_imp_disc_name(const PeImpDisc *d, int i);     /* "PL1/PLD/PL01.PLD": upper case, '/' */
uint32_t pe_imp_disc_size(const PeImpDisc *d, int i);
int pe_imp_disc_find(const PeImpDisc *d, const char *path);  /* index or -1 */
/* *data is malloc'd (free it); files past PE_IMP_FILE_MAX are refused. */
int pe_imp_disc_read(PeImpDisc *d, int i, uint8_t **data, uint32_t *size, char *why, size_t n);
const char *pe_imp_disc_label(const PeImpDisc *d);           /* the image's file name, no folder */
uint32_t pe_imp_disc_lba(const PeImpDisc *d, int i);         /* the file's first sector */
uint32_t pe_imp_disc_count(const PeImpDisc *d);              /* sectors in the image */
/* `count` 2048-byte sectors from `lba` into out; -1 outside the image or on a bad sector. */
int pe_imp_disc_sectors(PeImpDisc *d, uint32_t lba, uint32_t count, uint8_t *out, char *why, size_t n);

/* ---- Model: a skeleton, parts bound to bones, an 8-bit indexed texture ----
 * As the source stores it: y down, its own units. Part p's vertices are in bone p's frame; parts
 * past the last bone are extra pieces some games carry (fitting ignores them). */
typedef struct {
    int16_t v[4][3], n[4][3];
    uint8_t uv[4][2];
    uint16_t clut, page;                      /* the PS1 words as stored */
} PeImpFace;
typedef struct {
    PeImpFace *tri, *quad;
    uint32_t tris, quads;
} PeImpPart;
typedef struct {
    uint32_t bones;
    int16_t *parent;                          /* -1: root */
    int32_t (*rel)[3], (*pos)[3];             /* offset from the parent; rest position */
    uint32_t parts;
    PeImpPart *part;
    uint32_t pal_rows;
    uint16_t (*pal)[256];                     /* PS1 15-bit colours, 0 = transparent */
    uint32_t w, h;
    uint8_t *image;                           /* w * h palette indexes */
} PeImpModel;
#define PE_IMP_MODEL_VERSION 1
#define PE_IMP_MODEL_BONES_MAX 256
#define PE_IMP_MODEL_PARTS_MAX 256
#define PE_IMP_MODEL_FACES_MAX 65536
void pe_imp_model_free(PeImpModel *m);
int pe_imp_model_check(const PeImpModel *m, char *why, size_t n);
int pe_imp_model_save(const PeImpModel *m, const char *folder, char *why, size_t n);
int pe_imp_model_load(const char *folder, PeImpModel *m, char *why, size_t n);

/* Sound is a short mono PCM clip. The allocation owns its PCM frames. */
#define PE_IMP_SOUND_VERSION 1
typedef struct {
    uint32_t rate, frames;
    int16_t pcm[];
} PeImpSound;
PeImpSound *pe_imp_sound_alloc(uint32_t rate, uint32_t frames);
int pe_imp_sound_save(const PeImpSound *sound, const char *folder, char *why, size_t n);
int pe_imp_sound_load(const char *folder, PeImpSound **sound, char *why, size_t n);

/* Song stores a bounded source sequence and its instruments for the builder.
 * Gameplay playback will use a prepared track. Payload order is main sequence,
 * optional secondary sequence, instrument header, then samples. */
#define PE_IMP_SONG_VERSION 1
#define PE_IMP_SONG_MAX (2u << 20)
enum { PE_IMP_SONG_PSX_SEQ_VAB = 1 };
typedef struct {
    uint32_t format, seq_size[2], instrument_size, sample_size;
    uint8_t payload[];
} PeImpSong;
PeImpSong *pe_imp_song_alloc(uint32_t format, uint32_t main_size, uint32_t sub_size,
                            uint32_t instrument_size, uint32_t sample_size);
int pe_imp_song_save(const PeImpSong *song, const char *folder, char *why, size_t n);
int pe_imp_song_load(const char *folder, PeImpSong **song, char *why, size_t n);

/* ---- readers ---- */
typedef struct PeImpSink PeImpSink;
/* A reader hands each item it decodes to the sink: `proposed` is a local ID ("player/pl01"),
 * `name` a display name, `obj` the type's struct (the sink takes ownership, even on failure). */
int pe_imp_emit(PeImpSink *s, uint32_t type, const char *proposed, const char *name, void *obj);
typedef int (*PeImpReadFn)(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink,
                           char *why, size_t n);
typedef int (*PeImpReadPairFn)(const uint8_t *primary, uint32_t primary_size,
                               const uint8_t *companion, uint32_t companion_size,
                               const char *path, PeImpSink *sink, char *why, size_t n);
typedef struct {
    PeImpApi api;                             /* {PE_IMP_API_VERSION, sizeof(PeImpReader)} */
    const char *game, *platform, *format;     /* "re2", "ps1", "pld" */
    uint32_t version;                         /* raise when its output changes: the store re-reads */
    uint32_t produces;                        /* PE_IMP_* bits */
    const char *match;                        /* file pattern, '?' one char, '*' within a folder */
    const char *tags;                         /* "human player", copied into the catalog */
    PeImpReadFn read;
    const char *companion;                    /* exact ISO path, or .extension to pair by stem */
    PeImpReadPairFn read_pair;                /* both inputs participate in the store key */
    int path_key;                             /* single-file reader derives IDs from its ISO path */
} PeImpReader;
int pe_imp_add_reader(const PeImpReader *r); /* 0, or -1 (bad, duplicate, full) */
int pe_imp_readers(void);
const PeImpReader *pe_imp_reader(int i);
int pe_imp_match(const char *pattern, const char *path);
/* A naming table renames proposed IDs once, by hand: {"player/pl01": {"id": "player/claire",
 * "name": "Claire"}}. Set by the source game's mod; the JSON text must outlive the game. */
int pe_imp_set_naming(const char *game, const char *json);
/* A hash of every naming table and disc ID: when it changes, disc images already read are scanned
 * again so the catalog takes the new names. */
uint32_t pe_imp_naming_hash(void);

/* ---- source games and rigs (registered by pe_src_<game>) ---- */
/* "re2", "Resident Evil 2", the Wardrobe group "Resident Evil" */
int pe_imp_add_game(const char *game, const char *title, const char *group);
const char *pe_imp_game_title(const char *game);           /* the id itself if unknown */
/* A file that tells the game's disc apart from others with the same layout ("SLUS_009.22": Dino
 * Crisis and Dino Crisis 2 share PSX/DATA/CORE??.DAT). A game with disc IDs claims a disc only when
 * one of them is on it; a game without any goes by its readers' file patterns alone. */
int pe_imp_add_disc_id(const char *game, const char *pattern);
int pe_imp_disc_id_count(const char *game);
const char *pe_imp_disc_id(const char *game, int i);
const char *pe_imp_game_group(const char *game);
struct PeImpRig;
/* Assets "<game>:<prefix>..." fit with this rig ("re2", "player/", the RE2 human rig). */
int pe_imp_add_rig(const char *game, const char *id_prefix, const struct PeImpRig *rig);
const struct PeImpRig *pe_imp_rig_for(const char *asset_id);

/* What a source game builds by itself once its disc is there: an asset, the module that builds
 * it, and the disc to ask for while it's missing ("Resident Evil 2 disc 2"). */
typedef struct { const char *asset_id, *module, *disc; } PeImpAuto;
int pe_imp_add_auto(const PeImpAuto *a);
int pe_imp_autos(void);
const PeImpAuto *pe_imp_auto(int i);

/* Progress, for a page to show while a worker runs: `what` ("room animations"), done of total. */
typedef struct {
    void (*fn)(void *user, const char *what, int done, int total);
    void *user;
} PeImpProgress;
#define PE_IMP_PROGRESS(p, what, done, total) do { if ((p) && (p)->fn) (p)->fn((p)->user, (what), (done), (total)); } while (0)

/* ---- builder targets (registered by each module's mod: the Wardrobe builds skins) ---- */
struct PeImpPe;
typedef struct {
    const char *store;                        /* the store's root */
    struct PeImpPe *pe;                       /* Aya's data and the rooms */
    const int16_t (*normals)[4];              /* the game's 240 vertex normals */
    const PeImpProgress *progress;            /* may be NULL */
} PeImpBuild;
typedef struct {
    PeImpApi api;                             /* {PE_IMP_API_VERSION, sizeof(PeImpTarget)} */
    const char *module;                       /* "skins" */
    uint32_t version, consumes;               /* consumes: PE_IMP_* bits */
    const char *verb;                         /* "Build as a costume" */
    int needs_rig;                            /* only assets a rig fits */
    /* Build one asset into `folder` (created, empty or holding an older build). */
    int (*build)(const PeImpBuild *ctx, const char *asset_id, const char *folder, char *log, size_t logn,
                 char *why, size_t n);
} PeImpTarget;
int pe_imp_add_target(const PeImpTarget *t);
int pe_imp_targets(void);
const PeImpTarget *pe_imp_target(int i);

/* ---- store and catalog ---- */
int pe_imp_sha256_hex(const uint8_t *data, size_t size, char out[65]);
int pe_imp_mkdirs(const char *path);                         /* every missing folder along `path` */
typedef struct {
    int files_read, items, skipped, refused;  /* skipped: already current in the store */
    char game[16];
    char last_error[200];
} PeImpScan;
/* Extract one disc image into the store under `root`: the game is the one whose readers match
 * the most files (or `game` if given), every matching file is read, stored and catalogued. */
int pe_imp_scan(const char *root, const char *image, const char *game, PeImpScan *out);
/* An asset's model from its first copy (PS1 first), loaded from the store. */
int pe_imp_catalog_model(const char *root, const char *asset_id, PeImpModel *m, char *why, size_t n);
int pe_imp_catalog_sound(const char *root, const char *asset_id, PeImpSound **sound, char *why, size_t n);
int pe_imp_catalog_song(const char *root, const char *asset_id, PeImpSong **song, char *why, size_t n);
/* A game's catalog entries, sorted by ID; *out is malloc'd (free it). */
typedef struct {
    char id[PE_IMP_ID_MAX], name[PE_IMP_NAME_MAX], type[16], tags[64];
    char sha256[65], platform[16], path[128];               /* its first copy */
} PeImpAsset;
int pe_imp_catalog_assets(const char *root, const char *game, PeImpAsset **out);

/* ---- packs (format 1, as the Wardrobe loads them today) ----
 * <packs>/<game>-local/: pack.json and LICENSE (written once), characters/<name>.<game>/ with
 * character.json (name, group, tier and where it came from) and the builder's files. `folder`
 * receives that character folder's path. */
int pe_imp_pack_character(const char *packs, const PeImpAsset *a, const char *builder,
                          char *folder, size_t fn, char *why, size_t n);

#endif
