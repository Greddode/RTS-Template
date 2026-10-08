// ai.c - A simple computer opponent.
//
// Every AI_THINK_TICKS (2 s) the AI:
//   1. Barracks: once it has AI_BARRACKS_WORKERS workers and the gold, one
//      worker builds a Barracks near its base (another worker takes over if
//      the builder dies; rebuilt if destroyed). If gold piles up past
//      AI_EXTRA_BARRACKS_GOLD while every Barracks has a full queue, it builds
//      another, up to AI_MAX_BARRACKS.
//   1b. Tech: once a Barracks is finished, it builds one of each building
//      in AI_TECH_ORDER (Archery Range, then Academy), each after the one
//      before is finished, rebuilt if destroyed. Combat training waits while
//      it saves up for the next one. Ones a map file gave it are used too.
//   2. Workers: each finished drop-off base wants AI_WORKERS_PER_NODE workers
//      per reachable gold node near it (at most AI_MAX_WORKERS_PER_BASE). The
//      base that's furthest below its target trains one, if the AI can pay.
//      Once every base is saturated it stops, so gold goes to the army.
//      (While it's still waiting for a Barracks, the Barracks comes first.)
//   3. Expansion (every AI_EXPAND_CHECK_TICKS): look for a gold FIELD (a node
//      plus the nodes within AI_FIELD_TILES of it, like a StarCraft mineral
//      field) with enough gold that no Base of either side is near yet, that
//      it can reach and that isn't next to an enemy building; the nearest
//      wins. The new Base goes where it's closest to the whole field, with
//      AI_BASE_GOLD_GAP tiles of open ground left for the workers. With the
//      base's cost plus a reserve (or just the cost when its own nodes run
//      low), one worker builds it. Once its
//      workers are saturated (or its gold runs low) it also saves up for it,
//      pausing combat training (AI_SAVE_FOR_EXPANSION). One expansion
//      at a time; if the builder dies, the site is cancelled (refunded) and
//      that field isn't tried again. The new base's rally point faces its gold.
//   4. Idle units: workers go to the gold node near their base with the
//      fewest workers on it; combat units attack the nearest player unit (or
//      building, or march on the player's base).
// Separately, every AI_TRAIN_TICKS it queues combat units: each time the type
// furthest below its share in AI_ARMY_MIX (army alive + queued), at a building
// that trains it and has room, until the queues are full or the gold runs out
// (then it saves for that type). Types whose `requires` building it lacks are
// skipped (UnitsCanTrain).
// Air defence: while it sees fewer than AI_ANTI_AIR_PER_FLYER units that can
// hit air (alive + queued) per player flyer, it only trains types with
// hitsAir, even while saving for a building (and keeps its gold for them while
// their buildings are busy; with no such building it trains the normal mix). It never trains flyers itself
// (they aren't in AI_ARMY_MIX), and idle units are only sent after player
// units they can hit.
//
// "Reachable" uses PathRegion(): a flood fill of the walkable tiles, redone
// every think, so it answers "could pathfinding get there?" instantly.
// All numbers are in config.h. No pools of its own: it keeps a few slot +
// serial pairs (Barracks, expansion site, builder, node) and fixed arrays.

#include "ai.h"
#include "config.h"
#include "buildings.h"
#include "economy.h"
#include "fog.h"
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
static int          techSlot[AI_TECH_COUNT];     // our tech buildings (AI_TECH_ORDER): slots...
static unsigned int techSerial[AI_TECH_COUNT];   // ...and serials
static bool         savingForTech = false;

// Expansion in progress (one at a time).
static bool         expanding = false, savingForExpansion = false;
static int          expSite, expBuilder;
static unsigned int expSiteSerial, expBuilderSerial;
static unsigned int expField[MAX_GOLD_NODES];   // serials of the field's nodes (forgotten if it fails)
static int          expFieldCount = 0;
static unsigned int failedNodes[AI_MAX_FAILED_NODES];   // serials of nodes it gave up on
static int          failedCount = 0;

// Filled every think.
static int  homeRegion;                     // the walkable area our base stands in
static int  gatherers[MAX_GOLD_NODES];      // our workers mining each node
static int  workerCount, workerTarget, baseCount;
static char barracksNote[64], techNote[64], workerNote[64], expandNote[96];

