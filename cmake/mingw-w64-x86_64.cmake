# Toolchain para compilar o port para Windows x64 a partir do Linux/WSL com MinGW-w64.
# Uso: cmake -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DHOH_WIN_DEPS=<prefixo> ...
#   HOH_WIN_DEPS: prefixo com SDL2 (pacote SDL2-devel-*-mingw, pasta x86_64-w64-mingw32) e
#   libogg/libvorbis compilados para MinGW (scripts/build-windows.sh monta tudo isso).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)
# Variante "posix" do GCC: std::thread/std::mutex usados pela camada de plataforma.
set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}-gcc-posix)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++-posix)
set(CMAKE_RC_COMPILER ${TOOLCHAIN_PREFIX}-windres)

if(DEFINED ENV{HOH_WIN_DEPS} AND NOT HOH_WIN_DEPS)
  set(HOH_WIN_DEPS $ENV{HOH_WIN_DEPS})
endif()
set(CMAKE_FIND_ROOT_PATH /usr/${TOOLCHAIN_PREFIX} ${HOH_WIN_DEPS})
set(CMAKE_PREFIX_PATH ${HOH_WIN_DEPS})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
# pkg-config so deve enxergar as libs do Windows
set(ENV{PKG_CONFIG_LIBDIR} "${HOH_WIN_DEPS}/lib/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "")
