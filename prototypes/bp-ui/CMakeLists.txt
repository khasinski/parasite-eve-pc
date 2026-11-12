cmake_minimum_required(VERSION 3.21)
project(pe_bp_ui_preview LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Standalone only: not included by the production root CMakeLists or mods list.
add_library(bp-ui-model STATIC bp_ui.c bp_ui_demo.c)
target_include_directories(bp-ui-model PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
add_executable(bp-ui-tests tests.c)
target_link_libraries(bp-ui-tests PRIVATE bp-ui-model)
add_executable(bp-ui-export export.c)
target_link_libraries(bp-ui-export PRIVATE bp-ui-model)

# Live backend: pure rules and transactions from the pe_bp mod, and the live catalog.
set(PE_BP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../mods/pe_bp")
add_library(bp-live STATIC ${PE_BP_DIR}/bp_rules.c ${PE_BP_DIR}/bp_txn.c bp_catalog.c)
target_include_directories(bp-live PUBLIC "${PE_BP_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bp-live PUBLIC bp-ui-model)
add_executable(bp-live-tests tests_live.c)
target_link_libraries(bp-live-tests PRIVATE bp-live)

# Compile real native adapter without linking it into any game executable.
add_library(bp-ui-native-check OBJECT bp_ui_native.c)
target_include_directories(bp-ui-native-check PRIVATE . ../../include ../../mods/host)

foreach(target bp-ui-model bp-ui-tests bp-ui-export bp-live bp-live-tests bp-ui-native-check)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    endif()
endforeach()
enable_testing()
add_test(NAME bp-ui-contracts COMMAND bp-ui-tests)
add_test(NAME bp-live-backend COMMAND bp-live-tests)
