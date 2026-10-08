// heal.c - Healers: units with canHeal in UNIT_STATS (the Medic).
//
// A healer never attacks. Instead, when idle or attack-moving, it looks for
// the nearest damaged ally within HEAL_SEARCH_RADIUS (spatial grid query),
// walks into healRange and adds healRate HP per second, every tick, up to the
// target's max HP. A plain move order ignores healing (like it ignores enemies).
// Hold position: only allies already within healRange, and no walking.
// Leash: like combat.c, an idle healer that walks off to heal remembers where
// it stood, gives up past COMBAT_LEASH_TILES, and walks back when done.
//
// UnitNeedsHealing() is the one rule for "damaged". A target is dropped the
// moment it dies, is full, leaves the search radius or walks into fog, and the
// next one is picked in the same tick. A right click on a damaged ally
// (HealOrderFollow) follows that ally anywhere until it's full or dead.
//
// Spreading out: every healed unit is stamped with the tick it was healed.
// A healer looking for a patient prefers allies nobody healed this tick or
// the last one, and only shares a target when there's no other. Picking a
// target stamps it at once, so the next healer in the same tick looks elsewhere.

#include "heal.h"
#include "combat.h"
#include "config.h"
#include "fog.h"
#include "grid.h"
#include "map.h"
#include "units.h"
#include "raymath.h"

#define HEAL_CHASE_RETHINK_TICKS 10                        // walking to a patient: re-plan 3x per second
#define HEAL_LINE_COLOR          (Color){ 80, 255, 120, 255 }
#define HEAL_DRAW_MARGIN         128.0f                    // view margin, so lines from off-screen healers show

static unsigned int tickStamp = 2;               // goes up once per sim tick
static unsigned int healedStamp[MAX_UNITS];      // tick each unit was last healed (or picked) by a healer

bool UnitNeedsHealing(int id)
{
    return id >= 0 && id < MAX_UNITS && units[id].active && units[id].hp < UNIT_STATS[units[id].type].hp;
}

void HealBeginTick(void)
{
    tickStamp++;
}

static bool BeingHealed(int id)
{
    return healedStamp[id] + 1 >= tickStamp;   // this tick or the last one
}

// Can healer `hid` heal unit `id`? A damaged ally (not itself) its team can see.
static bool ValidPatient(int hid, int id)
{
    const Unit *h = &units[hid];
    return id != hid && UnitNeedsHealing(id) && units[id].team == h->team && FogCanSee(h->team, units[id].pos);
}

// How far it looks: the search radius, or only its heal range when holding.
static float SearchRadius(const Unit *u)
{
    return u->holdPosition ? UNIT_STATS[u->type].healRange : HEAL_SEARCH_RADIUS;
}

// Nearest damaged ally within `radius`, preferring one nobody else is healing.
static int FindPatient(int hid, float radius)
{
    static int near[MAX_UNITS];
    const Unit *h = &units[hid];
    Rectangle area = { h->pos.x - radius, h->pos.y - radius, radius*2.0f, radius*2.0f };
    int count = GridQuery(area, near, MAX_UNITS);

    int best = -1, shared = -1;
    float bestDist = 0.0f, sharedDist = 0.0f;
    for (int k = 0; k < count; k++)
    {
        int id = near[k];
        if (!ValidPatient(hid, id)) continue;
        float d = Vector2Distance(h->pos, units[id].pos);
        if (d > radius) continue;
        if (BeingHealed(id)) { if (shared == -1 || d < sharedDist) { shared = id; sharedDist = d; } }
        else if (best == -1 || d < bestDist) { best = id; bestDist = d; }
    }
    return (best != -1) ? best : shared;
}

static void StartHealing(int hid, int target, bool ordered)
{
    Unit *u = &units[hid];
    u->healing = true;
    u->healOrdered = ordered;
    u->healTarget = target;
    u->healTargetSerial = units[target].serial;
    u->chaseDirect = false;
    u->chaseTicks = 0;                 // decide how to reach it on its very next tick
    healedStamp[target] = tickStamp;   // claimed: the next healer this tick looks elsewhere
}

// Nobody left to heal: carry on with the attack-move, walk back to where the
// leash started, or just stand.
static void StopHealing(int id)
{
    Unit *u = &units[id];
    u->healing = false;
    u->healOrdered = false;
    if (u->attackMove) UnitMoveTo(id, u->attackMoveDest);
    else if (u->leashed) { u->leashed = false; UnitMoveTo(id, u->leashHome); }
    else UnitStop(id);
}

