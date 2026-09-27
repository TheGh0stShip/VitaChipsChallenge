cmake_minimum_required(VERSION 3.20)
project(VitaChipsChallenge VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(vcc_core STATIC src/dat.c)
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

