// map.h - Tile map: a fixed grid of terrain tiles.
#ifndef MAP_H_INCLUDED
#define MAP_H_INCLUDED

#include "raylib.h"
#include <stdbool.h>

#define TILE_SIZE   32                  // world pixels per tile
#define MAP_W       128                 // tiles across
#define MAP_H       128                 // tiles down
#define MAP_PIXEL_W (MAP_W * TILE_SIZE)
#define MAP_PIXEL_H (MAP_H * TILE_SIZE)

typedef enum { TILE_GRASS, TILE_DIRT, TILE_WATER, TILE_ROCK, TILE_COUNT } TileType;

void     MapGenerate(unsigned int seed);   // same seed = same map
TileType MapGetTile(int tx, int ty);       // outside the map counts as TILE_ROCK
bool     MapTileWalkable(int tx, int ty);
bool     MapIsWalkable(Vector2 worldPos);
bool     MapCircleWalkable(Vector2 centre, float radius);            // a round unit fits here
bool     MapLineClear(Vector2 from, Vector2 to, float radius);       // a round unit can walk straight from -> to
void     MapDraw(Rectangle view);          // draws only tiles inside `view` (world coords)

#endif
