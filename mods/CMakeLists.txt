# Mod host and mods, built only with -DPE_MODS=ON. See mods/README.md.
# Each mod is a folder here holding .c files and defining `const PeMod pe_mod_<folder>`.
# pe_hello is the example mod; add it to the list to build it. pe_lang (language packs) is
# opt-in too.
set(PE_MOD_LIST pe_qol pe_rebalance pe_economy pe_collection pe_stacks pe_warp pe_quicksave pe_cheats CACHE STRING "Mods built into the game (folder names)")

set(pe_host_sources
    host/pe_mod_host.c      # registry, hooks, services, events, logs
    host/pe_mod_menu.c      # Mods panel rows and pe_mods.ini
    host/pe_mod_panel.c     # the F1 panel: navigation and input
    host/pe_pages.c         # PE Enhancements page layout
    host/pe_game.c          # game access helpers
    host/pe_game_facts.c    # game facts as tables: effect names and groups, power tags
    host/pe_main_menu.c     # extra rows in Aya's main menu, screens from game widgets
    host/pe_equip_sync.c    # edited equipment reaches the next battle
    host/pe_save.c          # mods' per-save data: records the host saves, loads and carries
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

if(BUILD_TESTING AND "pe_qol" IN_LIST PE_MOD_LIST)
    add_executable(pe-mod-frequency
        ${PROJECT_SOURCE_DIR}/tests/pe_mod_frequency_test.c
        pe_qol/pe_battle_frequency.c
        pe_qol/pe_move.c)
    target_include_directories(pe-mod-frequency PRIVATE ${PROJECT_SOURCE_DIR}/include host pe_qol
        "$<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>")
    target_compile_definitions(pe-mod-frequency PRIVATE __psyz PE_MODS=1)
    target_link_libraries(pe-mod-frequency PRIVATE psyz)
    add_test(NAME pe-mod-frequency COMMAND pe-mod-frequency)
endif()

if(BUILD_TESTING)
    add_executable(test-mod-settings ${PROJECT_SOURCE_DIR}/tests/original_input_rules.c)
    target_include_directories(test-mod-settings PRIVATE ${PROJECT_SOURCE_DIR}/include host)
    target_compile_definitions(test-mod-settings PRIVATE PE_MOD_SETTINGS_TEST=1 __psyz)
    target_link_libraries(test-mod-settings PRIVATE psyz)
    add_test(NAME mod-settings-save-safety COMMAND test-mod-settings "${CMAKE_CURRENT_BINARY_DIR}/settings-evidence")

    add_executable(test-mod-crash-tail ${PROJECT_SOURCE_DIR}/tests/pe_mod_crash_tail_test.c host/pe_mod_host.c host/pe_save.c)
    target_include_directories(test-mod-crash-tail PRIVATE ${PROJECT_SOURCE_DIR}/include host
        "$<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>")
    target_compile_definitions(test-mod-crash-tail PRIVATE __psyz PE_MODS=1)
    target_link_libraries(test-mod-crash-tail PRIVATE psyz)
    add_test(NAME mod-crash-save-tail COMMAND test-mod-crash-tail)
endif()

if(BUILD_TESTING AND "pe_qol" IN_LIST PE_MOD_LIST)
    add_executable(test-mod-inventory ${PROJECT_SOURCE_DIR}/tests/pe_mod_inventory_test.c
        pe_qol/pe_key_items.c pe_qol/pe_bag_sort.c pe_stacks/pe_stacks.c host/pe_save.c
        ${PROJECT_SOURCE_DIR}/src/main/item/armor_capacity.c)
    add_executable(test-mod-inventory-menu ${PROJECT_SOURCE_DIR}/tests/pe_mod_inventory_menu_test.c
        pe_qol/pe_inventory_menu.c)
    foreach(target test-mod-inventory test-mod-inventory-menu)
        target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/include host pe_qol)
        target_compile_definitions(${target} PRIVATE __psyz PE_MODS=1 PE_INTERFACE_TEXT_FALLBACK=1)
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
        target_compile_definitions(${target} PRIVATE __psyz PE_MODS=1 PE_INTERFACE_TEXT_FALLBACK=1)
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

