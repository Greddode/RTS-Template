// ai.c - A very simple computer opponent.
//
// Every AI_THINK_TICKS (2 s of sim time) the AI looks at its idle units and
// sends each one to attack the nearest player unit. If the player has no
// units left, idle AI units march on the player's base position instead.
// Units that are already fighting or walking are left alone, and once they
// arrive, combat.c's auto-targeting takes over.
//
// Finding "the nearest player unit" uses the spatial grid. Units standing in
// the same grid cell share one search per think, so a wave of 20 units costs
// only a few searches.

#include "ai.h"
#include "config.h"
#include "grid.h"
#include "units.h"

static Vector2 playerBase, aiSpawn;
static int thinkCountdown = AI_THINK_TICKS;

// Per-think cache: grid cell -> nearest player unit found from it.
static int          cellTarget[GRID_W*GRID_H];
static unsigned int cellStamp[GRID_W*GRID_H];
static unsigned int thinkStamp = 0;

void AiInit(Vector2 base, Vector2 spawn)
{
    playerBase = base;
    aiSpawn = spawn;
}

static int NearestPlayerUnit(Vector2 from)
{
    int cx = (int)(from.x/GRID_CELL_SIZE), cy = (int)(from.y/GRID_CELL_SIZE);
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    if (cx > GRID_W - 1) cx = GRID_W - 1;
    if (cy > GRID_H - 1) cy = GRID_H - 1;
    int cell = cy*GRID_W + cx;

    if (cellStamp[cell] != thinkStamp)
    {
        cellStamp[cell] = thinkStamp;
        cellTarget[cell] = GridFindNearestEnemy(from, (float)(MAP_PIXEL_W + MAP_PIXEL_H), AI_TEAM);
    }
    return cellTarget[cell];
}

void AiTick(void)
{
    if (--thinkCountdown > 0) return;
    thinkCountdown = AI_THINK_TICKS;
    thinkStamp++;

    // Scanning the pool every 2 s to find our idle units is cheap; it isn't a
    // "who's nearby" search, those go through the grid.
    static int toBase[MAX_UNITS];
    int toBaseCount = 0;
    for (int i = 0; i < MAX_UNITS; i++)
    {
        Unit *u = &units[i];
        if (!u->active || u->team != AI_TEAM || u->moving || u->attacking) continue;

        int target = NearestPlayerUnit(u->pos);
        if (target != -1) UnitsOrderAttack(&i, 1, target);
        else toBase[toBaseCount++] = i;
    }
    UnitsOrderMove(toBase, toBaseCount, playerBase);
}

void AiSpawnWave(int count)
{
    static Vector2 spots[MAX_UNITS];
    if (count > MAX_UNITS) count = MAX_UNITS;
    int found = UnitsOpenSpots(aiSpawn, count, spots);
    for (int k = 0; k < found; k++) UnitSpawn(spots[k], (k % 2) ? UNIT_RANGED : UNIT_MELEE, AI_TEAM);
}