// Per-think cache: grid cell -> nearest player unit found from it, one answer
// per "what can it hit" (ground / air / both). Every answer for a cell is
// searched from the same point (the first unit that asked), so units that hit
// the same things get the same target, whatever their type.
static Vector2      cellFrom[GRID_W*GRID_H];
static unsigned int cellStamp[GRID_W*GRID_H];
static int          cellTarget[4][GRID_W*GRID_H];      // [hitsGround + 2*hitsAir]
static unsigned int cellTargetStamp[4][GRID_W*GRID_H];
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
    return goldNodes[n].active && goldNodes[n].amount > 0 && PathRegion(MOVE_GROUND, goldNodes[n].pos) == homeRegion;
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
    if (!BuildingsCanBuild(AI_TEAM, type)) return -1;   // prerequisite not met
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

// --- 1b. Tech buildings --------------------------------------------------------------

static bool HaveFinishedBarracks(void)
{
    for (int k = 0; k < barracksCount; k++) if (!buildings[barracksSlot[k]].constructing) return true;
    return false;
}

// One of our buildings of this type (a finished one if there is one), or -1.
static int FindOwn(BuildingType type)
{
    int found = -1;
    for (int b = 0; b < MAX_BUILDINGS; b++)
    {
        const Building *bd = &buildings[b];
        if (!bd->active || bd->team != AI_TEAM || bd->type != type) continue;
        if (!bd->constructing) return b;
        found = b;
    }
    return found;
}

