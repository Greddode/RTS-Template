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
//
// Combat: attacking units are steered by combat.c (CombatUnitTick); idle and
// attack-moving units let combat.c look for nearby enemies (CombatAcquireTick).
//
// Attack-move is a normal move order plus the `attackMove` flag: while
// walking, the unit scans for enemies like an idle unit does. When a fight
// ends with no enemies left nearby, combat.c sends it on to attackMoveDest.
//
// Workers ordered to mine are steered by economy.c (EconomyWorkerTick), and
// workers ordered to construct by buildings.c (BuildingsWorkerTick). Workers
// don't auto-attack, so they stay on the job.
//
// Healers (heal.c) never take the combat path: when idle or attack-moving
// they look for damaged allies instead of enemies, and an attack order sends
// them along as an attack-move. So does any unit with damage 0.
//
// Stop drops every order (the unit is idle, so it still auto-attacks).
// Hold is stop plus `holdPosition`: combat.c then only lets it target enemies
// already in range, and never chase.

#include "units.h"
#include "config.h"
#include "buildings.h"
#include "combat.h"
#include "economy.h"
#include "fog.h"
#include "grid.h"
#include "heal.h"
#include "map.h"
#include "path.h"
#include "sprites.h"
#include "raymath.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PLAYER_COLOR        (Color){ 220, 200, 60, 255 }
#define AI_COLOR            (Color){ 210, 60, 50, 255 }
#define ARCHER_DOT_COLOR    (Color){ 40, 30, 20, 255 }   // archers get a dark centre dot
#define KNIGHT_RING_COLOR   (Color){ 40, 30, 20, 255 }   // knights get a dark ring
#define MEDIC_CROSS_COLOR   (Color){ 245, 245, 240, 255 } // medics get a white cross
#define MAGE_MARK_COLOR     (Color){ 150, 120, 255, 255 } // mages get a violet diamond
#define WORKER_MARK_COLOR   (Color){ 235, 235, 225, 255 } // workers get a light square
#define UNIT_SELECTED_COLOR (Color){ 60, 255, 90, 255 }
#define HEALTH_BAR_W        14.0f
#define HEALTH_BAR_H        3.0f
#define UNIT_DRAW_SEGMENTS  12   // circle smoothness; units are small, keep it cheap
#define MAX_NEIGHBOURS      32   // neighbours checked for separation
#define UNIT_REPATH_TICKS   TICK_RATE           // 1 s without progress = ask for a new path
#define UNIT_MAX_REPATHS    3
#define UNIT_ARRIVE_DIST    (TILE_SIZE*0.5f)    // close enough to the target to count as arrived
#define FORMATION_SPACING   (UNIT_RADIUS*2.5f)  // gap between formation spots
#define FORMATION_MAX_RINGS 64                  // how far out to look for free spots
#define FACING_MIN_STEP     0.1f                // sideways step (px per tick) needed to turn around

Unit units[MAX_UNITS];
static int activeCount = 0;
static unsigned int nextSerial = 1;

int UnitSpawn(Vector2 pos, UnitType type, int team)
{
    // Linear search is fine: spawning is rare compared to ticking.
    for (int i = 0; i < MAX_UNITS; i++)
    {
        if (units[i].active) continue;
        units[i] = (Unit){
            .active = true,
            .serial = nextSerial++,
            .type = type,
            .team = team,
            .hp = UNIT_STATS[type].hp,
            .pos = pos, .prevPos = pos, .target = pos,
            .radius = UNIT_RADIUS,
            .speed = UNIT_STATS[type].speed,
            .acquireTicks = i % COMBAT_ACQUIRE_TICKS,   // spread enemy checks across ticks
        };
        activeCount++;
        return i;
    }
    return -1;
}

// The slot is simply marked free. Anything still pointing at this unit (an
// attacker, a projectile) notices through UnitIsAlive() and lets go.
void UnitDespawn(int id)
{
    if (!units[id].active) return;
    units[id].active = false;
    units[id].selected = false;
    units[id].moving = false;
    units[id].attacking = false;
    PathCancel(id);
    activeCount--;
}

