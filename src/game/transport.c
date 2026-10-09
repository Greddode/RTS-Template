// transport.c - Units that carry units.
//
// A transport is any unit type with cargoCapacity > 0 in UNIT_STATS (the
// Airship: 8 slots). Every unit type has cargoSlots: how many slots it takes
// inside one (Knight 2, most ground units 1, flyers and transports 0 = can't
// be carried). Making another transport is just a table row.
//
// Cargo lives in the normal unit pool. A loaded unit has `loaded` set and
// points at its transport (slot + serial). It keeps its slot and HP, but
// UnitIsActiveInWorld() says no, so the grid, drawing, minimap, fog sight,
// selection, targeting, splash, healing and separation all skip it. Each tick
// it just copies its transport's position (TransportCargoTick). Nothing is
// stored in the transport itself: its cargo is "every loaded unit pointing at
// it", found by a pass over the pool when needed (rare: inspector, unloading).
//
// Boarding: TransportOrderBoard() gives units a boarding order; each tick they
// walk at the transport (straight if the line is clear, else a path) and get
// in once within TRANSPORT_BOARD_DISTANCE - if there are enough free slots.
// If not, the order ends ("Airship full" for the player). KEY_LOAD orders the
// idle units within TRANSPORT_LOAD_RADIUS_TILES to board, nearest first, as
// many as fit.
//
// Unloading: units only come out where a ground unit can stand. The transport
// flies to the open ground tile nearest the requested point (if the point is
// water, rock, lava or a building, it keeps going to the nearest good tile),
// then lets out TRANSPORT_UNLOAD_PER_TICK units per tick, each on a free spot
// around the drop point, so they don't stack. They come out as ordinary idle units.
//
// Destroyed transport (TransportDestroyed, from UnitDespawn): over walkable
// terrain its cargo drops around it with its current HP; over anything else
// it's lost. One log line. A loaded unit whose transport is gone (checked by
// serial every tick) is handled the same way, so none can stay loaded.
//
// The AI doesn't use transports: only the player's input gives these orders.

#include "transport.h"
#include "grid.h"
#include "map.h"
#include "path.h"
#include "ui.h"
#include "units.h"
#include "raymath.h"
#include <math.h>
#include <stdlib.h>

#define DROP_SPACING    (UNIT_RADIUS*2.5f)   // between dropped units (like formation spots)
#define DROP_MAX_RINGS  12                   // how far round a drop point it looks for a free spot
#define BOARD_RETHINK   10                   // boarding: re-plan the walk 3x per second

bool IsTransport(int id)
{
    return UNIT_STATS[units[id].type].cargoCapacity > 0;
}

static bool InTransport(int id, int transport)
{
    const Unit *u = &units[id];
    return u->active && u->loaded && u->transport == transport && u->transportSerial == units[transport].serial;
}

int TransportCargo(int transport, int *out, int max)
{
    int n = 0;
    for (int i = 0; i < MAX_UNITS && n < max; i++) if (InTransport(i, transport)) out[n++] = i;
    return n;
}

int TransportUsedSlots(int transport)
{
    int used = 0;
    for (int i = 0; i < MAX_UNITS; i++) if (InTransport(i, transport)) used += UNIT_STATS[units[i].type].cargoSlots;
    return used;
}

int TransportFreeSlots(int transport)
{
    return UNIT_STATS[units[transport].type].cargoCapacity - TransportUsedSlots(transport);
}

// --- Getting in -----------------------------------------------------------------------

static bool CanBoard(int id, int transport)
{
    const Unit *u = &units[id];
    return id != transport && UnitIsActiveInWorld(u) && u->team == units[transport].team && UNIT_STATS[u->type].cargoSlots > 0;
}

static void ClearOrders(Unit *u)
{
    int id = (int)(u - units);
    UnitStop(id);
    u->attacking = false; u->attackMove = false; u->holdPosition = false; u->leashed = false;
    u->gatherState = GATHER_NONE; u->buildOrder = false; u->healing = false; u->healOrdered = false;
    u->boarding = false;
}

