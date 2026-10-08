# Automated tests. Separate from the game build: `make test` builds and runs them.
#   controls_overflow_test            the Controls page at several window sizes (see the .c file)
#   controls_overflow_test_longnames  the same, with much longer unit / building names
cmake_minimum_required(VERSION 3.24)
project(rts_tests C)

set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/..)

# raylib: the same way as the game (installed one if present, else downloaded).
include(FetchContent)
FetchContent_Declare(raylib URL https://github.com/raysan5/raylib/archive/refs/tags/6.0.tar.gz
                     DOWNLOAD_EXTRACT_TIMESTAMP OFF FIND_PACKAGE_ARGS 6.0)
FetchContent_MakeAvailable(raylib)
get_target_property(RAYLIB_IMPORTED raylib IMPORTED)
if (NOT RAYLIB_IMPORTED)
    target_compile_options(raylib PRIVATE -w)
endif()

# The game's shared systems, minus menu.c: the test #includes it to reach the
# Controls page's internals. ui.c gets the hooks (text_hooks.h).
# The long-names build uses a copy of src/game whose config.h has the longest
# names made much longer. (A copy of the whole folder: #include "config.h"
# looks next to the including file first, so a copy of config.h alone would be ignored.)
file(GLOB GAME_FILES CONFIGURE_DEPENDS ${REPO}/src/game/*.c ${REPO}/src/game/*.h)
set(LONG_DIR ${CMAKE_BINARY_DIR}/longnames/game)
file(MAKE_DIRECTORY ${LONG_DIR})
foreach(file IN LISTS GAME_FILES)
    configure_file(${file} ${LONG_DIR} COPYONLY)   # copied again whenever the original changes
endforeach()
file(READ ${REPO}/src/game/config.h CONFIG_TEXT)
string(REPLACE "\"Archery Range\"," "\"Royal Archery Range of the Northern Marches\"," CONFIG_TEXT "${CONFIG_TEXT}")
string(REPLACE "\"Academy\","       "\"Academy of Arcane and Medical Sciences\","      CONFIG_TEXT "${CONFIG_TEXT}")
string(REPLACE "\"Knight\","        "\"Knight-Commander of the Silver Order\","        CONFIG_TEXT "${CONFIG_TEXT}")
string(REPLACE "\"Scout\","         "\"Outrider\","                                     CONFIG_TEXT "${CONFIG_TEXT}")
file(WRITE ${LONG_DIR}/config.h "${CONFIG_TEXT}")

foreach(variant IN ITEMS "" "_longnames")
    if (variant STREQUAL "_longnames")
        set(dir ${LONG_DIR})
    else()
        set(dir ${REPO}/src/game)
    endif()
    file(GLOB sources ${dir}/*.c)
    list(REMOVE_ITEM sources ${dir}/menu.c)
    set(exe controls_overflow_test${variant})
    add_executable(${exe} controls_overflow_test.c ${sources})
    target_include_directories(${exe} BEFORE PRIVATE ${dir})
    target_include_directories(${exe} PRIVATE ${REPO}/src)
    target_compile_options(${exe} PRIVATE -Wall -Wextra)
    target_compile_definitions(${exe} PRIVATE FONTS_SOURCE_DIR="${REPO}/assets/fonts")
    set_target_properties(${exe} PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_link_libraries(${exe} PRIVATE raylib m)
    set_source_files_properties(${dir}/ui.c PROPERTIES COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/text_hooks.h")
endforeach()
set_source_files_properties(controls_overflow_test.c PROPERTIES COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/text_hooks.h")
