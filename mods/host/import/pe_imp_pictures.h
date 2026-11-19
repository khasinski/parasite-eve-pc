#ifndef PE_IMP_PICTURES_H
#define PE_IMP_PICTURES_H
/* Pictures (files, maps, menus): PS1 TIM images kept as stored, in the sectioned container
 * (pe_imp_logic.h), stored as pictures.bin. Section TIMS: records = pictures; bytes: per picture u32
 * size, the TIM, padded to 4. pe_imp_tim_decode turns one into RGBA. */
#include <stdlib.h>

#include "pe_imp_logic.h"

#define PE_IMP_PICTURES_VERSION 1
/* TIMS: the pictures. DOCS: the documents of a file collection (RE2, RE3): records = documents;
 * bytes: per document u16 first picture, u16 pictures (its 8-bit background, then its 4-bit text
 * pages), u16 its item number in the game's ItemTable (its name there; 0: not an item), u16 0. FILE: the whole source file as stored, when it holds more than pictures (RE3's
 * map: its room and label tables). GREY (empty): the pictures had no palette on the disc and carry
 * a placeholder of 16 greys (RE2's maps). */
enum { PE_IMP_PICTURE_TIMS = PE_IMP_LOGIC_TAG('T', 'I', 'M', 'S'), PE_IMP_PICTURE_FILE = PE_IMP_LOGIC_TAG('F', 'I', 'L', 'E'),
       PE_IMP_PICTURE_GREY = PE_IMP_LOGIC_TAG('G', 'R', 'E', 'Y'),
       PE_IMP_PICTURE_DOCS = PE_IMP_LOGIC_TAG('D', 'O', 'C', 'S') };

/* The TIM's byte count if `tim` starts a valid one (palette and pixels inside `size`), else 0. */
uint32_t pe_imp_tim_size(const uint8_t *tim, uint32_t size);
/* RGBA (alpha 0 for colour 0, PS1's see-through), malloc'd; NULL for a bad or 24-bit TIM. */
uint8_t *pe_imp_tim_decode(const uint8_t *tim, uint32_t size, unsigned *width, unsigned *height);
/* Picture i of a Pictures record; NULL if out of range. */
const uint8_t *pe_imp_picture_tim(const PeImpRoomLogic *pictures, uint32_t i, uint32_t *size);
/* For readers: every TIM in `data` (one at 0 if step is 0; else right after the previous one, or at
 * the next multiple of `step` where one starts) as one Pictures record. 0 also when there are none. */
int pe_imp_emit_tims(PeImpSink *sink, const char *id, const char *name, const uint8_t *data, uint32_t size,
                     uint32_t step, char *why, size_t n);
/* The same, and with keep_file also the whole file (FILE). */
int pe_imp_emit_tims_ex(PeImpSink *sink, const char *id, const char *name, const uint8_t *data, uint32_t size,
                        uint32_t step, int keep_file, char *why, size_t n);
/* The same for a file collection, with DOCS: each 8-bit TIM starts a document, the TIMs after it
 * (its text pages) belong to it. Document k is item first_item + k (0: they aren't items). */
int pe_imp_emit_documents(PeImpSink *sink, const char *id, const char *name, const uint8_t *data, uint32_t size,
                          uint32_t step, uint32_t first_item, char *why, size_t n);
/* 256 x 256 4-bit pages without a palette, as TIMs with a placeholder grey palette (GREY). */
int pe_imp_emit_pages4(PeImpSink *sink, const char *id, const char *name, const uint8_t *data, uint32_t size,
                       char *why, size_t n);
/* A palette of palette_bytes (first 16 colours used), then 4-bit pixels `width` wide, cut into pages
 * page_h lines tall, each a 4-bit TIM with that palette (RE1's file text pages). */
int pe_imp_emit_pix4(PeImpSink *sink, const char *id, const char *name, const uint8_t *data, uint32_t size,
                     uint32_t palette_bytes, uint32_t width, uint32_t page_h, char *why, size_t n);
/* 8-bit pixels, pictures of width x height back to back, each a TIM with the given 256-colour palette
 * (512 bytes; RE1's item icons with its status screen's palette). */
int pe_imp_emit_pix8(PeImpSink *sink, const char *id, const char *name, const uint8_t *pixels, uint32_t size,
                     const uint8_t *palette, uint32_t width, uint32_t height, char *why, size_t n);
int pe_imp_pictures_save(const PeImpRoomLogic *l, const char *folder, char *why, size_t n);
int pe_imp_pictures_load(const char *folder, PeImpRoomLogic **l, char *why, size_t n);
int pe_imp_catalog_pictures(const char *root, const char *asset_id, PeImpRoomLogic **l, char *why, size_t n);

#endif
