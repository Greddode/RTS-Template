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
// Targets are remembered as (index, serial). When a unit dies its pool slot
// can be reused by a new unit; the serial tells them apart, so nobody ends up
// attacking the newcomer by mistake.
//
// Auto-targeting: idle and attack-moving units look for the nearest enemy within
// COMBAT_AGGRO_RADIUS using the spatial grid, and attack it.
//
// Overkill: each unit tracks `incomingDamage`, the damage in projectiles
// already flying at it. Once that's enough to kill it, the unit is "doomed":
// nobody fires at it or picks it as a target, so ranged units don't waste
// their cooldown on shots that will hit nothing. When a target dies or
// becomes doomed, the attacker picks the next one on the same tick.

#include "combat.h"
#include "config.h"
#include "grid.h"
#include "map.h"
#include "units.h"
#include "raymath.h"

#define CHASE_RETHINK_TICKS 10                   // how often a chasing unit re-plans (3x per second)
#define PROJECTILE_SPEED    320.0f               // world px per second
#define PROJECTILE_RADIUS   2.0f
#define PROJECTILE_COLOR    (Color){ 255, 240, 200, 255 }

typedef struct Projectile {
    bool         active;
    Vector2      pos, prevPos;
    int          target;
    unsigned int targetSerial;
    float        damage;
} Projectile;

static Projectile projectiles[MAX_PROJECTILES];
static int projectileCount = 0;

static bool IsDoomed(int id)
{
    return units[id].hp <= units[id].incomingDamage;
}

static void DealDamage(int target, float damage)
{
    units[target].hp -= damage;
    if (units[target].hp <= 0.0f) UnitDespawn(target);   // frees the slot, clears selection
}

// Fire at a target. Uses a free projectile slot; if the pool is full (very
// unlikely), the damage lands immediately instead of being lost.
static void FireProjectile(Vector2 from, int target, float damage)
{
    for (int i = 0; i < MAX_PROJECTILES; i++)
    {
        if (projectiles[i].active) continue;
        projectiles[i] = (Projectile){
            .active = true,
            .pos = from, .prevPos = from,
            .target = target,
            .targetSerial = units[target].serial,
            .damage = damage,
        };
        units[target].incomingDamage += damage;
        projectileCount++;
        return;
    }
    DealDamage(target, damage);
}

Vector2 CombatUnitTick(int id)
{
    Unit *u = &units[id];
    Vector2 none = { 0 };

    bool alive = UnitIsAlive(u->attackTarget, u->attackTargetSerial);
    if (!alive || IsDoomed(u->attackTarget))
    {
        // Switch right away (same tick) to the nearest enemy worth attacking.
        int next = GridFindNearestEnemy(u->pos, COMBAT_AGGRO_RADIUS, u->team);
        if (next != -1) UnitsOrderAttack(&id, 1, next);
        else if (!alive)
        {
            // Nothing nearby: an attack-moving unit carries on to its
            // destination; anyone else goes idle and keeps scanning.
            u->attacking = false;
            if (u->attackMove) UnitMoveTo(id, u->attackMoveDest);
            else UnitStop(id);
            return none;
        }
        else return none;           // only a doomed target left: hold fire, it's dying anyway
    }

    const Unit *t = &units[u->attackTarget];
    const UnitStats *stats = &UNIT_STATS[u->type];

    // In range: stand still and attack whenever the cooldown allows.
    if (Vector2Distance(u->pos, t->pos) <= stats->range)
    {
        if (u->moving) UnitStop(id);
        if (u->cooldownTicks == 0)
        {
            if (u->type == UNIT_RANGED) FireProjectile(u->pos, u->attackTarget, stats->damage);
            else DealDamage(u->attackTarget, stats->damage);
            u->cooldownTicks = (int)(stats->cooldown*TICK_RATE);
        }
        return none;
    }

    // Out of range: chase. Re-plan every CHASE_RETHINK_TICKS.
    if (--u->chaseTicks <= 0)
    {
        u->chaseTicks = CHASE_RETHINK_TICKS;
        u->chaseDirect = MapLineClear(u->pos, t->pos, u->radius);
        if (u->chaseDirect)
        {
            if (u->moving) UnitStop(id);
        }
        else if (!u->moving || Vector2Distance(u->target, t->pos) > TILE_SIZE)
        {
            UnitMoveTo(id, t->pos);   // path only when needed, and only if the target moved
        }
    }

    if (u->chaseDirect) return UnitStepToward(id, t->pos);
    return u->moving ? UnitFollowPath(id) : none;
}

void CombatAcquireTick(int id)
{
    Unit *u = &units[id];
    if (--u->acquireTicks > 0) return;
    u->acquireTicks = COMBAT_ACQUIRE_TICKS;

    int enemy = GridFindNearestEnemy(u->pos, COMBAT_AGGRO_RADIUS, u->team);
    if (enemy != -1) UnitsOrderAttack(&id, 1, enemy);
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
        bool targetAlive = UnitIsAlive(p->target, p->targetSerial);
        if (targetAlive)
        {
            Vector2 to = Vector2Subtract(units[p->target].pos, p->pos);
            float dist = Vector2Length(to);
            if (dist <= maxStep) hit = true;
            else
            {
                p->pos = Vector2Add(p->pos, Vector2Scale(to, maxStep/dist));
                continue;
            }
        }

        if (targetAlive) units[p->target].incomingDamage -= p->damage;   // no longer in flight
        if (hit) DealDamage(p->target, p->damage);
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
