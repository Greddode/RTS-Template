// buildings.h - Building pool: bases (and future building types).
#ifndef BUILDINGS_H_INCLUDED
#define BUILDINGS_H_INCLUDED

#include "raylib.h"
#include "config.h"
#include <stdbool.h>

#define MAX_BUILDINGS    64
#define MAX_QUEUE        5     // production queue length

typedef struct Building {
    bool         active;          // false = free slot in the pool
    unsigned int serial;          // unique per placement (same idea as units)
    BuildingType type;
    int          team;
    float        hp;
    float        incomingDamage;  // projectiles already flying at it (see combat.c)
    bool         seenByPlayer;    // fog of war: the player has seen it, so it's drawn under fog
    int          tx, ty, size;    // top-left tile and size in tiles

    // Construction: placed buildings start unfinished; workers build them up.
    bool         constructing;
    int          buildTicks;      // progress so far

    // Production
    UnitType     queue[MAX_QUEUE];
    int          queueCount;
    int          trainTicks;      // progress on queue[0]
    Vector2      rally;           // trained units walk here (default: where they appear)
} Building;

extern Building buildings[MAX_BUILDINGS];

Rectangle BuildingFootprint(BuildingType type, Vector2 centre);    // tiles it would cover, snapped to the grid
bool BuildingCanPlace(BuildingType type, Vector2 centre);          // open ground, no building or gold node in the way
int  BuildingPlace(BuildingType type, int team, Vector2 centre, bool unfinished);   // index, or -1 if blocked / pool full
void BuildingDestroy(int id);
bool BuildingIsAlive(int id, unsigned int serial);

Rectangle BuildingRect(int id);                     // footprint in world pixels
Vector2   BuildingCentre(int id);
float     BuildingDistance(int id, Vector2 p);      // from p to the nearest wall (0 if inside)
Vector2   BuildingApproachPoint(int id, Vector2 from, float radius);   // open spot just outside, facing `from` (or the nearest open one)
int       BuildingAt(Vector2 p);                    // building under a point, or -1
int       BuildingsFindNearestEnemy(Vector2 pos, float maxDist, int team);   // skips doomed ones
int       BuildingsFindDropOff(Vector2 pos, int team);                       // nearest finished gold drop-off, or -1
bool      BuildingsFindSpot(BuildingType type, Vector2 near, Vector2 *out);  // nearest place it fits, searching outward
void      BuildingSetRally(int id, Vector2 point);

bool BuildingQueueTrain(int id, UnitType type);     // pays the cost; false if too poor, queue full or wrong building
void BuildingCancelQueued(int id, int index);       // remove queue[index] and refund its cost
float BuildingBuildProgress(int id);                // 0..1 while unfinished

void    BuildingsOrderConstruct(const int *ids, int count, int building);   // workers walk over and build it
Vector2 BuildingsWorkerTick(int id);                // a constructing worker's step this tick
void BuildingsTick(void);                           // production; once per sim tick
void BuildingsDraw(Rectangle view);
void BuildingsDrawLook(BuildingType type, int team, Rectangle r, bool unfinished);   // one building's art or shape (also used by the editor)
void BuildingsReset(void);   // remove every building (new game)
int  BuildingsCount(int team);   // finished + unfinished buildings this team has

#endif
