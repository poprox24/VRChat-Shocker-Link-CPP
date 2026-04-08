set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSROOT /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Prevent CMake find_* commands from ever picking up Linux host paths
set(CMAKE_IGNORE_PATH
    /usr/include
    /usr/lib
    /usr/lib/x86_64-linux-gnu
    /usr/lib/gcc/x86_64-linux-gnu
    /usr/share
)

# Force pkg-config to use the MinGW sysroot only (not the host system).
# This prevents find_package(CURL) from pointing CURL_INCLUDE_DIR at /usr/include.
set(ENV{PKG_CONFIG_LIBDIR} "/usr/x86_64-w64-mingw32/lib/pkgconfig")
set(ENV{PKG_CONFIG_PATH}   "")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "/usr/x86_64-w64-mingw32")

# Use wrapper scripts that:
#   1. unset CPATH / C_INCLUDE_PATH / CPLUS_INCLUDE_PATH
#      (GCC docs: these are NOT suppressed by -nostdinc)
#   2. strip -isystem /usr/include injected by CMake imported targets (e.g. CURL::libcurl)
# The wrapper must live in the same directory as this toolchain file.
get_filename_component(_TOOLCHAIN_DIR "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
set(CMAKE_C_COMPILER   "${_TOOLCHAIN_DIR}/mingw-wrap.sh" "--cc")
set(CMAKE_CXX_COMPILER "${_TOOLCHAIN_DIR}/mingw-wrap.sh" "--cxx")
set(CMAKE_RC_COMPILER  x86_64-w64-mingw32-windres)

# === Compiler flags ===
set(CMAKE_C_FLAGS_INIT " -nostdinc -isystem ${CMAKE_SYSROOT}/include -isystem /usr/lib/gcc/x86_64-w64-mingw32/15.2.0/include -DWIN32_LEAN_AND_MEAN -DNOMINMAX")
set(CMAKE_CXX_FLAGS_INIT " -nostdinc -nostdinc++ -isystem ${CMAKE_SYSROOT}/include/c++/15.2.0 -isystem ${CMAKE_SYSROOT}/include/c++/15.2.0/x86_64-w64-mingw32 -isystem ${CMAKE_SYSROOT}/include -isystem /usr/lib/gcc/x86_64-w64-mingw32/15.2.0/include -DWIN32_LEAN_AND_MEAN -DNOMINMAX")

# GCC 15 MinGW: demote (1<<32) overflow in <chrono>/<ratio> from error to warning,
# and silence other noisy-but-harmless MinGW GCC 15 warnings.
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} -fpermissive -Wno-narrowing -Wno-shift-count-overflow -Wno-template-body -Wno-error=narrowing")