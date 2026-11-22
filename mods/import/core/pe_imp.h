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

#define PE_IMP_API_VERSION 2
#define PE_IMP_FILE_MAX    (128u << 20)     /* one file read from a disc image (RE2's longest movie is 92 MB) */
#define PE_IMP_PATH_MAX    1100
#define PE_IMP_ID_MAX      80               /* "re2:player/claire" */
#define PE_IMP_NAME_MAX    48

typedef struct { uint32_t version, size; } PeImpApi;

/* ---- data types (bits, so a reader or target can name several) ---- */
enum {
    PE_IMP_MODEL = 1u << 0, PE_IMP_ANIMATION = 1u << 1, PE_IMP_IMAGESET = 1u << 2,
    PE_IMP_SOUND = 1u << 3, PE_IMP_SONG = 1u << 4, PE_IMP_TEXTSET = 1u << 5,
    PE_IMP_ROOMLOGIC = 1u << 6,               /* pe_imp_logic.h */
    PE_IMP_DOORSCENE = 1u << 7,               /* pe_imp_logic.h */
    PE_IMP_ITEMTABLE = 1u << 8,               /* pe_imp_logic.h */
    PE_IMP_MOVIE = 1u << 9,                   /* below */
    PE_IMP_PICTURES = 1u << 10                /* pe_imp_pictures.h; the last type: the store walks types up to it */
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
/* A file's CD-XA audio sectors (mode 2 form 2, the audio bit set), as records of
 * PE_IMP_XA_RECORD bytes: file, channel, submode, coding (the subheader), then the 2304 bytes of
 * sound groups. Only raw images (.bin, 2352-byte sectors) keep them: -1 for a cooked .iso.
 * *data is malloc'd (free it). */
#define PE_IMP_XA_RECORD 2308u
int pe_imp_disc_xa(PeImpDisc *d, int i, uint8_t **data, uint32_t *size, char *why, size_t n);

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
    /* Optional skin (model.bin v2): per face corner, the bone it follows and a second bone it
     * follows half way (PE_IMP_NO_BONE: none). A skinned part's vertices are in the rest pose's
     * model frame (each bone at `pos`, no rotation), not in its bone's frame. NULL: rigid. */
    uint8_t (*tri_skin)[4][2], (*quad_skin)[4][2];
} PeImpPart;
#define PE_IMP_NO_BONE 0xFF
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
#define PE_IMP_MODEL_VERSION 1                /* the store's stamp: v1 files and v2 (skinned) both load */
#define PE_IMP_MODEL_SKIN_VERSION 2           /* model.bin of a model with skinned parts */
#define PE_IMP_MODEL_BONES_MAX 256
#define PE_IMP_MODEL_PARTS_MAX 256
#define PE_IMP_MODEL_FACES_MAX 65536
void pe_imp_model_free(PeImpModel *m);
int pe_imp_model_check(const PeImpModel *m, char *why, size_t n);
int pe_imp_model_save(const PeImpModel *m, const char *folder, char *why, size_t n);
int pe_imp_model_load(const char *folder, PeImpModel *m, char *why, size_t n);

/* ---- Animation: a model's moves ----
 * Frames at `fps`, in-between steps already expanded. Per frame: the root's offset s16[3] (model
 * frame, the source's units, y down), then s16[3] angles per bone (4096 = a full turn, -2048..2047;
 * rotation X * Y * Z as Resident Evil applies it, parent first), and one event word: the sound the
 * source plays on that frame (PE_IMP_EVENT_SOUND: 1-255, 0 none; a number in the model's sound
 * bank) and PE_IMP_EVENT_HIT on the frames a blow can land. */
#define PE_IMP_ANIMATION_VERSION 1
#define PE_IMP_ANIM_MOVES_MAX 256
#define PE_IMP_ANIM_FRAMES_MAX 4096           /* per move */
enum { PE_IMP_EVENT_SOUND = 0x00ff, PE_IMP_EVENT_HIT = 0x0100 };
typedef struct {
    uint32_t frames;
    int16_t *data;                            /* frames * (3 + 3 * bones) */
    uint16_t *event;                          /* frames */
} PeImpMove;
typedef struct {
    uint32_t bones, fps, moves;
    PeImpMove *move;                          /* an empty move (0 frames) keeps the source's numbering */
} PeImpAnimation;
void pe_imp_animation_free(PeImpAnimation *a);
int pe_imp_animation_check(const PeImpAnimation *a, char *why, size_t n);
int pe_imp_animation_save(const PeImpAnimation *a, const char *folder, char *why, size_t n);
int pe_imp_animation_load(const char *folder, PeImpAnimation *a, char *why, size_t n);

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
#define PE_IMP_SONG_MAX (8u << 20)
/* PSX_SEQ_VAB: payload as above. PSX_XA: an XA stream (streamed music): main_size is its 16-byte
 * header ("XAS1", coding, file, channel, 0, u32 sectors, u32 0), sample_size the sectors' sound
 * groups (2304 bytes each), no secondary sequence or instruments. */
