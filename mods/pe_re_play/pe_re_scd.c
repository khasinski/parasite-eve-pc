/* The RE2 room script interpreter. See pe_re_scd.h. The handlers follow bio2-ds's SCE1.C in what
 * they do (read for behaviour, written here independently). */
#include <string.h>

#include "pe_re_scd.h"
#include "pe_imp_rdt.h"
#include "pe_imp_room.h"

enum {
    OP_NOP = 0x00, OP_EVT_END = 0x01, OP_EVT_NEXT = 0x02, OP_EVT_CHAIN = 0x03, OP_EVT_EXEC = 0x04, OP_EVT_KILL = 0x05,
    OP_IF = 0x06, OP_ELSE = 0x07, OP_ENDIF = 0x08, OP_SLEEP = 0x09, OP_SLEEPING = 0x0A, OP_WSLEEP = 0x0B,
    OP_WSLEEPING = 0x0C, OP_FOR = 0x0D, OP_NEXT = 0x0E, OP_WHILE = 0x0F, OP_EWHILE = 0x10, OP_DO = 0x11,
    OP_EDWHILE = 0x12, OP_SWITCH = 0x13, OP_CASE = 0x14, OP_DEFAULT = 0x15, OP_ESWITCH = 0x16, OP_GOTO = 0x17,
    OP_GOSUB = 0x18, OP_RETURN = 0x19, OP_BREAK = 0x1A, OP_FOR2 = 0x1B,
    OP_CK = 0x21, OP_SET = 0x22, OP_CMP = 0x23, OP_SAVE = 0x24, OP_COPY = 0x25, OP_CALC = 0x26, OP_CALC2 = 0x27,
    OP_MESSAGE_ON = 0x2B, OP_AOT_SET = 0x2C, OP_DOOR_AOT_SE = 0x3B, OP_AOT_RESET = 0x46, OP_ITEM_AOT_SET = 0x4E,
    OP_AOT_ON = 0x47, OP_SCE_KEY_CK = 0x4F, OP_SCE_TRG_CK = 0x50, OP_KEEP_ITEM_CK = 0x5E, OP_ITEM_LOST = 0x62,
    OP_ITEM_GET = 0x76, OP_ITEM_CK_LOST = 0x88, OP_SCE_EM_SET = 0x44, OP_CUT_CHG = 0x29, OP_CUT_OLD = 0x2A,
    OP_CUT_AUTO = 0x3C, OP_PLC_DEST = 0x40, OP_WORK_SET = 0x2E, OP_POS_SET = 0x32, OP_DIR_SET = 0x33,
    OP_MEMBER_SET = 0x34, OP_MEMBER_SET2 = 0x35, OP_MEMBER_COPY = 0x3D, OP_MEMBER_CMP = 0x3E, OP_MEMBER_CALC = 0x55,
    OP_MEMBER_CALC2 = 0x56, OP_WORK_COPY = 0x1D, OP_SCA_ID_SET = 0x37, OP_PLC_RET = 0x42,
    OP_SPEED_SET = 0x2F, OP_ADD_SPEED = 0x30, OP_ADD_ASPEED = 0x31,
    OP_AOT_SET_4P = 0x67, OP_DOOR_AOT_SET_4P = 0x68, OP_ITEM_AOT_SET_4P = 0x69
};

#define SCRIPT_INIT 16                         /* RDT sections */
#define SCRIPT_MAIN 17
#define BUDGET_ENTRY 20000
#define BUDGET_TICK 6000
#define BUDGET_TASK 800                        /* a task that won't yield gives up its frame */

enum { FALSE_COND = 0, CONTINUE = 1, YIELD = 2 };

uint32_t pe_re_scd_skips[256];

int pe_re_flag(const PeReFlags *f, unsigned group, unsigned bit) {
    return group < PE_RE_FLAG_GROUPS && bit < 256 && (f->bits[group][bit >> 3] >> (bit & 7) & 1);
}
void pe_re_flag_set(PeReFlags *f, unsigned group, unsigned bit, int on) {
    if (group >= PE_RE_FLAG_GROUPS || bit >= 256) return;
    if (on) f->bits[group][bit >> 3] |= (uint8_t)(1u << (bit & 7));
    else f->bits[group][bit >> 3] &= (uint8_t)~(1u << (bit & 7));
}

