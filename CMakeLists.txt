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

  add_executable(hoh
    src/main.cpp
    src/compat/platform_sdl.cpp
    src/compat/audio_sdl.cpp)
  if(TARGET SDL2::SDL2main)
    target_link_libraries(hoh PRIVATE SDL2::SDL2main)
  endif()
  if(TARGET SDL2::SDL2)
    target_link_libraries(hoh PRIVATE SDL2::SDL2)
  else()
    target_include_directories(hoh PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(hoh PRIVATE ${SDL2_LIBRARIES})
  endif()
  target_link_libraries(hoh PRIVATE hohcore PkgConfig::VORBISFILE)
  if(WIN32)
    set_target_properties(hoh PROPERTIES WIN32_EXECUTABLE ON)
  endif()
endif()