/* AKAO: a Square sequence (main_size: an 80-byte header, "AKS1" then at 8 its instrument bank's
 * local catalog ID, NUL-padded; then the "AKAO" block). AKAO_BANK: that bank (main_size 16: "AKB1",
 * u32 SPU address of the first sample byte; instrument_size: 128 records of 64 bytes;
 * sample_size: the samples). A bank's local ID starts with "bank/"; music lists leave it out. */
enum { PE_IMP_SONG_PSX_SEQ_VAB = 1, PE_IMP_SONG_PSX_XA = 2, PE_IMP_SONG_AKAO = 3, PE_IMP_SONG_AKAO_BANK = 4 };
typedef struct {
    uint32_t format, seq_size[2], instrument_size, sample_size;
    uint8_t payload[];
} PeImpSong;
PeImpSong *pe_imp_song_alloc(uint32_t format, uint32_t main_size, uint32_t sub_size,
                            uint32_t instrument_size, uint32_t sample_size);
int pe_imp_song_save(const PeImpSong *song, const char *folder, char *why, size_t n);
int pe_imp_song_load(const char *folder, PeImpSong **song, char *why, size_t n);

/* ImageSet stores still pictures as the source game keeps them, decoded on demand. MDEC: each
 * picture is a PS1 MDEC frame (pe_imp_mdec.h) of width x height pixels; Resident Evil's room
 * backgrounds, one picture per camera view. Payload: the frames back to back, size[i] bytes each. */
#define PE_IMP_IMAGESET_VERSION 2
#define PE_IMP_IMAGESET_MAX (4u << 20)
#define PE_IMP_IMAGESET_VIEWS 32
enum { PE_IMP_IMAGESET_MDEC = 1 };
typedef struct {
    uint32_t format, width, height, count, size[PE_IMP_IMAGESET_VIEWS];
    uint32_t extra[PE_IMP_IMAGESET_VIEWS];  /* optional per-view room data, after all frames */
    uint8_t payload[];
} PeImpImageSet;
/* sizes: `count` frame sizes; the payload is left for the caller to fill */
PeImpImageSet *pe_imp_imageset_alloc(uint32_t format, uint32_t width, uint32_t height, uint32_t count,
                                     const uint32_t *sizes);
PeImpImageSet *pe_imp_imageset_alloc_extra(uint32_t format, uint32_t width, uint32_t height, uint32_t count,
                                           const uint32_t *sizes, const uint32_t *extra);
const uint8_t *pe_imp_imageset_extra(const PeImpImageSet *set, uint32_t i, uint32_t *size);
/* where picture i starts in the payload */
const uint8_t *pe_imp_imageset_frame(const PeImpImageSet *set, uint32_t i, uint32_t *size);
int pe_imp_imageset_save(const PeImpImageSet *set, const char *folder, char *why, size_t n);
int pe_imp_imageset_load(const char *folder, PeImpImageSet **set, char *why, size_t n);

/* Movie: a PS1 STR movie as the game keeps it, copied once. Frames are MDEC frames (pe_imp_mdec.h)
 * of width x height, in order; audio is the movie's CD-XA sectors as PE_IMP_XA_RECORD records
 * (pe_imp_disc_xa: file, channel, submode, coding, 2304 bytes), in disc order. movie.bin:
 *   "MOV1", u32 version 1, width, height, frames, audio records, then per frame u32 offset and
 *   u32 size (from the start of the frame data), the frame data, then the audio records. */
#define PE_IMP_MOVIE_VERSION 1
#define PE_IMP_MOVIE_MAX (160u << 20)
enum { PE_IMP_XA_BOTH = 2 };
typedef struct {
    uint32_t width, height, frames, audio_records;
    uint32_t *offset, *size;                  /* per frame, into frame_data */
    uint8_t *frame_data, *audio;
    uint32_t frame_bytes;
} PeImpMovie;
/* From an STR file's 2048-byte sectors (pe_imp_disc_read; interleaved audio sectors are skipped by
 * their header) and its XA records (may be NULL). NULL if no frame was found. */
