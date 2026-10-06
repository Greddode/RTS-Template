// combat.c - Attacking, auto-targeting and projectiles.
//
// Attack order: the unit chases its target until it's within range (from
// UNIT_STATS), then hits it every `cooldown` seconds. Melee hits land at once;
// ranged hits spawn a projectile that flies to the target.
//
// Chasing: a few times a second the unit checks whether the straight line to
// its target is clear. If so it just walks at the target (no pathfinding).
// Only when something is in the way does it ask path.c for a route, which
// goes through the normal budgeted queue.
//
// Targets are remembered as (index, serial, isBuilding). When a unit dies its
// pool slot can be reused by a new unit; the serial tells them apart, so
// nobody ends up attacking the newcomer by mistake. The small Target*()
// helpers below hide whether the target is a unit or a building. Range to a
// building is measured to its nearest wall, not its centre.
//
// Hold position: a holding unit only picks targets already within its attack
// range, and drops a target that leaves range instead of chasing it.
//
// Auto-targeting: idle and attack-moving units look for the nearest enemy within
// COMBAT_AGGRO_RADIUS using the spatial grid, and attack it. Enemy units come
// first; enemy buildings only when no unit is in reach. Workers don't
// auto-attack.
//
// Overkill: each unit tracks `incomingDamage`, the damage in projectiles
// already flying at it. Once that's enough to kill it, the unit is "doomed":
// nobody fires at it or picks it as a target, so ranged units don't waste
// their cooldown on shots that will hit nothing. When a target dies or
// becomes doomed, the attacker picks the next one on the same tick.

#include "combat.h"
#include "buildings.h"
#include "config.h"
#include "grid.h"
#include "map.h"
#include "units.h"
#include "raymath.h"
#include <string.h>

#define CHASE_RETHINK_TICKS 10                   // how often a chasing unit re-plans (3x per second)
#define PROJECTILE_SPEED    320.0f               // world px per second
#define PROJECTILE_RADIUS   2.0f
#define PROJECTILE_COLOR    (Color){ 255, 240, 200, 255 }

typedef struct Projectile {
    bool         active;
    Vector2      pos, prevPos;
    int          target;
    unsigned int targetSerial;
    bool         targetIsBuilding;
    float        damage;
} Projectile;

static Projectile projectiles[MAX_PROJECTILES];
static int projectileCount = 0;

// How far a unit looks for new targets: its aggro radius, or only its own
// attack range when holding position.
static float SearchRadius(const Unit *u)
{
    return u->holdPosition ? UNIT_STATS[u->type].range : COMBAT_AGGRO_RADIUS;
}

// --- Target helpers: a target is a unit or a building -------------------------
static bool TargetAlive(bool isBuilding, int id, unsigned int serial)
{
    return isBuilding ? BuildingIsAlive(id, serial) : UnitIsAlive(id, serial);
}

static float *TargetIncoming(bool isBuilding, int id)
{
    return isBuilding ? &buildings[id].incomingDamage : &units[id].incomingDamage;
}

static bool TargetDoomed(bool isBuilding, int id)
{
    float hp = isBuilding ? buildings[id].hp : units[id].hp;
    return hp <= *TargetIncoming(isBuilding, id);
}

// Distance from `from` to the target: its centre for units, its nearest wall for buildings.
static float TargetDistance(bool isBuilding, int id, Vector2 from)
{
    return isBuilding ? BuildingDistance(id, from) : Vector2Distance(from, units[id].pos);
}

static void DealDamage(bool isBuilding, int target, float damage)
{
    if (isBuilding)
    {
        buildings[target].hp -= damage;
        if (buildings[target].hp <= 0.0f) BuildingDestroy(target);   // unblocks its tiles
    }
    else
    {
        units[target].hp -= damage;
        if (units[target].hp <= 0.0f) UnitDespawn(target);   // frees the slot, clears selection
    }
}

// Attack the nearest enemy worth attacking within `radius`: units first, then
// buildings. Returns false if there's nothing to attack.
static bool AttackNearest(int id, float radius)
{
    Unit *u = &units[id];
    int enemy = GridFindNearestEnemy(u->pos, radius, u->team);
    if (enemy != -1) { UnitsOrderAttack(&id, 1, enemy); return true; }
    int building = BuildingsFindNearestEnemy(u->pos, radius, u->team);
    if (building != -1) { UnitsOrderAttackBuilding(&id, 1, building); return true; }
    return false;
}

// Fire at a target. Uses a free projectile slot; if the pool is full (very
// unlikely), the damage lands immediately instead of being lost.
static void FireProjectile(Vector2 from, bool isBuilding, int target, unsigned int serial, float damage)
{
    for (int i = 0; i < MAX_PROJECTILES; i++)
    {
        if (projectiles[i].active) continue;
        projectiles[i] = (Projectile){
            .active = true,
            .pos = from, .prevPos = from,
            .target = target,
            .targetSerial = serial,
            .targetIsBuilding = isBuilding,
            .damage = damage,
        };
        *TargetIncoming(isBuilding, target) += damage;
        projectileCount++;
        return;
    }
    DealDamage(isBuilding, target, damage);
}

