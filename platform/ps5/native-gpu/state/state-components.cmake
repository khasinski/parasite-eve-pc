# Reviewed development-only source integration. No states UI or automatic requests.
option(PE_NATIVE_STATE_CPU_COMPONENTS "Build partial native CPU state components" OFF)
if(NOT PE_NATIVE_STATE_CPU_COMPONENTS)
    return()
endif()
if(NOT PE_PS5_GAME OR NOT TARGET parasite-eve-pc OR NOT TARGET SDL3::SDL3)
    message(FATAL_ERROR "Native CPU components require actual PS5 game and SDL target")
endif()
# Before enabling: apply previous numeric GTE append patch and state_numeric.h,
# and this batch's exact frame_loop + room_load ownership hooks. Source relocation is manager-owned.
target_sources(parasite-eve-pc PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/pe_state.c"
    "${CMAKE_CURRENT_LIST_DIR}/pe_state_rng.c"
    "${CMAKE_CURRENT_LIST_DIR}/pe_state_gte.c"
    "${CMAKE_CURRENT_LIST_DIR}/pe_state_gpu.c"
    "${CMAKE_CURRENT_LIST_DIR}/pe_state_audio.c"
    "${CMAKE_CURRENT_LIST_DIR}/pe_state_producers.c"
    "${CMAKE_CURRENT_LIST_DIR}/pe_state_akao.c"
    "${CMAKE_CURRENT_LIST_DIR}/pe_state_render.c"
    "${CMAKE_CURRENT_LIST_DIR}/pe_state_room_object.c"
    "${CMAKE_CURRENT_LIST_DIR}/pe_state_checkpoint.c"
    "${CMAKE_CURRENT_LIST_DIR}/pe_state_mirror.c"
    "${CMAKE_CURRENT_LIST_DIR}/pe_state_globals.c"
    "${CMAKE_CURRENT_LIST_DIR}/pe_state_geometry.c"
    "${CMAKE_CURRENT_LIST_DIR}/pe_state_files.c"
    "${CMAKE_CURRENT_LIST_DIR}/pe_state_actor_prepare.c"
    "${CMAKE_CURRENT_LIST_DIR}/pe_state_boundary.c"
    "${CMAKE_CURRENT_LIST_DIR}/pe_state_node_types.c"
    "${CMAKE_CURRENT_LIST_DIR}/pe_state_lifetime.c"
    "${CMAKE_CURRENT_LIST_DIR}/pe_state_world.c"
    "${CMAKE_CURRENT_LIST_DIR}/pe_state_assets.c"
    "${CMAKE_CURRENT_LIST_DIR}/pe_state_battle.c"
    "${CMAKE_CURRENT_LIST_DIR}/pe_state_session.c")
target_include_directories(parasite-eve-pc PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
target_compile_definitions(parasite-eve-pc PRIVATE PE_NATIVE_STATE_CPU_COMPONENTS=1)
target_link_libraries(parasite-eve-pc PRIVATE SDL3::SDL3)

# Required: apply state_gpu.h + conditional SDL GPU/PGXP patches; propagate opt-in to PsyZ target.

if(NOT TARGET psyz)
    message(FATAL_ERROR "Native VRAM state requires actual psyz backend target")
endif()
target_compile_definitions(psyz PRIVATE PE_NATIVE_STATE_CPU_COMPONENTS=1)
target_include_directories(psyz PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../psyz-src/psyz/include")
target_include_directories(parasite-eve-pc PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../psyz-src/psyz/include")

foreach(owner_target parasite-eve-original parasite-eve-original-akao parasite-eve-original-entity-textures pe-mods)
 target_compile_definitions(${owner_target} PRIVATE PE_NATIVE_STATE_CPU_COMPONENTS=1)
 target_include_directories(${owner_target} PRIVATE "${CMAKE_CURRENT_LIST_DIR}" "${CMAKE_CURRENT_LIST_DIR}/../psyz-src/psyz/include")
 target_link_libraries(${owner_target} PRIVATE SDL3::SDL3)
endforeach()

target_sources(parasite-eve-pc PRIVATE "${CMAKE_CURRENT_LIST_DIR}/pe_state_theater.c")