static void Load(int id, int transport)
{
    Unit *u = &units[id];
    ClearOrders(u);
    u->loaded = true;
    u->transport = transport;
    u->transportSerial = units[transport].serial;
    u->selected = false;
    u->incomingDamage = 0.0f;   // projectiles flying at it vanish (their target left the world)
    u->pos = u->prevPos = units[transport].pos;
}

void TransportOrderBoard(const int *ids, int count, int transport)
{
    int ordered = 0;
    for (int k = 0; k < count; k++)
    {
        int id = ids[k];
        if (!CanBoard(id, transport)) continue;
        ClearOrders(&units[id]);
        units[id].boarding = true;
        units[id].transport = transport;
        units[id].transportSerial = units[transport].serial;
        units[id].chaseTicks = 0;   // plan the walk on its next tick
        ordered++;
    }
    if (ordered > 0 && TransportFreeSlots(transport) <= 0 && units[transport].team == PLAYER_TEAM)
        UiShowMessage(TextFormat("%s full", UNIT_STATS[units[transport].type].name));
}

int TransportLoadNearby(int transport)
{
    if (!UnitIsActiveInWorld(&units[transport]) || !IsTransport(transport)) return 0;
    static int near[MAX_UNITS], order[MAX_UNITS];
    static float dist[MAX_UNITS];
    float r = TRANSPORT_LOAD_RADIUS_TILES*TILE_SIZE;
    Vector2 at = units[transport].pos;
    int count = GridQuery((Rectangle){ at.x - r, at.y - r, r*2.0f, r*2.0f }, near, MAX_UNITS);

    // Idle units of ours that can be carried, nearest first.
    int n = 0;
    for (int k = 0; k < count; k++)
    {
        const Unit *u = &units[near[k]];
        bool idle = !u->moving && !u->attacking && u->gatherState == GATHER_NONE && !u->buildOrder && !u->healing && !u->boarding;
        float d = Vector2Distance(u->pos, at);
        if (!CanBoard(near[k], transport) || !idle || d > r) continue;
        order[n] = near[k]; dist[n] = d; n++;
    }
    for (int a = 1; a < n; a++)   // insertion sort by distance (n is small); ties keep pool order
        for (int b = a; b > 0 && (dist[b] < dist[b - 1] || (dist[b] == dist[b - 1] && order[b] < order[b - 1])); b--)
        {
            float td = dist[b]; dist[b] = dist[b - 1]; dist[b - 1] = td;
            int to = order[b]; order[b] = order[b - 1]; order[b - 1] = to;
        }

    // As many as fit, counting units already walking to it.
    int free = TransportFreeSlots(transport);
    for (int i = 0; i < MAX_UNITS; i++)
        if (units[i].active && units[i].boarding && units[i].transport == transport && units[i].transportSerial == units[transport].serial)
            free -= UNIT_STATS[units[i].type].cargoSlots;
    int told = 0;
    for (int k = 0; k < n; k++)
    {
        int slots = UNIT_STATS[units[order[k]].type].cargoSlots;
        if (slots > free) continue;   // a Knight may not fit where a Worker still does
        TransportOrderBoard(&order[k], 1, transport);
        free -= slots;
        told++;
    }
    if (units[transport].team == PLAYER_TEAM)
    {
        if (told == 0 && TransportFreeSlots(transport) <= 0) UiShowMessage(TextFormat("%s full", UNIT_STATS[units[transport].type].name));
        else if (told == 0) UiShowMessage("No idle units nearby that fit");
    }
    return told;
}

