/* Crash reports for players on Linux and macOS: the same report crash_report.c
 * makes on Windows.
 *
 * Everything the game prints still reaches the terminal and is also kept in
 * logs/parasite-eve-log.txt next to the executable while the game runs (a
 * normal quit deletes it). When that folder cannot
 * be written, or the game is a macOS app (writing inside it would break its
 * signature), the logs folder is in ~/Library/Logs/Parasite Eve on macOS and
 * in $XDG_STATE_HOME/parasite-eve (~/.local/state/parasite-eve) on Linux.
 * After a crash that log becomes one file, logs/crash-<date>.txt: what went
 * wrong and where, every thread's call stack, registers and memory around the
 * crash, the game's state, the mods, the computer, the loaded libraries, then
 * the whole log. A dialog says where the file is (osascript on macOS; zenity,
 * kdialog or xmessage on Linux, whichever is installed; none on iOS).
 *
 * Function names come from the executable's own symbol table (ELF .symtab,
 * Mach-O LC_SYMTAB), so a report from any build can be read without that
 * build at hand (unless the build was stripped: the log's header says so).
 *
 * A stack overflow is reported only on the main thread (the one with the
 * spare signal stack). Nothing here runs per frame: two threads wait on pipes
 * and only wake up when the game prints something. */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif

#include "pe1/crash_report.h"

#if (defined(__linux__) || defined(__APPLE__)) && !defined(__EMSCRIPTEN__)

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <poll.h>
#include <sys/syscall.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach/mach.h>
#include <sys/sysctl.h>
#define CRASH_DIALOG (!TARGET_OS_IPHONE)
#else
#include <link.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <ucontext.h>
#include <unwind.h>
/* Android has no Unix dialog helpers or posix_spawnp before API 28. Keep
 * native crash reports; the Android frontend owns reporting their paths. */
#if defined(__ANDROID__)
#define CRASH_DIALOG 0
#else
#define CRASH_DIALOG 1
#endif
#endif

#if CRASH_DIALOG
#include <spawn.h>
extern char **environ;
#endif

#ifndef PE_VERSION
#define PE_VERSION "unknown"
#endif

/* The report plus the log stays well under Discord's 10 MB upload limit. */
#define CRASH_LOG_LIMIT (4u << 20)
#define CRASH_TAIL_SIZE (256u << 10)
#define CRASH_TEXT_SIZE (1u << 20)
#define CRASH_MAX_THREADS 64
#define CRASH_MAX_FRAMES 48
#define CRASH_PATH 1024
/* User-space addresses fit in 47 bits; the bits above can hold a pointer
 * signature on Apple CPUs. */
#define CRASH_PC_MASK ((uintptr_t)0x00007FFFFFFFFFFFull)

static struct {
    char dir[CRASH_PATH];
    char exe[CRASH_PATH];
    const void *exe_base;
    int log;
    unsigned log_size;
    int pipes[2];
    int console[2];
    int busy;
    pthread_mutex_t lock;
    char tail[CRASH_TAIL_SIZE];
    size_t tail_pos;
    int tail_wrapped;
    int interactive;
    long game_thread;
    struct timespec started;
    char system[4096];
    void (*describe)(char *out, size_t size);
    void (*describe_mods)(char *out, size_t size, int on_game_thread);
    void (*describe_save)(char *out, size_t size);
    int crashing;
} crash = {.log = -1, .pipes = {-1, -1}, .console = {-1, -1}, .lock = PTHREAD_MUTEX_INITIALIZER};

/* The signal handler writes a minimal report first, then wakes a prestarted
 * reporter. It never formats text, flushes stdio, allocates or takes a lock.
 * A blocked reporter cannot strand the process: the emergency file survives. */
static struct {
    int pipe[2], ack[2], emergency;
    char path[CRASH_PATH + 96], temporary[CRASH_PATH + 100];
    siginfo_t *info;
    void *context;
    long thread;
    int signal, ready, progress;
} handoff = {.pipe={-1,-1}, .ack={-1,-1}, .emergency=-1};

static char crash_text[CRASH_TEXT_SIZE];
static size_t crash_text_size;

static long Crash_ThreadId(void) {
#ifdef __APPLE__
    uint64_t id=0;pthread_threadid_np(NULL,&id);return (long)id;
#else
    return (long)syscall(SYS_gettid);
#endif
}

static void Crash_Sleep(int ms) {
    struct timespec time = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&time, NULL);
}

static void Crash_Write(int fd, const void *data, size_t size) {
    const char *at = data;

    while (size) {
        ssize_t written = write(fd, at, size);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            return;
        }
        at += written;
        size -= (size_t)written;
    }
}