static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static int16_t s16(const uint8_t *p) { return (int16_t)le16(p); }

typedef struct {
    const uint8_t *rdt;
    uint32_t size;
    PeReFlags *flags;
    const PeReScdHost *host;
    PeReScdRun *run;
    int budget;
} Vm;

/* procedure `proc` of script `script` (0 init, 1 main): where it starts in the room file, or 0 */
static uint32_t proc_start(const Vm *vm, int script, unsigned proc) {
    const uint32_t off = vm->run->sec[script], len = vm->run->len[script];
    if (!off || len < 2) return 0;
    const uint32_t count = le16(vm->rdt + off) / 2;
    if (!count || count * 2 > len || proc >= count) return 0;
    const uint32_t a = le16(vm->rdt + off + 2 * proc);
    return a < len ? off + a : 0;
}

/* work_set's object (WORK_TYPE_*: 1 player, 2 partner, 3 enemy n, 4 scene object n, 5 door n) */
static int work_slot(unsigned kind, int n) {
    switch (kind) {
    case 1: return 0;
    case 2: return 1;
    case 3: return n >= 0 && n < 16 ? 2 + n : -1;
    case 4: return n >= 0 && n < 32 ? 18 + n : -1;
    case 5: return n >= 0 && n < 14 ? 50 + n : -1;
    default: return -1;
    }
}

/* work_copy's patch at `at`, or the empty slot it would go in (open addressing; never full) */
static int patch_slot(const PeReScdRun *r, uint32_t at) {
    int k = (int)((at * 2654435761u) >> 23) & (PE_RE_PATCHES - 1);
    while (r->patch[k].at && r->patch[k].at != at) k = (k + 1) & (PE_RE_PATCHES - 1);
    return k;
}

static int16_t *member(Vm *vm, const PeReScdTask *t, unsigned m) {
    static int16_t scratch;
    if (t->work < 0 || m >= PE_RE_MEMBERS) { scratch = 0; return &scratch; }
    return &vm->run->works[t->work][m];
}

static void task_start(PeReScdTask *t, uint32_t pc) {      /* Event_init */
    t->status = pc != 0;
    t->ifel[0] = -1, t->loop[0] = -1;
    t->work = 0;
    memset(t->spd, 0, sizeof t->spd);
    t->pc = pc;
}

static void exec_task(Vm *vm, unsigned level, unsigned proc) {   /* Event_exec: 10 and up = the first free 2-9 */
    if (level >= PE_RE_TASKS)
        for (level = 2; vm->run->task[level].status && level != PE_RE_TASKS - 1; level++) {}
    PeReScdTask *t = &vm->run->task[level];
    memset(t, 0, sizeof *t);
    task_start(t, proc_start(vm, 1, proc));
    t->proc = (int16_t)proc;
}

static void live(Vm *vm, uint32_t at) {
    for (int i = 0; i < vm->run->nlive; i++) if (vm->run->live[i] == at) return;
    if (vm->run->nlive < PE_RE_LIVE_MAX) vm->run->live[vm->run->nlive++] = at;
}

/* A zone instruction ran: it takes its id's slot. The zone that held it is gone, and so is any
 * aot_reset of that id (the game writes the new zone over the slot). */
static void zone_set(Vm *vm, uint8_t id, uint32_t at) {
    PeReScdRun *r = vm->run;
    const uint32_t old = r->slot[id];
    if (old && old != at)
        for (int i = 0; i < r->nlive; i++)
            if (r->live[i] == old) { r->live[i] = r->live[--r->nlive]; break; }
    int k = 0;
    for (int i = 0; i < r->nreset; i++) if (r->reset[i].id != id) r->reset[k++] = r->reset[i];
    r->nreset = k;
    r->slot[id] = at;
    live(vm, at);
}

/* Pad_set's Key_type[0]: the pad button (libetc order) behind each of the game's key bits */
static const uint16_t KEY_TYPE[16] = { 0x1000, 0x2000, 0x4000, 0x8000, 0x1000, 0x4000, 0x0040, 0x0040,
                                       0x0008, 0x0080, 0x8000, 0x2000, 0x0040, 0x0010, 0x0020, 0x0000 };

uint32_t pe_re_scd_keys(uint16_t pad) {
    uint32_t key = 0;
    for (int i = 0; i < 16; i++) if (pad & KEY_TYPE[i]) key |= 1u << i;
    return key;
}

