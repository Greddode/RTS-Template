// input.c - Selection, orders and building placement.
//   Left click          select the unit, own building or gold node under the cursor
//   Left drag           box select (units)
//   Shift + click/drag  add units to the current selection
//   Right click         move; on an enemy: attack; on gold: workers mine;
//                       on your unfinished building: workers help build;
//                       on your damaged unit: selected healers follow and heal it;
//                       with a building selected: set its rally point (flag)
//   A, then right click attack-move there (left click or Esc cancels)
//   S / H               stop / hold position
//   Transports (transport.c), e.g. the Airship:
//     right click your transport   the selected units that fit walk to it and board
//     L / U                        transport selected: nearby idle units board / unload below it
//     Ctrl + right click ground    the transport flies there and unloads (Shift on the web,
//                                  where Ctrl pauses: KEY_UNLOAD_MODIFIER)
//   Placing a building  (started from the inspector's Build buttons) a ghost
//                       follows the mouse, green = can build, red = blocked.
//                       Left click places, right click or Esc cancels.
//
// Selection is a list of (slot, serial) pairs: dead units drop out by
// themselves, and "is anything selected?" costs nothing. Each unit's
// `selected` flag mirrors the list (it's what draws the green ring). A
// selected building or gold node is also stored as (slot, serial).
//
// Keys come from the bindings in config.h. Mouse presses over UI (ui.c) are
// ignored here, so clicking a button never also selects or orders units.
// The drag start is stored in world coords, so the box stays anchored to the
// ground even if the camera pans mid-drag.

#include "input.h"
#include "buildings.h"
#include "camera.h"
#include "economy.h"
#include "fog.h"
#include "grid.h"
#include "heal.h"
#include "transport.h"
#include "ui.h"
#include "units.h"
#include "raymath.h"
#include <math.h>
#include <string.h>

#define DRAG_THRESHOLD 4.0f   // screen pixels the mouse must move before a click becomes a drag
#define BOX_COLOR      (Color){ 60, 255, 90, 255 }
#define RANGE_COLOR     (Color){ 255, 255, 255, 110 }   // a selected tower's range ring
#define RANGE_RING_SEGMENTS 64
#define BOX_FILL       (Color){ 60, 255, 90, 40 }
#define AMOVE_COLOR    (Color){ 255, 70, 50, 255 }
#define GHOST_OK       (Color){ 60, 230, 90, 255 }
#define GHOST_BLOCKED  (Color){ 240, 60, 50, 255 }
#define RALLY_COLOR    (Color){ 60, 200, 255, 255 }
#define MARKER_TIME    1.0    // seconds the destination marker stays visible
#define MARKER_SIZE    8.0f   // screen pixels

static bool    leftHeld = false;
static bool    attackMoveArmed = false;   // A was pressed: next right click is an attack-move
static bool    placing = false;           // a Build button was pressed: next left click places
static BuildingType placingType;
static Vector2 markerPos;
static double  markerTime = -100.0;
static Vector2 dragStartWorld;
static Vector2 dragStartScreen;

// Selection
static int          selUnits[MAX_UNITS];
static unsigned int selSerials[MAX_UNITS];
static int          selCount = 0;
static int          selBuilding = -1;    // slot...
static unsigned int selBuildingSerial;   // ...and serial
static int          selNode = -1;
static unsigned int selNodeSerial;

static int found[MAX_UNITS];   // scratch buffer for grid queries and orders

static bool IsDragging(void)
{
    return leftHeld && Vector2Distance(dragStartScreen, GetMousePosition()) > DRAG_THRESHOLD;
}

static Rectangle DragBox(void)
{
    Vector2 a = dragStartWorld, b = CamMouseWorld();
    return (Rectangle){ fminf(a.x, b.x), fminf(a.y, b.y), fabsf(a.x - b.x), fabsf(a.y - b.y) };
}

// --- Selection list -------------------------------------------------------------

// Drop entries whose unit died (or whose slot now holds a different unit).
static void PruneSelection(void)
{
    int kept = 0;
    for (int k = 0; k < selCount; k++)
    {
        if (!UnitIsAlive(selUnits[k], selSerials[k]) || !UnitIsActiveInWorld(&units[selUnits[k]])) continue;   // dead, or boarded a transport
        selUnits[kept] = selUnits[k];
        selSerials[kept] = selSerials[k];
        kept++;
    }
    selCount = kept;
}

