set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_SYSROOT /usr/x86_64-w64-mingw32)   
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_CXX_FLAGS_INIT "-U__GLIBC__")
set(CMAKE_C_FLAGS_INIT "-U__GLIBC__")

# === CRITICAL FIXES ===
set(CMAKE_C_FLAGS_INIT " -nostdinc -isystem ${CMAKE_SYSROOT}/include -DWIN32_LEAN_AND_MEAN -DNOMINMAX")
set(CMAKE_CXX_FLAGS_INIT " -nostdinc++ -isystem ${CMAKE_SYSROOT}/include/c++/15.2.0 -isystem ${CMAKE_SYSROOT}/include/c++/15.2.0/x86_64-w64-mingw32 -isystem ${CMAKE_SYSROOT}/include -isystem /usr/lib/gcc/x86_64-w64-mingw32/15.2.0/include -DWIN32_LEAN_AND_MEAN -DNOMINMAX")

# Silence the noisy warnings that become errors on GCC 15 + MinGW
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} -Wno-narrowing -Wno-shift-count-overflow -Wno-template-body -Wno-error=narrowing")