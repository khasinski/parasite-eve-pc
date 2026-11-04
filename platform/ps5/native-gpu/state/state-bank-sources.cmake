# Append after state-components.cmake; original game source remains the owner.
if(PE_NATIVE_STATE_CPU_COMPONENTS)
 target_sources(parasite-eve-pc PRIVATE "${CMAKE_CURRENT_LIST_DIR}/pe_state_bank_sources.c")
endif()