static const char *Crash_BaseName(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* Reads memory that may not be there, without faulting. */
static int Crash_Read(uintptr_t address, void *out, size_t size) {
#ifdef __APPLE__
    vm_size_t read = 0;
    return vm_read_overwrite(mach_task_self(), (vm_address_t)address, size, (vm_address_t)(uintptr_t)out, &read) ==
               KERN_SUCCESS && read == size;
#else
    struct iovec local = {out, size}, remote = {(void *)address, size};
    return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == (ssize_t)size;
#endif
}

/* ---- the log ---- */

/* A full log starts over with its last 256 KB, so a crash late in a long
 * session still has what led up to it. */
static void Crash_RestartLog(void) {
    static const char note[] = "[log reached 4 MB and started over; the 256 KB before that follow]\n";

    if (ftruncate(crash.log, 0) || lseek(crash.log, 0, SEEK_SET)) {
        return;
    }
    Crash_Write(crash.log, note, sizeof(note) - 1);
    crash.log_size = sizeof(note) - 1;
    if (crash.tail_wrapped) {
        Crash_Write(crash.log, crash.tail + crash.tail_pos, CRASH_TAIL_SIZE - crash.tail_pos);
        crash.log_size += (unsigned)(CRASH_TAIL_SIZE - crash.tail_pos);
    }
    Crash_Write(crash.log, crash.tail, crash.tail_pos);
    crash.log_size += (unsigned)crash.tail_pos;
}

static void Crash_Keep(const char *data, size_t size) {
    pthread_mutex_lock(&crash.lock);
    if (crash.log >= 0) {
        if (crash.log_size + size > CRASH_LOG_LIMIT) {
            Crash_RestartLog();
        }
        Crash_Write(crash.log, data, size);
        crash.log_size += (unsigned)size;
    }
    while (size) {
        size_t part = CRASH_TAIL_SIZE - crash.tail_pos;
        if (part > size) {
            part = size;
        }
        memcpy(crash.tail + crash.tail_pos, data, part);
        crash.tail_pos += part;
        if (crash.tail_pos == CRASH_TAIL_SIZE) {
            crash.tail_pos = 0;
            crash.tail_wrapped = 1;
        }
        data += part;
        size -= part;
    }
    pthread_mutex_unlock(&crash.lock);
}

static void *Crash_Forward(void *param) {
    int stream = (int)(intptr_t)param;
    char buffer[4096];

#ifdef __APPLE__
    pthread_setname_np("log copy");
#else
    pthread_setname_np(pthread_self(), "log copy");
#endif
    for (;;) {
        ssize_t size = read(crash.pipes[stream], buffer, sizeof(buffer));
        if (size < 0 && errno == EINTR) {
            continue;
        }
        if (size <= 0) {
            break;
        }
        __atomic_add_fetch(&crash.busy, 1, __ATOMIC_SEQ_CST);
        if (crash.console[stream] >= 0) {
            Crash_Write(crash.console[stream], buffer, (size_t)size);
        }
        Crash_Keep(buffer, (size_t)size);
        __atomic_sub_fetch(&crash.busy, 1, __ATOMIC_SEQ_CST);
    }
    return NULL;
}

static int Crash_Pending(void) {
    for (int i = 0; i < 2; ++i) {
        int waiting = 0;
        if (crash.pipes[i] >= 0 && !ioctl(crash.pipes[i], FIONREAD, &waiting) && waiting > 0) {
            return 1;
        }
    }
    return __atomic_load_n(&crash.busy, __ATOMIC_SEQ_CST) != 0;
}

/* Waits until the threads have passed on everything printed so far. */
static void Crash_Drain(int timeout_ms) {
    if (crash.pipes[0] < 0 && crash.pipes[1] < 0) {
        return;
    }
    Crash_Sleep(5);
    for (int waited = 5; waited < timeout_ms && Crash_Pending(); waited += 5) {
        Crash_Sleep(5);
    }
}

/* A normal quit leaves no file behind: the log is only kept inside a crash
 * report. (A crash after this still gets the last 256 KB in its report.) */
static void Crash_Finish(void) {
    char path[CRASH_PATH + 32];

    fflush(stdout);
    fflush(stderr);
    Crash_Drain(500);
    pthread_mutex_lock(&crash.lock);
    if (crash.log >= 0 && !__atomic_load_n(&crash.crashing, __ATOMIC_SEQ_CST)) {
        close(crash.log);
        crash.log = -1;
        snprintf(path, sizeof(path), "%s/parasite-eve-log.txt", crash.dir);
        unlink(path);
    }
    pthread_mutex_unlock(&crash.lock);
    if(!__atomic_load_n(&crash.crashing,__ATOMIC_SEQ_CST) && handoff.emergency>=0) {
        close(handoff.emergency);handoff.emergency=-1;unlink(handoff.path);
    }
}

/* Sends stdout (0) or stderr (1) through a pipe, so it can be copied to both
 * the terminal and the log. */
static int Crash_Redirect(int stream) {
    int fd = stream + 1;
    FILE *file = stream ? stderr : stdout;
    int ends[2];
    pthread_t thread;

    fflush(file);
    crash.console[stream] = fcntl(fd, F_DUPFD_CLOEXEC, 3);
    if (pipe(ends)) {
        return 0;
    }
    fcntl(ends[0], F_SETFD, FD_CLOEXEC);
    crash.pipes[stream] = ends[0];
    if (pthread_create(&thread, NULL, Crash_Forward, (void *)(intptr_t)stream)) {
        close(ends[0]);
        close(ends[1]);
        crash.pipes[stream] = -1;
        return 0;
    }
    pthread_detach(thread);
    if (dup2(ends[1], fd) < 0) {
        close(ends[1]);
        return 0;
    }
    close(ends[1]);
    setvbuf(file, NULL, _IONBF, 0);
    return 1;
}

static void Crash_MakeFolders(const char *path) {
    char part[CRASH_PATH];

    snprintf(part, sizeof(part), "%s", path);
    for (char *slash = strchr(part + 1, '/'); slash; slash = strchr(slash + 1, '/')) {
        *slash = 0;
        mkdir(part, 0755);
        *slash = '/';
    }
    mkdir(part, 0755);
}

static int Crash_TryFolder(const char *base) {
    char probe[CRASH_PATH + 16];

    if ((size_t)snprintf(crash.dir, sizeof(crash.dir), "%s/logs", base) >= sizeof(crash.dir) ||
        (mkdir(crash.dir, 0755) && errno != EEXIST)) {
        return 0;
    }
    snprintf(probe, sizeof(probe), "%s/.write-test", crash.dir);
    int fd = open(probe, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        return 0;
    }
    close(fd);
    unlink(probe);
    return 1;
}

static int Crash_FindFolder(void) {
    char base[CRASH_PATH];
    const char *home = getenv("HOME");
    char *slash;

    snprintf(base, sizeof(base), "%s", crash.exe);
    slash = strrchr(base, '/');
    if (slash) {
        *slash = 0;
#ifdef __APPLE__
        if (!strstr(base, ".app/") && Crash_TryFolder(base)) {
#else
        if (Crash_TryFolder(base)) {
#endif
            return 1;
        }
    }
    base[0] = 0;
#ifdef __APPLE__
    if (home && home[0] == '/') {
        snprintf(base, sizeof(base), "%s/Library/Logs/Parasite Eve", home);
    }
#else
    const char *state = getenv("XDG_STATE_HOME");
    if (state && state[0] == '/') {
        snprintf(base, sizeof(base), "%s/parasite-eve", state);
    } else if (home && home[0] == '/') {
        snprintf(base, sizeof(base), "%s/.local/state/parasite-eve", home);
    }
#endif
    if (base[0]) {
        Crash_MakeFolders(base);
        if (Crash_TryFolder(base)) {
            return 1;
        }
    }
    crash.dir[0] = 0;
    return 0;
}

static void Crash_Now(struct tm *local) {
    time_t now = time(NULL);
    localtime_r(&now, local);
}

static int Crash_OpenLog(void) {
    char path[CRASH_PATH + 32], header[160];
    struct tm now;

    snprintf(path, sizeof(path), "%s/parasite-eve-log.txt", crash.dir);
    crash.log = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (crash.log < 0) {
        return 0;
    }
    Crash_Now(&now);
    int size = snprintf(header, sizeof(header), "Parasite Eve %s log, started %04d-%02d-%02d %02d:%02d:%02d\n",
                        PE_VERSION, now.tm_year + 1900, now.tm_mon + 1, now.tm_mday, now.tm_hour, now.tm_min,
                        now.tm_sec);
    Crash_Keep(header, (size_t)size);
    Crash_Keep(crash.system, strlen(crash.system));
    Crash_Keep("\n", 1);
    return 1;
}

/* ---- report text ---- */

static void Crash_Append(const char *format, ...) {
    va_list args;

    if (crash_text_size >= sizeof(crash_text) - 1) {
        return;
    }
    va_start(args, format);
    int size = vsnprintf(crash_text + crash_text_size, sizeof(crash_text) - crash_text_size, format, args);
    va_end(args);
    if (size > 0) {
        crash_text_size += (size_t)size;
        if (crash_text_size > sizeof(crash_text) - 1) {
            crash_text_size = sizeof(crash_text) - 1;
        }
    }
}

/* The game's and the mods' sections end lines with \r\n, for Windows. */
static void Crash_AppendSection(const char *text) {
    for (; *text && crash_text_size < sizeof(crash_text) - 1; ++text) {
        if (*text != '\r') {
            crash_text[crash_text_size++] = *text;
        }
    }
    crash_text[crash_text_size] = 0;
}

static size_t Crash_ReadFile(const char *path, char *out, size_t size) {
    size_t used = 0;
    int fd = open(path, O_RDONLY | O_CLOEXEC);

    if (fd >= 0) {
        while (used < size - 1) {
            ssize_t got = read(fd, out + used, size - 1 - used);
            if (got < 0 && errno == EINTR) {
                continue;
            }
            if (got <= 0) {
                break;
            }
            used += (size_t)got;
        }
        close(fd);
    }
    out[used] = 0;
    return used;
}

/* ---- names for addresses ----
 * The executable keeps a symbol table (functions and globals, static ones
 * too) unless it was stripped. Other libraries get their nearest export. */

static struct {
#ifdef __APPLE__
    const struct nlist_64 *symbols;
#else
    const ElfW(Sym) *symbols;
#endif
    size_t count;
    size_t locals;
    const char *strings;
    size_t strings_size;
    uintptr_t bias;
} crash_names;

#ifdef __APPLE__
static int Crash_UsableSymbol(const struct nlist_64 *symbol) {
    return !(symbol->n_type & N_STAB) && (symbol->n_type & N_TYPE) == N_SECT &&
           symbol->n_un.n_strx < crash_names.strings_size && crash_names.strings[symbol->n_un.n_strx];
}

static void Crash_LoadNames(void) {
    const struct mach_header_64 *header = (const struct mach_header_64 *)_dyld_get_image_header(0);
    const struct symtab_command *symtab = NULL;
    const struct segment_command_64 *linkedit = NULL;

    if (!header || header->magic != MH_MAGIC_64) {
        return;
    }
    const struct load_command *command = (const struct load_command *)(header + 1);
    for (uint32_t i = 0; i < header->ncmds; ++i) {
        if (command->cmd == LC_SYMTAB) {
            symtab = (const struct symtab_command *)command;
        } else if (command->cmd == LC_SEGMENT_64 &&
                   !strncmp(((const struct segment_command_64 *)command)->segname, SEG_LINKEDIT, 16)) {
            linkedit = (const struct segment_command_64 *)command;
        }
        command = (const struct load_command *)((const char *)command + command->cmdsize);
    }
    if (!symtab || !linkedit) {
        return;
    }
    crash_names.bias = (uintptr_t)_dyld_get_image_vmaddr_slide(0);
    const char *base = (const char *)(crash_names.bias + linkedit->vmaddr - linkedit->fileoff);
    crash_names.symbols = (const struct nlist_64 *)(base + symtab->symoff);
    crash_names.count = symtab->nsyms;
    crash_names.strings = base + symtab->stroff;
    crash_names.strings_size = symtab->strsize;
    for (size_t i = 0; i < crash_names.count; ++i) {
        crash_names.locals += Crash_UsableSymbol(&crash_names.symbols[i]) && !(crash_names.symbols[i].n_type & N_EXT);
    }
}
#else
static int Crash_UsableSymbol(const ElfW(Sym) *symbol) {
    int type = symbol->st_info & 0xF;
    return (type == STT_FUNC || type == STT_OBJECT) && symbol->st_shndx != SHN_UNDEF &&
           symbol->st_name < crash_names.strings_size && crash_names.strings[symbol->st_name];
}

static int Crash_FindExe(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size;
    *(uintptr_t *)data = (uintptr_t)info->dlpi_addr;
    return 1;
}

/* The section headers and .symtab are not loaded, so the file is mapped. */
static void Crash_LoadNames(void) {
    struct stat file;
    int fd = open(crash.exe, O_RDONLY | O_CLOEXEC);

    if (fd < 0) {
        return;
    }
    if (fstat(fd, &file) || (size_t)file.st_size < sizeof(ElfW(Ehdr))) {
        close(fd);
        return;
    }
    size_t size = (size_t)file.st_size;
    const char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) {
        return;
    }
    const ElfW(Ehdr) *header = (const ElfW(Ehdr) *)map;
    if (memcmp(header->e_ident, ELFMAG, SELFMAG) ||
        header->e_ident[EI_CLASS] != (sizeof(void *) == 8 ? ELFCLASS64 : ELFCLASS32) || !header->e_shoff ||
        header->e_shentsize != sizeof(ElfW(Shdr)) || header->e_shoff + (size_t)header->e_shnum * sizeof(ElfW(Shdr)) > size) {
        munmap((void *)map, size);
        return;
    }
    const ElfW(Shdr) *sections = (const ElfW(Shdr) *)(map + header->e_shoff);
    for (unsigned i = 0; i < header->e_shnum; ++i) {
        const ElfW(Shdr) *strings = &sections[sections[i].sh_link < header->e_shnum ? sections[i].sh_link : 0];
        if (sections[i].sh_type == SHT_SYMTAB && sections[i].sh_offset + sections[i].sh_size <= size &&
            strings->sh_offset + strings->sh_size <= size) {
            crash_names.symbols = (const ElfW(Sym) *)(map + sections[i].sh_offset);
            crash_names.count = sections[i].sh_size / sizeof(ElfW(Sym));
            crash_names.strings = map + strings->sh_offset;
            crash_names.strings_size = strings->sh_size;
        }
    }
    if (!crash_names.symbols) {
        munmap((void *)map, size);
        return;
    }
    dl_iterate_phdr(Crash_FindExe, &crash_names.bias);
    for (size_t i = 0; i < crash_names.count; ++i) {
        crash_names.locals += Crash_UsableSymbol(&crash_names.symbols[i]) &&
                              (crash_names.symbols[i].st_info >> 4) == STB_LOCAL;
    }
}
#endif

/* Nearest symbol at or before address in the executable. */
static int Crash_ExeName(uintptr_t address, char *name, size_t size, uintptr_t *offset) {
    uintptr_t best = 0;
    const char *found = NULL;

    for (size_t i = 0; i < crash_names.count; ++i) {
        if (!Crash_UsableSymbol(&crash_names.symbols[i])) {
            continue;
        }
#ifdef __APPLE__
        uintptr_t start = crash_names.bias + crash_names.symbols[i].n_value;
        const char *text = crash_names.strings + crash_names.symbols[i].n_un.n_strx;
#else
        uintptr_t start = crash_names.bias + crash_names.symbols[i].st_value;
        const char *text = crash_names.strings + crash_names.symbols[i].st_name;
#endif
        if (start <= address && start >= best) {
            best = start;
            found = text;
        }
    }
    if (!found) {
        return 0;
    }
#ifdef __APPLE__
    if (found[0] == '_') {
        ++found;
    }
#endif
    snprintf(name, size, "%.*s", 160, found);
    *offset = address - best;
    return 1;
}

/* "Function+0x12  (program+0x1234)" or "library+0x1234 (near export
 * Export+0x12)"; nothing when the address is in no executable or library. */
static int Crash_AppendAddress(uintptr_t address) {
    Dl_info info;
    char name[192];
    uintptr_t offset;

    if (!dladdr((const void *)address, &info) || !info.dli_fbase) {
        return 0;
    }
    unsigned long rva = (unsigned long)(address - (uintptr_t)info.dli_fbase);
    if (info.dli_fbase == crash.exe_base) {
        const char *module = Crash_BaseName(crash.exe);
        if (Crash_ExeName(address, name, sizeof(name), &offset)) {
            Crash_Append("%s+0x%lX  (%s+0x%lX)", name, (unsigned long)offset, module, rva);
        } else {
            Crash_Append("%s+0x%lX", module, rva);
        }
        return 1;
    }
    const char *module = Crash_BaseName(info.dli_fname ? info.dli_fname : "?");
    if (info.dli_sname && info.dli_saddr && address - (uintptr_t)info.dli_saddr < 0x4000) {
        Crash_Append("%s+0x%lX (near export %s+0x%lX)", module, rva, info.dli_sname,
                     (unsigned long)(address - (uintptr_t)info.dli_saddr));
    } else {
        Crash_Append("%s+0x%lX", module, rva);
    }
    return 1;
}

/* ---- registers ---- */

typedef struct {
    uintptr_t pc, sp, fp, lr;
} CrashRegs;

static CrashRegs Crash_Regs(const void *context) {
    CrashRegs regs = {0, 0, 0, 0};
    const ucontext_t *uc = context;

    if (!uc) {
        return regs;
    }
#if defined(__APPLE__) && defined(__x86_64__)
    regs.pc = uc->uc_mcontext->__ss.__rip;
    regs.sp = uc->uc_mcontext->__ss.__rsp;
    regs.fp = uc->uc_mcontext->__ss.__rbp;
#elif defined(__APPLE__) && defined(__arm64__)
    regs.pc = (uintptr_t)arm_thread_state64_get_pc(uc->uc_mcontext->__ss) & CRASH_PC_MASK;
    regs.sp = (uintptr_t)arm_thread_state64_get_sp(uc->uc_mcontext->__ss);
    regs.fp = (uintptr_t)arm_thread_state64_get_fp(uc->uc_mcontext->__ss);
    regs.lr = (uintptr_t)arm_thread_state64_get_lr(uc->uc_mcontext->__ss) & CRASH_PC_MASK;
#elif defined(__linux__) && defined(__x86_64__)
    regs.pc = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
    regs.sp = (uintptr_t)uc->uc_mcontext.gregs[REG_RSP];
    regs.fp = (uintptr_t)uc->uc_mcontext.gregs[REG_RBP];
#elif defined(__linux__) && defined(__aarch64__)
    regs.pc = (uintptr_t)uc->uc_mcontext.pc;
    regs.sp = (uintptr_t)uc->uc_mcontext.sp;
    regs.fp = (uintptr_t)uc->uc_mcontext.regs[29];
    regs.lr = (uintptr_t)uc->uc_mcontext.regs[30];
#endif
    return regs;
}

/* "reading", "writing" or "running code at", or NULL when the CPU does not
 * say. */
static const char *Crash_AccessKind(const void *context) {
    const ucontext_t *uc = context;

    if (!uc) {
        return NULL;
    }
#if defined(__x86_64__) && (defined(__APPLE__) || defined(__linux__))
#ifdef __APPLE__
    unsigned long error = uc->uc_mcontext->__es.__err;
#else
    unsigned long error = (unsigned long)uc->uc_mcontext.gregs[REG_ERR];
#endif
    return error & 16 ? "running code at" : error & 2 ? "writing" : "reading";
#elif defined(__APPLE__) && defined(__arm64__)
    uint32_t esr = uc->uc_mcontext->__es.__esr, kind = esr >> 26;
    if (kind == 0x20 || kind == 0x21) {
        return "running code at";
    }
    if (kind == 0x24 || kind == 0x25) {
        return esr & (1u << 6) ? "writing" : "reading";
    }
    return NULL;
#else
    return NULL;
#endif
}

/* ---- call stacks ---- */

typedef struct {
    long id;
    char name[64];
    int crashed;
    int frame_count;
    uintptr_t frames[CRASH_MAX_FRAMES];
} CrashThread;

static CrashThread crash_threads[CRASH_MAX_THREADS];
static int crash_thread_count;

#ifdef __APPLE__
/* Apple code always keeps frame pointers: each frame starts with the
 * caller's frame pointer and the return address. A function that keeps no
 * frame of its own still has its return address in lr. */
static int Crash_Walk(CrashRegs regs, uintptr_t *frames, int max) {
    uintptr_t fp = regs.fp, record[2];
    int count = 0;

    if (regs.pc) {
        frames[count++] = regs.pc;
    }
    if (regs.lr && count < max && !(Crash_Read(fp, record, sizeof(record)) && (record[1] & CRASH_PC_MASK) == regs.lr)) {
        frames[count++] = regs.lr;
    }
    while (count < max && fp && Crash_Read(fp, record, sizeof(record))) {
        uintptr_t back = record[1] & CRASH_PC_MASK;
        if (!back) {
            break;
        }
        frames[count++] = back;
        if (record[0] <= fp) {
            break;
        }
        fp = record[0];
    }
    return count;
}

/* Every other thread is stopped only while its registers are read. */
static void Crash_TakeOtherStacks(void) {
    thread_act_array_t list;
    mach_msg_type_number_t list_count = 0;
    mach_port_t self = mach_thread_self();

    if (task_threads(mach_task_self(), &list, &list_count) != KERN_SUCCESS) {
        return;
    }
    for (mach_msg_type_number_t i = 0; i < list_count; ++i) {
        if (list[i] != self && crash_thread_count < CRASH_MAX_THREADS) {
            CrashThread *thread = &crash_threads[crash_thread_count++];
            thread_identifier_info_data_t identity;
            mach_msg_type_number_t size = THREAD_IDENTIFIER_INFO_COUNT;
            CrashRegs regs = {0, 0, 0, 0};
            pthread_t handle = pthread_from_mach_thread_np(list[i]);

            memset(thread, 0, sizeof(*thread));
            if (thread_info(list[i], THREAD_IDENTIFIER_INFO, (thread_info_t)&identity, &size) == KERN_SUCCESS) {
                thread->id = (long)identity.thread_id;
            }
            if (thread->id == handoff.thread) {
                --crash_thread_count;
                mach_port_deallocate(mach_task_self(), list[i]);
                continue;
            }
            if (handle) {
                pthread_getname_np(handle, thread->name, sizeof(thread->name));
            }
            if (thread_suspend(list[i]) == KERN_SUCCESS) {
#if defined(__x86_64__)
                x86_thread_state64_t state;
                size = x86_THREAD_STATE64_COUNT;
                if (thread_get_state(list[i], x86_THREAD_STATE64, (thread_state_t)&state, &size) == KERN_SUCCESS) {
                    regs.pc = state.__rip;
                    regs.fp = state.__rbp;
                }
#elif defined(__arm64__)
                arm_thread_state64_t state;
                size = ARM_THREAD_STATE64_COUNT;
                if (thread_get_state(list[i], ARM_THREAD_STATE64, (thread_state_t)&state, &size) == KERN_SUCCESS) {
                    regs.pc = (uintptr_t)arm_thread_state64_get_pc(state) & CRASH_PC_MASK;
                    regs.fp = (uintptr_t)arm_thread_state64_get_fp(state);
                    regs.lr = (uintptr_t)arm_thread_state64_get_lr(state) & CRASH_PC_MASK;
                }
#endif
                if (regs.pc) {
                    thread->frame_count = Crash_Walk(regs, thread->frames, CRASH_MAX_FRAMES);
                }
                thread_resume(list[i]);
            }
        }
        mach_port_deallocate(mach_task_self(), list[i]);
    }
    mach_port_deallocate(mach_task_self(), self);
    vm_deallocate(mach_task_self(), (vm_address_t)list, list_count * sizeof(list[0]));
}
#else
static int Crash_Walk(CrashRegs regs, uintptr_t *frames, int max) {
    uintptr_t fp=regs.fp, record[2];int count=0;
    if(regs.pc)frames[count++]=regs.pc;
    while(count<max && fp && Crash_Read(fp,record,sizeof(record))) {
        if(!record[1])break;
        frames[count++]=record[1];
        if(record[0]<=fp || record[0]-fp>(16u<<20))break;
        fp=record[0];
    }
    return count;
}

/* Other threads unwind themselves: each in turn gets a signal and fills in
 * its frames. */
static struct {
    long id;
    CrashThread *thread;
    int done;
} crash_request;

static void Crash_StackSignal(int number, siginfo_t *info, void *context) {
    int saved = errno;

    (void)number;
    (void)info;
    if (__atomic_load_n(&crash_request.id, __ATOMIC_SEQ_CST) == Crash_ThreadId()) {
        CrashThread *thread = crash_request.thread;
        thread->frame_count = Crash_Walk(Crash_Regs(context), thread->frames, CRASH_MAX_FRAMES);
        __atomic_store_n(&crash_request.done, 1, __ATOMIC_SEQ_CST);
    }
    errno = saved;
}

static void Crash_TakeOtherStacks(void) {
    struct sigaction action;
    long self = Crash_ThreadId();
    DIR *tasks = opendir("/proc/self/task");
    struct dirent *entry;

    if (!tasks) {
        return;
    }
    memset(&action, 0, sizeof(action));
    action.sa_sigaction = Crash_StackSignal;
    action.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&action.sa_mask);
    sigaction(SIGRTMIN + 3, &action, NULL);
    while ((entry = readdir(tasks)) && crash_thread_count < CRASH_MAX_THREADS) {
        long id = strtol(entry->d_name, NULL, 10);
        char path[64];
        if (id <= 0 || id == self || id == handoff.thread) {
            continue;
        }
        CrashThread *thread = &crash_threads[crash_thread_count++];
        memset(thread, 0, sizeof(*thread));
        thread->id = id;
        snprintf(path, sizeof(path), "/proc/self/task/%ld/comm", id);
        Crash_ReadFile(path, thread->name, sizeof(thread->name));
        thread->name[strcspn(thread->name, "\n")] = 0;

        crash_request.thread = thread;
        __atomic_store_n(&crash_request.done, 0, __ATOMIC_SEQ_CST);
        __atomic_store_n(&crash_request.id, id, __ATOMIC_SEQ_CST);
        if (!syscall(SYS_tgkill, getpid(), (pid_t)id, SIGRTMIN + 3)) {
            /* A thread that blocks the signal leaves its stack out. */
            for (int waited = 0; waited < 200 && !__atomic_load_n(&crash_request.done, __ATOMIC_SEQ_CST); ++waited) {
                Crash_Sleep(1);
            }
        }
        __atomic_store_n(&crash_request.id, 0, __ATOMIC_SEQ_CST);
    }
    closedir(tasks);
}
#endif

