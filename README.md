# RTS Kit

A small, complete real-time strategy game in plain **C99 + [raylib](https://www.raylib.com/)**,
made to be read, changed and extended. It runs on **Linux desktop and in the browser**
(WebAssembly), and it's built to run smoothly on a 4 GB Celeron laptop.

**What you get:** workers that mine gold and construct buildings, 9 unit types (Melee, Archer,
Knight, Worker, Medic, Mage, Scout, and the flying Falcon and Airship) with armor and damage
types, splash damage, healers and air/ground targeting, 5 production buildings plus a Guard Tower, with prerequisites, a computer opponent that builds, expands and attacks,
fog of war, a minimap, A* pathfinding with a per-frame time budget, a map editor with
test play, swappable PNG art (placeholders included), a readable UI font (Inter), a debug
overlay (F3), and a web build ready to upload to itch.io. Units, buildings, tiles and damage types are rows in tables in
`config.h`: most new content needs no other code.

Version **1.0.0** (`GAME_VERSION` in `config.h`, shown on the main menu). Licences of the
libraries used: [THIRD_PARTY.md](THIRD_PARTY.md). Release checklist: [PLAYTEST.md](PLAYTEST.md).

## Screenshots

Suggested shots for a store page (on desktop, raylib saves the window to `screenshot000.png`
when you press **F12**):

1. A big fight on River Crossing: Knights, Archers and Mages with splash rings and heal lines.
2. A base with all four buildings, workers mining, the inspector showing a building's queue.
3. Fog of war and the minimap: explored terrain dimmed, enemy buildings remembered.
4. The map editor painting a map, then Test Play.
5. The F3 debug overlay over a 700-unit battle (shows it runs at 60 FPS).
6. The same scene with art deleted: coloured shapes (shows the art is swappable).
7. The web build in a browser tab.

## Quick start

**1. Install the tools** (Linux; Windows and macOS aren't tested, see [Known limitations](#known-limitations)):

| Distro | Command |
|---|---|
| Arch | `sudo pacman -S base-devel cmake raylib python` |
| Ubuntu / Debian | `sudo apt install build-essential cmake git python3 libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev` |
| Fedora | `sudo dnf install gcc make cmake git python3 libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel mesa-libGL-devel` |

You need CMake 3.24+. **raylib is optional:** if raylib 6.0 or newer isn't installed (Ubuntu's
and Fedora's packages are older), the first build downloads and compiles it (needs internet,
about a minute and a half on the Celeron). The X11 / GL packages above are what that needs.
(The Ubuntu line is built automatically on GitHub, see [Automatic builds](#automatic-builds).
The Fedora line uses the usual package names for raylib's needs and isn't tested.)

For the **web build**, also install **Emscripten**: `sudo pacman -S emscripten` on Arch (then open
a new terminal), or anywhere with the official SDK: <https://emscripten.org/docs/getting_started/downloads.html>
(`./emsdk install latest && ./emsdk activate latest && source ./emsdk_env.sh`).

**2. Build and run:**

```sh
make run       # build and run on desktop    (output: build/game)
make serve     # build for web and serve it   (open http://localhost:8080/index.html)
make web-zip   # build for web and zip it for itch.io   (output: build-web/rts-kit-web.zip)
make release   # clean builds of both + the web zip and a source zip in dist/
make test      # build and run the automated tests (tests/)
make clean     # delete build folders
```

`make desktop` / `make web` build without running. The real build config is in `CMakeLists.txt`;
the Makefile just holds shortcuts. The first web build compiles raylib, so it takes a minute or two.
In a game, **Esc** (**Ctrl** in the web build) opens the pause menu and the **Controls** page; F3 shows the debug overlay.

**Where to change things:**

| I want to... | Section |
|---|---|
| add a unit | [Adding a new unit type](#adding-a-new-unit-type-walkthrough) |
| add a building (or a prerequisite) | [the Temple example](#adding-a-new-unit-type-walkthrough) in the same section |
| make a map | [Maps](#maps), [Map editor](#map-editor) |
| replace the art | [Art (sprites)](#art-sprites) |
| add an armor or damage type | [Damage and armor](#damage-and-armor) |
| tune the AI | [Computer opponent (AI)](#computer-opponent-ai): every number is a named constant in `config.h` |
| make the AI use a new unit or building | [Extending the AI](#extending-the-ai) |
| change keys | `KEY_...` defines and the `CONTROLS` table in `config.h` |
| add a tile, or change who can cross one | [Tiles and movement classes](#tiles-and-movement-classes) |
| change the font | [Font](#font) |
| change a unit's or building's description | the `description` column, see [Adding a new unit type](#adding-a-new-unit-type-walkthrough) |

## Maps

**Play** opens a map picker: **Random** (the generated map) plus every `.map` file in `maps/`.
The list is built by scanning the folder, so a new map needs no code change. On desktop the
build copies `maps/` next to the game (`make run` re-copies it each time); the web build bundles
it into the page.

Included: `dire_straight_64x64.map` ("Dire Straight", 64×64) and `river_crossing_128x128.map`
("River Crossing", 128×128), both made with the map editor, and `islands_64x64.map` ("Islands",
64×64): two islands with lava fields and water between them and no ground path, so the only way
across is by air (the AI learns to ferry its army there, see below), and `harbor_64x64.map`
("Harbor", 64×64): a bay between the two bases with room for a Dock on each shore, and land
bridges north and south so the ground war still works (see [Naval units](#naval-units-dock-boat-ship)).
**The full file format, the rules a map must follow and tips for fair maps are in
[`maps/FORMAT.md`](maps/FORMAT.md).**

### Map file format

Plain text, one character per tile, so it's easy to edit and to diff. In short (the full guide
is [`maps/FORMAT.md`](maps/FORMAT.md)):

```
# comment (outside the tile grid)
name Duel (64x64)
width 64                  # 8..128
height 64                 # 8..128
tiles                     # then exactly <height> rows of <width> characters
..,,~~~##::^              # . grass  , dirt  : gravel  ~ water  # rock  ^ lava  (# is read as-is here, not a comment)
...
base 0 8 52               # <building> <team> <x> <y>   x,y = top-left tile; team 0 player, 1 AI
worker 0 12 51            # <unit> <team> <x> <y>       worker, melee, archer, knight, ...
archery_range 0 20 50     # a space in a name is written _
gold 5 46 1500            # gold <x> <y> <amount>   (gold has no team: anyone can mine it)
```

Building and unit keywords are the names in `BUILDING_STATS` / `UNIT_STATS` (any case, spaces
written as `_`: "Archery Range" → `archery_range`), so new types work in map files automatically. Each team needs at least one building. Mistakes (unknown character,
wrong row length, object on a tile it can't stand on or outside the map, overlapping buildings, ...) are shown on
screen as `file:line: what's wrong`, and the game plays the Random map instead.

## Tiles and movement classes

Every unit has a **movement class** (`moveClass` in `UNIT_STATS`, the `MoveClass` enum in
`config.h`), and every tile says which classes may cross it (`TILE_INFO` in `src/game/map.c`):

| Tile | Map character | GROUND (and buildings, gold) | NAVAL | AIR |
|---|---|---|---|---|
| Grass | `.` | yes | | yes |
| Dirt | `,` | yes | | yes |
| Gravel | `:` | yes | | yes |
| Water | `~` | | yes | yes |
| Rock | `#` | | | yes |
| Lava | `^` | | | yes |

- **GROUND** units walk on land; **NAVAL** units sail on water. Buildings block both.
- **AIR** units never pathfind: they fly in a straight line over every tile whose `air` column
  is `true` (all six) and over buildings, and they can't leave the map.
- **The Falcon and the Airship are AIR units** (see [Flying units](#flying-units-what-can-hit-what-and-unit-prerequisites));
  the **Boat and the Ship are NAVAL** (see [Naval units](#naval-units-dock-boat-ship)); the
  other seven are GROUND. There are no landings: naval units don't carry troops.
- **One check for everything:** `MapTileWalkable(class, x, y)` (map.c) is used by pathfinding,
  movement, straight-line checks (`MapLineClear`), formation spots (`UnitsOpenSpots`) and the
  "can I get there?" regions (`PathRegion(class, pos)`, which the AI uses). Change a column in
  `TILE_INFO` and all of them follow. Buildings and gold always need a GROUND tile.
- **Lava** is simply impassable for ground and naval units for now (it doesn't hurt anything).

**Colours** were picked to stay easy to tell apart three ways: in full light, dimmed by the fog
of war (explored but not visible), and dimmed on the minimap. Rock is much darker than before
(62,60,70) so it stands out from the light grey gravel, but not so dark that explored rock
under the fog looks like unexplored black. Lava (orange) is kept away from the red AI dots on
the minimap. Placeholder art: `assets/sprites/tiles/gravel.png`, `lava.png`, `rock.png`.

**To add a tile:** add it to `TileType` in `map.h` (before `TILE_COUNT`), give it a row in
`TILE_INFO` with a map character no other tile uses (not a space), its colour and its three
columns, and optionally `assets/sprites/tiles/<name>.png`. The editor gets a brush button, the
map loader the character, and the minimap the colour automatically. `make test` runs
`tile_class_test`, which lists the expected answer for every tile and class: add your tile's
row there too.

## Web build

The same C code runs on desktop and in the browser. The few differences are `#if defined(__EMSCRIPTEN__)`
blocks: the browser drives the main loop, Exit buttons are hidden, maps and art come from
`/maps` and `/assets/sprites` (bundled with `--preload-file`), and the editor's Save downloads the file (Load is off).
`make serve` builds it and serves it at http://localhost:8080/index.html. Opening the `.html`
file directly doesn't work: browsers won't load the game's files from `file://`.

**The page** is `web/shell.html` (linked with `--shell-file`; edit it to change the title,
colours or loading screen). Emscripten turns it into `build-web/index.html` next to `index.js`,
`index.wasm` and `index.data` (the bundled maps and art). It has:
- a dark background and the game canvas centred, **scaled to fit the window with its aspect
  ratio kept** (bars on the sides or top/bottom). The game draws at `SCREEN_W` × `SCREEN_H`
  (1280×720, in `config.h`), so that's its resolution and aspect ratio in the browser. On desktop
  the window is resizable instead.
- "Loading..." with a progress bar while the files download, gone when the game starts;
- no right-click menu on the canvas (right click gives orders), no page scrolling, no text
  selection, no Emscripten logo or output box. The game's log goes to the browser console (F12).
- the browser's own function-key actions are blocked (F3 would open "find in page", F1 help),
  since F1–F4 are game keys. F5 (reload), F11 (fullscreen) and F12 (developer tools) still work.

**Publishing on itch.io:** `make web-zip` makes `build-web/rts-kit-web.zip` with `index.html`,
`index.js`, `index.wasm` and `index.data` at the top level. On itch.io: *Kind of project* →
**HTML**, upload the zip, tick **"This file will be played in the browser"**, and set the
viewport to **1280 × 720** (or tick *Click to launch in fullscreen*: the page scales to any size).

## Map editor

Open it from the main menu (**Map Editor**, starts a blank 64×64 map) or press **F2** while
playing (opens the current map; **Exit** or F2 returns to the paused game exactly as it was).

| Tool | What it does |
|---|---|
| Tile brushes (Grass, Dirt, Water, Rock, Gravel, Lava) | Left-click / drag to paint; brush size 1, 3 or 5. A tile is never painted under an object that couldn't stand on it (water or lava under a Base, land under a Boat), and painting land never takes the last water from a Dock. **Ctrl+Z** undoes painting (32 steps) |
| Player / AI | Which team new objects belong to |
| Base, Barracks, Archery Range, Academy, Air Factory, Guard Tower, Dock | Click to place; a green/red ghost shows if it fits (same rules as map files), and a refused click says why ("Dock must be next to water") |
| Melee, Archer, Worker, Knight, Medic, Mage, Scout, Falcon, Airship, Boat, Ship | Click or drag to place units. The **brush size** (1, 3, 5) places a 1×1, 3×3 or 5×5 block centred on the cursor, one unit per tile. Tiles that can't take that unit (for a ground unit water or rock, for a Boat or Ship anything but water; a building or a unit already there) are skipped: the ghost shows every tile, green = a unit goes there, red = skipped. A drag never puts two units on one tile. At the limit (`MAP_MAX_OBJECTS`, 1,024 objects per map) the stroke stops with a message |
| Gold + amount | Click to place a gold node with that amount |
| Erase object | Click (or drag over) objects to remove them |
| New map 32 / 64 / 128 | Start again, all grass |
| Save | Writes `maps/<name>.map`, then reads it back with the normal loader and shows any error (e.g. a team with no building) |
| Load | Pick from the same list as the map picker |
| Test Play | Saves a temporary copy and starts a game on it; Esc → **Back to Editor** (or F2) returns |

The camera pans and zooms as in the game, except that you can zoom out until the **whole map**
fits beside the tool panel, whatever the map and window size (`CamUpdateEditor()` in
`camera.c`; the game keeps its own limits, 0.5 to 2). Going back to the game (F2 / Exit) restores
its camera exactly. The tool buttons are generated from `TILE_INFO`, `BUILDING_STATS` and
`UNIT_STATS`, so new tiles or types appear automatically.

**Where maps are saved:** builds made from this source save into the project's `maps/` folder,
so new maps show up in git. A shipped game saves next to the executable. **In the browser**,
Save downloads the file instead (browsers can't write to disk), and Load is disabled.

## Computer opponent (AI)

Every 2 seconds the AI:

1. **Barracks:** once it has 3 workers and 150 gold, one worker builds a Barracks near its base.
   If gold piles up past 600 while every Barracks has a full queue, it builds another (up to 3).
   **Tech buildings:** once a Barracks is finished, it builds one of each building in
   `AI_TECH_ORDER` (Archery Range, Academy, Air Factory, then **2 Guard Towers** by its main
   base), each after the one before is finished, rebuilt if destroyed. It pauses army training
   while it saves up for the next one, and uses any it was given by the map file. One that doesn't
   fit near its base is skipped rather than saved for. `AI_MAIN_BASE_TOWERS` (0–3) sets how many
   towers; `AI_BUILDS_TOWERS 0` turns them off. Expansions get no towers.
2. **Workers:** each base aims for **8 workers per reachable gold node** near it (at most **16**),
   training at the base that needs them most. When every base is saturated it stops, and the
   gold goes into the army.
3. **Expansion to gold fields:** gold is treated like StarCraft mineral fields. A **field** is a
   gold node plus every node within 7 tiles of it (`AI_FIELD_TILES`). The AI expands to the
   nearest field that:
   - still holds **3,000+ gold** that no Base is near yet: a node with any Base (either side,
     even unfinished) within 10 tiles is taken (`AI_EXPAND_MIN_GOLD`, `AI_CLAIMED_TILES`);
   - it can **reach**, with no enemy building within 20 tiles (`AI_EXPAND_ENEMY_TILES`).

   Of the overlapping fields around that spot it takes the one with the most gold. It then puts the
   Base where it's **closest to all of that field's nodes**, on open reachable ground, keeping
   2 tiles of open ground between the Base and any gold so workers can walk around
   (`AI_BASE_GOLD_GAP`). With a base's cost plus a reserve it sends **one** worker to build it.
   It saves up for one (pausing army training) once its workers are saturated or its own gold is
   running low. One expansion at a time, at most 3 bases. If the builder dies, the site is
   cancelled (refunded) and that field isn't tried again.
4. **Army:** idle workers go to the near node with the fewest workers; combat units attack the
   nearest player unit **it can hit** or building (Medics follow along and heal; Mages hold fire
   while their own units are in the splash; Melee and Knights are never sent after flyers). Every 5 seconds it queues units by the **army mix**,
   `AI_ARMY_MIX` in `config.h`:

   | Type | Share | Cap (alive) |
   |---|---|---|
   | Melee | 4 | |
   | Knight | 2 | |
   | Archer | 3 | |
   | Mage | 1 | |
   | Scout | 1 | 2 |
   | Medic | 1 | 4 |
   | Falcon | 1 | 4 |

   Each time it picks the type furthest below its share (counting units alive and queued), at a
   building that trains it and has room in its queue (2 per building), until the queues are full.
   Types whose `requires` building it lacks are skipped.
   **Air defence:** for every player flyer it can see it wants `AI_ANTI_AIR_PER_FLYER` (2) units
   that can hit air (alive + queued). While it has fewer, it only trains types with `hitsAir`
   (Archers, Scouts, Mages, Falcons), even while saving for a building, and keeps its gold for
   them while their buildings are busy. Falcons are its only fighting flyers: it never trains
   Airships to fight (only to ferry, 5.), and never builds a Dock or trains Boats or Ships.
   If it can't afford that type yet, it stops and saves for it instead of buying something
   cheaper. Change the shares to change its style. A new unit type is used once it has a row here.
5. **Ferrying (`ai_ferry.c`):** if **no** player building can be reached on foot from its base
   (different ground regions, e.g. on Islands), it needs transport. Then it builds what the
   Airship needs (Barracks, Academy, Air Factory, saving gold like for an expansion) and trains an
   Airship; more (up to `AI_FERRY_MAX_AIRSHIPS`) when every one is busy and gold piles up. Each
   Airship gathers idle combat units near the Air Factory (never workers; at most
   `AI_FERRY_MEDICS_PER_TRIP` Medic, only with `AI_FERRY_ESCORT_MIN` fighters; never units in a
   fight), flies them to a **drop point** and unloads, and the landed units attack as usual.
   The drop point is the nearest tile to the target, searched in a spiral, that is open ground
   on the target's island, outside the range of towers it knows about, not in the middle of
   enemy units, and with no anti-air near it or on the way there (it only counts what it can see,
   so `AI_SEES_THROUGH_FOG` applies). If none is found near the target it tries near the shore.
   **Safety:** it turns back if it loses `AI_FERRY_ABORT_DAMAGE` of its HP on the way, or if
   anti-air appears at the drop point and no other safe one is left (once within
   `AI_FERRY_COMMIT_TILES` it lands anyway). Back home it lets the group out. Units are only ever
   let out on open ground (the transport code itself never unloads onto water, lava or a
   building). Every lost Airship is counted: it waits `AI_FERRY_RETRY_SECONDS` before training
   another, and gives up ferrying after `AI_FERRY_MAX_FAILURES` (F3 shows it).
   On a map where everything is reachable on foot none of this happens: the AI plays exactly as
   before (checked: same results to the tick on Random, Dire Straight and River Crossing).
   `AI_FERRY_EXPANSION` (default 0): an idle Airship can carry a Worker to build a Base on a gold
   field on another island.

Every number (thresholds, distances, caps, timings) is a named constant in the **AI tuning**
block of `config.h`. The debug overlay (**F3**, top left) shows the AI's gold, workers
(have/target), bases, and what it's currently trying to do.

## Extending the AI

**What it does today:** builds Barracks (up to 3), then an Archery Range, an Academy, an Air
Factory and 2 Guard Towers by its main base; keeps 8 workers per gold node; expands to up to 3
bases; trains Melee, Knights, Archers, Mages, Scouts, Medics and Falcons by the army mix; switches
to anti-air units when it sees your flyers; and ferries its army by Airship when it can't walk
to you. All of that is driven by three places in `src/game/config.h`, so most changes are a
table row, not code:

| Edit | What it controls | Example |
|---|---|---|
| `AI_ARMY_MIX` | Which units it trains, how many of each (`share`), and a cap (`maxAlive`, 0 = none). A type that isn't listed is never trained. | `{ UNIT_FALCON, 1, 4 }`: one Falcon per 11 fighters, at most 4 alive |
| `AI_TECH_ORDER` | Which buildings it builds after its first Barracks, in order. A type listed twice means two of them. A unit is only trained once the building it's `trainedAt` (and its `requires`) is finished, so a unit in the mix needs its building here. | `BUILDING_AIR_FACTORY` after `BUILDING_ACADEMY` |
| The **AI tuning** block | Every number: timings, worker targets, expansion rules, Barracks cap, anti-air, ferrying, `AI_BUILDS_TOWERS` / `AI_MAIN_BASE_TOWERS`. | `AI_ANTI_AIR_PER_FLYER 2` |

Two safety rules hold for anything you add: it never saves up for a building that doesn't fit
near its base (it skips it), and it only places a building where `BuildingCanPlace` allows. A
unit it can't train yet (no building, or the `requires` building missing) is skipped, so a wrong
row can't stall it.

### Worked example: make the AI train Spearmen

Follow [Adding a new unit type](#adding-a-new-unit-type-walkthrough) first, so `UNIT_SPEARMAN`
exists with its row (`trainedAt BUILDING_BARRACKS`, `cost 80`, `hitsAir false`). Then add one
line to `AI_ARMY_MIX`:

```c
    { UNIT_SPEARMAN,  2,     0 },   // 2 Spearmen per ~13 fighters, no cap
```

That's all. The Barracks is the AI's first building, so it needs no `AI_TECH_ORDER` row; had you
made it `trainedAt BUILDING_TEMPLE`, you would add `BUILDING_TEMPLE` to `AI_TECH_ORDER` too.
Because `hitsAir` is false, it won't train Spearmen while it's short of anti-air, and idle
Spearmen are never sent after flyers. To check it, play a game: Spearmen join the enemy's waves
once its Barracks is up (scout for them, or turn fog off in the pause menu). `make test` still
passes; its AI test checks the Falcon, not your row.

### Not covered (good places to extend)

These need code in `ai.c` / `ai_ferry.c`, not just a row:

- **Airship bombing runs.** The AI flies Airships only as ferries; it never sends one to bomb.
- **Naval units and Docks.** It ignores water: no Dock, Boats or Ships. (`AI_BUILDS_DOCKS 1` is
  an unfinished start: it adds a Dock and Boats to the tables, but no naval tactics; it is off
  and isn't part of the tested behaviour.)
- **A new movement class.** Targets are filtered by `moveClass`, but expansion, ferrying and
  "can I reach the player" all assume ground units walking (plus Airships carrying them).
- **Towers at expansions.** Tech buildings all go by its main base.

## Debug overlay (F3)

**F3** shows or hides it on every screen, desktop and web. It's hidden at startup in release
builds (`make run`, `make web`) and shown in debug builds: `DEBUG_OVERLAY_DEFAULT` in
`overlay.h`. While playing it shows (top left):
- FPS now, plus the minimum and average over the last 5 s, and the frame time in ms;
- sim tick, fog and pathfinding times, paths queued, and what drawing the overlay itself costs;
- units per team (and of the pool), projectiles, buildings per team;
- the AI's gold, workers, bases, Barracks / Archery Range and current plan.

In the menus, the pause menu, Victory / Defeat and the editor it's one FPS line, top right. It
never blocks clicks and stays clear of the gold counter, minimap and inspector. Hidden, it costs
nothing (one key check a frame). The performance line in the console (`PERF: ...` every 5 s)
counts frames by itself, so it's correct either way. Draw calls aren't shown: raylib has no
cheap way to count them.

## Fog of war

Each team sees only what's near its units and buildings: **black** = never seen, **dimmed** =
seen before (terrain, gold and enemy buildings you saw are remembered, units aren't), **full
colour** = in sight right now. Each unit and building type has a `sight` (in tiles) in the stats
tables (defaults `UNIT_SIGHT` / `BUILDING_SIGHT` in `config.h`).

You can't see, click or auto-target enemies under fog, and units stop chasing a target that goes
into it. `AI_SEES_THROUGH_FOG` (default 1) lets the AI ignore fog so it isn't crippled.
`FOG_OF_WAR_ENABLED` turns fog off entirely, and the pause menu has a **Fog of war: On/Off**
button. The map editor never shows fog.

## Minimap

Bottom-left, scaled with the UI. Terrain comes from the tile table (one pixel per tile), with the
fog applied: black = never seen, dimmed = explored. Dots show your units and buildings, enemies
you can see right now, enemy buildings you've seen, and gold you've explored. The white outline is
the camera's view. The terrain/fog picture is a cached texture redrawn only when the map or fog
changes (at most 5× a second); `MINIMAP_ENABLED` in `config.h` turns it off.

## Art (sprites)

Units, buildings and map tiles can use your own PNGs. Anything without a PNG is drawn as the
usual coloured shape, so you can replace art one type at a time. Placeholder PNGs are included
as templates to paint over.

```
assets/sprites/units/      melee.png  archer.png  worker.png  knight.png  medic.png  mage.png  scout.png  falcon.png  airship.png  boat.png  ship.png   (names from UNIT_STATS)
assets/sprites/buildings/  base.png   barracks.png  archery_range.png  academy.png  air_factory.png  guard_tower.png  dock.png  (names from BUILDING_STATS)
assets/sprites/tiles/      grass.png  dirt.png  water.png  rock.png  gravel.png  lava.png  (names from TILE_INFO)
```

**Naming:** the file name is the type's `name` from the table, in lower case, with spaces
written as `_` ("Gold Mine" → `gold_mine.png`). A new type added to a table picks up its PNG
automatically. Upper/lower case in the file name doesn't matter.

**How it's drawn:**
- **Units** are fitted into the unit's circle (12×12 world pixels by default; 32×32 PNGs are
  recommended). Draw them **facing right**: they're mirrored when walking left.
- **Buildings** are fitted into their footprint (Base 96×96; Barracks, Archery Range and Academy
  64×64 at 32 px per tile).
- Units and buildings keep their PNG's aspect ratio. **Tiles** are stretched to fill one tile
  (32×32) and should tile seamlessly.
- Units and buildings are **tinted with the team colour**: the tint multiplies the PNG, so paint
  team-coloured parts white or grey and keep other parts dark. Tiles aren't tinted.
- Health bars, selection circles, fog and the minimap don't change (the minimap keeps flat colours).

**Size limits:** at startup all PNGs are packed into one texture (the atlas), at most
**2048×2048**. So one PNG can be at most 2046×2046 (1 px border on each side), and all of them
together must fit. Art that doesn't fit falls back to its shape.

**Debugging:** the console (desktop terminal, browser console on the web) has one `SPRITES:`
line for each type without art, each file that couldn't be read or didn't fit, and each PNG
whose name matches no type. Then a summary with the atlas size and load time.

**Where the art is read from:** builds made from this source read the project's
`assets/sprites/` directly, so a new PNG shows up the next time you start the game, no rebuild
needed. A shipped game reads the copy next to the executable. The web build bundles the folder
into the page (rebuild after changing art). For crisp pixel art, set `SPRITES_FILTER` to
`TEXTURE_FILTER_POINT` in `sprites.h`.

## Font

All text is drawn in **Inter** (`assets/fonts/Inter-Regular.ttf`, SIL Open Font License, see
[THIRD_PARTY.md](THIRD_PARTY.md)). Every string goes through two functions in `ui.c`:
`UiLabel()` draws and `UiTextWidth()` measures. Nothing else calls raylib's `DrawText` /
`MeasureText`, so the font is set in one place. `UiTextWrapped()` word-wraps text to a width
(used by the description box and the Controls page).

- **Use another font:** put a `.ttf` or `.otf` in `assets/fonts/` and set `UI_FONT_FILE` in
  `ui.c`. Fonts under the SIL OFL or Apache licence can ship with a commercial game; add its
  licence to `THIRD_PARTY.md`.
- **Sizes:** a font file is turned into a texture of letters at one pixel size, and text drawn
  much bigger or smaller than that looks blurry. So the font is loaded twice, at the normal
  text size (`UI_FONT_SIZE`, 20) and at title size (`UI_FONT_TITLE`, 34), both scaled with the
  window height like the rest of the UI, and reloaded when the window is resized. Each piece of
  text uses the nearer one. Loading takes about 6 ms on the Celeron (13 ms in the browser).
- **Letters:** Basic Latin plus Latin-1 (English and the accented letters of most western
  European languages). For other alphabets, add their code points in `UiFontLoad()`.
- **Missing file:** the game uses raylib's built-in font and logs one `UI font:` warning.
- The web build bundles `assets/fonts` like the art (rebuild after changing it).

## Damage and armor

Every unit deals one **damage type** and wears one **armor type**, and has a flat **armor**
number. All three are columns in `UNIT_STATS`, and the inspector shows them for a selected unit.
A hit does

```
damage = max(base × DAMAGE_MIN_FRACTION,  base × DAMAGE_VS_ARMOR[damageType][armorType] − armor)
```

so the multiplier is applied first, then the flat armor comes off, and a hit always does at least
10% of its base damage (`DAMAGE_MIN_FRACTION`). One function, `CombatDamage()` in `combat.c`, does
this for melee hits and arrows alike. Hits on buildings do the plain base damage.

The counter table in `config.h`, row = attacker's damage type, column = target's armor type:

| | Light | Medium | Heavy |
|---|---|---|---|
| **Pierce** | 1.25 | 1.0 | 0.5 |
| **Blunt** | 1.0 | 1.0 | 1.5 |
| **Magic** | 1.0 | 1.0 | 1.5 |

| Unit | Damage | Armor | Trained at | Role |
|---|---|---|---|---|
| Melee | 12 Blunt | 1 Medium | Barracks (M) | Cheap front line; good vs Knights |
| Archer | 9 Pierce | 0 Light | Archery Range (C) | Ranged; arrows bounce off Knights (2.5 per hit) |
| Knight | 18 Blunt | 2 Heavy | Barracks (N) | Slow, tough, expensive; shrugs off arrows, loses to blunt |
| Medic | none (heals 8 HP/s) | 0 Light | Academy (D) | Heals damaged allies; see [Buildings that need another, and healers](#buildings-that-need-another-and-healers) |
| Scout | 4 Pierce, ranged (100) | 0 Light | Archery Range (O) | Fastest unit (110), sees 11 tiles (the most), 35 HP, 60 gold: for spotting, not fighting |
| Mage | 30 Magic, splash | 0 Light | Academy (G) | Slow, fragile, 200 gold; long range (200), a bolt that splashes everyone near the landing spot; see [Splash and minimum range](#splash-and-minimum-range-mage) |
| Worker | 4 Blunt | 0 Light | Base (W) | Mines and builds |

Magic (the Mage) does 1.5× to Heavy armor: Knights are its favourite target.

**Adding an armor type** (e.g. Fortified):
1. Add `ARMOR_FORTIFIED` to the `ArmorType` enum in `config.h`, before `ARMOR_TYPE_COUNT`.
2. Add its name to `ARMOR_TYPE_NAMES` (shown in the inspector).
3. Add a column to **every** row of `DAMAGE_VS_ARMOR` (how much each damage type does to it).
4. Use it in a unit's `armorType` column.

**Adding a damage type** (e.g. Siege):
1. Add `DAMAGE_SIEGE` to the `DamageType` enum, before `DAMAGE_TYPE_COUNT`.
2. Add its name to `DAMAGE_TYPE_NAMES`.
3. Add a row `[DAMAGE_SIEGE] = { ... }` to `DAMAGE_VS_ARMOR`, one number per armor type.
4. Use it in a unit's `damageType` column.

A missing column or row isn't a compile error in C. The missing numbers are 0, so that damage
would always fall to the 10% minimum. Fill in every cell.

## Buildings that need another, and healers

**Prerequisites.** `BUILDING_STATS` has a `requires` column. The **Academy** (450 gold, the most
expensive building; hotkey E) requires a **Barracks**: workers can only start one while you own
at least one *finished* Barracks (`BuildingsCanBuild()` in `buildings.c`, used by the player and
the AI). Until then its Build button is greyed and says "Academy - Requires Barracks". A selected
Academy shows "needs: Barracks". Losing your last Barracks stops *new* Academies, but the ones you
have keep working. Map files and the editor can place anything.

**Healers.** `UNIT_STATS` has `canHeal`, `healRate` (HP per second) and `healRange` (pixels). The
**Medic** (Academy, 125 gold, hotkey D) has no attack. It heals 8 HP/s within 64 px:
- **Idle or attack-moving:** finds the nearest damaged ally within 160 px (`HEAL_SEARCH_RADIUS`
  in `heal.h`), walks into range and heals it every tick, never above max HP. When the ally dies,
  is full, gets out of reach or goes into fog, the Medic picks the next one in the same tick.
- **Plain move:** ignores healing (like soldiers ignore enemies). **Hold:** only heals allies
  already in range, without moving.
- **Several Medics** spread over several damaged allies. They only share one when there's no other.
- **Leash:** like soldiers, an idle Medic that walked off to heal goes back to where it stood.
- **Right click a damaged unit of yours** with Medics selected: they follow and heal it (any
  distance) until it's full. The rest of the selection gets the normal right-click order.
- **Attack orders:** a Medic can't attack. Ordered to attack, it attack-moves to the target
  instead (follows the army, heals on the way). The same goes for any unit with damage 0.
- A thin green line shows who's healing whom (only where you can see).

`UnitNeedsHealing()` in `heal.c` is the one rule for "damaged": alive and below max HP.

## Splash and minimum range (Mage)

Three more `UNIT_STATS` columns, 0 ("off") for every unit that doesn't use them:

| Column | Mage | Meaning |
|---|---|---|
| `splashRadius` | 48 | > 0: fires a slow magic bolt (220 px/s) instead of hitting directly |
| `splashFalloff` | 0.3 | damage at the edge of the splash, as a fraction of the centre's |
| `minRange` | 72 | won't fire at anything closer than this |

**The bolt** flies to the spot where the target stood **when it was fired**. A unit that walks away
in time takes nothing. When it lands, **every** unit and building within `splashRadius` is hit,
**your own included** (friendly fire is intended). Damage at distance `d` from the landing spot:

```
base = damage × (1 − (1 − splashFalloff) × d / splashRadius)      30 at the centre, 18.5 halfway, 9 at the edge
hit  = CombatDamage(base, Magic, target's armor type, target's armor)   (buildings: base)
```

So a Melee unit (Medium, armor 1) loses 29 / 18.5 / 8, and a Knight (Heavy, armor 2) loses 43 at
the centre. Units are found with the spatial grid. Buildings count by their nearest wall.

**Minimum range:** a Mage never fires at a target closer than 72 px. It switches to an enemy
further out if there is one, otherwise it backs off toward the middle of its range band
(72–200 px). The band is wide, so it can't flip between "too close" and "too far". A Mage on hold
just drops a target that's too close.

**The AI's Mages** also hold fire while one of their own units is inside the splash, and look for
a safer target. **Yours don't**: aiming is your call.

**Fog:** the damage happens either way. Only the ring is hidden where you can't see.

**Visuals:** a small violet bolt with a short trail and an expanding ring where it lands, drawn
with the arrows in one batch. **Projectile pool:** if all `MAX_PROJECTILES` (1024) slots are
flying, new shots (arrows and bolts) are skipped. The console says so once.

The inspector shows a selected Mage's splash radius, edge damage and minimum range.

## Flying units, what can hit what, and unit prerequisites

**Air Factory** (250 gold, hotkey F, needs a finished Barracks) trains the two flyers:

| | Falcon (L) | Airship (U) |
|---|---|---|
| Role | very fast scout and skirmisher | slow, tough bomber |
| HP / armor | 45, Light | 420, Heavy (3) |
| Attack | 6 Pierce, range 90, hits **air and ground** | 28 Blunt bombs, range 24, splash 44 px, hits **ground only** |
| Speed / cost | 140 px/s, 70 gold | 38 px/s, 450 gold |
| Needs | the Air Factory | the Air Factory **and a finished Academy** |
| Carries | nothing | **8 slots of ground units** (see [Transports](#transports-airship-cargo)) |

**Flying** (`moveClass` `MOVE_AIR`): flyers go in a straight line over water, rock, lava and
buildings, with no pathfinding. Nothing on the map blocks them, but they can't leave it. They
only push apart from other flyers, so ground units walk underneath them and never block them.
They're drawn above the ground units with a small shadow, in their own batched pass. Everything
else works as for any unit: selection, the minimap, fog sight, hold, attack-move, the leash,
Medic healing and rally points.

**What can hit what:** two `UNIT_STATS` columns, `hitsGround` (ground and naval units, and
buildings) and `hitsAir` (flyers). The rule is `UnitCanHitUnit()` in `units.h`:

| | hits ground | hits air |
|---|---|---|
| Archer, Scout, Mage, Falcon | yes | yes |
| Melee, Knight, Worker, Airship | yes | no |
| Medic (no attack) | no | no |

Every place that picks, keeps or damages a target uses it: auto-targeting and the grid search
(`GridFindNearestEnemy`), attack orders, the AI's orders, the target a unit already has, and splash
victims. So a Knight never targets, chases or hurts a Falcon, and an Airship's bombs pass through
flyers (friendly or not) but hit everything on the ground in the blast, friends included.
**Right-clicking a flyer** with units that can't hit it gives those units a plain move to it
(they walk underneath); the ones that can hit it attack.

**Unit prerequisites:** a `requires` column in `UNIT_STATS` (a building type, or
`BUILDING_NONE`). Besides the building that trains it, the team must own a **finished** one of
these. `UnitsCanTrain(team, type)` in `units.c` is the one check, used by `BuildingQueueTrain()`,
so the player and the AI follow the same rule. Until then the Train button is greyed and says
"Airship - Requires Academy"; clicking it or pressing its hotkey says "Requires Academy".

## Transports (Airship cargo)

Two `UNIT_STATS` columns: `cargoCapacity` (slots it carries; 0 = not a transport) and
`cargoSlots` (slots it takes inside one; 0 = can't be carried). The **Airship** carries 8 slots.
Worker, Melee, Archer, Scout, Medic and Mage take 1, the Knight takes 2, and flyers (and
transports) take 0. So 4 Knights fill an Airship, and 7 Workers leave no room for a Knight.

| You do | What happens |
|---|---|
| Right click your Airship (ground units selected) | They walk to it and get in when within `TRANSPORT_BOARD_DISTANCE` (28 px), while there are free slots. One that doesn't fit stops, and you see "Airship full" |
| **L** (Airship selected) | Your idle ground units within `TRANSPORT_LOAD_RADIUS_TILES` (5 tiles) board it, nearest first, as many as fit |
| **U** (Airship selected) | Unload everything below it |
| **Ctrl + right click** the ground (Airship selected; **Shift** in the web build, where Ctrl pauses) | It flies there and unloads |
| Click a unit's icon in the Airship's inspector | That unit gets out (if the Airship is over open ground) |

- **Getting out:** units only come out where a ground unit can stand. If the drop point is
  water, rock, lava or a building, the Airship first flies to the nearest open ground tile
  (searching up to `TRANSPORT_DROP_SEARCH_TILES`). They come out `TRANSPORT_UNLOAD_PER_TICK`
  (2) per tick, each on a free spot around the drop point, as ordinary idle units with the HP
  they had. Then the Airship is empty and free again.
- **Inside**, a unit keeps its pool slot and HP but is out of the world: nothing can target,
  splash, heal or select it, it gives no fog sight, it has no minimap dot, and it doesn't push
  others. `UnitIsActiveInWorld()` (`units.h`) is the one test for "takes part in the world";
  everything that used to ask "is this slot active?" for that purpose asks it instead. Unit
  counts (the F3 overlay, the pool limit) still count units inside transports.
- **Destroyed Airship:** over walkable ground its cargo drops around it with its current HP;
  over water, rock or lava the cargo is lost. One line goes to the log. A loaded unit whose
  transport is gone (checked every tick by serial) is handled the same way, so no unit can stay
  loaded in a transport that no longer exists.
- **Not saved in maps:** map files place Airships empty, and the editor's copy of a running game
  leaves units inside transports out. A new game starts with every transport empty.
- **The AI uses transports only to ferry** its army when it can't walk to you (see
  [Computer opponent](#computer-opponent-ai), 5.), and the Airship's bombs and targeting are unchanged.
- **Hotkeys:** L and U also train Falcons and Airships when an **Air Factory** is selected.
  That's no clash: a building is never selected together with units. The startup check
  (`InspectorCheckHotkeys`) knows when each key is active, and only warns about keys that could
  be pressed in the same situation.

**Making another transport** (a boat, a wagon, a bigger airship): give its `UNIT_STATS` row a
`cargoCapacity` above 0, and give each unit that may ride in it a `cargoSlots` above 0. The
boarding, cargo panel, L / U keys and unloading all follow from the table. A ground or naval
transport works the same way: cargo always gets out onto ground tiles.

## Guard Tower (buildings that attack)

`BUILDING_STATS` has six attack columns: `damage`, `damageType`, `range`, `cooldown`,
`hitsGround`, `hitsAir`. `damage` 0 means the building doesn't attack (every building except the
tower). The **Guard Tower** (200 gold, hotkey V, needs a finished Barracks; 1,000 HP) shoots
12 Pierce arrows every 0.7 s at enemies within 190 px of its centre, on the ground and in the
air. Pierce does half damage to Heavy armor, so Knights (and Airships) are its counter.

- **Targets** (`CombatBuildingTick()` in `combat.c`, called by `BuildingsTick()`): like a unit,
  it keeps shooting its target while that one is alive, visible, in range and not doomed;
  otherwise it asks the spatial grid for the nearest enemy unit in range that the team can see
  (fog), that it can hit (`hitsGround` / `hitsAir`) and that arrows already in flight won't kill
  (the same overkill rule as units). It shoots units, not buildings. Arrows come from the normal
  projectile pool.
- **Unfinished towers don't fire**, and a destroyed one stops (arrows already flying still land).
- **Selected:** a ring shows its range, and the inspector shows "Damage 12 Pierce, Range 190,
  Every 0.7 s, Hits ground and air", all from the table. The ring also shows while placing one.
- Measured: 10 towers beat 30 attacking Melee (about the same gold) with 5 towers left; 300
  Melee overrun them (killing 4 on the way).
- Make another tower by adding a `BUILDING_STATS` row with `damage` > 0 (for example a cheaper
  ground-only "Bolt Tower" with `hitsAir` false): no other code needed.

## Naval units (Dock, Boat, Ship)

| | Dock | Boat | Ship |
|---|---|---|---|
| Key | D (worker) | T (Dock) | P (Dock) |
| Cost, time | 250 gold, 30 s, needs a Barracks | 80 gold, 7 s | 400 gold, 18 s |
| HP, armor | 900 | 90, Light | 520, Heavy (3) |
| Attack | none | 7 Pierce, range 110, hits **ground, boats and flyers** | 32 Blunt, range 240, 40 px splash (**friends too**), **can't hit flyers** |
| Role | trains Boats and Ships | fast, fragile skirmisher | slow, long-range bombardment |

All of it comes from table rows in `config.h`: the Boat and Ship have `moveClass MOVE_NAVAL`
and `trainedAt BUILDING_DOCK`, so they path, move and spread out on water only, using the same
code as ground units (see [Tiles and movement classes](#tiles-and-movement-classes)). Moving
ones leave a short fading wake (`DrawWake` in `units.c`, drawn with the shapes so it adds no draw calls).
Art: `dock.png`, `boat.png`, `ship.png`.

**The `needsWater` rule:** a `BUILDING_STATS` column. A building with it must stand wholly on
open ground (never on water) with a water tile within `BUILDING_WATER_MARGIN` (1, `buildings.h`)
of its edge. One function, `BuildingsPlacementOK()`, decides this for the player's click, the
green/red ghost, the AI, the map loader and the editor, and says why not: "Dock must be next to
water". Give any new building `needsWater true` to get the same rule.

**Spawning:** a naval unit appears on the free water tile next to the Dock nearest its centre.
If every one is taken, training is refused *before* paying ("No free water next to the Dock"); a
unit that finishes while the water is full waits. A Dock's rally point starts on its water.

**The reach rule, `PathCanReach(moveClass, from, target, range)` (`path.c`):** can a unit of this
class get somewhere its target is within range? Flyers: always. Same region (`PathRegion`, a
flood fill of the tiles that class can cross): yes, one lookup. Otherwise it checks the tiles
around the target for one in the unit's own region that comes within range. Everything that
picks or keeps a target asks it (combat, `GridFindNearestEnemy`, `BuildingsFindNearestEnemy`,
healing, the AI), so:
- a Melee ignores a Boat out at sea; an Archer, Scout, Mage or Guard Tower on the shore shoots one in range;
- a target that becomes unreachable mid-chase is dropped; an attack order on one becomes a move to
  the nearest spot it can get to (the shore);
- a Medic doesn't walk toward a hurt boat, and an idle Boat ignores units inland.

Move orders stay in the unit's own region too: a Boat ordered onto land goes to the nearest water
of its own lake, a Melee ordered into a lake stops on the shore.

**Map files and the editor:** keywords `dock`, `boat`, `ship` (see [`maps/FORMAT.md`](maps/FORMAT.md)).
A Dock away from water or a Boat on land fails to load, with the line number and the reason.

**The AI** ignores water by default: no Dock, Boats or Ships. `AI_BUILDS_DOCKS` (`config.h`,
**off**) adds a Dock to its tech list (skipped when there's no shore near its base) and Boats to
its army mix (at most `AI_BOATS_MAX`, 6); it never trains Ships and has no naval tactics, and
this switch isn't part of the tested behaviour. See [Extending the AI](#extending-the-ai).

**Tests:** `tests/naval_test.c` (in `make test`) checks placement, spawning, sailing, targeting,
the Ship's splash, regions, map keywords and the editor on a map it builds itself.

## Adding a new unit type (walkthrough)

Example: a **Spearman**, a pierce-damage foot soldier with medium armor, trained at the Barracks.

**1. Add it to the enum** in `src/game/config.h`, before `UNIT_TYPE_COUNT`:

```c
typedef enum { UNIT_MELEE, UNIT_ARCHER, UNIT_WORKER, UNIT_KNIGHT, UNIT_MEDIC, UNIT_MAGE, UNIT_SCOUT, UNIT_FALCON, UNIT_AIRSHIP, UNIT_BOAT, UNIT_SHIP, UNIT_SPEARMAN, UNIT_TYPE_COUNT } UnitType;
```

**2. Give it a stats row** in `UNIT_STATS` (same file):

```c
//                   name        trainedAt          hotkey  hp      damage  damageType     range  cooldown  speed  armor  armorType     cost  trainTime  sight       canHeal  healRate  healRange  splash  falloff  minRange  hitsGround  hitsAir  requires       cargoCapacity  cargoSlots  moveClass    description
[UNIT_SPEARMAN] = { "Spearman", BUILDING_BARRACKS, KEY_J,  100.0f, 10.0f,  DAMAGE_PIERCE, 20.0f, 0.9f,     70.0f, 1.0f,  ARMOR_MEDIUM, 80,   6.0f,      UNIT_SIGHT, false,   0.0f,     0.0f,      0.0f,   0.0f,    0.0f,     true,       false,   BUILDING_NONE, 0,             1,          MOVE_GROUND, "A cheap pikeman who keeps charging Knights at bay." },
```

- `name` is used everywhere: inspector, editor button, map files, PNG file name.
- `trainedAt` puts a Train button on that building (`BUILDING_NONE` = can't be trained).
- `damageType`, `armor` and `armorType`: see [Damage and armor](#damage-and-armor).
- `canHeal`, `healRate`, `healRange`: `false, 0, 0` for a fighter (healers: see below).
- `hitsGround`, `hitsAir`: what it can attack; a pike can't reach flyers. `requires`: a building
  it needs besides the Barracks (`BUILDING_NONE` here). See [Flying units](#flying-units-what-can-hit-what-and-unit-prerequisites).
- `cargoCapacity`, `cargoSlots`: 0 and 1, so it isn't a transport but can ride in one (see [Transports](#transports-airship-cargo)).
- `moveClass`: `MOVE_GROUND` for a soldier (see [Tiles and movement classes](#tiles-and-movement-classes)).
- `description`: one sentence the inspector shows above its panel when a Spearman is selected,
  or when the mouse is over its Train button. Long text is word-wrapped and scrolls; `""` shows
  no box. Buildings have the same column.
- `splashRadius`, `splashFalloff`, `minRange`: `0, 0, 0` for a normal attack. Give a unit a
  splash radius and it fires bolts like the Mage, with no other code (see
  [Splash and minimum range](#splash-and-minimum-range-mage)).
- Every row must fill every column: C won't complain about a missing one (it becomes 0), but the
  compiler warns, and the numbers would silently be wrong.
- Pick an unused hotkey. If two actions share a key, the console says `HOTKEY CONFLICT` at startup.

**3. Add the art:** save a 32×32 PNG, facing right, as `assets/sprites/units/spearman.png`
(see [Art (sprites)](#art-sprites)). Without it the Spearman is a plain team-coloured circle; to
give that shape a mark of its own, add a case to `UnitsDrawIcon()` in `units.c`.

**4. Build and run** (`make run`). The console should say `SPRITES: 25 of 25 PNGs packed`
(one more than before). If it says `no units/spearman.png`, check the file name.

**5. Optional: put Spearmen in a map.** Add a line to a `.map` file (`<unit> <team> <x> <y>`, in tiles):

```
spearman 0 13 55
```

Or place one with the map editor, which now has a **Spearman** button.

**What you get without more code:** the Train button and hotkey on the Barracks, its queue icon,
the inspector's stats (damage, armor) and description box, the Controls page entry, the editor button, map-file
support, fog sight and the minimap dot. A new **building** works the same way: a row in
`BUILDING_STATS` gives it a Build button for workers, a hotkey, map-file and editor support.

**Variant: a healer.** A **Priest** is the same five steps with `damage 0`, `canHeal true` and its
own heal numbers. It then behaves exactly like the Medic (no code):

```c
[UNIT_PRIEST] = { "Priest", BUILDING_TEMPLE, KEY_I, 50.0f, 0.0f, DAMAGE_MAGIC, 0.0f, 0.0f, 65.0f, 0.0f, ARMOR_LIGHT, 150, 9.0f, UNIT_SIGHT, true, 12.0f, 80.0f, 0.0f, 0.0f, 0.0f, false, false, BUILDING_NONE, 0, 1, MOVE_GROUND, "Heals allies from further away than a Medic." },
```

**Variant: a building that needs another.** A **Temple** that trains the Priest and needs an
Academy is one enum entry (`BUILDING_TEMPLE`, before `BUILDING_TYPE_COUNT`) plus one row.
`requires` is the prerequisite:

```c
//                           name       hp       size  cost  buildTime  hotkey  dropOff  sight           requires          needsWater  damage  damageType     range  cooldown  hitsGround  hitsAir  description
[BUILDING_TEMPLE]        = { "Temple",  900.0f,  2,    300,  30.0f,     KEY_T,  false,   BUILDING_SIGHT, BUILDING_ACADEMY, false,      0.0f,   DAMAGE_PIERCE, 0.0f,  0.0f,     false,      false,   "Trains Priests, once you own a finished Academy." },
```

Workers get a "Temple - Requires Academy" button until an Academy is finished. The Temple gets
the Priest's Train button (`trainedAt BUILDING_TEMPLE`), a map keyword (`temple`), an editor
button and `temple.png` art, all from the row.

**What needs code:**
- Nothing for ranged units: any unit whose `range` is over 32 px (`ARROW_MIN_RANGE` in
  `combat.c`) shoots arrows (Archer, Scout), any unit with a `splashRadius` fires bolts, and the
  rest hit instantly at their `range`.
- `sight` (tiles it reveals in the fog) can be up to `FOG_MAX_SIGHT` (16, `config.h`).
- The AI trains the types in `AI_ARMY_MIX` at the buildings in `AI_TECH_ORDER` (both in
  `config.h`). It never builds a building or trains a unit it isn't told to, so new rows don't
  change it: add your unit to `AI_ARMY_MIX` to have the AI use it (see [Extending the AI](#extending-the-ai)).

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
| Minimap: left click / drag | Move the camera there |
| Minimap: right click | Move the selected units there |
| Mouse wheel | Zoom (over the inspector's buttons or the description box: scroll them) |
| Left click / drag | Select unit / box select |
| Left click own building | Select it: the inspector shows HP, queue and Train buttons |
| Left click gold node | Inspect gold left |
| Shift + select | Add to selection |
| Right click | Move selected units |
| Right click ground (building selected) | Set its rally point (blue flag): newly trained units walk there |
| Right click on enemy unit or building | Attack it (selected units that can't hit it, e.g. Melee on a Falcon, just move there) |
| Right click on gold (workers selected) | Mine it: workers carry gold to the nearest base and repeat |
| Right click your unfinished building (workers selected) | Workers help build it |
| Right click your damaged unit (Medics selected) | Medics follow and heal it until it's full; the rest of the selection moves there |
| A, then right click | Attack-move: walk there, fighting any enemies met on the way (left click or Esc cancels) |
| S | Stop: drop all orders (units still fight enemies that come close) |
| H | Hold position: stay put, only attack enemies already in range |
| W (Base selected) | Train a Worker (50); queue up to 5 |
| M / N (Barracks selected) | Train Melee (75) / Knight (175); queue up to 5 |
| C / O (Archery Range selected) | Train an Archer (100) / Scout (60); queue up to 5 |
| D / G (Academy selected) | Train a Medic (125) / Mage (200); queue up to 5 |
| L / U (Air Factory selected) | Train a Falcon (70) / Airship (450, needs a finished Academy); queue up to 5 |
| T / P (Dock selected) | Train a Boat (80) / Ship (400); queue up to 5. Refused, unpaid, when no water next to the Dock is free |
| L / U (Airship selected) | Nearby idle ground units board it / unload everything below it |
| Right click your Airship (ground units selected) | They walk to it and get in (8 slots; a Knight takes 2) |
| Ctrl + right click ground (web: Shift), Airship selected | It flies there and unloads (on the nearest open ground) |
| Click a queue icon (building selected) | Cancel that unit, gold refunded (destroying the building loses its queue) |
| B / K / R / E / F / V / D (workers selected) | Build a Base (400) / Barracks (150) / Archery Range (175) / Academy (450, needs a finished Barracks) / Air Factory (250, needs a finished Barracks) / Guard Tower (200, needs a finished Barracks) / Dock (250, needs a finished Barracks, must touch water): a ghost follows the mouse, green = OK, red = blocked; left click places, right click / Esc / the key again cancels |
| Esc (web: Ctrl) | Cancel a pending attack-move or building placement; otherwise open the pause menu (Resume, Fog of war on/off, Controls, Main Menu, Exit). The web build uses Left Ctrl because browsers use Esc to leave fullscreen; change it with `KEY_PAUSE` in `config.h` |
| F1 | Debug: spawn a wave of 20 enemies |
| F4 | Debug: spawn 500 units for each side around its base (a mix of Melee, Archers, Knights, Mages, Medics, Scouts and Falcons), on free spots; press again for more, up to the unit pool (`MAX_UNITS`, 16,384). For testing big battles: `DEBUG_ARMY_SIZE` and `DEBUG_ARMY_MIX` in `config.h` |
| F2 | Map editor on the current map (F2 / Exit returns to the paused game) |
| F3 | Show / hide the debug overlay (any screen) |
| Ctrl+Z (editor) | Undo tile painting |

**Idle units defend themselves on a leash:** an idle unit attacks enemies that come close, but
chases at most `COMBAT_LEASH_TILES` (6) tiles from where it was standing, then walks back (also
after the fight ends). Your attack and attack-move orders aren't leashed; hold position never chases.

Esc never quits the game directly; use Exit in a menu or close the window. (The web build has no Exit buttons.)

## Code layout

`maps/` holds the map files, `assets/sprites/` the art and `assets/fonts/` the font. Automated
tests are in `tests/` (`make test`). Source is in `src/`: `main.c` (game states) at the top,
`src/game/` for the game and the systems the editor reuses (ui, map, map files, tables),
and `src/editor/` for the editor.


| File | System |
|---|---|
| `main.c` | Window, game states (menu / playing / paused / victory / defeat / editor), fixed 30 Hz sim loop, new game (map file or Random), win/lose check, editor ↔ game hand-over, performance log |
| `game/overlay.c` | F3 debug overlay: FPS (now, min / avg), frame / tick / fog / path times, counts per team, AI state; one FPS line outside the game |
| `game/config.h` | Shared settings: tick rate, teams, unit and building stats tables, game states, key bindings, controls list |
| `game/map.c` | Tile map: `TILE_INFO`, generated "Random" map, real size of the loaded map, walkability per movement class (`MapTileWalkable`: terrain + building-blocked tiles), culled drawing |
| `game/mapfile.c` | Map files: `MapDoc` (tiles + objects), parse + full validation with file:line errors, load into the game, write, scan the folder |
| `editor/editor.c` | Map editor: tile brushes with undo, object tools, unit brush (1×1 / 3×3 / 5×5), save / load / test play |
| `editor/web_download.js` | Web build only: the editor's Save hands the file to the browser as a download |
| `web/shell.html` | Web build only: the page around the game (canvas scaling, loading bar, no right-click menu, game keys kept from the browser) |
| `game/sprites.c` | Optional PNG art: scans `assets/sprites`, packs it into one atlas texture (shelf packer), draws units / buildings / tiles from it |
| `game/camera.c` | Pan / zoom, visible-area queries; the editor's zoom-to-fit (`CamUpdateEditor()`) |
| `game/transport.c` | Transports: boarding, cargo (`cargoCapacity` / `cargoSlots`), L / U, unloading onto free ground, cargo of a destroyed transport |
| `game/units.c` | Unit pool, movement, separation (flyers only with flyers), drawing (flyers above, with shadows), orders, `UnitsCanTrain()` |
| `game/grid.c` | Spatial grid for nearby-unit queries (nearest enemy: ground ones, flyers, or both; it walks only the other team's units, kept in a list per team in each cell) |
| `game/path.c` | A* pathfinding per movement class (air: straight, no search): request queue, per-frame time budget, path smoothing; regions per class ("can I get there?") |
| `game/input.c` | Selection list (units, building, gold node), orders, hotkeys, building placement ghost |
| `game/minimap.c` | Minimap: cached terrain/fog texture, unit dots, camera outline, click to move camera / units |
| `game/fog.c` | Fog of war: per-team visibility grid, recomputed 5× a second, one batched overlay pass |
| `game/heal.c` | Healers (`canHeal`): find the nearest damaged ally (grid), walk into range, heal per tick, spread over patients, follow-and-heal order, green heal lines |
| `game/combat.c` | Attacking, chasing, auto-targeting (aggro), what can hit what (`hitsGround` / `hitsAir`), towers (`CombatBuildingTick`), projectile pool (arrows, magic bolts and bombs with splash, splash rings), minimum range, the damage formula (`CombatDamage()`: damage type × armor type, minus armor) |
| `game/ai.c` | Enemy AI: trains workers to a per-node target, builds a Barracks and tech buildings, expands to new gold, trains its army (and air defence against player flyers), sends idle units at player units they can hit |
| `game/ai_ferry.c` | Enemy AI, ferrying: needs transport? builds the Air Factory and Airships, gathers a group, picks a safe drop point, flies, unloads, turns back when in danger (`ai_internal.h` is what it shares with `ai.c`) |
| `game/economy.c` | Gold per team, gold node pool, worker mining loop, gold HUD (top right) |
| `game/buildings.c` | Building pool, tile blocking, placement checks, prerequisites (`BuildingsCanBuild()`), production queue (cancel/refund), rally points, gold drop-off lookup, construction by workers, drawing |
| `game/ui.c` | Tiny immediate-mode UI (buttons, panels, labels, tabs, scroll areas, word-wrapped text boxes), scales with window height, blocks clicks from reaching the game; the UI font: all text is drawn and measured here |
| `game/menu.c` | Main menu, map picker, pause menu, Controls page (measured: shrinks / wraps to its columns, scrolls), Victory / Defeat screen |
| `game/inspector.c` | Bottom panel for the selection; Train / Build buttons generated from the stats tables; description box; a tower's attack (and its range ring, drawn by `input.c`); hotkey clash check |
| `tests/tile_class_test.c` | Automated test: every tile × movement class, building blocking, the map edge, straight lines, regions, air paths, map characters |
| `tests/controls_overflow_test.c` | Automated test: draws the Controls page at 6 window sizes (and with very long names) and fails if any text leaves its column (see the file) |

## Performance

**In short, on the Celeron:** 60 FPS holds up to about **2,000 units in a big fight**; normal
games (a few hundred units) run at 60 with lots of headroom; the game itself uses about **30 MB**
of memory.

Measured for version 1.0.0 on the target hardware: **Intel Celeron N4120** (4 cores, 1.1 GHz
base), **4 GB RAM** (3.6 GB usable), integrated graphics, Arch Linux. Release build, 1280×720.
Scenario: River Crossing, 100 workers mining plus a 300 vs 300 battle (Melee, Archers, Knights
and 30 Scouts; 732 units in all).

| | Desktop (60 FPS cap) | Desktop (uncapped) | Web (Firefox) |
|---|---|---|---|
| FPS, 100 workers + 300 vs 300 | 60 (min 60) | 390 avg (min 355) | 59 (min 59) |
| Same, with the Inter font (camera zoomed out over the base and the battle, Base selected) | 60 (min 59.6) | 351–406 avg (3 runs; 1.0.0 in the same runs: 381–433) | |
| Same, plus 5 Airships per side carrying 80 units in all | 60 (min 59.7) | 351–355 avg (3 runs; the plain battle in the same runs: 356–404) | |
| Same, plus 10 Guard Towers per side | 60 (min 59.9) | 335–352 avg (3 runs; without towers in the same runs: 348–360) | |
| Same, each side's 300 including 15 Falcons and 5 Airships | 60 (min 59.5) | 331–356 avg (3 runs; all-ground in the same runs: 315–412) | |
| Map editor, whole 128×128 map on screen | | 124 avg | |
| Sim tick (30 per second), battle | 1.1–1.7 ms avg, 4.8 ms worst | | |
| Fog of war update (5 per second) | 0.14–0.23 ms avg, 0.38 ms worst (battle) | | |
| **v1.1, unit pool raised to 16,384:** 100 workers + 300 vs 300 (scripted again, ~708 units; runs interleaved with the previous commit) | 60 (min 59.8) in 3 of 4 runs; one run dipped to 45 for 5 s with the sim tick under 1.5 ms, and it didn't happen again (previous commit: 60, min 59.8, 4 of 4) | 356–631 avg (previous commit, 2,048 pool, in the same runs: 363–591) | |
| Memory (v1.1) | about 30 MB (29–32 MB private memory at every unit count up to 16,000). Total RSS reads 91–168 MB because it also counts the graphics driver's shared libraries, which any OpenGL program maps | | wasm heap 29 MB, sized at startup to fit the unit pool; it never grew, even at 16,000 units |
| Startup: packing the art atlas | 2–4 ms | | 10–40 ms |
| Startup: loading the font (two sizes) | 6 ms | | 13–15 ms |
| Startup: first frame | ~190 ms from `main()` (1.0.0: the same) | | ~360 ms after the page opens, served locally (1.0.0: ~340 ms). v1.1, measured differently (from navigation start, headless Firefox, fresh profile): 1.0–1.1 s, the previous commit 1.07–1.22 s the same way; ~110 ms of it is the game's own start-up |
| Download size | | | 448 KB zip (v1.1; the previous commit: 447 KB, so the bigger unit pool adds nothing; 1.0.0: 207 KB) |

### Big battles (F4): how many units it can take

Press **F4** while playing to add 500 units to each side (see [Controls](#controls)). Measured
on the same Celeron with v1.1: River Crossing, F4 armies around both bases, each side
attack-moving to the other's Base: ~20 s of marching, then the fight at the river and its bridges.
Camera over the middle bridge, 1280×720. Numbers are 5-second averages over 45 s, the ranges
cover the capped and the uncapped run while the armies are fighting; "worst" is the slowest
single tick. The web runs were measured before the projectile-pool fix (it matters only in small
games).

| Units | Desktop, 60 FPS cap | Desktop, uncapped | Web (headless Firefox, caps at 60) | Sim tick avg (worst) | Fog update | Drawing the world (units on screen) | Minimap |
|---|---|---|---|---|---|---|---|
| 2,000 | **60** (min 59.7) | 390–534 | 42–60 | 2.8–6.7 ms (16.6) | 0.1–0.5 ms | 0.3–2.5 ms | 0.1–0.7 ms |
| 4,000 | 59 (58.6–59.5) | 178–537 | 36–59 | 5.8–12.5 ms (28.5) | 0.1–0.7 ms | 0.2–3.2 ms | 0.2–0.9 ms |
| 8,000 | 56–57 | 71–455 | 17–41 | 10–23 ms (62) | 0.1–0.9 ms | 0.2–4.6 ms | 0.4–1.2 ms |
| 16,000 | 35–45 | 34–80 | 4–17 | 19.5–22.6 ms (46) | 1.1–1.5 ms | 0.5–6.2 ms | 1.9–2.6 ms |

**60 FPS holds up to about 2,000 units in a big fight** (min 59.7 capped). 4,000 drops a frame
here and there (58.6–59.5) when the armies clash, because single ticks then take 20–28 ms.
On the web, 60 holds only once fights are small; 2,000 fighting runs at 42–60. 16,000 runs, but
as a stress test, not something to play: the sim takes ~21 ms of every 33 ms tick.

**The three biggest costs at 16,000 units**, and what's done about them:
1. **Separation** (units pushing apart, `SeparationPush` in `units.c`): about half of the sim tick.
   Every unit checks every unit in the grid cells around it, every tick; in a dense crowd that's
   ~35 neighbours each. Not changed: any cheaper version visits neighbours in a different order,
   which changes the result in the last bits and with it how every battle plays out.
2. **Pathfinding throughput:** a 16,000-unit order across the river queues ~13,600 path requests,
   and the per-frame budget (`PATH_BUDGET_MS`, 1 ms) clears ~75 a second, so most units wait
   minutes before they start walking. Frame rate is protected (that's what the budget is for);
   responsiveness isn't. The real fix is one shared path (or a flow field) per group: a new system,
   not done here. Raising `PATH_BUDGET_MS` trades frame rate for faster starts.
3. **Drawing:** up to ~6 ms a frame with thousands of units on screen, plus ~2 ms for the minimap
   (one dot per unit, every frame).

**Fixed** (each one changes no result: the AI vs AI baselines and a 4,000-unit battle replay
bit-for-bit identically): the nearest-enemy search now skips the searcher's own team (it was
58% of the tick: 16,000 units went from 3 FPS to 34–80), two hot flags sit next to each other in
memory, and loops over the unit and projectile pools stop at the highest slot in use, so the big
pool costs a normal game almost nothing.

Limits are fixed pools, set in headers: 16,384 units (`MAX_UNITS`, `units.h`; about 0.7 KB of
memory per slot, used or not), 64 buildings (`MAX_BUILDINGS`), 64 gold nodes, 8,192 projectiles
(`MAX_PROJECTILES` = `MAX_UNITS`/2), maps up to 128×128 tiles. Other apps running on the same
machine lower the uncapped numbers a lot. Press F3 to see the live numbers.

## Known limitations

- **No multiplayer** (no network or hot-seat play) and **no saving or loading a game** in progress.
- **One computer opponent** (team 1), in 1 vs 1 games only.
- **The AI has one fixed plan** (no difficulty levels, no reaction to what you build): a fixed
  army mix and build order. By default it sees through the fog (`AI_SEES_THROUGH_FOG`).
- **Balance is rough.** In equal-gold fights Melee beats every other type, and in AI vs AI games
  one side of the map won 4 of 5. On the **Random** map the player starts with 20 soldiers
  and the AI with none, which makes it easy. See [Balance (measured)](#balance-measured) below.
- **Destroyed buildings you can't see simply disappear.** Remembered enemy buildings stay on the
  map until you look again, but one destroyed out of sight vanishes instead of leaving a "ghost".
  Units under fog aren't remembered at all.
- **No sound or music.** **Placeholder art only** (simple shapes).
- **No control groups** (Ctrl+1–9), no shift-queued orders, no formations beyond a grid of spots.
- **Linux and web only.** The code is plain C99 + raylib and should port, but Windows and macOS
  builds aren't tested or set up. Only Firefox was tested for the web build.
- **The web build draws at 1280×720** and scales that to the browser window (slightly soft on big
  screens). The editor's **Load** is off in the browser (Save downloads the file).
- Maps and art are read at startup; changing them needs a restart (the web build needs a rebuild).
- **Text is Latin-1 only** (English and western European accents), see [Font](#font).
- **Every unit is the same size** (`UNIT_RADIUS`), so the Airship is drawn as small as a Falcon.
- **The AI's only fighting flyers are Falcons** (at most 4). It uses Airships only to ferry, never
  for bombing runs, and ignores water: no Docks, Boats or Ships. See [Extending the AI](#extending-the-ai).
- **Big crowds: three good places to improve** (measured costs in [Big battles](#big-battles-f4-how-many-units-it-can-take)):
  - **Separation** (units pushing apart) is about half the sim tick at 16,000 units: each unit
    checks every unit in the grid cells around it, every tick. Look at `SeparationPush()` in
    `units.c` and the cell size in `grid.h`. Any change alters battle results (so the AI vs AI
    baselines in `tests/baselines/` change too).
  - **Path-queue throughput:** an order for thousands of units queues thousands of searches, and
    the per-frame budget (`PATH_BUDGET_MS`, `path.h`; the queue is worked in `PathUpdate()`,
    `path.c`) clears ~75 a second, so units start walking minutes late. Shared paths or a flow
    field per group would fix it.
  - **Minimap drawing:** one dot per unit, every frame (~2 ms at 16,000 units). Look at
    `DrawDots()` in `minimap.c`; drawing dots less often, or into a texture, would cut it.
- **The AI's ferrying is simple.** It lands near the closest player building rather than
  picking a weak spot, and it lands even under fire once it's close (`AI_FERRY_COMMIT_TILES`);
  `AI_FERRY_DANGER_TILES` above 0 makes it more careful but on Islands it then turned back on most
  trips and never finished the game. It ferries only when *nothing* of the player's can be
  reached on foot (a map with one island cut off is played as a normal ground game), and it
  doesn't shoot down or chase the player's Airships on purpose.

### Balance (measured)

Equal-gold fights (1,500 gold each, two groups walking into each other):

| | Result |
|---|---|
| Melee vs Archer / Knight / Mage / Scout | Melee wins every time (keeps 95% / 85% / 45% / 100% of its value) |
| Knight vs Archer | Knight wins, loses nothing |
| Mage vs Archer | Mage wins, keeps 86% |
| Knight vs Mage | Knight wins, keeps 62% |

AI vs AI (the same AI on both sides, fog off, starting soldiers removed) ends in 4–10 minutes,
and both sides train all seven ground unit types on every map with an economy. Which side wins depends
on the map: side 0 won 4 of 5. Against a player who does nothing, the AI wins in 1–4 minutes. Change the numbers in
`UNIT_STATS` (`config.h`); every number above can be re-measured after a change.

## Automatic builds

`.github/workflows/build.yml` makes GitHub build the kit every time you push (the **Actions**
tab of the repository; the builds are attached to each run):

| Job | What it checks |
|---|---|
| Linux (`ubuntu:24.04`, `ubuntu:22.04`) | a clean Ubuntu with only the README's packages: `make desktop`, then the game runs 10 s on a virtual screen |
| Web | `make web-zip` with the latest Emscripten |
| Windows (Visual Studio), Windows (MinGW), macOS | **not supported yet**: these show what would need porting |

## Releasing

`make release` deletes the build folders, builds the desktop and web versions from scratch, and
puts two zips in `dist/`:

- `rts-kit-<version>-web.zip`: `index.html`, `index.js`, `index.wasm`, `index.data` at the top
  level, ready to upload to itch.io (HTML project, "This file will be played in the browser",
  viewport 1280 × 720).
- `rts-kit-<version>-source.zip`: the source, `README.md`, `LICENSE`, `THIRD_PARTY.md`,
  `PLAYTEST.md`, `assets/`, `maps/` and `tests/`, in one `rts-kit-<version>/` folder.

For 1.0.0: web zip 207 KB, source zip 149 KB; about 1.5 minutes on the Celeron (the web build
compiles raylib from scratch). With the Inter font the web zip is 428 KB.

The version comes from `GAME_VERSION` in `config.h`. Go through [PLAYTEST.md](PLAYTEST.md) first.