static void SelectUnit(int id)
{
    if (units[id].selected) return;
    units[id].selected = true;
    selUnits[selCount] = id;
    selSerials[selCount] = units[id].serial;
    selCount++;
}

static void ClearSelection(void)
{
    for (int k = 0; k < selCount; k++)
    {
        if (UnitIsAlive(selUnits[k], selSerials[k])) units[selUnits[k]].selected = false;
    }
    selCount = 0;
    selBuilding = -1;
    selNode = -1;
}

// Copy the live selection into `found` (orders may reorder it). Returns the count.
static int CollectSelected(void)
{
    PruneSelection();
    memcpy(found, selUnits, selCount*sizeof(int));
    return selCount;
}

bool InputHasSelection(void)
{
    return selCount > 0 || selBuilding != -1 || selNode != -1;
}

int InputSelectedUnits(const int **ids)
{
    PruneSelection();
    *ids = selUnits;
    return selCount;
}

int InputSelectedBuilding(void)
{
    if (selBuilding != -1 && !BuildingIsAlive(selBuilding, selBuildingSerial)) selBuilding = -1;
    return selBuilding;
}

int InputSelectedNode(void)
{
    if (selNode != -1 && !EconomyNodeIsAlive(selNode, selNodeSerial)) selNode = -1;
    return selNode;
}

static void SelectInBox(Rectangle box)
{
    int count = GridQuery(box, found, MAX_UNITS);
    for (int k = 0; k < count; k++)
    {
        if (units[found[k]].team == PLAYER_TEAM) SelectUnit(found[k]);
    }
}

// The closest unit of `team` under the cursor, or -1. Uses its own buffer:
// callers often hold the selection in `found` while asking this.
static int UnitAtPoint(Vector2 point, int team)
{
    static int near[64];
    float reach = UNIT_RADIUS + 2.0f;   // a little forgiveness around small units
    Rectangle area = { point.x - reach, point.y - reach, reach*2.0f, reach*2.0f };
    int count = GridQuery(area, near, 64);

    int best = -1;
    float bestDist = reach;
    for (int k = 0; k < count; k++)
    {
        if (units[near[k]].team != team) continue;
        if (team != PLAYER_TEAM && !FogCanSee(PLAYER_TEAM, units[near[k]].pos)) continue;   // hidden by fog
        float d = Vector2Distance(units[near[k]].pos, point);
        if (d <= bestDist) { best = near[k]; bestDist = d; }
    }
    return best;
}

static void ClickSelect(Vector2 point)
{
    int unit = UnitAtPoint(point, PLAYER_TEAM);
    int building = BuildingAt(point);
    int node = EconomyNodeAt(point);
    if (node != -1 && !FogExplored(PLAYER_TEAM, goldNodes[node].pos)) node = -1;   // never seen

    if (unit != -1) { selBuilding = -1; selNode = -1; SelectUnit(unit); }
    else if (building != -1 && buildings[building].team == PLAYER_TEAM)
    {
        ClearSelection();   // a building is selected on its own
        selBuilding = building;
        selBuildingSerial = buildings[building].serial;
    }
    else if (node != -1)
    {
        ClearSelection();
        selNode = node;
        selNodeSerial = goldNodes[node].serial;
    }
}

// --- Orders ------------------------------------------------------------------------

// Keep only the units in `found` that aren't workers; returns how many.
static int KeepNonWorkers(int count)
{
    int others = 0;
    for (int k = 0; k < count; k++)
    {
        if (units[found[k]].type != UNIT_WORKER) found[others++] = found[k];
    }
    return others;
}

// Right click, in order of priority: enemy unit, enemy building, your
// unfinished building (workers help build), gold node, ground. Before the
// last three, selected healers on a damaged friendly unit follow and heal it
// (the rest of the selection gets the normal order).
static bool UnloadModifierDown(void)
{
#if defined(__EMSCRIPTEN__)
    return IsKeyDown(KEY_UNLOAD_MODIFIER) || IsKeyDown(KEY_RIGHT_SHIFT);
#else
    return IsKeyDown(KEY_UNLOAD_MODIFIER) || IsKeyDown(KEY_RIGHT_CONTROL);
#endif
}