/* The crashed thread's frames come from the signal's registers; every other
 * thread's are taken next. Names and the text come after. */
static void Crash_TakeStacks(const void *context) {
    CrashThread *thread = &crash_threads[0];

    crash_thread_count = 1;
    memset(thread, 0, sizeof(*thread));
    thread->id = handoff.thread;
    thread->crashed = 1;
    snprintf(thread->name,sizeof(thread->name),"%s",handoff.thread==crash.game_thread?"game":"faulting thread");
    thread->frame_count = Crash_Walk(Crash_Regs(context), thread->frames, CRASH_MAX_FRAMES);
    Crash_TakeOtherStacks();
}

static void Crash_AppendStacks(void) {
    Crash_Append("\n== Call stacks (newest call first) ==\n");
    for (int i = 0; i < crash_thread_count; ++i) {
        const CrashThread *thread = &crash_threads[i];

        Crash_Append("\nThread %ld%s%s%s%s%s:\n", thread->id, thread->name[0] ? " \"" : "", thread->name,
                     thread->name[0] ? "\"" : "", thread->id == crash.game_thread ? " (game thread)" : "",
                     thread->crashed ? " - CRASHED HERE" : "");
        if (!thread->frame_count) {
            Crash_Append("  (stack could not be read)\n");
        }
        for (int frame = 0; frame < thread->frame_count; ++frame) {
            Crash_Append("  %2d  ", frame);
            if (!Crash_AppendAddress(thread->frames[frame])) {
                Crash_Append("0x%llX (not in any module)", (unsigned long long)thread->frames[frame]);
            }
            Crash_Append("\n");
        }
    }
}

