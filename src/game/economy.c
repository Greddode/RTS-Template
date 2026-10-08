// economy.c - Gold, gold nodes and the worker mining loop.
//
// Each team has a gold counter. Gold nodes are a fixed pool; each holds a
// limited amount and disappears when empty.
//
// Workers ordered to a node run a small state machine (Unit.gatherState):
//   TO_NODE  walk to the node
//   MINING   stand there for MINE_TIME seconds, then take up to CARRY_AMOUNT
//   TO_BASE  walk to the nearest drop-off (a building with `dropOff` in
//            BUILDING_STATS, i.e. a Base) and drop the gold off,
//            then head back to the same node
// If the node runs out (or there's no base left), the worker goes idle.
// The node and base are remembered as (slot, serial), like attack targets.

#include "economy.h"
#include "buildings.h"
#include "config.h"
#include "fog.h"
#include "map.h"
#include "units.h"
#include "ui.h"
#include "raymath.h"
#include <string.h>

#define MINE_TIME     2.0f    // seconds per load
#define CARRY_AMOUNT  8       // gold per trip
#define NODE_RADIUS   10.0f   // drawn size of a full node
#define MINE_REACH    40.0f   // worker centre to node centre, close enough to mine
#define WAIT_RADIUS   90.0f   // a worker this close to a crowded node waits instead of re-pathing
#define DROP_REACH    (UNIT_RADIUS + 8.0f)   // worker centre to base wall, close enough to drop off
#define MAX_RETRIES   3       // path attempts to reach a node/base before giving up
#define NODE_COLOR    (Color){ 240, 200, 40, 255 }
#define NODE_EDGE     (Color){ 120, 90, 10, 255 }

GoldNode goldNodes[MAX_GOLD_NODES];
static unsigned int nextNodeSerial = 1;
static int gold[2];

void EconomyInit(void)
{
    memset(goldNodes, 0, sizeof(goldNodes));
    gold[PLAYER_TEAM] = START_GOLD;
    gold[AI_TEAM] = START_GOLD;
}

int EconomyGold(int team)
{
    return gold[team];
}

bool EconomySpend(int team, int amount)
{
    if (gold[team] < amount) return false;
    gold[team] -= amount;
    return true;
}

void EconomyAdd(int team, int amount)
{
    gold[team] += amount;
}

int EconomySpawnNode(Vector2 pos, int amount)
{
    for (int i = 0; i < MAX_GOLD_NODES; i++)
    {
        if (goldNodes[i].active) continue;
        goldNodes[i] = (GoldNode){ .active = true, .serial = nextNodeSerial++, .pos = pos, .amount = amount };
        return i;
    }
    return -1;
}

bool EconomyNodeIsAlive(int id, unsigned int serial)
{
    return id >= 0 && id < MAX_GOLD_NODES && goldNodes[id].active && goldNodes[id].serial == serial;
}

// The node pool is small, so scanning it is cheap.
int EconomyNodeAt(Vector2 p)
{
    for (int i = 0; i < MAX_GOLD_NODES; i++)
    {
        if (goldNodes[i].active && Vector2Distance(p, goldNodes[i].pos) <= NODE_RADIUS + 4.0f) return i;
    }
    return -1;
}

int EconomyNearestNode(Vector2 pos, float maxDist)
{
    int best = -1;
    float bestDist = maxDist;
    for (int i = 0; i < MAX_GOLD_NODES; i++)
    {
        if (!goldNodes[i].active) continue;
        float d = Vector2Distance(pos, goldNodes[i].pos);
        if (d <= bestDist) { bestDist = d; best = i; }
    }
    return best;
}

static void GoToNode(int id)
{
    Unit *u = &units[id];
    u->gatherState = GATHER_TO_NODE;
    UnitMoveTo(id, goldNodes[u->gatherNode].pos);
}

// Find the nearest own drop-off and walk to it. False if the team has none.
static bool GoToBase(int id)
{
    Unit *u = &units[id];
    int base = BuildingsFindDropOff(u->pos, u->team);
    if (base == -1) return false;
    u->gatherState = GATHER_TO_BASE;
    u->dropBase = base;
    u->dropBaseSerial = buildings[base].serial;
    UnitMoveTo(id, BuildingApproachPoint(base, u->pos, u->radius));
    return true;
}

static void StopGathering(int id)
{
    units[id].gatherState = GATHER_NONE;
    UnitStop(id);
}

