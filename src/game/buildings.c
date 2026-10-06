// buildings.c - Buildings: placement, production queue, drawing.
//
// Pool: like units, buildings live in a fixed array with an `active` flag and
// a `serial` number per placement. Anything that remembers a building
// (workers, the selection, attackers) stores (slot, serial) and checks
// BuildingIsAlive() before using it.
//
// Blocking: a building marks its tiles as blocked in the map (MapSetBlocked),
// so pathfinding and movement treat it like rock. Units standing there when
// it's placed are pushed to the nearest open spot. Destroying it unblocks
// the tiles again.
//
// Production: each building has a queue of up to MAX_QUEUE units. The cost is
// paid when queuing; the first unit trains for its `trainTime`, then appears
// next to the building and the rest of the queue moves up.

#include "buildings.h"
#include "economy.h"
#include "grid.h"
#include "map.h"
#include "units.h"
#include "raymath.h"

#define PLAYER_BASE_COLOR (Color){ 170, 150, 40, 255 }
#define AI_BASE_COLOR     (Color){ 150, 40, 35, 255 }
#define ROOF_COLOR        (Color){ 0, 0, 0, 60 }
#define HEALTH_BAR_H      5.0f

Building buildings[MAX_BUILDINGS];
static unsigned int nextSerial = 1;

Rectangle BuildingRect(int id)
{
    const Building *b = &buildings[id];
    return (Rectangle){ (float)(b->tx*TILE_SIZE), (float)(b->ty*TILE_SIZE), (float)(b->size*TILE_SIZE), (float)(b->size*TILE_SIZE) };
}

Vector2 BuildingCentre(int id)
{
    Rectangle r = BuildingRect(id);
    return (Vector2){ r.x + r.width*0.5f, r.y + r.height*0.5f };
}

static Vector2 ClosestPointOnRect(Rectangle r, Vector2 p)
{
    return (Vector2){ Clamp(p.x, r.x, r.x + r.width), Clamp(p.y, r.y, r.y + r.height) };
}

float BuildingDistance(int id, Vector2 p)
{
    return Vector2Distance(p, ClosestPointOnRect(BuildingRect(id), p));
}

// The point on the wall nearest `from`, pushed outward so a unit of `radius`
// fits there, then snapped to the nearest open spot.
Vector2 BuildingApproachPoint(int id, Vector2 from, float radius)
{
    Vector2 wall = ClosestPointOnRect(BuildingRect(id), from);
    Vector2 out = Vector2Subtract(wall, BuildingCentre(id));
    out = Vector2Scale(Vector2Normalize(out), radius + 2.0f);
    Vector2 spot = Vector2Add(wall, out);
    UnitsOpenSpots(spot, 1, &spot);
    return spot;
}

int BuildingAt(Vector2 p)
{
    for (int i = 0; i < MAX_BUILDINGS; i++)
    {
        if (buildings[i].active && CheckCollisionPointRec(p, BuildingRect(i))) return i;
    }
    return -1;
}

bool BuildingIsAlive(int id, unsigned int serial)
{
    return id >= 0 && id < MAX_BUILDINGS && buildings[id].active && buildings[id].serial == serial;
}

// Scanning the building pool is fine: it's small (MAX_BUILDINGS), unlike units.
int BuildingsFindNearest(Vector2 pos, float maxDist, int team, bool enemy)
{
    int best = -1;
    float bestDist = maxDist;
    for (int i = 0; i < MAX_BUILDINGS; i++)
    {
        const Building *b = &buildings[i];
        if (!b->active) continue;
        bool isEnemy = (b->team != team);
        if (isEnemy != enemy) continue;   // we want enemy buildings, or our own
        if (enemy && b->hp <= b->incomingDamage) continue;   // doomed: don't waste attacks
        float d = BuildingDistance(i, pos);
        if (d <= bestDist) { bestDist = d; best = i; }
    }
    return best;
}

// Units caught under a new building move to the nearest open spot.
static void PushUnitsOut(int id)
{
    static int found[MAX_UNITS];
    Rectangle r = BuildingRect(id);
    Rectangle area = { r.x - UNIT_RADIUS, r.y - UNIT_RADIUS, r.width + UNIT_RADIUS*2.0f, r.height + UNIT_RADIUS*2.0f };
    int count = GridQuery(area, found, MAX_UNITS);
    for (int k = 0; k < count; k++)
    {
        Unit *u = &units[found[k]];
        if (MapCircleWalkable(u->pos, u->radius)) continue;
        UnitsOpenSpots(u->pos, 1, &u->pos);
        u->prevPos = u->pos;
    }
}

