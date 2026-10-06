# RTS

An RTS game template in C using [raylib](https://www.raylib.com/). Builds for Linux desktop and the web.

## Requirements

- CMake 3.24+
- Desktop: raylib (`sudo pacman -S raylib`)
- Web: Emscripten (`sudo pacman -S emscripten`, then open a new terminal)

## Build & run

```sh
make run     # build and run on desktop    (output: build/game)
make serve   # build for web and serve it   (open http://localhost:8080/game.html)
make clean   # delete build folders
```

`make desktop` / `make web` build without running. The real build config is in `CMakeLists.txt`; the Makefile just holds shortcuts.

## Controls

| Input | Action |
|---|---|
| WASD / arrows / middle-drag | Pan camera |
| Mouse wheel | Zoom |
| Left click / drag | Select unit / box select |
| Shift + select | Add to selection |
| Right click | Move selected units |

## Code layout (`src/`)

| File | System |
|---|---|
| `main.c` | Window, fixed 30 Hz sim loop, debug overlay |
| `config.h` | Settings shared between systems |
| `map.c` | Tile map: generation, walkability, culled drawing |
| `camera.c` | Pan / zoom, visible-area queries |
| `units.c` | Unit pool, movement, separation, drawing |
| `grid.c` | Spatial grid for nearby-unit queries |
| `path.c` | A* pathfinding: request queue, per-frame time budget, path smoothing |
| `input.c` | Selection and move orders |
