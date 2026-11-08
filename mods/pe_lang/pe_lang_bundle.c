/* Opening a .pelang file: a plain pack, or a zip with the pack inside (pe_lang_bundle.h).
 * Plain data and files only, no SDL, so it is tested on its own. */
#include "pe_lang_bundle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if PE_LANG_ZIP
#include "miniz.h"
#endif

static int is_zip(const unsigned char *bytes, size_t size) {
    return size >= 4 && bytes[0] == 'P' && bytes[1] == 'K' && bytes[2] == 3 && bytes[3] == 4;
}

/* The pack is checked whole by pe_lang_pack_parse, which takes its own copy. */
static PeLangOpen parse_copy(PeLangPack *pack, const unsigned char *bytes, size_t size) {
    unsigned char *data;
    if (!size || size > PE_LANG_PACK_MAX || !(data = malloc(size))) return PE_LANG_OPEN_UNREADABLE;
    memcpy(data, bytes, size);
    return pe_lang_pack_parse(pack, data, size) ? PE_LANG_OPEN_BAD_PACK : PE_LANG_OPEN_OK;
}

#if PE_LANG_ZIP
/* The part called `name`, malloc'd, or NULL with *found 0 when the zip has no such part. */
static unsigned char *zip_part(mz_zip_archive *zip, const char *name, size_t max, size_t *size, int *found) {
    mz_zip_archive_file_stat stat;
    unsigned char *data;
    /* Names are matched exactly: a folder of that name, or another case, is not the part. */
    int index = mz_zip_reader_locate_file(zip, name, NULL, MZ_ZIP_FLAG_CASE_SENSITIVE);
    *found = index >= 0;
    if (index < 0 || !mz_zip_reader_file_stat(zip, (mz_uint)index, &stat) || stat.m_is_directory ||
        stat.m_is_encrypted || !stat.m_is_supported || !stat.m_uncomp_size || stat.m_uncomp_size > max ||
        !(data = malloc((size_t)stat.m_uncomp_size)))
        return NULL;
    /* extract_to_mem checks the CRC, so a part damaged in the zip is refused here. */
    if (!mz_zip_reader_extract_to_mem(zip, (mz_uint)index, data, (size_t)stat.m_uncomp_size, 0)) {
        free(data);
        return NULL;
    }
    *size = (size_t)stat.m_uncomp_size;
    return data;
}

static PeLangOpen parse_zip(PeLangPack *pack, const unsigned char *bytes, size_t size) {
    mz_zip_archive zip;
    unsigned char *data;
    size_t part_size = 0;
    int found;
    PeLangOpen result = PE_LANG_OPEN_BAD_ZIP;
    memset(&zip, 0, sizeof zip);
    if (!mz_zip_reader_init_mem(&zip, bytes, size, 0)) return PE_LANG_OPEN_BAD_ZIP;
    if ((data = zip_part(&zip, PE_LANG_BUNDLE_TEXT, PE_LANG_PACK_MAX, &part_size, &found)))
        result = pe_lang_pack_parse(pack, data, part_size) ? PE_LANG_OPEN_BAD_PACK : PE_LANG_OPEN_OK;
    if (result == PE_LANG_OPEN_OK) {
        /* Pictures are optional, but a file whose pictures are damaged is refused whole. */
        data = zip_part(&zip, PE_LANG_BUNDLE_PICTURES, PE_LANG_PICTURES_MAX, &part_size, &found);
        if (found && (!data || pe_lang_pictures_parse(&pack->pictures, data, part_size))) {
            pe_lang_pack_free(pack);
            result = PE_LANG_OPEN_BAD_PICTURES;
        }
    }
    mz_zip_reader_end(&zip);
    return result;
}
#endif

PeLangOpen pe_lang_bundle_parse(PeLangPack *pack, const unsigned char *bytes, size_t size) {
    memset(pack, 0, sizeof *pack);
    if (!bytes || !size) return PE_LANG_OPEN_UNREADABLE;
    if (!is_zip(bytes, size)) return parse_copy(pack, bytes, size);
#if PE_LANG_ZIP
    return parse_zip(pack, bytes, size);
#else
    return PE_LANG_OPEN_NO_ZIP;
#endif
}

PeLangOpen pe_lang_bundle_load(PeLangPack *pack, const char *path) {
    FILE *f = fopen(path, "rb");
    long size;
    unsigned char *bytes = NULL;
    PeLangOpen result = PE_LANG_OPEN_UNREADABLE;
    memset(pack, 0, sizeof *pack);
    if (!f) return result;
    if (!fseek(f, 0, SEEK_END) && (size = ftell(f)) > 0 && (unsigned long)size <= PE_LANG_BUNDLE_MAX &&
        !fseek(f, 0, SEEK_SET) && (bytes = malloc((size_t)size)) && fread(bytes, 1, (size_t)size, f) == (size_t)size)
        result = pe_lang_bundle_parse(pack, bytes, (size_t)size);
    free(bytes);
    fclose(f);
    return result;
}

const char *pe_lang_bundle_error(PeLangOpen result) {
    switch (result) {
    case PE_LANG_OPEN_OK: return "ok";
    case PE_LANG_OPEN_UNREADABLE: return "the file can't be read, is empty or is too big";
    case PE_LANG_OPEN_NO_ZIP: return "this build can't open zipped translations";
    case PE_LANG_OPEN_BAD_ZIP: return "the zip is damaged or has no " PE_LANG_BUNDLE_TEXT;
    case PE_LANG_OPEN_BAD_PACK: return "its text is damaged or in a newer format";
    case PE_LANG_OPEN_BAD_PICTURES: return "its pictures are damaged or in a newer format";
    }
    return "unknown error";
}
