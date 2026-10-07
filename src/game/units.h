// units.h - Unit pool: every unit in the game lives in one fixed array.
#ifndef UNITS_H_INCLUDED
#define UNITS_H_INCLUDED

#include "raylib.h"
#include "config.h"
#include <stdbool.h>

#define MAX_UNITS   2048
#define UNIT_RADIUS 6.0f    // world pixels

typedef enum { GATHER_NONE, GATHER_TO_NODE, GATHER_MINING, GATHER_TO_BASE } GatherState;

typedef struct Unit {
    bool         active;      // false = free slot in the pool
    unsigned int serial;      // unique per spawn: tells a reused slot apart from the unit that died in it
    UnitType     type;
    int          team;
    float        hp;
    float        incomingDamage;  // damage in projectiles already flying at this unit
    bool         selected;

    // Movement
    bool    moving;      // following a path
    Vector2 pos;         // position after the latest sim tick
    Vector2 prevPos;     // position one tick earlier (used to draw smoothly between ticks)
    Vector2 target;      // where the unit is walking to, if moving
    float   radius;
    float   speed;
    float   bestDist;    // closest it has got to its current waypoint
    int     stuckTicks;  // ticks since bestDist last improved
    int     repathsLeft; // new paths it may still ask for before giving up on this order
    bool    attackMove;      // attack-move order: fight enemies met on the way...
    Vector2 attackMoveDest;  // ...then carry on to this spot
    bool    holdPosition;    // hold order: only fight enemies already in range, never chase
    bool    facingLeft;      // art is drawn mirrored (sprites face right in the PNG)

    // Combat (combat.c)
    bool         attacking;
    bool         attackTargetIsBuilding;  // target is in buildings[], not units[]
    int          attackTarget;            // slot index...
    unsigned int attackTargetSerial;      // ...and its serial, to detect that it died
    bool         chaseDirect;         // straight line to target is clear: walk at it, no path needed
    int          cooldownTicks;       // ticks until it can attack again
    int          acquireTicks;        // idle: ticks until it next looks for enemies
    int          chaseTicks;          // attacking: ticks until it re-plans its chase
    bool         leashed;             // chasing on its own (auto-target): may not stray far from...
    Vector2      leashHome;           // ...here, where it was standing

    // Gathering (economy.c) - workers only
    GatherState  gatherState;
    int          gatherNode;          // gold node slot...
    unsigned int gatherNodeSerial;    // ...and serial
    int          dropBase;            // base being walked to (slot + serial)
    unsigned int dropBaseSerial;
    int          gatherTicks;         // mining countdown
    int          carryGold;

    // Construction (buildings.c) - workers only
    bool         buildOrder;          // walking to / building an unfinished building
    int          buildSite;           // building slot...
    unsigned int buildSiteSerial;     // ...and serial

    int          orderRetries;        // gather/build: path attempts left before giving up
} Unit;

extern Unit units[MAX_UNITS];

int  UnitSpawn(Vector2 pos, UnitType type, int team);   // returns the unit's index, or -1 if the pool is full
void UnitDespawn(int id);
bool UnitIsAlive(int id, unsigned int serial);          // is this exact unit still in the game?
int  UnitsActiveCount(void);
void UnitsReset(void);   // remove every unit (new game)

void UnitsTick(void);                        // advance every unit by one sim tick
void UnitsDraw(Rectangle view, float alpha); // alpha: 0..1, how far we are between ticks
void UnitsDrawIcon(UnitType type, int team, Vector2 centre, float radius);   // the unit's look (also used by the inspector)

// Orders (from the player or the AI)
void UnitsOrderMove(const int *ids, int count, Vector2 dest);
void UnitsOrderAttack(const int *ids, int count, int target);
void UnitsOrderAttackBuilding(const int *ids, int count, int building);
void UnitsOrderAttackMove(const int *ids, int count, Vector2 dest);   // move, but fight anything met on the way
void UnitsOrderStop(const int *ids, int count);   // drop all orders and go idle
void UnitsOrderHold(const int *ids, int count);   // stop, then stay put: attack only what's in range
int  UnitsOpenSpots(Vector2 centre, int count, Vector2 *out);  // free spots around centre, closest first

// Movement helpers used by combat.c
void    UnitMoveTo(int id, Vector2 dest);       // queue a (budgeted) path to dest
void    UnitStop(int id);
Vector2 UnitFollowPath(int id);                 // this tick's step along the path
Vector2 UnitStepToward(int id, Vector2 point);  // this tick's step straight at a point

#endif
