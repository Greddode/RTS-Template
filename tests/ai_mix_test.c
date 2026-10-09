// ai_mix_test.c - Checks the AI's table-driven choices (config.h): the army mix
// (AI_ARMY_MIX) picks the Falcon when it is furthest below its share, never
// without a finished Air Factory, and never past its maxAlive cap; and the tech
// list (AI_TECH_ORDER) treats a type listed twice (the Guard Towers) as two
// buildings. Calls the AI's own TrainTick / TechTick on a small map built here.
// No window needed.
//
//   make test        (or: ./tests/build/ai_mix_test after building tests/)
//
// ai.c is #included (like naval_test does with editor.c) to reach its statics,
// so it is left out of this test's source list.

#include "../src/game/ai.c"
#include "combat.h"
#include <stdio.h>
#include <stdlib.h>

static int failures = 0, checks = 0;
static void Check(bool ok, const char *what)
{
    checks++;
    if (!ok) { failures++; printf("  FAIL %s\n", what); }
    else if (getenv("AI_MIX_VERBOSE")) printf("  ok   %s\n", what);   // AI_MIX_VERBOSE=1: list every check
}

#define TW 48
#define TH 48
static unsigned char testTiles[TW*TH];

static Vector2 Tile(float x, float y) { return (Vector2){ (x + 0.5f)*TILE_SIZE, (y + 0.5f)*TILE_SIZE }; }

static void SetGold(int gold)
{
    EconomySpend(AI_TEAM, EconomyGold(AI_TEAM));
    EconomyAdd(AI_TEAM, gold);
}

// An all-grass map with a finished AI Base, Barracks, Archery Range and Academy,
// and (if asked) an Air Factory, finished or not. Gold 0.
static int airFactory;
static void NewWorld(bool withAirFactory, bool airFactoryFinished)
{
    for (int i = 0; i < TW*TH; i++) testTiles[i] = TILE_GRASS;
    UnitsReset();
    BuildingsReset();
    CombatReset();
    PathReset();
    EconomyInit();
    MapSetTiles(TW, TH, testTiles);
    FogSetEnabled(false);
    GridRebuild();
    int base = BuildingPlace(BUILDING_BASE, AI_TEAM, Tile(10, 10), false);
    BuildingPlace(BUILDING_BARRACKS,      AI_TEAM, Tile(18, 10), false);
    BuildingPlace(BUILDING_ARCHERY_RANGE, AI_TEAM, Tile(24, 10), false);
    BuildingPlace(BUILDING_ACADEMY,       AI_TEAM, Tile(30, 10), false);
    airFactory = withAirFactory ? BuildingPlace(BUILDING_AIR_FACTORY, AI_TEAM, Tile(36, 10), !airFactoryFinished) : -1;
    PathComputeRegions();
    AiInit(Tile(40, 40), Tile(10, 14), base);
    SetGold(0);
}

// AI units of each type, standing in a row (they only count towards the mix).
static void Army(int melee, int knights, int archers, int mages, int scouts, int medics, int falcons)
{
    const int counts[] = { melee, knights, archers, mages, scouts, medics, falcons };
    const UnitType types[] = { UNIT_MELEE, UNIT_KNIGHT, UNIT_ARCHER, UNIT_MAGE, UNIT_SCOUT, UNIT_MEDIC, UNIT_FALCON };
    int n = 0;
    for (int t = 0; t < 7; t++)
        for (int k = 0; k < counts[t]; k++, n++) UnitSpawn(Tile((float)(4 + n % 40), (float)(30 + n/40)), types[t], AI_TEAM);
    GridRebuild();
}

static int Queued(UnitType type)
{
    int n = 0;
    for (int b = 0; b < MAX_BUILDINGS; b++)
        if (buildings[b].active && buildings[b].team == AI_TEAM)
            for (int q = 0; q < buildings[b].queueCount; q++) n += buildings[b].queue[q] == type;
    return n;
}

static int QueuedTotal(void)
{
    int n = 0;
    for (int t = 0; t < UNIT_TYPE_COUNT; t++) n += Queued((UnitType)t);
    return n;
}

static void Train(void) { trainCountdown = 1; TrainTick(); }

static int MixShare(UnitType type, int *maxAlive)
{
    for (int m = 0; m < AI_ARMY_MIX_COUNT; m++)
        if (AI_ARMY_MIX[m].type == type) { *maxAlive = AI_ARMY_MIX[m].maxAlive; return AI_ARMY_MIX[m].share; }
    *maxAlive = 0;
    return 0;
}

