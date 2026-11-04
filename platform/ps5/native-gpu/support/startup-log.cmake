include("@ROOT@/game-support/application.cmake")
add_library(pe-startup-log OBJECT "@ROOT@/game-support/startup-log.c")
target_sources(pe-startup-log PRIVATE "@ROOT@/game-support/application-startup-log.c")
target_include_directories(pe-startup-log PRIVATE "@ROOT@/game-support")
target_link_libraries(pe-startup-log PRIVATE SDL3::SDL3)
target_sources(parasite-eve-pc PRIVATE $<TARGET_OBJECTS:pe-startup-log>)
target_compile_definitions(pe-startup-log PRIVATE PE_APPLICATION_SERVICE_DIAGNOSTICS=1)
target_sources(pe-startup-log PRIVATE "@ROOT@/game-support/application-allocation-log.c")
target_link_libraries(parasite-eve-pc PRIVATE --wrap=main)
target_link_libraries(parasite-eve-pc PRIVATE --wrap=chdir)
target_link_libraries(parasite-eve-pc PRIVATE --wrap=dup2)
target_link_libraries(parasite-eve-pc PRIVATE --wrap=fopen)
target_link_libraries(parasite-eve-pc PRIVATE --wrap=stat)
target_link_libraries(parasite-eve-pc PRIVATE --wrap=mkdir)
target_link_libraries(parasite-eve-pc PRIVATE --wrap=opendir)
target_link_libraries(parasite-eve-pc PRIVATE --wrap=readdir)
target_link_libraries(parasite-eve-pc PRIVATE --wrap=SDL_CreateDirectory)
target_link_libraries(parasite-eve-pc PRIVATE --wrap=SDL_GetPathInfo)
target_link_libraries(parasite-eve-pc PRIVATE --wrap=SDL_EnumerateDirectory)
target_link_libraries(parasite-eve-pc PRIVATE --wrap=SDL_SaveFile)
target_link_libraries(parasite-eve-pc PRIVATE --wrap=SDL_Init)
target_link_libraries(parasite-eve-pc PRIVATE --wrap=SDL_InitSubSystem)
target_link_libraries(parasite-eve-pc PRIVATE --wrap=SDL_CreateWindow)
target_link_libraries(parasite-eve-pc PRIVATE --wrap=SDL_GetWindowSurface)
target_link_libraries(parasite-eve-pc PRIVATE --wrap=SDL_UpdateWindowSurface)
target_link_libraries(parasite-eve-pc PRIVATE --wrap=SDL_OpenAudioDevice)
target_link_libraries(parasite-eve-pc PRIVATE --wrap=SDL_OpenAudioDeviceStream)
target_link_libraries(parasite-eve-pc PRIVATE --wrap=SDL_ResumeAudioStreamDevice)
target_link_libraries(parasite-eve-pc PRIVATE --wrap=SDL_OpenGamepad)
target_link_libraries(parasite-eve-pc PRIVATE --wrap=Boot_InitSystem)
target_link_libraries(parasite-eve-pc PRIVATE --wrap=Boot_InitGameState)
target_link_libraries(parasite-eve-pc PRIVATE --wrap=Boot_InitSubsystems)
target_link_libraries(parasite-eve-pc PRIVATE --wrap=Asset_OpenPeImage)
target_link_libraries(parasite-eve-pc PRIVATE --wrap=Scene_LoadSceneData)
target_link_libraries(parasite-eve-pc PRIVATE --wrap=sceUserServiceInitialize)
target_link_libraries(parasite-eve-pc PRIVATE --wrap=scePadInit)
target_link_libraries(parasite-eve-pc PRIVATE --wrap=sceSystemServiceHideSplashScreen)
target_link_libraries(parasite-eve-pc PRIVATE --wrap=sceVideoOutOpen)
target_link_libraries(parasite-eve-pc PRIVATE --wrap=malloc)
target_link_libraries(parasite-eve-pc PRIVATE --wrap=calloc)
target_link_libraries(parasite-eve-pc PRIVATE --wrap=realloc)
target_sources(pe-startup-log PRIVATE "@ROOT@/game-support/gpu-trace.c")
target_link_libraries(parasite-eve-pc PRIVATE --wrap=sceKernelAllocateDirectMemory --wrap=sceKernelReleaseDirectMemory)





# Optional effects, independent default-off settings.
target_sources(psyz PRIVATE "@ROOT@/psyz-src/psyz/src/platform/fsr1/cpe_fsr1.c")
target_sources(parasite-eve-pc PRIVATE "@ROOT@/game-src/src/main/render/model_fill_lighting.c")

# Matching public provider ABI isolated from game DIR layout.
set(PE_NATIVE_DIRECTORY_PROVIDER_SYSROOT "@BASE@/deps/radv-sdk/ps5-payload-sdk/target")
include("${CMAKE_CURRENT_LIST_DIR}/pe-native-directory.cmake")

# Optional bounded render-only FallingSprite augmentation.
target_sources(parasite-eve-pc PRIVATE "@ROOT@/game-src/src/main/fieldfx/particle_mesh/pe_battle_particle_mesh.c" "@ROOT@/game-src/src/main/fieldfx/particle_mesh/pe_falling_sprite_adapter.c" "@ROOT@/game-src/src/main/fieldfx/particle_mesh/pe_particle_render.c")
set_source_files_properties("@ROOT@/game-src/src/main/fieldfx/particle_mesh/pe_falling_sprite_adapter.c" PROPERTIES COMPILE_DEFINITIONS PE_BATTLE_PARTICLE_PROTOTYPE=1)

target_sources(psyz PRIVATE "@ROOT@/psyz-src/psyz/src/platform/movie_cleanup/movie_cleanup.c")

target_sources(parasite-eve-pc PRIVATE
 "@ROOT@/game-src/src/main/boot/installer/pe_bootstrap.c"
 "@ROOT@/game-src/src/main/boot/installer/pe_bootstrap_game_disc.c"
 "@ROOT@/game-src/src/main/boot/installer/pe_pack_check.c")

set(PE_NATIVE_STATE_CPU_COMPONENTS ON)
include("@ROOT@/state/state-components.cmake")
# Manager explicitly selected typed same-run/current-room transactions.
include("@ROOT@/state/state-bank-sources.cmake")
include("@ROOT@/state/state-transition.cmake")

include("@ROOT@/state/state-assists.cmake")

# Optional dialogue glyph adapter; retained Plex/OFL and stb notices remain.
target_sources(parasite-eve-pc PRIVATE "@ROOT@/game-src/src/main/render/dialogue_alpha.c")
target_include_directories(parasite-eve-pc PRIVATE "@ROOT@/game-src/src/main/boot")

# Native prepared character subset validation, no download or asset writes.
target_sources(parasite-eve-pc PRIVATE "@ROOT@/game-src/src/main/boot/installer/pe_character_pack.c")
