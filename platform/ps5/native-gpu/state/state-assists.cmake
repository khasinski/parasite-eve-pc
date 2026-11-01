# Append AFTER reviewed state-components.cmake; original source paths preserved.
if(PE_NATIVE_STATE_CPU_COMPONENTS AND TARGET pe-mods AND "pe_assists" IN_LIST PE_MOD_LIST)
    target_compile_definitions(pe-mods PRIVATE PE_NATIVE_STATE_ASSISTS=1)
endif()