bool UnitIsAlive(int id, unsigned int serial)
{
    return id >= 0 && id < MAX_UNITS && units[id].active && units[id].serial == serial;
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

void UnitStop(int id)
{
    StopMoving(id);
}

void UnitMoveTo(int id, Vector2 dest)
{
    Unit *u = &units[id];
    u->target = dest;
    u->moving = true;
    u->repathsLeft = UNIT_MAX_REPATHS;
    RequestPath(id);
}

Vector2 UnitStepToward(int id, Vector2 point)
{
    Unit *u = &units[id];
    Vector2 to = Vector2Subtract(point, u->pos);
    float dist = Vector2Length(to);
    float maxStep = u->speed*TICK_DT;
    return (dist <= maxStep) ? to : Vector2Scale(to, maxStep/dist);
}

// Ask for a new path, or give up if this order has used all its retries.
static void Repath(int id)
{
    if (units[id].repathsLeft-- > 0) RequestPath(id);
    else StopMoving(id);
}

// This tick's step along the unit's path (zero while waiting for the path).
Vector2 UnitFollowPath(int id)
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
        else { StopMoving(id); u->attackMove = false; }   // arrived: order complete
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
    HealBeginTick();
    for (int i = 0; i < MAX_UNITS; i++)
    {
        Unit *u = &units[i];
        if (!u->active) continue;

        u->prevPos = u->pos;
        if (u->cooldownTicks > 0) u->cooldownTicks--;

        Vector2 step = { 0 };
        bool gathering = (u->gatherState != GATHER_NONE);
        bool busy = u->attacking || gathering || u->buildOrder || u->healing;
        if (!busy && (!u->moving || u->attackMove))   // look for enemies (or, for healers, damaged allies)
        {
            if (UNIT_STATS[u->type].canHeal) HealAcquireTick(i);
            else CombatAcquireTick(i);
        }
        if (u->healing) step = HealUnitTick(i);
        else if (u->attacking) step = CombatUnitTick(i);   // may kill other units
        else if (gathering) step = EconomyWorkerTick(i);
        else if (u->buildOrder) step = BuildingsWorkerTick(i);
        else if (u->moving) step = UnitFollowPath(i);

        if (!u->active) continue;
        // Face the way the unit wants to go. Separation pushes are left out, so
        // a unit jostled in a crowd doesn't flicker left and right.
        if (step.x < -FACING_MIN_STEP) u->facingLeft = true;
        if (step.x > FACING_MIN_STEP) u->facingLeft = false;
        step = Vector2Add(step, SeparationPush(i));
        MoveWithTerrain(u, step);
    }
}

// Body in team colour plus a type mark: archer = dark dot, worker = light
// square, knight = dark ring, medic = white cross, mage = violet diamond.
// With art for the type (sprites.c), the art tinted in team colour instead.
void UnitsDrawIcon(UnitType type, int team, Vector2 p, float radius)
{
    Color body = (team == PLAYER_TEAM) ? PLAYER_COLOR : AI_COLOR;
    if (SpritesHaveUnit(type)) { SpritesDrawUnit(type, p, radius, false, body); return; }

    DrawCircleSector(p, radius, 0.0f, 360.0f, UNIT_DRAW_SEGMENTS, body);
    if (type == UNIT_ARCHER) DrawCircleSector(p, radius*0.4f, 0.0f, 360.0f, UNIT_DRAW_SEGMENTS, ARCHER_DOT_COLOR);
    if (type == UNIT_KNIGHT) DrawRing(p, radius*0.45f, radius*0.75f, 0.0f, 360.0f, UNIT_DRAW_SEGMENTS, KNIGHT_RING_COLOR);
    if (type == UNIT_MAGE) DrawPoly(p, 4, radius*0.6f, 0.0f, MAGE_MARK_COLOR);
    if (type == UNIT_MEDIC)
    {
        float l = radius*1.1f, w = radius*0.4f;
        DrawRectangleRec((Rectangle){ p.x - l*0.5f, p.y - w*0.5f, l, w }, MEDIC_CROSS_COLOR);
        DrawRectangleRec((Rectangle){ p.x - w*0.5f, p.y - l*0.5f, w, l }, MEDIC_CROSS_COLOR);
    }
    if (type == UNIT_WORKER)
    {
        float s = radius*0.9f;
        DrawRectangleRec((Rectangle){ p.x - s*0.5f, p.y - s*0.5f, s, s }, WORKER_MARK_COLOR);
    }
}

