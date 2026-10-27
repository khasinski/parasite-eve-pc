cmake_minimum_required(VERSION 3.21..3.31)
# Explicit deployment target avoids inheriting the build host's macOS version.
if(CMAKE_HOST_APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET
   AND (NOT DEFINED CMAKE_SYSTEM_NAME OR CMAKE_SYSTEM_NAME STREQUAL "Darwin"))
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum supported macOS version")
endif()
project(parasite-eve-pc VERSION 0.5.0 LANGUAGES C)
option(PE_MACOS_BUNDLE "Package the macOS application bundle" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(PE_ENABLE_COVERAGE "Measure execution coverage with Clang/LLVM" OFF)
if(PE_ENABLE_COVERAGE)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "PE_ENABLE_COVERAGE requires Clang and its LLVM profile runtime")
    endif()
    add_compile_options(-fprofile-instr-generate -fcoverage-mapping -fprofile-update=atomic)
    add_link_options(-fprofile-instr-generate)
    add_compile_definitions(PE_NATIVE_COVERAGE=1)
endif()

option(BUILD_TESTING "Build original game tests" OFF)
include(CTest)

# Mod host (mods/). Off: the PE_HOOK lines compile to nothing.
option(PE_MODS "Build with the mod host and the mods in mods/" OFF)
if(PE_MODS)
    add_compile_definitions(PE_MODS=1)
endif()

# PsyZ belongs to the original game build, independently of the abandoned frontend.
set(PSYZ_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/psyz)
add_subdirectory(${PSYZ_DIR}/psyz ${CMAKE_CURRENT_BINARY_DIR}/psyz)
target_compile_definitions(psyz PRIVATE __psyz)

# The original actor/task workspaces are shared by distinct layout views.
# Preserve those accesses instead of assuming different struct tags cannot alias.
# Set this after configuring PsyZ: its SDK/SDL targets retain their own options.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options("$<$<COMPILE_LANGUAGE:C>:-fno-strict-aliasing>"
        "$<$<COMPILE_LANGUAGE:C>:-ffunction-sections>"
        "$<$<COMPILE_LANGUAGE:C>:-fdata-sections>")
elseif(MSVC)
    add_compile_options("$<$<COMPILE_LANGUAGE:C>:/Gy>" "$<$<COMPILE_LANGUAGE:C>:/Gw>")
endif()

# Whole original modules may contain entry points not yet called by each test.
# Keep function-level linking instead of splitting source files for test targets.
if(APPLE)
    add_link_options(-Wl,-dead_strip)
elseif(NOT MSVC)
    add_link_options(-Wl,--gc-sections)
else()
    add_link_options(/OPT:REF)
endif()

function(pe_configure_object_library target_name)
    target_include_directories(${target_name} PRIVATE include)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /Gy /Gw)
    else()
        target_compile_options(${target_name} PRIVATE -ffunction-sections -fdata-sections)
    endif()
endfunction()

# Original scene/archive module owns file-image I/O and completion flags.
add_library(parasite-eve-original-disc OBJECT src/main/cdrom/scene_assets.c)
pe_configure_object_library(parasite-eve-original-disc)

