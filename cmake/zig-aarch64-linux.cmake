# Toolchain para o R36S (aarch64, ArkOS glibc 2.30) usando o Zig como compilador cruzado.
# O Zig mira uma glibc antiga exata (2.28) e linka a libc++ estaticamente, entao o binario roda
# no ArkOS sem sysroot nem bibliotecas extras. Os wrappers (cc, c++, ar, ranlib) sao gerados por
# scripts/build-r36s.sh em HOH_ZIG_WRAPPERS.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(DEFINED ENV{HOH_ZIG_WRAPPERS} AND NOT HOH_ZIG_WRAPPERS)
  set(HOH_ZIG_WRAPPERS $ENV{HOH_ZIG_WRAPPERS})
endif()
if(DEFINED ENV{HOH_R36S_DEPS} AND NOT HOH_R36S_DEPS)
  set(HOH_R36S_DEPS $ENV{HOH_R36S_DEPS})
endif()
set(CMAKE_C_COMPILER ${HOH_ZIG_WRAPPERS}/cc)
set(CMAKE_CXX_COMPILER ${HOH_ZIG_WRAPPERS}/c++)
set(CMAKE_AR ${HOH_ZIG_WRAPPERS}/ar CACHE FILEPATH "")
set(CMAKE_RANLIB ${HOH_ZIG_WRAPPERS}/ranlib CACHE FILEPATH "")

# Contas em ponto flutuante iguais as do x87 do original (sem fusao em FMA no ARM).
set(CMAKE_C_FLAGS_INIT "-ffp-contract=off")
set(CMAKE_CXX_FLAGS_INIT "-ffp-contract=off")

set(CMAKE_FIND_ROOT_PATH ${HOH_R36S_DEPS})
set(CMAKE_PREFIX_PATH ${HOH_R36S_DEPS})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(ENV{PKG_CONFIG_LIBDIR} "${HOH_R36S_DEPS}/lib/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "")
