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
    int          tx, ty, size;    // top-left tile and size in tiles

    // Production
    UnitType     queue[MAX_QUEUE];
    int          queueCount;
    int          trainTicks;      // progress on queue[0]
} Building;

extern Building buildings[MAX_BUILDINGS];

int  BuildingPlace(BuildingType type, int team, Vector2 centre);   // index, or -1 if blocked / pool full
void BuildingDestroy(int id);
bool BuildingIsAlive(int id, unsigned int serial);

Rectangle BuildingRect(int id);                     // footprint in world pixels
Vector2   BuildingCentre(int id);
float     BuildingDistance(int id, Vector2 p);      // from p to the nearest wall (0 if inside)
Vector2   BuildingApproachPoint(int id, Vector2 from, float radius);   // open spot just outside, facing `from`
int       BuildingAt(Vector2 p);                    // building under a point, or -1
int       BuildingsFindNearest(Vector2 pos, float maxDist, int team, bool enemy);   // enemy: skips doomed ones

bool BuildingQueueTrain(int id, UnitType type);     // pays the cost; false if too poor or queue full
void BuildingsTick(void);                           // production; once per sim tick
void BuildingsDraw(Rectangle view);

#endif