static int compare(int a, int op, int b) {
    switch (op) {
    case 0: return a == b;
    case 1: return a > b;
    case 2: return a >= b;
    case 3: return a < b;
    case 4: return a <= b;
    case 5: return a != b;
    default: return a == b;
    }
}

static int16_t calc(int a, int op, int b) {
    switch (op) {
    case 0: return (int16_t)(a + b);
    case 1: return (int16_t)(a - b);
    case 2: return (int16_t)(a * b);
    case 3: return (int16_t)(b ? a / b : 0);
    case 4: return (int16_t)(b ? a % b : 0);
    case 5: return (int16_t)(a | b);
    case 6: return (int16_t)(a & b);
    case 7: return (int16_t)(a ^ b);
    case 8: return (int16_t)~a;
    case 9: return (int16_t)(a << (b & 15));
    case 10: return (int16_t)((uint16_t)a >> (b & 15));
    case 11: return (int16_t)(a >> (b & 15));
    default: return (int16_t)a;
    }
}

static int step(Vm *vm, PeReScdTask *t);

/* While_main: the first term, then "s16 and(0) / or, the next term" up to `len` bytes on */
static int conditions(Vm *vm, PeReScdTask *t, uint32_t len) {
    const uint32_t end = t->pc + len;
    int ok = step(vm, t) != FALSE_COND;
    while (t->pc < end && t->status && vm->budget-- > 0) {
        const int logic = s16(vm->rdt + t->pc);
        t->pc += 2;
        const int term = step(vm, t) != FALSE_COND;
        ok = logic ? (ok | term) : (ok & term);
    }
    t->pc = end;
    return ok;
}

