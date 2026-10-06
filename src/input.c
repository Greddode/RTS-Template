// input.c - Selection and orders.
//   Left click          select the unit under the cursor
//   Left drag           box select
//   Shift + click/drag  add to the current selection
//   Right click         move selected units there
//   Right click enemy   attack that unit
//   A, then right click attack-move there (left click or Esc cancels)
//   S                   stop: drop all orders
//   H                   hold position: attack only what's in range, never chase
//   Right click gold    selected workers mine it (other units walk there)
//   Click own base      select it; W queues a worker
//
// Only the player's own units and buildings can be selected. A selected
// building is remembered as (slot, serial), so a destroyed one is dropped.
//
// The drag start is stored in world coords, so the box stays anchored to the
// ground even if the camera pans mid-drag.

#include "input.h"
#include "buildings.h"
#include "camera.h"
#include "economy.h"
#include "grid.h"
#include "units.h"
#include "raymath.h"
#include <math.h>

#define DRAG_THRESHOLD 4.0f   // screen pixels the mouse must move before a click becomes a drag
#define BOX_COLOR      (Color){ 60, 255, 90, 255 }
#define BOX_FILL       (Color){ 60, 255, 90, 40 }
#define AMOVE_COLOR    (Color){ 255, 70, 50, 255 }
#define MARKER_TIME    1.0    // seconds the destination marker stays visible
#define MARKER_SIZE    8.0f   // screen pixels

static bool    leftHeld = false;
static bool    attackMoveArmed = false;   // A was pressed: next right click is an attack-move
static Vector2 markerPos;
static double  markerTime = -100.0;

static int          selBuilding = -1;    // selected building: slot...
static unsigned int selBuildingSerial;   // ...and serial
static const char  *message = "";        // short feedback ("Not enough gold")
static double       messageTime = -100.0;
#define MESSAGE_TIME 1.5
static Vector2 dragStartWorld;
static Vector2 dragStartScreen;

static int found[MAX_UNITS];   // scratch buffer for grid queries

static bool IsDragging(void)
{
    return leftHeld && Vector2Distance(dragStartScreen, GetMousePosition()) > DRAG_THRESHOLD;
}

static Rectangle DragBox(void)
{
    Vector2 a = dragStartWorld, b = CamMouseWorld();
    return (Rectangle){ fminf(a.x, b.x), fminf(a.y, b.y), fabsf(a.x - b.x), fabsf(a.y - b.y) };
}

static int SelectedBuilding(void)
{
    return BuildingIsAlive(selBuilding, selBuildingSerial) ? selBuilding : -1;
}

static void ShowMessage(const char *text)
{
    message = text;
    messageTime = GetTime();
}

// Selection only changes on a click, so scanning the pool here is fine.
static void ClearSelection(void)
{
    for (int i = 0; i < MAX_UNITS; i++) units[i].selected = false;
    selBuilding = -1;
}

static void SelectInBox(Rectangle box)
{
    int count = GridQuery(box, found, MAX_UNITS);
    for (int k = 0; k < count; k++)
    {
        if (units[found[k]].team == PLAYER_TEAM) units[found[k]].selected = true;
    }
}

// The closest unit of `team` under the cursor, or -1.
static int UnitAtPoint(Vector2 point, int team)
{
    float reach = UNIT_RADIUS + 2.0f;   // a little forgiveness around small units
    Rectangle area = { point.x - reach, point.y - reach, reach*2.0f, reach*2.0f };
    int count = GridQuery(area, found, MAX_UNITS);

    int best = -1;
    float bestDist = reach;
    for (int k = 0; k < count; k++)
    {
        if (units[found[k]].team != team) continue;
        float d = Vector2Distance(units[found[k]].pos, point);
        if (d <= bestDist) { best = found[k]; bestDist = d; }
    }
    return best;
}

static int CollectSelected(void)
{
    int count = 0;
    for (int i = 0; i < MAX_UNITS; i++)
    {
        if (units[i].active && units[i].selected) found[count++] = i;
    }
    return count;
}

