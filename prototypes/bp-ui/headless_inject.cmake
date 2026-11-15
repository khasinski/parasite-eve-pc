# This explicit BP build follows the complete current upstream suite plus pe_bp.
# pe_bp is appended as a normal (directory-scoped) variable, never written to the
# cache: configuring the same build dir without this file builds the default list.
# The linker wrappers below need GNU ld/lld; Apple ld64 has no --wrap.
include(CheckLinkerFlag)
check_linker_flag(C "-Wl,--wrap=pe_host_init" BP_UI_LINKER_HAS_WRAP)
if(NOT BP_UI_LINKER_HAS_WRAP)
    message(WARNING "BP UI headless preview skipped: the linker does not support "
        "-Wl,--wrap (Apple ld64 on macOS). Use GNU ld or lld (Linux, Windows "
        "clang/MinGW). This build is the normal game with the default mod list.")
    return()
endif()
set(PE_EXTRA_MODS ${PE_EXTRA_MODS} pe_bp)
list(REMOVE_DUPLICATES PE_EXTRA_MODS)
# Explicit developer-only injection. Production CMake and mod lists stay untouched.
# Configure the game with -DCMAKE_PROJECT_parasite-eve-pc_INCLUDE=<this file>.
# The live backend is the pe_bp mod (mods/pe_bp), appended to PE_EXTRA_MODS above.
function(bp_ui_headless_attach)
    if(NOT TARGET pe-mods OR NOT TARGET parasite-eve-pc)
        message(FATAL_ERROR "BP UI headless preview requires PE_MODS=ON")
    endif()
    target_sources(parasite-eve-pc PRIVATE
        ${PROJECT_SOURCE_DIR}/prototypes/bp-ui/bp_ui.c
        ${PROJECT_SOURCE_DIR}/prototypes/bp-ui/bp_ui_demo.c
        ${PROJECT_SOURCE_DIR}/prototypes/bp-ui/bp_ui_native.c
        ${PROJECT_SOURCE_DIR}/prototypes/bp-ui/bp_catalog.c
        ${PROJECT_SOURCE_DIR}/prototypes/bp-ui/bp_live_ui.c
        ${PROJECT_SOURCE_DIR}/prototypes/bp-ui/battle_probe.c
        ${PROJECT_SOURCE_DIR}/prototypes/bp-ui/headless_entry.c)
    target_include_directories(parasite-eve-pc PRIVATE ${PROJECT_SOURCE_DIR}/mods/host ${PROJECT_SOURCE_DIR}/mods/pe_shops
        ${PROJECT_SOURCE_DIR}/mods/pe_bp ${PROJECT_SOURCE_DIR}/prototypes/bp-ui)
    target_compile_definitions(parasite-eve-pc PRIVATE BP_UI_HEADLESS_TEST=1)
    # Test-only rank setter for the battle probe (battle_probe.c); preview build only.
    target_compile_definitions(pe-mods PRIVATE PE_BP_TEST_HOOKS=1)
    add_executable(bp-host-settings-tests EXCLUDE_FROM_ALL ${PROJECT_SOURCE_DIR}/tests/original_input_rules.c)
    target_include_directories(bp-host-settings-tests PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/mods/host "$<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>")
    target_compile_definitions(bp-host-settings-tests PRIVATE __psyz PE_MOD_SETTINGS_TEST=1)
    target_link_libraries(bp-host-settings-tests PRIVATE psyz)
    set_target_properties(parasite-eve-pc PROPERTIES OUTPUT_NAME parasite-eve-ui-preview)
    target_link_options(parasite-eve-pc PRIVATE -Wl,--wrap=pe_main_menu_init -Wl,--wrap=pe_host_init)
endfunction()
cmake_language(DEFER CALL bp_ui_headless_attach)
