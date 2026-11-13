#ifndef PE1_PE_IMAGE_H
#define PE1_PE_IMAGE_H

#include "pe1/game_state.h"

/* PE.IMG -- the game's main on-disc asset archive (~206 MB).
 *
 * Everything that is not the executable or streamed FMV/XA audio lives in
 * PE.IMG: room geometry, entity models, TIM textures, the image font glyphs,
 * the string/message tables, scene init data, etc. For a PC port this is the
 * single container we must understand byte-for-byte.
 *
 * == HOW IT IS LOCATED (boot) ==
 *   Asset_OpenPeImage() (src/main/asset/game_state.c) searches the ISO9660 volume
 *   in the bound read-only CUE/BIN image for these paths (verified from data at
 *   0x80011330 / 0x80011348 / 0x80011354):
 *     "\FMV1\PEDISC01.IDF;1"   -- CD1 marker
 *     "\PE.IMG;1"              -- the archive itself
 *     "\FMV2\PEDISC02.IDF;1"   -- CD2 marker
 *   The matched directory record's start sector is turned into an integer LBA
 *   from the directory extent and stored in g_PeImageBaseLba. The FMV marker that was
 *   found sets g_PeImageMountFlags (bit0 = CD1, bit1 = CD2) -- this is the
 *   CD1/CD2 disc-detection used elsewhere. So the base LBA is NOT a hardcoded
 *   constant; it is discovered at runtime (retail pressing happens to place
 *   PE.IMG at LBA 1013, but the code never assumes that).
 *
 * == HOW DATA IS ADDRESSED (hybrid: baked offset tables + internal directory) ==
 *
 *   EVERY read is:  lba = g_PeImageBaseLba + <sector offset> ; then
 *   CdRom_ReadSectorsFromLba(lba, dst, sectorCount). The sector offset is
 *   RELATIVE to the start of PE.IMG, so in an extracted PE.IMG file the byte
 *   offset is simply  (sectorOffset * PE_IMAGE_SECTOR_SIZE).  No read ever
 *   uses an absolute disc LBA, and no read chains off a sector number that was
 *   itself read from an earlier sector -- the offsets come from one of:
 *
 *   TIER 1 -- fixed sector-offset tables baked into the EXE (u16 entries,
 *   stride 2, sector units relative to base). A loader picks an index, then
 *     start = baseTable[idx];  end = endTable[idx];  count = end - start;
 *     CdRom_ReadSectorsFromLba(g_PeImageBaseLba + start, dst, count);
 *   Known tables (data lives in Data_0818A0.inc region; dump for exact rows):
 *     g_PeImageSectorTable     one u16 boundary table: range k is entries
 *                              [k]..[k+1]; g_PeImageSectorBoundsNext is [1]
 *                              (Scene_LoadEntityTexture(s), Render_InitDisplayLists)
 *     g_RoomSectorTable        8-byte rows: start sector, then scratch,
 *                              texture and room sector counts (Scene_LoadRoom)
 *     Movie/extra-effect code is linked statically; its old bank ranges
 *     are retained only inside the authored sector table, without runtime aliases.
 *     g_MainAssetLbaTbl / g_LargeTexLbaTbl / g_FontUiLbaTbl
 *                              overlay/main asset + font/UI table ranges
 *                              (Boot_LoadOverlayTables)
 *     g_PeImageSectorTable[79] font glyph data (Render_LoadFontGlyph)
 *   These define WHERE each *category* of asset sits inside PE.IMG.
 *
 *   TIER 2 -- a self-describing directory that lives inside PE.IMG. Once a
 *   Tier-1 range is read into RAM (the "base"), individual assets are found by
 *   key with the Asset_FindTable* family (src/main/asset/). The format
 *   (verified from those functions) is:
 *     base + *(u32*)(base + 4)            -> sub-header
 *     descriptor = *(u32*)(subHeader + K) -- K = 0x08, 0x2C or 0x30 selects a table
 *     recordTable = base + (descriptor & 0x3FFFFF)   // low 22 bits = byte offset
 *     recordCount = descriptor >> 22                 // high 10 bits = count
 *   Each record is 0xC bytes:
 *     +0x04  u32  payload offset, masked 0xFFFFFF, relative to base (the asset)
 *     +0x08  u32  key   (Asset_FindTable08ByU32Key)
 *     +0x0A  u16  key   (Asset_FindTable2CByU16Key @0x2C, Asset_FindTable30ByU16Key @0x30)
 *   A lookup linearly scans recordCount records; on key match it returns
 *   base + (record[+4] & 0xFFFFFF). So PE.IMG has NO ISO9660 filesystem of its
 *   own -- it is a raw sector archive whose category chunks each begin with one
 *   of these compact descriptor/record directories.
 *
 * == READ PRIMITIVES ==
 *   CdRom_ReadSectorsFromLba / CdRom_ReadSectors  -- issue a sector read
 *   CdRom_ReadSync / CQ_vsync_system      -- wait / poll completion
 *   The async "DsRead" subsystem (CdRom_*DsRead*) lets a load run across frames.
 *
 * == PORTING NOTES ==
 *   - Asset_OpenPeImage discovers PE.IMG and marker files in the bound image.
 *     CdRom_BindImage borrows that stream; no drive polling or lid checks.
 *   - sectorOffset * 2048 == byte offset into the extracted file.
 *   - Keep the Tier-1 tables (dump them from the EXE) and the Asset_FindTable*
 *     descriptor logic verbatim; only the read primitive changes (fseek+fread).
 *   - To enumerate "every byte": walk the Tier-1 tables to get every category
 *     range, then parse each range's Tier-2 directory to enumerate its records.
 */

