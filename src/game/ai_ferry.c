// ai_ferry.c - The AI ferries its army by Airship when it can't walk to the player.
//
// "Needs transport": every think (ai.c), it looks for a player building that
// its army can reach on foot - one with an open tile next to it in the AI's
// home region (PathRegion). If there is none (the player is on another
// island), the nearest player building (or the player's base spot) becomes the
// target and the ferry logic runs. On maps where the army can walk to the
// player it never does anything, so those games play exactly as before.
//
// Getting ready (AiFerryThink): the Barracks comes from the normal AI. Then it
// builds an Academy and an Air Factory (BuildingsCanBuild, saving gold for each
// the way expansions do, but only when there's room to place it), then trains
// an Airship (UnitsCanTrain). More Airships, up to AI_FERRY_MAX_AIRSHIPS, while
// gold piles up past AI_FERRY_EXTRA_AIRSHIP_GOLD and every Airship is busy.
//
// Each Airship's trip (AiFerryTick, every AI_FERRY_CHECK_TICKS):
//   IDLE    parked by the Air Factory. When transport is needed: pick a group
//           of idle ground fighters (no Workers; Medics only as escorts) and
//           tell them to board (TransportOrderBoard).
//   GATHER  wait until it's full, or AI_FERRY_GATHER_SECONDS have passed with
//           at least AI_FERRY_MIN_CARGO_SLOTS aboard; then pick a drop point
//           and fly there (TransportOrderUnload).
//   FLY     transport.c flies and lets the units out on open ground. Turns
//           back if it loses AI_FERRY_ABORT_DAMAGE of its HP on the way; picks
//           another drop point if enemies that can hit air gather near it
//           (until it's within AI_FERRY_COMMIT_TILES: then it lands anyway).
//   RETURN  back to the parking spot (with any cargo it didn't drop: that
//           gets out at home), then IDLE again.
// Dropped units are ordinary idle units: the normal AI sends them at the
// player units and buildings they can now reach.
//
// Drop point: a spiral search outward from the target for an open ground tile
// in the target's region, further than a known tower's range (plus a margin),
// with few enemy units near it, and where neither the tile nor the straight
// flight line to it is within range (+ AI_FERRY_DANGER_TILES) of an enemy that
// can hit air. If nothing near the target is safe (its army stands there), the
// same spiral runs round the shore of the target's island nearest the Airship:
// the units land there and walk. "Known" and "near" use the AI's fog rules
// (FogCanSee; AI_SEES_THROUGH_FOG applies). transport.c never lets cargo out
// on water, rock or lava.
//
// Losing an Airship counts as a failure: it waits AI_FERRY_RETRY_SECONDS
// before training a new one, and stops ferrying after AI_FERRY_MAX_FAILURES.
//
// AI_FERRY_EXPANSION (off by default): an idle Airship may also carry a Worker
// to a gold field in another region; it builds a Base there (ai.c treats it
// like any other expansion).

#include "ai_internal.h"
#include "buildings.h"
#include "economy.h"
#include "fog.h"
#include "grid.h"
#include "map.h"
#include "path.h"
#include "transport.h"
#include "units.h"
#include "raymath.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef enum { FERRY_IDLE, FERRY_GATHER, FERRY_FLY, FERRY_RETURN } FerryState;

typedef struct Ferry {
    int          ship;            // the Airship: slot...
    unsigned int serial;          // ...and serial
    FerryState   state;
    int          stateTicks;      // ticks in this state
    Vector2      drop;            // where it's flying to unload
    Vector2      target;          // what this trip heads for...
    int          targetRegion;    // ...and the ground region the cargo must land in
    float        tripStartHp;     // its HP when the trip began
    int          cargoAtStart;    // units aboard when it left
    bool         expansion;       // this trip carries a Worker to build a Base (AI_FERRY_EXPANSION)
    int          worker;          // ...that one
    unsigned int workerSerial;
    Vector2      baseSpot;
} Ferry;

