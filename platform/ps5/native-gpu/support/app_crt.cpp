/*
 * ps5-native-app-boilerplate - Native application startup.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Initializes the platform runtime, runs static constructors and main, and
 * hands normal process termination back to the platform runtime.
 */

#include <cstddef>
#include <cstdint>

using Destructor = void (*)();
using Initializer = void (*)();

extern "C"
{
    void pe_application_stage(const char *message);
    void _init_env(void *process_parameters);
    int atexit(Destructor callback);
    [[noreturn]] void exit(int status);
    int application_main(int argc, char **argv, char **envp) __asm__("main");

    extern Initializer __preinit_array_start[] __attribute__((weak));
    extern Initializer __preinit_array_end[] __attribute__((weak));
    extern Initializer __init_array_start[] __attribute__((weak));
    extern Initializer __init_array_end[] __attribute__((weak));
    extern Initializer __fini_array_start[] __attribute__((weak));
    extern Initializer __fini_array_end[] __attribute__((weak));
}

namespace
{
void run_forward(Initializer *first, Initializer *last) noexcept
{
    if (first == nullptr || last == nullptr)
        return;
    while (first != last)
        (*first++)();
}

void run_reverse(Initializer *first, Initializer *last) noexcept
{
    if (first == nullptr || last == nullptr)
        return;
    while (last != first)
        (*--last)();
}
} // namespace

extern "C" __attribute__((weak)) void catchReturnFromMain(int status)
{
    (void)status;
}

extern "C" void _init()
{
    run_forward(__preinit_array_start, __preinit_array_end);
    run_forward(__init_array_start, __init_array_end);
}

extern "C" void _fini()
{
    run_reverse(__fini_array_start, __fini_array_end);
}

extern "C" [[noreturn]] __attribute__((visibility("default"))) void
_start(void *process_parameters, Destructor loader_teardown)
{
    pe_application_stage("\n=== PE application: entered _start ===\n");
    const int argc = *static_cast<const int *>(process_parameters);
    auto *parameters = static_cast<std::uint8_t *>(process_parameters);
    auto **argv = reinterpret_cast<char **>(parameters + sizeof(std::uint64_t));

    pe_application_stage("before _init_env\n");
    _init_env(process_parameters);
    pe_application_stage("after _init_env\n");
    if (loader_teardown != nullptr)
        (void)atexit(loader_teardown);
    (void)atexit(_fini);
    pe_application_stage("before constructors\n");
    _init();
    pe_application_stage("after constructors; before main\n");
    const int status = application_main(argc, argv, nullptr);
    pe_application_stage("main returned\n");
    catchReturnFromMain(status);
    pe_application_stage("before exit\n");
    exit(status);
}
