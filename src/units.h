// units.h - Unit pool: every unit in the game lives in one fixed array.
#ifndef UNITS_H_INCLUDED
#define UNITS_H_INCLUDED

#include "raylib.h"
#include <stdbool.h>

#define MAX_UNITS   2048
#define UNIT_RADIUS 6.0f    // world pixels
#define UNIT_SPEED  70.0f   // world pixels per second

typedef struct Unit {
    bool    active;     // false = free slot in the pool
    bool    selected;
    bool    moving;
    Vector2 pos;        // position after the latest sim tick
    Vector2 prevPos;    // position one tick earlier (used to draw smoothly between ticks)
    Vector2 target;     // where the unit is walking to, if moving
    float   radius;
    float   speed;
    float   bestDist;    // closest it has got to its current waypoint
    int     stuckTicks;  // ticks since bestDist last improved
    int     repathsLeft; // new paths it may still ask for before giving up on this order
} Unit;

extern Unit units[MAX_UNITS];

int  UnitSpawn(Vector2 pos);    // returns the unit's index, or -1 if the pool is full
void UnitDespawn(int id);
int  UnitsActiveCount(void);

void UnitsTick(void);                       // advance every unit by one sim tick
void UnitsDraw(Rectangle view, float alpha); // alpha: 0..1, how far we are between ticks
void UnitsOrderMove(const int *ids, int count, Vector2 dest);

#endif
