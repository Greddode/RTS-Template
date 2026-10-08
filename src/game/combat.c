// combat.c - Attacking, auto-targeting and projectiles.
//
// Attack order: the unit chases its target until it's within range (from
// UNIT_STATS), then hits it every `cooldown` seconds. Melee hits land at once;
// ranged units' hits (reach over ARROW_MIN_RANGE: Archer, Scout) spawn an
// arrow that flies to the target.
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
// Leash: an idle unit that picks a target on its own remembers where it was
// standing. If the chase takes it more than COMBAT_LEASH_TILES from there, it
// gives up and walks back (a plain move: units only look for new targets
// when standing still, so it doesn't turn round half way). It also walks back
// once the fight is over. Picking the next target after a kill keeps the same
// home spot. Orders from the player or AI and attack-move aren't leashed.
//
// Auto-targeting: idle and attack-moving units look for the nearest enemy within
// COMBAT_AGGRO_RADIUS using the spatial grid, and attack it. Enemy units come
// first; enemy buildings only when no unit is in reach. Workers don't
// auto-attack.
//
// What a unit can hit (hitsGround / hitsAir in UNIT_STATS; UnitCanHitUnit in
// units.h): flyers only by hitsAir types, everything else (ground units,
// boats, buildings) only by hitsGround types. Every place below that picks,
// keeps or damages a target asks it, so a Knight never targets, chases or
// hurts a Falcon, and an Airship's bombs pass through flyers.
//
// Damage: CombatDamage() turns a hit's base damage into what the target
// actually loses, using its armor (the damage/armor table in config.h). It
// runs once, when the attack happens: melee damage lands at once, and a
// projectile carries the already-reduced number, so "incoming damage" below
// is exact. Buildings take the plain base damage.
//
// Splash (Mage, Airship): a unit with splashRadius fires a slow bolt (magic) or
// bomb (other damage types) at the spot its target stood on when it fired.
// Whatever is near that spot when it lands, and that the shooter could hit,
// gets hit, so a unit that walks away dodges it, and friends nearby get hit
// too (friendly fire is intended). Damage falls off from the centre to
// splashFalloff x at the edge, then goes through CombatDamage() per victim.
// The AI's splash units hold fire while their own units are in the splash.
//
// minRange: such a unit never fires at a target closer than that. It picks a
// target further out, or backs off to the middle of its range band (between
// minRange and range), so it can't flip between "too close" and "too far".
//
// Projectile pool full (rare): the shot is skipped, with one log line.
//
// Overkill: each unit tracks `incomingDamage`, the damage in projectiles
// already flying at it. Once that's enough to kill it, the unit is "doomed":
// nobody fires at it or picks it as a target, so archers don't waste
// their cooldown on shots that will hit nothing. When a target dies or
// becomes doomed, the attacker picks the next one on the same tick.

#include "combat.h"
#include "buildings.h"
#include "config.h"
#include "fog.h"
#include "grid.h"
#include "map.h"
#include "units.h"
#include "raymath.h"
#include <string.h>

#define CHASE_RETHINK_TICKS 10                   // how often a chasing unit re-plans (3x per second)
#define ARROW_MIN_RANGE     32.0f                // units that reach further than this shoot arrows (Archer, Scout)
#define PROJECTILE_SPEED    320.0f               // world px per second
#define PROJECTILE_RADIUS   2.0f
#define PROJECTILE_COLOR    (Color){ 255, 240, 200, 255 }
#define BOLT_SPEED          220.0f               // magic bolts: slow enough to dodge
#define BOLT_RADIUS         3.0f
#define BOLT_TRAIL          3                    // trail dots behind a bolt
#define BOLT_COLOR          (Color){ 150, 120, 255, 255 }
#define BOMB_COLOR          (Color){ 60, 55, 50, 255 }   // non-magic splash (Airship bombs)
#define SPLASH_FX_TICKS     9                    // the ring grows for 0.3 s
#define SPLASH_RING_WIDTH   2.0f
#define MAX_SPLASH_FX       128
#define SPLASH_QUERY_MARGIN (UNIT_RADIUS + 8.0f) // grid positions are up to a tick old

typedef struct Projectile {
    bool         active;
    Vector2      pos, prevPos;
    int          target;
    unsigned int targetSerial;
    bool         targetIsBuilding;
    float        damage;
    bool         bolt;      // magic bolt: flies to `land` and splashes there
    Vector2      land;      // where its target stood when it was fired
    UnitType     shooter;   // whose stats the splash uses
} Projectile;

