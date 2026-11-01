# Opt-in public SDK game build; desktop defaults remain in root CMake.
if(NOT PS5 OR NOT EXISTS "${PSYZ_DIR}/psyz/CMakeLists.txt")
    message(FATAL_ERROR "PE_PS5_GAME requires the PS5 toolchain and staged PSYZ_DIR")
endif()
find_package(SDL3 CONFIG REQUIRED)
set(PE_PS5_GPU_NATIVE TRUE)
add_compile_definitions(PE_PS5=1 PE_PS5_GPU_NATIVE=1 __BSD_VISIBLE=1)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
option(PE_PS5_PLUGINS "Built-in playtest assists and CPU panel; no dynamic loader" OFF)
set(PE_MODS ${PE_PS5_PLUGINS} CACHE BOOL "" FORCE)
if(PE_PS5_PLUGINS)
    set(PE_MOD_LIST pe_qol pe_rebalance pe_economy pe_collection pe_assists CACHE STRING "Reviewed PS5 built-ins" FORCE)
    add_compile_definitions(PE_PS5_PLUGINS=1)
endif()
add_compile_options(-std=gnu11)
link_libraries(SceVideoOut SceAudioOut ScePad SceUserService)
# The public SDK Windows linker wrapper uses system() and cannot carry the game's
# expanded object list. Invoke the same LLVM linker/script with Ninja response files.
if(CMAKE_HOST_WIN32)
    file(TO_CMAKE_PATH "${PE_PS5_LLVM_BIN}" llvm_bin)
    file(TO_CMAKE_PATH "${PS5_PAYLOAD_SDK}" sdk_path)
    set(CMAKE_C_LINK_EXECUTABLE "${llvm_bin}/ld.lld.exe -m elf_x86_64 -pie --eh-frame-hdr -z max-page-size=0x4000 -T ${sdk_path}/ldscripts/elf_x86_64.x --gc-sections -L${sdk_path}/target/lib <OBJECTS> -o <TARGET> <LINK_LIBRARIES> ${sdk_path}/target/lib/crt1.o -lc -lkernel_web -lSceLibcInternal -lSceNet")
endif()
# Recompile SDL's real core with the console-only GTK teardown excluded.
# The installed archive is preserved; its SDL.c member is never extracted because
# these real definitions are supplied directly to the executable.
get_filename_component(sdl_build_root "${SDL3_DIR}/../../../../" ABSOLUTE)
set(PE_PS5_SDL3_SOURCE "${sdl_build_root}/src" CACHE PATH "Generated public SDL3 sources matching the installed archive")
set(PE_PS5_SDL3_BUILD "${sdl_build_root}/b" CACHE PATH "Matching generated SDL3 configuration")
set(sdl_source "${PE_PS5_SDL3_SOURCE}")
foreach(required "${sdl_source}/src/SDL.c" "${PE_PS5_SDL3_BUILD}/include-config-release/build_config/SDL_build_config.h")
    if(NOT EXISTS "${required}")
        message(FATAL_ERROR "SDL3 core override needs matching generated source/config: ${required}")
    endif()
endforeach()
file(READ "${sdl_source}/src/SDL.c" sdl_core)
string(REPLACE "&& !defined(SDL_PLATFORM_PRIVATE)" "&& !defined(SDL_PLATFORM_PRIVATE) && !defined(PE_PS5)" sdl_core "${sdl_core}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/SDL-ps5-core.c" "${sdl_core}")
add_library(pe-ps5-sdl-core OBJECT "${CMAKE_CURRENT_BINARY_DIR}/SDL-ps5-core.c")
target_include_directories(pe-ps5-sdl-core PRIVATE
    "${sdl_source}/include" "${sdl_source}/src"
    "${PE_PS5_SDL3_BUILD}/include-config-release/build_config"
    "${PE_PS5_SDL3_BUILD}/include-revision")
target_compile_definitions(pe-ps5-sdl-core PRIVATE SDL_STATIC_LIB USING_GENERATED_CONFIG_H
    SDL_BUILD_MAJOR_VERSION=3 SDL_BUILD_MINOR_VERSION=4 SDL_BUILD_MICRO_VERSION=16)
