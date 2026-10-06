// units.c - Unit pool, movement and drawing.
//
// Pool: units[] is allocated once. Spawning finds a slot with active == false;
// despawning just clears the flag. No malloc/free while the game runs, so no
// fragmentation or hitches, and memory use is known up front.
//
// Movement: each tick a moving unit steps toward its target, gets pushed away
// from units it overlaps (separation), and refuses to step onto unwalkable
// tiles. Units walk in straight lines - pathfinding will plug in here later.
// A unit that stops getting closer to its target (blocked by water, a crowd,
// ...) gives up after UNIT_GIVE_UP_TICKS instead of pushing forever.

#include "units.h"
#include "config.h"
#include "grid.h"
#include "map.h"
#include "raymath.h"
#include <float.h>
#include <math.h>

#define UNIT_COLOR          (Color){ 220, 200, 60, 255 }
#define UNIT_SELECTED_COLOR (Color){ 60, 255, 90, 255 }
#define UNIT_DRAW_SEGMENTS  12   // circle smoothness; units are small, keep it cheap
#define MAX_NEIGHBOURS      32   // neighbours checked for separation
#define UNIT_GIVE_UP_TICKS  (TICK_RATE*3/2)   // 1.5 s without progress = give up
#define DEST_SEARCH_TILES   8    // how far to look for open ground around a blocked destination

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

// True if a unit's body at `pos` is clear of water/rock. Checks the four edge
// points of the circle: cheap, and close enough for small round units.
static bool UnitFits(Vector2 pos, float r)
{
    return MapIsWalkable((Vector2){ pos.x + r, pos.y }) && MapIsWalkable((Vector2){ pos.x - r, pos.y }) &&
           MapIsWalkable((Vector2){ pos.x, pos.y + r }) && MapIsWalkable((Vector2){ pos.x, pos.y - r });
}

// Apply a step, but never into water/rock. X and Y are tried separately so
// units slide along walls instead of sticking to them.
static void MoveWithTerrain(Unit *u, Vector2 step)
{
    if (UnitFits((Vector2){ u->pos.x + step.x, u->pos.y }, u->radius)) u->pos.x += step.x;
    if (UnitFits((Vector2){ u->pos.x, u->pos.y + step.y }, u->radius)) u->pos.y += step.y;
}

void UnitsTick(void)
{
    for (int i = 0; i < MAX_UNITS; i++)
    {
        Unit *u = &units[i];
        if (!u->active) continue;

        u->prevPos = u->pos;

        Vector2 step = { 0 };
        if (u->moving)
        {
            Vector2 toTarget = Vector2Subtract(u->target, u->pos);
            float dist = Vector2Length(toTarget);
            float maxStep = u->speed*TICK_DT;
            if (dist <= maxStep) { step = toTarget; u->moving = false; }
            else step = Vector2Scale(toTarget, maxStep/dist);

            if (dist < u->bestDist - 0.5f) { u->bestDist = dist; u->stuckTicks = 0; }
            else if (++u->stuckTicks > UNIT_GIVE_UP_TICKS) u->moving = false;
        }

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

// Each unit gets its own spot in a square formation around `dest`, so a group
// doesn't fight over a single point when it arrives. Spots that are off the
// map or in water/rock are moved to the nearest open tile.
void UnitsOrderMove(const int *ids, int count, Vector2 dest)
{
    if (count <= 0) return;

    int side = (int)ceilf(sqrtf((float)count));
    float spacing = UNIT_RADIUS*2.5f;
    float half = (side - 1)*spacing*0.5f;

    for (int k = 0; k < count; k++)
    {
        Unit *u = &units[ids[k]];
        Vector2 spot = { dest.x - half + (k % side)*spacing, dest.y - half + (k / side)*spacing };
        spot.x = Clamp(spot.x, 0.0f, MAP_PIXEL_W - 1.0f);
        spot.y = Clamp(spot.y, 0.0f, MAP_PIXEL_H - 1.0f);
        if (!UnitFits(spot, u->radius)) MapNearestWalkable(spot, DEST_SEARCH_TILES, &spot);

        u->target = spot;
        u->moving = true;
        u->bestDist = FLT_MAX;
        u->stuckTicks = 0;
    }
}