void HealAcquireTick(int id)
{
    Unit *u = &units[id];
    if (--u->acquireTicks > 0) return;   // staggered, like combat's enemy scan
    u->acquireTicks = COMBAT_ACQUIRE_TICKS;

    int patient = FindPatient(id, SearchRadius(u));
    if (patient == -1) return;
    if (!u->attackMove && !u->holdPosition && !u->leashed)   // idle: this walk is on a leash
    {
        u->leashed = true;
        u->leashHome = u->pos;
    }
    if (u->moving) UnitStop(id);   // attack-moving: pause; attackMoveDest is kept
    StartHealing(id, patient, false);
}

Vector2 HealUnitTick(int id)
{
    Unit *u = &units[id];
    const UnitStats *s = &UNIT_STATS[u->type];
    Vector2 none = { 0 };

    // Still a patient? (A followed one is kept at any distance.)
    int t = u->healTarget;
    bool valid = UnitIsAlive(t, u->healTargetSerial) && ValidPatient(id, t);
    if (valid && !u->healOrdered && Vector2Distance(u->pos, units[t].pos) > SearchRadius(u)) valid = false;
    if (!valid)
    {
        int next = u->healOrdered ? -1 : FindPatient(id, SearchRadius(u));   // switch in the same tick
        if (next == -1) { StopHealing(id); return none; }
        StartHealing(id, next, false);
        t = next;
    }

    // In range: stand still and heal, never above max HP.
    float dist = Vector2Distance(u->pos, units[t].pos);
    if (dist <= s->healRange)
    {
        if (u->moving) UnitStop(id);
        Unit *p = &units[t];
        float maxHp = UNIT_STATS[p->type].hp;
        p->hp += s->healRate*TICK_DT;
        healedStamp[t] = tickStamp;
        if (p->hp >= maxHp)   // full now: line up the next patient in this same tick
        {
            p->hp = maxHp;
            int next = u->healOrdered ? -1 : FindPatient(id, SearchRadius(u));
            if (next == -1) StopHealing(id);
            else StartHealing(id, next, false);
        }
        return none;
    }

    // Walked off on its own and too far from home: give up and walk back.
    if (u->leashed && Vector2Distance(u->pos, u->leashHome) > COMBAT_LEASH_TILES*TILE_SIZE)
    {
        u->healing = false;
        u->leashed = false;
        UnitMoveTo(id, u->leashHome);
        return none;
    }

    // Out of range: walk to the patient (straight if the line is clear, else a path).
    Vector2 goal = units[t].pos;
    if (--u->chaseTicks <= 0)
    {
        u->chaseTicks = HEAL_CHASE_RETHINK_TICKS;
        u->chaseDirect = MapLineClear(UnitMoveClass(u), u->pos, goal, u->radius);
        if (u->chaseDirect) { if (u->moving) UnitStop(id); }
        else if (!u->moving || Vector2Distance(u->target, goal) > TILE_SIZE) UnitMoveTo(id, goal);
    }
    if (u->chaseDirect) return UnitStepToward(id, goal);
    return u->moving ? UnitFollowPath(id) : none;
}

void HealOrderFollow(const int *ids, int count, int target)
{
    if (!UnitNeedsHealing(target)) return;
    for (int k = 0; k < count; k++)
    {
        int id = ids[k];
        if (!UNIT_STATS[units[id].type].canHeal || !ValidPatient(id, target)) continue;
        UnitsOrderStop(&id, 1);   // drops every other order (and hold)
        StartHealing(id, target, true);
    }
}

// One pass, lines only (all in one batch): healer -> patient, while it's
// actually healing (in range) and both ends are visible to the player.
void HealDraw(Rectangle view, float alpha)
{
    static int near[MAX_UNITS];
    Rectangle area = { view.x - HEAL_DRAW_MARGIN, view.y - HEAL_DRAW_MARGIN, view.width + 2.0f*HEAL_DRAW_MARGIN, view.height + 2.0f*HEAL_DRAW_MARGIN };
    int count = GridQuery(area, near, MAX_UNITS);
    for (int k = 0; k < count; k++)
    {
        const Unit *u = &units[near[k]];
        if (!u->healing || !UnitIsAlive(u->healTarget, u->healTargetSerial)) continue;
        const Unit *p = &units[u->healTarget];
        if (Vector2Distance(u->pos, p->pos) > UNIT_STATS[u->type].healRange) continue;   // still walking there
        if (!FogCanSee(PLAYER_TEAM, u->pos) || !FogCanSee(PLAYER_TEAM, p->pos)) continue;
        DrawLineV(Vector2Lerp(u->prevPos, u->pos, alpha), Vector2Lerp(p->prevPos, p->pos, alpha), HEAL_LINE_COLOR);
    }
}
