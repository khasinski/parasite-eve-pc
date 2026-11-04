# SPDX-License-Identifier: MIT
if(NOT PE_PS5_PARTICLE_ADDON)
  return()
endif()
foreach(_pe_target IN ITEMS parasite-eve-pc parasite-eve-original)
  if(NOT TARGET "${_pe_target}")
    message(FATAL_ERROR "Missing native game target ${_pe_target}")
  endif()
  target_compile_definitions("${_pe_target}" PRIVATE __psyz PE_PS5=1 PE_PS5_GPU_NATIVE=1)
endforeach()
set(_pe_particle_dir "${CMAKE_SOURCE_DIR}/src/main/fieldfx/particle_mesh")
target_sources(parasite-eve-pc PRIVATE
  "${_pe_particle_dir}/pe_battle_particle_mesh.c"
  "${_pe_particle_dir}/pe_falling_sprite_adapter.c"
  "${_pe_particle_dir}/pe_particle_render.c")
set_source_files_properties("${_pe_particle_dir}/pe_falling_sprite_adapter.c"
  PROPERTIES COMPILE_DEFINITIONS PE_BATTLE_PARTICLE_PROTOTYPE=1)
