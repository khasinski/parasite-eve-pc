# Run the build's checker against an isolated tree with known good and bad sources.
cmake_minimum_required(VERSION 3.16)
set(fixture "${CMAKE_CURRENT_BINARY_DIR}/re-portability-guard-fixture")
file(MAKE_DIRECTORY "${fixture}/cmake" "${fixture}/pe_re_play/hosts/pe1"
    "${fixture}/import/core" "${fixture}/pe_src_re2" "${fixture}/pe_import/hosts/pe1")
configure_file("${guard}" "${fixture}/cmake/CheckRePortability.cmake" COPYONLY)
set(check "${CMAKE_COMMAND}" -P "${fixture}/cmake/CheckRePortability.cmake")

file(WRITE "${fixture}/pe_re_play/fixture.c" [=[
/* #include "pe1/in_comment.h"; g_Comment */
const char *example = "escaped quote: \" #include <pe1/in_string.h> g_String";
#include "pe_mod_api.h"
]=])
file(WRITE "${fixture}/pe_re_play/hosts/pe1/fixture.c"
    "#include <pe1/game_state.h>\nint g_PlayerEntity;\n")
file(WRITE "${fixture}/pe_src_re2/fixture.c" "int g_re2_reader;\n")
file(WRITE "${fixture}/import/core/fixture.h" "#include <stdint.h>\n")
file(WRITE "${fixture}/pe_import/fixture.c" "#include \"import_host.h\"\n")
file(WRITE "${fixture}/host/re_play_api.h" "#include <stdint.h>\n")
file(WRITE "${fixture}/pe_import/hosts/pe1/fixture.c"
    "#include \"pe_imp_pe.h\"\n#include <pe1/cdrom.h>\nint g_CdImageVolumes;\nint x = PE_HOOK_FRAME;\n")

execute_process(COMMAND ${check} RESULT_VARIABLE result ERROR_VARIABLE diagnostic)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Guard rejected portable code or scanned the adapter:\n${diagnostic}")
endif()

file(APPEND "${fixture}/pe_re_play/fixture.c" [=[
# /* comment before directive */ include /* comment before path */ "../pe1/game_state.h"
# include <pe_game.h>
# inc\
lude "pe_hooks.h"
int fixture(void) { return g_PlayerEntity + PE_HOOK_FRAME; }
]=])
file(APPEND "${fixture}/import/core/fixture.h" "extern int g_ImporterEngineGlobal;\n")
file(APPEND "${fixture}/pe_src_re2/fixture.c" "extern int g_ReaderEngineGlobal;\n")
file(APPEND "${fixture}/pe_import/fixture.c" "#include \"pe_imp_pe.h\"\nextern int g_ImportMenuGlobal;\n")
file(APPEND "${fixture}/host/re_play_api.h" "#include \"pe1/battle_runtime.h\"\n")

execute_process(COMMAND ${check} RESULT_VARIABLE result ERROR_VARIABLE diagnostic)
if(result STREQUAL "0")
    message(FATAL_ERROR "Guard accepted engine dependencies")
endif()
foreach(expected IN ITEMS pe1/game_state.h pe_game.h pe_hooks.h g_PlayerEntity PE_HOOK_FRAME
        g_ImporterEngineGlobal g_ReaderEngineGlobal pe_imp_pe.h g_ImportMenuGlobal pe1/battle_runtime.h)
    string(FIND "${diagnostic}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Guard missed ${expected}:\n${diagnostic}")
    endif()
endforeach()
