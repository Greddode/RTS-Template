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
// Placement: BuildingsPlacementOK() is the one rule (player, ghost, AI, map
// files, editor): open ground under the whole footprint, and for `needsWater`
// types (the Dock) a water tile within BUILDING_WATER_MARGIN of its edge.
//
// Production: each building has a queue of up to MAX_QUEUE units. The cost is
// paid when queuing (and refunded if cancelled); the first unit trains for its
// `trainTime`, then appears next to the building and the rest move up. A
// building can only train unit types whose `trainedAt` is its type.
// Naval units appear on the nearest free water tile next to it; with none
// free, queuing is refused (nothing paid), and a finished one waits.
//
// Rally point: each building has one (default: the spot where its units
// appear). A newly trained unit is given a move order to it.
//
// Destroying a building loses its queue, including the gold paid for it.
//
// Construction: the player pays when placing, and the building appears at
// once, unfinished, so its spot is reserved. Workers with buildOrder walk to
// it and add progress every tick they stand next to it (more workers = faster).
// HP rises from 10% to full as it's built. Unfinished buildings can be
// attacked, but can't train units or take gold drop-offs.

#include "buildings.h"
#include "combat.h"
#include "economy.h"
#include "fog.h"
#include "grid.h"
#include "map.h"
#include "path.h"
#include "sprites.h"
#include "ui.h"
#include "units.h"
#include "raymath.h"
#include <string.h>