/* One instruction: CONTINUE, YIELD (this task is done for the frame), or FALSE_COND (a condition) */
static int step(Vm *vm, PeReScdTask *t) {
    if (t->pc >= vm->size) { t->status = 0; return YIELD; }
    const uint8_t *ins = vm->rdt + t->pc, code = ins[0];
    const uint32_t n = pe_imp_scd_size(PE_IMP_RE2, code);
    if (!n || t->pc + n > vm->size) { t->status = 0; return YIELD; }   /* unreadable: the task ends */
    uint8_t patched[64];
    if (vm->run->npatch && n <= sizeof patched) {            /* work_copy wrote into this instruction? */
        int any = 0;
        for (uint32_t at = t->pc + 1; at < t->pc + n; at++) {   /* never the operation byte */
            const int k = patch_slot(vm->run, at);
            if (vm->run->patch[k].at != at) continue;
            if (!any) memcpy(patched, ins, n), any = 1;
            const uint16_t v = (uint16_t)vm->run->patch[k].value;
            patched[at - t->pc] = (uint8_t)v;
            if (vm->run->patch[k].wide && at + 1 < t->pc + n) patched[at + 1 - t->pc] = (uint8_t)(v >> 8);
        }
        if (any) ins = patched;
    }
    int8_t *ifel = &t->ifel[t->sub], *loop = &t->loop[t->sub];
    const int s = t->sub, l = *loop;
    vm->run->executed++;
    switch (code) {
    case OP_NOP: t->pc += 1; return CONTINUE;
    case OP_EVT_END: case OP_RETURN:                       /* back to the gosub, or the task ends */
        if (t->sub > 0) { t->sub--; t->pc = t->ret[t->sub]; return CONTINUE; }
        t->status = 0;
        return YIELD;
    case OP_EVT_NEXT: t->pc += 1; return YIELD;
    case OP_EVT_CHAIN: task_start(t, proc_start(vm, 1, ins[3])); return CONTINUE;
    case OP_EVT_EXEC: t->pc += 4; exec_task(vm, ins[1], ins[3]); return CONTINUE;
    case OP_EVT_KILL: if (ins[1] < PE_RE_TASKS) vm->run->task[ins[1]].status = 0; t->pc += 2; return CONTINUE;
    case OP_GOSUB: {
        const uint32_t to = proc_start(vm, 1, ins[1]);
        t->pc += 2;
        if (!to || t->sub + 1 >= PE_RE_LEVELS) return CONTINUE;
        t->ret[t->sub] = t->pc;
        t->sub++;
        t->ifel[t->sub] = -1, t->loop[t->sub] = -1;
        t->pc = to;
        return CONTINUE;
    }
    case OP_IF:
        t->pc += 4;
        if (*ifel < 7) t->ifend[s][++*ifel] = t->pc + le16(ins + 2);
        return CONTINUE;
    case OP_ELSE:                                           /* the true branch ran: past the else's block */
        if (*ifel >= 0) (*ifel)--;
        t->pc += le16(ins + 2);
        return CONTINUE;
    case OP_ENDIF:
        if (*ifel >= 0) (*ifel)--;
        t->pc += 2;
        return CONTINUE;
    case OP_SLEEP:                                          /* then sleeping counts the frames down */
        t->pc += 1;
        if (l < 3) t->lcnt[s][++*loop] = (int16_t)le16(ins + 2);
        return CONTINUE;
    case OP_SLEEPING:
        if (l < 0 || --t->lcnt[s][l] <= 0) { t->pc += 3; if (l >= 0) (*loop)--; }
        return YIELD;
    case OP_WSLEEP: if (l < 3) (*loop)++; t->pc += 1; return CONTINUE;
    case OP_WSLEEPING:                                      /* until the message is gone */
        if (!(vm->host && vm->host->talking && vm->host->talking(vm->host->ctx))) { t->pc += 1; if (l >= 0) (*loop)--; }
        return YIELD;
    case OP_FOR: case OP_FOR2: {
        const int count = code == OP_FOR ? s16(ins + 4) : vm->flags->work[ins[5]];
        const int brk = s16(ins + 2);
        if (count && l < 3) {
            (*loop)++;
            t->lcnt[s][*loop] = (int16_t)count;
            t->pc += 6;
            t->lbreak[s][*loop] = (uint32_t)((int32_t)t->pc + brk), t->lstack[s][*loop] = t->pc;
            t->loop_if[s][*loop] = *ifel;
        } else {
            t->pc = (uint32_t)((int32_t)t->pc + 6 + brk);
        }
        return CONTINUE;
    }
    case OP_NEXT:
        if (l >= 0 && --t->lcnt[s][l]) t->pc = t->lstack[s][l];
        else { t->pc += 2; if (l >= 0) (*loop)--; }
        return CONTINUE;
    case OP_WHILE:
        if (l >= 3) { t->status = 0; return YIELD; }
        (*loop)++;
        t->lstack[s][*loop] = t->pc;                        /* ewhile comes back to the while itself */
        t->pc += 4;
        t->lbreak[s][*loop] = (uint32_t)((int32_t)t->pc + s16(ins + 2));
        t->loop_if[s][*loop] = *ifel;
        if (!conditions(vm, t, ins[1])) { t->pc = t->lbreak[s][*loop]; (*loop)--; }
        return CONTINUE;
    case OP_EWHILE:
        if (l >= 0) { t->pc = t->lstack[s][l]; (*loop)--; } else t->pc += 2;
        return CONTINUE;
    case OP_DO:
        if (l >= 3) { t->status = 0; return YIELD; }
        (*loop)++;
        t->pc += 4;
        t->lstack[s][*loop] = t->pc, t->lbreak[s][*loop] = (uint32_t)((int32_t)t->pc + s16(ins + 2));
        t->loop_if[s][*loop] = *ifel;
        return CONTINUE;
    case OP_EDWHILE:
        t->pc += 2;
        if (conditions(vm, t, ins[1]) && l >= 0) t->pc = t->lstack[s][l];
        else if (l >= 0) (*loop)--;
        return CONTINUE;
    case OP_SWITCH: {
        if (l >= 3) { t->status = 0; return YIELD; }
        (*loop)++;
        uint32_t p = t->pc + 4;
        t->lbreak[s][*loop] = p + le16(ins + 2), t->loop_if[s][*loop] = *ifel;
        const int v = vm->flags->work[ins[1]];
        for (int guard = 0; p + 6 <= vm->size && guard < 256; guard++) {
            const uint8_t c = vm->rdt[p];
            if (c == OP_DEFAULT) { p += 2; break; }
            if (c == OP_ESWITCH) { (*loop)--; p += 2; break; }
            const uint32_t ofs = le16(vm->rdt + p + 2);
            const int val = s16(vm->rdt + p + 4);
            p += 6;
            if (v == val) break;
            p += ofs;
        }
        t->pc = p;
        return CONTINUE;
    }
    case OP_CASE: t->pc += 6; return CONTINUE;
    case OP_DEFAULT: t->pc += 2; return CONTINUE;
    case OP_ESWITCH: if (l >= 0) (*loop)--; t->pc += 2; return CONTINUE;
    case OP_GOTO:
        *ifel = (int8_t)ins[1], *loop = (int8_t)ins[2];
        if (*ifel > 7) *ifel = 7;
        if (*loop > 3) *loop = 3;
        t->pc = (uint32_t)((int32_t)t->pc + s16(ins + 4));
        return CONTINUE;
    case OP_BREAK:
        if (l >= 0) { t->pc = t->lbreak[s][l]; *ifel = t->loop_if[s][l]; (*loop)--; } else t->pc += 2;
        return CONTINUE;
    case OP_CK:
        t->pc += 4;
        return pe_re_flag(vm->flags, ins[1], ins[2]) == (ins[3] != 0) ? CONTINUE : FALSE_COND;
    case OP_SET:
        pe_re_flag_set(vm->flags, ins[1], ins[2], ins[3] == 7 ? !pe_re_flag(vm->flags, ins[1], ins[2]) : ins[3] != 0);
        t->pc += 4;
        return CONTINUE;
    case OP_CMP: t->pc += n; return compare(vm->flags->work[ins[2]], ins[3], s16(ins + 4)) ? CONTINUE : FALSE_COND;
    case OP_SAVE: vm->flags->work[ins[1]] = s16(ins + 2); t->pc += n; return CONTINUE;
    case OP_COPY: vm->flags->work[ins[1]] = vm->flags->work[ins[2]]; t->pc += n; return CONTINUE;
    case OP_CALC: vm->flags->work[ins[3]] = calc(vm->flags->work[ins[3]], ins[2], s16(ins + 4)); t->pc += n; return CONTINUE;
    case OP_CALC2: vm->flags->work[ins[2]] = calc(vm->flags->work[ins[2]], ins[1], vm->flags->work[ins[3]]); t->pc += n; return CONTINUE;
    case OP_KEEP_ITEM_CK:
        t->pc += n;
        return vm->host && vm->host->has && vm->host->has(vm->host->ctx, ins[1]) ? CONTINUE : FALSE_COND;
    case OP_SCE_KEY_CK: case OP_SCE_TRG_CK: {          /* byte 1: the answer when a key matches */
        const uint32_t keys = !vm->host ? 0 : code == OP_SCE_KEY_CK ? vm->host->key : vm->host->key_trg;
        const int hit = (keys & le16(ins + 2)) != 0;
        t->pc += n;
        return (hit ? ins[1] : ins[1] ^ 1) ? CONTINUE : FALSE_COND;
    }
    case OP_MESSAGE_ON: vm->run->message = ins[2], vm->run->messages++; t->pc += n; return CONTINUE;
    case OP_AOT_SET: case OP_DOOR_AOT_SE: case OP_ITEM_AOT_SET:
    case OP_AOT_SET_4P: case OP_DOOR_AOT_SET_4P: case OP_ITEM_AOT_SET_4P:
        zone_set(vm, ins[1], t->pc);
        t->pc += n;
        return CONTINUE;
    case OP_CUT_CHG: case OP_CUT_OLD: {                    /* Cut_chg_main: the last camera kept */
        PeReScdRun *r = vm->run;
        const int to = code == OP_CUT_CHG ? ins[1] & 0x7F : r->camera_old;
        r->camera_old = r->camera, r->camera = to, r->camera_lock = code == OP_CUT_CHG;
        t->pc += n;
        return CONTINUE;
    }
    case OP_CUT_AUTO: vm->run->camera_lock = ins[1] != 1; t->pc += n; return CONTINUE;
    case OP_WORK_COPY: {                                   /* a work value into the script, ins[2] bytes past this */
        PeReScdRun *r = vm->run;
        const uint32_t at = t->pc + 4 + ins[2];
        const int k = patch_slot(r, at);
        if (r->patch[k].at == at || (!r->patch[k].at && r->npatch < PE_RE_PATCHES - 1)) {
            if (!r->patch[k].at) r->npatch++;
            r->patch[k].at = at, r->patch[k].value = vm->flags->work[ins[1]], r->patch[k].wide = ins[3] != 0;
        }
        t->pc += n;
        return CONTINUE;
    }
    case OP_SCA_ID_SET:                                    /* box ins[1] + 1's Id top bits: a wall on or off */
        vm->run->sca_id[(ins[1] + 1) & 0xFF] = (uint16_t)((le16(ins + 2) & 0xFF80) | 1);
        vm->run->sca_changed = 1;
        t->pc += n;
        return CONTINUE;
    case OP_WORK_SET: t->work = (int8_t)work_slot(ins[1], (int8_t)ins[2]); t->pc += n; return CONTINUE;
    case OP_POS_SET: case OP_DIR_SET: {
        const unsigned at = code == OP_POS_SET ? PE_RE_MEMBER_POS_X : 14;
        for (int k = 0; k < 3; k++) *member(vm, t, at + (unsigned)k) = s16(ins + 2 + 2 * k);
        if (t->work == 0) vm->run->player_set = 1;
        t->pc += n;
        return CONTINUE;
    }
    case OP_MEMBER_SET: *member(vm, t, ins[1]) = s16(ins + 2); if (t->work == 0) vm->run->player_set = 1; t->pc += n; return CONTINUE;
    case OP_MEMBER_SET2: *member(vm, t, ins[1]) = vm->flags->work[ins[2]]; if (t->work == 0) vm->run->player_set = 1; t->pc += n; return CONTINUE;
    case OP_MEMBER_COPY: vm->flags->work[ins[1]] = *member(vm, t, ins[2]); t->pc += n; return CONTINUE;
    case OP_MEMBER_CMP: {                                  /* the comparison word: operator << 8 | member */
        const unsigned word = le16(ins + 2);
        const int v = *member(vm, t, word & 0xFF), want = s16(ins + 4), op = (int)(word >> 8);
        t->pc += n;
        return (op == 6 ? (v & want) != 0 : compare(v, op, want)) ? CONTINUE : FALSE_COND;
    }
    case OP_MEMBER_CALC: {                                 /* the command word: member << 8 | operation */
        const unsigned word = le16(ins + 2);
        int16_t *m = member(vm, t, word >> 8);
        *m = calc(*m, (int)(word & 0xFF), s16(ins + 4));
        t->pc += n;
        return CONTINUE;
    }
    case OP_MEMBER_CALC2: {
        int16_t *m = member(vm, t, ins[2]);
        *m = calc(*m, ins[1], vm->flags->work[ins[3]]);
        t->pc += n;
        return CONTINUE;
    }
    case OP_PLC_DEST: {                                    /* walk to (x, z); ins[3]: the arrival flag */
        PeReScdRun *r = vm->run;
        const int16_t x = s16(ins + 4), z = s16(ins + 6);
        if (t->work == 0) {                                /* the player: the caller moves her */
            if (x || z) r->dest_x = x, r->dest_z = z, r->dest_moved = 1;
        } else if (x || z) {                               /* the partner or anyone else: just there */
            *member(vm, t, PE_RE_MEMBER_POS_X) = x, *member(vm, t, PE_RE_MEMBER_POS_Z) = z;
        }
        if (t->work >= 0) r->arrived[t->work] = ins[3];   /* each walker's own flag, from the next frame on */
        t->pc += n;
        return CONTINUE;
    }
    case OP_SPEED_SET:                                     /* one of the task's 12 speeds */
        if (ins[1] < 12) t->spd[ins[1]] = s16(ins + 2);
        t->pc += n;
        return CONTINUE;
    case OP_ADD_SPEED:                                     /* its object moves and turns by them */
        for (int k = 0; k < 3; k++) {
            *member(vm, t, PE_RE_MEMBER_POS_X + (unsigned)k) += t->spd[k];
            *member(vm, t, 14 + (unsigned)k) += t->spd[3 + k];
        }
        if (t->work == 0) vm->run->player_set = 1;
        t->pc += n;
        return CONTINUE;
    case OP_ADD_ASPEED:                                    /* the speeds change by their accelerations */
        for (int k = 0; k < 6; k++) t->spd[k] = (int16_t)(t->spd[k] + t->spd[6 + k]);
        t->pc += n;
        return CONTINUE;
    case OP_PLC_RET:                                       /* back to normal: no more arrival flag */
        if (t->work >= 0) vm->run->arrived[t->work] = 0;
        t->pc += n;
        return CONTINUE;
    case OP_SCE_EM_SET: {                                  /* an enemy placed (the caller checks its flag) */
        PeReScdRun *r = vm->run;
        int k = 0;
        while (k < r->nenemy && r->enemy[k] != t->pc) k++;
        if (k == r->nenemy && r->nenemy < PE_RE_ENEMIES_MAX) r->enemy[r->nenemy++] = t->pc;
        const int slot = work_slot(3, (int8_t)ins[2]);
        if (slot >= 0) {                                   /* SCE_EM_SET_WORK: id, type, floor, position, facing */
            int16_t *w = r->works[slot];
            w[0] = 1, w[6] = ins[3], w[7] = s16(ins + 4), w[17] = ins[6];
            w[11] = s16(ins + 10), w[12] = s16(ins + 12), w[13] = s16(ins + 14), w[15] = s16(ins + 16);
        }
        t->pc += n;
        return CONTINUE;
    }
    case OP_AOT_ON:                                        /* the zone acts now, as if set off */
        if (vm->run->naot_on < PE_RE_AOT_ON_MAX) vm->run->aot_on[vm->run->naot_on++] = ins[1];
        t->pc += n;
        return CONTINUE;
    case OP_ITEM_LOST: case OP_ITEM_CK_LOST: case OP_ITEM_GET:
        if (vm->host && code == OP_ITEM_GET && vm->host->give) vm->host->give(vm->host->ctx, ins[1], n > 2 ? ins[2] : 1);
        else if (vm->host && code != OP_ITEM_GET && vm->host->lose)
            vm->host->lose(vm->host->ctx, ins[1], code == OP_ITEM_CK_LOST && n > 2 ? ins[2] : 0);
        t->pc += n;
        return CONTINUE;
    case OP_AOT_RESET:
        if (vm->run->nreset < PE_RE_RESETS_MAX) {
            PeReReset *r = &vm->run->reset[vm->run->nreset++];
            r->id = ins[1], r->sce = ins[2], r->sat = ins[3];
            memcpy(r->data, ins + 4, sizeof r->data);
        }
        t->pc += n;
        return CONTINUE;
    default:
        if (!vm->run->skipped++) vm->run->first_skipped = code;
        if (vm->run->skipped_by_op[code] < 0xFFFF) vm->run->skipped_by_op[code]++;
        pe_re_scd_skips[code]++;
        t->pc += n;
        return CONTINUE;
    }
}

