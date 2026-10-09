// grid.h - Spatial grid: fast "which units are near here?" queries.
#ifndef GRID_H_INCLUDED
#define GRID_H_INCLUDED

#include "raylib.h"
#include "map.h"

#define GRID_CELL_SIZE 64                          // world pixels per cell
#define GRID_W ((MAP_PIXEL_W + GRID_CELL_SIZE - 1) / GRID_CELL_SIZE)
#define GRID_H ((MAP_PIXEL_H + GRID_CELL_SIZE - 1) / GRID_CELL_SIZE)

void GridRebuild(void);                              // call after units move, and once before the first query
void GridNoteSpawn(int id, int team);                // UnitSpawn calls this (keeps the per-team lists exact)
int  GridQuery(Rectangle area, int *out, int maxOut); // unit ids whose centre is in `area`
// Closest live, visible, not-doomed enemy (ground ones / flyers), or -1. Only ones the searcher
// can hit from `pos` (within `range`) or reach: a searcher of `moveClass` (PathCanReach).
int  GridFindNearestEnemy(Vector2 pos, float maxDist, int myTeam, bool ground, bool air, MoveClass moveClass, float range);

#endif