Vector2 TransportBoardTick(int id)
{
    Unit *u = &units[id];
    Vector2 none = { 0 };
    int t = u->transport;
    if (!UnitIsAlive(t, u->transportSerial) || !UnitIsActiveInWorld(&units[t]))   // it died (or isn't there): forget it
    {
        u->boarding = false;
        UnitStop(id);
        return none;
    }
    Vector2 goal = units[t].pos;
    if (Vector2Distance(u->pos, goal) <= TRANSPORT_BOARD_DISTANCE)
    {
        if (UNIT_STATS[u->type].cargoSlots <= TransportFreeSlots(t)) { Load(id, t); return none; }
        if (u->team == PLAYER_TEAM) UiShowMessage(TextFormat("%s full", UNIT_STATS[units[t].type].name));
        u->boarding = false;   // doesn't fit: the order ends, it stays where it is
        UnitStop(id);
        return none;
    }
    // Walk at it: straight while the line is clear, else a path (re-planned when it moves).
    if (--u->chaseTicks <= 0)
    {
        u->chaseTicks = BOARD_RETHINK;
        u->chaseDirect = MapLineClear(UnitMoveClass(u), u->pos, goal, u->radius);
        if (u->chaseDirect) { if (u->moving) UnitStop(id); }
        else if (!u->moving || Vector2Distance(u->target, goal) > TILE_SIZE) UnitMoveTo(id, goal);
    }
    if (u->chaseDirect) return UnitStepToward(id, goal);
    return u->moving ? UnitFollowPath(id) : none;
}

// --- Getting out ----------------------------------------------------------------------

static bool GroundTile(Vector2 p)   // a ground unit could stand on this tile (terrain and no building)
{
    return MapTileWalkable(MOVE_GROUND, (int)floorf(p.x/TILE_SIZE), (int)floorf(p.y/TILE_SIZE));
}

// The centre of the open ground tile nearest `p` (p's own tile if it's open).
static bool NearestGroundTile(Vector2 p, Vector2 *out)
{
    int cx = (int)floorf(p.x/TILE_SIZE), cy = (int)floorf(p.y/TILE_SIZE);
    if (MapTileWalkable(MOVE_GROUND, cx, cy)) { *out = p; return true; }
    for (int r = 1; r <= TRANSPORT_DROP_SEARCH_TILES; r++)
    {
        float best = 1e30f; bool found = false;
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++)
            {
                if (abs(dx) != r && abs(dy) != r) continue;   // this ring only
                if (!MapTileWalkable(MOVE_GROUND, cx + dx, cy + dy)) continue;
                Vector2 c = { (cx + dx + 0.5f)*TILE_SIZE, (cy + dy + 0.5f)*TILE_SIZE };
                float d = Vector2Distance(c, p);
                if (d < best) { best = d; *out = c; found = true; }
            }
        if (found) return true;
    }
    return false;
}

// A spot near `around` where a ground unit fits and nobody is standing (nor
// any of the `taken` spots handed out this tick).
static bool FreeDropSpot(Vector2 around, const Vector2 *taken, int nTaken, Vector2 *out)
{
    static int near[64];
    for (int ring = 0; ring <= DROP_MAX_RINGS; ring++)
        for (int gy = -ring; gy <= ring; gy++)
            for (int gx = -ring; gx <= ring; gx++)
            {
                if (abs(gx) != ring && abs(gy) != ring) continue;
                Vector2 p = { around.x + gx*DROP_SPACING, around.y + gy*DROP_SPACING };
                if (!MapCircleWalkable(MOVE_GROUND, p, UNIT_RADIUS)) continue;
                bool clear = true;
                float m = UNIT_RADIUS*2.0f;
                int n = GridQuery((Rectangle){ p.x - m, p.y - m, m*2.0f, m*2.0f }, near, 64);
                for (int k = 0; k < n && clear; k++) if (!UnitIsFlying(&units[near[k]]) && Vector2Distance(units[near[k]].pos, p) < m) clear = false;
                for (int k = 0; k < nTaken && clear; k++) if (Vector2Distance(taken[k], p) < m) clear = false;
                if (clear) { *out = p; return true; }
            }
    return false;
}

static void PutOut(int id, Vector2 spot)
{
    Unit *u = &units[id];
    u->loaded = false;
    u->boarding = false;
    u->pos = u->prevPos = u->target = spot;
    u->moving = false;
    u->incomingDamage = 0.0f;
    u->acquireTicks = 1;   // looks around at once, like any idle unit
}

bool TransportUnloadOne(int transport, int cargo)
{
    if (!InTransport(cargo, transport)) return false;
    Vector2 spot;
    if (!GroundTile(units[transport].pos) || !FreeDropSpot(units[transport].pos, NULL, 0, &spot))
    {
        if (units[transport].team == PLAYER_TEAM) UiShowMessage("Can't unload here: not over open ground");
        return false;
    }
    PutOut(cargo, spot);
    return true;
}