/* ---- the computer ---- */

static size_t Crash_SystemAppend(size_t used, const char *format, ...) {
    va_list args;

    if (used >= sizeof(crash.system)) {
        return used;
    }
    va_start(args, format);
    int size = vsnprintf(crash.system + used, sizeof(crash.system) - used, format, args);
    va_end(args);
    return size > 0 ? used + (size_t)size : used;
}

#ifdef __APPLE__
static void Crash_Sysctl(const char *name, char *out, size_t size) {
    size_t length = size - 1;

    if (sysctlbyname(name, out, &length, NULL, 0)) {
        length = 0;
    }
    out[length < size ? length : size - 1] = 0;
}

static uint64_t Crash_SysctlNumber(const char *name) {
    uint64_t value = 0;
    size_t length = sizeof(value);

    if (sysctlbyname(name, &value, &length, NULL, 0)) {
        return 0;
    }
    return length == 4 ? (uint32_t)value : value;
}

static double Crash_FreeMemory(void) {
    vm_statistics64_data_t statistics;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;

    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&statistics, &count) != KERN_SUCCESS) {
        return 0;
    }
    return (double)statistics.free_count * (double)vm_page_size;
}
#else
/* The value after "key" in "key: value" or key="value" text. */
static int Crash_Field(const char *text, const char *key, char *out, size_t size) {
    size_t length = strlen(key);

    out[0] = 0;
    for (const char *line = text; line && *line; line = strchr(line, '\n'), line = line ? line + 1 : NULL) {
        if (strncmp(line, key, length) || (line[length] != ':' && line[length] != '=' && line[length] != ' ' &&
                                           line[length] != '\t')) {
            continue;
        }
        line += length;
        while (*line == ' ' || *line == '\t' || *line == ':' || *line == '=' || *line == '"') {
            ++line;
        }
        size_t end = strcspn(line, "\"\n");
        snprintf(out, size, "%.*s", (int)(end < size ? end : size - 1), line);
        return 1;
    }
    return 0;
}