void EconomyOrderGather(const int *ids, int count, int node)
{
    for (int k = 0; k < count; k++)
    {
        int id = ids[k];
        if (units[id].type != UNIT_WORKER) continue;
        UnitsOrderStop(&id, 1);   // drop any other order first
        Unit *u = &units[id];
        u->gatherNode = node;
        u->gatherNodeSerial = goldNodes[node].serial;
        u->orderRetries = MAX_RETRIES;
        GoToNode(id);
    }
}

// Walking toward a node or base: follow the path; if it ended without getting
// close enough, try again a few times before giving up.
static Vector2 Walk(int id, bool arrived, bool closeEnoughToWait)
{
    Unit *u = &units[id];
    if (u->moving) return UnitFollowPath(id);
    if (arrived || closeEnoughToWait) return (Vector2){ 0 };
    if (u->orderRetries-- > 0) UnitMoveTo(id, u->target);
    else StopGathering(id);
    return (Vector2){ 0 };
}

Vector2 EconomyWorkerTick(int id)
{
    Unit *u = &units[id];
    Vector2 none = { 0 };

    switch (u->gatherState)
    {
        case GATHER_TO_NODE:
        {
            if (!EconomyNodeIsAlive(u->gatherNode, u->gatherNodeSerial)) { StopGathering(id); return none; }
            float d = Vector2Distance(u->pos, goldNodes[u->gatherNode].pos);
            if (d <= MINE_REACH)
            {
                UnitStop(id);
                u->gatherState = GATHER_MINING;
                u->gatherTicks = (int)(MINE_TIME*TICK_RATE);
                u->orderRetries = MAX_RETRIES;
                return none;
            }
            return Walk(id, false, d <= WAIT_RADIUS);   // crowded node: wait nearby for a gap
        }

        case GATHER_MINING:
        {
            if (!EconomyNodeIsAlive(u->gatherNode, u->gatherNodeSerial)) { StopGathering(id); return none; }
            if (--u->gatherTicks > 0) return none;

            GoldNode *n = &goldNodes[u->gatherNode];
            int take = (n->amount < CARRY_AMOUNT) ? n->amount : CARRY_AMOUNT;
            n->amount -= take;
            u->carryGold += take;
            if (n->amount <= 0) n->active = false;   // depleted: every worker on it notices via the serial

            if (!GoToBase(id)) StopGathering(id);
            return none;
        }

        case GATHER_TO_BASE:
        {
            if (!BuildingIsAlive(u->dropBase, u->dropBaseSerial) && !GoToBase(id)) { StopGathering(id); return none; }
            if (BuildingDistance(u->dropBase, u->pos) <= DROP_REACH)
            {
                gold[u->team] += u->carryGold;
                u->carryGold = 0;
                u->orderRetries = MAX_RETRIES;
                if (EconomyNodeIsAlive(u->gatherNode, u->gatherNodeSerial)) GoToNode(id);
                else StopGathering(id);   // node ran out while we were away
                return none;
            }
            return Walk(id, false, false);
        }

        default:
            return none;
    }
}

void EconomyDrawNodes(Rectangle view)
{
    for (int i = 0; i < MAX_GOLD_NODES; i++)
    {
        const GoldNode *n = &goldNodes[i];
        if (!n->active || !CheckCollisionPointRec(n->pos, view)) continue;
        if (!FogExplored(PLAYER_TEAM, n->pos)) continue;   // never seen: hidden
        EconomyDrawNode(n->pos, n->amount);
    }
}

void EconomyDrawNode(Vector2 pos, int amount)
{
    float frac = amount/(float)GOLD_NODE_AMOUNT;
    if (frac > 1.0f) frac = 1.0f;   // a big map-file node doesn't get huge
    float r = NODE_RADIUS*(0.5f + 0.5f*frac);   // shrinks as it's mined
    DrawPoly(pos, 4, r + 2.0f, 45.0f, NODE_EDGE);
    DrawPoly(pos, 4, r, 45.0f, NODE_COLOR);
}

void EconomyDrawHud(int team)
{
    const char *text = TextFormat("Gold: %d", gold[team]);
    float size = Ui(22.0f);
    float w = UiTextWidth(text, size);
    float x = GetScreenWidth() - w - Ui(14.0f);   // top-right corner
    UiPanel((Rectangle){ x - Ui(14.0f), 0.0f, w + Ui(28.0f), Ui(34.0f) });
    UiLabel(text, x, Ui(6.0f), size, GOLD);
}
