# SPDX-License-Identifier: GPL-3.0-only
cmake_minimum_required(VERSION 3.20)
project(VitaChipsChallenge VERSION 0.1.0 LANGUAGES C)

if(CMAKE_SYSTEM_PROCESSOR STREQUAL "armv7-a" AND DEFINED VITASDK)
  set(VCC_VITA TRUE)
  include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(vcc_core STATIC src/dat.c src/game.c)
target_include_directories(vcc_core PUBLIC include)
target_compile_options(vcc_core PRIVATE
  -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow)

include(CTest)
if(BUILD_TESTING)
  add_executable(vcc_dat_tests tests/test_dat.c)
  target_link_libraries(vcc_dat_tests PRIVATE vcc_core)
  target_compile_options(vcc_dat_tests PRIVATE
    -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow)
  add_test(NAME dat_parser COMMAND vcc_dat_tests)

  add_executable(vcc_game_tests tests/test_game.c)
  target_link_libraries(vcc_game_tests PRIVATE vcc_core)
  target_compile_options(vcc_game_tests PRIVATE
    -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow)
  add_test(NAME game_core COMMAND vcc_game_tests)

  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/reference/extracted/CHIPS.DAT")
    add_executable(vcc_reference_dat_tests tests/test_reference_dat.c)
    target_link_libraries(vcc_reference_dat_tests PRIVATE vcc_core)
    target_compile_definitions(vcc_reference_dat_tests PRIVATE
      VCC_REFERENCE_DAT="${CMAKE_CURRENT_SOURCE_DIR}/reference/extracted/CHIPS.DAT")
    target_compile_options(vcc_reference_dat_tests PRIVATE
      -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow)
    add_test(NAME reference_dat_parser COMMAND vcc_reference_dat_tests)
  endif()
endif()

if(VCC_VITA)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/reference/extracted/CHIPS.DAT")
    message(FATAL_ERROR "Run tools/extract_reference.py before a personal Vita build")
  endif()
  if(NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/assets/OBJ32_4.bmp")
    message(FATAL_ERROR "Extract original NE bitmaps into build-vita/assets first")
  endif()
  find_package(SDL2 CONFIG REQUIRED)
  add_executable(vita_chips src/vita_main.c)
  target_link_libraries(vita_chips PRIVATE vcc_core SDL2::SDL2)
  target_compile_options(vita_chips PRIVATE
    -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow)
  vita_create_self(vita_chips.self vita_chips)
  vita_create_vpk(VitaChipsChallenge.vpk "VITA00001" vita_chips.self
    VERSION "01.00"
    NAME "Vita Chips Challenge"
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/assets/vita/icon0.png" "sce_sys/icon0.png"
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/assets/vita/pic0.png" "sce_sys/pic0.png"
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/assets/vita/bg0.png" "sce_sys/livearea/contents/bg0.png"
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/assets/vita/startup.png" "sce_sys/livearea/contents/startup.png"
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/assets/vita/template.xml" "sce_sys/livearea/contents/template.xml"
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/reference/extracted/CHIPS.DAT" "data/CHIPS.DAT"
    FILE "${CMAKE_CURRENT_BINARY_DIR}/assets/OBJ32_4_RGB.bmp" "data/OBJ32_4_RGB.bmp"
    FILE "${CMAKE_CURRENT_BINARY_DIR}/assets/OBJ32_4E_RGB.bmp" "data/OBJ32_4E_RGB.bmp"
    FILE "${CMAKE_CURRENT_BINARY_DIR}/assets/OBJ32_MASKED.bmp" "data/OBJ32_MASKED.bmp"
    FILE "${CMAKE_CURRENT_BINARY_DIR}/assets/BACKGROUND_RGB.bmp" "data/BACKGROUND_RGB.bmp"
    FILE "${CMAKE_CURRENT_BINARY_DIR}/assets/INFOWND_RGB.bmp" "data/INFOWND_RGB.bmp"
    FILE "${CMAKE_CURRENT_BINARY_DIR}/assets/200_RGB.bmp" "data/200_RGB.bmp"
    FILE "${CMAKE_CURRENT_BINARY_DIR}/assets/CHIPEND_RGB.bmp" "data/CHIPEND_RGB.bmp"
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/reference/extracted/BLIP2.WAV" "data/BLIP2.WAV"
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/reference/extracted/BUMMER.WAV" "data/BUMMER.WAV"
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/reference/extracted/CLICK3.WAV" "data/CLICK3.WAV"
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/reference/extracted/DITTY1.WAV" "data/DITTY1.WAV"
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/reference/extracted/DOOR.WAV" "data/DOOR.WAV"
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/reference/extracted/OOF3.WAV" "data/OOF3.WAV"
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/reference/extracted/POP2.WAV" "data/POP2.WAV"
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/reference/extracted/STRIKE.WAV" "data/STRIKE.WAV"
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/reference/extracted/TELEPORT.WAV" "data/TELEPORT.WAV"
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/reference/extracted/WATER2.WAV" "data/WATER2.WAV"
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/reference/extracted/CHIP01.MID" "data/CHIP01.MID"
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/reference/extracted/CHIP02.MID" "data/CHIP02.MID")
endif()
