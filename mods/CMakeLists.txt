# Mod host and mods, built only with -DPE_MODS=ON. See mods/README.md.
# Each mod is a folder here holding .c files and defining `const PeMod pe_mod_<folder>`.
# pe_hello is the example mod; add it to the list to build it.
set(PE_MOD_LIST pe_qol pe_rebalance pe_economy pe_rewards pe_catalog pe_shops pe_collection pe_wardrobe pe_stacks pe_warp pe_quicksave pe_cheats pe_randomizer CACHE STRING "Mods built into the game (folder names)")
# Existing build directories may still cache the now-parked language mod.
if("pe_lang" IN_LIST PE_MOD_LIST)
    list(REMOVE_ITEM PE_MOD_LIST pe_lang)
    set(PE_MOD_LIST "${PE_MOD_LIST}" CACHE STRING "Mods built into the game (folder names)" FORCE)
endif()

set(pe_host_sources
    host/pe_mod_host.c      # registry, hooks, services, events, logs
    host/pe_mod_menu.c      # Mods panel rows and pe_mods.ini
    host/pe_mod_panel.c     # the F1 panel: navigation and input
    host/pe_pages.c         # PE Enhancements page layout
    host/pe_game.c          # game access helpers
    host/pe_game_facts.c    # game facts as tables: effect names and groups, power tags
    host/pe_main_menu.c     # extra rows in Aya's main menu, screens from game widgets
    host/pe_equip_sync.c    # edited equipment reaches the next battle
    host/pe_save_query.c
    host/pe_save.c          # mods' per-save data: records the host saves, loads and carries
    host/pe_recovery_snapshot.c
    host/pe_legacy_guard.c
    host/pe_api_native.c
    host/pe_api_native_game.c
    host/pe_profile_transactions.c
    host/pe_inventory_api.c
    host/pe_shop_api.c
    host/pe_extension_store.c
    host/pe_checkpoint_game.c
    host/pe_economy_policy.c
    host/pe_content_api.c
    host/pe_registry.c      # mods' definitions: collected at start, sealed, then read-only
    host/pe_present.c       # image presenter: pictures pasted into the finished frame
    host/pe_disc_assets.c   # read-only game data from the player's disc (rooms)
    host/pe_model_skin.c    # isolated Wardrobe asset reads
    host/pe_model.c         # decoded game models: geometry, textures, idle animation
    host/pe_model_view.c    # a model posed, turned and drawn into a picture
    host/pe_model_stage.c   # a model on show: turning, idle animation, box or full screen
    host/ui/pe_ui_raster.c  # drawing: boxes and text
    host/ui/pe_ui_gpu.c     # the overlay quad
    host/ui/pe_toast.c)     # one-line messages

if(PE_PS5_PLUGINS AND NOT PE_PS5_GPU_NATIVE)
    list(REMOVE_ITEM pe_host_sources host/ui/pe_ui_gpu.c)
    list(APPEND pe_host_sources host/ui/pe_ui_ps5_cpu.c)
