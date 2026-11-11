#ifndef PE1_CDROM_H
#define PE1_CDROM_H

#include "common.h"

/* File images replace physical drives; serialized sectors retain their PS1 layout. */
#include <stdio.h>

typedef struct PeDiscFile {
    uint32_t sector;
    uint32_t size;
    int directory;
} PeDiscFile;

struct PeChd;

typedef struct PeDisc {
    FILE *stream; /* A CHD image keeps its file here, read through chd. */
    struct PeChd *chd;
    uint64_t image_size;
    uint64_t track_offset;
    uint32_t sector_size;
    uint32_t payload_offset;
    PeDiscFile root;
    char error[192];
} PeDisc;

/* Read-only ISO9660 access to ISO, MODE2/2352 BIN, a single-track CUE or a CHD
 * whose first track is MODE2_RAW (found by its header, not its name).
 * CUE FILE paths are relative to the descriptor, INDEX 01 sets sector zero. */
int pe_disc_open(PeDisc *disc, const char *path);
/* Bytes of the image as a MODE2/2352 BIN would hold them, from its first byte. */
int pe_disc_read_image(PeDisc *disc, uint64_t offset, void *data, size_t size);
void pe_disc_close(PeDisc *disc);
int pe_disc_find(PeDisc *disc, const char *path, PeDiscFile *file);
int pe_disc_read(PeDisc *disc, const PeDiscFile *file, uint64_t offset,
                 void *data, size_t size);
/* Full raw sectors preserve XA audio and STR headers for the movie decoder. */
int pe_disc_read_raw(PeDisc *disc, const PeDiscFile *file, uint32_t sector,
                     void *data);
/* Borrow an image; a NULL archive permits Asset_OpenPeImage discovery.
 * A NULL image unmounts it. The caller owns the stream lifetime. */
extern PeDisc *g_CdImage;
/* Borrowed mounted volumes, indexed by the authored disc identity (1/2).
 * NULL entries are unavailable; no ownership transfer or physical swap. */
extern PeDisc *g_CdImageVolumes[2];
/* Optional native file chooser, called by the original disc check only for a
 * missing identity. NULL in headless runs. Success must install that volume. */
extern int (*g_CdImageRequest)(unsigned identity);
void CdRom_BindImage(struct PeDisc *disc, const struct PeDiscFile *archive);

struct CdlLOC;

int DsControl(unsigned char command, void *param, void *result);
int DsControlB(unsigned char command, void *param, void *result);
int DsSystemStatus(void);
int DsShellOpen(void);
int DsQueueLen(void);
void DS_restart(void);
int DS_system_active(void);
int DsReset(void);
int ER_retry(void);
int DS_lastcom(void);
int DS_lastmode(void);
struct CdlLOC *DsLastPos(struct CdlLOC *destination);
struct CdlLOC *DsIntToPos(int sector, struct CdlLOC *position);
int DsPosToInt(struct CdlLOC *position);
int CdRom_PollReady(void);
int DsReadSync(void *argument);
int DsReady(u8 *destination);
int CdRom_ReadSectors(u32 lba, u32 offset, void *destination, u32 size);
int CdRom_ReadSectorsFromLba(u32 lba, void *destination, u32 size);
int DsRead(struct CdlLOC *position, int sectors, void *destination,
                    int mode);

/* Disc type the shell must still report before a read is issued; a
 * different value means the lid was opened and another disc inserted. */
extern u16 g_CdDiskType;
/* "CD: Read error (%d / %d)\n" */
extern char g_CdReadErrorFormat[];

/* CdRom_ReadAssetSectors state: PE.IMG start sector of the current read, its total
 * and remaining sector counts, and the sectors requested by the last chunk. */
extern u32 g_CdReadStartLba;
extern u32 g_CdReadTotalSectors;
extern u32 g_CdReadRemainingSectors;
extern u32 g_CdReadChunkSectors;
/* Per-kind sector ranges (SceneSectorDirectory: base sector + u16 offsets). */
extern u8 g_SceneSectorDirectory[];

/* CdRom_SeekToTrack result: the scene track record and its bank (-1: none). */
struct SceneTrackRecord;
extern struct SceneTrackRecord *g_CdTrackRecord;
extern s16 g_CdTrackBank;

/* CdRom_StepReadState: stream bank being loaded, and the fade-out started at
 * g_CdFadeStartTime with g_CdFadeFramesLeft frames still to wait. */
extern int g_CdStreamBankIndex;
extern int g_CdFadeStartTime;
extern int g_CdFadeFramesLeft;

#include "pe1/akao/bgm.h"

void Akao_ResetPositionalState(void);
int CdRom_SeekToTrack(int key, int mode, int group, int *slot, int immediate);

#endif
