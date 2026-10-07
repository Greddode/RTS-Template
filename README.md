# RTS Kit

A small, complete real-time strategy game in plain **C99 + [raylib](https://www.raylib.com/)**,
made to be read, changed and extended. It runs on **Linux desktop and in the browser**
(WebAssembly), and it's built to run smoothly on a 4 GB Celeron laptop.

**What you get:** workers that mine gold and construct buildings, 7 unit types (Melee, Archer,
Knight, Worker, Medic, Mage, Scout) with armor and damage types, splash damage and healers,
4 production buildings with prerequisites, a computer opponent that builds, expands and attacks,
fog of war, a minimap, A* pathfinding with a per-frame time budget, a map editor with
test play, swappable PNG art (placeholders included), a debug overlay (F3), and a web build
ready to upload to itch.io. Units, buildings, tiles and damage types are rows in tables in
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
make clean     # delete build folders
```

`make desktop` / `make web` build without running. The real build config is in `CMakeLists.txt`;
the Makefile just holds shortcuts. The first web build compiles raylib, so it takes a minute or two.
In a game, **Esc** opens the pause menu and the **Controls** page; F3 shows the debug overlay.

**Where to change things:**

| I want to... | Section |
|---|---|
| add a unit | [Adding a new unit type](#adding-a-new-unit-type-walkthrough) |
| add a building (or a prerequisite) | [the Temple example](#adding-a-new-unit-type-walkthrough) in the same section |
| make a map | [Maps](#maps), [Map editor](#map-editor) |
| replace the art | [Art (sprites)](#art-sprites) |
| add an armor or damage type | [Damage and armor](#damage-and-armor) |
| tune the AI | [Computer opponent (AI)](#computer-opponent-ai): every number is a named constant in `config.h` |
| change keys | `KEY_...` defines and the `CONTROLS` table in `config.h` |

## Maps

**Play** opens a map picker: **Random** (the generated map) plus every `.map` file in `maps/`.
The list is built by scanning the folder, so a new map needs no code change. On desktop the
build copies `maps/` next to the game (`make run` re-copies it each time); the web build bundles
it into the page.

Included: `dire_straight_64x64.map` ("Dire Straight", 64×64) and `river_crossing_128x128.map`
("River Crossing", 128×128), both made with the map editor. **The full file format, the rules a
map must follow and tips for fair maps are in [`maps/FORMAT.md`](maps/FORMAT.md).**

### Map file format

Plain text, one character per tile, so it's easy to edit and to diff. In short (the full guide
is [`maps/FORMAT.md`](maps/FORMAT.md)):

```
# comment (outside the tile grid)
name Duel (64x64)
width 64                  # 8..128
height 64                 # 8..128
tiles                     # then exactly <height> rows of <width> characters
..,,~~~##...              # . grass   , dirt   ~ water   # rock (read as-is: not a comment here)
...
base 0 8 52               # <building> <team> <x> <y>   x,y = top-left tile; team 0 player, 1 AI
worker 0 12 51            # <unit> <team> <x> <y>       worker, melee, archer, knight, ...
archery_range 0 20 50     # a space in a name is written _
gold 5 46 1500            # gold <x> <y> <amount>   (gold has no team: anyone can mine it)
```

Building and unit keywords are the names in `BUILDING_STATS` / `UNIT_STATS` (any case, spaces
written as `_`: "Archery Range" → `archery_range`), so new types work in map files automatically. Each team needs at least one building. Mistakes (unknown character,
wrong row length, object on water or outside the map, overlapping buildings, ...) are shown on
screen as `file:line: what's wrong`, and the game plays the Random map instead.

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
  since F1–F3 are game keys. F5 (reload), F11 (fullscreen) and F12 (developer tools) still work.

**Publishing on itch.io:** `make web-zip` makes `build-web/rts-kit-web.zip` with `index.html`,
`index.js`, `index.wasm` and `index.data` at the top level. On itch.io: *Kind of project* →
**HTML**, upload the zip, tick **"This file will be played in the browser"**, and set the
viewport to **1280 × 720** (or tick *Click to launch in fullscreen*: the page scales to any size).

## Map editor

Open it from the main menu (**Map Editor**, starts a blank 64×64 map) or press **F2** while
playing (opens the current map; **Exit** or F2 returns to the paused game exactly as it was).

| Tool | What it does |
|---|---|
| Tile brushes (Grass, Dirt, Water, Rock) | Left-click / drag to paint; brush size 1, 3 or 5. Water and rock never paint under an object. **Ctrl+Z** undoes painting (32 steps) |
| Player / AI | Which team new objects belong to |
| Base, Barracks, Archery Range, Academy, Melee, Archer, Worker, Knight, Medic, Mage, Scout | Click to place; a green/red ghost shows if it fits (same rules as map files) |
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

## Computer opponent (AI)

Every 2 seconds the AI:

1. **Barracks:** once it has 3 workers and 150 gold, one worker builds a Barracks near its base.
   If gold piles up past 600 while every Barracks has a full queue, it builds another (up to 3).
   **Tech buildings:** once a Barracks is finished, it builds one of each building in
   `AI_TECH_ORDER` (Archery Range, then Academy), each after the one before is finished, rebuilt
   if destroyed. It pauses army training while it saves up for the next one, and uses any it was
   given by the map file.
2. **Workers:** each base aims for **8 workers per reachable gold node** near it (at most **16**),
   training at the base that needs them most. When every base is saturated it stops, and the
   gold goes into the army.
3. **Expansion:** looks for a gold node that is far from its bases (15+ tiles), still rich
   (800+ gold), **reachable**, and not near a player building (20 tiles). With a base's cost plus
   a reserve it sends **one** worker to build a new Base there. It saves up for one (pausing army
   training) once its workers are saturated or its own gold is running low. One expansion at a
   time, at most 3 bases. If the builder dies, the site is cancelled (refunded) and that node
   isn't tried again.
4. **Army:** idle workers go to the near node with the fewest workers; combat units attack the
   nearest player unit or building (Medics follow along and heal; Mages hold fire while their
   own units are in the splash). Every 5 seconds it queues units by the **army mix**,
   `AI_ARMY_MIX` in `config.h`:

   | Type | Share | Cap (alive) |
   |---|---|---|
   | Melee | 4 | |
   | Knight | 2 | |
   | Archer | 3 | |
   | Mage | 1 | |
   | Scout | 1 | 2 |
   | Medic | 1 | 4 |

   Each time it picks the type furthest below its share (counting units alive and queued), at a
   building that trains it and has room in its queue (2 per building), until the queues are full.
   If it can't afford that type yet, it stops and saves for it instead of buying something
   cheaper. Change the shares to change its style. A new unit type is used once it has a row here.

Every number (thresholds, distances, caps, timings) is a named constant in the **AI tuning**
block of `config.h`. The debug overlay (**F3**, top left) shows the AI's gold, workers
(have/target), bases, and what it's currently trying to do.

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
assets/sprites/units/      melee.png  archer.png  worker.png  knight.png  medic.png  mage.png  scout.png   (names from UNIT_STATS)
assets/sprites/buildings/  base.png   barracks.png  archery_range.png  academy.png  (names from BUILDING_STATS)
assets/sprites/tiles/      grass.png  dirt.png  water.png  rock.png  (names from TILE_INFO)
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

## Adding a new unit type (walkthrough)

Example: a **Spearman**, a pierce-damage foot soldier with medium armor, trained at the Barracks.

**1. Add it to the enum** in `src/game/config.h`, before `UNIT_TYPE_COUNT`:

```c
typedef enum { UNIT_MELEE, UNIT_ARCHER, UNIT_WORKER, UNIT_KNIGHT, UNIT_MEDIC, UNIT_MAGE, UNIT_SCOUT, UNIT_SPEARMAN, UNIT_TYPE_COUNT } UnitType;
```

**2. Give it a stats row** in `UNIT_STATS` (same file):

```c
//                   name        trainedAt          hotkey  hp      damage  damageType     range  cooldown  speed  armor  armorType     cost  trainTime  sight       canHeal  healRate  healRange  splash  falloff  minRange
[UNIT_SPEARMAN] = { "Spearman", BUILDING_BARRACKS, KEY_P,  100.0f, 10.0f,  DAMAGE_PIERCE, 20.0f, 0.9f,     70.0f, 1.0f,  ARMOR_MEDIUM, 80,   6.0f,      UNIT_SIGHT, false,   0.0f,     0.0f,      0.0f,   0.0f,    0.0f },
```

- `name` is used everywhere: inspector, editor button, map files, PNG file name.
- `trainedAt` puts a Train button on that building (`BUILDING_NONE` = can't be trained).
- `damageType`, `armor` and `armorType`: see [Damage and armor](#damage-and-armor).
- `canHeal`, `healRate`, `healRange`: `false, 0, 0` for a fighter (healers: see below).
- `splashRadius`, `splashFalloff`, `minRange`: `0, 0, 0` for a normal attack. Give a unit a
  splash radius and it fires bolts like the Mage, with no other code (see
  [Splash and minimum range](#splash-and-minimum-range-mage)).
- Every row must fill every column: C won't complain about a missing one (it becomes 0), but the
  compiler warns, and the numbers would silently be wrong.
- Pick an unused hotkey. If two actions share a key, the console says `HOTKEY CONFLICT` at startup.

**3. Add the art:** save a 32×32 PNG, facing right, as `assets/sprites/units/spearman.png`
(see [Art (sprites)](#art-sprites)). Without it the Spearman is a plain team-coloured circle; to
give that shape a mark of its own, add a case to `UnitsDrawIcon()` in `units.c`.

**4. Build and run** (`make run`). The console should say `SPRITES: 16 of 16 PNGs packed`. If
it says `no units/spearman.png`, check the file name.

**5. Optional: put Spearmen in a map.** Add a line to a `.map` file (`<unit> <team> <x> <y>`, in tiles):

```
spearman 0 13 55
```

Or place one with the map editor, which now has a **Spearman** button.

**What you get without more code:** the Train button and hotkey on the Barracks, its queue icon,
the inspector's stats (damage, armor), the Controls page entry, the editor button, map-file
support, fog sight and the minimap dot. A new **building** works the same way: a row in
`BUILDING_STATS` gives it a Build button for workers, a hotkey, map-file and editor support.

**Variant: a healer.** A **Priest** is the same five steps with `damage 0`, `canHeal true` and its
own heal numbers. It then behaves exactly like the Medic (no code):

```c
[UNIT_PRIEST] = { "Priest", BUILDING_TEMPLE, KEY_I, 50.0f, 0.0f, DAMAGE_MAGIC, 0.0f, 0.0f, 65.0f, 0.0f, ARMOR_LIGHT, 150, 9.0f, UNIT_SIGHT, true, 12.0f, 80.0f, 0.0f, 0.0f, 0.0f },
```

**Variant: a building that needs another.** A **Temple** that trains the Priest and needs an
Academy is one enum entry (`BUILDING_TEMPLE`, before `BUILDING_TYPE_COUNT`) plus one row. The last
column is the prerequisite:

```c
//                           name       hp       size  cost  buildTime  hotkey  dropOff  sight           requires
[BUILDING_TEMPLE]        = { "Temple",  900.0f,  2,    300,  30.0f,     KEY_T,  false,   BUILDING_SIGHT, BUILDING_ACADEMY },
```

Workers get a "Temple - Requires Academy" button until an Academy is finished. The Temple gets
the Priest's Train button (`trainedAt BUILDING_TEMPLE`), a map keyword (`temple`), an editor
button and `temple.png` art, all from the row.

**What needs code:**
- Nothing for ranged units: any unit whose `range` is over 32 px (`ARROW_MIN_RANGE` in
  `combat.c`) shoots arrows (Archer, Scout), any unit with a `splashRadius` fires bolts, and the
  rest hit instantly at their `range`.
- `sight` (tiles it reveals in the fog) can be up to `FOG_MAX_SIGHT` (16, `config.h`).
- The AI trains Melee at its Barracks and Archers at its Archery Range (`TrainTick()` in `ai.c`).
  It never builds a building or trains a unit it isn't told to, so new rows don't change it.

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
| Right click your damaged unit (Medics selected) | Medics follow and heal it until it's full; the rest of the selection moves there |
| A, then right click | Attack-move: walk there, fighting any enemies met on the way (left click or Esc cancels) |
| S | Stop: drop all orders (units still fight enemies that come close) |
| H | Hold position: stay put, only attack enemies already in range |
| W (Base selected) | Train a Worker (50); queue up to 5 |
| M / N (Barracks selected) | Train Melee (75) / Knight (175); queue up to 5 |
| C / O (Archery Range selected) | Train an Archer (100) / Scout (60); queue up to 5 |
| D / G (Academy selected) | Train a Medic (125) / Mage (200); queue up to 5 |
| Click a queue icon (building selected) | Cancel that unit, gold refunded (destroying the building loses its queue) |
| B / K / R / E (workers selected) | Build a Base (400) / Barracks (150) / Archery Range (175) / Academy (450, needs a finished Barracks): a ghost follows the mouse, green = OK, red = blocked; left click places, right click / Esc / the key again cancels |
| Esc | Cancel a pending attack-move or building placement; otherwise open the pause menu (Resume, Fog of war on/off, Controls, Main Menu, Exit) |
| F1 | Debug: spawn a wave of 20 enemies |
| F2 | Map editor on the current map (F2 / Exit returns to the paused game) |
| F3 | Show / hide the debug overlay (any screen) |
| Ctrl+Z (editor) | Undo tile painting |

**Idle units defend themselves on a leash:** an idle unit attacks enemies that come close, but
chases at most `COMBAT_LEASH_TILES` (6) tiles from where it was standing, then walks back (also
after the fight ends). Your attack and attack-move orders aren't leashed; hold position never chases.

Esc never quits the game directly; use Exit in a menu or close the window. (The web build has no Exit buttons.)

## Code layout

`maps/` holds the map files and `assets/sprites/` the art. Source is in `src/`: `main.c` (game states) at the top,
`src/game/` for the game and the systems the editor reuses (ui, map, map files, tables),
and `src/editor/` for the editor.


| File | System |
|---|---|
| `main.c` | Window, game states (menu / playing / paused / victory / defeat / editor), fixed 30 Hz sim loop, new game (map file or Random), win/lose check, editor ↔ game hand-over, performance log |
| `game/overlay.c` | F3 debug overlay: FPS (now, min / avg), frame / tick / fog / path times, counts per team, AI state; one FPS line outside the game |
| `game/config.h` | Shared settings: tick rate, teams, unit and building stats tables, game states, key bindings, controls list |
| `game/map.c` | Tile map: generated "Random" map, real size of the loaded map, walkability (terrain + building-blocked tiles), culled drawing |
| `game/mapfile.c` | Map files: `MapDoc` (tiles + objects), parse + full validation with file:line errors, load into the game, write, scan the folder |
| `editor/editor.c` | Map editor: tile brushes with undo, object tools, save / load / test play |
| `editor/web_download.js` | Web build only: the editor's Save hands the file to the browser as a download |
| `web/shell.html` | Web build only: the page around the game (canvas scaling, loading bar, no right-click menu, game keys kept from the browser) |
| `game/sprites.c` | Optional PNG art: scans `assets/sprites`, packs it into one atlas texture (shelf packer), draws units / buildings / tiles from it |
| `game/camera.c` | Pan / zoom, visible-area queries |
| `game/units.c` | Unit pool, movement, separation, drawing |
| `game/grid.c` | Spatial grid for nearby-unit queries |
| `game/path.c` | A* pathfinding: request queue, per-frame time budget, path smoothing; walkable regions ("can I get there?") |
| `game/input.c` | Selection list (units, building, gold node), orders, hotkeys, building placement ghost |
| `game/minimap.c` | Minimap: cached terrain/fog texture, unit dots, camera outline, click to move camera / units |
| `game/fog.c` | Fog of war: per-team visibility grid, recomputed 5× a second, one batched overlay pass |
| `game/heal.c` | Healers (`canHeal`): find the nearest damaged ally (grid), walk into range, heal per tick, spread over patients, follow-and-heal order, green heal lines |
| `game/combat.c` | Attacking, chasing, auto-targeting (aggro), projectile pool (arrows, magic bolts with splash, splash rings), minimum range, the damage formula (`CombatDamage()`: damage type × armor type, minus armor) |
| `game/ai.c` | Enemy AI: trains workers to a per-node target, builds a Barracks and an Archery Range, expands to new gold, trains its army, sends idle units at the player |
| `game/economy.c` | Gold per team, gold node pool, worker mining loop, gold HUD (top right) |
| `game/buildings.c` | Building pool, tile blocking, placement checks, prerequisites (`BuildingsCanBuild()`), production queue (cancel/refund), rally points, gold drop-off lookup, construction by workers, drawing |
| `game/ui.c` | Tiny immediate-mode UI (buttons, panels, labels, tabs, scroll areas), scales with window height, blocks clicks from reaching the game |
| `game/menu.c` | Main menu, map picker, pause menu, Controls page, Victory / Defeat screen |
| `game/inspector.c` | Bottom panel for the selection; Train / Build buttons generated from the stats tables; hotkey clash check |

## Performance

Measured for version 1.0.0 on the target hardware: **Intel Celeron N4120** (4 cores, 1.1 GHz
base), **4 GB RAM** (3.6 GB usable), integrated graphics, Arch Linux. Release build, 1280×720.
Scenario: River Crossing, 100 workers mining plus a 300 vs 300 battle (Melee, Archers, Knights
and 30 Scouts; 732 units in all).

| | Desktop (60 FPS cap) | Desktop (uncapped) | Web (Firefox) |
|---|---|---|---|
| FPS, 100 workers + 300 vs 300 | 60 (min 60) | 390 avg (min 355) | 59 (min 59) |
| FPS, full unit pool (2,046 units) | 59 (min 58) | 293 avg (min 259) | not measured |
| Sim tick (30 per second), battle | 1.1–1.7 ms avg, 4.8 ms worst | | |
| Sim tick, 2,046 units | 5–8 ms avg, 20 ms worst | | |
| Fog of war update (5 per second) | 0.14–0.23 ms avg, 0.38 ms worst (battle); 0.70 ms worst (2,046 units) | | |
| Peak memory | 51 MB | | |
| Startup: packing the art atlas | 2–4 ms | | 10–40 ms |
| Download size | | | 207 KB zip (wasm 405 KB, js 180 KB, maps + art 36 KB) |

Limits are fixed pools, set in headers: 2,048 units (`MAX_UNITS`), 64 buildings
(`MAX_BUILDINGS`), 64 gold nodes, 1,024 projectiles, maps up to 128×128 tiles. Other apps
running on the same machine lower the uncapped numbers a lot. Press F3 to see the live numbers.

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

### Balance (measured)

Equal-gold fights (1,500 gold each, two groups walking into each other):

| | Result |
|---|---|
| Melee vs Archer / Knight / Mage / Scout | Melee wins every time (keeps 95% / 85% / 45% / 100% of its value) |
| Knight vs Archer | Knight wins, loses nothing |
| Mage vs Archer | Mage wins, keeps 86% |
| Knight vs Mage | Knight wins, keeps 62% |

AI vs AI (the same AI on both sides, fog off, starting soldiers removed) ends in 4–10 minutes,
and both sides train all seven unit types on every map with an economy. Which side wins depends
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
  `PLAYTEST.md`, `assets/` and `maps/`, in one `rts-kit-<version>/` folder.

For 1.0.0: web zip 207 KB, source zip 149 KB; about 1.5 minutes on the Celeron (the web build
compiles raylib from scratch).

The version comes from `GAME_VERSION` in `config.h`. Go through [PLAYTEST.md](PLAYTEST.md) first.
