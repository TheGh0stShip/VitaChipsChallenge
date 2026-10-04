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

add_library(vcc_core STATIC src/dat.c src/game.c src/score.c src/progress.c src/music.c)
target_include_directories(vcc_core PUBLIC include)
if(NOT VCC_VITA)
  target_link_libraries(vcc_core PUBLIC m)
endif()
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

  add_executable(vcc_score_tests tests/test_score.c)
  target_link_libraries(vcc_score_tests PRIVATE vcc_core)
  target_compile_options(vcc_score_tests PRIVATE
    -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow)
  add_test(NAME score COMMAND vcc_score_tests)

  add_executable(vcc_progress_tests tests/test_progress.c)
  target_link_libraries(vcc_progress_tests PRIVATE vcc_core)
  target_compile_options(vcc_progress_tests PRIVATE
    -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow)
  add_test(NAME progress COMMAND vcc_progress_tests)

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
  option(VCC_RELEASE "Build a VPK without game data; it reads the data pack" OFF)
  find_package(SDL2 CONFIG REQUIRED)
  find_package(SDL2_ttf CONFIG REQUIRED)
  add_executable(vita_chips src/vita_main.c src/ui.c src/help.c)
  target_link_libraries(vita_chips PRIVATE vcc_core SDL2_ttf::SDL2_ttf-static
    SDL2::SDL2 freetype png bz2 z)
  target_compile_options(vita_chips PRIVATE
    -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow)
  vita_create_self(vita_chips.self vita_chips)

  # Game data from the player's copy of Chip's Challenge, prepared by
  # tools/make_datapack.py into build-vita/datapack/VitaChipsChallenge/data.
  set(VCC_GAME_FILES)
  if(VCC_RELEASE)
    target_compile_definitions(vita_chips PRIVATE VCC_EXTERNAL_DATA)
  else()
    set(VCC_DATAPACK "${CMAKE_CURRENT_BINARY_DIR}/datapack/VitaChipsChallenge/data")
    if(NOT EXISTS "${VCC_DATAPACK}/CHIPS.DAT")
      message(FATAL_ERROR "Run tools/make_datapack.py before a personal Vita build")
    endif()
    file(GLOB_RECURSE VCC_DATA_FILES RELATIVE "${VCC_DATAPACK}" "${VCC_DATAPACK}/*")
    foreach(data_file ${VCC_DATA_FILES})
      list(APPEND VCC_GAME_FILES FILE "${VCC_DATAPACK}/${data_file}" "data/${data_file}")
    endforeach()
  endif()

  vita_create_vpk(VitaChipsChallenge.vpk "VITA00001" vita_chips.self
    VERSION "01.00"
    NAME "Vita Chips Challenge"
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/assets/vita/icon0.png" "sce_sys/icon0.png"
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/assets/vita/pic0.png" "sce_sys/pic0.png"
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/assets/vita/bg0.png" "sce_sys/livearea/contents/bg0.png"
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/assets/vita/startup.png" "sce_sys/livearea/contents/startup.png"
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/assets/vita/template.xml" "sce_sys/livearea/contents/template.xml"
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/LiberationSans-Regular.ttf" "data/fonts/LiberationSans-Regular.ttf"
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/LiberationSans-Bold.ttf" "data/fonts/LiberationSans-Bold.ttf"
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/assets/music/GENMIDI.op2" "data/GENMIDI.op2"
    ${VCC_GAME_FILES})
endif()
