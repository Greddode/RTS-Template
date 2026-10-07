// minimap.h - The minimap in the bottom-left corner.
#ifndef MINIMAP_H_INCLUDED
#define MINIMAP_H_INCLUDED

#include "raylib.h"
#include <stdbool.h>

void      MinimapDraw(void);          // draw + handle clicks; call while playing, after EndMode2D()
Rectangle MinimapRect(void);          // where it is on screen (others keep clear of it)
void      MinimapReset(void);         // new game / new map: redraw the picture next frame
double    MinimapLastRebuildMs(void); // cost of the last picture redraw

#endif
