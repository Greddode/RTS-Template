// fog.h - Fog of war: what each team can see.
#ifndef FOG_H_INCLUDED
#define FOG_H_INCLUDED

#include "raylib.h"
#include <stdbool.h>

typedef enum { FOG_UNSEEN, FOG_EXPLORED, FOG_VISIBLE } FogState;

void     FogReset(void);                   // new game: everything unseen, then reveal around the starting units
void     FogTick(void);                    // once per sim tick; recomputes every FOG_UPDATE_TICKS
void     FogUpdateNow(void);               // recompute right away
void     FogDraw(Rectangle view);          // the dark overlay; call inside BeginMode2D, after the world

bool     FogEnabled(void);
void     FogSetEnabled(bool on);           // pause menu toggle
FogState FogTileState(int team, int tx, int ty);
bool     FogCanSee(int team, Vector2 pos); // may `team` see this point now? (fog off / AI cheating: always)
bool     FogCanSeeRect(int team, Rectangle r);   // any part of the area (buildings)
bool     FogExplored(int team, Vector2 pos);     // seen at some point (gold nodes stay shown)
double   FogLastUpdateMs(void);            // cost of the last recompute

#endif