endif()
set(pe_mod_sources ${pe_host_sources})
set(pe_mod_decls "")
set(pe_mod_refs "")
foreach(mod IN LISTS PE_MOD_LIST)
    file(GLOB mod_files CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${mod}/*.c)
    list(APPEND pe_mod_sources ${mod_files})
    string(APPEND pe_mod_decls "extern const PeMod pe_mod_${mod};\n")
    string(APPEND pe_mod_refs "    &pe_mod_${mod},\n")
endforeach()
configure_file(pe_mod_list.c.in ${CMAKE_CURRENT_BINARY_DIR}/pe_mod_list.c @ONLY)

add_library(pe-mods OBJECT ${pe_mod_sources} ${CMAKE_CURRENT_BINARY_DIR}/pe_mod_list.c)
target_include_directories(pe-mods PRIVATE ${PROJECT_SOURCE_DIR}/include host
    "$<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>")
target_compile_definitions(pe-mods PRIVATE __psyz)
target_link_libraries(pe-mods PRIVATE psyz)
if(TARGET pe_download_json AND TARGET pe_download_zip)
    target_link_libraries(pe-mods PRIVATE pe_download_json pe_download_zip)
endif()

if(BUILD_TESTING AND "pe_qol" IN_LIST PE_MOD_LIST)
    add_executable(pe-mod-frequency
        ${PROJECT_SOURCE_DIR}/tests/pe_mod_frequency_test.c
        pe_qol/pe_battle_frequency.c
        pe_qol/pe_move.c)
    target_include_directories(pe-mod-frequency PRIVATE ${PROJECT_SOURCE_DIR}/include host pe_qol
        "$<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>")
    target_compile_definitions(pe-mod-frequency PRIVATE __psyz PE_MODS=1 PE_NATIVE_STATE_CPU_COMPONENTS=1)
    target_link_libraries(pe-mod-frequency PRIVATE psyz)
    add_test(NAME pe-mod-frequency COMMAND pe-mod-frequency)
endif()

if(BUILD_TESTING)
    add_executable(test-mod-settings ${PROJECT_SOURCE_DIR}/tests/original_input_rules.c)
    target_include_directories(test-mod-settings PRIVATE ${PROJECT_SOURCE_DIR}/include host)
    target_compile_definitions(test-mod-settings PRIVATE PE_MOD_SETTINGS_TEST=1 __psyz)
    target_link_libraries(test-mod-settings PRIVATE psyz)
    add_test(NAME mod-settings-save-safety COMMAND test-mod-settings "${CMAKE_CURRENT_BINARY_DIR}/settings-evidence")

    add_executable(test-mod-crash-tail ${PROJECT_SOURCE_DIR}/tests/pe_mod_crash_tail_test.c host/pe_mod_host.c host/pe_save.c host/pe_save_query.c host/pe_registry.c
        host/pe_legacy_guard.c host/pe_api_native.c host/pe_game_facts.c host/pe_content_api.c host/pe_economy_policy.c
        host/pe_recovery_snapshot.c host/pe_extension_store.c host/pe_inventory_api.c host/pe_shop_api.c host/pe_profile_transactions.c)
    target_include_directories(test-mod-crash-tail PRIVATE ${PROJECT_SOURCE_DIR}/include host
        "$<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>")
    target_compile_definitions(test-mod-crash-tail PRIVATE __psyz PE_MODS=1)
    target_link_libraries(test-mod-crash-tail PRIVATE psyz)
    add_test(NAME mod-crash-save-tail COMMAND test-mod-crash-tail)

    add_executable(test-mod-econ-store ${PROJECT_SOURCE_DIR}/tests/pe_mod_econ_store_test.c pe_economy/pe_econ_store.c pe_economy/pe_econ_config.c pe_economy/pe_econ_engine.c
        host/pe_mod_host.c host/pe_save.c host/pe_save_query.c host/pe_registry.c
        host/pe_legacy_guard.c host/pe_api_native.c host/pe_game_facts.c host/pe_content_api.c host/pe_economy_policy.c
        host/pe_recovery_snapshot.c host/pe_extension_store.c host/pe_inventory_api.c host/pe_shop_api.c host/pe_profile_transactions.c)
    target_include_directories(test-mod-econ-store PRIVATE ${PROJECT_SOURCE_DIR}/include host pe_economy
        "$<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>")
    target_compile_definitions(test-mod-econ-store PRIVATE __psyz PE_MODS=1 PE_ECON_STORE_TEST=1)
    target_compile_options(test-mod-econ-store PRIVATE $<IF:$<BOOL:${MSVC}>,/UNDEBUG,-UNDEBUG>)
    target_link_libraries(test-mod-econ-store PRIVATE psyz)
    add_test(NAME mod-econ-store COMMAND test-mod-econ-store)

    add_executable(test-mod-alone-save ${PROJECT_SOURCE_DIR}/tests/pe_mod_alone_save_test.c host/pe_mod_host.c host/pe_save.c host/pe_save_query.c host/pe_registry.c
        host/pe_legacy_guard.c host/pe_api_native.c host/pe_game_facts.c host/pe_content_api.c host/pe_economy_policy.c
        host/pe_recovery_snapshot.c host/pe_extension_store.c host/pe_inventory_api.c host/pe_shop_api.c host/pe_profile_transactions.c)
    target_include_directories(test-mod-alone-save PRIVATE ${PROJECT_SOURCE_DIR}/include host
        "$<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>")
    target_compile_definitions(test-mod-alone-save PRIVATE __psyz PE_MODS=1)
    target_link_libraries(test-mod-alone-save PRIVATE psyz)
    add_test(NAME mod-alone-save COMMAND test-mod-alone-save)
endif()

if(BUILD_TESTING AND "pe_qol" IN_LIST PE_MOD_LIST)
    add_executable(test-mod-inventory ${PROJECT_SOURCE_DIR}/tests/pe_mod_inventory_test.c
        pe_qol/pe_key_items.c pe_qol/pe_bag_sort.c pe_stacks/pe_stacks.c host/pe_save.c
        ${PROJECT_SOURCE_DIR}/src/main/item/armor_capacity.c)
    add_executable(test-mod-inventory-menu ${PROJECT_SOURCE_DIR}/tests/pe_mod_inventory_menu_test.c
        pe_qol/pe_inventory_menu.c)
    foreach(target test-mod-inventory test-mod-inventory-menu)
        target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/include host pe_qol)
        target_compile_definitions(${target} PRIVATE __psyz PE_MODS=1)
        target_link_libraries(${target} PRIVATE psyz)
        if(MSVC)
            target_compile_options(${target} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${target} PRIVATE -UNDEBUG)
        endif()
    endforeach()
    add_test(NAME mod-inventory COMMAND test-mod-inventory)
    add_test(NAME mod-inventory-menu COMMAND test-mod-inventory-menu)
endif()

if(BUILD_TESTING)
    add_executable(test-mod-save ${PROJECT_SOURCE_DIR}/tests/pe_mod_save_test.c host/pe_save.c)
    target_include_directories(test-mod-save PRIVATE ${PROJECT_SOURCE_DIR}/include host)
    target_compile_definitions(test-mod-save PRIVATE __psyz PE_MODS=1)
    target_link_libraries(test-mod-save PRIVATE psyz)
    add_test(NAME mod-save-records COMMAND test-mod-save)
    add_executable(test-portable-checkpoint ${PROJECT_SOURCE_DIR}/tests/pe_checkpoint_portable_test.c
        host/pe_save.c host/pe_extension_store.c)
    target_include_directories(test-portable-checkpoint PRIVATE ${PROJECT_SOURCE_DIR}/include host)
    target_compile_definitions(test-portable-checkpoint PRIVATE __psyz PE_MODS=1)
    target_link_libraries(test-portable-checkpoint PRIVATE psyz)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/portable-checkpoint-evidence")
    add_test(NAME mod-portable-checkpoint COMMAND test-portable-checkpoint
        "${CMAKE_CURRENT_BINARY_DIR}/portable-checkpoint-evidence")
endif()

if(BUILD_TESTING AND "pe_rebalance" IN_LIST PE_MOD_LIST)
    add_executable(test-mod-rebalance ${PROJECT_SOURCE_DIR}/tests/pe_mod_rebalance_test.c
        pe_rebalance/pe_mod_effects.c ${PROJECT_SOURCE_DIR}/src/main/battle/enemy_movement.c)
    target_include_directories(test-mod-rebalance PRIVATE ${PROJECT_SOURCE_DIR}/include host)
    target_compile_definitions(test-mod-rebalance PRIVATE __psyz PE_MODS=1)
    target_link_libraries(test-mod-rebalance PRIVATE psyz)
    if(MINGW)
        # COFF unwind tables retain unused entry points from the whole original module.
        # This fixture tests damage only; the game keeps its normal unwind metadata.
        target_compile_options(test-mod-rebalance PRIVATE -fno-asynchronous-unwind-tables)
    endif()
    add_test(NAME mod-rebalance-vanilla COMMAND test-mod-rebalance --vanilla)
    add_test(NAME mod-rebalance-saved-vanilla COMMAND test-mod-rebalance --saved-vanilla)
    add_test(NAME mod-rebalance-scoped COMMAND test-mod-rebalance --scoped)
    add_test(NAME mod-rebalance-poison COMMAND test-mod-rebalance --poison)
endif()
if(BUILD_TESTING)
    add_test(NAME mod-preserve-save-tail COMMAND test-mod-save --opaque)
    add_test(NAME mod-save-registration-bounds COMMAND test-mod-save --overflow)
endif()

if(BUILD_TESTING AND "pe_rebalance" IN_LIST PE_MOD_LIST)
    add_executable(test-mod-atb ${PROJECT_SOURCE_DIR}/tests/pe_mod_atb_test.c
        pe_rebalance/pe_atb_items.c pe_rebalance/pe_pe_balance.c host/pe_game_facts.c)
    add_executable(test-mod-combine ${PROJECT_SOURCE_DIR}/tests/pe_mod_combine_colour_test.c
        pe_rebalance/pe_combine.c pe_rebalance/pe_panacea.c)
    add_executable(test-mod-pe ${PROJECT_SOURCE_DIR}/tests/pe_mod_pe_test.c pe_rebalance/pe_pe_balance.c
        ${PROJECT_SOURCE_DIR}/src/main/item/actions.c)
    foreach(target test-mod-atb test-mod-combine test-mod-pe)
        target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/include host pe_rebalance)
        target_compile_definitions(${target} PRIVATE __psyz PE_MODS=1)
        target_link_libraries(${target} PRIVATE psyz)
    endforeach()
    if(MINGW)
        # Allow unused original item-module entry points to be discarded in this fixture.
        target_compile_options(test-mod-pe PRIVATE -fno-asynchronous-unwind-tables)
    endif()
    add_test(NAME mod-atb COMMAND test-mod-atb)
    add_test(NAME mod-combine COMMAND test-mod-combine)
    add_test(NAME mod-panacea-off COMMAND test-mod-combine --panacea-off)
    add_test(NAME mod-pe-vanilla COMMAND test-mod-pe --vanilla)
    add_test(NAME mod-pe-saved-vanilla COMMAND test-mod-pe --saved-vanilla)
    add_test(NAME mod-pe-cost COMMAND test-mod-pe --cost)
    add_test(NAME mod-pe-actor COMMAND test-mod-pe --actor)
endif()

if(BUILD_TESTING AND "pe_randomizer" IN_LIST PE_MOD_LIST)
    # Every room through the real loader; the step listing feeds mods/pe_randomizer/tools.
    add_executable(test-mod-randomizer-rooms ${PROJECT_SOURCE_DIR}/tests/pe_mod_randomizer_rooms_test.c
        $<TARGET_OBJECTS:parasite-eve-test-disc>
        $<TARGET_OBJECTS:parasite-eve-original-akao>
        $<TARGET_OBJECTS:parasite-eve-original-camera>
        ${PE_COMMON_GAME_OBJECTS}
        $<TARGET_OBJECTS:pe-mods>
        $<TARGET_OBJECTS:parasite-eve-original-actor-animation>
        $<TARGET_OBJECTS:parasite-eve-original-entity-textures>
        $<TARGET_OBJECTS:parasite-eve-original-pad>
        $<TARGET_OBJECTS:parasite-eve-original-shadow>)
    target_include_directories(test-mod-randomizer-rooms PRIVATE ${PROJECT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/pe_randomizer "$<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>")
    target_compile_definitions(test-mod-randomizer-rooms PRIVATE __psyz PE_MODS=1)
    target_link_libraries(test-mod-randomizer-rooms PRIVATE psyz)
    # Second file: the randomizer's spot table against the walk (sites, and the GiveItems it leaves alone).
    # Third: actor models, VRAM words and TIM rectangles (battle rando model swap).
    add_test(NAME mod-randomizer-rooms COMMAND test-mod-randomizer-rooms
        "${CMAKE_CURRENT_BINARY_DIR}/randomizer-rooms.txt" "${CMAKE_CURRENT_BINARY_DIR}/randomizer-spots.txt"
        "${CMAKE_CURRENT_BINARY_DIR}/randomizer-models.txt")
    set_tests_properties(mod-randomizer-rooms PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300 LABELS "game-assets"
        ENVIRONMENT "PSYZ_HEADLESS=1;SDL_AUDIODRIVER=dummy" PASS_REGULAR_EXPRESSION "Randomizer spots:")

    # The New York map (door target M0000): destinations, their enable bits and the room each pick loads.
    add_executable(test-mod-randomizer-exits ${PROJECT_SOURCE_DIR}/tests/pe_mod_randomizer_exits_test.c
        $<TARGET_OBJECTS:parasite-eve-test-disc>
        $<TARGET_OBJECTS:parasite-eve-original-akao>
        $<TARGET_OBJECTS:parasite-eve-original-camera>
        ${PE_COMMON_GAME_OBJECTS}
        $<TARGET_OBJECTS:pe-mods>
        $<TARGET_OBJECTS:parasite-eve-original-actor-animation>
        $<TARGET_OBJECTS:parasite-eve-original-entity-textures>
        $<TARGET_OBJECTS:parasite-eve-original-pad>
        $<TARGET_OBJECTS:parasite-eve-original-shadow>)
    target_include_directories(test-mod-randomizer-exits PRIVATE ${PROJECT_SOURCE_DIR}/include
        "$<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>")
    target_compile_definitions(test-mod-randomizer-exits PRIVATE __psyz PE_MODS=1)
    target_link_libraries(test-mod-randomizer-exits PRIVATE psyz)
    add_test(NAME mod-randomizer-exits COMMAND test-mod-randomizer-exits
        "${CMAKE_CURRENT_BINARY_DIR}/randomizer-exits.txt")
    # Only the summary line counts as a pass: a run cut short with exit code 0 must fail.
    set_tests_properties(mod-randomizer-exits PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300 LABELS "game-assets"
        ENVIRONMENT "PSYZ_HEADLESS=1;SDL_AUDIODRIVER=dummy" PASS_REGULAR_EXPRESSION "Randomizer exits:")

    # Battle rando models: enemy models, textures and rows swapped across rooms, through the real loader and spawn.
    add_executable(test-mod-randomizer-models ${PROJECT_SOURCE_DIR}/tests/pe_mod_randomizer_models_test.c
        $<TARGET_OBJECTS:parasite-eve-test-disc>
        $<TARGET_OBJECTS:parasite-eve-original-akao>
        $<TARGET_OBJECTS:parasite-eve-original-camera>
        ${PE_COMMON_GAME_OBJECTS}
        $<TARGET_OBJECTS:pe-mods>
        $<TARGET_OBJECTS:parasite-eve-original-actor-animation>
        $<TARGET_OBJECTS:parasite-eve-original-entity-textures>
        $<TARGET_OBJECTS:parasite-eve-original-pad>
        $<TARGET_OBJECTS:parasite-eve-original-shadow>)
    target_include_directories(test-mod-randomizer-models PRIVATE ${PROJECT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/pe_randomizer "$<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>")
    target_compile_definitions(test-mod-randomizer-models PRIVATE __psyz PE_MODS=1)
    target_link_libraries(test-mod-randomizer-models PRIVATE psyz)
    add_test(NAME mod-randomizer-models COMMAND test-mod-randomizer-models
        "${CMAKE_CURRENT_BINARY_DIR}/randomizer-models-swap.txt")
    set_tests_properties(mod-randomizer-models PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300 LABELS "game-assets"
        ENVIRONMENT "PSYZ_HEADLESS=1;SDL_AUDIODRIVER=dummy" PASS_REGULAR_EXPRESSION "Randomizer models:")
    # Room scripts run headless at given story values: which actors a room places, what its start writes.
    add_executable(test-mod-randomizer-run ${PROJECT_SOURCE_DIR}/tests/pe_mod_randomizer_run_test.c
        $<TARGET_OBJECTS:parasite-eve-test-disc>
        $<TARGET_OBJECTS:parasite-eve-original-akao>
        $<TARGET_OBJECTS:parasite-eve-original-camera>
        ${PE_COMMON_GAME_OBJECTS}
        $<TARGET_OBJECTS:pe-mods>
        $<TARGET_OBJECTS:parasite-eve-original-actor-animation>
        $<TARGET_OBJECTS:parasite-eve-original-entity-textures>
        $<TARGET_OBJECTS:parasite-eve-original-pad>
        $<TARGET_OBJECTS:parasite-eve-original-shadow>)
    target_include_directories(test-mod-randomizer-run PRIVATE ${PROJECT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/pe_randomizer "$<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>")
    target_compile_definitions(test-mod-randomizer-run PRIVATE __psyz PE_MODS=1)
    target_link_libraries(test-mod-randomizer-run PRIVATE psyz)
    # Without a runs file: every room once at V0x4a = 0. mods/pe_randomizer/tools/runlist.py writes a
    # fuller runs file from the story walk; run the test binary on it by hand (into randomizer-run.txt,
    # which this test's own output doesn't overwrite).
    add_test(NAME mod-randomizer-run COMMAND test-mod-randomizer-run
        "${CMAKE_CURRENT_BINARY_DIR}/randomizer-run-rooms.txt")
    set_tests_properties(mod-randomizer-run PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600 LABELS "game-assets"
        ENVIRONMENT "PSYZ_HEADLESS=1;SDL_AUDIODRIVER=dummy" PASS_REGULAR_EXPRESSION "Randomizer runs:")
    # The spots with a seed's layout patched in (tools/giveruns.py wrote the runs): each give hands out the
    # layout's item, and a spot's copy in another room gives nothing once its flag is set.
    add_test(NAME mod-randomizer-gives COMMAND test-mod-randomizer-run
        "${CMAKE_CURRENT_BINARY_DIR}/randomizer-gives.txt" "${CMAKE_CURRENT_SOURCE_DIR}/pe_randomizer/tools/gives-runs.txt")
    set_tests_properties(mod-randomizer-gives PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600 LABELS "game-assets"
        ENVIRONMENT "PSYZ_HEADLESS=1;SDL_AUDIODRIVER=dummy" PASS_REGULAR_EXPRESSION "Randomizer runs:")
    # The same on Disc 2's image (PE_TEST_DISC2; skipped without it): after the disc change the rooms load
    # from disc 2, and every give still hands out the layout's item.
    add_test(NAME mod-randomizer-gives-disc2 COMMAND test-mod-randomizer-run
        "${CMAKE_CURRENT_BINARY_DIR}/randomizer-gives-disc2.txt" "${CMAKE_CURRENT_SOURCE_DIR}/pe_randomizer/tools/gives-runs.txt")
    set_tests_properties(mod-randomizer-gives-disc2 PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600 LABELS "game-assets"
        ENVIRONMENT "PSYZ_HEADLESS=1;SDL_AUDIODRIVER=dummy;PE_RUN_DISC2=1" PASS_REGULAR_EXPRESSION "Randomizer runs:")
    # Story gates with the real mod (its host starts headless, files in randomizer-gate.txt.mods): a moved
    # key's lock answers "not held" until the story bit of the key's vanilla spot is set.
    add_test(NAME mod-randomizer-gate COMMAND test-mod-randomizer-run
        "${CMAKE_CURRENT_BINARY_DIR}/randomizer-gate.txt" "${CMAKE_CURRENT_SOURCE_DIR}/pe_randomizer/tools/gate-runs.txt")
    set_tests_properties(mod-randomizer-gate PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300 LABELS "game-assets"
        ENVIRONMENT "PSYZ_HEADLESS=1;SDL_AUDIODRIVER=dummy" PASS_REGULAR_EXPRESSION "Randomizer runs:")
    # The enemy option end to end (tools/foegame-runs.txt): with F1 Randomize enemies on, the mod's own New Game puts
    # in seed 123456's foe plan; each room it moves has exactly those swaps, and its fight runs them and is won.
    add_test(NAME mod-randomizer-foegame COMMAND test-mod-randomizer-run
        "${CMAKE_CURRENT_BINARY_DIR}/randomizer-foegame.txt" "${CMAKE_CURRENT_SOURCE_DIR}/pe_randomizer/tools/foegame-runs.txt")
    set_tests_properties(mod-randomizer-foegame PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300 LABELS "game-assets"
        ENVIRONMENT "PSYZ_HEADLESS=1;SDL_AUDIODRIVER=dummy" PASS_REGULAR_EXPRESSION "Randomizer runs:")
    # Story windows for the fill (tools/windowruns.py wrote the runs from the story walk): every table give
    # in every walk state of its room, every walked lock with and without its key. Data, no checks;
    # `windowruns.py table` turns the output into windows.txt.
    add_test(NAME mod-randomizer-windows COMMAND test-mod-randomizer-run
        "${CMAKE_CURRENT_BINARY_DIR}/randomizer-windows.txt" "${CMAKE_CURRENT_SOURCE_DIR}/pe_randomizer/tools/windows-runs.txt")
    set_tests_properties(mod-randomizer-windows PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600 LABELS "game-assets"
        ENVIRONMENT "PSYZ_HEADLESS=1;SDL_AUDIODRIVER=dummy" PASS_REGULAR_EXPRESSION "Randomizer runs:")
    # Chest revisits for Hard (tools/revisitruns.py wrote the runs): each chest of a key's day or earlier,
    # loaded at each story value where the key's lock asks, by its usual door. Data, no checks;
    # `revisitruns.py table` turns the output into revisit.txt.
    add_test(NAME mod-randomizer-revisit COMMAND test-mod-randomizer-run
        "${CMAKE_CURRENT_BINARY_DIR}/randomizer-revisit.txt" "${CMAKE_CURRENT_SOURCE_DIR}/pe_randomizer/tools/revisit-runs.txt")
    set_tests_properties(mod-randomizer-revisit PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600 LABELS "game-assets"
        ENVIRONMENT "PSYZ_HEADLESS=1;SDL_AUDIODRIVER=dummy" PASS_REGULAR_EXPRESSION "Randomizer runs:")
    # Hard's moved doors with the real mod (files in randomizer-doors.txt.mods): each fires to its new room,
    # the new room places Aya on its floor and its doors fire; on Day 1 the doors stay vanilla.
    add_test(NAME mod-randomizer-doors COMMAND test-mod-randomizer-run
        "${CMAKE_CURRENT_BINARY_DIR}/randomizer-doors.txt" "${CMAKE_CURRENT_SOURCE_DIR}/pe_randomizer/tools/door-runs.txt")
    set_tests_properties(mod-randomizer-doors PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300 LABELS "game-assets"
        ENVIRONMENT "PSYZ_HEADLESS=1;SDL_AUDIODRIVER=dummy" PASS_REGULAR_EXPRESSION "Randomizer runs:")
    # Hard's key walks (tools/hardwalk.sh wrote the runs, hardwalk-runs.txt in build-mods/mods): every door the
    # walks to Hard's key chests went through, at the key's lock-time values, still fires to the same room.
    add_test(NAME mod-randomizer-hardwalk COMMAND test-mod-randomizer-run
        "${CMAKE_CURRENT_BINARY_DIR}/randomizer-hardwalk.txt" "${CMAKE_CURRENT_SOURCE_DIR}/pe_randomizer/tools/hardwalk-runs.txt")
    set_tests_properties(mod-randomizer-hardwalk PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300 LABELS "game-assets"
        ENVIRONMENT "PSYZ_HEADLESS=1;SDL_AUDIODRIVER=dummy" PASS_REGULAR_EXPRESSION "Randomizer runs:")
    # Moved keys in chests on a full bag: cancelling leaves the key in the chest, the swap menu puts it in the bag.
    add_test(NAME mod-randomizer-bag COMMAND test-mod-randomizer-run
        "${CMAKE_CURRENT_BINARY_DIR}/randomizer-bag.txt" "${CMAKE_CURRENT_SOURCE_DIR}/pe_randomizer/tools/bag-runs.txt")
    set_tests_properties(mod-randomizer-bag PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300 LABELS "game-assets"
        ENVIRONMENT "PSYZ_HEADLESS=1;SDL_AUDIODRIVER=dummy" PASS_REGULAR_EXPRESSION "Randomizer runs:")
    # The shipped generator (key moves on; tests/run_pe_randomizer_test.ps1 builds the same), for its --layouts.
    add_executable(test-mod-randomizer-keys ${PROJECT_SOURCE_DIR}/tests/pe_mod_randomizer_test.c
        ${CMAKE_CURRENT_SOURCE_DIR}/pe_randomizer/pe_rand_gen.c)
    target_include_directories(test-mod-randomizer-keys PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/pe_randomizer)
    target_compile_definitions(test-mod-randomizer-keys PRIVATE PE_RAND_MOVE_KEYS=1)
    # Hard layouts of seeds 1..300 walked by tools/keysolve.py --hard (tools/keysolve_ctest.py). Needs the
    # story walk's outputs in build-mods/mods (tools/storyruns.sh, fixpoint.sh), else skipped.
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME mod-randomizer-keysolve COMMAND ${Python3_EXECUTABLE} -I
            ${CMAKE_CURRENT_SOURCE_DIR}/pe_randomizer/tools/keysolve_ctest.py $<TARGET_FILE:test-mod-randomizer-keys> 300
            story-graph.txt randomizer-exits.txt ${CMAKE_CURRENT_SOURCE_DIR}/pe_randomizer/pe_rand_spots.h
            --runs rwin/base-fix.txt --zones @rwin/zones.list --hard ${CMAKE_CURRENT_SOURCE_DIR}/pe_randomizer/pe_randomizer.c)
        set_tests_properties(mod-randomizer-keysolve PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 900 LABELS "story-walk"
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
        # Enemy census (tools/enemyscan.py) from the rooms listing into enemies.txt, checked against the
        # bestiary's creature keys. Reads the disc (PE_TEST_DISC) for starting actors only; no game window.
        add_test(NAME mod-randomizer-enemies COMMAND ${Python3_EXECUTABLE} -I
            ${CMAKE_CURRENT_SOURCE_DIR}/pe_randomizer/tools/enemyscan.py randomizer-rooms.txt
            ${CMAKE_CURRENT_SOURCE_DIR}/pe_collection/pe_collection_data.h --out enemies.txt --check)
        set_tests_properties(mod-randomizer-enemies PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600 LABELS "game-assets"
            DEPENDS mod-randomizer-rooms WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
        # Enemy AI scan (tools/enemyai.py): what each census actor's script needs from its room (PM slots,
        # entity numbers, spawner events and vars) and the conflicts per pair, into enemy-ai.txt. Reads the
        # disc for the rooms' PM slot records; no game window.
        add_test(NAME mod-randomizer-enemyai COMMAND ${Python3_EXECUTABLE} -I
            ${CMAKE_CURRENT_SOURCE_DIR}/pe_randomizer/tools/enemyai.py randomizer-rooms.txt enemies.txt
            --source ${PROJECT_SOURCE_DIR}/src/main/scene/room_load.c --out enemy-ai.txt --check)
        set_tests_properties(mod-randomizer-enemyai PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300 LABELS "game-assets"
            DEPENDS mod-randomizer-enemies PASS_REGULAR_EXPRESSION "Enemy AI: 0 problems"
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
        # Battle randomizer stats: fights run headless with the real mod and the enemy ops on; each room's own
        # setup matches pe_rand_enemies.h, and a forced actor takes another's stat block, the rest unchanged.
        add_test(NAME mod-randomizer-battlestats COMMAND ${Python3_EXECUTABLE} -I
            ${CMAKE_CURRENT_SOURCE_DIR}/pe_randomizer/tools/battlestats.py randomizer-battlestats.txt
            ${CMAKE_CURRENT_SOURCE_DIR}/pe_randomizer/pe_rand_enemies.h
            --run $<TARGET_FILE:test-mod-randomizer-run> ${CMAKE_CURRENT_SOURCE_DIR}/pe_randomizer/tools/battlestats-runs.txt)
        set_tests_properties(mod-randomizer-battlestats PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600 LABELS "game-assets"
            ENVIRONMENT "PSYZ_HEADLESS=1;SDL_AUDIODRIVER=dummy" PASS_REGULAR_EXPRESSION "Battle stats: .* 0 problems"
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
        # Battle randomizer AI: a forced actor runs another room's script (with its model and numbers) in a fight
        # run headless with the real mod; the run test checks the script was given and run and the fight won.
        add_test(NAME mod-randomizer-battleai COMMAND ${Python3_EXECUTABLE} -I
            ${CMAKE_CURRENT_SOURCE_DIR}/pe_randomizer/tools/battlestats.py randomizer-battleai.txt
            ${CMAKE_CURRENT_SOURCE_DIR}/pe_randomizer/pe_rand_enemies.h
            --run $<TARGET_FILE:test-mod-randomizer-run> ${CMAKE_CURRENT_SOURCE_DIR}/pe_randomizer/tools/battleai-runs.txt)
        set_tests_properties(mod-randomizer-battleai PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600 LABELS "game-assets"
            ENVIRONMENT "PSYZ_HEADLESS=1;SDL_AUDIODRIVER=dummy" PASS_REGULAR_EXPRESSION "Battle stats: .* 0 problems"
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
        # Battle randomizer seed plans (seeds 1-3, the real generator's): each room whose own fight is won headless
        # runs it with every moved slot's foe (script and model) and must win it (tools/foeruns.py: foefights*.txt).
        add_test(NAME mod-randomizer-foefights COMMAND ${Python3_EXECUTABLE} -I
            ${CMAKE_CURRENT_SOURCE_DIR}/pe_randomizer/tools/foeruns.py $<TARGET_FILE:test-mod-randomizer-keys>
            $<TARGET_FILE:test-mod-randomizer-run> 1,2,3 enemies.txt randomizer-rooms.txt
            ${CMAKE_CURRENT_SOURCE_DIR}/pe_randomizer/pe_rand_enemies.h
            --known ${CMAKE_CURRENT_SOURCE_DIR}/pe_randomizer/tools/foefights-known.txt)
        set_tests_properties(mod-randomizer-foefights PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600 LABELS "game-assets"
            ENVIRONMENT "PSYZ_HEADLESS=1;SDL_AUDIODRIVER=dummy" PASS_REGULAR_EXPRESSION "Foe fights: .* 0 problems"
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    endif()
endif()

# Feature regressions must run under CTest on every supported platform, rather
# than only through the standalone PowerShell helpers.
function(pe_feature_test name)
    add_executable(test-mod-${name} ${ARGN})
    target_include_directories(test-mod-${name} PRIVATE ${PROJECT_SOURCE_DIR}/include host pe_economy pe_catalog pe_shops pe_collection pe_wardrobe)
    target_compile_definitions(test-mod-${name} PRIVATE __psyz PE_MODS=1)
    target_link_libraries(test-mod-${name} PRIVATE psyz)
    if(MSVC)
        target_compile_options(test-mod-${name} PRIVATE /UNDEBUG)
    else()
        target_compile_options(test-mod-${name} PRIVATE -UNDEBUG)
    endif()
    if(MINGW)
        # Isolated fixtures include whole modules; discard unused entry points.
        target_compile_options(test-mod-${name} PRIVATE -fno-asynchronous-unwind-tables)
    endif()
    add_test(NAME mod-${name} COMMAND test-mod-${name})
endfunction()
if(BUILD_TESTING AND "pe_economy" IN_LIST PE_MOD_LIST)
    pe_feature_test(loot ${PROJECT_SOURCE_DIR}/tests/pe_mod_loot_test.c pe_economy/pe_econ_store.c pe_economy/pe_econ_config.c pe_economy/pe_econ_engine.c)
    pe_feature_test(loot-parity ${PROJECT_SOURCE_DIR}/tests/pe_mod_loot_parity_test.c pe_economy/pe_econ_store.c pe_economy/pe_econ_config.c pe_economy/pe_econ_engine.c)
    pe_feature_test(econ-config ${PROJECT_SOURCE_DIR}/tests/pe_mod_econ_config_test.c pe_economy/pe_econ_config.c)
    pe_feature_test(econ-engine ${PROJECT_SOURCE_DIR}/tests/pe_mod_econ_engine_test.c pe_economy/pe_econ_engine.c pe_economy/pe_econ_config.c)
    pe_feature_test(junk-counter ${PROJECT_SOURCE_DIR}/tests/pe_mod_junk_counter_test.c pe_economy/pe_junk_counter.c)
    pe_feature_test(armor-effect-display ${PROJECT_SOURCE_DIR}/tests/pe_mod_armor_effect_display_test.c pe_catalog/pe_armor_effect_display.c)
    pe_feature_test(catalog-rules ${PROJECT_SOURCE_DIR}/tests/pe_mod_catalog_rules_test.c pe_catalog/pe_catalog.c pe_catalog/pe_dismantle.c host/pe_legacy_guard.c host/pe_game_facts.c)
    pe_feature_test(catalog-menu ${PROJECT_SOURCE_DIR}/tests/pe_mod_catalog_menu_test.c)
    pe_feature_test(catalog-wallet ${PROJECT_SOURCE_DIR}/tests/pe_mod_catalog_wallet_test.c pe_catalog/pe_catalog_wallet.c)
    pe_feature_test(requisition ${PROJECT_SOURCE_DIR}/tests/pe_mod_requisition_test.c pe_shops/pe_requisition.c host/pe_save.c pe_shops/pe_requisition_menu.c)
    pe_feature_test(smoke ${PROJECT_SOURCE_DIR}/tests/pe_mod_smoke_test.c pe_economy/pe_smoke.c)
    pe_feature_test(workshop ${PROJECT_SOURCE_DIR}/tests/pe_mod_workshop_test.c pe_shops/pe_workshop_model.c)
endif()
if(BUILD_TESTING)
    pe_feature_test(registry ${PROJECT_SOURCE_DIR}/tests/pe_mod_registry_test.c host/pe_registry.c)
endif()
if(BUILD_TESTING AND "pe_wardrobe" IN_LIST PE_MOD_LIST)
    pe_feature_test(skin-bounds ${PROJECT_SOURCE_DIR}/tests/pe_mod_skin_bounds_test.c)
    pe_feature_test(wardrobe-menu ${PROJECT_SOURCE_DIR}/tests/pe_mod_wardrobe_menu_test.c)
    pe_feature_test(packs ${PROJECT_SOURCE_DIR}/tests/pe_mod_packs_test.c pe_wardrobe/pe_packs.c)
    target_link_libraries(test-mod-packs PRIVATE pe_download_json pe_download_zip)
    set_tests_properties(mod-packs PROPERTIES ENVIRONMENT "PE_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}")
endif()
if(BUILD_TESTING AND "pe_collection" IN_LIST PE_MOD_LIST AND "pe_wardrobe" IN_LIST PE_MOD_LIST)
    pe_feature_test(collection ${PROJECT_SOURCE_DIR}/tests/pe_mod_collection_test.c pe_collection/pe_collection.c pe_collection/pe_profile.c pe_collection/pe_bestiary.c pe_collection/pe_pack_pe1.c
        pe_wardrobe/pe_wardrobe_mod.c pe_wardrobe/pe_wardrobe_profile.c pe_wardrobe/pe_wardrobe.c pe_wardrobe/pe_pack_pe1_costumes.c)
    set_tests_properties(mod-collection PROPERTIES ENVIRONMENT "PE_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}")
endif()
if(BUILD_TESTING AND "pe_collection" IN_LIST PE_MOD_LIST)
    pe_feature_test(model ${PROJECT_SOURCE_DIR}/tests/pe_mod_model_test.c host/pe_model.c host/pe_model_skin.c host/pe_model_view.c)
    set_tests_properties(mod-model PROPERTIES ENVIRONMENT "PE_MODEL_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}")
    pe_feature_test(present ${PROJECT_SOURCE_DIR}/tests/pe_mod_present_test.c host/pe_present.c)
    pe_feature_test(stage ${PROJECT_SOURCE_DIR}/tests/pe_mod_stage_test.c host/pe_model_stage.c)
endif()

# Rust save-editor interoperability uses the actual C serializer, including the
# compatibility window. Keep assertions active in release builds of this oracle.
if(BUILD_TESTING)
    add_executable(test-editor-mod-save ${PROJECT_SOURCE_DIR}/tests/pe_editor_mod_fixture.c host/pe_save.c)
    target_include_directories(test-editor-mod-save PRIVATE ${PROJECT_SOURCE_DIR}/include host)
    target_compile_definitions(test-editor-mod-save PRIVATE __psyz PE_MODS=1)
    target_link_libraries(test-editor-mod-save PRIVATE psyz)
    if(MSVC)
        target_compile_options(test-editor-mod-save PRIVATE /UNDEBUG)
    else()
        target_compile_options(test-editor-mod-save PRIVATE -UNDEBUG)
    endif()
endif()

if(BUILD_TESTING)
    add_executable(test-ps5-checkpoint-session ${PROJECT_SOURCE_DIR}/tests/pe_ps5_checkpoint_session_test.c
        ${PROJECT_SOURCE_DIR}/platform/ps5/native-gpu/state/pe_state.c)
    target_include_directories(test-ps5-checkpoint-session PRIVATE ${PROJECT_SOURCE_DIR}/tests/ps5_state_stubs ${PROJECT_SOURCE_DIR}/include host
        ${PROJECT_SOURCE_DIR}/platform/ps5/native-gpu/state ${PSYZ_DIR}/psyz/include
        "$<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>")
    target_compile_definitions(test-ps5-checkpoint-session PRIVATE __psyz)
    add_test(NAME mod-ps5-checkpoint-session COMMAND test-ps5-checkpoint-session)
endif()
if(BUILD_TESTING AND WIN32)
    add_executable(test-preservation-regressions ${PROJECT_SOURCE_DIR}/tests/pe_preservation_regressions.c
        host/pe_registry.c host/pe_legacy_guard.c host/pe_api_native.c host/pe_game_facts.c
        host/pe_content_api.c host/pe_economy_policy.c host/pe_recovery_snapshot.c
        host/pe_extension_store.c host/pe_inventory_api.c host/pe_shop_api.c
        host/pe_profile_transactions.c host/pe_save.c host/pe_save_query.c)
    target_include_directories(test-preservation-regressions PRIVATE ${PROJECT_SOURCE_DIR}/include host)
    target_compile_definitions(test-preservation-regressions PRIVATE __psyz PE_MODS=1)
    foreach(scenario unknown-preservation definition-revision receipt-provider-off save-load
        cancelled-save missing-companion save-io-failure retention identity-refusal receipt-replay)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/preservation-${scenario}")
        add_test(NAME mod-preservation-${scenario} COMMAND test-preservation-regressions
            ${scenario} "${CMAKE_CURRENT_BINARY_DIR}/preservation-${scenario}")
    endforeach()
endif()

if(BUILD_TESTING)
    find_program(PE_COMPAT_NODE node)
    if(PE_COMPAT_NODE)
        add_test(NAME mod-ps5-compatibility-export COMMAND ${PE_COMPAT_NODE}
            ${PROJECT_SOURCE_DIR}/tests/pe_compatibility_export_test.cjs ${CMAKE_CURRENT_BINARY_DIR})
    endif()
endif()