static Ferry  ferries[AI_FERRY_MAX_AIRSHIPS];
static int    ferryCount;
static bool   needs;              // last think: no player building reachable by ground
static Vector2 target;            // what the ferries carry the army towards
static int    targetRegion;
static bool   saving, building;
static long   retryAt;            // no new Airship before this tick (after a loss)
static int    checkCountdown;
static AiFerryStats stats;
static char   status[96];

void AiFerryReset(void)
{
    ferryCount = 0;
    needs = saving = building = false;
    retryAt = 0;
    checkCountdown = AI_FERRY_CHECK_TICKS;
    stats = (AiFerryStats){ .needsSince = -1, .academyDone = -1, .factoryDone = -1, .firstAirship = -1, .firstLanding = -1 };
    status[0] = '\0';
}

const AiFerryStats *AiFerryGetStats(void) { return &stats; }
bool AiFerryNeedsTransport(void) { return needs; }
bool AiFerrySaving(void) { return saving; }
bool AiFerryBuilding(void) { return building; }
const char *AiFerryStatus(void) { return status; }

static int Region(Vector2 p) { return PathRegion(MOVE_GROUND, p); }

bool AiFerryCanReach(Vector2 from, Vector2 to)
{
    int a = Region(from), b = Region(to);
    return a != 0 && a == b;
}

bool AiFerryOwns(int unit)
{
    for (int k = 0; k < ferryCount; k++) if (ferries[k].ship == unit && UnitIsAlive(unit, ferries[k].serial)) return true;
    return false;
}

bool AiFerryHasIdleShip(void)
{
    for (int k = 0; k < ferryCount; k++) if (ferries[k].state == FERRY_IDLE && TransportUsedSlots(ferries[k].ship) == 0) return true;
    return false;
}

// --- Needs transport? -------------------------------------------------------------------

// The ground region of an open tile next to building b that `want` matches,
// or (want == 0) the first open neighbouring tile's region; 0 if none.
static int RegionNextTo(int b, int want)
{
    const Building *bd = &buildings[b];
    int found = 0;
    for (int y = bd->ty - 1; y <= bd->ty + bd->size; y++)
        for (int x = bd->tx - 1; x <= bd->tx + bd->size; x++)
        {
            if (x >= bd->tx && x < bd->tx + bd->size && y >= bd->ty && y < bd->ty + bd->size) continue;   // its own tiles
            int r = PathRegion(MOVE_GROUND, (Vector2){ (x + 0.5f)*TILE_SIZE, (y + 0.5f)*TILE_SIZE });
            if (r == 0) continue;
            if (want == 0 || r == want) return r;
            if (!found) found = r;
        }
    return (want == 0) ? found : 0;
}

static void CheckNeeds(int anchor)
{
    int home = AiHomeRegion();
    needs = false;
    if (anchor == -1 || home == 0) return;
    Vector2 from = BuildingCentre(anchor);
    int nearest = -1;
    float best = 0.0f;
    for (int b = 0; b < MAX_BUILDINGS; b++)
    {
        if (!buildings[b].active || buildings[b].team == AI_TEAM) continue;
        if (RegionNextTo(b, home)) return;   // the army can walk to this one: no transport needed
        float d = BuildingDistance(b, from);
        if (nearest == -1 || d < best) { nearest = b; best = d; }
    }
    if (nearest != -1) { target = BuildingCentre(nearest); targetRegion = RegionNextTo(nearest, 0); }
    else { target = AiPlayerBase(); targetRegion = Region(target); }   // no buildings left: their base spot
    needs = targetRegion != 0 && targetRegion != home;
    if (needs && stats.needsSince < 0) stats.needsSince = stats.ticks;
}

// --- Getting the Academy, Air Factory and Airships ready ---------------------------------

static int  Find(BuildingType t) { return AiFindOwn(t); }
static bool Finished(int b) { return b != -1 && !buildings[b].constructing; }

