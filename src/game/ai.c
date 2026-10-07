// ai.c - A simple computer opponent.
//
// Every AI_THINK_TICKS (2 s) the AI:
//   1. Barracks: once it has AI_BARRACKS_WORKERS workers and the gold, one
//      worker builds a Barracks near its base (another worker takes over if
//      the builder dies; rebuilt if destroyed). If gold piles up past
//      AI_EXTRA_BARRACKS_GOLD while every Barracks has a full queue, it builds
//      another, up to AI_MAX_BARRACKS.
//   1b. Archery Range: once a Barracks is finished, one worker builds one
//      Archery Range (rebuilt if destroyed). Combat training waits while
//      it saves up for it.
//   2. Workers: each finished drop-off base wants AI_WORKERS_PER_NODE workers
//      per reachable gold node near it (at most AI_MAX_WORKERS_PER_BASE). The
//      base that's furthest below its target trains one, if the AI can pay.
//      Once every base is saturated it stops, so gold goes to the army.
//      (While it's still waiting for a Barracks, the Barracks comes first.)
//   3. Expansion (every AI_EXPAND_CHECK_TICKS): look for a gold node that is
//      far from its drop-offs, still rich, reachable and not next to an enemy
//      building. With the base's cost plus a reserve (or just the cost when
//      its own nodes run low), one worker builds a new Base near it. Once its
//      workers are saturated (or its gold runs low) it also saves up for it,
//      pausing combat training (AI_SAVE_FOR_EXPANSION). One expansion
//      at a time; if the builder dies, the site is cancelled (refunded) and
//      that node isn't tried again. The new base's rally point faces its node.
//   4. Idle units: workers go to the gold node near their base with the
//      fewest workers on it; combat units attack the nearest player unit (or
//      building, or march on the player's base).
// Separately, every AI_TRAIN_TICKS each Barracks with room queues a Melee
// unit and the Archery Range an Archer. (Knights aren't trained yet.)
//
// "Reachable" uses PathRegion(): a flood fill of the walkable tiles, redone
// every think, so it answers "could pathfinding get there?" instantly.
// All numbers are in config.h. No pools of its own: it keeps a few slot +
// serial pairs (Barracks, expansion site, builder, node) and fixed arrays.

#include "ai.h"
#include "config.h"
#include "buildings.h"
#include "economy.h"
#include "grid.h"
#include "path.h"
#include "units.h"
#include "raymath.h"
#include <stdarg.h>
#include <stdio.h>

static Vector2      playerBase, aiSpawn;
static int          aiBase;                 // the starting base: slot...
static unsigned int aiBaseSerial;           // ...and serial
static int          thinkCountdown, trainCountdown, expandCountdown;
static int          barracksSlot[AI_MAX_BARRACKS];     // our Barracks: slots...
static unsigned int barracksSerial[AI_MAX_BARRACKS];   // ...and serials
static int          barracksCount = 0;
static int          rangeSlot = -1;         // our Archery Range: slot...
static unsigned int rangeSerial;            // ...and serial
static bool         savingForRange = false;

// Expansion in progress (one at a time).
static bool         expanding = false, savingForExpansion = false;
static int          expSite, expBuilder;
static unsigned int expSiteSerial, expBuilderSerial, expNodeSerial;
static unsigned int failedNodes[AI_MAX_FAILED_NODES];   // serials of nodes it gave up on
static int          failedCount = 0;

// Filled every think.
static int  homeRegion;                     // the walkable area our base stands in
static int  gatherers[MAX_GOLD_NODES];      // our workers mining each node
static int  workerCount, workerTarget, baseCount;
static char barracksNote[64], rangeNote[64], workerNote[64], expandNote[96];

// Per-think cache: grid cell -> nearest player unit found from it.
static int          cellTarget[GRID_W*GRID_H];
static unsigned int cellStamp[GRID_W*GRID_H];
static unsigned int thinkStamp = 0;

static void Note(char *note, int size, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(note, size, fmt, args);
    va_end(args);
}

// --- What do we own? ----------------------------------------------------------------

static bool IsOurDropOff(int b)
{
    const Building *bd = &buildings[b];
    return bd->active && bd->team == AI_TEAM && !bd->constructing && BUILDING_STATS[bd->type].dropOff;
}

