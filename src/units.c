// units.c - Unit pool, movement and drawing.
//
// Pool: units[] is allocated once. Spawning finds a slot with active == false;
// despawning just clears the flag. No malloc/free while the game runs, so no
// fragmentation or hitches, and memory use is known up front.
//
// Movement: each tick a moving unit steps toward its target, gets pushed away
// from units it overlaps (separation), and refuses to step onto unwalkable
// tiles. Units walk in straight lines - pathfinding will plug in here later.

#include "units.h"
#include "config.h"
#include "grid.h"
#include "map.h"
#include "raymath.h"
#include <math.h>

#define UNIT_COLOR          (Color){ 220, 200, 60, 255 }
#define UNIT_SELECTED_COLOR (Color){ 60, 255, 90, 255 }
#define UNIT_DRAW_SEGMENTS  12   // circle smoothness; units are small, keep it cheap
#define MAX_NEIGHBOURS      32   // neighbours checked for separation

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

// Apply a step, but never onto unwalkable tiles. X and Y are tried separately
// so units slide along walls instead of sticking to them.
static void MoveWithTerrain(Unit *u, Vector2 step)
{
    if (MapIsWalkable((Vector2){ u->pos.x + step.x, u->pos.y })) u->pos.x += step.x;
    if (MapIsWalkable((Vector2){ u->pos.x, u->pos.y + step.y })) u->pos.y += step.y;
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
// doesn't fight over a single point when it arrives.
void UnitsOrderMove(const int *ids, int count, Vector2 dest)
{
    if (count <= 0) return;

    int side = (int)ceilf(sqrtf((float)count));
    float spacing = UNIT_RADIUS*2.5f;
    float half = (side - 1)*spacing*0.5f;

    for (int k = 0; k < count; k++)
    {
        Unit *u = &units[ids[k]];
        u->target = (Vector2){ dest.x - half + (k % side)*spacing, dest.y - half + (k / side)*spacing };
        u->moving = true;
    }
}
