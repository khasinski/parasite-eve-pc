# Mod host and mods, built only with -DPE_MODS=ON. See mods/README.md.
# Each mod is a folder here holding .c files and defining `const PeMod pe_mod_<folder>`.
# pe_hello is the example mod; add it to the list to build it. Language packs
# are available in standard builds, but the original text remains the default and no pack ships.
# pe_fonts adds F1 > Font (Original, or a picture from mods/fonts/); Original is the default.
set(pe_default_mods pe_qol pe_rebalance pe_economy pe_rewards pe_catalog pe_shops pe_collection pe_wardrobe pe_styles pe_stacks pe_warp pe_quicksave pe_cheats pe_phone
    pe_src_re1 pe_src_re2 pe_src_re3 pe_src_dc1 pe_import)   # F1 > Import: source games first, then the page
if(NOT PE_PS5_GAME)
    list(PREPEND pe_default_mods pe_assets)
    list(REMOVE_ITEM pe_default_mods pe_import)
    list(APPEND pe_default_mods pe_src_ff7 pe_import pe_lang pe_fonts pe_enemy_import pe_rooms)   # pe_enemy_import: F1 > Fights; pe_rooms: RE rooms (prototype); desktop only
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

# F1 > Font reads mods/fonts/ beside the game; copy the shipped pictures there.
if("pe_fonts" IN_LIST PE_MOD_LIST)
    if(APPLE AND PE_MACOS_BUNDLE)
        set(pe_font_dir "$<TARGET_BUNDLE_CONTENT_DIR:parasite-eve-pc>/Resources/mods/fonts")
    else()
        set(pe_font_dir "$<TARGET_FILE_DIR:parasite-eve-pc>/mods/fonts")
    endif()
    add_custom_target(pe-font-files
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/fonts" "${pe_font_dir}"
        COMMENT "Copy the font pictures beside the game"
        VERBATIM)
    add_dependencies(parasite-eve-pc pe-font-files)
endif()

# The importer core (design: Cross-Game Asset Import). Plain C, PC and PS5. Source games register
# their readers from their own mods (pe_src_<game>); nothing here names a game.
set(pe_import_core_sources
    host/import/pe_imp_disc.c       # PS1 disc images: .cue / .bin / .iso, ISO 9660
    host/import/pe_imp_rofs.c       # RE3 PC rofs*.dat archives, a game folder read like a disc
    host/import/pe_imp_json.c       # a small bounded JSON reader and writer (no cJSON on PS5)
    host/import/pe_imp_sha256.c     # store keys
    host/import/pe_imp_model.c      # the Model type: check, store, load
    host/import/pe_imp_anim.c       # the Animation type: moves with per-frame events
    host/import/pe_imp_sound.c      # the Sound type: bounded PCM
    host/import/pe_imp_song.c       # the Song type: sequenced source music
    host/import/pe_imp_imageset.c   # the ImageSet type: still pictures (room backgrounds)
    host/import/pe_imp_spu.c        # shared PS1 sound decoder
    host/import/pe_imp_seq.c        # PS1 sequences (SEQ + VAB) played as PCM
    host/import/pe_imp_xa.c         # CD-XA streamed audio: decoding and playback
    host/import/pe_imp_akao.c       # Square's AKAO sequences (FF7) played as PCM
    host/import/pe_imp_mdec.c       # MDEC frames (room backgrounds) to RGB
    host/import/pe_imp_rdt.c        # RE1-3 rooms: doors, trigger zones, items, camera zones
    host/import/pe_imp_logic.c      # the RoomLogic type: what happens in a room, for the runtime
    host/import/pe_imp_door.c       # RE door transitions as DoorScene records
    host/import/pe_imp_items.c      # a game's item table (names, icons)
    host/import/pe_imp_movie.c      # PS1 STR movies, copied once
    host/import/pe_imp_pictures.c   # TIM pictures (files, maps) kept as stored, and a TIM decoder
    host/import/pe_imp_room.c       # Resident Evil PS1 cameras and foreground masks
    host/import/pe_imp_registry.c   # readers, file patterns, naming tables
    host/import/pe_imp_store.c      # the extract-once store and the catalog
    host/import/pe_imp_packs.c      # packs in the format the Wardrobe loads
    host/import/pe_imp_pe_disc.c    # Aya's bundles and rooms from the player's PE disc images
    host/import/pe_imp_pe_model.c   # PE models and animation rows: decode, encode, pose
    host/import/pe_imp_pe_retarget.c)   # Aya's rows on another skeleton; the rows rooms install