// The building we organise around: the starting base, or any drop-off left.
static int AnchorBase(void)
{
    if (BuildingIsAlive(aiBase, aiBaseSerial)) return aiBase;
    for (int b = 0; b < MAX_BUILDINGS; b++) if (IsOurDropOff(b)) return b;
    return -1;
}

// Nearest finished drop-off within `maxDist` of a point, or -1.
static int NearestDropOff(Vector2 p, float maxDist)
{
    int best = -1;
    float bestDist = maxDist;
    for (int b = 0; b < MAX_BUILDINGS; b++)
    {
        if (!IsOurDropOff(b)) continue;
        float d = BuildingDistance(b, p);
        if (d <= bestDist) { bestDist = d; best = b; }
    }
    return best;
}

static bool NodeUsable(int n)
{
    return goldNodes[n].active && goldNodes[n].amount > 0 && PathRegion(goldNodes[n].pos) == homeRegion;
}

// The base a node belongs to: the nearest drop-off within AI_NODE_RANGE_TILES.
static int NodeBase(int n)
{
    return NearestDropOff(goldNodes[n].pos, AI_NODE_RANGE_TILES*TILE_SIZE);
}

static bool NodeFailed(int n)
{
    for (int i = 0; i < failedCount; i++) if (failedNodes[i] == goldNodes[n].serial) return true;
    return false;
}

static void ForgetNode(unsigned int serial)
{
    if (failedCount < AI_MAX_FAILED_NODES) failedNodes[failedCount++] = serial;
}

// Rally a base's new units toward the gold node nearest to it.
static void RallyTowardNode(int base)
{
    int node = EconomyNearestNode(BuildingCentre(base), AI_NODE_RANGE_TILES*TILE_SIZE);
    if (node != -1) BuildingSetRally(base, Vector2Lerp(BuildingCentre(base), goldNodes[node].pos, 0.6f));
}

// --- 1. Barracks --------------------------------------------------------------------

// Forget Barracks that were destroyed (slot reused or empty).
static void PruneBarracks(void)
{
    int kept = 0;
    for (int k = 0; k < barracksCount; k++)
    {
        if (!BuildingIsAlive(barracksSlot[k], barracksSerial[k])) continue;
        barracksSlot[kept] = barracksSlot[k];
        barracksSerial[kept] = barracksSerial[k];
        kept++;
    }
    barracksCount = kept;
}

static int UnfinishedBarracks(void)
{
    for (int k = 0; k < barracksCount; k++) if (buildings[barracksSlot[k]].constructing) return barracksSlot[k];
    return -1;
}

static bool AllBarracksFull(void)
{
    for (int k = 0; k < barracksCount; k++) if (buildings[barracksSlot[k]].queueCount < AI_BARRACKS_QUEUE) return false;
    return true;
}

// Our workers: how many, whether one is building `site` (-1 = none), and a
// worker that's free to build something (-1 if none).
static int ScanBuilders(int site, bool *beingBuilt, int *freeWorker)
{
    int workers = 0;
    *beingBuilt = false;
    *freeWorker = -1;
    for (int i = 0; i < MAX_UNITS; i++)
    {
        const Unit *u = &units[i];
        if (!u->active || u->team != AI_TEAM || u->type != UNIT_WORKER) continue;
        workers++;
        if (u->buildOrder && site != -1 && u->buildSite == site) *beingBuilt = true;
        else if (*freeWorker == -1 && !u->buildOrder) *freeWorker = i;
    }
    return workers;
}

// Pay for a building near the anchor base and send one worker to build it.
// Returns the site, or -1 (no room, or no gold).
static int StartSite(BuildingType type, int worker, int anchor)
{
    int cost = BUILDING_STATS[type].cost;
    Vector2 spot;
    if (!BuildingsFindSpot(type, BuildingCentre(anchor), &spot)) return -1;
    if (!EconomySpend(AI_TEAM, cost)) return -1;
    int site = BuildingPlace(type, AI_TEAM, spot, true);
    if (site == -1) { EconomyAdd(AI_TEAM, cost); return -1; }
    BuildingsOrderConstruct(&worker, 1, site);
    return site;
}

static void StartBarracks(int worker, int anchor)
{
    int site = StartSite(BUILDING_BARRACKS, worker, anchor);
    if (site == -1) return;
    barracksSlot[barracksCount] = site;
    barracksSerial[barracksCount] = buildings[site].serial;
    barracksCount++;
    Note(barracksNote, sizeof(barracksNote), "Building a Barracks (%d of max %d)", barracksCount, AI_MAX_BARRACKS);
}