/* Sce_scheduler_main: every task in order, each until it yields */
static void schedule(Vm *vm) {
    vm->run->waiting = 0;
    for (int k = 0; k < PE_RE_TASKS; k++) {
        PeReScdTask *t = &vm->run->task[k];
        int own = BUDGET_TASK;
        while (t->status && vm->budget > 0) {
            if (own-- <= 0) { vm->run->stopped++; break; }
            vm->budget--;
            const int r = step(vm, t);
            if (r == CONTINUE) continue;
            if (r == YIELD) break;
            if (t->ifel[t->sub] < 0) break;                 /* outside any if: on next frame */
            t->pc = t->ifend[t->sub][t->ifel[t->sub]--];    /* to the end of its if block */
        }
        vm->run->waiting += t->status != 0;
    }
}

static void work_clr(PeReFlags *f) {                       /* Sce_work_clr */
    for (int i = PE_RE_WORK_F_ATARI; i <= PE_RE_WORK_GET_ID; i++) f->work[i] = -1;
    memset(f->bits[PE_RE_FG_USE], 0, sizeof f->bits[PE_RE_FG_USE]);
    memset(&f->bits[5][4], 0, 4);                          /* Room_flg[1]: script bits 32-63 */
}

static int setup(Vm *vm, const PeImpRoomLogic *l, PeReFlags *flags, const PeReScdHost *host, PeReScdRun *run,
                 int budget) {
    uint32_t size = 0;
    if (!l || l->game != PE_IMP_RE2) return 0;
    const uint8_t *rdt = pe_imp_logic_section(l, PE_IMP_LOGIC_RDT0, NULL, &size);
    if (!rdt) return 0;
    vm->rdt = rdt, vm->size = size, vm->flags = flags, vm->host = host, vm->run = run, vm->budget = budget;
    return 1;
}