int BuildingPlace(BuildingType type, int team, Vector2 centre)
{
    int size = BUILDING_STATS[type].size;
    int tx = (int)(centre.x/TILE_SIZE) - size/2, ty = (int)(centre.y/TILE_SIZE) - size/2;

    // Every tile must be open ground with no other building on it.
    for (int y = ty; y < ty + size; y++)
    {
        for (int x = tx; x < tx + size; x++)
        {
            if (!MapTileWalkable(x, y)) return -1;
        }
    }

    for (int i = 0; i < MAX_BUILDINGS; i++)
    {
        if (buildings[i].active) continue;
        buildings[i] = (Building){
            .active = true,
            .serial = nextSerial++,
            .type = type,
            .team = team,
            .hp = BUILDING_STATS[type].hp,
            .tx = tx, .ty = ty, .size = size,
        };
        MapSetBlocked(tx, ty, size, size, true);
        PushUnitsOut(i);
        return i;
    }
    return -1;
}

void BuildingDestroy(int id)
{
    Building *b = &buildings[id];
    if (!b->active) return;
    b->active = false;   // queued units are lost (their gold too)
    MapSetBlocked(b->tx, b->ty, b->size, b->size, false);
}

bool BuildingQueueTrain(int id, UnitType type)
{
    Building *b = &buildings[id];
    if (!b->active || b->queueCount >= MAX_QUEUE) return false;
    if (!EconomySpend(b->team, UNIT_STATS[type].cost)) return false;
    b->queue[b->queueCount++] = type;
    return true;
}

void BuildingsTick(void)
{
    for (int i = 0; i < MAX_BUILDINGS; i++)
    {
        Building *b = &buildings[i];
        if (!b->active || b->queueCount == 0) continue;

        if (++b->trainTicks < (int)(UNIT_STATS[b->queue[0]].trainTime*TICK_RATE)) continue;

        // Done: spawn just below the building. If the unit pool is full, wait.
        Rectangle r = BuildingRect(i);
        Vector2 spot = { r.x + r.width*0.5f, r.y + r.height + UNIT_RADIUS*2.0f };
        UnitsOpenSpots(spot, 1, &spot);
        if (UnitSpawn(spot, b->queue[0], b->team) == -1) continue;

        for (int k = 1; k < b->queueCount; k++) b->queue[k - 1] = b->queue[k];
        b->queueCount--;
        b->trainTicks = 0;
    }
}

void BuildingsDraw(Rectangle view)
{
    for (int i = 0; i < MAX_BUILDINGS; i++)
    {
        const Building *b = &buildings[i];
        if (!b->active) continue;
        Rectangle r = BuildingRect(i);
        if (!CheckCollisionRecs(r, view)) continue;   // off screen

        DrawRectangleRec(r, (b->team == PLAYER_TEAM) ? PLAYER_BASE_COLOR : AI_BASE_COLOR);
        DrawRectangleRec((Rectangle){ r.x + 10, r.y + 10, r.width - 20, r.height - 20 }, ROOF_COLOR);

        float maxHp = BUILDING_STATS[b->type].hp;
        if (b->hp < maxHp)
        {
            Rectangle bar = { r.x, r.y - HEALTH_BAR_H - 3.0f, r.width, HEALTH_BAR_H };
            float frac = b->hp/maxHp;
            DrawRectangleRec(bar, BLACK);
            DrawRectangleRec((Rectangle){ bar.x, bar.y, bar.width*frac, bar.height }, (frac > 0.5f) ? GREEN : (frac > 0.25f) ? ORANGE : RED);
        }

        // Production progress along the bottom edge.
        if (b->queueCount > 0)
        {
            float frac = b->trainTicks/(UNIT_STATS[b->queue[0]].trainTime*TICK_RATE);
            DrawRectangleRec((Rectangle){ r.x, r.y + r.height - 4.0f, r.width*frac, 4.0f }, SKYBLUE);
        }
    }
}
