# Mod host and mods, built only with -DPE_MODS=ON. See mods/README.md.
# Each mod is a folder here holding .c files and defining `const PeMod pe_mod_<folder>`.
# pe_hello is the example mod; add it to the list to build it. Language packs
# are available in standard builds, but the original text remains the default and no pack ships.
set(pe_default_mods pe_qol pe_rebalance pe_economy pe_rewards pe_catalog pe_shops pe_collection pe_wardrobe pe_styles pe_stacks pe_warp pe_quicksave pe_cheats pe_phone)
if(NOT PE_PS5_GAME)
    list(APPEND pe_default_mods pe_lang)
endif()
set(PE_MOD_LIST ${pe_default_mods} CACHE STRING "Mods built into the game (folder names)")

# Off: a release holds no translated files. Players add .pelang packs themselves (lang/ beside the
# game, or F1 > Language > Add a translation). ON compiles the languages submitted under
# localization/ into lang/ beside the game, for work on them.
option(PE_BUNDLE_LANGUAGE_PACKS "Compile the submitted language packs into lang/ beside the game" OFF)
if(PE_BUNDLE_LANGUAGE_PACKS AND NOT PE_PS5_GAME AND "pe_lang" IN_LIST PE_MOD_LIST)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    if(APPLE AND PE_MACOS_BUNDLE)
        set(pe_language_pack_dir "$<TARGET_BUNDLE_CONTENT_DIR:parasite-eve-pc>/Resources/lang")
    else()
        set(pe_language_pack_dir "$<TARGET_FILE_DIR:parasite-eve-pc>/lang")
    endif()
    add_custom_target(pe-language-packs
        COMMAND "${Python3_EXECUTABLE}" -I "${PROJECT_SOURCE_DIR}/localization/check_translations.py"
            --out "${pe_language_pack_dir}"
        COMMENT "Validate and compile bundled language packs"
        VERBATIM)
    add_dependencies(parasite-eve-pc pe-language-packs)
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

# Phone Calls: the game's calls are a text file (pe_phone/calls/pe1.txt), built into the game as
# bytes; players get an editable copy in their pe_calls folder. Editing it reconfigures the build.
if("pe_phone" IN_LIST PE_MOD_LIST)
    set(pe_calls_txt ${CMAKE_CURRENT_SOURCE_DIR}/pe_phone/calls/pe1.txt)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${pe_calls_txt})
    file(READ ${pe_calls_txt} pe_calls_hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," pe_calls_bytes "${pe_calls_hex}")
    file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/pe_calls_builtin.c CONTENT
        "/* Generated from mods/pe_phone/calls/pe1.txt: edit that file, not this one. */\nconst unsigned char g_pe_calls_pe1_txt[] = {@pe_calls_bytes@ 0};\nconst unsigned g_pe_calls_pe1_txt_size = sizeof g_pe_calls_pe1_txt - 1;\n"
        @ONLY)
    list(APPEND pe_mod_sources ${CMAKE_CURRENT_BINARY_DIR}/pe_calls_builtin.c)
endif()

add_library(pe-mods OBJECT ${pe_mod_sources} ${CMAKE_CURRENT_BINARY_DIR}/pe_mod_list.c)
target_include_directories(pe-mods PRIVATE ${PROJECT_SOURCE_DIR}/include host
    "$<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>")
target_compile_definitions(pe-mods PRIVATE __psyz)
target_link_libraries(pe-mods PRIVATE psyz)
if(TARGET pe_download_json AND TARGET pe_download_zip)
    target_link_libraries(pe-mods PRIVATE pe_download_json pe_download_zip)
    if("pe_lang" IN_LIST PE_MOD_LIST)
        # Zipped .pelang files (pe_lang/pe_lang_bundle.c).
        target_compile_definitions(pe-mods PRIVATE PE_LANG_ZIP=1)
    endif()
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
    add_test(NAME mod-rebalance-club COMMAND test-mod-rebalance --club)
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
    target_include_directories(test-mod-${name} PRIVATE ${PROJECT_SOURCE_DIR}/include host pe_economy pe_catalog pe_shops pe_collection pe_wardrobe pe_phone)
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
if(BUILD_TESTING AND "pe_phone" IN_LIST PE_MOD_LIST)
    # The pure core and the game's own pack; the game side is covered by pe-native-debug's
    # scenarios/phone_calls.py.
    pe_feature_test(phone ${PROJECT_SOURCE_DIR}/tests/pe_mod_phone_test.c pe_phone/pe_phone_text.c
        pe_phone/pe_phone_record.c pe_phone/pe_phone_pick.c pe_phone/pe_phone_file.c)
    target_compile_definitions(test-mod-phone PRIVATE PE_PHONE_CALLS_FILE="${CMAKE_CURRENT_SOURCE_DIR}/pe_phone/calls/pe1.txt")
