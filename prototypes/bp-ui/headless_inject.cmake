# Explicit developer-only injection. Production CMake and mod lists stay untouched.
# Configure the game with -DCMAKE_PROJECT_parasite-eve-pc_INCLUDE=<this file>.
# The live backend is the pe_bp mod (mods/pe_bp); add it to PE_MOD_LIST for live data.
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
        ${PROJECT_SOURCE_DIR}/prototypes/bp-ui/headless_entry.c)
    target_include_directories(parasite-eve-pc PRIVATE ${PROJECT_SOURCE_DIR}/mods/host ${PROJECT_SOURCE_DIR}/mods/pe_shops
        ${PROJECT_SOURCE_DIR}/mods/pe_bp ${PROJECT_SOURCE_DIR}/prototypes/bp-ui)
    target_compile_definitions(parasite-eve-pc PRIVATE BP_UI_HEADLESS_TEST=1)
    set_target_properties(parasite-eve-pc PROPERTIES OUTPUT_NAME parasite-eve-ui-preview)
    target_link_options(parasite-eve-pc PRIVATE -Wl,--wrap=pe_main_menu_init -Wl,--wrap=pe_host_init)
endfunction()
cmake_language(DEFER CALL bp_ui_headless_attach)
