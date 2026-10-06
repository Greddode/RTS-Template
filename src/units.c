// units.c - Unit pool, movement and drawing.
//
// Pool: units[] is allocated once. Spawning finds a slot with active == false;
// despawning just clears the flag. No malloc/free while the game runs, so no
// fragmentation or hitches, and memory use is known up front.
//
// Movement: a move order asks path.c for a route. Each tick a moving unit steps
// toward its current waypoint (or waits if its path isn't ready yet), gets
// pushed away from units it overlaps (separation), and refuses to step into
// water/rock.
//
// Repathing: if a unit stops getting closer to its waypoint for
// UNIT_REPATH_TICKS (blocked by a crowd, pushed off its route, ...), it asks
// for a fresh path from where it stands. After UNIT_MAX_REPATHS tries it gives up.

#include "units.h"
#include "config.h"
#include "grid.h"
#include "map.h"
#include "path.h"
#include "raymath.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>

#define UNIT_COLOR          (Color){ 220, 200, 60, 255 }
#define UNIT_SELECTED_COLOR (Color){ 60, 255, 90, 255 }
#define UNIT_DRAW_SEGMENTS  12   // circle smoothness; units are small, keep it cheap
#define MAX_NEIGHBOURS      32   // neighbours checked for separation
#define UNIT_REPATH_TICKS   TICK_RATE           // 1 s without progress = ask for a new path
#define UNIT_MAX_REPATHS    3
#define UNIT_ARRIVE_DIST    (TILE_SIZE*0.5f)    // close enough to the target to count as arrived
#define FORMATION_SPACING   (UNIT_RADIUS*2.5f)  // gap between formation spots
#define FORMATION_MAX_RINGS 64                  // how far out to look for free spots

Unit units[MAX_UNITS];
static int activeCount = 0;

int UnitSpawn(Vector2 pos)
{
    // Linear search is fine: spawning is rare compared to ticking.
    for (int i = 0; i < MAX_UNITS; i++)
    {
        if (units[i].active) continue;
        units[i] = (Unit){
            .active = true,
            .pos = pos, .prevPos = pos, .target = pos,
            .radius = UNIT_RADIUS,
            .speed = UNIT_SPEED,
        };
        activeCount++;
        return i;
    }
    return -1;
}

void UnitDespawn(int id)
{
    if (!units[id].active) return;
    units[id].active = false;
    PathCancel(id);
    activeCount--;
}

int UnitsActiveCount(void)
{
    return activeCount;
}

// Push away from overlapping neighbours so groups spread out instead of
// stacking on one point. Uses the spatial grid: only nearby units are checked.
static Vector2 SeparationPush(int self)
{
    const Unit *u = &units[self];
    float range = u->radius*2.0f;
    Rectangle area = { u->pos.x - range, u->pos.y - range, range*2.0f, range*2.0f };

    int near[MAX_NEIGHBOURS];
    int count = GridQuery(area, near, MAX_NEIGHBOURS);

    Vector2 push = { 0 };
    for (int k = 0; k < count; k++)
    {
        int other = near[k];
        if (other == self) continue;

        Vector2 away = Vector2Subtract(u->pos, units[other].pos);
        float dist = Vector2Length(away);
        float minDist = u->radius + units[other].radius;
        if (dist >= minDist) continue;

        // Exactly on top of each other: pick opposite directions by index.
        if (dist < 0.001f) { away = (Vector2){ (self < other) ? 1.0f : -1.0f, 0.0f }; dist = 1.0f; }

        // Each unit moves half the overlap; the other unit does the other half.
        push = Vector2Add(push, Vector2Scale(away, 0.5f*(minDist - dist)/dist));
    }
    return push;
}

// Apply a step, but never into water/rock. X and Y are tried separately so
// units slide along walls instead of sticking to them.
static void MoveWithTerrain(Unit *u, Vector2 step)
{
    if (MapCircleWalkable((Vector2){ u->pos.x + step.x, u->pos.y }, u->radius)) u->pos.x += step.x;
    if (MapCircleWalkable((Vector2){ u->pos.x, u->pos.y + step.y }, u->radius)) u->pos.y += step.y;
}

static void RequestPath(int id)
{
    Unit *u = &units[id];
    PathRequest(id, u->pos, u->target);
    u->bestDist = FLT_MAX;
    u->stuckTicks = 0;
}

static void StopMoving(int id)
{
    units[id].moving = false;
    PathCancel(id);
}

// Ask for a new path, or give up if this order has used all its retries.
static void Repath(int id)
{
    if (units[id].repathsLeft-- > 0) RequestPath(id);
    else StopMoving(id);
}

// This tick's step along the unit's path (zero while waiting for the path).
static Vector2 FollowPath(int id)
{
    Unit *u = &units[id];
    Vector2 none = { 0 };

    PathStatus status = PathGetStatus(id);
    if (status == PATH_PENDING) return none;
    if (status != PATH_READY) { StopMoving(id); return none; }

    Vector2 waypoint;
    if (!PathCurrentWaypoint(id, &waypoint))
    {
        // End of the path. If it stopped short of the target (partial path, or
        // pushed off it on the way), try again from here.
        if (Vector2Distance(u->pos, u->target) > UNIT_ARRIVE_DIST) Repath(id);
        else StopMoving(id);
        return none;
    }

    Vector2 toWaypoint = Vector2Subtract(waypoint, u->pos);
    float dist = Vector2Length(toWaypoint);
    float maxStep = u->speed*TICK_DT;
    if (dist <= maxStep)
    {
        PathAdvance(id);
        u->bestDist = FLT_MAX;
        u->stuckTicks = 0;
        return toWaypoint;
    }

    if (dist < u->bestDist - 0.5f) { u->bestDist = dist; u->stuckTicks = 0; }
    else if (++u->stuckTicks > UNIT_REPATH_TICKS) { Repath(id); return none; }

    return Vector2Scale(toWaypoint, maxStep/dist);
}