bool TransportOrderUnload(int transport, Vector2 at)
{
    Unit *t = &units[transport];
    if (TransportUsedSlots(transport) == 0)
    {
        if (t->team == PLAYER_TEAM) UiShowMessage(TextFormat("%s is empty", UNIT_STATS[t->type].name));
        return false;
    }
    Vector2 drop;
    if (!NearestGroundTile(at, &drop))
    {
        if (t->team == PLAYER_TEAM) UiShowMessage("No open ground near there to unload");
        return false;
    }
    t->attacking = false; t->attackMove = false; t->holdPosition = false; t->leashed = false;
    t->unloading = true;
    t->unloadAt = drop;
    UnitMoveTo(transport, drop);   // a flyer: straight there
    return true;
}

void TransportTick(int id)
{
    Unit *t = &units[id];
    if (!t->unloading) return;
    if (t->attacking) { t->unloading = false; return; }   // given another order meanwhile
    if (Vector2Distance(t->pos, t->unloadAt) > TILE_SIZE*0.5f)
    {
        if (!t->moving) UnitMoveTo(id, t->unloadAt);   // pushed off, or the move ended short: go on
        return;
    }
    static int cargo[TRANSPORT_MAX_CARGO];
    Vector2 taken[TRANSPORT_UNLOAD_PER_TICK];
    int n = TransportCargo(id, cargo, TRANSPORT_MAX_CARGO), out = 0;
    for (int k = 0; k < n && out < TRANSPORT_UNLOAD_PER_TICK; k++)
    {
        Vector2 spot;
        if (!FreeDropSpot(t->unloadAt, taken, out, &spot)) break;   // crowded: try again next tick
        PutOut(cargo[k], spot);
        taken[out++] = spot;
    }
    if (n - out == 0) t->unloading = false;   // empty: free again
}

// --- Losing the transport ----------------------------------------------------------------

// Its transport is gone: drop it near `where` if that's walkable terrain, else it's lost.
// Returns true if it was dropped.
static bool DropOrLose(int id, Vector2 where, const Vector2 *taken, int nTaken, Vector2 *spotOut)
{
    int tx = (int)floorf(where.x/TILE_SIZE), ty = (int)floorf(where.y/TILE_SIZE);
    bool land = tx >= 0 && ty >= 0 && tx < MapWidth() && ty < MapHeight() && TileAllows(MapGetTile(tx, ty), MOVE_GROUND);
    Vector2 spot;
    if (land && FreeDropSpot(where, taken, nTaken, &spot)) { PutOut(id, spot); *spotOut = spot; return true; }
    units[id].loaded = false;
    UnitDespawn(id);
    return false;
}

void TransportDestroyed(int id)
{
    static int cargo[TRANSPORT_MAX_CARGO];
    static Vector2 taken[TRANSPORT_MAX_CARGO];
    int n = TransportCargo(id, cargo, TRANSPORT_MAX_CARGO), dropped = 0;
    if (n == 0) return;
    Vector2 where = units[id].pos;
    for (int k = 0; k < n; k++)
    {
        Vector2 spot;
        if (DropOrLose(cargo[k], where, taken, dropped, &spot)) taken[dropped++] = spot;
    }
    TraceLog(LOG_INFO, "TRANSPORT: %s destroyed with %d units aboard: %d dropped, %d lost", UNIT_STATS[units[id].type].name, n, dropped, n - dropped);
}

void TransportCargoTick(int id)
{
    Unit *u = &units[id];
    int t = u->transport;
    if (!UnitIsAlive(t, u->transportSerial))   // safety net: its transport is gone (TransportDestroyed should have run)
    {
        Vector2 spot;
        bool dropped = DropOrLose(id, u->pos, NULL, 0, &spot);
        TraceLog(LOG_WARNING, "TRANSPORT: a loaded unit outlived its transport: %s", dropped ? "dropped" : "lost");
        return;
    }
    u->pos = u->prevPos = units[t].pos;   // rides along
}
