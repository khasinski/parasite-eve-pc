/* Resident Evil door transitions as DoorScene records (pe_imp_logic.h).
 *   RE2  COMMON/DOOR/DOORnn.DO2: 16 bytes, then a VAB (its header, 8 bytes, its samples; the total
 *        at +12 of the header leaves out those 8), then from the next 2 KB boundary the scene
 *        (animation, the door model, its TIM). VAB0 keeps the VAB with the gap as stored.
 *        The samples are also cataloged as Sounds in bank "doornn".
 *   RE3  CD_DATA/STAGEs/DOORnn.DO1 and RE1 <dir>/ITEM_M1/DOORnn[K].DOR: the whole file is the
 *        scene for now; their layouts aren't worked out yet. */
#include <stdio.h>
#include <string.h>

#include "pe_imp_door.h"
#include "pe_imp_spu.h"

static uint32_t le16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }

/* the door's name from its path: "00", "00k"; stage from an RE3 STAGEs folder (else 0) */
static int door_name(const char *path, char out[8], unsigned *stage) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (strncmp(base, "DOOR", 4)) return -1;
    size_t k = 0;
    for (const char *c = base + 4; *c && *c != '.' && k < 7; c++) {
        if (!((*c >= '0' && *c <= '9') || (*c >= 'A' && *c <= 'Z'))) return -1;
        out[k++] = (char)(*c >= 'A' ? *c + 32 : *c);
    }
    out[k] = 0;
    if (!k) return -1;
    const char *st = strstr(path, "STAGE");
    *stage = st && st[5] >= '1' && st[5] <= '9' ? (unsigned)(st[5] - '0') : 0;
    return 0;
}

int pe_imp_emit_re_door(PeImpSink *sink, unsigned game, const char *path, const uint8_t *data, uint32_t size,
                        char *why, size_t n) {
    char name[8], id[32], label[32], bank[24];
    unsigned stage;
    if (door_name(path, name, &stage)) return 0;                       /* not a door file */
    PeImpLogicBuilder *b = pe_imp_logic_begin(game, 0, stage, 0);
    if (game == PE_IMP_RE2 && size > 16 + 32 && !memcmp(data + 16, "pBAV", 4)) {
        uint32_t vab = le32(data + 16 + 12), programs = le16(data + 16 + 18), waves = le16(data + 16 + 22);
        /* the samples start 8 bytes after the header (checked on all 55 US doors); the VAB's total
         * size counts the header and the samples, not the gap */
        uint32_t header = 32 + 2048 + programs * 512 + 512;
        if (vab < header || vab + 8 > size - 16 || programs > 128 || waves > 254) {
            pe_imp_logic_finish(b);
            if (why && n) snprintf(why, n, "%s: a damaged sound bank", path);
            return -1;
        }
        pe_imp_logic_add(b, PE_IMP_DOOR_VAB0, 0, data + 16, vab + 8);
        uint32_t scene = (16 + vab + 8 + 2047) & ~2047u;
        if (scene < size) pe_imp_logic_add(b, PE_IMP_DOOR_SCN0, 0, data + scene, size - scene);
        snprintf(bank, sizeof bank, "door%s", name);
        if (pe_imp_vab_read(sink, bank, data + 16, header, data + 16 + header + 8, vab - header, why, n)) {
            pe_imp_logic_finish(b);
            return -1;
        }
    } else {
        pe_imp_logic_add(b, PE_IMP_DOOR_SCN0, 0, data, size);
    }
    PeImpRoomLogic *l = pe_imp_logic_finish(b);
    if (!l) {
        if (why && n) snprintf(why, n, "%s: too large", path);
        return -1;
    }
    if (stage) {
        snprintf(id, sizeof id, "door/%u-%s", stage, name);
        snprintf(label, sizeof label, "Door %u-%s", stage, name);
    } else {
        snprintf(id, sizeof id, "door/%s", name);
        snprintf(label, sizeof label, "Door %s", name);
    }
    for (char *c = label; *c; c++) if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);
    memcpy(label, "Door", 4);
    if (pe_imp_emit(sink, PE_IMP_DOORSCENE, id, label, l)) {
        if (why && n) snprintf(why, n, "couldn't catalog %s", id);
        return -1;
    }
    return 0;
}