static void BarracksTick(void)
{
    barracksNote[0] = '\0';
    PruneBarracks();
    int anchor = AnchorBase();
    if (anchor == -1) return;

    int site = UnfinishedBarracks();
    bool beingBuilt;
    int freeWorker;
    int workers = ScanBuilders(site, &beingBuilt, &freeWorker);

    if (site != -1)   // one is being built: make sure somebody is on it
    {
        Note(barracksNote, sizeof(barracksNote), "Building a Barracks (%d%%)", (int)(BuildingBuildProgress(site)*100));
        if (!beingBuilt && freeWorker != -1) BuildingsOrderConstruct(&freeWorker, 1, site);
        return;
    }
    if (freeWorker == -1) return;

    int cost = BUILDING_STATS[BUILDING_BARRACKS].cost;
    if (barracksCount == 0)   // the first one
    {
        if (workers < AI_BARRACKS_WORKERS) { Note(barracksNote, sizeof(barracksNote), "Needs %d workers for a Barracks", AI_BARRACKS_WORKERS); return; }
        if (EconomyGold(AI_TEAM) < cost) { Note(barracksNote, sizeof(barracksNote), "Saving for a Barracks (%d/%d)", EconomyGold(AI_TEAM), cost); return; }
        StartBarracks(freeWorker, anchor);
    }
    else if (barracksCount < AI_MAX_BARRACKS && EconomyGold(AI_TEAM) > AI_EXTRA_BARRACKS_GOLD && AllBarracksFull())
    {
        StartBarracks(freeWorker, anchor);   // gold is piling up faster than they can spend it
    }
}

// --- 1b. Archery Range --------------------------------------------------------------

static bool HaveFinishedBarracks(void)
{
    for (int k = 0; k < barracksCount; k++) if (!buildings[barracksSlot[k]].constructing) return true;
    return false;
}

static void RangeTick(void)
{
    rangeNote[0] = '\0';
    savingForRange = false;
    bool alive = BuildingIsAlive(rangeSlot, rangeSerial);
    if (alive && !buildings[rangeSlot].constructing) return;   // have one
    if (!alive && !HaveFinishedBarracks()) return;              // not yet: the Barracks comes first
    int anchor = AnchorBase();
    if (anchor == -1) return;

    bool beingBuilt;
    int freeWorker;
    ScanBuilders(alive ? rangeSlot : -1, &beingBuilt, &freeWorker);
    if (alive)   // being built: make sure somebody is on it
    {
        Note(rangeNote, sizeof(rangeNote), "Building an Archery Range (%d%%)", (int)(BuildingBuildProgress(rangeSlot)*100));
        if (!beingBuilt && freeWorker != -1) BuildingsOrderConstruct(&freeWorker, 1, rangeSlot);
        return;
    }
    if (freeWorker == -1) return;

    int cost = BUILDING_STATS[BUILDING_ARCHERY_RANGE].cost;
    if (EconomyGold(AI_TEAM) < cost)
    {
        savingForRange = true;
        Note(rangeNote, sizeof(rangeNote), "Saving for an Archery Range (%d/%d)", EconomyGold(AI_TEAM), cost);
        return;
    }
    int site = StartSite(BUILDING_ARCHERY_RANGE, freeWorker, anchor);
    if (site == -1) return;
    rangeSlot = site;
    rangeSerial = buildings[site].serial;
    Note(rangeNote, sizeof(rangeNote), "Building an Archery Range");
}

// --- 2. Workers -----------------------------------------------------------------------

static int QueuedWorkers(int b)
{
    int n = 0;
    for (int q = 0; q < buildings[b].queueCount; q++) n += (buildings[b].queue[q] == UNIT_WORKER);
    return n;
}