endif()
if(BUILD_TESTING AND "pe_styles" IN_LIST PE_MOD_LIST)
    pe_feature_test(combat-styles ${PROJECT_SOURCE_DIR}/tests/pe_mod_combat_styles_test.c
        pe_styles/pe_style_catalog.c pe_styles/pe_style_engine.c
        pe_styles/pe_style_quickstep.c pe_styles/pe_style_channeler.c
        pe_styles/pe_style_striker.c pe_styles/pe_style_bulwark.c)
endif()
if(BUILD_TESTING AND "pe_wardrobe" IN_LIST PE_MOD_LIST)
    pe_feature_test(skin-bounds ${PROJECT_SOURCE_DIR}/tests/pe_mod_skin_bounds_test.c)
    pe_feature_test(wardrobe-menu ${PROJECT_SOURCE_DIR}/tests/pe_mod_wardrobe_menu_test.c)
    # Match the standalone fixture: eliminate unused native inline helpers.
    target_compile_options(test-mod-wardrobe-menu PRIVATE $<IF:$<BOOL:${MSVC}>,/O1,-O1>)
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
    add_executable(test-mod-lang-bundle ${PROJECT_SOURCE_DIR}/tests/pe_mod_lang_bundle_test.c
        pe_lang/pe_lang_bundle.c pe_lang/pe_lang_pack.c pe_lang/pe_lang_pictures.c)
    target_include_directories(test-mod-lang-bundle PRIVATE pe_lang)
    if(TARGET pe_download_zip)
        target_link_libraries(test-mod-lang-bundle PRIVATE pe_download_zip)
        target_compile_definitions(test-mod-lang-bundle PRIVATE PE_LANG_ZIP=1)
    endif()
    if(MSVC)
        target_compile_options(test-mod-lang-bundle PRIVATE /UNDEBUG)
    else()
        target_compile_options(test-mod-lang-bundle PRIVATE -UNDEBUG)
    endif()
    add_test(NAME mod-lang-bundle COMMAND test-mod-lang-bundle)
    add_executable(test-mod-lang-names ${PROJECT_SOURCE_DIR}/tests/pe_mod_lang_names_test.c
        pe_lang/pe_lang_names.c pe_lang/pe_lang_pack.c pe_lang/pe_lang_pictures.c)
    target_include_directories(test-mod-lang-names PRIVATE pe_lang)
    add_test(NAME mod-lang-names COMMAND test-mod-lang-names)
    add_executable(test-mod-lang-font ${PROJECT_SOURCE_DIR}/tests/pe_mod_lang_font_test.c pe_lang/pe_lang_font.c pe_lang/pe_lang_cjk.c
        pe_lang/pe_lang_pack.c)
    target_include_directories(test-mod-lang-font PRIVATE pe_lang ${PROJECT_SOURCE_DIR}/tests)
    if(MSVC)
        target_compile_options(test-mod-lang-font PRIVATE /UNDEBUG)
    else()
        target_compile_options(test-mod-lang-font PRIVATE -UNDEBUG)
    endif()
    add_test(NAME mod-lang-font COMMAND test-mod-lang-font)
    # More letters than the 163 one-byte codes: pelang.py builds the pack, the game's reader and
    # letter cache take it
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME mod-lang-many-letters COMMAND "${Python3_EXECUTABLE}" -I
            "${PROJECT_SOURCE_DIR}/localization/tests/many_letters_pack.py" --check-with $<TARGET_FILE:test-mod-lang-font>)
    endif()
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