void pe_re_scd_run(const PeImpRoomLogic *l, PeReFlags *flags, const PeReScdHost *host, PeReScdRun *out) {
    Vm vm;
    memset(out, 0, sizeof *out);
    out->message = -1;
    if (!setup(&vm, l, flags, host, out, BUDGET_ENTRY)) return;
    if (pe_imp_rdt_section(vm.rdt, vm.size, SCRIPT_INIT, &out->sec[0], &out->len[0])) out->sec[0] = out->len[0] = 0;
    if (pe_imp_rdt_section(vm.rdt, vm.size, SCRIPT_MAIN, &out->sec[1], &out->len[1])) out->sec[1] = out->len[1] = 0;
    if (!out->sec[0] && !out->sec[1]) return;
    out->ran = 1;
    work_clr(flags);
    task_start(&out->task[0], proc_start(&vm, 0, 0));      /* Event_exec(0, 0) on the init script */
    out->task[0].proc = -1;
    task_start(&out->task[1], proc_start(&vm, 1, 0));      /* Event_exec(1, 0) on the main script */
    schedule(&vm);
}

/* Enemy stand-ins. Enemies' own code doesn't run here, but a few scenes wait on what it does to a
 * field the scripts read. Each stand-in does just that part, after about how long it takes in game:
 * - enemy 0x31 (a G form), Sce_free[0]:
 *   bit 0x10 is "come out of pod n" (the low nibble); its appearing routine clears the bit when it's
 *   out (EM31.C Em31_mv08_br / Em31_mv08). 5-00's sky tram scene waits on it four times.
 *   bit 1 starts it breaking out of its shell; it sets bit 2 when it's out (EM31.C, the routine
 *   that checks Sce_free0 & 1). 5-09's scene waits on that. */