/* kB value from /proc/meminfo or /proc/self/status, in bytes. */
static double Crash_Kilobytes(const char *text, const char *key) {
    char value[64];
    return Crash_Field(text, key, value, sizeof(value)) ? strtod(value, NULL) * 1024.0 : 0;
}

static const char *Crash_Vendor(unsigned id) {
    switch (id) {
    case 0x1002: return "AMD";
    case 0x10DE: return "NVIDIA";
    case 0x8086: return "Intel";
    default: return "";
    }
}

static int Crash_FindBuildId(struct dl_phdr_info *info, size_t size, void *data) {
    char *out = data;

    (void)size;
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr) *header = &info->dlpi_phdr[i];
        const unsigned char *note = (const unsigned char *)(info->dlpi_addr + header->p_vaddr);
        const unsigned char *end = note + header->p_memsz;
        while (header->p_type == PT_NOTE && note + sizeof(ElfW(Nhdr)) <= end) {
            const ElfW(Nhdr) *entry = (const ElfW(Nhdr) *)note;
            const unsigned char *name = note + sizeof(*entry);
            const unsigned char *desc = name + ((entry->n_namesz + 3) & ~3u);
            if (entry->n_type == NT_GNU_BUILD_ID && entry->n_namesz == 4 && !memcmp(name, "GNU", 4)) {
                for (unsigned at = 0; at < entry->n_descsz && at < 32; ++at) {
                    snprintf(out + at * 2, 3, "%02x", desc[at]);
                }
                return 1;
            }
            note = desc + ((entry->n_descsz + 3) & ~3u);
        }
    }
    return 1; /* only the executable */
}
#endif

/* The executable's identity, so a report can be matched to its build. */
static size_t Crash_DescribeExecutable(size_t used) {
    struct stat file;
    char id[72] = "none";

#ifdef __APPLE__
    const struct mach_header_64 *header = (const struct mach_header_64 *)_dyld_get_image_header(0);
    const struct load_command *command = header ? (const struct load_command *)(header + 1) : NULL;
    for (uint32_t i = 0; command && i < header->ncmds; ++i) {
        if (command->cmd == LC_UUID) {
            const uint8_t *u = ((const struct uuid_command *)command)->uuid;
            snprintf(id, sizeof(id), "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X", u[0],
                     u[1], u[2], u[3], u[4], u[5], u[6], u[7], u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
        }
        command = (const struct load_command *)((const char *)command + command->cmdsize);
    }
#else
    id[0] = 0;
    dl_iterate_phdr(Crash_FindBuildId, id);
    if (!id[0]) {
        snprintf(id, sizeof(id), "none");
    }
#endif
    unsigned long long size = stat(crash.exe, &file) ? 0 : (unsigned long long)file.st_size;
    used = Crash_SystemAppend(used, "Executable: %s, version %s, %llu bytes, build id %s\n", Crash_BaseName(crash.exe),
                              PE_VERSION, size, id);
    if (crash_names.locals) {
        used = Crash_SystemAppend(used, "Function names: in the executable (%lu symbols)\n",
                                  (unsigned long)crash_names.count);
    } else {
        used = Crash_SystemAppend(used, "Function names: NONE, this build was stripped; crash reports only "
                                        "have addresses and need this exact build to be read\n");
    }
    return used;
}

static void Crash_DescribeSystem(void) {
    struct utsname name;
    char text[512], extra[256];
    size_t used = 0;

    if (uname(&name)) {
        memset(&name, 0, sizeof(name));
    }
#ifdef __APPLE__
    Crash_Sysctl("kern.osproductversion", text, sizeof(text));
    Crash_Sysctl("kern.osversion", extra, sizeof(extra));
    used = Crash_SystemAppend(used, "%s: %s (%s), kernel %s %s%s\n", TARGET_OS_IPHONE ? "iOS" : "macOS",
                              text[0] ? text : "?", extra, name.release, name.machine,
                              Crash_SysctlNumber("sysctl.proc_translated") == 1 ? ", running under Rosetta" : "");
    Crash_Sysctl("hw.model", text, sizeof(text));
    used = Crash_SystemAppend(used, "Model: %s\n", text[0] ? text : "?");
    Crash_Sysctl("machdep.cpu.brand_string", text, sizeof(text));
    used = Crash_SystemAppend(used, "CPU: %s, %lu logical processors\n", text[0] ? text : "?",
                              (unsigned long)Crash_SysctlNumber("hw.logicalcpu"));
    used = Crash_SystemAppend(used, "Memory: %.1f GB, %.1f GB free at start\n",
                              (double)Crash_SysctlNumber("hw.memsize") / 1073741824.0, Crash_FreeMemory() / 1073741824.0);
#else
    static char file[64u << 10];
    const char *desktop = getenv("XDG_CURRENT_DESKTOP"), *session = getenv("XDG_SESSION_TYPE");

    Crash_ReadFile("/etc/os-release", file, sizeof(file));
    if (!Crash_Field(file, "PRETTY_NAME", text, sizeof(text))) {
        snprintf(text, sizeof(text), "?");
    }
    used = Crash_SystemAppend(used, "Linux: %s, kernel %s %s\n", text, name.release, name.machine);
    used = Crash_SystemAppend(used, "Desktop: %s, %s session\n", desktop ? desktop : "?", session ? session : "?");
    Crash_ReadFile("/proc/cpuinfo", file, sizeof(file));
    if (!Crash_Field(file, "model name", text, sizeof(text))) {
        snprintf(text, sizeof(text), "?");
    }
    used = Crash_SystemAppend(used, "CPU: %s, %ld logical processors\n", text, sysconf(_SC_NPROCESSORS_ONLN));
    Crash_ReadFile("/proc/meminfo", file, sizeof(file));
    used = Crash_SystemAppend(used, "Memory: %.1f GB, %.1f GB free at start\n",
                              Crash_Kilobytes(file, "MemTotal") / 1073741824.0,
                              Crash_Kilobytes(file, "MemAvailable") / 1073741824.0);

    for (int card = 0; card < 8; ++card) {
        char path[128], driver[256];
        snprintf(path, sizeof(path), "/sys/class/drm/card%d/device/vendor", card);
        if (!Crash_ReadFile(path, text, sizeof(text))) {
            continue;
        }
        unsigned vendor = (unsigned)strtoul(text, NULL, 16);
        snprintf(path, sizeof(path), "/sys/class/drm/card%d/device/device", card);
        Crash_ReadFile(path, extra, sizeof(extra));
        snprintf(path, sizeof(path), "/sys/class/drm/card%d/device/driver", card);
        ssize_t length = readlink(path, driver, sizeof(driver) - 1);
        driver[length > 0 ? length : 0] = 0;
        used = Crash_SystemAppend(used, "Graphics: card%d %s, PCI %04x:%04x, driver %s\n", card, Crash_Vendor(vendor),
                                  vendor, (unsigned)strtoul(extra, NULL, 16), driver[0] ? Crash_BaseName(driver) : "?");
    }
    if (Crash_ReadFile("/proc/driver/nvidia/version", text, sizeof(text))) {
        text[strcspn(text, "\n")] = 0;
        used = Crash_SystemAppend(used, "NVIDIA driver: %s\n", text);
    }
#endif
    Crash_DescribeExecutable(used);
}

#ifdef __APPLE__
static void Crash_AppendModules(void) {
    uint32_t count = _dyld_image_count(), system = 0;

    Crash_Append("\n== Loaded libraries (overlays and injected tools show up here) ==\n");
    for (uint32_t i = 0; i < count; ++i) {
        const char *name = _dyld_get_image_name(i);
        if (name && (!strncmp(name, "/System/", 8) || !strncmp(name, "/usr/lib/", 9))) {
            ++system;
            continue;
        }
        Crash_Append("  0x%016llX  %s\n", (unsigned long long)(uintptr_t)_dyld_get_image_header(i), name ? name : "?");
    }
    Crash_Append("  (and %u system libraries)\n", (unsigned)system);
}
#else
static int Crash_AppendModule(struct dl_phdr_info *info, size_t size, void *data) {
    uintptr_t low = UINTPTR_MAX, high = 0;

    (void)size;
    (void)data;
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr) *header = &info->dlpi_phdr[i];
        if (header->p_type == PT_LOAD) {
            uintptr_t start = info->dlpi_addr + header->p_vaddr;
            low = start < low ? start : low;
            high = start + header->p_memsz > high ? start + header->p_memsz : high;
        }
    }
    if (high) {
        Crash_Append("  0x%016llX %8lu KB  %s\n", (unsigned long long)low, (unsigned long)((high - low) >> 10),
                     info->dlpi_name && info->dlpi_name[0] ? info->dlpi_name : crash.exe);
    }
    return 0;
}