// Split `found` into transports (moved to `transports`) and the rest (kept in `found`).
static int SplitTransports(int count, int *transports, int *nTransports)
{
    int rest = 0;
    *nTransports = 0;
    for (int k = 0; k < count; k++)
    {
        if (IsTransport(found[k])) transports[(*nTransports)++] = found[k];
        else found[rest++] = found[k];
    }
    return rest;
}

static void OrderSelected(Vector2 point)
{
    int count = CollectSelected();
    if (count == 0)
    {
        int b = InputSelectedBuilding();
        if (b != -1) BuildingSetRally(b, point);   // a building is selected: right click = rally point
        return;
    }

    // Unload modifier held: selected transports fly there and unload; everyone else moves there.
    static int transports[MAX_UNITS];
    int nt;
    if (UnloadModifierDown())
    {
        int rest = SplitTransports(count, transports, &nt);
        if (nt > 0)
        {
            for (int k = 0; k < nt; k++) TransportOrderUnload(transports[k], point);
            UnitsOrderMove(found, rest, point);
            return;
        }
    }

    // Right click on one of our transports: the selected units that can be carried board it.
    int own = UnitAtPoint(point, PLAYER_TEAM);
    if (own != -1 && IsTransport(own))
    {
        static int boarders[MAX_UNITS];
        int nb = 0, rest = 0;
        for (int k = 0; k < count; k++)
        {
            if (found[k] != own && UNIT_STATS[units[found[k]].type].cargoSlots > 0) boarders[nb++] = found[k];
            else found[rest++] = found[k];
        }
        if (nb > 0)
        {
            TransportOrderBoard(boarders, nb, own);
            UnitsOrderMove(found, rest, point);   // flyers and other transports just go there
            return;
        }
        count = rest + nb;   // nobody can board: an ordinary right click below
    }

    int enemy = UnitAtPoint(point, AI_TEAM);
    int building = BuildingAt(point);
    bool enemyBuilding = (building != -1 && buildings[building].team != PLAYER_TEAM && FogCanSeeRect(PLAYER_TEAM, BuildingRect(building)));
    if (building != -1 && buildings[building].team != PLAYER_TEAM && !enemyBuilding) building = -1;   // in fog: just ground
    bool unfinished = (building != -1 && !enemyBuilding && buildings[building].constructing);
    int node = EconomyNodeAt(point);
    if (node != -1 && !FogExplored(PLAYER_TEAM, goldNodes[node].pos)) node = -1;   // never seen

    int ally = (enemy == -1 && !enemyBuilding) ? UnitAtPoint(point, PLAYER_TEAM) : -1;
    if (ally != -1 && UnitNeedsHealing(ally))
    {
        static int healers[MAX_UNITS];
        int n = 0, others = 0;
        for (int k = 0; k < count; k++)
        {
            if (UNIT_STATS[units[found[k]].type].canHeal && found[k] != ally) healers[n++] = found[k];
            else found[others++] = found[k];
        }
        HealOrderFollow(healers, n, ally);
        count = others;
        if (count == 0) return;
    }

    if (enemy != -1 || enemyBuilding)
    {
        for (int k = 0; k < count; k++)   // a direct order replaces attack-move / hold
        {
            units[found[k]].attackMove = false;
            units[found[k]].holdPosition = false;
        }
        if (enemy != -1) UnitsOrderAttack(found, count, enemy);
        else UnitsOrderAttackBuilding(found, count, building);
    }
    else if (unfinished)
    {
        BuildingsOrderConstruct(found, count, building);   // workers only
        UnitsOrderMove(found, KeepNonWorkers(count), point);
    }
    else if (node != -1)
    {
        EconomyOrderGather(found, count, node);            // workers only
        UnitsOrderMove(found, KeepNonWorkers(count), point);
    }
    else UnitsOrderMove(found, count, point);
}

void InputMoveSelectedTo(Vector2 worldPos)
{
    int count = CollectSelected();
    UnitsOrderMove(found, count, worldPos);
}

// --- Building placement ---------------------------------------------------------