#define PLAYER_BASE_COLOR (Color){ 170, 150, 40, 255 }
#define AI_BASE_COLOR     (Color){ 150, 40, 35, 255 }
#define ROOF_COLOR        (Color){ 0, 0, 0, 60 }
#define HEALTH_BAR_H      5.0f
#define BUILD_REACH       (UNIT_RADIUS + 8.0f)   // worker centre to wall, close enough to build
#define BUILD_START_HP    0.1f                   // fraction of full HP an unfinished building starts with
#define BUILD_RETRIES     3
#define UNFINISHED_ALPHA  0.45f
#define SPOT_MIN_TILES    4      // BuildingsFindSpot: start this far out (leave room to walk around)
#define SPOT_MAX_TILES    14
#define RALLY_MIN_DIST    (TILE_SIZE*0.5f)   // rally closer than this to the spawn spot: don't bother walking

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
Vector2 BuildingApproachPoint(int id, Vector2 from, float radius, MoveClass moveClass)
{
    Vector2 wall = ClosestPointOnRect(BuildingRect(id), from);
    Vector2 out = Vector2Subtract(wall, BuildingCentre(id));
    out = Vector2Scale(Vector2Normalize(out), radius + 2.0f);
    Vector2 spot = Vector2Add(wall, out);
    if (MapCircleWalkable(moveClass, spot, radius)) return spot;

    // Blocked (e.g. the corner touches a neighbouring building): walk round the
    // building just outside its walls and take the open spot nearest to `from`.
    // Every spot on that ring is close enough to the wall to build or drop off.
    Rectangle r = BuildingRect(id);
    float gap = radius + 2.0f, step = TILE_SIZE*0.25f;
    Rectangle ring = { r.x - gap, r.y - gap, r.width + 2.0f*gap, r.height + 2.0f*gap };
    bool found = false;
    float bestDist = 0.0f;
    for (float t = 0.0f; t <= ring.width + ring.height; t += step)
    {
        // t runs along the top and right edges; each point is mirrored onto the opposite edge.
        Vector2 p = (t <= ring.width) ? (Vector2){ ring.x + t, ring.y } : (Vector2){ ring.x + ring.width, ring.y + (t - ring.width) };
        Vector2 q = { 2.0f*ring.x + ring.width - p.x, 2.0f*ring.y + ring.height - p.y };
        Vector2 cand[2] = { p, q };
        for (int k = 0; k < 2; k++)
        {
            if (!MapCircleWalkable(moveClass, cand[k], radius)) continue;
            float d = Vector2Distance(cand[k], from);
            if (!found || d < bestDist) { found = true; bestDist = d; spot = cand[k]; }
        }
    }
    if (!found) UnitsOpenSpots(moveClass, spot, 1, &spot);   // boxed in all round: nearest open spot
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
int BuildingsFindNearestEnemy(Vector2 pos, float maxDist, int team, MoveClass moveClass, float range)
{
    int best = -1;
    float bestDist = maxDist;
    for (int i = 0; i < MAX_BUILDINGS; i++)
    {
        const Building *b = &buildings[i];
        if (!b->active || b->team == team) continue;
        if (b->hp <= b->incomingDamage) continue;   // doomed: don't waste attacks
        if (!FogCanSeeRect(team, BuildingRect(i))) continue;   // hidden by fog
        float d = BuildingDistance(i, pos);
        if (d > bestDist) continue;
        if (d > range && !PathCanReach(moveClass, pos, BuildingRect(i), range)) continue;   // across the water, for a Melee
        bestDist = d; best = i;
    }
    return best;
}

int BuildingsFindDropOff(Vector2 pos, int team)
{
    int best = -1;
    float bestDist = 0.0f;
    for (int i = 0; i < MAX_BUILDINGS; i++)
    {
        const Building *b = &buildings[i];
        if (!b->active || b->team != team || b->constructing || !BUILDING_STATS[b->type].dropOff) continue;
        float d = BuildingDistance(i, pos);
        if (best == -1 || d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

// Square rings of tiles around `near`, from SPOT_MIN_TILES out; first fit wins.
bool BuildingsFindSpot(BuildingType type, Vector2 near, Vector2 *out)
{
    int cx = (int)(near.x/TILE_SIZE), cy = (int)(near.y/TILE_SIZE);
    for (int r = SPOT_MIN_TILES; r <= SPOT_MAX_TILES; r++)
    {
        for (int y = cy - r; y <= cy + r; y++)
        {
            for (int x = cx - r; x <= cx + r; x++)
            {
                if (x != cx - r && x != cx + r && y != cy - r && y != cy + r) continue;   // ring edge only
                Vector2 p = { (x + 0.5f)*TILE_SIZE, (y + 0.5f)*TILE_SIZE };
                if (BuildingCanPlace(type, p, NULL)) { *out = p; return true; }
            }
        }
    }
    return false;
}

void BuildingSetRally(int id, Vector2 point)
{
    buildings[id].rally = point;
}

// Where trained units appear: an open spot (for that movement class) just below the building.
static Vector2 SpawnSpot(int id, MoveClass moveClass)
{
    Rectangle r = BuildingRect(id);
    Vector2 spot = { r.x + r.width*0.5f, r.y + r.height + UNIT_RADIUS*2.0f };
    UnitsOpenSpots(moveClass, spot, 1, &spot);
    return spot;
}

// Naval units: the free water tile next to the building (within
// BUILDING_WATER_MARGIN of its edge) nearest its centre. Free = water with no
// building on it and no boat or ground unit standing in it. False if none.
static bool FreeWaterSpot(int id, Vector2 *out)
{
    const Building *b = &buildings[id];
    Vector2 centre = BuildingCentre(id);
    int m = BUILDING_WATER_MARGIN;
    bool found = false;
    float best = 0.0f;
    for (int y = b->ty - m; y < b->ty + b->size + m; y++)
        for (int x = b->tx - m; x < b->tx + b->size + m; x++)
        {
            if (!MapTileWalkable(MOVE_NAVAL, x, y)) continue;
            Vector2 p = { (x + 0.5f)*TILE_SIZE, (y + 0.5f)*TILE_SIZE };
            float d = Vector2Distance(p, centre);
            if (found && d >= best) continue;
            int near[8];
            int n = GridQuery((Rectangle){ x*TILE_SIZE - UNIT_RADIUS, y*TILE_SIZE - UNIT_RADIUS, TILE_SIZE + UNIT_RADIUS*2.0f, TILE_SIZE + UNIT_RADIUS*2.0f }, near, 8), taken = 0;
            for (int k = 0; k < n; k++) taken += !UnitIsFlying(&units[near[k]]) && Vector2Distance(units[near[k]].pos, p) < TILE_SIZE*0.5f + units[near[k]].radius;
            if (taken) continue;
            found = true; best = d; *out = p;
        }
    return found;
}

bool BuildingHasSpawnRoom(int id, UnitType type)
{
    Vector2 unused;
    return UNIT_STATS[type].moveClass != MOVE_NAVAL || FreeWaterSpot(id, &unused);
}

// Where a newly trained unit of this type appears. False: nowhere yet (naval, no free water).
static bool UnitSpawnSpot(int id, UnitType type, Vector2 *out)
{
    if (UNIT_STATS[type].moveClass == MOVE_NAVAL) return FreeWaterSpot(id, out);
    *out = SpawnSpot(id, UNIT_STATS[type].moveClass);
    return true;
}

// The default rally point: where its units appear (on water for a Dock).
static Vector2 DefaultRally(int id)
{
    for (int t = 0; t < UNIT_TYPE_COUNT; t++)
    {
        Vector2 spot;
        if (UNIT_STATS[t].trainedAt == buildings[id].type && UNIT_STATS[t].moveClass == MOVE_NAVAL && FreeWaterSpot(id, &spot)) return spot;
    }
    return SpawnSpot(id, MOVE_GROUND);
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
        if (MapCircleWalkable(UnitMoveClass(u), u->pos, u->radius)) continue;   // flyers aren't in the way
        UnitsOpenSpots(UnitMoveClass(u), u->pos, 1, &u->pos);
        u->prevPos = u->pos;
    }
}

static void FootprintTiles(BuildingType type, Vector2 centre, int *tx, int *ty)
{
    int size = BUILDING_STATS[type].size;
    *tx = (int)(centre.x/TILE_SIZE) - size/2;
    *ty = (int)(centre.y/TILE_SIZE) - size/2;
}

Rectangle BuildingFootprint(BuildingType type, Vector2 centre)
{
    int tx, ty, size = BUILDING_STATS[type].size;
    FootprintTiles(type, centre, &tx, &ty);
    return (Rectangle){ (float)(tx*TILE_SIZE), (float)(ty*TILE_SIZE), (float)(size*TILE_SIZE), (float)(size*TILE_SIZE) };
}

// Prerequisites: a type with `requires` can be started only while the team
// owns at least one FINISHED building of that type. Checked when a worker
// (player or AI) starts one, not when map files or the editor place it.
// Losing the required building later only stops new ones; existing ones keep working.
bool BuildingsCanBuild(int team, BuildingType type)
{
    BuildingType need = BUILDING_STATS[type].requires;
    if (need == BUILDING_NONE) return true;
    for (int i = 0; i < MAX_BUILDINGS; i++)
    {
        const Building *b = &buildings[i];
        if (b->active && b->team == team && b->type == need && !b->constructing) return true;
    }
    return false;
}

bool BuildingsPlacementOK(BuildingType type, int tx, int ty, TileOpenFn tileOpen, const void *source, const char **why)
{
    const BuildingStats *s = &BUILDING_STATS[type];
    const char *unused;
    if (why == NULL) why = &unused;

    // Every tile must be open ground (no water, rock, lava or other building)...
    for (int y = ty; y < ty + s->size; y++)
        for (int x = tx; x < tx + s->size; x++)
            if (!tileOpen(source, x, y, MOVE_GROUND)) { *why = "Can't build there"; return false; }

    // ...and a Dock needs water touching it: any tile in the ring around the footprint.
    if (s->needsWater)
    {
        int m = BUILDING_WATER_MARGIN;
        for (int y = ty - m; y < ty + s->size + m; y++)
            for (int x = tx - m; x < tx + s->size + m; x++)
                if (tileOpen(source, x, y, MOVE_NAVAL)) return true;
        *why = TextFormat("%s must be next to water", s->name);
        return false;
    }
    return true;
}

static bool LiveTileOpen(const void *source, int tx, int ty, MoveClass moveClass)
{
    (void)source;
    return MapTileWalkable(moveClass, tx, ty);   // terrain plus the buildings already standing
}

bool BuildingCanPlace(BuildingType type, Vector2 centre, const char **why)
{
    int tx, ty;
    FootprintTiles(type, centre, &tx, &ty);
    if (!BuildingsPlacementOK(type, tx, ty, LiveTileOpen, NULL, why)) return false;

    // In the game it also mustn't cover a gold node (the editor checks its own objects).
    Rectangle r = BuildingFootprint(type, centre);
    for (int i = 0; i < MAX_GOLD_NODES; i++)
    {
        if (goldNodes[i].active && CheckCollisionPointRec(goldNodes[i].pos, r))
        {
            if (why) *why = "Can't build on gold";
            return false;
        }
    }
    return true;
}

int BuildingPlace(BuildingType type, int team, Vector2 centre, bool unfinished)
{
    if (!BuildingCanPlace(type, centre, NULL)) return -1;
    int tx, ty, size = BUILDING_STATS[type].size;
    FootprintTiles(type, centre, &tx, &ty);

    for (int i = 0; i < MAX_BUILDINGS; i++)
    {
        if (buildings[i].active) continue;
        float maxHp = BUILDING_STATS[type].hp;
        buildings[i] = (Building){
            .active = true,
            .serial = nextSerial++,
            .type = type,
            .team = team,
            .hp = unfinished ? maxHp*BUILD_START_HP : maxHp,
            .tx = tx, .ty = ty, .size = size,
            .constructing = unfinished,
        };
        MapSetBlocked(tx, ty, size, size, true);
        PushUnitsOut(i);
        buildings[i].rally = DefaultRally(i);   // right where its units appear
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
    if (!b->active || b->constructing || b->queueCount >= MAX_QUEUE) return false;
    if (UNIT_STATS[type].trainedAt != b->type) return false;
    if (!UnitsCanTrain(b->team, type)) return false;   // its `requires` building (player and AI alike)
    if (!BuildingHasSpawnRoom(id, type))   // a Dock boxed in: refused before paying
    {
        if (b->team == PLAYER_TEAM) UiShowMessage(TextFormat("No free water next to the %s", BUILDING_STATS[b->type].name));
        return false;
    }
    if (!EconomySpend(b->team, UNIT_STATS[type].cost)) return false;
    b->queue[b->queueCount++] = type;
    return true;
}

void BuildingCancelQueued(int id, int index)
{
    Building *b = &buildings[id];
    if (!b->active || index < 0 || index >= b->queueCount) return;
    EconomyAdd(b->team, UNIT_STATS[b->queue[index]].cost);
    for (int k = index + 1; k < b->queueCount; k++) b->queue[k - 1] = b->queue[k];
    b->queueCount--;
    if (index == 0) b->trainTicks = 0;   // the one in training was cancelled
}

static int BuildTotalTicks(int id)
{
    return (int)(BUILDING_STATS[buildings[id].type].buildTime*TICK_RATE);
}

float BuildingBuildProgress(int id)
{
    return (float)buildings[id].buildTicks/BuildTotalTicks(id);
}

void BuildingsOrderConstruct(const int *ids, int count, int building)
{
    for (int k = 0; k < count; k++)
    {
        int id = ids[k];
        if (units[id].type != UNIT_WORKER || units[id].team != buildings[building].team) continue;
        UnitsOrderStop(&id, 1);   // drop any other order first
        Unit *u = &units[id];
        u->buildOrder = true;
        u->buildSite = building;
        u->buildSiteSerial = buildings[building].serial;
        u->orderRetries = BUILD_RETRIES;
        UnitMoveTo(id, BuildingApproachPoint(building, u->pos, u->radius, UnitMoveClass(u)));
    }
}

// One tick of one worker's work: progress and HP go up together.
static void AddBuildProgress(int id)
{
    Building *b = &buildings[id];
    float maxHp = BUILDING_STATS[b->type].hp;
    b->buildTicks++;
    b->hp += maxHp*(1.0f - BUILD_START_HP)/BuildTotalTicks(id);
    if (b->hp > maxHp) b->hp = maxHp;
    if (b->buildTicks >= BuildTotalTicks(id)) b->constructing = false;   // finished
}

Vector2 BuildingsWorkerTick(int id)
{
    Unit *u = &units[id];
    Vector2 none = { 0 };
    int site = u->buildSite;

    // Site destroyed or finished (by us or someone else): back to idle.
    if (!BuildingIsAlive(site, u->buildSiteSerial) || !buildings[site].constructing)
    {
        u->buildOrder = false;
        UnitStop(id);
        return none;
    }

    if (BuildingDistance(site, u->pos) <= BUILD_REACH)
    {
        if (u->moving) UnitStop(id);
        u->orderRetries = BUILD_RETRIES;   // got here: retries are only for reaching the site
        AddBuildProgress(site);
        return none;
    }

    if (u->moving) return UnitFollowPath(id);
    if (u->orderRetries-- > 0) UnitMoveTo(id, BuildingApproachPoint(site, u->pos, u->radius, UnitMoveClass(u)));
    else { u->buildOrder = false; UnitStop(id); }   // can't get there
    return none;
}

void BuildingsTick(void)
{
    for (int i = 0; i < MAX_BUILDINGS; i++)
    {
        Building *b = &buildings[i];
        if (!b->active || b->constructing) continue;   // unfinished towers don't fire either
        if (BUILDING_STATS[b->type].damage > 0.0f) CombatBuildingTick(i);   // a tower: shoot (combat.c)
        if (b->queueCount == 0) continue;

        if (++b->trainTicks < (int)(UNIT_STATS[b->queue[0]].trainTime*TICK_RATE)) continue;

        // Done: spawn just below the building (naval: on free water next to it),
        // then head for the rally point. No free water or the unit pool is full: wait.
        Vector2 spot;
        if (!UnitSpawnSpot(i, b->queue[0], &spot)) continue;
        int unit = UnitSpawn(spot, b->queue[0], b->team);
        if (unit == -1) continue;
        if (Vector2Distance(spot, b->rally) > RALLY_MIN_DIST) UnitsOrderMove(&unit, 1, b->rally);

        for (int k = 1; k < b->queueCount; k++) b->queue[k - 1] = b->queue[k];
        b->queueCount--;
        b->trainTicks = 0;
    }
}

// A building's body: its art tinted in team colour (sprites.c), or else a
// team-coloured square with a darker roof. Unfinished ones are see-through.
void BuildingsDrawLook(BuildingType type, int team, Rectangle r, bool unfinished)
{
    Color body = (team == PLAYER_TEAM) ? PLAYER_BASE_COLOR : AI_BASE_COLOR;
    if (unfinished) body = Fade(body, UNFINISHED_ALPHA);   // unfinished: see-through
    if (SpritesHaveBuilding(type)) { SpritesDrawBuilding(type, r, body); return; }
    DrawRectangleRec(r, body);
    DrawRectangleRec((Rectangle){ r.x + 10, r.y + 10, r.width - 20, r.height - 20 }, ROOF_COLOR);
}

// Construction, health and production bars.
static void DrawBuildingBars(int id, Rectangle r)
{
    const Building *b = &buildings[id];

    // Construction progress along the bottom edge.
    if (b->constructing)
    {
        DrawRectangleRec((Rectangle){ r.x, r.y + r.height - 6.0f, r.width, 6.0f }, Fade(BLACK, 0.6f));
        DrawRectangleRec((Rectangle){ r.x, r.y + r.height - 6.0f, r.width*BuildingBuildProgress(id), 6.0f }, ORANGE);
    }

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

// Is building i on screen and not hidden by fog?
static bool BuildingShown(int i, Rectangle view)
{
    const Building *b = &buildings[i];
    if (!b->active) return false;
    Rectangle r = BuildingRect(i);
    if (!CheckCollisionRecs(r, view)) return false;   // off screen
    // Fog: enemy buildings show while visible, or (dimmed by the fog) once seen.
    return b->team == PLAYER_TEAM || b->seenByPlayer || FogCanSeeRect(PLAYER_TEAM, r);
}

// Same pass idea as UnitsDraw(): buildings without art are drawn complete
// first (exactly as before sprites existed), then all the art in one batch,
// then the bars of the buildings with art on top.
void BuildingsDraw(Rectangle view)
{
    for (int pass = 0; pass < 3; pass++)
    {
        for (int i = 0; i < MAX_BUILDINGS; i++)
        {
            if (!BuildingShown(i, view)) continue;
            const Building *b = &buildings[i];
            bool art = SpritesHaveBuilding(b->type);
            Rectangle r = BuildingRect(i);
            if (pass == 0 && !art) { BuildingsDrawLook(b->type, b->team, r, b->constructing); DrawBuildingBars(i, r); }
            if (pass == 1 && art) BuildingsDrawLook(b->type, b->team, r, b->constructing);
            if (pass == 2 && art) DrawBuildingBars(i, r);
        }
    }
}

void BuildingsReset(void)
{
    memset(buildings, 0, sizeof(buildings));
}

int BuildingsCount(int team)
{
    int count = 0;
    for (int i = 0; i < MAX_BUILDINGS; i++) count += (buildings[i].active && buildings[i].team == team);
    return count;
}
