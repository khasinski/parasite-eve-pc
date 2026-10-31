target_sources(parasite-eve-pc PRIVATE "@ROOT@/game-support/app_crt.o")
target_sources(parasite-eve-pc PRIVATE "@ROOT@/game-support/application-static-dlfcn.c")
# This static software-rendered application has no plugin loader. Use SDL's
# explicit unsupported backend rather than payload CRT dlsym callbacks.
target_sources(pe-ps5-sdl-core PRIVATE "@ROOT@/sdl3-src/src/loadso/dummy/SDL_sysloadso.c")
target_compile_definitions(pe-ps5-sdl-core PRIVATE SDL_LOADSO_DUMMY=1)
# Process creation is unsupported for this application. The real SDL dummy
# backend avoids pulling raw inline vfork/setsid syscalls from SDK libc.a.
target_sources(pe-ps5-sdl-core PRIVATE "@ROOT@/sdl3-src/src/process/dummy/SDL_dummyprocess.c" "@ROOT@/game-support/SDL-application-filesystem.c")
target_compile_definitions(pe-ps5-sdl-core PRIVATE SDL_PROCESS_DUMMY=1)
# Generated filesystem source retains upstream relative include paths.
target_include_directories(pe-ps5-sdl-core PRIVATE "@ROOT@/sdl3-src/src/filesystem/unix")
set(CMAKE_C_LINK_EXECUTABLE "\"@BASE@/deps/llvm/bin/ld.lld.exe\" <OBJECTS> -o <TARGET> <LINK_LIBRARIES> \"@@ROOT@/GAME-RADV-ARGS.rsp\"")



