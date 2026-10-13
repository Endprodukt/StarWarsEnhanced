# The Pi build without WSL: clang and lld on the PC, against a copy of the
# cabinet's own libraries and headers (build-pi-sysroot, see README.md). The
# copy the Tempest 2000 port keeps can be pointed to with SWVP_PI_SYSROOT.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(SWVP_PI_SYSROOT "${CMAKE_CURRENT_LIST_DIR}/../build-pi-sysroot" CACHE PATH "Copy of the cabinet's /usr and /lib")
get_filename_component(SWVP_PI_SYSROOT "${SWVP_PI_SYSROOT}" ABSOLUTE)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_SYSROOT "${SWVP_PI_SYSROOT}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld -static-libstdc++ -static-libgcc -Wl,--allow-shlib-undefined")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH "${SWVP_PI_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# SDL2 comes from the sysroot as a ready target (see CMakeLists.txt).
set(CMAKE_DISABLE_FIND_PACKAGE_SDL2 TRUE)
# pthread is part of glibc on the cabinet. FindThreads cannot tell (its
# test links nothing, CMAKE_TRY_COMPILE_TARGET_TYPE being a library) and
# would otherwise ask for a libpthreads that does not exist.
set(CMAKE_HAVE_LIBC_PTHREAD 1 CACHE INTERNAL "pthread is in glibc")
set(SWVP_PI_BUILD TRUE)