// Right click, in order of priority: enemy unit, enemy building, gold node, ground.
static void OrderSelected(Vector2 point)
{
    int count = CollectSelected();
    if (count == 0) return;

    int enemy = UnitAtPoint(point, AI_TEAM);
    int building = BuildingAt(point);
    if (building != -1 && buildings[building].team == PLAYER_TEAM) building = -1;   // own building: just move
    int node = EconomyNodeAt(point);

    if (enemy != -1 || building != -1)
    {
        for (int k = 0; k < count; k++)   // a direct order replaces attack-move / hold
        {
            units[found[k]].attackMove = false;
            units[found[k]].holdPosition = false;
        }
        if (enemy != -1) UnitsOrderAttack(found, count, enemy);
        else UnitsOrderAttackBuilding(found, count, building);
    }
    else if (node != -1)
    {
        EconomyOrderGather(found, count, node);   // workers only
        int others = 0;
        for (int k = 0; k < count; k++)
        {
            if (units[found[k]].type != UNIT_WORKER) found[others++] = found[k];
        }
        UnitsOrderMove(found, others, point);
    }
    else UnitsOrderMove(found, count, point);
}

static void TrainAtSelectedBuilding(UnitType type)
{
    int b = SelectedBuilding();
    if (b == -1) return;
    if (buildings[b].queueCount >= MAX_QUEUE) ShowMessage("Queue full");
    else if (!BuildingQueueTrain(b, type)) ShowMessage("Not enough gold");
}

void InputUpdate(void)
{
    if (IsKeyPressed(KEY_A)) attackMoveArmed = true;
    if (IsKeyPressed(KEY_W)) TrainAtSelectedBuilding(UNIT_WORKER);
    if (IsKeyPressed(KEY_S) || IsKeyPressed(KEY_H))
    {
        int count = CollectSelected();
        if (IsKeyPressed(KEY_S)) UnitsOrderStop(found, count);
        else UnitsOrderHold(found, count);
    }

    if (attackMoveArmed)
    {
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
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
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsKeyPressed(KEY_ESCAPE))
        {
            attackMoveArmed = false;   // cancel
            return;
        }
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        leftHeld = true;
        dragStartScreen = GetMousePosition();
        dragStartWorld = CamMouseWorld();
    }

    if (leftHeld && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        bool additive = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        if (!additive) ClearSelection();

        if (IsDragging()) { selBuilding = -1; SelectInBox(DragBox()); }
        else
        {
            Vector2 mouse = CamMouseWorld();
            int picked = UnitAtPoint(mouse, PLAYER_TEAM);
            int building = BuildingAt(mouse);
            if (picked != -1) { selBuilding = -1; units[picked].selected = true; }
            else if (building != -1 && buildings[building].team == PLAYER_TEAM)
            {
                ClearSelection();   // a building is selected on its own
                selBuilding = building;
                selBuildingSerial = buildings[building].serial;
            }
        }

        leftHeld = false;
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) OrderSelected(CamMouseWorld());
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

    int b = SelectedBuilding();
    if (b != -1) DrawRectangleLinesEx(BuildingRect(b), 2.0f/gameCamera.zoom, BOX_COLOR);

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

// Screen-space panel for the selected building, plus short messages.
void InputDrawHud(void)
{
    int b = SelectedBuilding();
    if (b != -1)
    {
        const Building *bd = &buildings[b];
        int x = 10, y = GetScreenHeight() - 130;
        DrawRectangle(x - 6, y - 6, 300, 66, Fade(BLACK, 0.6f));
        DrawText(TextFormat("Base   HP %d / %d", (int)bd->hp, (int)BUILDING_STATS[bd->type].hp), x, y, 16, RAYWHITE);
        DrawText(TextFormat("Queue %d / %d", bd->queueCount, MAX_QUEUE), x, y + 20, 16, RAYWHITE);
        if (bd->queueCount > 0)
        {
            float frac = bd->trainTicks/(UNIT_STATS[bd->queue[0]].trainTime*TICK_RATE);
            DrawRectangle(x + 110, y + 22, 170, 12, DARKGRAY);
            DrawRectangle(x + 110, y + 22, (int)(170*frac), 12, SKYBLUE);
        }
        DrawText(TextFormat("W: train worker (%d gold)", UNIT_STATS[UNIT_WORKER].cost), x, y + 40, 16, LIGHTGRAY);
    }

    if (GetTime() - messageTime < MESSAGE_TIME)
    {
        int w = MeasureText(message, 20);
        DrawText(message, GetScreenWidth()/2 - w/2, 40, 20, ORANGE);
    }
}
