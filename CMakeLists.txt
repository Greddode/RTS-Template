cmake_minimum_required(VERSION 3.24)
project(rts C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # lets editors (VSCode/clangd) understand the code

# --- raylib ------------------------------------------------------------------
# Desktop: use the raylib installed on the system if there is one.
# Web: the system raylib is a Linux library, useless in a browser, so we always
#      download raylib's source and compile it with Emscripten instead.
include(FetchContent)
set(RAYLIB_URL https://github.com/raysan5/raylib/archive/refs/tags/6.0.tar.gz)

if (EMSCRIPTEN)
    set(PLATFORM Web CACHE STRING "raylib target platform" FORCE)
    FetchContent_Declare(raylib URL ${RAYLIB_URL} DOWNLOAD_EXTRACT_TIMESTAMP OFF)
else()
    FetchContent_Declare(raylib URL ${RAYLIB_URL} DOWNLOAD_EXTRACT_TIMESTAMP OFF
                         FIND_PACKAGE_ARGS 6.0)
endif()
FetchContent_MakeAvailable(raylib)
# When raylib is compiled from source (downloaded), hide warnings from its own
# code and bundled libraries: they aren't ours to fix. Our code keeps -Wall -Wextra.
get_target_property(RAYLIB_IMPORTED raylib IMPORTED)
if (NOT RAYLIB_IMPORTED)
    target_compile_options(raylib PRIVATE -w)
endif()

# --- game --------------------------------------------------------------------
# Every .c file under src/ (main.c, game/, editor/) is built; new files are picked up automatically.
file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS src/*.c)
add_executable(game ${GAME_SOURCES})
target_include_directories(game PRIVATE src src/game src/editor)   # so #include "units.h" works from any folder
target_compile_options(game PRIVATE -Wall -Wextra)
# Strict C99 for OUR code only. (Set globally, it would also apply to raylib
# when the web build compiles it, and raylib's audio code needs GNU C there.)
set_target_properties(game PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_link_libraries(game PRIVATE raylib m)

# Map files (maps/*.map), sprite art (assets/sprites/*/*.png) and the UI font
# (assets/fonts) ship with the game. The asset files are listed so that adding
# or changing one re-bundles the web build.
# The folder is (re)created if missing, so deleting all art never breaks the build.
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/assets/sprites)
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/assets/fonts)   # no font file = raylib's default font
file(GLOB_RECURSE SPRITE_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/assets/sprites/*)
file(GLOB_RECURSE FONT_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/assets/fonts/*)
if (EMSCRIPTEN)
    # Emit index.html + index.js / .wasm / .data: itch.io (and most hosts) open index.html.
    set_target_properties(game PROPERTIES OUTPUT_NAME index SUFFIX ".html")
    set_property(TARGET game APPEND PROPERTY LINK_DEPENDS ${CMAKE_SOURCE_DIR}/src/editor/web_download.js
                 ${CMAKE_SOURCE_DIR}/web/shell.html ${SPRITE_FILES} ${FONT_FILES})   # relink when they change
    target_link_options(game PRIVATE
        -sUSE_GLFW=3              # raylib's window/input layer, provided by Emscripten
        # Memory: the unit pool and the other fixed arrays are static data (about 13 MB with
        # MAX_UNITS 16384). Emscripten sizes the starting memory to fit them (29 MB here), so
        # changing MAX_UNITS needs no edit in this file; a 16,000-unit battle never grew past it.
        # Growth stays on as a safety net (bigger pools or maps): the heap can grow up to 2 GB.
        -sALLOW_MEMORY_GROWTH=1
        # Bundle maps/ and the art into the browser's virtual file system. "SHELL:"
        # keeps each flag with its folder: CMake drops repeated options otherwise.
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/maps@/maps"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets/sprites@/assets/sprites"
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets/fonts@/assets/fonts"
        --js-library ${CMAKE_SOURCE_DIR}/src/editor/web_download.js   # the editor's Save -> browser download
        "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/web/shell.html"     # our page instead of Emscripten's default one
    )
else()
    # Copy maps/ next to the executable on every build, even if no code changed,
    # so `make run` always picks up edited map files.
    add_custom_target(copy_maps ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/maps ${CMAKE_BINARY_DIR}/maps)   # game is built into the build folder
    add_dependencies(game copy_maps)
    add_custom_target(copy_sprites ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets/sprites ${CMAKE_BINARY_DIR}/assets/sprites)
    add_dependencies(game copy_sprites)
    add_custom_target(copy_fonts ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets/fonts ${CMAKE_BINARY_DIR}/assets/fonts)
    add_dependencies(game copy_fonts)
    # Builds made from source read and save maps in the project's maps/ folder
    # (so the editor's saves are kept and seen by git). A shipped game, without
    # this folder, uses the copy next to the executable.
    target_compile_definitions(game PRIVATE MAPS_SOURCE_DIR="${CMAKE_SOURCE_DIR}/maps"
                                            SPRITES_SOURCE_DIR="${CMAKE_SOURCE_DIR}/assets/sprites"    # same idea for art
                                            FONTS_SOURCE_DIR="${CMAKE_SOURCE_DIR}/assets/fonts")       # and the font
endif()
