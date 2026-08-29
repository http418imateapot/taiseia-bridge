cmake_minimum_required(VERSION 3.16)
project(taiseia-bridge C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TSB_BUILD_TESTS "Build the host unit tests" ON)
option(TSB_SANITIZE    "Build with AddressSanitizer and UBSan where available" ON)
option(TSB_WERROR      "Treat warnings as errors" OFF)

# ------------------------------------------------------------------ warnings
#
# TSB_WERROR is off by default and on in CI. A warning that survives one commit
# survives all of them, so the gate belongs somewhere it cannot be skipped --
# but a contributor on a newer compiler should not be blocked by a diagnostic
# that did not exist when the code was written.
#
add_library(tsb_warnings INTERFACE)
if(MSVC)
    target_compile_options(tsb_warnings INTERFACE /W4)
    if(TSB_WERROR)
        target_compile_options(tsb_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(tsb_warnings INTERFACE
        -Wall -Wextra -Wsign-compare -Wshadow -Wpointer-arith -Wcast-align
        -Wstrict-prototypes -Wmissing-prototypes -Wold-style-definition
        -Wwrite-strings -Wformat=2)
    if(TSB_WERROR)
        target_compile_options(tsb_warnings INTERFACE -Werror)
    endif()
endif()

# ---------------------------------------------------------------- sanitizers
#
# Sanitizers do not recover: an undefined-behaviour report has to fail the
# build, not print a line into a log nobody reads. They are disabled on
# Windows, where the MinGW runtime does not carry them.
#
if(TSB_SANITIZE AND NOT MSVC AND NOT WIN32)
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all
                        -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()

# ---------------------------------------------------------------------- core
#
# Everything under src/core and src/ports is freestanding: no platform headers,
# no allocation, no I/O. The platform reaches it only through the port
# interfaces.
#
# This is enforced rather than merely documented -- see the core_is_freestanding
# test below, which runs on every build.
#
add_library(tsb_core STATIC
    src/core/util/result.c
    src/core/taiseia/frame.c
    src/core/taiseia/datatype.c
    src/ports/appliance_link.c)
target_include_directories(tsb_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tsb_core PRIVATE tsb_warnings)

# ------------------------------------------------------------------ adapters
add_library(tsb_sim STATIC
    src/adapters/sim/sim_link.c)
target_link_libraries(tsb_sim PUBLIC tsb_core PRIVATE tsb_warnings)

# --------------------------------------------------------------------- tests
if(TSB_BUILD_TESTS)
    enable_testing()

    add_executable(tsb_tests
        tests/test_main.c
        tests/test_frame.c
        tests/test_datatype.c
        tests/test_sim_link.c)
    target_include_directories(tsb_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(tsb_tests PRIVATE tsb_sim tsb_warnings)

    add_test(NAME unit COMMAND tsb_tests)

    add_test(NAME core_is_freestanding
             COMMAND ${CMAKE_COMMAND}
                     -DTSB_SRC=${CMAKE_CURRENT_SOURCE_DIR}/src
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_freestanding.cmake)
endif()