void UnitsTick(void)
{
    for (int i = 0; i < MAX_UNITS; i++)
    {
        Unit *u = &units[i];
        if (!u->active) continue;

        u->prevPos = u->pos;

        Vector2 step = u->moving ? FollowPath(i) : (Vector2){ 0 };
        step = Vector2Add(step, SeparationPush(i));
        MoveWithTerrain(u, step);
    }
}

void UnitsDraw(Rectangle view, float alpha)
{
    // Ask the grid for units near the screen instead of checking all of them.
    // The margin covers unit size and the small lerp between ticks.
    static int visible[MAX_UNITS];
    float margin = UNIT_RADIUS*4.0f;
    Rectangle area = { view.x - margin, view.y - margin, view.width + margin*2.0f, view.height + margin*2.0f };
    int count = GridQuery(area, visible, MAX_UNITS);

    // Everything here is a filled circle (same draw mode, no textures), so
    // raylib batches all of it into a few draw calls.
    for (int k = 0; k < count; k++)
    {
        const Unit *u = &units[visible[k]];
        Vector2 p = Vector2Lerp(u->prevPos, u->pos, alpha);
        if (u->selected) DrawCircleSector(p, u->radius + 2.0f, 0.0f, 360.0f, UNIT_DRAW_SEGMENTS, UNIT_SELECTED_COLOR);
        DrawCircleSector(p, u->radius, 0.0f, 360.0f, UNIT_DRAW_SEGMENTS, UNIT_COLOR);
    }
}

// Fill `spots` with up to `count` open positions around `dest`, closest first:
// walk square rings outward from the centre and keep every spot a unit fits
// on. Every unit gets its own spot, so a group never fights over one point.
// Returns how many spots were found.
static int FormationSpots(Vector2 dest, int count, Vector2 *spots)
{
    int found = 0;
    for (int ring = 0; ring <= FORMATION_MAX_RINGS && found < count; ring++)
    {
        for (int gy = -ring; gy <= ring && found < count; gy++)
        {
            for (int gx = -ring; gx <= ring && found < count; gx++)
            {
                if (abs(gx) != ring && abs(gy) != ring) continue;   // only this ring's edge
                Vector2 p = { dest.x + gx*FORMATION_SPACING, dest.y + gy*FORMATION_SPACING };
                if (MapCircleWalkable(p, UNIT_RADIUS)) spots[found++] = p;
            }
        }
    }
    return found;
}

// Sorting helper: qsort an array of indices by sortKey[index].
static float sortKey[MAX_UNITS];
static int CompareKeys(const void *a, const void *b)
{
    float ka = sortKey[*(const int *)a], kb = sortKey[*(const int *)b];
    return (ka < kb) - (ka > kb);   // largest key first
}

static void SortByAxis(int *order, int n, const Vector2 *points, Vector2 axis)
{
    for (int k = 0; k < n; k++) sortKey[order[k]] = Vector2DotProduct(points[order[k]], axis);
    qsort(order, n, sizeof(int), CompareKeys);
}

// Order `points` front-to-back along `forward`, in rows of `rowSize`, each
// row sorted left-to-right. Sorting units and spots the same way and pairing
// them up keeps the group's shape: nobody walks through the crowd.
static void FormationOrder(int *order, int n, const Vector2 *points, Vector2 forward, int rowSize)
{
    Vector2 side = { -forward.y, forward.x };
    for (int k = 0; k < n; k++) order[k] = k;
    SortByAxis(order, n, points, forward);
    for (int row = 0; row < n; row += rowSize)
    {
        SortByAxis(order + row, (n - row < rowSize) ? n - row : rowSize, points, side);
    }
}

void UnitsOrderMove(const int *ids, int count, Vector2 dest)
{
    static Vector2 spots[MAX_UNITS], unitPos[MAX_UNITS];
    static int spotOrder[MAX_UNITS], unitOrder[MAX_UNITS];
    if (count <= 0) return;

    int found = FormationSpots(dest, count, spots);

    // Move direction: from the group's centre toward the destination.
    Vector2 centre = { 0 };
    for (int k = 0; k < count; k++)
    {
        unitPos[k] = units[ids[k]].pos;
        centre = Vector2Add(centre, unitPos[k]);
    }
    centre = Vector2Scale(centre, 1.0f/count);
    Vector2 forward = Vector2Normalize(Vector2Subtract(dest, centre));
    if (forward.x == 0.0f && forward.y == 0.0f) forward = (Vector2){ 1.0f, 0.0f };

    int rowSize = (int)ceilf(sqrtf((float)count));
    FormationOrder(unitOrder, count, unitPos, forward, rowSize);
    FormationOrder(spotOrder, found, spots, forward, rowSize);

    for (int k = 0; k < count; k++)
    {
        int id = ids[unitOrder[k]];
        Unit *u = &units[id];
        u->target = (k < found) ? spots[spotOrder[k]] : dest;   // more units than open spots: rare
        u->moving = true;
        u->repathsLeft = UNIT_MAX_REPATHS;
        RequestPath(id);
    }
}