// Build one of `type` near the anchor, or keep its construction going. Returns true when it's finished.
static bool Ensure(BuildingType type, int anchor)
{
    int b = Find(type);
    if (Finished(b)) return true;
    bool beingBuilt;
    int worker = AiFreeWorker(b, &beingBuilt);
    if (b != -1)   // under construction: make sure somebody is on it
    {
        snprintf(status, sizeof(status), "Ferry: building %s (%d%%)", BUILDING_STATS[type].name, (int)(BuildingBuildProgress(b)*100));
        if (!beingBuilt && worker != -1) BuildingsOrderConstruct(&worker, 1, b);
        return false;
    }
    if (!BuildingsCanBuild(AI_TEAM, type)) { snprintf(status, sizeof(status), "Ferry: waiting for %s", BUILDING_STATS[BUILDING_STATS[type].requires].name); return false; }
    Vector2 spot;
    if (!BuildingsFindSpot(type, BuildingCentre(anchor), &spot)) { snprintf(status, sizeof(status), "Ferry: no room for %s", BUILDING_STATS[type].name); return false; }   // can't place: don't save for it
    int cost = BUILDING_STATS[type].cost;
    if (EconomyGold(AI_TEAM) < cost) { saving = true; snprintf(status, sizeof(status), "Ferry: saving for %s (%d/%d)", BUILDING_STATS[type].name, EconomyGold(AI_TEAM), cost); return false; }
    if (worker != -1 && AiStartSite(type, worker, anchor) != -1) snprintf(status, sizeof(status), "Ferry: building %s", BUILDING_STATS[type].name);
    return false;
}

static int QueuedAirships(int factory)
{
    int n = 0;
    for (int q = 0; q < buildings[factory].queueCount; q++) n += UNIT_STATS[buildings[factory].queue[q]].cargoCapacity > 0;
    return n;
}

// Our Airships the ferry isn't using yet (just trained, or given by the map) become ferries.
static void ClaimAirships(void)
{
    for (int i = 0; i < MAX_UNITS && ferryCount < AI_FERRY_MAX_AIRSHIPS; i++)
    {
        const Unit *u = &units[i];
        if (!UnitIsActiveInWorld(u) || u->team != AI_TEAM || !IsTransport(i) || AiFerryOwns(i)) continue;
        ferries[ferryCount++] = (Ferry){ .ship = i, .serial = u->serial, .state = FERRY_IDLE };
        if (stats.firstAirship < 0) stats.firstAirship = stats.ticks;
    }
}

// A ferry's Airship died: count it, wait before replacing it.
static void PruneLost(void)
{
    int kept = 0;
    for (int k = 0; k < ferryCount; k++)
    {
        if (UnitIsAlive(ferries[k].ship, ferries[k].serial)) { ferries[kept++] = ferries[k]; continue; }
        stats.lost++;
        retryAt = stats.ticks + (long)AI_FERRY_RETRY_SECONDS*TICK_RATE;
        if (stats.lost >= AI_FERRY_MAX_FAILURES) stats.gaveUp = true;
        if (ferries[k].expansion) AiFerryExpansionFailed();
        TraceLog(LOG_INFO, "AI FERRY: lost an Airship (%d so far)%s", stats.lost, stats.gaveUp ? " - giving up ferrying" : "");
    }
    ferryCount = kept;
}

void AiFerryThink(int anchor)
{
    saving = building = false;
    status[0] = '\0';
    CheckNeeds(anchor);
    PruneLost();
    ClaimAirships();
    if (!needs || anchor == -1) return;
    if (stats.gaveUp) { snprintf(status, sizeof(status), "Ferry: gave up after losing %d Airships", stats.lost); return; }

    // Academy (the Airship needs it), then the Air Factory (needs a Barracks: the normal AI builds that).
    building = true;
    if (!Ensure(BUILDING_ACADEMY, anchor)) return;
    if (stats.academyDone < 0) stats.academyDone = stats.ticks;
    if (!Ensure(BUILDING_AIR_FACTORY, anchor)) return;
    if (stats.factoryDone < 0) stats.factoryDone = stats.ticks;
    building = false;

    // Airships: one, then more while gold piles up and every one is busy.
    int factory = Find(BUILDING_AIR_FACTORY);
    int have = ferryCount + QueuedAirships(factory);
    bool allBusy = true;
    for (int k = 0; k < ferryCount; k++) allBusy &= ferries[k].state != FERRY_IDLE;
    bool want = have == 0 || (have < AI_FERRY_MAX_AIRSHIPS && allBusy && QueuedAirships(factory) == 0 && EconomyGold(AI_TEAM) > AI_FERRY_EXTRA_AIRSHIP_GOLD);
    if (!want) return;
    if (stats.ticks < retryAt) { snprintf(status, sizeof(status), "Ferry: lost an Airship, waiting %lds", (retryAt - stats.ticks)/TICK_RATE); return; }
    if (!UnitsCanTrain(AI_TEAM, UNIT_AIRSHIP)) return;
    if (BuildingQueueTrain(factory, UNIT_AIRSHIP)) { stats.airshipsTrained++; snprintf(status, sizeof(status), "Ferry: training an Airship"); }
    else if (have == 0) { saving = true; snprintf(status, sizeof(status), "Ferry: saving for an Airship (%d/%d)", EconomyGold(AI_TEAM), UNIT_STATS[UNIT_AIRSHIP].cost); }
}