# Feature regressions must run under CTest on every supported platform, rather
# than only through the standalone PowerShell helpers.
function(pe_feature_test name)
    add_executable(test-mod-${name} ${ARGN})
    target_include_directories(test-mod-${name} PRIVATE ${PROJECT_SOURCE_DIR}/include host pe_economy pe_collection)
    # These isolated fixtures have no service host; full interface checks stay in pe-native-debug.
    target_compile_definitions(test-mod-${name} PRIVATE __psyz PE_MODS=1 PE_INTERFACE_TEXT_FALLBACK=1)
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
    pe_feature_test(loot ${PROJECT_SOURCE_DIR}/tests/pe_mod_loot_test.c)
    pe_feature_test(junk-counter ${PROJECT_SOURCE_DIR}/tests/pe_mod_junk_counter_test.c pe_economy/pe_junk_counter.c)
    pe_feature_test(armor-effect-display ${PROJECT_SOURCE_DIR}/tests/pe_mod_armor_effect_display_test.c pe_economy/pe_armor_effect_display.c)
    pe_feature_test(catalog-rules ${PROJECT_SOURCE_DIR}/tests/pe_mod_catalog_rules_test.c pe_economy/pe_catalog.c pe_economy/pe_dismantle.c host/pe_game_facts.c)
    pe_feature_test(catalog-menu ${PROJECT_SOURCE_DIR}/tests/pe_mod_catalog_menu_test.c)
    pe_feature_test(requisition ${PROJECT_SOURCE_DIR}/tests/pe_mod_requisition_test.c pe_economy/pe_requisition.c host/pe_save.c pe_economy/pe_requisition_menu.c)
    pe_feature_test(smoke ${PROJECT_SOURCE_DIR}/tests/pe_mod_smoke_test.c pe_economy/pe_smoke.c)
endif()
if(BUILD_TESTING AND "pe_collection" IN_LIST PE_MOD_LIST)
    pe_feature_test(skin-bounds ${PROJECT_SOURCE_DIR}/tests/pe_mod_skin_bounds_test.c)
    pe_feature_test(collection ${PROJECT_SOURCE_DIR}/tests/pe_mod_collection_test.c pe_collection/pe_collection.c pe_collection/pe_profile.c pe_collection/pe_bestiary.c pe_collection/pe_wardrobe.c pe_collection/pe_pack_pe1.c)
    set_tests_properties(mod-collection PROPERTIES ENVIRONMENT "PE_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}")
    pe_feature_test(model ${PROJECT_SOURCE_DIR}/tests/pe_mod_model_test.c host/pe_model.c host/pe_model_skin.c host/pe_model_view.c)
    set_tests_properties(mod-model PROPERTIES ENVIRONMENT "PE_MODEL_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}")
    pe_feature_test(wardrobe-menu ${PROJECT_SOURCE_DIR}/tests/pe_mod_wardrobe_menu_test.c)
    pe_feature_test(present ${PROJECT_SOURCE_DIR}/tests/pe_mod_present_test.c host/pe_present.c)
    pe_feature_test(stage ${PROJECT_SOURCE_DIR}/tests/pe_mod_stage_test.c host/pe_model_stage.c)
endif()

# The language pack reader is plain data code, so its test runs whichever mods are built.
if(BUILD_TESTING)
    add_executable(test-mod-lang-pack ${PROJECT_SOURCE_DIR}/tests/pe_mod_lang_test.c pe_lang/pe_lang_pack.c)
    target_include_directories(test-mod-lang-pack PRIVATE pe_lang)
    if(MSVC)
        target_compile_options(test-mod-lang-pack PRIVATE /UNDEBUG)
    else()
        target_compile_options(test-mod-lang-pack PRIVATE -UNDEBUG)
    endif()
    add_test(NAME mod-lang-pack COMMAND test-mod-lang-pack)
    add_executable(test-mod-lang-font ${PROJECT_SOURCE_DIR}/tests/pe_mod_lang_font_test.c pe_lang/pe_lang_font.c)
    target_include_directories(test-mod-lang-font PRIVATE pe_lang ${PROJECT_SOURCE_DIR}/tests)
    if(MSVC)
        target_compile_options(test-mod-lang-font PRIVATE /UNDEBUG)
    else()
        target_compile_options(test-mod-lang-font PRIVATE -UNDEBUG)
    endif()
    add_test(NAME mod-lang-font COMMAND test-mod-lang-font)
    add_executable(test-mod-lang-selection ${PROJECT_SOURCE_DIR}/tests/pe_mod_lang_selection_test.c
        pe_lang/pe_lang_selection.c)
    target_include_directories(test-mod-lang-selection PRIVATE pe_lang)
    target_link_libraries(test-mod-lang-selection PRIVATE SDL3::SDL3)
    if(MSVC)
        target_compile_options(test-mod-lang-selection PRIVATE /UNDEBUG)
    else()
        target_compile_options(test-mod-lang-selection PRIVATE -UNDEBUG)
    endif()
    add_test(NAME mod-lang-selection COMMAND test-mod-lang-selection)
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
