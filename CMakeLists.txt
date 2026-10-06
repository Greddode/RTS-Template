cmake_minimum_required(VERSION 3.24)
project(rts C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)           # plain C99 (-std=c99), not GNU C
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

# --- game --------------------------------------------------------------------
# Every .c file under src/ (main.c, game/, editor/) is built; new files are picked up automatically.
file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS src/*.c)
add_executable(game ${GAME_SOURCES})
target_include_directories(game PRIVATE src src/game src/editor)   # so #include "units.h" works from any folder
target_compile_options(game PRIVATE -Wall -Wextra)
target_link_libraries(game PRIVATE raylib m)

# Map files (maps/*.map) ship with the game.
if (EMSCRIPTEN)
    set_target_properties(game PROPERTIES SUFFIX ".html")  # emit game.html + .js + .wasm
    target_link_options(game PRIVATE
        -sUSE_GLFW=3              # raylib's window/input layer, provided by Emscripten
        -sALLOW_MEMORY_GROWTH=1   # let the heap grow (lots of units later)
        --preload-file ${CMAKE_SOURCE_DIR}/maps@/maps   # bundle maps/ into the browser's virtual file system
    )
else()
    # Copy maps/ next to the executable on every build, even if no code changed,
    # so `make run` always picks up edited map files.
    add_custom_target(copy_maps ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/maps ${CMAKE_BINARY_DIR}/maps)   # game is built into the build folder
    add_dependencies(game copy_maps)
    # Builds made from source read and save maps in the project's maps/ folder
    # (so the editor's saves are kept and seen by git). A shipped game, without
    # this folder, uses the copy next to the executable.
    target_compile_definitions(game PRIVATE MAPS_SOURCE_DIR="${CMAKE_SOURCE_DIR}/maps")
endif()
