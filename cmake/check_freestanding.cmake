# Copyright 2026 Calvin
# SPDX-License-Identifier: Apache-2.0
#
# Fail if anything under src/core or src/ports has grown a platform dependency.
#
# The layering rule is only worth stating if something checks it. Reviews miss
# a single added include; this does not. Run as a CTest case, so a violation
# shows up the same way a failing assertion does.
#
# Usage: cmake -DTSB_SRC=<repo>/src -P cmake/check_freestanding.cmake

if(NOT DEFINED TSB_SRC)
    message(FATAL_ERROR "TSB_SRC is not set")
endif()

set(BANNED_INCLUDES
    "unistd.h"
    "sys/[a-z]+\\.h"
    "netinet/[a-z]+\\.h"
    "arpa/[a-z]+\\.h"
    "windows.h"
    "winsock2?.h"
    "pthread.h"
    "time.h"
    "stdio.h")

# Calls that imply allocation or file I/O. The trailing parenthesis keeps the
# match off prose, so documentation may still name these functions.
set(BANNED_CALLS
    "malloc[ \t]*\\("
    "calloc[ \t]*\\("
    "realloc[ \t]*\\("
    "free[ \t]*\\("
    "fopen[ \t]*\\("
    "socket[ \t]*\\(")

file(GLOB_RECURSE SOURCES "${TSB_SRC}/core/*.c" "${TSB_SRC}/core/*.h"
                          "${TSB_SRC}/ports/*.c" "${TSB_SRC}/ports/*.h")

if(NOT SOURCES)
    message(FATAL_ERROR "no sources found under ${TSB_SRC}; check TSB_SRC")
endif()

set(VIOLATIONS "")

foreach(FILE ${SOURCES})
    file(READ "${FILE}" CONTENT)

    foreach(HDR ${BANNED_INCLUDES})
        if(CONTENT MATCHES "#[ \t]*include[ \t]*<${HDR}>")
            list(APPEND VIOLATIONS "${FILE}: includes <${HDR}>")
        endif()
    endforeach()

    foreach(CALL ${BANNED_CALLS})
        if(CONTENT MATCHES "${CALL}")
            list(APPEND VIOLATIONS "${FILE}: calls ${CALL}")
        endif()
    endforeach()
endforeach()

list(LENGTH SOURCES N_SOURCES)

if(VIOLATIONS)
    message("core and ports must not depend on the platform:")
    foreach(V ${VIOLATIONS})
        message("  ${V}")
    endforeach()
    message(FATAL_ERROR "layering violation")
endif()

message("core is freestanding (${N_SOURCES} files checked)")
