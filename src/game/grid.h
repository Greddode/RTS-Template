// grid.h - Spatial grid: fast "which units are near here?" queries.
#ifndef GRID_H_INCLUDED
#define GRID_H_INCLUDED

#include "raylib.h"
#include "map.h"

#define GRID_CELL_SIZE 64                          // world pixels per cell
#define GRID_W ((MAP_PIXEL_W + GRID_CELL_SIZE - 1) / GRID_CELL_SIZE)
#define GRID_H ((MAP_PIXEL_H + GRID_CELL_SIZE - 1) / GRID_CELL_SIZE)

void GridRebuild(void);                              // call after units move, and once before the first query
int  GridQuery(Rectangle area, int *out, int maxOut); // unit ids whose centre is in `area`
int  GridFindNearestEnemy(Vector2 pos, float maxDist, int myTeam, bool ground, bool air);   // closest live, visible, not-doomed enemy (ground ones / flyers), or -1

#endif