// --- Trips ---------------------------------------------------------------------------------

// Where an Airship waits for its passengers: open ground next to the Air Factory (or the anchor).
static Vector2 ParkingSpot(void)
{
    int anchor = AiAnchorBase();
    int factory = Find(BUILDING_AIR_FACTORY);
    int b = (factory != -1) ? factory : anchor;
    if (b == -1) return target;
    Vector2 toward = (anchor != -1) ? BuildingCentre(anchor) : BuildingCentre(b);
    return BuildingApproachPoint(b, toward, UNIT_RADIUS, MOVE_GROUND);
}

static bool Idle(const Unit *u)
{
    return !u->moving && !u->attacking && u->gatherState == GATHER_NONE && !u->buildOrder && !u->healing && !u->boarding;
}

// Tell idle ground fighters near home to board, nearest first, filling the free slots.
static void Gather(Ferry *f)
{
    static int pick[MAX_UNITS];
    static float dist[MAX_UNITS];
    int ship = f->ship, home = AiHomeRegion();
    int free = TransportFreeSlots(ship);
    for (int i = 0; i < MAX_UNITS; i++)   // those already walking to it count
        if (units[i].active && units[i].boarding && units[i].transport == ship && units[i].transportSerial == f->serial) free -= UNIT_STATS[units[i].type].cargoSlots;
    if (free <= 0) return;

    int n = 0, fighters = 0;
    for (int i = 0; i < MAX_UNITS; i++)
    {
        const Unit *u = &units[i];
        if (!UnitIsActiveInWorld(u) || u->team != AI_TEAM || !Idle(u)) continue;
        if (UNIT_STATS[u->type].cargoSlots <= 0 || u->type == UNIT_WORKER || Region(u->pos) != home) continue;
        if (UNIT_STATS[u->type].canHeal || UNIT_STATS[u->type].damage <= 0.0f) continue;   // Medics: below, as escorts
        pick[n] = i; dist[n] = Vector2Distance(u->pos, units[ship].pos); n++;
    }
    for (int a = 1; a < n; a++)   // nearest first (ties: pool order)
        for (int b = a; b > 0 && dist[b] < dist[b - 1]; b--)
        { float td = dist[b]; dist[b] = dist[b - 1]; dist[b - 1] = td; int tp = pick[b]; pick[b] = pick[b - 1]; pick[b - 1] = tp; }
    for (int k = 0; k < n && free > 0; k++)
    {
        int slots = UNIT_STATS[units[pick[k]].type].cargoSlots;
        if (slots > free) continue;
        TransportOrderBoard(&pick[k], 1, ship);
        free -= slots;
        fighters++;
    }
    static int aboardIds[TRANSPORT_MAX_CARGO];
    int aboard = TransportCargo(ship, aboardIds, TRANSPORT_MAX_CARGO);
    if (fighters + aboard < AI_FERRY_ESCORT_MIN) return;   // Medics only escort a real group
    int medics = 0;
    for (int i = 0; i < MAX_UNITS && free > 0 && medics < AI_FERRY_MEDICS_PER_TRIP; i++)
    {
        const Unit *u = &units[i];
        if (!UnitIsActiveInWorld(u) || u->team != AI_TEAM || !Idle(u) || !UNIT_STATS[u->type].canHeal) continue;
        if (UNIT_STATS[u->type].cargoSlots > free || Region(u->pos) != home) continue;
        TransportOrderBoard(&i, 1, ship);
        free -= UNIT_STATS[u->type].cargoSlots;
        medics++;
    }
}

