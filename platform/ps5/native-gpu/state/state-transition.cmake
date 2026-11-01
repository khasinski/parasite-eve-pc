# Include after state-components.cmake and state-bank-sources.cmake.
# No automatic runtime call or availability expansion.
target_sources(parasite-eve-pc PRIVATE "${CMAKE_CURRENT_LIST_DIR}/pe_state_transition.c")
target_sources(parasite-eve-pc PRIVATE "${CMAKE_CURRENT_LIST_DIR}/pe_state_graph_archive.c")
option(PE_NATIVE_STATE_CHANGED_COHORTS "Experimental typed same-room cohort transactions" OFF)
if(PE_NATIVE_STATE_CHANGED_COHORTS)
  target_compile_definitions(parasite-eve-pc PRIVATE PE_NATIVE_STATE_CHANGED_COHORTS=1)
endif()
