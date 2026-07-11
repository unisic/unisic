# Build-time stamp: the exact date the built state landed on git (HEAD commit
# date, YYYYMMDD-HHMM) — shown next to the version/build number in the UI so
# any screenshot of the footer identifies the commit iteration. Runs on EVERY
# build (cheap) but rewrites the header only when the value changes, so no
# needless recompiles. Order of truth:
#   1. UNISIC_BUILD_DATE env (tarball builds — rpm/OBS — have no .git)
#   2. git log -1 of HEAD (local + CI checkouts)
#   3. empty — the UI omits the stamp
# Invoked as: cmake -DOUT=<header> -DSRC=<source dir> -P BuildDate.cmake
set(date "")
if(DEFINED ENV{UNISIC_BUILD_DATE} AND NOT "$ENV{UNISIC_BUILD_DATE}" STREQUAL "")
    set(date "$ENV{UNISIC_BUILD_DATE}")
else()
    execute_process(
        COMMAND git -C "${SRC}" log -1 --format=%cd --date=format:%Y%m%d-%H%M
        OUTPUT_VARIABLE date
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        set(date "")
    endif()
endif()
set(content "#pragma once\n#define UNISIC_BUILD_DATE \"${date}\"\n")
set(old "")
if(EXISTS "${OUT}")
    file(READ "${OUT}" old)
endif()
if(NOT old STREQUAL content)
    file(WRITE "${OUT}" "${content}")
endif()
