// map.h - Tile map: a fixed grid of terrain tiles.
#ifndef MAP_H_INCLUDED
#define MAP_H_INCLUDED

#include "raylib.h"
#include "config.h"   // MoveClass
#include <stdbool.h>

#define TILE_SIZE   32                  // world pixels per tile
#define MAP_W       128                 // MAXIMUM tiles across (arrays are this big)
#define MAP_H       128                 // MAXIMUM tiles down
#define MAP_PIXEL_W (MAP_W * TILE_SIZE)
#define MAP_PIXEL_H (MAP_H * TILE_SIZE)
// A loaded map can be smaller: MapWidth()/MapHeight() give its real size, and
// nothing outside it can be entered (not even by air).

typedef enum { TILE_GRASS, TILE_DIRT, TILE_WATER, TILE_ROCK, TILE_GRAVEL, TILE_LAVA, TILE_COUNT } TileType;

// Everything about a tile type in one place: map drawing, who can cross it,
// the character used in map files, the minimap colour and the editor's brush
// buttons all read this. To add a tile: add it to the enum (before
// TILE_COUNT) and give it a row in TILE_INFO (map.c), with a character no
// other tile uses. Art: assets/sprites/tiles/<name>.png.
typedef struct TileInfo {
    const char *name;
    char        fileChar;   // character in .map files
    Color       color;      // drawn colour (also the minimap's)
    bool        ground;     // MOVE_GROUND units can walk on it (and buildings and gold can stand on it)
    bool        naval;      // MOVE_NAVAL units can sail on it
    bool        air;        // MOVE_AIR units can fly over it
} TileInfo;

extern const TileInfo TILE_INFO[TILE_COUNT];

void     MapGenerate(unsigned int seed);   // same seed = same map (always MAP_W x MAP_H)
void     MapSetTiles(int width, int height, const unsigned char *types);   // from a map file: types[y*width + x]
int      MapWidth(void);                   // this map, in tiles
unsigned MapVersion(void);                // goes up whenever the terrain changes (the minimap redraws then)
void     MapBackup(void);                  // remember the whole map (tiles, blocked tiles, size)...
void     MapRestore(void);                 // ...and put it back (the editor uses this to leave a paused game untouched)
int      MapHeight(void);
TileType MapGetTile(int tx, int ty);       // outside the map counts as TILE_ROCK
bool     TileAllows(TileType type, MoveClass moveClass);          // the TILE_INFO column for that class
// Can a unit of this class be on tile tx,ty? Inside the map, the tile allows
// the class, and (GROUND and NAVAL only) no building stands there.
bool     MapTileWalkable(MoveClass moveClass, int tx, int ty);
void     MapSetBlocked(int tx, int ty, int w, int h, bool blocked);  // buildings mark/unmark their tiles
void     MapClearArea(Vector2 worldPos, int radiusTiles);            // turn terrain to grass (room for a base)
bool     MapIsWalkable(MoveClass moveClass, Vector2 worldPos);
bool     MapCircleWalkable(MoveClass moveClass, Vector2 centre, float radius);            // a round unit fits here
bool     MapLineClear(MoveClass moveClass, Vector2 from, Vector2 to, float radius);       // a round unit can move straight from -> to
void     MapDraw(Rectangle view);          // draws only tiles inside `view` (world coords)

#endif