static void Crash_AppendModules(void) {
    Crash_Append("\n== Loaded libraries (overlays and injected tools show up here) ==\n");
    dl_iterate_phdr(Crash_AppendModule, NULL);
}
#endif

/* ---- the crash itself ---- */

static const char *Crash_SignalName(int number) {
    switch (number) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS: return "SIGBUS";
    case SIGILL: return "SIGILL";
    case SIGFPE: return "SIGFPE";
    case SIGTRAP: return "SIGTRAP";
    case SIGABRT: return "SIGABRT";
    default: return "signal";
    }
}

static const char *Crash_Problem(int number, int code, int stack_overflow) {
    switch (number) {
    case SIGSEGV:
    case SIGBUS:
        return stack_overflow ? "stack overflow (runaway recursion or a huge local array)"
                              : "access violation (bad memory access)";
    case SIGILL: return "illegal instruction";
    case SIGFPE:
        return code == FPE_INTDIV ? "integer divide by zero"
               : code == FPE_INTOVF ? "integer overflow"
               : code == FPE_FLTDIV ? "floating-point divide by zero"
                                    : "invalid arithmetic";
    case SIGTRAP: return "breakpoint";
    case SIGABRT: return "the game stopped itself (abort; see the last log lines)";
    default: return "unexpected signal";
    }
}

static void Crash_AppendMemoryAt(uintptr_t address) {
    if (address < 0x10000) {
        Crash_Append("nothing there (near address 0: a null pointer)");
        return;
    }
#ifdef __APPLE__
    vm_address_t region = (vm_address_t)address;
    vm_size_t size = 0;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object = MACH_PORT_NULL;

    if (vm_region_64(mach_task_self(), &region, &size, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &count,
                     &object) != KERN_SUCCESS || region > address) {
        Crash_Append("nothing there (free memory)");
        return;
    }
    Crash_Append("in use, %c%c%c", info.protection & VM_PROT_READ ? 'r' : '-',
                 info.protection & VM_PROT_WRITE ? 'w' : '-', info.protection & VM_PROT_EXECUTE ? 'x' : '-');
    size_t before = crash_text_size;
    Crash_Append(", inside ");
    if (!Crash_AppendAddress(address)) {
        crash_text_size = before;
        crash_text[before] = 0;
    }
#else
    static char maps[512u << 10];
    Crash_ReadFile("/proc/self/maps", maps, sizeof(maps));
    for (char *line = maps; *line; line += strcspn(line, "\n") + (line[strcspn(line, "\n")] ? 1 : 0)) {
        char *end;
        uintptr_t low = (uintptr_t)strtoull(line, &end, 16);
        uintptr_t high = *end == '-' ? (uintptr_t)strtoull(end + 1, &end, 16) : 0;
        if (address >= low && address < high) {
            int length = (int)strcspn(line, "\n");
            Crash_Append("in use: %.*s", length, line);
            return;
        }
    }
    Crash_Append("nothing there (free memory)");
#endif
}

static void Crash_AppendBytes(const char *title, uintptr_t address, size_t size) {
    unsigned char bytes[256];

    if (size > sizeof(bytes) || !Crash_Read(address, bytes, size)) {
        Crash_Append("%s: (not readable at 0x%llX)\n", title, (unsigned long long)address);
        return;
    }
    Crash_Append("%s:\n", title);
    for (size_t row = 0; row < size; row += 16) {
        Crash_Append("  %016llX ", (unsigned long long)(address + row));
        for (size_t i = row; i < row + 16 && i < size; ++i) {
            Crash_Append(" %02X", bytes[i]);
        }
        Crash_Append("\n");
    }
}

static void Crash_AppendRegister(const char *name, uintptr_t value) {
    Crash_Append("  %-3s %016llX", name, (unsigned long long)value);
    if (value >= 0x10000) {
        size_t before = crash_text_size;
        Crash_Append("  ");
        if (!Crash_AppendAddress(value)) {
            crash_text_size = before;
            crash_text[before] = 0;
        }
    }
    Crash_Append("\n");
}

static void Crash_AppendDetails(int number, const siginfo_t *info, const void *context) {
    const ucontext_t *uc = context;
    CrashRegs regs = Crash_Regs(context);

    Crash_Append("\n== Details ==\n");
    Crash_Append("Signal: %d (%s), code %d, address 0x%llX, at 0x%llX\n", number, Crash_SignalName(number),
                 info->si_code, (unsigned long long)(uintptr_t)info->si_addr, (unsigned long long)regs.pc);
    if (number == SIGSEGV || number == SIGBUS) {
        Crash_Append("Memory at the bad address: ");
        Crash_AppendMemoryAt((uintptr_t)info->si_addr);
        Crash_Append("\n");
    }
    if (!uc) {
        return;
    }
    Crash_Append("\nRegisters (with what they point to):\n");
#if defined(__APPLE__) && defined(__x86_64__)
    const struct __darwin_x86_thread_state64 *s = &uc->uc_mcontext->__ss;
    const uintptr_t values[] = {s->__rip, s->__rsp, s->__rbp, s->__rax, s->__rbx, s->__rcx, s->__rdx, s->__rsi,
                                s->__rdi, s->__r8,  s->__r9,  s->__r10, s->__r11, s->__r12, s->__r13, s->__r14,
                                s->__r15};
    unsigned long flags = (unsigned long)s->__rflags;
#elif defined(__linux__) && defined(__x86_64__)
    const greg_t *g = uc->uc_mcontext.gregs;
    const uintptr_t values[] = {g[REG_RIP], g[REG_RSP], g[REG_RBP], g[REG_RAX], g[REG_RBX], g[REG_RCX],
                                g[REG_RDX], g[REG_RSI], g[REG_RDI], g[REG_R8],  g[REG_R9],  g[REG_R10],
                                g[REG_R11], g[REG_R12], g[REG_R13], g[REG_R14], g[REG_R15]};
    unsigned long flags = (unsigned long)g[REG_EFL];
#endif
#if defined(__x86_64__)
    static const char *const names[] = {"rip", "rsp", "rbp", "rax", "rbx", "rcx", "rdx", "rsi", "rdi",
                                        "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15"};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        Crash_AppendRegister(names[i], values[i]);
    }
    Crash_Append("  eflags %08lX\n", flags);