static int NearestSelectedWorker(Vector2 to)
{
    int count = CollectSelected();
    int best = -1;
    float bestDist = 0.0f;
    for (int k = 0; k < count; k++)
    {
        const Unit *u = &units[found[k]];
        if (u->type != UNIT_WORKER) continue;
        float d = Vector2Distance(u->pos, to);
        if (best == -1 || d < bestDist) { best = found[k]; bestDist = d; }
    }
    return best;
}

void InputTogglePlacement(BuildingType type)
{
    placing = !(placing && placingType == type);
    placingType = type;
    attackMoveArmed = false;
}

bool InputIsPlacing(BuildingType type)
{
    return placing && placingType == type;
}

// Can the player put this building here? One answer for the ghost's colour and
// the click: its prerequisite, then the placement rule (BuildingCanPlace).
bool InputPlacementOK(BuildingType type, Vector2 at, const char **why)
{
    if (!BuildingsCanBuild(PLAYER_TEAM, type))
    {
        if (why) *why = TextFormat("Requires %s", BUILDING_STATS[BUILDING_STATS[type].requires].name);
        return false;
    }
    return BuildingCanPlace(type, at, why);
}

// Pay, place the unfinished building, and send the nearest selected worker.
static void PlaceBuilding(Vector2 at)
{
    int worker = NearestSelectedWorker(at);
    if (worker == -1) { placing = false; return; }
    if (!BuildingsCanBuild(PLAYER_TEAM, placingType))   // e.g. the Barracks was destroyed while placing
    {
        UiShowMessage(TextFormat("Requires %s", BUILDING_STATS[BUILDING_STATS[placingType].requires].name));
        placing = false;
        return;
    }
    const char *why;
    if (!InputPlacementOK(placingType, at, &why)) { UiShowMessage(why); return; }   // e.g. "Dock must be next to water"
    if (!EconomySpend(PLAYER_TEAM, BUILDING_STATS[placingType].cost)) { UiShowMessage("Not enough gold"); return; }

    int site = BuildingPlace(placingType, PLAYER_TEAM, at, true);
    if (site == -1) { EconomyAdd(PLAYER_TEAM, BUILDING_STATS[placingType].cost); return; }   // building pool full
    BuildingsOrderConstruct(&worker, 1, site);
    placing = false;
}

// --- Per frame -------------------------------------------------------------------

void InputUpdate(void)
{
    bool overUi = UiWantsMouse();

    if (placing)
    {
        if (NearestSelectedWorker(CamMouseWorld()) == -1) placing = false;   // workers deselected or dead
        else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !overUi) { PlaceBuilding(CamMouseWorld()); return; }
        else if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) || IsKeyPressed(KEY_PAUSE)) { placing = false; return; }
    }

    if (IsKeyPressed(KEY_ATTACK_MOVE)) { attackMoveArmed = true; placing = false; }
    if (IsKeyPressed(KEY_STOP) || IsKeyPressed(KEY_HOLD))
    {
        int count = CollectSelected();
        if (IsKeyPressed(KEY_STOP)) UnitsOrderStop(found, count);
        else UnitsOrderHold(found, count);
    }
    // Transports in the selection: KEY_LOAD = nearby idle units board, KEY_UNLOAD = unload below it.
    // (With an Air Factory selected, the same letters train units: a building is never selected with units.)
    if (IsKeyPressed(KEY_LOAD) || IsKeyPressed(KEY_UNLOAD))
    {
        int count = CollectSelected();
        for (int k = 0; k < count; k++)
        {
            if (!IsTransport(found[k])) continue;
            if (IsKeyPressed(KEY_LOAD)) TransportLoadNearby(found[k]);
            else TransportOrderUnload(found[k], units[found[k]].pos);
        }
    }

    if (attackMoveArmed)
    {
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && !overUi)
        {
            int count = CollectSelected();
            if (count > 0)
            {
                markerPos = CamMouseWorld();
                markerTime = GetTime();
                UnitsOrderAttackMove(found, count, markerPos);
            }
            attackMoveArmed = false;
            return;   // this click was the attack-move, not a normal move order
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsKeyPressed(KEY_PAUSE))
        {
            attackMoveArmed = false;   // cancel
            return;
        }
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !overUi)
    {
        leftHeld = true;
        dragStartScreen = GetMousePosition();
        dragStartWorld = CamMouseWorld();
    }

    if (leftHeld && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        bool additive = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        if (!additive) ClearSelection();

        if (IsDragging()) { selBuilding = -1; selNode = -1; SelectInBox(DragBox()); }
        else ClickSelect(CamMouseWorld());

        leftHeld = false;
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && !overUi) OrderSelected(CamMouseWorld());
}

