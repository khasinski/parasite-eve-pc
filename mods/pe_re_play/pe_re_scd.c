/* The RE2 room script interpreter, first cut. See pe_re_scd.h. */
#include <string.h>

#include "pe_re_scd.h"
#include "pe_imp_rdt.h"
#include "pe_imp_room.h"

enum {
    OP_NOP = 0x00, OP_EVT_END = 0x01, OP_EVT_NEXT = 0x02, OP_EVT_CHAIN = 0x03, OP_EVT_EXEC = 0x04,
    OP_IF = 0x06, OP_ELSE = 0x07, OP_ENDIF = 0x08, OP_SLEEP = 0x09, OP_SLEEPING = 0x0A, OP_WSLEEP = 0x0B,
    OP_WSLEEPING = 0x0C, OP_FOR = 0x0D, OP_WHILE = 0x0F, OP_DO = 0x11, OP_SWITCH = 0x13, OP_GOTO = 0x17,
    OP_GOSUB = 0x18, OP_RETURN = 0x19, OP_BREAK = 0x1A,
    OP_CK = 0x21, OP_SET = 0x22, OP_CMP = 0x23, OP_SAVE = 0x24, OP_COPY = 0x25, OP_CALC = 0x26, OP_CALC2 = 0x27,
    OP_AOT_SET = 0x2C, OP_DOOR_AOT_SE = 0x3B, OP_AOT_RESET = 0x46, OP_ITEM_AOT_SET = 0x4E,
    OP_SCE_KEY_CK = 0x4F, OP_SCE_TRG_CK = 0x50, OP_KEEP_ITEM_CK = 0x5E,
    OP_AOT_SET_4P = 0x67, OP_DOOR_AOT_SET_4P = 0x68, OP_ITEM_AOT_SET_4P = 0x69
};

#define SCRIPT_INIT 16                         /* RDT sections */
#define SCRIPT_MAIN 17
#define BUDGET 20000
#define FRAMES 8
#define IFS 32
#define THREADS 32
#define WORK 256

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
    uint32_t size, sec[2], len[2];             /* init, main: offset and length in the room file */
    PeReFlags *flags;
    PeReHasItem has;
    void *ctx;
    PeReScdRun *out;
    int16_t work[WORK];
    int budget;
    struct { int script; unsigned proc; } queue[THREADS];
    int queued;
} Vm;

/* procedure `proc` of script `script` (0 init, 1 main): its bytes in the room file */
static int proc_range(const Vm *vm, int script, unsigned proc, uint32_t *start, uint32_t *end) {
    const uint32_t off = vm->sec[script], len = vm->len[script];
    if (!off || len < 2) return -1;
    const uint32_t count = le16(vm->rdt + off) / 2;
    if (!count || count * 2 > len || proc >= count) return -1;
    const uint32_t a = le16(vm->rdt + off + 2 * proc), b = proc + 1 < count ? le16(vm->rdt + off + 2 * (proc + 1)) : len;
    if (a >= len || b > len || b < a) return -1;
    *start = off + a, *end = off + b;
    return 0;
}

