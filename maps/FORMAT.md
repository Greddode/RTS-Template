# Map file format

Every `.map` file in this folder shows up in the game's map picker (**Play**), at most 32. A map
is plain text, so you can write one by hand, edit one made in the map editor, and see changes in
a diff. The game only reads files ending in `.map`; this guide is ignored.

## Example

```
# Lines starting with # (outside the tile grid) and blank lines are comments.
name Duel (64x64)
width 64
height 64
tiles
................,,,,,...........~~~~~...........................
..........#####.,,,,,...........~~~~~...........................
(... 64 rows in all, each exactly 64 characters ...)

base 0 8 52
worker 0 12 51
worker 0 12 53
base 1 52 8
worker 1 51 12
archery_range 1 47 6
gold 5 46 1500
gold 58 17 1500
```

## Header

These come first, in this order before `tiles`:

| Line | Meaning |
|---|---|
| `name <text>` | Shown in the map picker. Optional: without it the file name is shown. Up to 63 characters. |
| `width <n>` | Tiles across, 8 to 128. |
| `height <n>` | Tiles down, 8 to 128. |
| `tiles` | Then exactly `height` rows of exactly `width` characters, one per tile. |

## Tiles

| Character | Tile | Ground units, buildings, gold | Naval units | Air units |
|---|---|---|---|---|
| `.` | grass | yes | no | yes |
| `,` | dirt | yes | no | yes |
| `:` | gravel | yes | no | yes |
| `~` | water | no | yes | yes |
| `#` | rock / wall | no | no | yes |
| `^` | lava | no | no | yes |

The tile rows are read exactly as written: inside the grid, `#` is rock, not a comment. The
characters and the three "who can cross it" columns come from `TILE_INFO` in `src/game/map.c`;
a new tile type gets its own character there. Every unit has a movement class (`moveClass` in
`UNIT_STATS`); all units are ground units for now.

## Objects

After the tiles, one object per line. Coordinates are in tiles, `0 0` is the top-left corner.

| Line | Meaning |
|---|---|
| `<building> <team> <x> <y>` | A finished building. `x y` is its **top-left** tile. |
| `<unit> <team> <x> <y>` | A unit standing on that tile. |
| `gold <x> <y> <amount>` | A gold node with 1 to 1,000,000 gold (1500 is the usual). |

- **Teams:** `0` = the player, `1` = the computer.
- **Building keywords:** `base` (3×3 tiles), `barracks`, `archery_range`, `academy`, `air_factory` (2×2 each).
- **Unit keywords:** `worker`, `melee`, `archer`, `knight`, `medic`, `mage`, `scout`, `falcon`, `airship`.
  Flyers (`falcon`, `airship`) may also stand on water, rock and lava (see the tile table).
- Keywords are the `name` columns of `BUILDING_STATS` / `UNIT_STATS` in `config.h`, in any case,
  with a space written as `_` ("Archery Range" → `archery_range`). A unit or building you add to
  those tables works in map files with no other change.
- **Gold has no team.** Any side's workers can mine any node; put nodes where you want each side
  to mine. (In the editor, the Player / AI choice doesn't apply to gold.)
- Map files and the editor can place any building, even one whose prerequisite is missing (an
  Academy without a Barracks). Prerequisites only apply when workers build in a game.

## Rules (checked when the map loads)

- Every row has exactly `width` characters, there are exactly `height` rows, and only the six
  tile characters are used.
- Every object is inside the map, on a tile it can stand on: buildings and gold on ground tiles
  (grass, dirt, gravel) with the building's whole footprint on them, units on tiles their
  movement class allows (see the table above).
- Objects don't overlap (a unit or gold node on a building's tiles counts as overlapping).
- **Each team has at least one building**, or it would lose at once.
- At most 1,024 objects; at most 64 buildings, 64 gold nodes and 2,048 units.

A mistake shows on screen as `file name:line: what's wrong`, and the game plays the Random map
instead. The editor's **Save** writes the file, then reads it back with the same checks.

## Making a fair map

- Give each side a **Base and workers** (4 is the usual start). Without workers a side can't mine.
- **Same gold near each base:** count the nodes within about 12 tiles' *walk* (around water and
  rock, not in a straight line), and their amounts.
- **Same expansions, equally safe:** the AI expands to a rich node 15+ tiles from its bases, and
  armies march along the shortest route between the bases. A side whose expansion sits on that
  route loses it again and again. Mirroring the map (every object at `x y` also at
  `width-1-x  height-1-y` for the other team) is the easiest way to be fair.
- Test it: watch the AI play its side (F3 shows what it's doing), and play both sides yourself.
