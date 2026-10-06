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

## Maps

**Play** opens a map picker: **Random** (the generated map) plus every `.map` file in `maps/`.
The list is built by scanning the folder, so a new map needs no code change. On desktop the
build copies `maps/` next to the game (`make run` re-copies it each time); the web build bundles
it into the page.

Included: `arena.map` (32×32 combat test), `duel.map` (64×64 1v1), `river.map` (128×128, a
river with three bridges).

### Map file format

Plain text, one character per tile, so it's easy to edit and to diff. The full description is
in a comment block at the top of `maps/duel.map`.

```
# comment (outside the tile grid)
name Duel (64x64)
width 64                  # 8..128
height 64                 # 8..128
tiles                     # then exactly <height> rows of <width> characters
..,,~~~##...              # . grass   , dirt   ~ water   # rock (read as-is: not a comment here)
...
base 0 8 52               # <building> <team> <x> <y>   x,y = top-left tile; team 0 player, 1 AI
worker 0 12 51            # <unit> <team> <x> <y>       worker, melee, ranged, ...
gold 5 46 1500            # gold <x> <y> <amount>
```

Building and unit keywords are the names in `BUILDING_STATS` / `UNIT_STATS`, so new types work
in map files automatically. Each team needs at least one building. Mistakes (unknown character,
wrong row length, object on water or outside the map, overlapping buildings, ...) are shown on
screen as `file:line: what's wrong`, and the game plays the Random map instead.

## Map editor

Open it from the main menu (**Map Editor**, starts a blank 64×64 map) or press **F2** while
playing (opens the current map; **Exit** or F2 returns to the paused game exactly as it was).

| Tool | What it does |
|---|---|
| Tile brushes (Grass, Dirt, Water, Rock) | Left-click / drag to paint; brush size 1, 3 or 5. Water and rock never paint under an object. **Ctrl+Z** undoes painting (32 steps) |
| Player / AI | Which team new objects belong to |
| Base, Barracks, Worker, Melee, Ranged | Click to place; a green/red ghost shows if it fits (same rules as map files) |
| Gold + amount | Click to place a gold node with that amount |
| Erase object | Click (or drag over) objects to remove them |
| New map 32 / 64 / 128 | Start again, all grass |
| Save | Writes `maps/<name>.map`, then reads it back with the normal loader and shows any error (e.g. a team with no building) |
| Load | Pick from the same list as the map picker |
| Test Play | Saves a temporary copy and starts a game on it; Esc → **Back to Editor** (or F2) returns |

The camera pans and zooms as in the game. The tool buttons are generated from `TILE_INFO`,
`BUILDING_STATS` and `UNIT_STATS`, so new tiles or types appear automatically.

**Where maps are saved:** builds made from this source save into the project's `maps/` folder,
so new maps show up in git. A shipped game saves next to the executable. **In the browser**,
Save downloads the file instead (browsers can't write to disk), and Load is disabled.

## Winning and losing

A side with no buildings left (finished or unfinished) loses. The check runs once per second,
after a 5 second grace period at the start. Victory / Defeat freezes the game and offers
**Play Again** (same map) or **Main Menu**.

## Controls

The game opens on a main menu (Play → pick a map / Controls / Exit). The in-game Controls page is
generated from `CONTROLS` and the key bindings in `config.h`; this table mirrors it.

| Input | Action |
|---|---|
| Arrows / middle-drag | Pan camera |
| Mouse wheel | Zoom (over the inspector's buttons: scroll them) |
| Left click / drag | Select unit / box select |
| Left click own building | Select it: the inspector shows HP, queue and Train buttons |
| Left click gold node | Inspect gold left |
| Shift + select | Add to selection |
| Right click | Move selected units |
| Right click ground (building selected) | Set its rally point (blue flag): newly trained units walk there |
| Right click on enemy unit or building | Attack it |
| Right click on gold (workers selected) | Mine it: workers carry gold to the nearest base and repeat |
| Right click your unfinished building (workers selected) | Workers help build it |
| A, then right click | Attack-move: walk there, fighting any enemies met on the way (left click or Esc cancels) |
| S | Stop: drop all orders (units still fight enemies that come close) |
| H | Hold position: stay put, only attack enemies already in range |
| W (Base selected) | Train a Worker (50); queue up to 5 |
| M / R (Barracks selected) | Train Melee (75) / Ranged (100); queue up to 5 |
| Click a queue icon (building selected) | Cancel that unit, gold refunded (destroying the building loses its queue) |
| B / K (workers selected) | Build a Base (400) / Barracks (150): a ghost follows the mouse, green = OK, red = blocked; left click places, right click / Esc / the key again cancels |
| Esc | Cancel a pending attack-move or building placement; otherwise open the pause menu |
| F1 | Debug: spawn a wave of 20 enemies |
| F2 | Map editor on the current map (F2 / Exit returns to the paused game) |
| Ctrl+Z (editor) | Undo tile painting |

Esc never quits the game directly; use Exit in a menu or close the window. (The web build has no Exit buttons.)

## Code layout

`maps/` holds the map files. Source is in `src/`: `main.c` (game states) at the top,
`src/game/` for the game and the systems the editor reuses (ui, map, map files, tables),
and `src/editor/` for the editor.


| File | System |
|---|---|
| `main.c` | Window, game states (menu / playing / paused / victory / defeat / editor), fixed 30 Hz sim loop, new game (map file or Random), win/lose check, editor ↔ game hand-over, debug overlay |
| `game/config.h` | Shared settings: tick rate, teams, unit and building stats tables, game states, key bindings, controls list |
| `game/map.c` | Tile map: generated "Random" map, real size of the loaded map, walkability (terrain + building-blocked tiles), culled drawing |
| `game/mapfile.c` | Map files: `MapDoc` (tiles + objects), parse + full validation with file:line errors, load into the game, write, scan the folder |
| `editor/editor.c` | Map editor: tile brushes with undo, object tools, save / load / test play |
| `game/camera.c` | Pan / zoom, visible-area queries |
| `game/units.c` | Unit pool, movement, separation, drawing |
| `game/grid.c` | Spatial grid for nearby-unit queries |
| `game/path.c` | A* pathfinding: request queue, per-frame time budget, path smoothing |
| `game/input.c` | Selection list (units, building, gold node), orders, hotkeys, building placement ghost |
| `game/combat.c` | Attacking, chasing, auto-targeting (aggro), projectile pool |
| `game/ai.c` | Enemy AI: mines with its workers, builds a Barracks, trains combat units there on a timer, sends idle units at the player |
| `game/economy.c` | Gold per team, gold node pool, worker mining loop, gold HUD (top right) |
| `game/buildings.c` | Building pool, tile blocking, placement checks, production queue (cancel/refund), rally points, gold drop-off lookup, construction by workers, drawing |
| `game/ui.c` | Tiny immediate-mode UI (buttons, panels, labels, tabs, scroll areas), scales with window height, blocks clicks from reaching the game |
| `game/menu.c` | Main menu, map picker, pause menu, Controls page, Victory / Defeat screen |
| `game/inspector.c` | Bottom panel for the selection; Train / Build buttons generated from the stats tables; hotkey clash check |