static void WorkerTick(void)
{
    workerNote[0] = '\0';
    // Who works where: a worker belongs to the base of the node it mines (or,
    // if it isn't mining, the drop-off nearest to it).
    int homeCount[MAX_BUILDINGS] = { 0 };
    int workers = 0;
    for (int n = 0; n < MAX_GOLD_NODES; n++) gatherers[n] = 0;
    for (int i = 0; i < MAX_UNITS; i++)
    {
        const Unit *u = &units[i];
        if (!u->active || u->team != AI_TEAM || u->type != UNIT_WORKER) continue;
        workers++;
        bool mining = (u->gatherState != GATHER_NONE && EconomyNodeIsAlive(u->gatherNode, u->gatherNodeSerial));
        if (mining) gatherers[u->gatherNode]++;
        int home = mining ? NodeBase(u->gatherNode) : NearestDropOff(u->pos, (float)(MAP_PIXEL_W + MAP_PIXEL_H));
        if (home != -1) homeCount[home]++;
    }

    // Each base's target, and the base furthest below it.
    int best = -1, bestNeed = 0, queued = 0;
    workerTarget = 0;
    for (int b = 0; b < MAX_BUILDINGS; b++)
    {
        if (!IsOurDropOff(b)) continue;
        int nodes = 0;
        for (int n = 0; n < MAX_GOLD_NODES; n++) nodes += (NodeUsable(n) && NodeBase(n) == b);
        int want = nodes*AI_WORKERS_PER_NODE;
        if (want > AI_MAX_WORKERS_PER_BASE) want = AI_MAX_WORKERS_PER_BASE;
        int inQueue = QueuedWorkers(b);
        int need = want - homeCount[b] - inQueue;
        workerTarget += want;
        queued += inQueue;
        if (need > bestNeed && buildings[b].queueCount < AI_WORKER_QUEUE) { bestNeed = need; best = b; }
    }
    workerCount = workers + queued;

    bool waitingForBarracks = (barracksCount == 0) && workers >= AI_BARRACKS_WORKERS;
    if (best == -1)
    {
        if (workerTarget == 0) Note(workerNote, sizeof(workerNote), "No gold left near our bases");
        else if (workerCount >= workerTarget) Note(workerNote, sizeof(workerNote), "Workers saturated (%d/%d)", workerCount, workerTarget);
        return;
    }
    if (waitingForBarracks) return;   // the Barracks gets the gold first
    if (BuildingQueueTrain(best, UNIT_WORKER)) Note(workerNote, sizeof(workerNote), "Training workers (%d/%d)", workerCount + 1, workerTarget);
    else Note(workerNote, sizeof(workerNote), "Saving for a worker (%d/%d)", workerCount, workerTarget);
}

// --- 3. Expansion ---------------------------------------------------------------------

static int CountOurBases(void)
{
    int n = 0;
    for (int b = 0; b < MAX_BUILDINGS; b++) n += (buildings[b].active && buildings[b].team == AI_TEAM && buildings[b].type == BUILDING_BASE);
    return n;
}

// Gold left in the nodes our bases are mining.
static int OwnGoldLeft(void)
{
    int total = 0;
    for (int n = 0; n < MAX_GOLD_NODES; n++) if (NodeUsable(n) && NodeBase(n) != -1) total += goldNodes[n].amount;
    return total;
}

// A node worth a new base: (a) far from our drop-offs, (b) rich enough,
// (c) reachable, (d) not next to an enemy building. Nearest one wins.
static int FindExpansionNode(Vector2 from)
{
    int best = -1;
    float bestDist = 0.0f;
    for (int n = 0; n < MAX_GOLD_NODES; n++)
    {
        const GoldNode *g = &goldNodes[n];
        if (!g->active || NodeFailed(n)) continue;
        if (NearestDropOff(g->pos, AI_EXPAND_MIN_TILES*TILE_SIZE) != -1) continue;                      // (a)
        if (g->amount < AI_EXPAND_MIN_GOLD) continue;                                                  // (b)
        if (PathRegion(g->pos) != homeRegion) continue;                                                // (c)
        if (BuildingsFindNearestEnemy(g->pos, AI_EXPAND_ENEMY_TILES*TILE_SIZE, AI_TEAM) != -1) continue; // (d)
        float d = Vector2Distance(from, g->pos);
        if (best == -1 || d < bestDist) { bestDist = d; best = n; }
    }
    return best;
}