#elif defined(__aarch64__) || defined(__arm64__)
    Crash_AppendRegister("pc", regs.pc);
    Crash_AppendRegister("sp", regs.sp);
    Crash_AppendRegister("fp", regs.fp);
    Crash_AppendRegister("lr", regs.lr);
    for (int i = 0; i < 29; ++i) {
        char name[8];
        snprintf(name, sizeof(name), "x%d", i);
#ifdef __APPLE__
        Crash_AppendRegister(name, (uintptr_t)uc->uc_mcontext->__ss.__x[i]);
#else
        Crash_AppendRegister(name, (uintptr_t)uc->uc_mcontext.regs[i]);
#endif
    }
#endif
    Crash_Append("\n");
    Crash_AppendBytes("Code before and at the crash (the crash is at the start of the third row)", regs.pc - 32, 64);
    Crash_AppendBytes("Top of the stack", regs.sp, 256);
}

static void Crash_BuildText(int number, const siginfo_t *info, const void *context) {
    static char section[64u << 10];
    CrashRegs regs = Crash_Regs(context);
    uintptr_t address = (uintptr_t)info->si_addr;
    int memory = number == SIGSEGV || number == SIGBUS;
    /* A bad access right next to the stack pointer is the stack's guard page. */
    int overflow = memory && regs.sp && address >= 0x10000 &&
                   (address > regs.sp ? address - regs.sp : regs.sp - address) < (64u << 10);
    int on_game_thread = handoff.thread == crash.game_thread;
    struct timespec now_mono;
    struct tm now;

    clock_gettime(CLOCK_MONOTONIC, &now_mono);
    unsigned long long seconds = (unsigned long long)(now_mono.tv_sec - crash.started.tv_sec);
    Crash_Now(&now);
    crash_text_size = 0;
    Crash_Append("Parasite Eve %s crash report\n", PE_VERSION);
    Crash_Append("\n== What happened ==\n");
    Crash_Append("Problem: %s (%s)", Crash_Problem(number, info->si_code, overflow), Crash_SignalName(number));
    if (memory) {
        const char *kind = Crash_AccessKind(context);
        Crash_Append(", %s address 0x%llX", kind ? kind : "at", (unsigned long long)address);
    }
    Crash_Append("\nWhere: ");
    if (!Crash_AppendAddress(regs.pc)) {
        Crash_Append("0x%llX (not in any module)", (unsigned long long)regs.pc);
    }
    Crash_Append("\nThread: %ld%s\n", handoff.thread, on_game_thread ? " (game thread)" : " (not the game thread)");
    Crash_Append("Time: %04d-%02d-%02d %02d:%02d:%02d, %llu min %llu s after the game started\n", now.tm_year + 1900,
                 now.tm_mon + 1, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec, seconds / 60, seconds % 60);
    if (crash.describe) {
        section[0] = 0;
        crash.describe(section, sizeof(section));
        Crash_AppendSection(section);
    }

    if (crash.describe_mods) {
        Crash_Append("\n== Mods ==\n");
        section[0] = 0;
        crash.describe_mods(section, sizeof(section), on_game_thread);
        Crash_AppendSection(section);
    }

    Crash_AppendDetails(number, info, context);
    Crash_AppendStacks();

    Crash_Append("\n== Memory when it crashed ==\n");
#ifdef __APPLE__
    task_vm_info_data_t vm;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    Crash_Append("Computer: %.1f GB free of %.1f GB\n", Crash_FreeMemory() / 1073741824.0,
                 (double)Crash_SysctlNumber("hw.memsize") / 1073741824.0);
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vm, &count) == KERN_SUCCESS) {
        Crash_Append("Game: %.0f MB in use (peak %.0f MB), footprint %.0f MB\n", (double)vm.resident_size / 1048576.0,
                     (double)vm.resident_size_peak / 1048576.0, (double)vm.phys_footprint / 1048576.0);
    }
#else
    Crash_ReadFile("/proc/meminfo", section, sizeof(section));
    Crash_Append("Computer: %.1f GB free of %.1f GB\n", Crash_Kilobytes(section, "MemAvailable") / 1073741824.0,
                 Crash_Kilobytes(section, "MemTotal") / 1073741824.0);
    Crash_ReadFile("/proc/self/status", section, sizeof(section));
    Crash_Append("Game: %.0f MB in use (peak %.0f MB)\n", Crash_Kilobytes(section, "VmRSS") / 1048576.0,
                 Crash_Kilobytes(section, "VmHWM") / 1048576.0);
#endif

    Crash_Append("\n== Computer ==\n%s", crash.system);
    Crash_AppendModules();

    if (crash.describe_save) {
        section[0] = 0;
        crash.describe_save(section, sizeof(section));
        Crash_AppendSection(section);
    }
}

/* ---- telling the player ---- */

#if CRASH_DIALOG
/* Runs a program and waits for it: its exit code, -1 when it did not finish
 * normally, -2 when it is not installed. */
static int Crash_Run(char *const argv[]) {
    pid_t child;
    int status;

    if (posix_spawnp(&child, argv[0], NULL, NULL, argv, environ)) {
        return -2;
    }
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            return -1;
        }
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

#ifndef __APPLE__
/* zenity reads its text as markup. */
static void Crash_Markup(const char *text, char *out, size_t size) {
    size_t used = 0;

    for (; *text && used + 6 < size; ++text) {
        const char *escape = *text == '&' ? "&amp;" : *text == '<' ? "&lt;" : *text == '>' ? "&gt;" : NULL;
        if (escape) {
            memcpy(out + used, escape, strlen(escape));
            used += strlen(escape);
        } else {
            out[used++] = *text;
        }
    }
    out[used] = 0;
}
#endif
#endif

static void Crash_Tell(const char *file, unsigned long long size) {
    char path[CRASH_PATH + 64], line[CRASH_PATH + 96];
    int out = crash.console[1] >= 0 ? crash.console[1] : 2;

    snprintf(path, sizeof(path), "%s/%s", crash.dir, file);
    int length = snprintf(line, sizeof(line), "Crash report: %s\n", path);
    if (length > 0) {
        Crash_Write(out, line, (size_t)length < sizeof(line) ? (size_t)length : sizeof(line) - 1);
    }
#if CRASH_DIALOG
    static char text[4096];
    char amount[32];

    if (!crash.interactive) {
        return;
    }
    if (size >= (1u << 20)) {
        snprintf(amount, sizeof(amount), "%.1f MB", (double)size / 1048576.0);
    } else {
        snprintf(amount, sizeof(amount), "%.0f KB", (double)size / 1024.0 + 0.5);
    }
    snprintf(text, sizeof(text),
             "Sorry, Parasite Eve has crashed.\n\n"
             "A crash report was saved. Please send this file to us on Discord:\n\n"
             "    %s  (%s)\n\n"
             "It is in this folder:\n%s\n\n"
             "Thank you for helping us with the game!",
             file, amount, crash.dir);
#ifdef __APPLE__
    char *dialog[] = {"osascript", "-e", "on run argv", "-e",
                      "display dialog (item 1 of argv) with title \"Parasite Eve\" buttons {\"Open crash folder\"} "
                      "default button 1 with icon stop",
                      "-e", "end run", text, NULL};
    char *open_folder[] = {"open", "-R", path, NULL};
    if (Crash_Run(dialog) == 0) {
        Crash_Run(open_folder);
    }
#else
    static char markup[8192];
    char *zenity[] = {"zenity", "--error", "--title=Parasite Eve", "--no-wrap", "--ok-label=Open crash folder",
                      "--text", markup, NULL};
    char *kdialog[] = {"kdialog", "--title", "Parasite Eve", "--error", text, NULL};
    char *xmessage[] = {"xmessage", "-center", "-buttons", "Open crash folder:0", text, NULL};
    char *const *dialogs[] = {zenity, kdialog, xmessage};
    char *open_folder[] = {"xdg-open", crash.dir, NULL};
    int result = -2;

    if (!getenv("DISPLAY") && !getenv("WAYLAND_DISPLAY")) {
        return;
    }
    Crash_Markup(text, markup, sizeof(markup));
    for (int i = 0; i < 3 && result == -2; ++i) {
        result = Crash_Run(dialogs[i]);
    }
    if (result == 0) {
        Crash_Run(open_folder);
    }
#endif
#else
    (void)size;
#endif
}

/* The report, then the whole log: the start of the file, and if the log
 * outgrew its limit, its last 256 KB. */