// Distance from p to the segment a-b.
static float SegmentDistance(Vector2 p, Vector2 a, Vector2 b)
{
    Vector2 ab = Vector2Subtract(b, a);
    float len2 = Vector2LengthSqr(ab);
    float t = (len2 > 0.0f) ? Clamp(Vector2DotProduct(Vector2Subtract(p, a), ab)/len2, 0.0f, 1.0f) : 0.0f;
    return Vector2Distance(p, Vector2Add(a, Vector2Scale(ab, t)));
}

// Could an enemy that shoots flyers (a unit, or a tower) hit an Airship anywhere
// on the line a-b (a == b: one point)? Its range + AI_FERRY_DANGER_TILES, as far as the AI knows.
static bool AntiAirOnLine(Vector2 a, Vector2 b)
{
    static int near[MAX_UNITS];
    float reach = 220.0f + AI_FERRY_DANGER_TILES*TILE_SIZE;   // a bit more than the longest range (Mage 200)
    Rectangle box = { fminf(a.x, b.x) - reach, fminf(a.y, b.y) - reach, fabsf(a.x - b.x) + reach*2.0f, fabsf(a.y - b.y) + reach*2.0f };
    int n = GridQuery(box, near, MAX_UNITS);
    for (int k = 0; k < n; k++)
    {
        const Unit *u = &units[near[k]];
        const UnitStats *s = &UNIT_STATS[u->type];
        if (u->team == AI_TEAM || !s->hitsAir || s->damage <= 0.0f || !FogCanSee(AI_TEAM, u->pos)) continue;
        if (SegmentDistance(u->pos, a, b) <= s->range + AI_FERRY_DANGER_TILES*TILE_SIZE) return true;
    }
    for (int k = 0; k < MAX_BUILDINGS; k++)
    {
        const Building *bd = &buildings[k];
        const BuildingStats *s = &BUILDING_STATS[bd->type];
        if (!bd->active || bd->team == AI_TEAM || s->damage <= 0.0f || !s->hitsAir || bd->constructing) continue;
        if (!FogCanSeeRect(AI_TEAM, BuildingRect(k))) continue;
        if (SegmentDistance(BuildingCentre(k), a, b) <= s->range + AI_FERRY_DANGER_TILES*TILE_SIZE) return true;
    }
    return false;
}

static bool AntiAirNear(Vector2 p) { return AntiAirOnLine(p, p); }

static bool GoodDrop(Vector2 p, int region)
{
    int tx = (int)floorf(p.x/TILE_SIZE), ty = (int)floorf(p.y/TILE_SIZE);
    if (!MapTileWalkable(MOVE_GROUND, tx, ty) || Region(p) != region) return false;   // open ground the army can fight from
    for (int b = 0; b < MAX_BUILDINGS; b++)   // outside the range of towers it knows about
    {
        const Building *bd = &buildings[b];
        const BuildingStats *s = &BUILDING_STATS[bd->type];
        if (!bd->active || bd->team == AI_TEAM || s->damage <= 0.0f || !FogCanSeeRect(AI_TEAM, BuildingRect(b))) continue;
        if (Vector2Distance(BuildingCentre(b), p) <= s->range + AI_FERRY_TOWER_MARGIN_TILES*TILE_SIZE) return false;
    }
    static int near[256];   // not in the middle of the enemy
    float r = AI_FERRY_CROWD_TILES*TILE_SIZE;
    int n = GridQuery((Rectangle){ p.x - r, p.y - r, r*2.0f, r*2.0f }, near, 256), enemies = 0;
    for (int k = 0; k < n; k++)
        if (units[near[k]].team != AI_TEAM && Vector2Distance(units[near[k]].pos, p) <= r && FogCanSee(AI_TEAM, units[near[k]].pos)) enemies++;
    if (enemies > AI_FERRY_CROWD_MAX) return false;
    return !AntiAirNear(p);
}