// Check on the expansion being built: finished, destroyed, or builder lost.
static void WatchExpansion(void)
{
    int cost = BUILDING_STATS[BUILDING_BASE].cost;

    if (!BuildingIsAlive(expSite, expSiteSerial))   // destroyed by the player
    {
        expanding = false;
        ForgetNode(expNodeSerial);
        Note(expandNote, sizeof(expandNote), "Lost an expansion");
        return;
    }
    if (!buildings[expSite].constructing)   // finished
    {
        expanding = false;
        RallyTowardNode(expSite);
        Note(expandNote, sizeof(expandNote), "Expansion finished");
        return;
    }
    bool builderOk = UnitIsAlive(expBuilder, expBuilderSerial) && units[expBuilder].buildOrder && units[expBuilder].buildSite == expSite;
    if (!builderOk)   // the builder died (or was pulled away): give up on this spot
    {
        BuildingDestroy(expSite);
        EconomyAdd(AI_TEAM, cost);   // our own cancellation: refunded
        ForgetNode(expNodeSerial);
        expanding = false;
        Note(expandNote, sizeof(expandNote), "Gave up an expansion (builder lost)");
        return;
    }
    Note(expandNote, sizeof(expandNote), "Building an expansion (%d%%)", (int)(BuildingBuildProgress(expSite)*100));
}

static void ExpandTick(void)
{
    expandNote[0] = '\0';
    savingForExpansion = false;
    if (expanding) { WatchExpansion(); return; }   // this check is spent watching it (or giving up)

    int anchor = AnchorBase();
    if (anchor == -1 || CountOurBases() >= AI_MAX_BASES) return;
    int node = FindExpansionNode(BuildingCentre(anchor));
    if (node == -1) return;   // nothing qualifies: don't expand

    int cost = BUILDING_STATS[BUILDING_BASE].cost;
    bool runningLow = OwnGoldLeft() < AI_EXPAND_LOW_GOLD;
    int needed = cost + (runningLow ? 0 : AI_EXPAND_RESERVE);
    if (EconomyGold(AI_TEAM) < needed)
    {
        // Only hold back the army for it when this base can't grow any more
        // (workers saturated) or its gold is running out; otherwise expand
        // whenever spare gold happens to be there.
        bool saturated = (workerCount >= workerTarget);
        if (saturated || runningLow)
        {
            savingForExpansion = AI_SAVE_FOR_EXPANSION;
            Note(expandNote, sizeof(expandNote), "Saving for an expansion (%d/%d)", EconomyGold(AI_TEAM), needed);
        }
        return;
    }

    Vector2 spot;
    if (!BuildingsFindSpot(BUILDING_BASE, goldNodes[node].pos, &spot) || PathRegion(spot) != homeRegion)
    {
        ForgetNode(goldNodes[node].serial);   // no room there
        return;
    }

    // The nearest worker that isn't building something.
    int builder = -1;
    float bestDist = 0.0f;
    for (int i = 0; i < MAX_UNITS; i++)
    {
        const Unit *u = &units[i];
        if (!u->active || u->team != AI_TEAM || u->type != UNIT_WORKER || u->buildOrder) continue;
        float d = Vector2Distance(u->pos, spot);
        if (builder == -1 || d < bestDist) { bestDist = d; builder = i; }
    }
    if (builder == -1 || !EconomySpend(AI_TEAM, cost)) return;
    int site = BuildingPlace(BUILDING_BASE, AI_TEAM, spot, true);
    if (site == -1) { EconomyAdd(AI_TEAM, cost); return; }
    BuildingsOrderConstruct(&builder, 1, site);

    expanding = true;
    expSite = site;               expSiteSerial = buildings[site].serial;
    expBuilder = builder;         expBuilderSerial = units[builder].serial;
    expNodeSerial = goldNodes[node].serial;
    Note(expandNote, sizeof(expandNote), "Expanding to gold at %d,%d", (int)(goldNodes[node].pos.x/TILE_SIZE), (int)(goldNodes[node].pos.y/TILE_SIZE));
}

// --- Combat units -----------------------------------------------------------------------

// Queue one unit at a finished building with room. False if out of gold.
static bool TrainAt(int b, unsigned int serial, UnitType type)
{
    if (!BuildingIsAlive(b, serial) || buildings[b].constructing || buildings[b].queueCount >= AI_BARRACKS_QUEUE) return true;
    return BuildingQueueTrain(b, type);
}

