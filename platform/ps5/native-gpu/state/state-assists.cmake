# Append AFTER reviewed state-components.cmake; original source paths preserved.
if(PE_NATIVE_STATE_CPU_COMPONENTS AND TARGET pe-mods AND "pe_assists" IN_LIST PE_MOD_LIST)
    target_compile_definitions(pe-mods PRIVATE PE_NATIVE_STATE_ASSISTS=1)
endif()

if(PE_NATIVE_STATE_CPU_COMPONENTS AND TARGET pe-mods AND "pe_qol" IN_LIST PE_MOD_LIST)
    target_compile_definitions(pe-mods PRIVATE PE_NATIVE_STATE_QOL=1)
endif()

if(PE_NATIVE_STATE_CPU_COMPONENTS AND TARGET pe-mods AND "pe_economy" IN_LIST PE_MOD_LIST)
    target_compile_definitions(pe-mods PRIVATE PE_NATIVE_STATE_ECONOMY=1)
endif()

if(PE_NATIVE_STATE_CPU_COMPONENTS AND TARGET pe-mods AND "pe_collection" IN_LIST PE_MOD_LIST)
    target_compile_definitions(pe-mods PRIVATE PE_NATIVE_STATE_COLLECTION=1)
endif()

if(PE_NATIVE_STATE_CPU_COMPONENTS AND TARGET pe-mods AND "pe_rebalance" IN_LIST PE_MOD_LIST)
    target_compile_definitions(pe-mods PRIVATE PE_NATIVE_STATE_REBALANCE=1)
endif()