// Spiral out from `around` (ring by ring, nearest tiles first) for a good drop
// tile that the Airship at `from` can also fly to safely.
static bool SpiralDrop(Vector2 around, int region, Vector2 from, Vector2 *out)
{
    int cx = (int)floorf(around.x/TILE_SIZE), cy = (int)floorf(around.y/TILE_SIZE);
    for (int r = 0; r <= AI_FERRY_DROP_SEARCH_TILES; r++)
    {
        bool found = false;
        float best = 0.0f;
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++)
            {
                if (abs(dx) != r && abs(dy) != r) continue;
                Vector2 p = { (cx + dx + 0.5f)*TILE_SIZE, (cy + dy + 0.5f)*TILE_SIZE };
                float d = Vector2Distance(p, around);
                if ((found && d >= best) || !GoodDrop(p, region) || AntiAirOnLine(from, p)) continue;
                found = true; best = d; *out = p;
            }
        if (found) return true;
    }
    return false;
}

// The open tile of `region` nearest to `from` (the shore facing the Airship).
static bool NearestTileOf(int region, Vector2 from, Vector2 *out)
{
    bool found = false;
    float best = 0.0f;
    for (int y = 0; y < MapHeight(); y++)
        for (int x = 0; x < MapWidth(); x++)
        {
            Vector2 p = { (x + 0.5f)*TILE_SIZE, (y + 0.5f)*TILE_SIZE };
            if (!MapTileWalkable(MOVE_GROUND, x, y) || Region(p) != region) continue;
            float d = Vector2Distance(p, from);
            if (!found || d < best) { found = true; best = d; *out = p; }
        }
    return found;
}

// Near the target if that's safe; otherwise on the nearest safe shore of its island.
static bool FindDrop(Vector2 around, int region, Vector2 from, Vector2 *out)
{
    if (SpiralDrop(around, region, from, out)) return true;
    Vector2 shore;
    return NearestTileOf(region, from, &shore) && SpiralDrop(shore, region, from, out);
}

static void SetState(Ferry *f, FerryState s) { f->state = s; f->stateTicks = 0; }

static void GoHome(Ferry *f)
{
    UnitsOrderMove(&f->ship, 1, ParkingSpot());   // also cancels any unloading
    SetState(f, FERRY_RETURN);
}

static void Launch(Ferry *f)
{
    f->target = f->expansion ? f->baseSpot : target;
    f->targetRegion = f->expansion ? Region(f->baseSpot) : targetRegion;
    Vector2 drop;
    if (!FindDrop(f->target, f->targetRegion, units[f->ship].pos, &drop)) { snprintf(status, sizeof(status), "Ferry: no safe drop point"); return; }
    static int c[TRANSPORT_MAX_CARGO];
    f->cargoAtStart = TransportCargo(f->ship, c, TRANSPORT_MAX_CARGO);
    for (int i = 0; i < MAX_UNITS; i++)   // anyone still walking to it misses this trip
        if (units[i].active && units[i].boarding && units[i].transport == f->ship && units[i].transportSerial == f->serial) { units[i].boarding = false; UnitStop(i); }
    if (!TransportOrderUnload(f->ship, drop)) return;
    f->drop = drop;
    f->tripStartHp = units[f->ship].hp;
    stats.trips++;
    SetState(f, FERRY_FLY);
}