static void TrainTick(void)
{
    if (--trainCountdown > 0) return;
    trainCountdown = AI_TRAIN_TICKS;
    if (savingForExpansion || savingForRange) return;   // gold is going into a building
    if (!TrainAt(rangeSlot, rangeSerial, UNIT_ARCHER)) return;   // out of gold
    for (int k = 0; k < barracksCount; k++)   // each Barracks with room gets one unit
        if (!TrainAt(barracksSlot[k], barracksSerial[k], UNIT_MELEE)) return;
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

// --- 4. Idle workers ---------------------------------------------------------------------

// The node near the worker's base with the fewest workers (spreads them out);
// failing that, the nearest reachable node anywhere.
static int PickNode(const Unit *u)
{
    int home = NearestDropOff(u->pos, (float)(MAP_PIXEL_W + MAP_PIXEL_H));
    int best = -1;
    float bestScore = 0.0f;
    for (int n = 0; n < MAX_GOLD_NODES; n++)
    {
        if (!NodeUsable(n)) continue;
        bool local = (home != -1 && NodeBase(n) == home);
        float score = (local ? 0.0f : 1000000.0f) + gatherers[n]*10000.0f + Vector2Distance(u->pos, goldNodes[n].pos);
        if (best == -1 || score < bestScore) { bestScore = score; best = n; }
    }
    return best;
}

void AiInit(Vector2 base, Vector2 spawn, int baseBuilding)
{
    playerBase = base;
    aiSpawn = spawn;
    aiBase = baseBuilding;
    aiBaseSerial = (baseBuilding >= 0) ? buildings[baseBuilding].serial : 0;
    thinkCountdown = AI_THINK_TICKS;
    trainCountdown = AI_TRAIN_TICKS;
    expandCountdown = AI_EXPAND_CHECK_TICKS;
    barracksCount = 0;
    rangeSlot = -1;
    savingForRange = false;
    expanding = savingForExpansion = false;
    failedCount = 0;
    workerCount = workerTarget = baseCount = 0;
    barracksNote[0] = rangeNote[0] = workerNote[0] = expandNote[0] = '\0';
    if (baseBuilding >= 0) RallyTowardNode(baseBuilding);
}

void AiTick(void)
{
    TrainTick();
    if (--thinkCountdown > 0) return;
    thinkCountdown = AI_THINK_TICKS;
    thinkStamp++;

    PathComputeRegions();   // buildings may have changed what's reachable
    int anchor = AnchorBase();
    homeRegion = (anchor != -1) ? PathRegion(buildings[anchor].rally) : 0;
    baseCount = CountOurBases();

    BarracksTick();
    RangeTick();
    WorkerTick();
    expandCountdown -= AI_THINK_TICKS;
    if (expandCountdown <= 0) { expandCountdown = AI_EXPAND_CHECK_TICKS; ExpandTick(); }

    // Scanning the pool every 2 s to find our idle units is cheap; it isn't a
    // "who's nearby" search, those go through the grid.
    static int toBase[MAX_UNITS];
    int toBaseCount = 0;
    for (int i = 0; i < MAX_UNITS; i++)
    {
        Unit *u = &units[i];
        if (!u->active || u->team != AI_TEAM || u->moving || u->attacking || u->gatherState != GATHER_NONE || u->buildOrder) continue;

        if (u->type == UNIT_WORKER)
        {
            int node = PickNode(u);
            if (node != -1) { EconomyOrderGather(&i, 1, node); gatherers[node]++; }
            continue;
        }

        int target = NearestPlayerUnit(u->pos);
        int building = (target == -1) ? BuildingsFindNearestEnemy(u->pos, (float)(MAP_PIXEL_W + MAP_PIXEL_H), AI_TEAM) : -1;
        if (target != -1) UnitsOrderAttack(&i, 1, target);
        else if (building != -1) UnitsOrderAttackBuilding(&i, 1, building);
        else toBase[toBaseCount++] = i;
    }
    UnitsOrderMove(toBase, toBaseCount, playerBase);
}

void AiSpawnWave(int count)
{
    static Vector2 spots[MAX_UNITS];
    if (count > MAX_UNITS) count = MAX_UNITS;
    int found = UnitsOpenSpots(aiSpawn, count, spots);
    for (int k = 0; k < found; k++) UnitSpawn(spots[k], (k % 2) ? UNIT_ARCHER : UNIT_MELEE, AI_TEAM);
}

const char *AiDebugLine(void)
{
    return TextFormat("AI gold %d  workers %d/%d  bases %d  barracks %d  range %d", EconomyGold(AI_TEAM), workerCount, workerTarget, baseCount, barracksCount,
                      BuildingIsAlive(rangeSlot, rangeSerial) ? 1 : 0);
}

const char *AiStatus(void)
{
    if (expandNote[0]) return expandNote;
    if (barracksNote[0]) return barracksNote;
    if (rangeNote[0]) return rangeNote;
    if (workerNote[0]) return workerNote;
    return "Training army";
}
