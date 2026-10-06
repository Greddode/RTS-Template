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
| Arrows / middle-drag | Pan camera |
| Mouse wheel | Zoom |
| Left click / drag | Select unit / box select |
| Left click own base | Select the base |
| Shift + select | Add to selection |
| Right click | Move selected units |
| Right click on enemy unit or building | Attack it |
| Right click on gold (workers selected) | Mine it: workers carry gold to the nearest base and repeat |
| A, then right click | Attack-move: walk there, fighting any enemies met on the way (left click or Esc cancels) |
| S | Stop: drop all orders (units still fight enemies that come close) |
| H | Hold position: stay put, only attack enemies already in range |
| W (base selected) | Train a worker (50 gold, queue up to 5) |
| F1 | Debug: spawn a wave of 20 enemies |

Esc does **not** quit (it cancels orders); close the window to exit.

## Code layout (`src/`)

| File | System |
|---|---|
| `main.c` | Window, fixed 30 Hz sim loop, starting setup (bases, gold, units), debug overlay, F1 debug key |
| `config.h` | Shared settings: tick rate, teams, unit and building stats tables (hp, damage, cost, train time, ...) |
| `map.c` | Tile map: generation, walkability (terrain + building-blocked tiles), culled drawing |
| `camera.c` | Pan / zoom, visible-area queries |
| `units.c` | Unit pool, movement, separation, drawing |
| `grid.c` | Spatial grid for nearby-unit queries |
| `path.c` | A* pathfinding: request queue, per-frame time budget, path smoothing |
| `input.c` | Selection (units and buildings), orders, hotkeys, selected-building panel |
| `combat.c` | Attacking, chasing, auto-targeting (aggro), projectile pool |
| `ai.c` | Enemy AI: mines with its workers, trains combat units on a timer, sends idle units at the player |
| `economy.c` | Gold per team, gold node pool, worker mining loop, gold HUD |
| `buildings.c` | Building pool, tile blocking, production queue, drawing |