#define PE_IMAGE_SECTOR_SIZE   2048   /* CD-ROM data sector; sectorOffset*this = file byte offset */

/* descriptor field packing (Tier-2 directory sub-header words) */
#define PE_DIR_OFFSET_MASK     0x3FFFFF   /* low 22 bits: byte offset of record table from base */
#define PE_DIR_COUNT_SHIFT     22         /* high 10 bits: record count */
#define PE_DIR_RECORD_STRIDE   0xC        /* bytes per directory record */
#define PE_DIR_PAYLOAD_MASK    0xFFFFFF   /* record +0x04 payload offset mask */

/* Native storage for the resident USA sector boundaries and linker aliases. */
extern u16 g_PeImageSectorTable[81];
#define g_PeImageSectorBoundsNext (g_PeImageSectorTable + 1)
#define g_StrFileDirLba (g_PeImageSectorTable + 70)
#define g_BootDisplayDataLbaTbl (g_PeImageSectorTable[71])
#define g_MainAssetLbaTbl (g_PeImageSectorTable + 72)
#define g_LargeTexLbaTbl (g_PeImageSectorTable + 73)
#define g_SmallTexLbaTbl (g_PeImageSectorTable + 74)
#define g_FontUiLbaTbl (g_PeImageSectorTable + 75)

/* Original scalar symbols alias their canonical game-state fields. */
#define g_PeImageBaseLba (g_GameState.pe_image_base_lba)
#define g_PeImageMountFlags (g_GameState.display_list_modes)

/* ISO9660 paths searched by Asset_OpenPeImage. */
extern char g_PeDisc1MarkerPath[]; /* "\FMV1\PEDISC01.IDF;1" */
extern char g_PeImagePath[];       /* "\PE.IMG;1" */
extern char g_PeDisc2MarkerPath[]; /* "\FMV2\PEDISC02.IDF;1" */

/* Which release the mounted discs are. Japanese discs (SLPS-01230/01231) keep the same program
 * with its tables elsewhere in the executable, Japanese text and the Japanese balance; the USA
 * discs (SLUS-00662/00668) are the default. Set when PE.IMG mounts (Asset_OpenPeImage). */
enum { PE_REGION_USA = 0, PE_REGION_JAPAN = 1 };
extern u8 g_GameRegion;
#define PE_JAPAN (g_GameRegion == PE_REGION_JAPAN)

int Asset_SearchByKeyType(int type);
/* -1: no image; -2: archive/marker missing; -3: invalid resident data. */
int Asset_OpenPeImage(void);

#endif /* PE1_PE_IMAGE_H */
