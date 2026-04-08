#!/usr/bin/env bash
# Wrapper for MinGW cross-compiler that strips Linux system include/lib paths.
# GCC's CPATH/C_INCLUDE_PATH are NOT suppressed by -nostdinc (documented GCC behavior),
# and CMake imported targets (CURL etc.) can inject -isystem /usr/include even with
# -nostdinc in CFLAGS. This wrapper removes those Linux paths before they reach the
# real compiler, which is the canonical fix used by MXE and similar setups.
#
# Usage in toolchain-mingw64.cmake:
#   set(CMAKE_C_COMPILER   "/path/to/mingw-wrap.sh" "--cc")
#   set(CMAKE_CXX_COMPILER "/path/to/mingw-wrap.sh" "--cxx")

REAL_CC="x86_64-w64-mingw32-gcc"
REAL_CXX="x86_64-w64-mingw32-g++"

# Clear env vars GCC reads that bypass -nostdinc
unset CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH OBJC_INCLUDE_PATH LIBRARY_PATH

# Determine which compiler to invoke
if [[ "$1" == "--cc" ]]; then
    shift
    COMPILER="$REAL_CC"
elif [[ "$1" == "--cxx" ]]; then
    shift
    COMPILER="$REAL_CXX"
else
    # Fallback: guess from script name
    case "$(basename "$0")" in
        *g++*|*c++*) COMPILER="$REAL_CXX" ;;
        *)            COMPILER="$REAL_CC"  ;;
    esac
fi

# Filter arguments: remove -isystem/-I pointing at Linux system paths
FILTERED=()
SKIP_NEXT=0
for arg in "$@"; do
    if [[ $SKIP_NEXT -eq 1 ]]; then
        SKIP_NEXT=0
        # Drop this arg if it's a Linux system path
        if [[ "$arg" == /usr/include* ]] || \
           [[ "$arg" == /usr/lib/x86_64-linux-gnu* ]] || \
           [[ "$arg" == /usr/lib/gcc/x86_64-linux-gnu* ]]; then
            continue
        fi
        FILTERED+=("$arg")
        continue
    fi
    # -isystem /usr/include/... — flag takes next arg as path
    if [[ "$arg" == "-isystem" ]]; then
        SKIP_NEXT=1
        FILTERED+=("$arg")
        continue
    fi
    # -isystem/usr/include/... (no space) or -I/usr/include/...
    if [[ "$arg" == -isystem/usr/include* ]] || \
       [[ "$arg" == -I/usr/include* ]] || \
       [[ "$arg" == -isystem/usr/lib/x86_64-linux-gnu* ]]; then
        continue
    fi
    FILTERED+=("$arg")
done

exec "$COMPILER" "${FILTERED[@]}"