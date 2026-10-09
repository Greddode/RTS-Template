// units.h - Unit pool: every unit in the game lives in one fixed array.
#ifndef UNITS_H_INCLUDED
#define UNITS_H_INCLUDED

#include "raylib.h"
#include "config.h"
#include <stdbool.h>

// The unit pool. Every array that holds "one entry per unit" (path results,
// grid links, selection, scratch lists) is sized from this, so raising it is
// one edit. Cost: about 0.7 KB of memory per slot, used or not (NOTES.md
// has the table), and loops over the whole pool (the tick, fog, minimap)
// skip free slots quickly. 16,384 measured on the target laptop: README.
#define MAX_UNITS   16384
#define UNIT_RADIUS 6.0f    // world pixels

typedef enum { GATHER_NONE, GATHER_TO_NODE, GATHER_MINING, GATHER_TO_BASE } GatherState;

typedef struct Unit {
    bool         active;      // false = free slot in the pool
    bool         loaded;      // inside a transport (transport.c): still in the pool, but out of the world (UnitIsActiveInWorld).
                              // Next to `active` on purpose: every grid query reads both, and together they're one memory fetch.
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

    // Healing (heal.c) - healers only
    bool         healing;             // has a patient
    bool         healOrdered;         // right-clicked: follow this one anywhere until it's full
    int          healTarget;          // unit slot...
    unsigned int healTargetSerial;    // ...and serial

    // Transports (transport.c)
    // (`loaded` is at the top of the struct, next to `active`)
    bool         boarding;            // walking to `transport` to get in
    int          transport;           // the transport it's in or boarding: slot...
    unsigned int transportSerial;     // ...and serial
    bool         unloading;           // a transport: flying to / letting cargo out at unloadAt
    Vector2      unloadAt;
} Unit;

extern Unit units[MAX_UNITS];

// One past the highest slot in use: every slot from here up is free. Loops
// over the pool stop here instead of at MAX_UNITS, so a big MAX_UNITS costs
// nothing in a small game (UnitSpawn always takes the lowest free slot, so
// units stay packed at the start). Read-only outside units.c.
extern int unitPoolEnd;
static inline int UnitsPoolEnd(void) { return unitPoolEnd; }

// THE test for "this unit takes part in the world": it's in the pool and not
// inside a transport. Loaded units keep their slot and HP but are left out of
// the grid, drawing, the minimap, fog sight, selection, targeting, splash,
// healing and separation - everything that asks this. (Plain `active` only
// means "the slot is used": unit counts include loaded units.)
static inline bool UnitIsActiveInWorld(const Unit *u) { return u->active && !u->loaded; }

static inline MoveClass UnitMoveClass(const Unit *u) { return UNIT_STATS[u->type].moveClass; }   // from UNIT_STATS
static inline bool UnitIsFlying(const Unit *u) { return UnitMoveClass(u) == MOVE_AIR; }

// The targeting rule, used everywhere a target is picked, kept or damaged:
// flyers can only be hit by types with hitsAir, everything else (ground and
// naval units, buildings) only by types with hitsGround.
static inline bool UnitCanHitUnit(UnitType attacker, const Unit *target)
{
    return UnitIsFlying(target) ? UNIT_STATS[attacker].hitsAir : UNIT_STATS[attacker].hitsGround;
}
static inline bool UnitCanHitBuildings(UnitType attacker) { return UNIT_STATS[attacker].hitsGround; }

bool UnitsCanTrain(int team, UnitType type);   // its `requires` building is met (see UNIT_STATS); the Train button and the AI ask this

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
int  UnitsOpenSpots(MoveClass moveClass, Vector2 centre, int count, Vector2 *out);  // free spots (for that class) around centre, closest first
int  UnitsFreeSpots(MoveClass moveClass, Vector2 centre, int count, Vector2 *out);    // open AND nobody standing there, searching far out (debug armies)
int  UnitsOpenSpotsIn(MoveClass moveClass, int region, Vector2 centre, int count, Vector2 *out);  // same, only in that region (PathRegion; 0 = any)

// Movement helpers used by combat.c
void    UnitMoveTo(int id, Vector2 dest);       // queue a (budgeted) path to dest
void    UnitStop(int id);
Vector2 UnitFollowPath(int id);                 // this tick's step along the path
Vector2 UnitStepToward(int id, Vector2 point);  // this tick's step straight at a point

#endif
