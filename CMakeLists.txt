cmake_minimum_required(VERSION 3.16)
project(hoh-port VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(HOH_BUILD_GAME "Compila o executavel SDL2 (precisa de SDL2 e vorbisfile)" ON)
set(HOH_DATA_DIR "" CACHE PATH "Pasta do jogo original (HoHOriginal.dat, Sound/) para os testes")

if(MSVC)
  add_compile_options(/W3 /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

# Nucleo sem dependencias: bitmaps, desenho, fonte, packfiles, datafiles, leitor dos dados.
add_library(hohcore STATIC
  src/compat/gfx.cpp
  src/compat/font.cpp
  src/compat/packfile.cpp
  src/compat/datafile.cpp
  src/data/hohdata.cpp)
target_include_directories(hohcore PUBLIC src)

add_executable(hoh-datatest tests/datatest.cpp)
target_link_libraries(hoh-datatest PRIVATE hohcore)

enable_testing()
if(HOH_DATA_DIR)
  add_test(NAME datatest COMMAND hoh-datatest "${HOH_DATA_DIR}")
endif()

if(HOH_BUILD_GAME)
  find_package(SDL2 REQUIRED)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(VORBISFILE REQUIRED IMPORTED_TARGET vorbisfile)

  # Camada de plataforma SDL2 (video, entrada, timers, audio)
  add_library(hohplatform STATIC src/compat/platform_sdl.cpp src/compat/audio_sdl.cpp)
  target_link_libraries(hohplatform PUBLIC hohcore PkgConfig::VORBISFILE)
  if(MINGW)
    # libogg/libvorbis estaticos: precisa das dependencias privadas (pkg-config --static)
    target_link_libraries(hohplatform PUBLIC ${VORBISFILE_STATIC_LDFLAGS})
  endif()
  if(TARGET SDL2::SDL2)
    target_link_libraries(hohplatform PUBLIC SDL2::SDL2)
  else()
    target_include_directories(hohplatform PUBLIC ${SDL2_INCLUDE_DIRS})
    target_link_libraries(hohplatform PUBLIC ${SDL2_LIBRARIES})
  endif()

  # O jogo: logica portada do HoH.exe (src/game) sobre a camada de compatibilidade
  file(GLOB HOH_GAME_SOURCES CONFIGURE_DEPENDS src/game/*.cpp)
  add_executable(hoh src/main.cpp ${HOH_GAME_SOURCES})
  # Visualizador de dados (marco 1): sprites, sons e musica
  add_executable(hoh-viewer tools/viewer.cpp)
  foreach(t hoh hoh-viewer)
    if(TARGET SDL2::SDL2main)
      target_link_libraries(${t} PRIVATE SDL2::SDL2main)
    endif()
    target_link_libraries(${t} PRIVATE hohplatform)
    if(WIN32)
      set_target_properties(${t} PROPERTIES WIN32_EXECUTABLE ON)
    endif()
    if(MINGW)
      # runtime do GCC/libstdc++/winpthread embutido: so o SDL2.dll acompanha o .exe
      target_link_options(${t} PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
  endforeach()
endif()
