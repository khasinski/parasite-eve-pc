/* Where a stuck thread is, for the randomizer run test's real-fight lines: a frame that never ends
 * leaves no crash to report, so the watchdog prints the game thread's pc and the return addresses on
 * its stack as offsets in the exe (addr2line -f -e <exe> 0x140000000+offset names them). A crash prints
 * the same line from where it happened.
 * Windows only (its own file: windows.h and the game's headers both define RECT); elsewhere it prints nothing. */
#include <stdio.h>
#ifdef _WIN32
#include <stdint.h>
#include <string.h>
#include <windows.h>

static HANDLE s_thread;

/* A pc and the return addresses on the stack under sp, as offsets in the exe. */
static void pe_stack_print(FILE *out, uintptr_t pc, uintptr_t stack) {
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
    uintptr_t size = nt->OptionalHeader.SizeOfImage;
    const uintptr_t *sp = (const uintptr_t *)stack;
    MEMORY_BASIC_INFORMATION mem;
    size_t words = 0;
    fprintf(out, "STACK %#llx", (unsigned long long)(pc - base));
    if (VirtualQuery(sp, &mem, sizeof(mem)))   /* stay inside the stack's committed region */
        words = ((uintptr_t)mem.BaseAddress + mem.RegionSize - (uintptr_t)sp) / sizeof(*sp);
    for (size_t k = 0, n = 0; k < words && k < 6000 && n < 48; ++k)
        if (sp[k] - base < size) { fprintf(out, " %#llx", (unsigned long long)(sp[k] - base)); ++n; }
    fputc('\n', out);
}

/* A crash (an access violation, a divide by zero, ...): the same line, once, before the process dies. */
static LONG CALLBACK pe_crash_stack(EXCEPTION_POINTERS *e) {
    static LONG seen;
    const DWORD code = e->ExceptionRecord->ExceptionCode;
    if (code < 0xC0000000u || code == 0xC06D007Eu || code == 0xC06D007Fu || InterlockedExchange(&seen, 1)) return EXCEPTION_CONTINUE_SEARCH;
    fprintf(stderr, "CRASH %#lx at address %#llx\n", (unsigned long)code,
            (unsigned long long)(e->ExceptionRecord->NumberParameters > 1 ? e->ExceptionRecord->ExceptionInformation[1] : 0));
    pe_stack_print(stderr, (uintptr_t)e->ContextRecord->Rip, (uintptr_t)e->ContextRecord->Rsp);
    fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}

void pe_stall_stack_init(void) {
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &s_thread, 0, FALSE, DUPLICATE_SAME_ACCESS);
    AddVectoredExceptionHandler(1, pe_crash_stack);
}

void pe_stall_stack_print(FILE *out) {
    CONTEXT c;
    memset(&c, 0, sizeof(c));
    c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    if (!s_thread || SuspendThread(s_thread) == (DWORD)-1 || !GetThreadContext(s_thread, &c)) return;
    pe_stack_print(out, (uintptr_t)c.Rip, (uintptr_t)c.Rsp);
    ResumeThread(s_thread);
}
#else
void pe_stall_stack_init(void) {}
void pe_stall_stack_print(FILE *out) { (void)out; }
#endif