static void TestArmyMix(void)
{
    int falconCap;
    Check(MixShare(UNIT_FALCON, &falconCap) > 0 && falconCap > 0, "the Falcon is in AI_ARMY_MIX with a share and a maxAlive cap");
    int airshipCap, boatCap, shipCap;
    Check(MixShare(UNIT_AIRSHIP, &airshipCap) == 0 && MixShare(UNIT_BOAT, &boatCap) == 0 && MixShare(UNIT_SHIP, &shipCap) == 0,
          "no Airships, Boats or Ships in AI_ARMY_MIX");

    // Every other type exactly at its share (ratio 1), no Falcons (ratio 0):
    // with gold for exactly one Falcon, the Falcon is the one bought.
    NewWorld(true, true);
    Army(4, 2, 3, 1, 1, 1, 0);
    SetGold(UNIT_STATS[UNIT_FALCON].cost);
    Train();
    Check(Queued(UNIT_FALCON) == 1 && buildings[airFactory].queueCount == 1 && QueuedTotal() == 1,
          TextFormat("Falcon furthest below its share: it is queued at the Air Factory (%d Falcons, %d total queued)", Queued(UNIT_FALCON), QueuedTotal()));

    // Plenty of gold: Falcons keep coming while they're furthest behind, up to the queue size.
    NewWorld(true, true);
    Army(8, 4, 6, 2, 2, 2, 0);
    SetGold(5000);
    Train();
    Check(Queued(UNIT_FALCON) == AI_BARRACKS_QUEUE, TextFormat("with lots of gold the Air Factory's queue fills with Falcons (%d of %d)", Queued(UNIT_FALCON), AI_BARRACKS_QUEUE));

    // No Air Factory at all: never a Falcon, the gold goes to the rest of the mix.
    NewWorld(false, false);
    Army(4, 2, 3, 1, 1, 1, 0);
    SetGold(5000);
    Train();
    Check(Queued(UNIT_FALCON) == 0 && QueuedTotal() > 0, TextFormat("no Air Factory: no Falcon queued, other units are (%d queued)", QueuedTotal()));

    // An Air Factory still being built doesn't count either.
    NewWorld(true, false);
    Army(4, 2, 3, 1, 1, 1, 0);
    SetGold(5000);
    Train();
    Check(Queued(UNIT_FALCON) == 0 && buildings[airFactory].queueCount == 0 && QueuedTotal() > 0, "unfinished Air Factory: no Falcon queued");

    // At the cap: no more Falcons, even though they're the furthest below their share.
    NewWorld(true, true);
    Army(40, 20, 30, 10, 2, 4, falconCap);
    SetGold(5000);
    Train();
    Check(Queued(UNIT_FALCON) == 0 && QueuedTotal() > 0, TextFormat("%d Falcons alive (the cap): no more queued", falconCap));

    // One below the cap: exactly one more (alive + queued count towards the cap).
    NewWorld(true, true);
    Army(40, 20, 30, 10, 2, 4, falconCap - 1);
    SetGold(5000);
    Train();
    Check(Queued(UNIT_FALCON) == 1, TextFormat("%d Falcons alive: exactly one more queued (got %d)", falconCap - 1, Queued(UNIT_FALCON)));
}

static int OwnCount(BuildingType type)
{
    int n = 0;
    for (int b = 0; b < MAX_BUILDINGS; b++) n += buildings[b].active && buildings[b].team == AI_TEAM && buildings[b].type == type;
    return n;
}

static int TowerRows(void)
{
    int n = 0;
    for (int k = 0; k < AI_TECH_COUNT; k++) n += AI_TECH_ORDER[k] == BUILDING_GUARD_TOWER;
    return n;
}

static void TestTechOrder(void)
{
    int academy = -1, air = -1, dock = -1;
    for (int k = 0; k < AI_TECH_COUNT; k++)
    {
        if (AI_TECH_ORDER[k] == BUILDING_ACADEMY) academy = k;
        if (AI_TECH_ORDER[k] == BUILDING_AIR_FACTORY) air = k;
        if (AI_TECH_ORDER[k] == BUILDING_DOCK) dock = k;
    }
    Check(academy != -1 && air == academy + 1, "AI_TECH_ORDER: the Air Factory comes right after the Academy");
    Check(dock == -1, "AI_TECH_ORDER: no Dock");
    Check(TowerRows() == (AI_BUILDS_TOWERS ? AI_MAIN_BASE_TOWERS : 0), TextFormat("AI_TECH_ORDER: %d Guard Tower rows", TowerRows()));

    // Everything before the towers stands, one tower already: each think starts
    // one more (finished at once here), until there are as many as rows, then no more.
    NewWorld(true, true);
    barracksSlot[0] = FindOwn(BUILDING_BARRACKS);
    barracksSerial[0] = buildings[barracksSlot[0]].serial;
    barracksCount = 1;
    BuildingPlace(BUILDING_GUARD_TOWER, AI_TEAM, Tile(10, 18), false);
    UnitSpawn(Tile(12, 14), UNIT_WORKER, AI_TEAM);
    GridRebuild();
    SetGold(5000);
    for (int think = 0; think < TowerRows() + 2; think++)
    {
        TechTick();
        for (int b = 0; b < MAX_BUILDINGS; b++)   // finish whatever it started
            if (buildings[b].active && buildings[b].team == AI_TEAM && buildings[b].constructing) buildings[b].constructing = false;
        for (int i = 0; i < UnitsPoolEnd(); i++) units[i].buildOrder = false;   // and free the worker
    }
    Check(OwnCount(BUILDING_GUARD_TOWER) == TowerRows(), TextFormat("one tower given, then %d tower rows: %d towers in the end", TowerRows(), OwnCount(BUILDING_GUARD_TOWER)));
    bool distinct = true;
    for (int a = 0; a < AI_TECH_COUNT; a++)
        for (int b = a + 1; b < AI_TECH_COUNT; b++)
            if (techSlot[a] == techSlot[b]) distinct = false;
    Check(distinct, "every tech row tracks its own building");
    Check(OwnCount(BUILDING_DOCK) == 0, "no Dock built");
    Check(EconomyGold(AI_TEAM) == 5000 - (TowerRows() - 1)*BUILDING_STATS[BUILDING_GUARD_TOWER].cost, "paid for exactly the missing towers");
}

int main(void)
{
    SetTraceLogLevel(LOG_WARNING);
    TestArmyMix();
    TestTechOrder();
    printf("%d AI mix checks; %s: %d problem(s)\n", checks, failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