static void Crash_WriteLog(int out) {
    static char buffer[64u << 10];
    char path[CRASH_PATH + 32];
    int locked = 0;

    snprintf(path, sizeof(path), "%s/parasite-eve-log.txt", crash.dir);
    int log = open(path, O_RDONLY | O_CLOEXEC);
    if (log >= 0) {
        static const char title[] = "\n== Log of this run ==\n";
        ssize_t size;
        Crash_Write(out, title, sizeof(title) - 1);
        while ((size = read(log, buffer, sizeof(buffer))) > 0) {
            Crash_Write(out, buffer, (size_t)size);
        }
        close(log);
    }
    for (int tries = 0; tries < 50 && !(locked = !pthread_mutex_trylock(&crash.lock)); ++tries) {
        Crash_Sleep(2);
    }
    if (log < 0 && (crash.tail_wrapped || crash.tail_pos)) {
        static const char title[] = "\n== End of the log ==\n";
        Crash_Write(out, title, sizeof(title) - 1);
        if (crash.tail_wrapped) {
            Crash_Write(out, crash.tail + crash.tail_pos, CRASH_TAIL_SIZE - crash.tail_pos);
        }
        Crash_Write(out, crash.tail, crash.tail_pos);
    }
    /* The log is in the report now; one file is left for the player. */
    if (crash.log >= 0) {
        close(crash.log);
        crash.log = -1;
        unlink(path);
    }
    if (locked) {
        pthread_mutex_unlock(&crash.lock);
    }
}

static void Crash_Report(int number, siginfo_t *info, void *context) {
    struct stat file;
    /* No stdio flush: the faulting thread may own a libc stream lock. */
    Crash_Drain(300);
    Crash_TakeStacks(context);
    Crash_BuildText(number, info, context);
    int out=open(handoff.temporary,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0644);
    if(out<0)return;
    Crash_Write(out,crash_text,crash_text_size);
    Crash_WriteLog(out);
    unsigned long long size=fstat(out,&file)?0:(unsigned long long)file.st_size;
    int ok=size>=crash_text_size && !fsync(out);
    if(close(out))ok=0;
    if(!ok || rename(handoff.temporary,handoff.path)) {unlink(handoff.temporary);return;}
    __atomic_store_n(&handoff.progress,1,__ATOMIC_RELEASE);
    char written='W';write(handoff.ack[1],&written,1); /* durable; UI may wait for the user */
    Crash_Tell(Crash_BaseName(handoff.path),size);
}

static void *Crash_Reporter(void *unused) {
    (void)unused;char wake;
    while(read(handoff.pipe[0],&wake,1)<0 && errno==EINTR){}
    if(__atomic_load_n(&handoff.ready,__ATOMIC_ACQUIRE))
        Crash_Report(handoff.signal,handoff.info,handoff.context);
    __atomic_store_n(&handoff.progress,2,__ATOMIC_RELEASE);
    char done='D';write(handoff.ack[1],&done,1);
    return NULL;
}

static void Crash_EmergencyField(const char *name,uintptr_t value) {
    char text[96];size_t n=0;
    while(*name && n<sizeof(text)-2*sizeof(value)-4)text[n++]=*name++;
    text[n++]='0';text[n++]='x';
    for(int shift=(int)(sizeof(value)*8)-4;shift>=0;shift-=4)
        text[n++]="0123456789abcdef"[(value>>shift)&15];
    text[n++]='\n';Crash_Write(handoff.emergency,text,n);
}

static void Crash_Signal(int number, siginfo_t *info, void *context) {
    if(__atomic_exchange_n(&crash.crashing,1,__ATOMIC_SEQ_CST))_exit(128+number);
    handoff.signal=number;handoff.info=info;handoff.context=context;handoff.thread=Crash_ThreadId();
    CrashRegs regs=Crash_Regs(context);
    static const char header[]="Parasite Eve emergency crash report\nFull reporting may be blocked by a lock held at the crash.\n";
    Crash_Write(handoff.emergency,header,sizeof(header)-1);
    Crash_EmergencyField("Signal: ",(uintptr_t)number);
    Crash_EmergencyField("Thread: ",(uintptr_t)handoff.thread);
    Crash_EmergencyField("PC: ",regs.pc);Crash_EmergencyField("SP: ",regs.sp);
    Crash_EmergencyField("Fault address: ",(uintptr_t)info->si_addr);
    __atomic_store_n(&handoff.ready,1,__ATOMIC_RELEASE);
    char wake=1;
    if(handoff.pipe[1]>=0 && write(handoff.pipe[1],&wake,1)==1) {
        struct pollfd ack={handoff.ack[0],POLLIN,0};
        int timeout=10000;
        for(;;) {
            int status=poll(&ack,1,timeout);
            if(status<0 && errno==EINTR)continue;
            if(status<=0)break;
            char stage;
            if(read(handoff.ack[0],&stage,1)!=1 || stage=='D')break;
            if(stage=='W')timeout=-1;
        }
    }
    _exit(128+number);
}

void CrashReport_Install(int interactive, void (*describe)(char *out, size_t size)) {
    static char spare_stack[256u << 10];
    static const int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP, SIGABRT};
    stack_t stack;
    struct sigaction action;
    Dl_info self;

    crash.interactive = interactive;
    crash.describe = describe;
    crash.game_thread = Crash_ThreadId();
    clock_gettime(CLOCK_MONOTONIC, &crash.started);
#ifdef __APPLE__
    uint32_t length = sizeof(crash.exe);
    char found[PATH_MAX];
    if (!_NSGetExecutablePath(crash.exe, &length) && realpath(crash.exe, found)) {
        snprintf(crash.exe, sizeof(crash.exe), "%s", found);
    }
#else
    ssize_t length = readlink("/proc/self/exe", crash.exe, sizeof(crash.exe) - 1);
    crash.exe[length > 0 ? length : 0] = 0;
    /* The first unwind loads the unwinder; that must not happen in a crash. */
    uintptr_t warm[2];
    CrashRegs none = {0, 0, 0, 0};
    Crash_Walk(none, warm, 2);
#endif
    if (dladdr((const void *)(uintptr_t)CrashReport_Install, &self)) {
        crash.exe_base = self.dli_fbase;
    }
    Crash_LoadNames();
    Crash_DescribeSystem();
    if (Crash_FindFolder() && interactive && Crash_OpenLog()) {
        Crash_Redirect(0);
        Crash_Redirect(1);
    }
    if(crash.dir[0]) {
        struct tm now;Crash_Now(&now);
        snprintf(handoff.path,sizeof(handoff.path),"%s/crash-%04d-%02d-%02d_%02d-%02d-%02d-%ld.txt",
            crash.dir,now.tm_year+1900,now.tm_mon+1,now.tm_mday,now.tm_hour,now.tm_min,now.tm_sec,(long)getpid());
        snprintf(handoff.temporary,sizeof(handoff.temporary),"%s.tmp",handoff.path);
        handoff.emergency=open(handoff.path,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0644);
        if(handoff.emergency>=0 && !pipe(handoff.pipe) && !pipe(handoff.ack)) {
            fcntl(handoff.pipe[0],F_SETFD,FD_CLOEXEC);fcntl(handoff.pipe[1],F_SETFD,FD_CLOEXEC);
            fcntl(handoff.pipe[1],F_SETFL,O_NONBLOCK);
            fcntl(handoff.ack[0],F_SETFD,FD_CLOEXEC);fcntl(handoff.ack[1],F_SETFD,FD_CLOEXEC);
            pthread_t reporter;
            if(!pthread_create(&reporter,NULL,Crash_Reporter,NULL))pthread_detach(reporter);
            else {close(handoff.pipe[0]);close(handoff.pipe[1]);handoff.pipe[0]=handoff.pipe[1]=-1;}
        }
        atexit(Crash_Finish);
    }
    /* Leaves room for the report after a stack overflow on the main thread. */
    memset(&stack, 0, sizeof(stack));
    stack.ss_sp = spare_stack;
    stack.ss_size = sizeof(spare_stack);
    sigaltstack(&stack, NULL);
    memset(&action, 0, sizeof(action));
    action.sa_sigaction = Crash_Signal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); ++i) {
        sigaction(signals[i], &action, NULL);
    }
}

void CrashReport_SetMods(void (*describe)(char *out, size_t size, int on_game_thread)) {
    crash.describe_mods = describe;
}

void CrashReport_SetSaveData(void (*describe)(char *out, size_t size)) {
    crash.describe_save = describe;
}

#endif