static void TechTick(void)
{
    techNote[0] = '\0';
    savingForTech = false;
    if (!HaveFinishedBarracks()) return;   // the Barracks comes first
    int anchor = AnchorBase();
    if (anchor == -1) return;

    for (int k = 0; k < AI_TECH_COUNT; k++)
    {
        BuildingType type = AI_TECH_ORDER[k];
        const char *name = BUILDING_STATS[type].name;
        if (!BuildingIsAlive(techSlot[k], techSerial[k]))   // lost, or never had one: maybe we own one anyway
        {
            techSlot[k] = FindOwn(type);
            if (techSlot[k] != -1) techSerial[k] = buildings[techSlot[k]].serial;
        }
        bool alive = BuildingIsAlive(techSlot[k], techSerial[k]);
        if (alive && !buildings[techSlot[k]].constructing) continue;   // have it: next one

        bool beingBuilt;
        int freeWorker;
        ScanBuilders(alive ? techSlot[k] : -1, &beingBuilt, &freeWorker);
        if (alive)   // being built: make sure somebody is on it
        {
            Note(techNote, sizeof(techNote), "Building %s (%d%%)", name, (int)(BuildingBuildProgress(techSlot[k])*100));
            if (!beingBuilt && freeWorker != -1) BuildingsOrderConstruct(&freeWorker, 1, techSlot[k]);
            return;
        }
        if (freeWorker == -1 || !BuildingsCanBuild(AI_TEAM, type)) return;

        int cost = BUILDING_STATS[type].cost;
        if (EconomyGold(AI_TEAM) < cost)
        {
            savingForTech = true;
            Note(techNote, sizeof(techNote), "Saving for %s (%d/%d)", name, EconomyGold(AI_TEAM), cost);
            return;
        }
        int site = StartSite(type, freeWorker, anchor);
        if (site == -1) return;
        techSlot[k] = site;
        techSerial[k] = buildings[site].serial;
        Note(techNote, sizeof(techNote), "Building %s", name);
        return;   // one at a time
    }
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

// --- Gold fields -----------------------------------------------------------------------

typedef struct GoldField {
    int     nodes[MAX_GOLD_NODES];
    int     count;
    int     gold;     // total gold left
    Vector2 centre;   // average position of its nodes
} GoldField;

// Free: has gold, wasn't given up on, and no Base (either side, finished or
// not) is near it yet. Worked out once per search by MarkFreeNodes().
static bool nodeFree[MAX_GOLD_NODES];

static void MarkFreeNodes(void)
{
    for (int n = 0; n < MAX_GOLD_NODES; n++)
    {
        nodeFree[n] = goldNodes[n].active && goldNodes[n].amount > 0 && !NodeFailed(n);
        for (int b = 0; b < MAX_BUILDINGS && nodeFree[n]; b++)
            if (buildings[b].active && buildings[b].type == BUILDING_BASE &&
                BuildingDistance(b, goldNodes[n].pos) <= AI_CLAIMED_TILES*TILE_SIZE) nodeFree[n] = false;
    }
}

static bool NodeFree(int n)
{
    return nodeFree[n];
}

// The field around node `seed`: it plus every free node within AI_FIELD_TILES of it.
static void FieldAround(int seed, GoldField *f)
{
    f->count = 0; f->gold = 0; f->centre = (Vector2){ 0 };
    for (int n = 0; n < MAX_GOLD_NODES; n++)
    {
        if (!NodeFree(n) || Vector2Distance(goldNodes[n].pos, goldNodes[seed].pos) > AI_FIELD_TILES*TILE_SIZE) continue;
        f->nodes[f->count++] = n;
        f->gold += goldNodes[n].amount;
        f->centre = Vector2Add(f->centre, goldNodes[n].pos);
    }
    if (f->count > 0) f->centre = Vector2Scale(f->centre, 1.0f/f->count);
}

// Is the field around node `seed` worth a Base? Enough free gold, reachable,
// no enemy building near.
static bool FieldWorthIt(int seed, GoldField *f)
{
    if (!NodeFree(seed) || PathRegion(MOVE_GROUND, goldNodes[seed].pos) != homeRegion) return false;
    FieldAround(seed, f);
    return f->gold >= AI_EXPAND_MIN_GOLD && BuildingsFindNearestEnemy(f->centre, AI_EXPAND_ENEMY_TILES*TILE_SIZE, AI_TEAM) == -1;
}

// The nearest field worth a Base. Each node gives a field (it and its
// neighbours), so one cluster of nodes gives several overlapping ones: of
// those near the nearest, take the one holding the most gold.
static bool FindExpansionField(Vector2 from, GoldField *best)
{
    static GoldField f;
    MarkFreeNodes();
    int nearest = -1;
    float nearestDist = 0.0f;
    for (int n = 0; n < MAX_GOLD_NODES; n++)
    {
        if (!FieldWorthIt(n, &f)) continue;
        float d = Vector2Distance(from, f.centre);
        if (nearest == -1 || d < nearestDist) { nearest = n; nearestDist = d; *best = f; }
    }
    if (nearest == -1) return false;
    Vector2 around = best->centre;
    for (int n = 0; n < MAX_GOLD_NODES; n++)
    {
        if (Vector2Distance(goldNodes[n].pos, around) > AI_FIELD_TILES*TILE_SIZE || !FieldWorthIt(n, &f)) continue;
        if (f.gold > best->gold) *best = f;
    }
    return true;
}

// Where a Base serves this field best: on open, reachable ground, at least
// AI_BASE_GOLD_GAP tiles from any gold, as close as possible to all the
// field's nodes (smallest total distance), keeping each within
// AI_NODE_RANGE_TILES when it can (so they all count as this base's gold).
static bool FieldBaseSpot(const GoldField *f, Vector2 *out)
{
    int cx = (int)(f->centre.x/TILE_SIZE), cy = (int)(f->centre.y/TILE_SIZE), r = AI_FIELD_TILES + 4;
    float bestScore = 0.0f;
    bool found = false, bestAllInRange = false;
    for (int ty = cy - r; ty <= cy + r; ty++)
        for (int tx = cx - r; tx <= cx + r; tx++)
        {
            Vector2 p = { (tx + 0.5f)*TILE_SIZE, (ty + 0.5f)*TILE_SIZE };
            if (!BuildingCanPlace(BUILDING_BASE, p) || PathRegion(MOVE_GROUND, p) != homeRegion) continue;
            Rectangle rect = BuildingFootprint(BUILDING_BASE, p);
            bool tooClose = false;
            for (int n = 0; n < MAX_GOLD_NODES && !tooClose; n++)
            {
                if (!goldNodes[n].active) continue;
                Vector2 q = { Clamp(goldNodes[n].pos.x, rect.x, rect.x + rect.width), Clamp(goldNodes[n].pos.y, rect.y, rect.y + rect.height) };
                tooClose = Vector2Distance(q, goldNodes[n].pos) < (AI_BASE_GOLD_GAP + 0.5f)*TILE_SIZE;
            }
            if (tooClose) continue;
            float score = 0.0f;
            bool allInRange = true;
            for (int k = 0; k < f->count; k++)
            {
                Vector2 g = goldNodes[f->nodes[k]].pos;
                Vector2 q = { Clamp(g.x, rect.x, rect.x + rect.width), Clamp(g.y, rect.y, rect.y + rect.height) };
                float d = Vector2Distance(q, g);
                score += d;
                if (d > AI_NODE_RANGE_TILES*TILE_SIZE) allInRange = false;
            }
            bool better = !found || (allInRange && !bestAllInRange) || (allInRange == bestAllInRange && score < bestScore);
            if (better) { found = true; bestScore = score; bestAllInRange = allInRange; *out = p; }
        }
    return found;
}

static void ForgetField(void)
{
    for (int k = 0; k < expFieldCount; k++) ForgetNode(expField[k]);
    expFieldCount = 0;
}

// Check on the expansion being built: finished, destroyed, or builder lost.
static void WatchExpansion(void)
{
    int cost = BUILDING_STATS[BUILDING_BASE].cost;

    if (!BuildingIsAlive(expSite, expSiteSerial))   // destroyed by the player
    {
        expanding = false;
        ForgetField();
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
        ForgetField();
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
    static GoldField field;
    if (!FindExpansionField(BuildingCentre(anchor), &field)) return;   // nothing qualifies: don't expand

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
    expFieldCount = 0;
    for (int k = 0; k < field.count; k++) expField[expFieldCount++] = goldNodes[field.nodes[k]].serial;
    if (!FieldBaseSpot(&field, &spot))
    {
        ForgetField();   // no room for a Base there
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
    Note(expandNote, sizeof(expandNote), "Expanding to a gold field at %d,%d (%d nodes, %d gold)",
         (int)(field.centre.x/TILE_SIZE), (int)(field.centre.y/TILE_SIZE), field.count, field.gold);
}

// --- Combat units -----------------------------------------------------------------------

// One of our finished buildings of this type with room in its queue, or -1.
static int ProductionBuilding(BuildingType type)
{
    for (int b = 0; b < MAX_BUILDINGS; b++)
    {
        const Building *bd = &buildings[b];
        if (bd->active && bd->team == AI_TEAM && bd->type == type && !bd->constructing && bd->queueCount < AI_BARRACKS_QUEUE) return b;
    }
    return -1;
}

static bool HitsAir(int type) { return UNIT_STATS[type].hitsAir && UNIT_STATS[type].damage > 0.0f; }

// Can we train any AI_ARMY_MIX type that hits air at all (a finished building
// for it, busy or not, and its `requires` met)?
static bool CanTrainAntiAir(void)
{
    for (int m = 0; m < AI_ARMY_MIX_COUNT; m++)
    {
        UnitType t = AI_ARMY_MIX[m].type;
        if (!HitsAir(t) || AI_ARMY_MIX[m].share <= 0 || !UnitsCanTrain(AI_TEAM, t)) continue;
        int b = FindOwn(UNIT_STATS[t].trainedAt);
        if (b != -1 && !buildings[b].constructing) return true;
    }
    return false;
}

static void TrainTick(void)
{
    if (--trainCountdown > 0) return;
    trainCountdown = AI_TRAIN_TICKS;

    // Our army by type: alive plus queued, and the player's flyers we can see.
    // A pass over the pool every 5 s is cheap (it isn't a "who's nearby" search).
    int have[UNIT_TYPE_COUNT] = { 0 };
    int playerFlyers = 0, antiAir = 0;
    for (int i = 0; i < MAX_UNITS; i++)
    {
        const Unit *u = &units[i];
        if (!u->active) continue;
        if (u->team == AI_TEAM) have[u->type]++;
        else if (UnitIsFlying(u) && FogCanSee(AI_TEAM, u->pos)) playerFlyers++;
    }
    for (int b = 0; b < MAX_BUILDINGS; b++)
        if (buildings[b].active && buildings[b].team == AI_TEAM)
            for (int q = 0; q < buildings[b].queueCount; q++) have[buildings[b].queue[q]]++;
    for (int t = 0; t < UNIT_TYPE_COUNT; t++) if (HitsAir(t)) antiAir += have[t];
    bool wantAntiAir = antiAir < playerFlyers*AI_ANTI_AIR_PER_FLYER;

    if ((savingForExpansion || savingForTech) && !wantAntiAir) return;   // gold is going into a building

    // Again and again: the type furthest below its share in AI_ARMY_MIX (not at
    // its cap, and with a building that can take it). If it can't be paid for,
    // stop: the gold is saved for it rather than spent on something cheaper.
    for (;;)
    {
        int bestType = -1, bestBuilding = -1;
        float bestRatio = 0.0f;
        for (int m = 0; m < AI_ARMY_MIX_COUNT; m++)
        {
            const AiArmyShare *mix = &AI_ARMY_MIX[m];
            if (mix->share <= 0 || (mix->maxAlive > 0 && have[mix->type] >= mix->maxAlive)) continue;
            if (!UnitsCanTrain(AI_TEAM, mix->type)) continue;      // its `requires` building is missing
            if (wantAntiAir && !HitsAir(mix->type)) continue;     // flyers about: only what can shoot them
            int b = ProductionBuilding(UNIT_STATS[mix->type].trainedAt);
            if (b == -1) continue;
            float ratio = have[mix->type]/(float)mix->share;
            if (bestType == -1 || ratio < bestRatio) { bestType = mix->type; bestBuilding = b; bestRatio = ratio; }
        }
        if (bestType == -1 && wantAntiAir)
        {
            if (CanTrainAntiAir()) return;    // those buildings are just busy: keep the gold for them
            wantAntiAir = false;              // no way to train any: the normal mix
            continue;
        }
        if (bestType == -1) return;                                        // every building is full
        if (!BuildingQueueTrain(bestBuilding, (UnitType)bestType)) return;   // not enough gold yet
        have[bestType]++;
        if (HitsAir(bestType) && ++antiAir >= playerFlyers*AI_ANTI_AIR_PER_FLYER) wantAntiAir = false;
    }
}

// Nearest player unit a unit of `type` can hit (cached per grid cell per think).
// Healers can't hit anything; they follow the army to any enemy (as an attack-move).
static int NearestPlayerUnit(Vector2 from, UnitType type)
{
    bool healer = UNIT_STATS[type].damage <= 0.0f;
    bool ground = healer || UNIT_STATS[type].hitsGround, air = healer || UNIT_STATS[type].hitsAir;
    int cx = (int)(from.x/GRID_CELL_SIZE), cy = (int)(from.y/GRID_CELL_SIZE);
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    if (cx > GRID_W - 1) cx = GRID_W - 1;
    if (cy > GRID_H - 1) cy = GRID_H - 1;
    int cell = cy*GRID_W + cx;

    if (cellStamp[cell] != thinkStamp) { cellStamp[cell] = thinkStamp; cellFrom[cell] = from; }
    int hits = (ground ? 1 : 0) + (air ? 2 : 0);
    if (cellTargetStamp[hits][cell] != thinkStamp)
    {
        cellTargetStamp[hits][cell] = thinkStamp;
        cellTarget[hits][cell] = GridFindNearestEnemy(cellFrom[cell], (float)(MAP_PIXEL_W + MAP_PIXEL_H), AI_TEAM, ground, air);
    }
    return cellTarget[hits][cell];
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
    for (int k = 0; k < AI_TECH_COUNT; k++) techSlot[k] = -1;
    savingForTech = false;
    expanding = savingForExpansion = false;
    failedCount = 0;
    workerCount = workerTarget = baseCount = 0;
    barracksNote[0] = techNote[0] = workerNote[0] = expandNote[0] = '\0';
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
    homeRegion = (anchor != -1) ? PathRegion(MOVE_GROUND, buildings[anchor].rally) : 0;   // where our workers walk
    baseCount = CountOurBases();

    BarracksTick();
    TechTick();
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

        int target = NearestPlayerUnit(u->pos, u->type);   // only ones it can hit (no Knights sent after Falcons)
        bool hitsBuildings = UnitCanHitBuildings(u->type) || UNIT_STATS[u->type].damage <= 0.0f;   // healers go along anyway
        int building = (target == -1 && hitsBuildings) ? BuildingsFindNearestEnemy(u->pos, (float)(MAP_PIXEL_W + MAP_PIXEL_H), AI_TEAM) : -1;
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
    int found = UnitsOpenSpots(MOVE_GROUND, aiSpawn, count, spots);   // Melee and Archers: ground units
    for (int k = 0; k < found; k++) UnitSpawn(spots[k], (k % 2) ? UNIT_ARCHER : UNIT_MELEE, AI_TEAM);
}

const char *AiDebugLine(void)
{
    int tech = 0;
    for (int k = 0; k < AI_TECH_COUNT; k++) tech += BuildingIsAlive(techSlot[k], techSerial[k]) && !buildings[techSlot[k]].constructing;
    return TextFormat("AI gold %d  workers %d/%d  bases %d  barracks %d  tech %d/%d", EconomyGold(AI_TEAM), workerCount, workerTarget, baseCount, barracksCount, tech, AI_TECH_COUNT);
}

const char *AiStatus(void)
{
    if (expandNote[0]) return expandNote;
    if (barracksNote[0]) return barracksNote;
    if (techNote[0]) return techNote;
    if (workerNote[0]) return workerNote;
    return "Training army";
}