// Carried gold and the health bar (only once the unit has taken damage).
static void DrawUnitOverlays(const Unit *u, Vector2 p)
{
    if (u->carryGold > 0) DrawCircleSector((Vector2){ p.x + u->radius*0.7f, p.y - u->radius*0.7f }, 2.5f, 0.0f, 360.0f, 6, GOLD);

    float maxHp = UNIT_STATS[u->type].hp;
    if (u->hp < maxHp)
    {
        Rectangle bar = { p.x - HEALTH_BAR_W*0.5f, p.y - u->radius - 6.0f, HEALTH_BAR_W, HEALTH_BAR_H };
        float frac = u->hp/maxHp;
        DrawRectangleRec(bar, BLACK);
        DrawRectangleRec((Rectangle){ bar.x, bar.y, bar.width*frac, bar.height }, (frac > 0.5f) ? GREEN : (frac > 0.25f) ? ORANGE : RED);
    }
}

// Three passes, so raylib can batch. Shapes use raylib's built-in white
// texture and art uses the atlas; every switch between the two ends a batch
// (one more draw call). So: (1) shapes underneath (selection circles, plus
// units without art drawn complete, exactly as before sprites existed),
// (2) every sprite in a row, all from the one atlas, (3) the overlays of the
// units with art on top.
void UnitsDraw(Rectangle view, float alpha)
{
    // Ask the grid for units near the screen instead of checking all of them.
    // The margin covers unit size and the small lerp between ticks.
    static int visible[MAX_UNITS];
    static Vector2 drawPos[MAX_UNITS];
    float margin = UNIT_RADIUS*4.0f;
    Rectangle area = { view.x - margin, view.y - margin, view.width + margin*2.0f, view.height + margin*2.0f };
    int found = GridQuery(area, visible, MAX_UNITS);

    // Pass 1: drop units hidden by fog, then the shapes.
    int count = 0;
    for (int k = 0; k < found; k++)
    {
        const Unit *u = &units[visible[k]];
        if (u->team != PLAYER_TEAM && !FogCanSee(PLAYER_TEAM, u->pos)) continue;   // hidden by fog
        Vector2 p = Vector2Lerp(u->prevPos, u->pos, alpha);
        visible[count] = visible[k];
        drawPos[count++] = p;

        if (u->selected) DrawCircleSector(p, u->radius + 2.0f, 0.0f, 360.0f, UNIT_DRAW_SEGMENTS, UNIT_SELECTED_COLOR);
        if (!SpritesHaveUnit(u->type))
        {
            UnitsDrawIcon(u->type, u->team, p, u->radius);
            DrawUnitOverlays(u, p);
        }
    }

    // Pass 2: the art, one batch.
    for (int k = 0; k < count; k++)
    {
        const Unit *u = &units[visible[k]];
        if (SpritesHaveUnit(u->type))
            SpritesDrawUnit(u->type, drawPos[k], u->radius, u->facingLeft, (u->team == PLAYER_TEAM) ? PLAYER_COLOR : AI_COLOR);
    }

    // Pass 3: overlays of the units with art.
    for (int k = 0; k < count; k++)
    {
        const Unit *u = &units[visible[k]];
        if (SpritesHaveUnit(u->type)) DrawUnitOverlays(u, drawPos[k]);
    }
}