static void live(Vm *vm, uint32_t at) {
    if (vm->out->nlive < PE_RE_LIVE_MAX) vm->out->live[vm->out->nlive++] = at;
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

static void queue(Vm *vm, int script, unsigned proc) {
    if (vm->queued < THREADS) vm->queue[vm->queued].script = script, vm->queue[vm->queued++].proc = proc;
}

/* One thread: a procedure and what it calls, until it ends, waits or loops. */
static void thread(Vm *vm, int script, unsigned proc) {
    struct { uint32_t pc, end; int script, ifs; } frame[FRAMES];
    uint32_t ifend[IFS];
    int frames = 0, ifs = 0;
    if (proc_range(vm, script, proc, &frame[0].pc, &frame[0].end)) return;
    frame[0].script = script, frame[0].ifs = 0, frames = 1;
    while (frames && vm->budget-- > 0) {
        uint32_t *pc = &frame[frames - 1].pc;
        if (*pc >= frame[frames - 1].end) { ifs = frame[--frames].ifs; continue; }   /* fell off the end: return */
        const uint8_t *ins = vm->rdt + *pc, op = ins[0];
        const uint32_t n = pe_imp_scd_size(PE_IMP_RE2, op);
        if (!n || *pc + n > vm->size) { vm->out->stopped++; return; }
        int cond = -1;                                     /* -1: not a condition */
        vm->out->executed++;
        switch (op) {
        case OP_NOP: break;
        case OP_EVT_END: case OP_RETURN:
            ifs = frame[--frames].ifs;
            continue;
        case OP_GOSUB:
            if (frames < FRAMES) {
                *pc += n;
                if (!proc_range(vm, frame[frames - 1].script, ins[1], &frame[frames].pc, &frame[frames].end)) {
                    frame[frames].script = frame[frames - 1].script, frame[frames].ifs = ifs;
                    frames++;
                }
                continue;
            }
            break;
        case OP_EVT_EXEC: queue(vm, 1, ins[3]); break;           /* evt_exec:ugp, its own thread */
        case OP_EVT_CHAIN: queue(vm, 1, ins[3]); vm->out->stopped++; return;
        case OP_EVT_NEXT: case OP_SLEEP: case OP_SLEEPING: case OP_WSLEEP: case OP_WSLEEPING:
        case OP_FOR: case OP_WHILE: case OP_DO: case OP_SWITCH: case OP_GOTO: case OP_BREAK:
            vm->out->stopped++;                            /* waits and loops: later */
            return;
        case OP_IF:
            if (ifs < IFS) ifend[ifs++] = *pc + 4 + le16(ins + 2);
            break;
        case OP_ELSE:                                      /* the true branch ran: past the else's block, */
            if (ifs > frame[frames - 1].ifs) ifs--;          /* counted from the else itself (Else_ck) */
            *pc += le16(ins + 2);
            continue;
        case OP_ENDIF:
            if (ifs > frame[frames - 1].ifs) ifs--;
            break;
        case OP_CK: cond = pe_re_flag(vm->flags, ins[1], ins[2]) == (ins[3] != 0); break;
        case OP_SET:
            pe_re_flag_set(vm->flags, ins[1], ins[2], ins[3] == 7 ? !pe_re_flag(vm->flags, ins[1], ins[2]) : ins[3] != 0);
            break;
        case OP_CMP: cond = compare(vm->work[ins[2]], ins[3], s16(ins + 4)); break;
        case OP_SAVE: vm->work[ins[1]] = s16(ins + 2); break;
        case OP_COPY: vm->work[ins[1]] = vm->work[ins[2]]; break;
        case OP_CALC: vm->work[ins[3]] = calc(vm->work[ins[3]], ins[2], s16(ins + 4)); break;
        case OP_CALC2: vm->work[ins[2]] = calc(vm->work[ins[2]], ins[1], vm->work[ins[3]]); break;
        case OP_KEEP_ITEM_CK: cond = vm->has ? vm->has(vm->ctx, ins[1]) != 0 : 0; break;
        case OP_SCE_KEY_CK: case OP_SCE_TRG_CK: cond = 0; break;   /* nothing pressed or triggered on entry */
        case OP_AOT_SET: case OP_DOOR_AOT_SE: case OP_ITEM_AOT_SET:
        case OP_AOT_SET_4P: case OP_DOOR_AOT_SET_4P: case OP_ITEM_AOT_SET_4P:
            live(vm, *pc);
            break;
        case OP_AOT_RESET:
            if (vm->out->nreset < PE_RE_RESETS_MAX) {
                PeReReset *r = &vm->out->reset[vm->out->nreset++];
                r->id = ins[1], r->sce = ins[2], r->sat = ins[3];
                memcpy(r->data, ins + 4, sizeof r->data);
            }
            break;
        default:
            if (!vm->out->skipped++) vm->out->first_skipped = op;
            break;
        }
        if (cond == 0) {                                   /* false: pop the if's block end and go there */
            if (ifs > frame[frames - 1].ifs) {
                *pc = ifend[--ifs];
            } else {                                       /* outside any if: the game waits until it's true */
                vm->out->stopped++;
                return;
            }
            continue;
        }
        *pc += n;
    }
    if (frames) vm->out->stopped++;                        /* out of budget */
}

void pe_re_scd_run(const PeImpRoomLogic *l, PeReFlags *flags, PeReHasItem has, void *ctx, PeReScdRun *out) {
    static Vm vm;                                          /* big: not on the stack */
    uint32_t size = 0;
    memset(out, 0, sizeof *out);
    if (!l || l->game != PE_IMP_RE2) return;
    const uint8_t *rdt = pe_imp_logic_section(l, PE_IMP_LOGIC_RDT0, NULL, &size);
    if (!rdt) return;
    memset(&vm, 0, sizeof vm);
    vm.rdt = rdt, vm.size = size, vm.flags = flags, vm.has = has, vm.ctx = ctx, vm.out = out, vm.budget = BUDGET;
    if (pe_imp_rdt_section(rdt, size, SCRIPT_INIT, &vm.sec[0], &vm.len[0])) vm.sec[0] = vm.len[0] = 0;
    if (pe_imp_rdt_section(rdt, size, SCRIPT_MAIN, &vm.sec[1], &vm.len[1])) vm.sec[1] = vm.len[1] = 0;
    if (!vm.sec[0] && !vm.sec[1]) return;
    out->ran = 1;
    thread(&vm, 0, 0);
    thread(&vm, 1, 0);
    for (int i = 0; i < vm.queued && vm.budget > 0; i++) thread(&vm, vm.queue[i].script, vm.queue[i].proc);
}

int pe_re_scd_live(const PeReScdRun *r, uint32_t at) {
    if (!r->ran || !at) return 1;                          /* not run, or a store from before ZAT0 */
    for (int i = 0; i < r->nlive; i++) if (r->live[i] == at) return 1;
    return 0;
}