// A small "X" drawn with two thin rotated rectangles (plain quads, so it batches).
static void DrawCross(Vector2 at, float size, Color color)
{
    float thick = 2.0f/gameCamera.zoom;
    Rectangle bar = { at.x, at.y, size*2.0f, thick };
    Vector2 pivot = { size, thick*0.5f };
    DrawRectanglePro(bar, pivot, 45.0f, color);
    DrawRectanglePro(bar, pivot, -45.0f, color);
}

void InputDraw(void)
{
    float size = MARKER_SIZE/gameCamera.zoom;   // same size on screen at any zoom
    float line = 2.0f/gameCamera.zoom;

    int b = InputSelectedBuilding();
    if (b != -1)
    {
        DrawRectangleLinesEx(BuildingRect(b), line, BOX_COLOR);
        if (BUILDING_STATS[buildings[b].type].damage > 0.0f)   // a tower: its range
            DrawRing(BuildingCentre(b), BUILDING_STATS[buildings[b].type].range - line, BUILDING_STATS[buildings[b].type].range, 0.0f, 360.0f, RANGE_RING_SEGMENTS, RANGE_COLOR);

        // Rally flag: a pole and a small cloth (plain rectangles, so it batches).
        // Only on buildings that train units (a tower has nothing to send there).
        bool trains = false;
        for (int t = 0; t < UNIT_TYPE_COUNT; t++) trains |= (UNIT_STATS[t].trainedAt == buildings[b].type);
        Vector2 r = buildings[b].rally;
        float s = 1.0f/gameCamera.zoom;   // constant size on screen
        if (trains)
        {
            DrawRectangleRec((Rectangle){ r.x - 1.0f*s, r.y - 18.0f*s, 2.0f*s, 18.0f*s }, RAYWHITE);
            DrawRectangleRec((Rectangle){ r.x + 1.0f*s, r.y - 18.0f*s, 10.0f*s, 7.0f*s }, RALLY_COLOR);
        }
    }
    int n = InputSelectedNode();
    if (n != -1) DrawCircleLinesV(goldNodes[n].pos, 16.0f, BOX_COLOR);

    // Placement ghost: the footprint under the mouse, green if it can go there.
    if (placing)
    {
        Vector2 at = CamMouseWorld();
        Rectangle r = BuildingFootprint(placingType, at);
        Color c = InputPlacementOK(placingType, at, NULL) ? GHOST_OK : GHOST_BLOCKED;
        DrawRectangleRec(r, Fade(c, 0.35f));
        DrawRectangleLinesEx(r, line, c);
        float range = BUILDING_STATS[placingType].range;
        if (BUILDING_STATS[placingType].damage > 0.0f)   // placing a tower: what it would cover
            DrawRing((Vector2){ r.x + r.width*0.5f, r.y + r.height*0.5f }, range - line, range, 0.0f, 360.0f, RANGE_RING_SEGMENTS, RANGE_COLOR);
    }

    // Attack-move armed: a red cross follows the mouse.
    if (attackMoveArmed) DrawCross(CamMouseWorld(), size, AMOVE_COLOR);

    // Destination marker, fading out.
    double age = GetTime() - markerTime;
    if (age < MARKER_TIME) DrawCross(markerPos, size, Fade(AMOVE_COLOR, 1.0f - (float)(age/MARKER_TIME)));

    if (!IsDragging()) return;
    Rectangle box = DragBox();
    DrawRectangleRec(box, BOX_FILL);
    DrawRectangleLinesEx(box, 1.0f/gameCamera.zoom, BOX_COLOR);   // 1 screen pixel thick at any zoom
}

bool InputHasPendingCommand(void)
{
    return attackMoveArmed || placing;
}

void InputReset(void)
{
    leftHeld = false;
    attackMoveArmed = false;
    placing = false;
    markerTime = -100.0;
    selCount = 0;
    selBuilding = -1;
    selNode = -1;
}