// Fill `spots` with up to `count` open positions around `dest`, closest first:
// walk square rings outward from the centre and keep every spot a unit fits
// on. Every unit gets its own spot, so a group never fights over one point.
// Returns how many spots were found.
int UnitsOpenSpots(Vector2 dest, int count, Vector2 *spots)
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

    int found = UnitsOpenSpots(dest, count, spots);

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
        units[id].attacking = false;
        units[id].attackMove = false;
        units[id].holdPosition = false;
        units[id].gatherState = GATHER_NONE;
        units[id].buildOrder = false;
        units[id].leashed = false;
        units[id].healing = false;
        UnitMoveTo(id, (k < found) ? spots[spotOrder[k]] : dest);   // more units than open spots: rare
    }
}

// Same formation and pathing as a move order; the flag makes the units fight
// whatever they meet on the way. Each remembers its own formation spot.
void UnitsOrderAttackMove(const int *ids, int count, Vector2 dest)
{
    UnitsOrderMove(ids, count, dest);
    for (int k = 0; k < count; k++)
    {
        units[ids[k]].attackMove = true;
        units[ids[k]].attackMoveDest = units[ids[k]].target;
    }
}

// Shared by unit and building attack orders.
static void SetAttackTarget(int id, int target, unsigned int serial, bool isBuilding)
{
    Unit *u = &units[id];
    StopMoving(id);
    u->gatherState = GATHER_NONE;
    u->buildOrder = false;
    u->leashed = false;   // an order; combat.c re-sets it for auto-targeting
    u->attacking = true;
    u->attackTargetIsBuilding = isBuilding;
    u->attackTarget = target;
    u->attackTargetSerial = serial;
    u->chaseDirect = false;
    u->chaseTicks = 0;   // decide how to reach it on its very next tick
}

// Units that can't attack (healers, damage 0) are given an attack-move to
// `where` instead: they go along with the army and heal on the way.
static void NonAttackersAttackMove(const int *ids, int count, Vector2 where)
{
    static int movers[MAX_UNITS];
    int n = 0;
    for (int k = 0; k < count; k++) if (UNIT_STATS[units[ids[k]].type].damage <= 0.0f) movers[n++] = ids[k];
    UnitsOrderAttackMove(movers, n, where);
}

void UnitsOrderAttack(const int *ids, int count, int target)
{
    for (int k = 0; k < count; k++)
    {
        if (ids[k] == target || units[ids[k]].team == units[target].team) continue;
        if (UNIT_STATS[units[ids[k]].type].damage <= 0.0f) continue;   // handled below
        SetAttackTarget(ids[k], target, units[target].serial, false);
    }
    NonAttackersAttackMove(ids, count, units[target].pos);
}

void UnitsOrderAttackBuilding(const int *ids, int count, int building)
{
    for (int k = 0; k < count; k++)
    {
        if (units[ids[k]].team == buildings[building].team) continue;
        if (UNIT_STATS[units[ids[k]].type].damage <= 0.0f) continue;   // handled below
        SetAttackTarget(ids[k], building, buildings[building].serial, true);
    }
    NonAttackersAttackMove(ids, count, BuildingCentre(building));
}

void UnitsOrderStop(const int *ids, int count)
{
    for (int k = 0; k < count; k++)
    {
        Unit *u = &units[ids[k]];
        StopMoving(ids[k]);
        u->attacking = false;
        u->attackMove = false;
        u->holdPosition = false;
        u->gatherState = GATHER_NONE;
        u->buildOrder = false;
        u->leashed = false;
        u->healing = false;
    }
}

void UnitsOrderHold(const int *ids, int count)
{
    UnitsOrderStop(ids, count);
    for (int k = 0; k < count; k++) units[ids[k]].holdPosition = true;
}

void UnitsReset(void)
{
    memset(units, 0, sizeof(units));
    activeCount = 0;
}