set(pe_debug_sources
    host/pe_debug_control.c
    host/debug/pe_debug_field.c
    host/debug/pe_debug_save.c
    host/debug/pe_debug_battle.c
    host/debug/pe_debug_rng.c
    host/debug/pe_debug_setup.c
    host/debug/pe_debug_settings.c
    host/debug/pe_debug_trace.c
)

set(pe_host_sources
    host/pe_mod_host.c      # registry, hooks, services, events, logs
    host/pe_mod_menu.c      # Mods panel rows and pe_mods.ini
    host/pe_mod_panel.c     # the F1 panel: navigation and input
    host/pe_pages.c         # PE Enhancements page layout
    host/pe_game.c          # game access helpers
    host/pe_encounter.c     # random encounters in room bytecode
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
    ${pe_import_core_sources}   # the importer core: other games' disc images, import mode only
    ${pe_debug_sources}    # opt-in transport and independently compiled command modules
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
set(PE_EXTRA_MODS "" CACHE STRING "Additional mods appended to the configured suite")
set(pe_enabled_mods ${PE_MOD_LIST} ${PE_EXTRA_MODS})
list(REMOVE_DUPLICATES pe_enabled_mods)
set(pe_mod_sources ${pe_host_sources})
set(pe_mod_decls "")
set(pe_mod_refs "")
foreach(mod IN LISTS pe_enabled_mods)
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
target_include_directories(pe-mods PRIVATE ${PROJECT_SOURCE_DIR}/include host host/import
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
        host/pe_encounter.c
        pe_qol/pe_move.c)
    target_include_directories(pe-mod-frequency PRIVATE ${PROJECT_SOURCE_DIR}/include host pe_qol
        "$<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>")
    target_compile_definitions(pe-mod-frequency PRIVATE __psyz PE_MODS=1 PE_NATIVE_STATE_CPU_COMPONENTS=1)
    target_link_libraries(pe-mod-frequency PRIVATE psyz)
    add_test(NAME pe-mod-frequency COMMAND pe-mod-frequency)
endif()

# Font pictures: the reader, the page writes, and the shipped mods/fonts/ pictures.
if(BUILD_TESTING AND "pe_fonts" IN_LIST PE_MOD_LIST)
    add_executable(test-mod-fonts ${PROJECT_SOURCE_DIR}/tests/pe_mod_fonts_test.c pe_fonts/pe_fonts_sheet.c)
    target_include_directories(test-mod-fonts PRIVATE pe_fonts)
    target_compile_options(test-mod-fonts PRIVATE $<IF:$<BOOL:${MSVC}>,/UNDEBUG,-UNDEBUG>)
    target_link_libraries(test-mod-fonts PRIVATE SDL3::SDL3)
    add_test(NAME mod-fonts COMMAND test-mod-fonts "${PROJECT_SOURCE_DIR}/mods/fonts")
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
if(BUILD_TESTING)
    # Importer core and the RE2 reader on made-up data; PE_TEST_RE2_DISC adds the real disc.
    pe_feature_test(import ${PROJECT_SOURCE_DIR}/tests/pe_mod_import_test.c ${pe_import_core_sources}
        pe_src_re2/pe_src_re2_pld.c pe_src_re2/pe_src_re2_sound.c pe_src_re2/pe_src_re2_music.c)
    target_include_directories(test-mod-import PRIVATE host/import pe_src_re2)
    if(NOT PE_PS5_GAME)
        pe_feature_test(assets ${PROJECT_SOURCE_DIR}/tests/pe_mod_assets_test.c
            pe_assets/pe_assets.c ${pe_import_core_sources})
        target_include_directories(test-mod-assets PRIVATE host/import)
    endif()
    # The RE3 reader on a made-up person; PE_TEST_RE3_DISC adds every player file on the disc.
    pe_feature_test(re3 ${PROJECT_SOURCE_DIR}/tests/pe_mod_re3_test.c ${pe_import_core_sources}
        pe_src_re3/pe_src_re3_pld.c)
    target_include_directories(test-mod-re3 PRIVATE host/import pe_src_re3)
    # RE3's enemies: the PC folder (rofs archives), EMD models with their moves, enemy sound banks,
    # on made-up data; PE_TEST_RE3_PC (the RE3 PC folder) adds Nemesis (EM34, E_34) read for real.
    pe_feature_test(re3-enemy ${PROJECT_SOURCE_DIR}/tests/pe_mod_re3_enemy_test.c ${pe_import_core_sources}
        pe_src_re3/pe_src_re3_emd.c pe_src_re3/pe_src_re3_tones.c pe_src_re3/pe_src_re3_enemies.c pe_src_re3/pe_src_re3_pld.c)
    target_include_directories(test-mod-re3-enemy PRIVATE host/import pe_src_re3)
    set_tests_properties(mod-re3-enemy PROPERTIES ENVIRONMENT "PE_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}")
    # The RE1 reader on a made-up person; PE_TEST_RE1_DISC adds the four people on the disc.
    pe_feature_test(re1 ${PROJECT_SOURCE_DIR}/tests/pe_mod_re1_test.c ${pe_import_core_sources}
        pe_src_re1/pe_src_re1_emd.c)
    target_include_directories(test-mod-re1 PRIVATE host/import pe_src_re1)
    # The Dino Crisis reader on a made-up archive; PE_TEST_DC1_DISC adds Regina's four outfits.
    pe_feature_test(dc1 ${PROJECT_SOURCE_DIR}/tests/pe_mod_dc1_test.c ${pe_import_core_sources}
        pe_src_dc1/pe_src_dc1_archive.c pe_src_dc1/pe_src_dc1_core.c pe_src_dc1/pe_src_dc1_sound.c)
    target_include_directories(test-mod-dc1 PRIVATE host/import pe_src_dc1)
    # Music playback on made-up songs; PE_TEST_RE2_DISC adds RE2's MAIN00 (PE_TEST_SEQ_WAV writes it out).
    pe_feature_test(seq ${PROJECT_SOURCE_DIR}/tests/pe_mod_seq_test.c ${pe_import_core_sources}
        pe_src_re2/pe_src_re2_music.c pe_src_re1/pe_src_re1_music.c pe_src_re3/pe_src_re3_music.c)
    target_include_directories(test-mod-seq PRIVATE host/import pe_src_re2 pe_src_re1 pe_src_re3)
    # Streamed (CD-XA) audio on made-up sectors; PE_TEST_DC1_DISC adds Dino Crisis's music.
    pe_feature_test(xa ${PROJECT_SOURCE_DIR}/tests/pe_mod_xa_test.c ${pe_import_core_sources}
        pe_src_dc1/pe_src_dc1_music.c)
    target_include_directories(test-mod-xa PRIVATE host/import pe_src_dc1)
    # AKAO (FF7) playback on made-up songs; PE_TEST_FF7_DISC adds FF7 CD1's music.
    pe_feature_test(akao ${PROJECT_SOURCE_DIR}/tests/pe_mod_akao_test.c ${pe_import_core_sources}
        pe_src_ff7/pe_src_ff7.c pe_src_ff7/pe_src_ff7_music.c)
    target_include_directories(test-mod-akao PRIVATE host/import pe_src_ff7)
    # MDEC frames and room backgrounds; PE_TEST_RE1/2/3_DISC decode every room view on those discs
    # (PE_TEST_ROOMS_DIR writes them out).
    pe_feature_test(mdec ${PROJECT_SOURCE_DIR}/tests/pe_mod_mdec_test.c ${pe_import_core_sources}
        pe_src_re1/pe_src_re1_rooms.c pe_src_re2/pe_src_re2_rooms.c pe_src_re3/pe_src_re3_rooms.c
        ${PROJECT_SOURCE_DIR}/src/overlays/mdec_diag/vlc_build.c)
    target_include_directories(test-mod-mdec PRIVATE host/import pe_src_re1 pe_src_re2 pe_src_re3)
    # RE2/RE3 room logic on a made-up room; PE_TEST_RE2_DISC / PE_TEST_RE3_DISC read every room's scripts.
    pe_feature_test(rdt ${PROJECT_SOURCE_DIR}/tests/pe_mod_rdt_test.c ${pe_import_core_sources}
        pe_src_re2/pe_src_re2_logic.c pe_src_re1/pe_src_re1_door.c pe_src_re2/pe_src_re2_door.c pe_src_re3/pe_src_re3_door.c
        pe_src_re1/pe_src_re1_items.c pe_src_re2/pe_src_re2_items.c pe_src_re3/pe_src_re3_items.c pe_src_re3/pe_src_re3_logic.c pe_src_re3/pe_src_re3_music.c
        pe_src_re1/pe_src_re1_movies.c pe_src_re2/pe_src_re2_movies.c pe_src_re3/pe_src_re3_movies.c
        pe_src_re1/pe_src_re1_files.c pe_src_re2/pe_src_re2_files.c pe_src_re3/pe_src_re3_files.c
        ${PROJECT_SOURCE_DIR}/src/overlays/mdec_diag/vlc_build.c)
    target_include_directories(test-mod-rdt PRIVATE host/import pe_src_re1 pe_src_re2 pe_src_re3)
    # The skins builder against tools/re2-skin's output: needs PE_TEST_DISC and PE_TEST_RE2_DISC.
    pe_feature_test(skin-build ${PROJECT_SOURCE_DIR}/tests/pe_mod_skin_build_test.c ${pe_import_core_sources}
        pe_src_re2/pe_src_re2_pld.c pe_wardrobe/pe_skin_build.c)
    target_include_directories(test-mod-skin-build PRIVATE host/import pe_src_re2)
    target_compile_definitions(test-mod-skin-build PRIVATE "PE_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
    set_tests_properties(mod-skin-build PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)
    if(TARGET pe_download_json AND TARGET pe_download_zip)
        # F1 > Import's scan and build, then the Wardrobe's pack reader loading the result.
        pe_feature_test(import-flow ${PROJECT_SOURCE_DIR}/tests/pe_mod_import_flow_test.c ${pe_import_core_sources}
            pe_src_re1/pe_src_re1_emd.c pe_src_re1/pe_src_re1_sound.c pe_src_re1/pe_src_re1_music.c pe_src_re1/pe_src_re1_rooms.c pe_src_re1/pe_src_re1_logic.c pe_src_re1/pe_src_re1_door.c pe_src_re1/pe_src_re1_items.c pe_src_re1/pe_src_re1_movies.c pe_src_re1/pe_src_re1_files.c pe_src_re1/pe_src_re1.c pe_src_re2/pe_src_re2_pld.c pe_src_re2/pe_src_re2_sound.c pe_src_re2/pe_src_re2_music.c pe_src_re2/pe_src_re2_rooms.c pe_src_re2/pe_src_re2_logic.c pe_src_re2/pe_src_re2_door.c pe_src_re2/pe_src_re2_items.c pe_src_re2/pe_src_re2_movies.c pe_src_re2/pe_src_re2_files.c pe_src_re2/pe_src_re2.c
            pe_src_re3/pe_src_re3_pld.c pe_src_re3/pe_src_re3_sound.c pe_src_re3/pe_src_re3_music.c pe_src_re3/pe_src_re3_rooms.c pe_src_re3/pe_src_re3_logic.c pe_src_re3/pe_src_re3_door.c pe_src_re3/pe_src_re3_items.c pe_src_re3/pe_src_re3_movies.c pe_src_re3/pe_src_re3_files.c pe_src_re3/pe_src_re3.c
            pe_src_re3/pe_src_re3_emd.c pe_src_re3/pe_src_re3_tones.c pe_src_re3/pe_src_re3_enemies.c
            pe_src_dc1/pe_src_dc1_archive.c pe_src_dc1/pe_src_dc1_core.c pe_src_dc1/pe_src_dc1_sound.c pe_src_dc1/pe_src_dc1_music.c pe_src_dc1/pe_src_dc1.c
            pe_wardrobe/pe_skin_build.c pe_wardrobe/pe_skin_target.c
            pe_wardrobe/pe_packs.c pe_import/pe_import_job.c)
        target_include_directories(test-mod-import-flow PRIVATE host/import pe_src_re1 pe_src_re2 pe_src_re3 pe_src_dc1 pe_import)
        target_compile_definitions(test-mod-import-flow PRIVATE "PE_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
        target_link_libraries(test-mod-import-flow PRIVATE pe_download_json pe_download_zip)
        set_tests_properties(mod-import-flow PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600
            ENVIRONMENT "PE_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}")
    endif()
endif()
if(BUILD_TESTING AND "pe_economy" IN_LIST PE_MOD_LIST)
    pe_feature_test(loot ${PROJECT_SOURCE_DIR}/tests/pe_mod_loot_test.c pe_economy/pe_econ_store.c pe_economy/pe_econ_config.c pe_economy/pe_econ_engine.c)
    pe_feature_test(loot-parity ${PROJECT_SOURCE_DIR}/tests/pe_mod_loot_parity_test.c pe_economy/pe_econ_store.c pe_economy/pe_econ_config.c pe_economy/pe_econ_engine.c)
    pe_feature_test(econ-config ${PROJECT_SOURCE_DIR}/tests/pe_mod_econ_config_test.c pe_economy/pe_econ_config.c)
    pe_feature_test(econ-engine ${PROJECT_SOURCE_DIR}/tests/pe_mod_econ_engine_test.c pe_economy/pe_econ_engine.c pe_economy/pe_econ_config.c)
    pe_feature_test(wayne-handin ${PROJECT_SOURCE_DIR}/tests/pe_mod_wayne_handin_test.c pe_economy/pe_econ_store.c pe_economy/pe_econ_config.c pe_economy/pe_econ_engine.c)
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
    pe_feature_test(debug-control ${PROJECT_SOURCE_DIR}/tests/pe_mod_debug_control_test.c ${pe_debug_sources})   # host: any mod list
    pe_feature_test(encounter ${PROJECT_SOURCE_DIR}/tests/pe_encounter_test.c host/pe_encounter.c)   # random-encounter spots
endif()
if(BUILD_TESTING AND "pe_enemy_import" IN_LIST PE_MOD_LIST)
    # Imported enemy packs: loading, refusals, posing; with PE_TEST_ENEMY_PACK also a real pack.
    pe_feature_test(enemy-pack ${PROJECT_SOURCE_DIR}/tests/pe_enemy_pack_test.c pe_enemy_import/pe_enemy_pack.c)
    target_include_directories(test-mod-enemy-pack PRIVATE pe_enemy_import)
    target_link_libraries(test-mod-enemy-pack PRIVATE pe_download_json)
    set_tests_properties(mod-enemy-pack PROPERTIES ENVIRONMENT "PE_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}")
    # The enemies builder: a made-up model and moves -> a pack that loads back (events, sounds, hit
    # frames); PE_TEST_RE3_PC adds Nemesis built from the RE3 PC folder through the importer's store.
    pe_feature_test(enemy-build ${PROJECT_SOURCE_DIR}/tests/pe_enemy_build_test.c pe_enemy_import/pe_enemy_build.c
        pe_enemy_import/pe_enemy_pack.c ${pe_import_core_sources}
        pe_src_re3/pe_src_re3_emd.c pe_src_re3/pe_src_re3_tones.c pe_src_re3/pe_src_re3_enemies.c pe_src_re3/pe_src_re3_pld.c)
    target_include_directories(test-mod-enemy-build PRIVATE pe_enemy_import host/import pe_src_re3)
    target_link_libraries(test-mod-enemy-build PRIVATE pe_download_json)
    set_tests_properties(mod-enemy-build PROPERTIES ENVIRONMENT "PE_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}" TIMEOUT 300)
endif()
if(BUILD_TESTING AND "pe_phone" IN_LIST PE_MOD_LIST)
    # The pure core and the game's own pack; the game side is covered by pe-native-debug's
    # scenarios/phone_calls.py.
    pe_feature_test(phone-cancel ${PROJECT_SOURCE_DIR}/tests/pe_mod_phone_cancel_test.c)
    pe_feature_test(phone ${PROJECT_SOURCE_DIR}/tests/pe_mod_phone_test.c pe_phone/pe_phone_text.c
        pe_phone/pe_phone_record.c pe_phone/pe_phone_pick.c pe_phone/pe_phone_file.c)
    target_compile_definitions(test-mod-phone PRIVATE PE_PHONE_CALLS_FILE="${CMAKE_CURRENT_SOURCE_DIR}/pe_phone/calls/pe1.txt")
endif()
if(BUILD_TESTING)
    pe_feature_test(title-choice ${PROJECT_SOURCE_DIR}/tests/pe_mod_title_choice_test.c)
endif()
if(BUILD_TESTING AND "pe_challenges" IN_LIST PE_MOD_LIST)
    pe_feature_test(challenge-start ${PROJECT_SOURCE_DIR}/tests/pe_mod_challenge_start_test.c)
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
        pe_wardrobe/pe_wardrobe_mod.c pe_wardrobe/pe_wardrobe_profile.c pe_wardrobe/pe_wardrobe.c pe_wardrobe/pe_pack_pe1_costumes.c
        pe_wardrobe/pe_skin_target.c pe_wardrobe/pe_skin_build.c ${pe_import_core_sources})   # the Wardrobe registers the skins target
    target_include_directories(test-mod-collection PRIVATE host/import)
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