Vector2 CombatUnitTick(int id)
{
    Unit *u = &units[id];
    Vector2 none = { 0 };

    bool alive = TargetAlive(u->attackTargetIsBuilding, u->attackTarget, u->attackTargetSerial);
    if (!alive || TargetDoomed(u->attackTargetIsBuilding, u->attackTarget))
    {
        // Switch right away (same tick) to the nearest enemy worth attacking.
        if (!AttackNearest(id, SearchRadius(u)))
        {
            if (alive) return none;   // only a doomed target left: hold fire, it's dying anyway

            // Nothing nearby: an attack-moving unit carries on to its
            // destination; anyone else goes idle and keeps scanning.
            u->attacking = false;
            if (u->attackMove) UnitMoveTo(id, u->attackMoveDest);
            else UnitStop(id);
            return none;
        }
    }

    bool isBuilding = u->attackTargetIsBuilding;
    int target = u->attackTarget;
    const UnitStats *stats = &UNIT_STATS[u->type];

    // In range: stand still and attack whenever the cooldown allows.
    if (TargetDistance(isBuilding, target, u->pos) <= stats->range)
    {
        if (u->moving) UnitStop(id);
        if (u->cooldownTicks == 0)
        {
            if (u->type == UNIT_RANGED) FireProjectile(u->pos, isBuilding, target, u->attackTargetSerial, stats->damage);
            else DealDamage(isBuilding, target, stats->damage);
            u->cooldownTicks = (int)(stats->cooldown*TICK_RATE);
        }
        return none;
    }

    // Out of range while holding: let it go (the next scan finds anything in range).
    if (u->holdPosition)
    {
        u->attacking = false;
        return none;
    }

    // Out of range: chase. Re-plan every CHASE_RETHINK_TICKS.
    // Units are chased at their centre; buildings at an open spot by the wall.
    Vector2 goal = isBuilding ? BuildingApproachPoint(target, u->pos, u->radius) : units[target].pos;
    if (--u->chaseTicks <= 0)
    {
        u->chaseTicks = CHASE_RETHINK_TICKS;
        u->chaseDirect = MapLineClear(u->pos, goal, u->radius);
        if (u->chaseDirect)
        {
            if (u->moving) UnitStop(id);
        }
        else if (!u->moving || Vector2Distance(u->target, goal) > TILE_SIZE)
        {
            UnitMoveTo(id, goal);   // path only when needed, and only if the target moved
        }
    }

    if (u->chaseDirect) return UnitStepToward(id, goal);
    return u->moving ? UnitFollowPath(id) : none;
}

void CombatAcquireTick(int id)
{
    Unit *u = &units[id];
    if (u->type == UNIT_WORKER) return;   // workers only fight when told to
    if (--u->acquireTicks > 0) return;
    u->acquireTicks = COMBAT_ACQUIRE_TICKS;

    AttackNearest(id, SearchRadius(u));
}

// Projectiles home in on their target. If it dies first, they vanish.
void CombatProjectilesTick(void)
{
    float maxStep = PROJECTILE_SPEED*TICK_DT;
    for (int i = 0; i < MAX_PROJECTILES; i++)
    {
        Projectile *p = &projectiles[i];
        if (!p->active) continue;
        p->prevPos = p->pos;

        bool hit = false;
        bool targetAlive = TargetAlive(p->targetIsBuilding, p->target, p->targetSerial);
        if (targetAlive)
        {
            float dist = TargetDistance(p->targetIsBuilding, p->target, p->pos);
            if (dist <= maxStep) hit = true;
            else
            {
                // Fly at the unit, or at the building's centre (it hits the wall first).
                Vector2 aim = p->targetIsBuilding ? BuildingCentre(p->target) : units[p->target].pos;
                Vector2 to = Vector2Subtract(aim, p->pos);
                p->pos = Vector2Add(p->pos, Vector2Scale(to, maxStep/Vector2Length(to)));
                continue;
            }
        }

        if (targetAlive) *TargetIncoming(p->targetIsBuilding, p->target) -= p->damage;   // no longer in flight
        if (hit) DealDamage(p->targetIsBuilding, p->target, p->damage);
        p->active = false;
        projectileCount--;
    }
}

void CombatProjectilesDraw(Rectangle view, float alpha)
{
    for (int i = 0; i < MAX_PROJECTILES; i++)
    {
        const Projectile *p = &projectiles[i];
        if (!p->active) continue;
        Vector2 pos = Vector2Lerp(p->prevPos, p->pos, alpha);
        if (!CheckCollisionPointRec(pos, view)) continue;   // off screen
        DrawCircleSector(pos, PROJECTILE_RADIUS, 0.0f, 360.0f, 6, PROJECTILE_COLOR);
    }
}

int CombatProjectileCount(void)
{
    return projectileCount;
}

void CombatReset(void)
{
    memset(projectiles, 0, sizeof(projectiles));
    projectileCount = 0;
}
