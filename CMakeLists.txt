# Desktop build: simulator, tests, font packer.
#
# The device build is PlatformIO (platformio.ini) and shares src/core and
# src/hal. This file must stay in sync with the Makefile, which exists so a
# clone builds with nothing but a compiler.
cmake_minimum_required(VERSION 3.16)
project(rsspaper CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Wshadow)

# GLOB rather than an explicit list: the Makefile globs too, so adding a
# source file never means remembering to edit two build files.
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp)
file(GLOB_RECURSE SIM_SOURCES CONFIGURE_DEPENDS src/sim/*.cpp src/hal/*.cpp)
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS test/*.cpp)
file(GLOB FONTGEN_SOURCES CONFIGURE_DEPENDS tools/fontgen/*.cpp)

add_library(rsspaper_core STATIC ${CORE_SOURCES})
target_include_directories(rsspaper_core PUBLIC src third_party)

add_executable(rsspaper-sim ${SIM_SOURCES})
target_link_libraries(rsspaper-sim PRIVATE rsspaper_core)
target_include_directories(rsspaper-sim PRIVATE third_party/stb)

if(TEST_SOURCES)
  enable_testing()
  add_executable(rsspaper-tests ${TEST_SOURCES})
  target_link_libraries(rsspaper-tests PRIVATE rsspaper_core)
  target_include_directories(rsspaper-tests PRIVATE third_party/doctest)
  # Tests read test/fixtures relative to the repository root.
  add_test(NAME unit
           COMMAND rsspaper-tests
           WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()

if(FONTGEN_SOURCES)
  add_executable(fontgen ${FONTGEN_SOURCES})
  target_link_libraries(fontgen PRIVATE rsspaper_core)
  target_include_directories(fontgen PRIVATE third_party/stb)

  # Bake the font pack as part of the build so the simulator always has one.
  set(FONT_PACK ${CMAKE_BINARY_DIR}/literata.rfp)
  file(GLOB FONT_TTFS CONFIGURE_DEPENDS assets/fonts/*.ttf)
  add_custom_command(
    OUTPUT ${FONT_PACK}
    COMMAND fontgen --fonts ${CMAKE_SOURCE_DIR}/assets/fonts --out ${FONT_PACK}
    DEPENDS fontgen ${FONT_TTFS}
    COMMENT "Baking font pack")
  add_custom_target(fonts ALL DEPENDS ${FONT_PACK})
endif()