# The executable starts the original game and compiles its modules in place.
add_executable(parasite-eve-pc src/main/boot/main.c)
target_include_directories(parasite-eve-pc PRIVATE include)
target_link_libraries(parasite-eve-pc PRIVATE psyz)
target_compile_definitions(parasite-eve-pc PRIVATE __psyz)
if(APPLE AND PE_MACOS_BUNDLE)
    set_target_properties(parasite-eve-pc PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist"
        OUTPUT_NAME "Parasite Eve")
    target_sources(parasite-eve-pc PRIVATE packaging/macos/ParasiteEve.icns)
    set_source_files_properties(packaging/macos/ParasiteEve.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    foreach(notice README.md CHANGELOG.md LICENSE.md THIRD_PARTY_NOTICES.md)
        target_sources(parasite-eve-pc PRIVATE ${notice})
        set_source_files_properties(${notice} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    endforeach()
    foreach(notice_dir third_party/psyz third_party/psyz/decomp
            third_party/psyz/psyz/src/platform third_party/psyz/psyz/src/psyz
            third_party/psyz/external/SDL/src/video/yuv2rgb)
        target_sources(parasite-eve-pc PRIVATE ${notice_dir}/LICENSE)
        set_source_files_properties(${notice_dir}/LICENSE PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources/licenses/${notice_dir}")
    endforeach()
    target_sources(parasite-eve-pc PRIVATE third_party/psyz/external/SDL/LICENSE.txt
        third_party/psyz/external/SDL/src/hidapi/LICENSE-bsd.txt)
    set_source_files_properties(third_party/psyz/external/SDL/LICENSE.txt
        third_party/psyz/external/SDL/src/hidapi/LICENSE-bsd.txt PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources/licenses/SDL)
    target_sources(parasite-eve-pc PRIVATE src/main/boot/fonts/OFL-IBMPlex.txt)
    set_source_files_properties(src/main/boot/fonts/OFL-IBMPlex.txt PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources/licenses/IBM-Plex)
endif()
psyz_title(parasite-eve-pc "Parasite Eve")
psyz_web_configure_target(parasite-eve-pc)
psyz_ios_configure_target(parasite-eve-pc
    BUNDLE_IDENTIFIER "dev.parasiteeve.pc" DISPLAY_NAME "Parasite Eve")

# The original motion module has its own copies of the common class
# methods and immutable tables, just as the original room library did.
add_library(parasite-eve-original-motion OBJECT
    src/overlays/room_lib/motion_particles.c
    src/overlays/room_lib/motion_particles_2.c
    src/overlays/room_lib/motion_particles_3.c
    src/overlays/room_lib/class_plant_script.c
    src/overlays/room_lib/class_register_draw_list.c
    src/overlays/room_lib/class_register_tables_gated.c
    src/overlays/room_lib/class_nop6.c)
pe_configure_object_library(parasite-eve-original-motion)
target_link_libraries(parasite-eve-original-motion PRIVATE psyz)
target_compile_definitions(parasite-eve-original-motion PRIVATE PE_OVERLAY=motion)

# The original paired-glow module retains its own common methods/tables.
add_library(parasite-eve-original-paired OBJECT
    src/overlays/room_lib/paired_glow.c
    src/overlays/room_lib/class_plant_script.c
    src/overlays/room_lib/class_register_draw_list.c
    src/overlays/room_lib/class_register_tables.c)
pe_configure_object_library(parasite-eve-original-paired)
target_link_libraries(parasite-eve-original-paired PRIVATE psyz)
target_compile_definitions(parasite-eve-original-paired PRIVATE PE_OVERLAY=paired)

# Hospital falling-burst module, retaining its original common class methods.
add_library(parasite-eve-original-falling OBJECT
    src/overlays/room_lib/falling_sprite_burst.c
    src/overlays/room_lib/class_plant_script.c
    src/overlays/room_lib/class_register_draw_list.c
    src/overlays/room_lib/class_register_tables.c)
pe_configure_object_library(parasite-eve-original-falling)
target_link_libraries(parasite-eve-original-falling PRIVATE psyz)
target_compile_definitions(parasite-eve-original-falling PRIVATE PE_OVERLAY=falling)

# E19 retains its original scene-specific copy of the shared grab step.
add_library(parasite-eve-original-e19 OBJECT
    src/overlays/scene_e19_cruiser/e19.c
    src/overlays/scene_e19_cruiser/e19_class.c
    src/overlays/room_lib/grab_await_stance.c)
pe_configure_object_library(parasite-eve-original-e19)
target_link_libraries(parasite-eve-original-e19 PRIVATE psyz)
target_compile_definitions(parasite-eve-original-e19 PRIVATE PE_OVERLAY=scene_e19_cruiser)

# E08 keeps its original module files and scene-specific script planter.
add_library(parasite-eve-original-e08 OBJECT
    src/overlays/scene_e08_chrysler/e08.c
    src/overlays/scene_e08_chrysler/SceneE08_ModuleStart.c
    src/overlays/room_lib/class_plant_script.c)
pe_configure_object_library(parasite-eve-original-e08)
target_link_libraries(parasite-eve-original-e08 PRIVATE psyz)
target_compile_definitions(parasite-eve-original-e08 PRIVATE PE_OVERLAY=scene_e08_chrysler)

# Compile the original game sources in place. Modules join this target as
# their platform dependencies are made portable; no duplicate game logic.
add_library(parasite-eve-original OBJECT
    src/main/battle/encounter.c
    src/main/battle/setup.c
    src/main/battle/turn.c
    src/main/battle/enemy_actions.c
    src/main/battle/status_hud.c
    src/main/battle/attack.c
    src/main/battle/context.c
    src/main/battle/player_turn.c
    src/main/battle/phase.c
    src/main/battle/enemy_slots.c
    src/main/battle/enemy_movement.c
    src/main/entity/combat.c
    src/main/aya/stats.c
    src/main/item/active_list.c
    src/main/item/actions.c
    src/main/item/swap.c
    src/main/menu/skills.c
    src/main/menu/equip_screen.c
    src/main/menu/equip_select.c
    src/main/menu/item_actions.c
    src/main/menu/item_use_panel.c
    src/main/menu/main_menu.c
    src/main/menu/main_update.c
    src/main/time/counters.c
    src/main/psyq/libsnd/ssinit_c.c
    src/main/save/game_data.c
    src/main/memcard/save_flow.c
    src/main/menu/inventory.c
    src/main/item/armor_capacity.c
    src/main/render/font.c
    src/main/render/font_load.c
    src/main/menu/status_panels.c
    src/main/menu/save_screens.c
    src/main/menu/save_slot.c
    src/main/menu/lists.c
    src/main/menu/item_detail.c
    src/main/battle/commands.c
    src/main/text/tables.c
    src/main/asset/tim_textures.c
    src/main/asset/unload_table.c
    src/main/math/fixed_mul.c
    src/main/math/fixed_div.c
    src/main/math/fixed_round.c
    src/main/math/sqrt_approx.c
    src/main/fieldfx/angle.c
    src/main/psyq/libgte/reg03.c
    src/main/psyq/libgte/fog_02.c
    src/main/psyq/libgte/mtx_000.c
    src/main/psyq/libgte/mtx_001.c
    src/main/psyq/libgte/msc06.c
    src/main/psyq/libgte/mtx_06.c
    src/main/psyq/libgte/cmb_06.c
    src/main/anim/bone_rotations.c
    src/main/render/object.c
    src/main/render/wireframe.c
    src/main/render/frame_update.c
    src/main/entity/frame_update.c
    src/main/boot/init.c
    src/main/boot/frame_loop.c
    src/main/draw/pipeline.c
    src/main/draw/gpu_regs.c
    src/main/draw/prim_queue.c
    src/main/draw/prim_color.c
    src/main/draw/frame.c
    src/main/draw/text.c
    src/main/draw/textbox.c
    src/overlays/menu_memcard/list_screen.c
    src/overlays/menu_memcard/video_playback.c
    src/overlays/menu_memcard/video.c
    src/overlays/menu_memcard/glint_effects.c
    src/overlays/menu_memcard/image_effects.c
    src/overlays/mdec_diag/vlc_build.c
    src/overlays/mdec_diag/vlc_c.c
    src/overlays/menu_memcard/page_flip.c
    src/overlays/menu_memcard/image_nodes.c
    src/overlays/menu_memcard/fade.c
    src/main/draw/wipe.c
    src/main/draw/color_quad.c
    src/main/pm/disc_tables.c
    src/main/pm/process.c
    src/main/entity/pool.c
    src/main/entity/allocation.c
    src/main/script/node_pool.c
    src/main/script/sfx_queue.c
    src/main/psyq/libds/dssys_3.c
    src/main/script/node_actions.c
    src/main/script/actor_opcodes.c
    src/main/script/value_ops.c
    src/main/script/flow_opcodes.c
    src/main/script/system_opcodes.c
    src/overlays/fx_common/effects.c
    src/overlays/fx_common/effect_init.c
    src/overlays/fx_common/motion_endpoints.c
    src/overlays/fx_common/camera_path.c
    src/overlays/fx_common/camera.c
    src/overlays/fx_common/model_buffers.c
    src/overlays/boot_display/text_screen.c
    src/overlays/boot_display/playback_file.c
    src/overlays/boot_display/playback.c
    src/overlays/boot_display/playback_stop.c
    src/overlays/boot_display/image_display.c
    src/overlays/boot_display/sprites.c
    src/main/fieldfx/geometry.c
    src/main/fieldfx/model.c
    src/main/fieldfx/sprite_render.c
    src/main/fieldfx/sprite_effects.c
    src/main/fieldfx/emitters.c
    src/main/fieldfx/collision.c
    src/main/fieldfx/glow.c
    src/main/fieldfx/burst.c
    src/main/fieldfx/object_script.c
    src/main/fieldfx/task_list.c
    src/main/fieldfx/vector.c
    src/main/field/actor_collision.c
    src/overlays/room_lib/effect_sequence.c
    src/overlays/room_lib/class_nop0.c
    src/overlays/room_lib/class_plant_script.c
    src/overlays/room_lib/glow_orb.c
    src/overlays/room_lib/three_particles.c
    src/overlays/room_lib/drop_field.c
    src/overlays/room_lib/orbit_set.c
    src/overlays/room_lib/spark_barrage.c
    src/overlays/room_lib/script_arg_pair.c
    src/overlays/room_lib/glow_orb_setup.c
    src/overlays/room_lib/class_spawn.c
    src/overlays/room_lib/class_close_target.c
    src/overlays/room_lib/class_register_active.c
    src/overlays/room_lib/actor_classes.c
    src/overlays/room_lib/actor_class_table.c
    src/overlays/room_lib/window_class.c
    src/overlays/room_lib/window_class_table.c
    src/overlays/room_m272_liberty/m272.c
    src/overlays/scene_e18_cruiser/e18_emitters.c
    src/overlays/scene_e20_cruiser/SceneE20_CommandEffectSlot.c
    src/overlays/scene_e20_cruiser/SceneE20_FlareParticle.c
    src/overlays/scene_e20_cruiser/SceneE20_HoverOrbController.c
    src/overlays/scene_e22_liberty/e22.c
    src/overlays/room_m273_liberty/m273.c
    src/overlays/room_m273_liberty/m273_class.c
    src/overlays/room_m273_liberty/RoomM273_PulseSpriteCallback.c
    src/overlays/room_m273_liberty/RoomEffect_PhasedArcSpriteFlow.c
    src/overlays/room_lib/class_update_callback.c
    src/overlays/room_lib/grab_await_stance.c
    src/overlays/room_m256_museum/m256.c
    src/overlays/room_m256_museum/m256_class.c
    src/overlays/room_m318_chrysler/m318.c
    src/overlays/room_m318_chrysler/m318_player_gate.c
    src/overlays/room_m404_chrysler/m404.c
    src/overlays/room_m349_cruiser/m349.c
    src/overlays/room_m350_cruiser/m350.c
    src/overlays/room_m350_cruiser/m350_callbacks.c
    src/overlays/room_m350_cruiser/RoomEffect_ConfiguredPulseCallback.c
    src/overlays/room_m005_theater/m005.c
    src/overlays/room_m086_central_park/m086.c
    src/overlays/room_m089_central_park/m089.c
    src/overlays/room_m023_theater/m023.c
    src/overlays/room_m123_nypd/m123.c
    src/overlays/room_m123_nypd/RoomM123_SelectSlotPointer.c
    src/overlays/room_lib/spark_attacks.c
    src/overlays/room_lib/seeker_spark.c
    src/overlays/room_lib/capture_frame_sample.c
    src/overlays/room_lib/script_arg.c
    src/overlays/room_lib/script_task_var.c
    src/overlays/room_lib/orbit_flare.c
    src/overlays/room_lib/line_burst.c
    src/overlays/room_lib/script_slot.c
    src/overlays/room_lib/dropped_flare.c
    src/overlays/room_lib/ground_eruption.c
    src/overlays/room_lib/homing_projectile.c
    src/overlays/room_lib/homing_flash.c
    src/overlays/room_lib/twelve_elements.c
    src/overlays/room_lib/floor_walker_class.c
    src/overlays/room_lib/trail_burst_attacks.c
    src/overlays/room_lib/spark_ring_burst.c
    src/overlays/room_lib/sound_burst.c
    src/overlays/room_lib/grab_attack.c
    src/overlays/room_lib/timed_spawn.c
    src/overlays/room_lib/homing_projectile_seeds.c
    src/overlays/room_lib/script_args.c)
pe_configure_object_library(parasite-eve-original)
target_link_libraries(parasite-eve-original PRIVATE psyz)
set_property(SOURCE src/main/boot/init.c APPEND PROPERTY INCLUDE_DIRECTORIES
    "$<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>")

set(PE_COMMON_GAME_OBJECTS
    $<TARGET_OBJECTS:parasite-eve-original>
    $<TARGET_OBJECTS:parasite-eve-original-motion>
    $<TARGET_OBJECTS:parasite-eve-original-paired>
    $<TARGET_OBJECTS:parasite-eve-original-falling>
    $<TARGET_OBJECTS:parasite-eve-original-e19>
    $<TARGET_OBJECTS:parasite-eve-original-e08>)
if(PE_MODS)
    add_subdirectory(mods)
    list(APPEND PE_COMMON_GAME_OBJECTS $<TARGET_OBJECTS:pe-mods>)
endif()

target_sources(parasite-eve-pc PRIVATE ${PE_COMMON_GAME_OBJECTS})

# Original AKAO units compile in place while their SPU/timer dependencies
# are connected. The title frontend uses the original effect playback path.
add_library(parasite-eve-original-akao OBJECT
    src/main/cdrom/track_read.c
    src/main/akao/voice_driver.c
    src/main/akao/voice_bank.c
    src/main/akao/sequencer.c
    src/main/akao/spu_transfer.c
    src/main/psyq/libspu/s_svva.c)
pe_configure_object_library(parasite-eve-original-akao)
target_link_libraries(parasite-eve-original-akao PRIVATE psyz)

set(akao_runtime_targets parasite-eve-pc)
foreach(akao_target IN LISTS akao_runtime_targets)
    target_sources(${akao_target} PRIVATE $<TARGET_OBJECTS:parasite-eve-original-actor-animation>
        $<TARGET_OBJECTS:parasite-eve-original-akao>
        $<TARGET_OBJECTS:parasite-eve-original-shadow>
        $<TARGET_OBJECTS:parasite-eve-original-disc>)
    target_include_directories(${akao_target} PRIVATE include)
    if(MSVC)
        target_link_options(${akao_target} PRIVATE /OPT:REF)
    elseif(APPLE)
        target_link_options(${akao_target} PRIVATE -Wl,-dead_strip)
    else()
        target_link_options(${akao_target} PRIVATE -Wl,--gc-sections)
    endif()
endforeach()

function(pe_add_common_unit_test target_name test_name source_file)
    add_executable(${target_name} ${source_file} ${ARGN} ${PE_COMMON_GAME_OBJECTS})
    target_include_directories(${target_name} PRIVATE include)
    target_link_libraries(${target_name} PRIVATE psyz)
    add_test(NAME ${test_name} COMMAND ${target_name})
endfunction()

if(BUILD_TESTING)
    add_executable(test-original-akao-layout tests/original_akao_layout.c)
    target_include_directories(test-original-akao-layout PRIVATE include)
    add_test(NAME original-game-akao-layout COMMAND test-original-akao-layout)

    pe_add_common_unit_test(test-original-math original-game-math tests/original_math.c)
    pe_add_common_unit_test(test-original-gte original-game-gte tests/original_gte.c)
    pe_add_common_unit_test(test-original-animation original-game-animation tests/original_animation.c)
    pe_add_common_unit_test(test-original-transforms original-game-transforms tests/original_transforms.c)
    pe_add_common_unit_test(test-original-projection original-game-projection tests/original_projection.c)
    pe_add_common_unit_test(test-original-hierarchy original-game-hierarchy tests/original_hierarchy.c)

    add_executable(test-psyz-spu-reference
        ${PSYZ_DIR}/psyz/tests/test_main.c
        ${PSYZ_DIR}/psyz/tests/test_libspu.c
        ${PSYZ_DIR}/ztest/ztest.c)
    target_compile_definitions(test-psyz-spu-reference PRIVATE __psyz)
    target_include_directories(test-psyz-spu-reference PRIVATE ${PSYZ_DIR}/ztest)
    target_link_libraries(test-psyz-spu-reference PRIVATE psyz)
    add_test(NAME original-game-spu-hardware-reference COMMAND test-psyz-spu-reference
        --filter=spu::GaussianHardwareImpulse,spu::GaussianKeyOnPitchPhase,spu::GaussianFmReference,spu::AdpcmOneShotEndPipeline,spu::VoiceGainLatchReference,spu::ReverbHardwareFirImpulse
        --output=plain)
    set_tests_properties(original-game-spu-hardware-reference PROPERTIES TIMEOUT 60
        ENVIRONMENT "PSYZ_HEADLESS=1;SDL_AUDIODRIVER=dummy;SDL_VIDEODRIVER=dummy"
        LABELS "audio")

    pe_add_common_unit_test(test-original-input-rules original-game-input-rules tests/original_input_rules.c)

    add_executable(test-host-settings tests/original_input_rules.c ${PE_COMMON_GAME_OBJECTS})
    target_compile_definitions(test-host-settings PRIVATE PE_HOST_SETTINGS_TEST=1 __psyz)
    target_include_directories(test-host-settings PRIVATE include)
    target_link_libraries(test-host-settings PRIVATE psyz)
    add_test(NAME host-settings COMMAND test-host-settings "${CMAKE_CURRENT_BINARY_DIR}/host-settings-evidence")
    set_tests_properties(host-settings PROPERTIES TIMEOUT 30)

    pe_add_common_unit_test(test-original-pools original-game-pools tests/original_pools.c)
    pe_add_common_unit_test(test-original-nodes original-game-nodes tests/original_nodes.c)

    pe_add_common_unit_test(test-original-queue original-game-queue tests/original_queue.c
        $<TARGET_OBJECTS:parasite-eve-original-actor-animation>
        $<TARGET_OBJECTS:parasite-eve-original-disc>
        $<TARGET_OBJECTS:parasite-eve-original-akao>
        $<TARGET_OBJECTS:parasite-eve-original-camera>)

    pe_add_common_unit_test(test-original-expression original-game-expression tests/original_expression.c
        $<TARGET_OBJECTS:parasite-eve-original-camera>)

    pe_add_common_unit_test(test-original-contact original-game-contact tests/original_contact.c)

    pe_add_common_unit_test(test-original-movement original-game-movement tests/original_movement.c
        src/main/asset/game_state.c
        $<TARGET_OBJECTS:parasite-eve-original-disc>
        $<TARGET_OBJECTS:parasite-eve-original-akao>
        $<TARGET_OBJECTS:parasite-eve-original-camera>)
endif()

add_library(parasite-eve-original-actor-animation OBJECT
    src/main/asset/game_state.c src/main/scene/player_movement.c src/main/psyq/libgte/mtx_004.c)
pe_configure_object_library(parasite-eve-original-actor-animation)

if(BUILD_TESTING)
    add_executable(test-original-actor-animation tests/original_actor_animation.c
        $<TARGET_OBJECTS:parasite-eve-test-disc>
        $<TARGET_OBJECTS:parasite-eve-original-akao>
        $<TARGET_OBJECTS:parasite-eve-original-camera>
        ${PE_COMMON_GAME_OBJECTS}
        $<TARGET_OBJECTS:parasite-eve-original-actor-animation>)
    target_include_directories(test-original-actor-animation PRIVATE include tests)
    target_link_libraries(test-original-actor-animation PRIVATE psyz)
    add_test(NAME original-game-save-safety COMMAND test-original-actor-animation --save-safety)
    add_test(NAME original-game-actor-animation COMMAND test-original-actor-animation)
    add_test(NAME original-game-animation-event-completion COMMAND test-original-actor-animation --anim-event-completion)
    add_test(NAME original-game-glint COMMAND test-original-actor-animation --glint)
    add_test(NAME original-game-fx-camera COMMAND test-original-actor-animation --fx-camera)
    add_test(NAME original-game-fx-display COMMAND test-original-actor-animation --fx-display)
    add_test(NAME original-game-fx-effects COMMAND test-original-actor-animation --fx-effects)
    add_test(NAME original-game-fx-scene COMMAND test-original-actor-animation --fx-scene)
    set_tests_properties(original-game-fx-scene PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    add_test(NAME original-game-fx-step COMMAND test-original-actor-animation --fx-step)
    set_tests_properties(original-game-fx-step PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    add_test(NAME original-game-credits-text COMMAND test-original-actor-animation --credits-text)
    set_tests_properties(original-game-credits-text PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60 LABELS "game-assets")
    add_test(NAME original-game-credits-images COMMAND test-original-actor-animation --credits-images)
    set_tests_properties(original-game-credits-images PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60 LABELS "game-assets;gpu")
    add_test(NAME original-game-credits-sprites COMMAND test-original-actor-animation --credits-sprites)
    set_tests_properties(original-game-credits-sprites PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60 LABELS "game-assets;gpu")
    add_test(NAME original-game-credits-playback COMMAND test-original-actor-animation --credits-playback)
    set_tests_properties(original-game-credits-playback PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90 LABELS "game-assets;gpu;audio")
    add_test(NAME original-game-texture-pages COMMAND test-original-actor-animation --texture-pages)
    set_tests_properties(original-game-texture-pages PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60 LABELS "game-assets;gpu")
    add_test(NAME original-game-live-graphics-controls COMMAND test-original-actor-animation --native-graphics-controls $<TARGET_FILE:parasite-eve-pc>)
    set_tests_properties(original-game-live-graphics-controls PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180 LABELS "game-assets;gpu" ENVIRONMENT "SDL_AUDIO_DRIVER=dummy")
    add_test(NAME original-game-story-movie-skip COMMAND test-original-actor-animation --native-movie-skip $<TARGET_FILE:parasite-eve-pc>)
    set_tests_properties(original-game-story-movie-skip PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 LABELS "game-assets;audio")
    add_test(NAME original-game-headless-control COMMAND test-original-actor-animation --native-control $<TARGET_FILE:parasite-eve-pc>)
    set_tests_properties(original-game-headless-control PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 LABELS "game-assets")
    add_test(NAME original-game-spu-mix-headroom COMMAND test-original-actor-animation --spu-mix-headroom)
    set_tests_properties(original-game-spu-mix-headroom PROPERTIES TIMEOUT 30 LABELS "audio")
    add_test(NAME original-game-spu-pitch-modulation COMMAND test-original-actor-animation --spu-pitch-modulation)
    add_test(NAME original-game-spu-noise COMMAND test-original-actor-animation --spu-noise)
    set_tests_properties(original-game-spu-pitch-modulation original-game-spu-noise PROPERTIES TIMEOUT 30 LABELS "audio")
    add_test(NAME original-game-spu-live-adsr COMMAND test-original-actor-animation --spu-live-adsr)
    set_tests_properties(original-game-spu-live-adsr PROPERTIES TIMEOUT 30 LABELS "audio")
    add_test(NAME original-game-spu-register-serialization COMMAND test-original-actor-animation --spu-register-serialization)
    set_tests_properties(original-game-spu-register-serialization PROPERTIES TIMEOUT 30 LABELS "audio")
    add_test(NAME original-game-seeker-particles COMMAND test-original-actor-animation --seeker-particles)
    set_tests_properties(original-game-seeker-particles PROPERTIES TIMEOUT 30 LABELS "fieldfx")
    add_test(NAME original-game-callback-storage COMMAND test-original-actor-animation --callback-list-storage)
    set_tests_properties(original-game-callback-storage PROPERTIES TIMEOUT 30 LABELS "fieldfx")
    add_test(NAME original-game-tutorial COMMAND test-original-actor-animation --native-tutorial $<TARGET_FILE:parasite-eve-pc>)
    set_tests_properties(original-game-tutorial PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 240 LABELS "game-assets;gpu;audio")
    add_test(NAME original-game-diagnostic-hp-arguments COMMAND test-original-actor-animation --native-diagnostic-hp-arguments $<TARGET_FILE:parasite-eve-pc>)
    set_tests_properties(original-game-diagnostic-hp-arguments PROPERTIES TIMEOUT 30)
    add_test(NAME original-game-bootstrap-cli COMMAND parasite-eve-pc --help)
    add_test(NAME original-game-bootstrap-arguments COMMAND parasite-eve-pc --frame-limit invalid)
    set_tests_properties(original-game-bootstrap-arguments PROPERTIES WILL_FAIL TRUE)
    add_test(NAME original-game-bootstrap COMMAND test-original-actor-animation --native-bootstrap $<TARGET_FILE:parasite-eve-pc>)
    set_tests_properties(original-game-bootstrap PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180 LABELS "game-assets;gpu")
    add_test(NAME original-game-field-control COMMAND test-original-actor-animation --native-field-control $<TARGET_FILE:parasite-eve-pc>)
    set_tests_properties(original-game-field-control PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 480 LABELS "game-assets;gpu;audio")
    add_test(NAME original-game-stage-transition COMMAND test-original-actor-animation --native-stage $<TARGET_FILE:parasite-eve-pc>)
    set_tests_properties(original-game-stage-transition PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180 LABELS "game-assets;gpu;audio")
    add_test(NAME original-game-battle-entry COMMAND test-original-actor-animation --native-battle-entry $<TARGET_FILE:parasite-eve-pc>)
    set_tests_properties(original-game-battle-entry PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180 LABELS "game-assets;gpu;audio")
    add_test(NAME original-attack-effect-status-gate COMMAND test-original-actor-animation --attack-effect-status-gate)
    add_test(NAME original-akao-queue-backpressure COMMAND test-original-actor-animation --akao-queue-backpressure)
    set_tests_properties(original-akao-queue-backpressure PROPERTIES TIMEOUT 30 LABELS "audio")
    add_test(NAME original-akao-bank-lifetime COMMAND test-original-actor-animation --akao-bank-lifetime)
    set_tests_properties(original-akao-bank-lifetime PROPERTIES TIMEOUT 30 LABELS "audio"
        ENVIRONMENT "PSYZ_HEADLESS=1;SDL_AUDIODRIVER=dummy")
    add_test(NAME original-game-m123-ai COMMAND test-original-actor-animation --m123-ai)
    set_tests_properties(original-game-m123-ai PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60 LABELS "game-assets")
    add_test(NAME original-game-m318 COMMAND test-original-actor-animation --m318)
    set_tests_properties(original-game-m318 PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60 LABELS "game-assets")
    add_test(NAME original-game-m318-data COMMAND test-original-actor-animation --m318-data)
    set_tests_properties(original-game-m318-data PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60 LABELS "game-assets")
    add_test(NAME original-game-m318-load COMMAND test-original-actor-animation --m318-load)
    set_tests_properties(original-game-m318-load PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60 LABELS "game-assets")
    add_test(NAME original-game-m256 COMMAND test-original-actor-animation --m256)
    set_tests_properties(original-game-m256 PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60 LABELS "game-assets")
    add_test(NAME original-game-m256-load COMMAND test-original-actor-animation --m256-load)
    set_tests_properties(original-game-m256-load PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60 LABELS "game-assets")
    add_test(NAME original-game-m404 COMMAND test-original-actor-animation --m404)
    set_tests_properties(original-game-m404 PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60 LABELS "game-assets")
    add_test(NAME original-game-m404-load COMMAND test-original-actor-animation --m404-load)
    set_tests_properties(original-game-m404-load PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60 LABELS "game-assets")
    add_test(NAME original-game-m350-load COMMAND test-original-actor-animation --m350-load)
    set_tests_properties(original-game-m350-load PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60 LABELS "game-assets")
    add_test(NAME original-game-m350 COMMAND test-original-actor-animation --m350)
    set_tests_properties(original-game-m350 PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60 LABELS "game-assets")
    add_test(NAME original-game-m349-load COMMAND test-original-actor-animation --m349-load)
    set_tests_properties(original-game-m349-load PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60 LABELS "game-assets")
    add_test(NAME original-game-m349 COMMAND test-original-actor-animation --m349)
    set_tests_properties(original-game-m349 PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60 LABELS "game-assets")
    add_test(NAME original-game-melissa-encounter COMMAND test-original-actor-animation --native-melissa $<TARGET_FILE:parasite-eve-pc>)
    set_tests_properties(original-game-melissa-encounter PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 240 LABELS "game-assets;gpu;audio")
    add_test(NAME original-game-backstage-transition COMMAND test-original-actor-animation --native-backstage $<TARGET_FILE:parasite-eve-pc>)
    set_tests_properties(original-game-backstage-transition PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300 LABELS "game-assets;gpu;audio")
    add_test(NAME original-game-hallway-transition COMMAND test-original-actor-animation --native-hallway $<TARGET_FILE:parasite-eve-pc>)
    set_tests_properties(original-game-hallway-transition PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300 LABELS "game-assets;gpu;audio")
    add_test(NAME original-game-rat-encounter COMMAND test-original-actor-animation --native-rat $<TARGET_FILE:parasite-eve-pc>)
    set_tests_properties(original-game-rat-encounter PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300 LABELS "game-assets;gpu;audio")
    add_test(NAME original-game-m020-items COMMAND test-original-actor-animation --native-m020-items $<TARGET_FILE:parasite-eve-pc>)
    set_tests_properties(original-game-m020-items PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 360 LABELS "game-assets;gpu;audio")
    add_test(NAME original-game-m017-survivor COMMAND test-original-actor-animation --native-m017 $<TARGET_FILE:parasite-eve-pc>)
    set_tests_properties(original-game-m017-survivor PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 420 LABELS "game-assets;gpu;audio")
    add_test(NAME original-tool-equipped-donor COMMAND test-original-actor-animation --tool-equipped-donor)
    add_test(NAME original-game-alligator-entry COMMAND test-original-actor-animation --native-alligator-entry $<TARGET_FILE:parasite-eve-pc>)
    set_tests_properties(original-game-alligator-entry PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 900 LABELS "game-assets;gpu;audio")
    add_test(NAME original-game-m028-sewer COMMAND test-original-actor-animation --native-m028 $<TARGET_FILE:parasite-eve-pc>)
    set_tests_properties(original-game-m028-sewer PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 720 LABELS "game-assets;gpu;audio")
    add_test(NAME original-game-m023-melissa COMMAND test-original-actor-animation --native-m023 $<TARGET_FILE:parasite-eve-pc>)
    set_tests_properties(original-game-m023-melissa PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600 LABELS "game-assets;gpu;audio")
    add_test(NAME original-game-m022-keys COMMAND test-original-actor-animation --native-m022 $<TARGET_FILE:parasite-eve-pc>)
    set_tests_properties(original-game-m022-keys PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 480 LABELS "game-assets;gpu;audio")
    add_dependencies(test-original-actor-animation parasite-eve-pc)
    add_test(NAME original-game-fx-frames COMMAND test-original-actor-animation --fx-frames)
    set_tests_properties(original-game-fx-frames PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 LABELS "game-assets;gpu")
    add_test(NAME original-game-fx-motion COMMAND test-original-actor-animation --fx-motion)
    set_tests_properties(original-game-fx-motion PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    add_test(NAME original-game-fx-models COMMAND test-original-actor-animation --fx-models)
    set_tests_properties(original-game-fx-models PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    add_test(NAME original-m418-chrysler-slots COMMAND test-original-actor-animation --m418-slots-draw)
    set_tests_properties(original-m418-chrysler-slots PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60 LABELS "game-assets;room;fieldfx;render")
    add_test(NAME original-m418-chrysler-draw COMMAND test-original-actor-animation --m418-cycles-draw)
    set_tests_properties(original-m418-chrysler-draw PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60 LABELS "game-assets;room;fieldfx;render")
    add_test(NAME original-m418-chrysler-update COMMAND test-original-actor-animation --m418-cycles-update)
    set_tests_properties(original-m418-chrysler-update PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60 LABELS "game-assets;room;fieldfx")
    add_test(NAME original-m418-chrysler-load COMMAND test-original-actor-animation --m418-load)
    set_tests_properties(original-m418-chrysler-load PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60 LABELS "game-assets;room")
    add_test(NAME original-m411-chrysler-load COMMAND test-original-actor-animation --m411-load)
    add_test(NAME original-m412-chrysler-load COMMAND test-original-actor-animation --m412-load)
    add_test(NAME original-m422-chrysler-load COMMAND test-original-actor-animation --m422-load)
    add_test(NAME original-m420-chrysler-load COMMAND test-original-actor-animation --m420-load)
    add_test(NAME original-m424-chrysler-load COMMAND test-original-actor-animation --m424-load)
    set_tests_properties(original-m411-chrysler-load original-m412-chrysler-load original-m422-chrysler-load original-m420-chrysler-load original-m424-chrysler-load PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60
        LABELS "game-assets;room" ENVIRONMENT "PSYZ_HEADLESS=1;SDL_AUDIODRIVER=dummy")
    add_test(NAME original-all-room-loads COMMAND test-original-actor-animation --all-room-loads)
    set_tests_properties(original-all-room-loads PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 LABELS "game-assets;room")
    add_test(NAME original-m417-chrysler-load COMMAND test-original-actor-animation --m417-load)
    set_tests_properties(original-m417-chrysler-load PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60
        LABELS "game-assets;room" ENVIRONMENT "PSYZ_HEADLESS=1;SDL_AUDIODRIVER=dummy")
    add_test(NAME original-m436-chrysler-load COMMAND test-original-actor-animation --m436-load)
    set_tests_properties(original-m436-chrysler-load PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60
        LABELS "game-assets;room" ENVIRONMENT "PSYZ_HEADLESS=1;SDL_AUDIODRIVER=dummy")
    add_test(NAME original-m430-cruiser-load COMMAND test-original-actor-animation --m430-load)
    set_tests_properties(original-m430-cruiser-load PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60
        LABELS "game-assets;room" ENVIRONMENT "PSYZ_HEADLESS=1;SDL_AUDIODRIVER=dummy")
    add_test(NAME original-m273-liberty-load COMMAND test-original-actor-animation --m273-load)
    set_tests_properties(original-m273-liberty-load PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60
        ENVIRONMENT "PSYZ_HEADLESS=1;SDL_AUDIODRIVER=dummy" LABELS "game-assets;render")
    add_test(NAME original-m245-trail-burst-load COMMAND test-original-actor-animation --m245-load)
    set_tests_properties(original-m245-trail-burst-load PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60
        ENVIRONMENT "PSYZ_HEADLESS=1;SDL_AUDIODRIVER=dummy" LABELS "render")
    add_test(NAME original-m205-sound-burst-load COMMAND test-original-actor-animation --m205-load)
    set_tests_properties(original-m205-sound-burst-load PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60
        ENVIRONMENT "PSYZ_HEADLESS=1;SDL_AUDIODRIVER=dummy" LABELS "render")
    add_test(NAME original-m221-spark-ring-burst-load COMMAND test-original-actor-animation --m221-load)
    set_tests_properties(original-m221-spark-ring-burst-load PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60
        ENVIRONMENT "PSYZ_HEADLESS=1;SDL_AUDIODRIVER=dummy" LABELS "render")
    add_test(NAME original-m269-grab-load COMMAND test-original-actor-animation --m269-load)
    set_tests_properties(original-m269-grab-load PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60
        ENVIRONMENT "PSYZ_HEADLESS=1;SDL_AUDIODRIVER=dummy" LABELS "render")
    add_test(NAME original-m186-twelve-orbit-load COMMAND test-original-actor-animation --m186-load)
    set_tests_properties(original-m186-twelve-orbit-load PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60
        ENVIRONMENT "PSYZ_HEADLESS=1;SDL_AUDIODRIVER=dummy" LABELS "render")
    add_test(NAME original-m188-spark-barrage-load COMMAND test-original-actor-animation --m188-load)
    set_tests_properties(original-m188-spark-barrage-load PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60
        ENVIRONMENT "PSYZ_HEADLESS=1;SDL_AUDIODRIVER=dummy" LABELS "render")
    add_test(NAME original-m187-orbit-load COMMAND test-original-actor-animation --m187-load)
    set_tests_properties(original-m187-orbit-load PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60
        ENVIRONMENT "PSYZ_HEADLESS=1;SDL_AUDIODRIVER=dummy" LABELS "render")
    add_test(NAME original-game-player-input-assets COMMAND test-original-actor-animation --assets)
    set_tests_properties(original-game-player-input-assets PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)

    pe_add_common_unit_test(test-original-floor original-game-floor tests/original_floor.c)
endif()

add_library(parasite-eve-original-camera OBJECT src/main/render/camera.c)
pe_configure_object_library(parasite-eve-original-camera)
target_sources(parasite-eve-pc PRIVATE $<TARGET_OBJECTS:parasite-eve-original-camera>)

if(BUILD_TESTING)
    pe_add_common_unit_test(test-original-camera original-game-camera tests/original_camera.c
        $<TARGET_OBJECTS:parasite-eve-original-disc>
        $<TARGET_OBJECTS:parasite-eve-original-camera>)
    set_tests_properties(original-game-camera PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
endif()

add_library(parasite-eve-original-pad OBJECT src/main/menu/widget.c src/main/menu/input_queue.c)
target_include_directories(parasite-eve-original-pad PRIVATE include)
target_link_libraries(parasite-eve-original-pad PRIVATE psyz)
foreach(pad_target IN LISTS akao_runtime_targets)
    target_sources(${pad_target} PRIVATE $<TARGET_OBJECTS:parasite-eve-original-pad>)
    if(NOT pad_target STREQUAL "parasite-eve-pc")
        target_sources(${pad_target} PRIVATE ${PE_COMMON_GAME_OBJECTS})
    endif()
endforeach()
if(BUILD_TESTING)
    target_sources(test-original-actor-animation PRIVATE $<TARGET_OBJECTS:parasite-eve-original-pad>)
    target_sources(test-original-input-rules PRIVATE $<TARGET_OBJECTS:parasite-eve-original-pad>)
    target_sources(test-host-settings PRIVATE $<TARGET_OBJECTS:parasite-eve-original-pad>
        $<TARGET_OBJECTS:parasite-eve-original-disc> $<TARGET_OBJECTS:parasite-eve-original-actor-animation>)

    pe_add_common_unit_test(test-original-actor-lifecycle original-game-actor-lifecycle tests/original_actor_lifecycle.c
        $<TARGET_OBJECTS:parasite-eve-original-akao>
        $<TARGET_OBJECTS:parasite-eve-original-camera>
        $<TARGET_OBJECTS:parasite-eve-original-disc>)
    if(MSVC)
        target_link_options(test-original-actor-lifecycle PRIVATE /OPT:REF)
    elseif(APPLE)
        target_link_options(test-original-actor-lifecycle PRIVATE -Wl,-dead_strip)
    else()
        target_compile_options(test-original-actor-lifecycle PRIVATE -ffunction-sections -fdata-sections)
        target_link_options(test-original-actor-lifecycle PRIVATE -Wl,--gc-sections)
    endif()
endif()

# Original area-bank state machine; native I/O binding is still pending.
add_library(parasite-eve-original-entity-textures OBJECT src/main/scene/room_load.c)
pe_configure_object_library(parasite-eve-original-entity-textures)
target_sources(parasite-eve-pc PRIVATE $<TARGET_OBJECTS:parasite-eve-original-entity-textures>)
if(BUILD_TESTING)
    target_sources(test-original-actor-animation PRIVATE $<TARGET_OBJECTS:parasite-eve-original-entity-textures>)

    # Compile the whole module; only this fixture replaces read/poll outcomes.
    # Rename its real I/O symbols to keep them distinct from the fixture doubles.
    add_library(parasite-eve-test-disc OBJECT src/main/cdrom/scene_assets.c)
    pe_configure_object_library(parasite-eve-test-disc)
    target_compile_definitions(parasite-eve-test-disc PRIVATE
        CdRom_ReadSectorsFromLba=CdRom_ImageReadSectorsFromLba
        CdRom_PollReady=CdRom_ImagePollReady
        Asset_Find08w=Asset_ImageFind08w)
endif()

add_library(parasite-eve-original-shadow OBJECT
    src/overlays/room_lib/mirror_model.c src/main/render/mesh_list.c src/main/render/mesh_entry.c)
pe_configure_object_library(parasite-eve-original-shadow)
target_compile_definitions(parasite-eve-original-shadow PRIVATE __psyz)
target_link_libraries(parasite-eve-original-shadow PRIVATE psyz)

if(BUILD_TESTING)
    # Resident script commands can open the original menu and render transition.
    # Link whole existing modules for every fixture that uses the real table.
    foreach(script_target test-original-queue test-original-movement test-original-actor-animation test-original-camera)
        target_sources(${script_target} PRIVATE $<TARGET_OBJECTS:parasite-eve-original-shadow>)
    endforeach()
    foreach(script_target test-original-queue test-original-movement)
        target_sources(${script_target} PRIVATE $<TARGET_OBJECTS:parasite-eve-original-pad>
            $<TARGET_OBJECTS:parasite-eve-original-entity-textures>)
    endforeach()

    pe_add_common_unit_test(test-original-shadow original-game-shadow tests/original_shadow.c
        $<TARGET_OBJECTS:parasite-eve-original-shadow>)
    target_compile_definitions(test-original-shadow PRIVATE __psyz)

    pe_add_common_unit_test(test-original-stat-assets original-game-stat-assets tests/original_stat_assets.c
        $<TARGET_OBJECTS:parasite-eve-original-disc>
        $<TARGET_OBJECTS:parasite-eve-original-shadow>
        $<TARGET_OBJECTS:parasite-eve-original-camera>
        $<TARGET_OBJECTS:parasite-eve-original-entity-textures>
        $<TARGET_OBJECTS:parasite-eve-original-pad>
        $<TARGET_OBJECTS:parasite-eve-original-actor-animation>
        $<TARGET_OBJECTS:parasite-eve-original-akao>)
    set_tests_properties(original-game-stat-assets PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)

    # Original-module integration runs without creating a window or GPU device.
    get_property(original_tests DIRECTORY PROPERTY TESTS)
    foreach(original_test IN LISTS original_tests)
        if(PE_ENABLE_COVERAGE)
            set_property(TEST ${original_test} APPEND PROPERTY ENVIRONMENT
                "LLVM_PROFILE_FILE=${CMAKE_CURRENT_BINARY_DIR}/profiles/${original_test}/%p.profraw")
            # Atomic pixel-loop counters are substantially slower than normal
            # execution; keep the same assertions with an instrumented budget.
            get_property(original_timeout TEST ${original_test} PROPERTY TIMEOUT)
            if(original_timeout)
                math(EXPR coverage_timeout "${original_timeout} * 4")
                set_property(TEST ${original_test} PROPERTY TIMEOUT ${coverage_timeout})
            endif()
        endif()
        if(original_test MATCHES "^original-game-")
            set_property(TEST ${original_test} APPEND PROPERTY ENVIRONMENT
                "PSYZ_HEADLESS=1" "SDL_AUDIODRIVER=dummy")
        endif()
    endforeach()

    if(MINGW AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
        # BFD keeps unused COFF functions alive through unwind records. Keep
        # the original fixture graph and its observer variants, compiling whole
        # original modules again for tests. Production objects retain unwind.
        function(pe_coff_fixture_module original_module output_module)
            set(fixture_module "${original_module}-coff-fixture")
            if(NOT TARGET ${fixture_module})
                get_target_property(fixture_files ${original_module} SOURCES)
                get_target_property(fixture_source_dir ${original_module} SOURCE_DIR)
                set(fixture_absolute_files)
                foreach(fixture_file IN LISTS fixture_files)
                    if(IS_ABSOLUTE "${fixture_file}" OR fixture_file MATCHES "^\\$<")
                        list(APPEND fixture_absolute_files "${fixture_file}")
                    else()
                        get_filename_component(fixture_absolute "${fixture_file}"
                            ABSOLUTE BASE_DIR "${fixture_source_dir}")
                        list(APPEND fixture_absolute_files "${fixture_absolute}")
                    endif()
                endforeach()
                add_library(${fixture_module} OBJECT ${fixture_absolute_files})
                foreach(fixture_property INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS LINK_LIBRARIES)
                    get_target_property(fixture_value ${original_module} ${fixture_property})
                    if(fixture_value)
                        set_property(TARGET ${fixture_module} PROPERTY ${fixture_property} "${fixture_value}")
                    endif()
                endforeach()
                target_compile_options(${fixture_module} PRIVATE
                    -fno-asynchronous-unwind-tables -fno-unwind-tables)
            endif()
            set(${output_module} ${fixture_module} PARENT_SCOPE)
        endfunction()
        get_property(native_test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
        foreach(native_test IN LISTS native_test_targets)
            if(native_test MATCHES "^test-")
                get_target_property(native_test_sources ${native_test} SOURCES)
                set(native_fixture_sources)
                foreach(native_source IN LISTS native_test_sources)
                    if(native_source MATCHES "^\\$<TARGET_OBJECTS:([^>]+)>$")
                        pe_coff_fixture_module(${CMAKE_MATCH_1} native_fixture_module)
                        list(APPEND native_fixture_sources "$<TARGET_OBJECTS:${native_fixture_module}>")
                    else()
                        list(APPEND native_fixture_sources ${native_source})
                    endif()
                endforeach()
                set_property(TARGET ${native_test} PROPERTY SOURCES ${native_fixture_sources})
                target_compile_options(${native_test} PRIVATE
                    -fno-asynchronous-unwind-tables -fno-unwind-tables)
            endif()
        endforeach()
    endif()
endif()
