# Standalone integration fragment. Provider headers MUST match the already-linked
# public ps5platform archive, not the game's different SDK compilation context.
if(NOT PE_NATIVE_DIRECTORY_PROVIDER_SYSROOT OR NOT PE_PS5_LLVM_BIN)
  message(FATAL_ERROR "Set PE_NATIVE_DIRECTORY_PROVIDER_SYSROOT and PE_PS5_LLVM_BIN for directory ABI bridge")
endif()
set(_pe_directory_bridge "${CMAKE_CURRENT_BINARY_DIR}/pe-native-directory-bridge.o")
add_custom_command(OUTPUT "${_pe_directory_bridge}"
  COMMAND "${PE_PS5_LLVM_BIN}/clang.exe" -target x86_64-sie-ps5
    "--sysroot=${PE_NATIVE_DIRECTORY_PROVIDER_SYSROOT}"
    -isystem "${PE_NATIVE_DIRECTORY_PROVIDER_SYSROOT}/include"
    -I "${CMAKE_CURRENT_LIST_DIR}/provider-headers"
    -std=gnu11 -fPIC -fno-stack-protector -femulated-tls -Wall -Wextra -Werror
    -c "${CMAKE_CURRENT_LIST_DIR}/pe-native-directory-bridge.c" -o "${_pe_directory_bridge}"
  DEPENDS "${CMAKE_CURRENT_LIST_DIR}/pe-native-directory-bridge.c"
          "${CMAKE_CURRENT_LIST_DIR}/provider-headers/errno.h")
target_sources(pe-startup-log PRIVATE "${CMAKE_CURRENT_LIST_DIR}/pe-game-directory.c")
target_sources(parasite-eve-pc PRIVATE "${_pe_directory_bridge}")
target_link_libraries(parasite-eve-pc PRIVATE --wrap=closedir --wrap=scandir)
