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
# Every .c file in src/ is part of the game; new files are picked up automatically.
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS src/*.c)
add_executable(game ${GAME_SOURCES})
target_compile_options(game PRIVATE -Wall -Wextra)
target_link_libraries(game PRIVATE raylib m)

if (EMSCRIPTEN)
    set_target_properties(game PROPERTIES SUFFIX ".html")  # emit game.html + .js + .wasm
    target_link_options(game PRIVATE
        -sUSE_GLFW=3              # raylib's window/input layer, provided by Emscripten
        -sALLOW_MEMORY_GROWTH=1   # let the heap grow (lots of units later)
    )
endif()
