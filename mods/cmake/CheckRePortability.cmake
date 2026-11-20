# Check the portable code without requiring a compiler, Python, or imported discs.
cmake_minimum_required(VERSION 3.16)
set(mods "${CMAKE_CURRENT_LIST_DIR}/..")
file(GLOB_RECURSE importer "${mods}/import/core/*.c" "${mods}/import/core/*.h")
file(GLOB runtime "${mods}/pe_re_play/*.c" "${mods}/pe_re_play/*.h")
file(GLOB_RECURSE readers "${mods}/pe_src_re[123]/*.c" "${mods}/pe_src_re[123]/*.h")
# The import mod's menu and job; its adapter is under pe_import/hosts/.
file(GLOB import_mod "${mods}/pe_import/*.c" "${mods}/pe_import/*.h")
# The contract other mods read about a game being played.
file(GLOB contracts "${mods}/host/re_play_api.h")

set(literal [=[("[^"\\]*(\\.[^"\\]*)*"|'[^'\\]*(\\.[^'\\]*)*')]=])
set(block_comment [=[/\*([^*]|\*+[^*/])*\*+/]=])
set(line_comment [=[//[^
]*]=])
set(directive_space "[ 	]|${block_comment}")
set(engine_header [=[([^
>"]*/)?(pe1/[^
>"]+|pe_game\.h|pe_hooks\.h|pe_imp_pe\.h)]=])
set(include "#(${directive_space})*include(${directive_space})*[<\"]${engine_header}[>\"]")
set(errors "")
foreach(file IN LISTS importer runtime readers import_mod contracts)
    file(READ "${file}" code)
    file(RELATIVE_PATH path "${mods}" "${file}")
    # C splices lines before recognizing tokens. Keep punctuation out of the CMake result list.
    string(REPLACE "\\\n" "" code "${code}")
    string(REGEX REPLACE "[][;]" " " code "${code}")
    # Recognize complete comments and literals before looking for engine tokens inside them.
    string(REGEX MATCHALL "${block_comment}|${line_comment}|${literal}|${include}|[A-Za-z_][A-Za-z_0-9]*" tokens "${code}")
    list(FILTER tokens INCLUDE REGEX "^(#|g_|PE_HOOK_)")
    list(REMOVE_DUPLICATES tokens)
    string(REGEX MATCH "^pe_src_(re[123])/" reader "${path}")
    set(reader_game "${CMAKE_MATCH_1}")
    foreach(token IN LISTS tokens)
        # Disc reader descriptors and naming tables are portable globals in the source mods.
        if(reader AND token MATCHES "^g_${reader_game}_")
            continue()
        endif()
        string(APPEND errors "\n  ${path}: ${token}")
    endforeach()
endforeach()

if(errors)
    message(FATAL_ERROR "Portable RE code depends on a game engine:${errors}")
endif()