static Projectile projectiles[MAX_PROJECTILES];
static int projectileCount = 0;
static bool poolFullLogged = false;

// Expanding rings where bolts landed (visual only).
typedef struct SplashFx {
    bool    active;
    Vector2 pos;
    float   radius;
    int     age;   // ticks
    Color   color; // the bolt's or bomb's
} SplashFx;
static SplashFx splashFx[MAX_SPLASH_FX];

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

static bool TargetVisible(const Unit *u)
{
    if (u->attackTargetIsBuilding) return FogCanSeeRect(u->team, BuildingRect(u->attackTarget));
    return FogCanSee(u->team, units[u->attackTarget].pos);
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

// Where a shot at this target lands: a unit's centre, a building's nearest wall.
static Vector2 LandingPoint(bool isBuilding, int id, Vector2 from)
{
    if (!isBuilding) return units[id].pos;
    Rectangle r = BuildingRect(id);
    return (Vector2){ Clamp(from.x, r.x, r.x + r.width), Clamp(from.y, r.y, r.y + r.height) };
}

// The AI's splash units don't fire while one of their own units is in the splash.
// (The player's do: friendly fire is the player's call.)
static bool FriendsInSplash(const Unit *u, Vector2 at)
{
    float r = UNIT_STATS[u->type].splashRadius;
    if (u->team != AI_TEAM || r <= 0.0f) return false;
    static int near[MAX_UNITS];
    float q = r + SPLASH_QUERY_MARGIN;
    int count = GridQuery((Rectangle){ at.x - q, at.y - q, q*2.0f, q*2.0f }, near, MAX_UNITS);
    for (int k = 0; k < count; k++)
        if (units[near[k]].team == u->team && UnitCanHitUnit(u->type, &units[near[k]]) && Vector2Distance(units[near[k]].pos, at) <= r) return true;
    return false;
}

// "Picky" units choose targets themselves: anyone with a minRange, and the
// AI's splash units (they check for friends). Everyone else takes the nearest enemy.
static bool Picky(const Unit *u)
{
    const UnitStats *s = &UNIT_STATS[u->type];
    return s->minRange > 0.0f || (s->splashRadius > 0.0f && u->team == AI_TEAM);
}

// Nearest enemy unit within maxDist that a picky unit may fire at: visible,
// not doomed, one it can hit, not closer than its minRange, and no friends in the splash.
static int FindTarget(const Unit *u, float maxDist)
{
    static int near[MAX_UNITS];
    float minRange = UNIT_STATS[u->type].minRange;
    int count = GridQuery((Rectangle){ u->pos.x - maxDist, u->pos.y - maxDist, maxDist*2.0f, maxDist*2.0f }, near, MAX_UNITS);
    int best = -1;
    float bestDist = 0.0f;
    for (int k = 0; k < count; k++)
    {
        const Unit *t = &units[near[k]];
        if (t->team == u->team || !FogCanSee(u->team, t->pos) || t->hp <= t->incomingDamage) continue;
        if (!UnitCanHitUnit(u->type, t)) continue;   // a flyer, for a unit without hitsAir (and so on)
        float d = Vector2Distance(u->pos, t->pos);
        if (d > maxDist || d < minRange || (best != -1 && d >= bestDist)) continue;
        if (FriendsInSplash(u, t->pos)) continue;
        best = near[k];
        bestDist = d;
    }
    return best;
}

// The one damage formula. See the damage/armor table in config.h.
float CombatDamage(float base, DamageType type, ArmorType armorType, float armor)
{
    float dealt = base*DAMAGE_VS_ARMOR[type][armorType] - armor;
    float least = base*DAMAGE_MIN_FRACTION;   // armor never blocks a hit completely
    return (dealt > least) ? dealt : least;
}

// What one hit from unit u does to its target.
static float HitDamage(const Unit *u, bool isBuilding, int target)
{
    const UnitStats *a = &UNIT_STATS[u->type];
    if (isBuilding) return a->damage;
    const UnitStats *t = &UNIT_STATS[units[target].type];
    return CombatDamage(a->damage, a->damageType, t->armorType, t->armor);
}

// Attack the nearest enemy worth attacking within `radius`: units first, then
// buildings. Returns false if there's nothing to attack. An auto-target: the
// unit's leash (if any) is kept, since the order itself clears it.
static bool AttackNearest(int id, float radius)
{
    Unit *u = &units[id];
    bool leashed = u->leashed;
    Vector2 home = u->leashHome;
    bool picky = Picky(u);
    int enemy = picky ? FindTarget(u, radius) : GridFindNearestEnemy(u->pos, radius, u->team, UNIT_STATS[u->type].hitsGround, UNIT_STATS[u->type].hitsAir);
    int building = (enemy == -1 && UnitCanHitBuildings(u->type)) ? BuildingsFindNearestEnemy(u->pos, radius, u->team) : -1;
    if (building != -1 && picky && (BuildingDistance(building, u->pos) < UNIT_STATS[u->type].minRange ||
                                    FriendsInSplash(u, LandingPoint(true, building, u->pos)))) building = -1;
    if (enemy != -1) UnitsOrderAttack(&id, 1, enemy);
    else if (building != -1) UnitsOrderAttackBuilding(&id, 1, building);
    else return false;
    u->leashed = leashed;
    u->leashHome = home;
    return true;
}

// A free projectile slot, or NULL if the pool is full: the shot is then
// skipped (logged once, so a too-small MAX_PROJECTILES is easy to spot).
static Projectile *NewProjectile(void)
{
    for (int i = 0; i < MAX_PROJECTILES; i++)
        if (!projectiles[i].active) { projectileCount++; return &projectiles[i]; }
    if (!poolFullLogged)
    {
        TraceLog(LOG_WARNING, "COMBAT: projectile pool full (MAX_PROJECTILES %d) - shots are skipped until some land", MAX_PROJECTILES);
        poolFullLogged = true;
    }
    return NULL;
}

// An arrow: homes in on its target.
static void FireProjectile(Vector2 from, bool isBuilding, int target, unsigned int serial, float damage)
{
    Projectile *p = NewProjectile();
    if (p == NULL) return;
    *p = (Projectile){
        .active = true,
        .pos = from, .prevPos = from,
        .target = target,
        .targetSerial = serial,
        .targetIsBuilding = isBuilding,
        .damage = damage,
    };
    *TargetIncoming(isBuilding, target) += damage;
}

// A magic bolt: flies to a fixed spot and splashes there.
static void FireBolt(const Unit *u, Vector2 land)
{
    Projectile *p = NewProjectile();
    if (p == NULL) return;
    *p = (Projectile){ .active = true, .pos = u->pos, .prevPos = u->pos, .bolt = true, .land = land, .shooter = u->type };
}

// A bolt landed: hit every unit (any team) and building within the splash
// that the shooter could hit (an Airship's bombs pass through flyers).
static void Splash(Vector2 at, UnitType shooter)
{
    const UnitStats *s = &UNIT_STATS[shooter];
    float r = s->splashRadius;
    static int near[MAX_UNITS];
    float q = r + SPLASH_QUERY_MARGIN;
    int count = GridQuery((Rectangle){ at.x - q, at.y - q, q*2.0f, q*2.0f }, near, MAX_UNITS);
    for (int k = 0; k < count; k++)
    {
        const Unit *t = &units[near[k]];
        float d = Vector2Distance(t->pos, at);
        if (!t->active || d > r || !UnitCanHitUnit(shooter, t)) continue;
        float base = s->damage*(1.0f - (1.0f - s->splashFalloff)*d/r);   // full at the centre, falloff x at the edge
        DealDamage(false, near[k], CombatDamage(base, s->damageType, UNIT_STATS[t->type].armorType, UNIT_STATS[t->type].armor));
    }
    for (int b = 0; b < MAX_BUILDINGS && UnitCanHitBuildings(shooter); b++)   // the building pool is small; distance to the nearest wall
    {
        if (!buildings[b].active) continue;
        float d = BuildingDistance(b, at);
        if (d <= r) DealDamage(true, b, s->damage*(1.0f - (1.0f - s->splashFalloff)*d/r));
    }
    for (int i = 0; i < MAX_SPLASH_FX; i++)
        if (!splashFx[i].active) { splashFx[i] = (SplashFx){ true, at, r, 0, (s->damageType == DAMAGE_MAGIC) ? BOLT_COLOR : BOMB_COLOR }; break; }
}

// The target is inside minRange: pick something further out, or back off to
// the middle of the range band. A holding unit just lets the target go.
static Vector2 TooClose(int id, bool isBuilding, int target)
{
    Unit *u = &units[id];
    const UnitStats *s = &UNIT_STATS[u->type];
    Vector2 none = { 0 };
    if (u->holdPosition) { u->attacking = false; return none; }

    Vector2 from = LandingPoint(isBuilding, target, u->pos);
    Vector2 away = Vector2Subtract(u->pos, from);
    away = (Vector2Length(away) > 0.01f) ? Vector2Normalize(away) : (Vector2){ 1.0f, 0.0f };
    Vector2 spot = Vector2Add(from, Vector2Scale(away, (s->minRange + s->range)*0.5f));
    if (--u->chaseTicks <= 0)
    {
        u->chaseTicks = CHASE_RETHINK_TICKS;
        if (AttackNearest(id, SearchRadius(u))) return none;   // something it can fire at
        u->chaseDirect = MapLineClear(UnitMoveClass(u), u->pos, spot, u->radius);
        if (u->chaseDirect) { if (u->moving) UnitStop(id); }
        else UnitMoveTo(id, spot);
    }
    if (u->chaseDirect) return UnitStepToward(id, spot);
    return u->moving ? UnitFollowPath(id) : none;
}

Vector2 CombatUnitTick(int id)
{
    Unit *u = &units[id];
    Vector2 none = { 0 };
    if (UNIT_STATS[u->type].damage <= 0.0f) { u->attacking = false; return none; }   // can't attack: never chase

    // A target that walked into the fog counts as gone: you can't chase what you
    // can't see. So does one it can't hit (orders already filter those out;
    // this keeps it true whatever set the target).
    bool alive = TargetAlive(u->attackTargetIsBuilding, u->attackTarget, u->attackTargetSerial) && TargetVisible(u) &&
                 (u->attackTargetIsBuilding ? UnitCanHitBuildings(u->type) : UnitCanHitUnit(u->type, &units[u->attackTarget]));
    if (!alive || TargetDoomed(u->attackTargetIsBuilding, u->attackTarget))
    {
        // Switch right away (same tick) to the nearest enemy worth attacking.
        if (!AttackNearest(id, SearchRadius(u)))
        {
            if (alive) return none;   // only a doomed target left: hold fire, it's dying anyway

            // Nothing nearby: an attack-moving unit carries on to its
            // destination, a leashed one walks back home; anyone else goes
            // idle and keeps scanning.
            u->attacking = false;
            if (u->attackMove) UnitMoveTo(id, u->attackMoveDest);
            else if (u->leashed) { u->leashed = false; UnitMoveTo(id, u->leashHome); }
            else UnitStop(id);
            return none;
        }
    }

    bool isBuilding = u->attackTargetIsBuilding;
    int target = u->attackTarget;
    const UnitStats *stats = &UNIT_STATS[u->type];

    float dist = TargetDistance(isBuilding, target, u->pos);
    if (dist < stats->minRange) return TooClose(id, isBuilding, target);

    // In range: stand still and attack whenever the cooldown allows.
    if (dist <= stats->range)
    {
        if (u->moving) UnitStop(id);
        if (u->cooldownTicks == 0)
        {
            if (stats->splashRadius > 0.0f)
            {
                Vector2 land = LandingPoint(isBuilding, target, u->pos);
                if (FriendsInSplash(u, land))   // AI: own units there; look for a safe target now and then
                {
                    if (--u->chaseTicks <= 0) { u->chaseTicks = CHASE_RETHINK_TICKS; AttackNearest(id, SearchRadius(u)); }
                    return none;
                }
                FireBolt(u, land);
            }
            else
            {
                float damage = HitDamage(u, isBuilding, target);
                if (stats->range > ARROW_MIN_RANGE) FireProjectile(u->pos, isBuilding, target, u->attackTargetSerial, damage);
                else DealDamage(isBuilding, target, damage);
            }
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

    // Chasing on its own and too far from home: give up and walk back.
    if (u->leashed && Vector2Distance(u->pos, u->leashHome) > COMBAT_LEASH_TILES*TILE_SIZE)
    {
        u->attacking = false;
        u->leashed = false;
        UnitMoveTo(id, u->leashHome);
        return none;
    }

    // Out of range: chase. Re-plan every CHASE_RETHINK_TICKS.
    // Units are chased at their centre; buildings at an open spot by the wall.
    Vector2 goal = isBuilding ? BuildingApproachPoint(target, u->pos, u->radius, UnitMoveClass(u)) : units[target].pos;
    if (--u->chaseTicks <= 0)
    {
        u->chaseTicks = CHASE_RETHINK_TICKS;
        u->chaseDirect = MapLineClear(UnitMoveClass(u), u->pos, goal, u->radius);
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
    if (UNIT_STATS[u->type].damage <= 0.0f) return;   // healers / no attack: never pick a target
    if (--u->acquireTicks > 0) return;
    u->acquireTicks = COMBAT_ACQUIRE_TICKS;

    // Standing idle (not attack-moving, not holding): this chase is on a leash.
    bool idle = !u->attackMove && !u->holdPosition;
    Vector2 here = u->pos;
    if (AttackNearest(id, SearchRadius(u)) && idle)
    {
        u->leashed = true;
        u->leashHome = here;
    }
}

// Projectiles home in on their target. If it dies first, they vanish.
void CombatProjectilesTick(void)
{
    for (int i = 0; i < MAX_SPLASH_FX; i++)
        if (splashFx[i].active && ++splashFx[i].age >= SPLASH_FX_TICKS) splashFx[i].active = false;

    float maxStep = PROJECTILE_SPEED*TICK_DT, boltStep = BOLT_SPEED*TICK_DT;
    for (int i = 0; i < MAX_PROJECTILES; i++)
    {
        Projectile *p = &projectiles[i];
        if (!p->active) continue;
        p->prevPos = p->pos;

        if (p->bolt)   // flies to a fixed spot; lands there even if the target has left
        {
            Vector2 to = Vector2Subtract(p->land, p->pos);
            float d = Vector2Length(to);
            if (d > boltStep) { p->pos = Vector2Add(p->pos, Vector2Scale(to, boltStep/d)); continue; }
            p->pos = p->land;
            p->active = false;
            projectileCount--;
            Splash(p->land, p->shooter);
            continue;
        }

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

// Arrows, bolts (with a short trail of dots) and splash rings: all plain
// circles and rings, so one batch. Only what the camera and the player see.
void CombatProjectilesDraw(Rectangle view, float alpha)
{
    for (int i = 0; i < MAX_PROJECTILES; i++)
    {
        const Projectile *p = &projectiles[i];
        if (!p->active) continue;
        Vector2 pos = Vector2Lerp(p->prevPos, p->pos, alpha);
        if (!CheckCollisionPointRec(pos, view)) continue;   // off screen
        if (!FogCanSee(PLAYER_TEAM, pos)) continue;          // hidden by fog
        if (!p->bolt) { DrawCircleSector(pos, PROJECTILE_RADIUS, 0.0f, 360.0f, 6, PROJECTILE_COLOR); continue; }

        Color c = (UNIT_STATS[p->shooter].damageType == DAMAGE_MAGIC) ? BOLT_COLOR : BOMB_COLOR;
        Vector2 back = Vector2Subtract(p->prevPos, p->pos);   // one tick of flight, backwards
        for (int k = BOLT_TRAIL; k >= 1; k--)
        {
            Vector2 dot = Vector2Add(pos, Vector2Scale(back, k*0.5f));
            DrawCircleSector(dot, BOLT_RADIUS*(1.0f - k*0.2f), 0.0f, 360.0f, 6, Fade(c, 0.8f - k*0.2f));
        }
        DrawCircleSector(pos, BOLT_RADIUS, 0.0f, 360.0f, 8, c);
    }
    for (int i = 0; i < MAX_SPLASH_FX; i++)
    {
        const SplashFx *f = &splashFx[i];
        if (!f->active || !CheckCollisionCircleRec(f->pos, f->radius, view)) continue;
        if (!FogCanSee(PLAYER_TEAM, f->pos)) continue;   // the damage happened anyway; only the picture is hidden
        float t = (f->age + alpha)/SPLASH_FX_TICKS;
        float r = f->radius*(0.3f + 0.7f*t);
        DrawRing(f->pos, r - SPLASH_RING_WIDTH, r, 0.0f, 360.0f, 24, Fade(f->color, 1.0f - t));
    }
}

int CombatProjectileCount(void)
{
    return projectileCount;
}

void CombatReset(void)
{
    memset(projectiles, 0, sizeof(projectiles));
    memset(splashFx, 0, sizeof(splashFx));
    projectileCount = 0;
}