static void enemy_standins(PeReScdRun *run) {
    for (int n = 0; n < 16; n++) {
        int16_t *w = run->works[2 + n];
        int16_t *f = &w[PE_RE_MEMBER_SCE_FREE0];
        int order = 0, frames = 0;
        if (w[0] && w[PE_RE_MEMBER_ID] == 0x31) {
            if (*f & 0x10) order = 0x10, frames = 60;              /* out of the pod: about a second */
            else if ((*f & 1) && !(*f & 2)) order = 1, frames = 90; /* out of the shell: a second and a half */
        }
        if (!order) { run->em_busy[n] = 0; continue; }
        if (!run->em_busy[n]) run->em_busy[n] = (uint8_t)frames;    /* the order's just come in */
        else if (!--run->em_busy[n]) *f = (int16_t)(order == 0x10 ? *f & ~0x10 : *f | 2);
    }
}

void pe_re_scd_tick(const PeImpRoomLogic *l, PeReFlags *flags, const PeReScdHost *host, PeReScdRun *run) {
    Vm vm;
    if (!run->ran || !setup(&vm, l, flags, host, run, BUDGET_TICK)) return;
    for (int w = 0; w < PE_RE_WORKS; w++)                  /* they got there (at once): the engine's arrival flags, */
        if (run->arrived[w]) pe_re_flag_set(flags, 5, run->arrived[w], 1);   /* set again after each frame's clear */
    enemy_standins(run);
    exec_task(&vm, 0, 1);                                  /* Event_exec(0, 1): the every-frame handler */
    schedule(&vm);
    work_clr(flags);
}

void pe_re_scd_exec(const PeImpRoomLogic *l, PeReScdRun *run, unsigned level, unsigned proc) {
    Vm vm;
    PeReFlags none;
    if (!run->ran || !setup(&vm, l, &none, NULL, run, 0)) return;
    exec_task(&vm, level, proc);
}

int pe_re_scd_running(const PeReScdRun *run, unsigned proc) {
    for (int i = 0; i < PE_RE_TASKS; i++) if (run->task[i].status && run->task[i].proc == (int)proc) return 1;
    return 0;
}

int pe_re_scd_live(const PeReScdRun *r, uint32_t at) {
    if (!r->ran || !at) return 1;                          /* not run, or a store from before ZAT0 */
    for (int i = 0; i < r->nlive; i++) if (r->live[i] == at) return 1;
    return 0;
}
