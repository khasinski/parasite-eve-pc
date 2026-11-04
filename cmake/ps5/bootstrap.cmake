# Runtime selection must precede compiler detection and all PsyZ/SDL setup.
option(PE_PS5_HOST_VALIDATE "Validate portable probes on the host; produces no PS5 payload" OFF)
if(NOT PE_PS5_HOST_VALIDATE)
    message(FATAL_ERROR
        "PS5 runtime unavailable: public loader ABI compatibility with retail 13.60 "
        "kstuff-lite/ShadowMount+ has not been verified. See docs/PS5_PORT.md. "
        "PE_PS5_HOST_VALIDATE=ON builds host probes only.")
endif()
if(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "PE_PS5_HOST_VALIDATE requires a native host compiler")
endif()
enable_language(C)
add_executable(pe-ps5-bootstrap-host "${CMAKE_CURRENT_LIST_DIR}/../../platform/ps5/bootstrap.c")
set_target_properties(pe-ps5-bootstrap-host PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
target_compile_definitions(pe-ps5-bootstrap-host PRIVATE PE_PS5_HOST_VALIDATE=1)