PeImpMovie *pe_imp_movie_from_str(const uint8_t *str, uint32_t size, const uint8_t *xa, uint32_t xa_size);
void pe_imp_movie_free(PeImpMovie *m);
int pe_imp_movie_save(const PeImpMovie *m, const char *folder, char *why, size_t n);
int pe_imp_movie_load(const char *folder, PeImpMovie **m, char *why, size_t n);
int pe_imp_catalog_movie(const char *root, const char *asset_id, PeImpMovie **m, char *why, size_t n);
/* For readers: an STR file (and its XA records) as "movie/<name>"; 0 also when it holds no video. */
struct PeImpSink;
int pe_imp_emit_str(struct PeImpSink *sink, const uint8_t *str, uint32_t size, const uint8_t *xa, uint32_t xa_size,
                    const char *path, char *why, size_t n);

/* ---- readers ---- */
typedef struct PeImpSink PeImpSink;
/* A reader hands each item it decodes to the sink: `proposed` is a local ID ("player/pl01"),
 * `name` a display name, `obj` the type's struct (the sink takes ownership, even on failure). */
int pe_imp_emit(PeImpSink *s, uint32_t type, const char *proposed, const char *name, void *obj);
/* Catalog another ID for the last emitted object, sharing its stored data. */
int pe_imp_emit_alias(PeImpSink *s, const char *proposed, const char *name);
typedef int (*PeImpReadFn)(const uint8_t *data, uint32_t size, const char *path, PeImpSink *sink,
                           char *why, size_t n);
typedef int (*PeImpReadPairFn)(const uint8_t *primary, uint32_t primary_size,
                               const uint8_t *companion, uint32_t companion_size,
                               const char *path, PeImpSink *sink, char *why, size_t n);
/* An optional paired input, resolved by the reader. Return -1 to run it with an empty companion. */
typedef int (*PeImpFindCompanionFn)(PeImpDisc *disc, const char *path);
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
    int xa;                                   /* 1: read gets the file's XA audio records (pe_imp_disc_xa);
                                               * PE_IMP_XA_BOTH: read_pair gets the file and, as the
                                               * companion, its XA records (a movie; none from an .iso) */
    PeImpFindCompanionFn find_companion;      /* alternative to a required .companion path */
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
const struct PeImpRig *pe_imp_rig_for_game(const char *game);   /* its first rig, or NULL */

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
struct PeImpAsset;
typedef struct {
    const char *store;                        /* the store's root */
    /* The game's own data its targets read, from the import mod's host (import_host.h); NULL when
     * the game supplies none. Its type is the game's: PE1 targets get a PeImpPe1Build
     * (import/targets/pe1/pe_imp_pe.h). */
    const void *host;
    const PeImpProgress *progress;            /* may be NULL */
} PeImpBuild;
enum { PE_IMP_COVERAGE_INCOMPLETE, PE_IMP_COVERAGE_COMPLETE, PE_IMP_COVERAGE_SCENE };
typedef struct {
    PeImpApi api;                             /* {PE_IMP_API_VERSION, sizeof(PeImpTarget)} */
    const char *module;                       /* "skins" */
    uint32_t version, consumes;               /* consumes: PE_IMP_* bits */
    const char *verb;                         /* "Build as a costume" */
    int needs_rig;                            /* only assets a rig fits */
    /* Build one asset into `folder` (created, empty or holding an older build). */
    int (*build)(const PeImpBuild *ctx, const char *asset_id, const char *folder, char *log, size_t logn,
                 char *why, size_t n);
    /* Optional (NULL: every model; the Wardrobe's packs, pe_imp_pack_character): */
    int (*accepts)(const char *asset_id);   /* 1: this target builds the asset */
    /* Make the asset's folder (filling `folder`) for a build by `builder`: 0, or -1 with a reason */
    int (*place)(const struct PeImpAsset *a, const char *builder, char *folder, size_t fn, char *why, size_t n);
    /* 1 if its place already holds a finished build of this source file by `builder` */
    int (*current)(const struct PeImpAsset *a, const char *builder);
    /* Optional: read the installed pack's coverage, or return -1 when unreadable. */
    int (*coverage)(const struct PeImpAsset *a, char *out, size_t size, char *audio, size_t audio_size);
} PeImpTarget;
/* Where a target builds an asset: its place, else the Wardrobe's pack folder. */
int pe_imp_target_place(const PeImpTarget *t, const char *packs, const struct PeImpAsset *a, const char *builder,
                        char *folder, size_t fn, char *why, size_t n);
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
int pe_imp_catalog_animation(const char *root, const char *asset_id, PeImpAnimation *a, char *why, size_t n);
int pe_imp_catalog_sound(const char *root, const char *asset_id, PeImpSound **sound, char *why, size_t n);
int pe_imp_catalog_song(const char *root, const char *asset_id, PeImpSong **song, char *why, size_t n);
int pe_imp_catalog_imageset(const char *root, const char *asset_id, PeImpImageSet **set, char *why, size_t n);
/* A game's catalog entries, sorted by ID; *out is malloc'd (free it). */
typedef struct PeImpAsset {
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