static void TickFerry(Ferry *f)
{
    Unit *s = &units[f->ship];
    int used = TransportUsedSlots(f->ship);
    f->stateTicks += AI_FERRY_CHECK_TICKS;
    switch (f->state)
    {
    case FERRY_IDLE:
        if (!needs) return;
        if (Vector2Distance(s->pos, ParkingSpot()) > TILE_SIZE*1.5f) { if (!s->moving) UnitsOrderMove(&f->ship, 1, ParkingSpot()); return; }
        Gather(f);
        SetState(f, FERRY_GATHER);
        return;
    case FERRY_GATHER:
    {
        if (f->expansion)   // carrying a Worker over to build a Base: go as soon as it's aboard
        {
            if (!UnitIsAlive(f->worker, f->workerSerial)) { f->expansion = false; AiFerryExpansionFailed(); SetState(f, FERRY_IDLE); return; }
            if (units[f->worker].loaded) Launch(f);
            return;
        }
        if (!needs) { SetState(f, FERRY_IDLE); return; }
        if (f->stateTicks % TICK_RATE < AI_FERRY_CHECK_TICKS) Gather(f);   // about once a second: more units may have come home
        int cap = UNIT_STATS[s->type].cargoCapacity;
        bool full = used >= cap;
        bool waited = f->stateTicks >= AI_FERRY_GATHER_SECONDS*TICK_RATE && used >= AI_FERRY_MIN_CARGO_SLOTS;
        if (full || waited) Launch(f);
        return;
    }
    case FERRY_FLY:
    {
        float maxHp = UNIT_STATS[s->type].hp;
        if (s->hp < f->tripStartHp - AI_FERRY_ABORT_DAMAGE*maxHp)   // shot up on the way: turn back
        {
            stats.aborts++;
            TraceLog(LOG_INFO, "AI FERRY: Airship took %.0f damage, turning back", f->tripStartHp - s->hp);
            GoHome(f);
            return;
        }
        // Enemies that can hit air gathered at the drop point: pick another. (Fire taken on
        // the way is the damage check's job above; the flight line was checked at launch.)
        bool committed = Vector2Distance(s->pos, f->drop) <= AI_FERRY_COMMIT_TILES*TILE_SIZE;   // too close to back off now
        if (s->unloading && used > 0 && !committed && AntiAirNear(f->drop))
        {
            Vector2 other;
            if (FindDrop(f->target, f->targetRegion, s->pos, &other)) { TransportOrderUnload(f->ship, other); f->drop = other; }
            else { stats.aborts++; TraceLog(LOG_INFO, "AI FERRY: no safe drop point left, turning back"); GoHome(f); }
            return;
        }
        if (!s->unloading && used == 0)   // everybody's out
        {
            stats.landed += f->cargoAtStart;
            if (stats.firstLanding < 0) stats.firstLanding = stats.ticks;
            if (f->expansion && UnitIsAlive(f->worker, f->workerSerial) && UnitIsActiveInWorld(&units[f->worker]))
            {
                int cost = BUILDING_STATS[BUILDING_BASE].cost, site = -1;
                if (EconomySpend(AI_TEAM, cost))
                {
                    site = BuildingPlace(BUILDING_BASE, AI_TEAM, f->baseSpot, true);
                    if (site == -1) EconomyAdd(AI_TEAM, cost);
                    else { BuildingsOrderConstruct(&f->worker, 1, site); AiFerryExpansionStarted(site, f->worker); }
                }
                if (site == -1) AiFerryExpansionFailed();
                f->expansion = false;
            }
            GoHome(f);
            return;
        }
        if (!s->unloading && used > 0) GoHome(f);   // the unload order was cancelled (attacked meanwhile...): bring them home
        return;
    }
    case FERRY_RETURN:
        if (s->moving || s->unloading) return;
        if (used > 0) { TransportOrderUnload(f->ship, s->pos); return; }   // turned back with passengers: let them out at home
        SetState(f, FERRY_IDLE);
        return;
    }
}

void AiFerryTick(void)
{
    stats.ticks++;
    if (--checkCountdown > 0) return;
    checkCountdown = AI_FERRY_CHECK_TICKS;
    for (int k = 0; k < ferryCount; k++)
        if (UnitIsAlive(ferries[k].ship, ferries[k].serial)) TickFerry(&ferries[k]);
}

bool AiFerryExpand(int worker, Vector2 spot)
{
    for (int k = 0; k < ferryCount; k++)
    {
        Ferry *f = &ferries[k];
        if (f->state != FERRY_IDLE || TransportUsedSlots(f->ship) > 0) continue;
        f->expansion = true;
        f->worker = worker; f->workerSerial = units[worker].serial;
        f->baseSpot = spot;
        TransportOrderBoard(&worker, 1, f->ship);
        SetState(f, FERRY_GATHER);   // a one-Worker trip: it flies as soon as the Worker is aboard
        return true;
    }
    return false;
}
