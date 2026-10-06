// input.c - Selection and orders.
//   Left click          select the unit under the cursor
//   Left drag           box select
//   Shift + click/drag  add to the current selection
//   Right click         move selected units there
//
// The drag start is stored in world coords, so the box stays anchored to the
// ground even if the camera pans mid-drag.

#include "input.h"
#include "camera.h"
#include "grid.h"
#include "units.h"
#include "raymath.h"
#include <math.h>

#define DRAG_THRESHOLD 4.0f   // screen pixels the mouse must move before a click becomes a drag
#define BOX_COLOR      (Color){ 60, 255, 90, 255 }
#define BOX_FILL       (Color){ 60, 255, 90, 40 }

static bool    leftHeld = false;
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

// Selection only changes on a click, so scanning the pool here is fine.
static void ClearSelection(void)
{
    for (int i = 0; i < MAX_UNITS; i++) units[i].selected = false;
}

static void SelectInBox(Rectangle box)
{
    int count = GridQuery(box, found, MAX_UNITS);
    for (int k = 0; k < count; k++) units[found[k]].selected = true;
}

static void SelectAtPoint(Vector2 point)
{
    float reach = UNIT_RADIUS + 2.0f;   // a little forgiveness around small units
    Rectangle area = { point.x - reach, point.y - reach, reach*2.0f, reach*2.0f };
    int count = GridQuery(area, found, MAX_UNITS);

    int best = -1;
    float bestDist = reach;
    for (int k = 0; k < count; k++)
    {
        float d = Vector2Distance(units[found[k]].pos, point);
        if (d <= bestDist) { best = found[k]; bestDist = d; }
    }
    if (best != -1) units[best].selected = true;
}

static void OrderSelectedMove(Vector2 dest)
{
    int count = 0;
    for (int i = 0; i < MAX_UNITS; i++)
    {
        if (units[i].active && units[i].selected) found[count++] = i;
    }
    UnitsOrderMove(found, count, dest);
}

void InputUpdate(void)
{
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

        if (IsDragging()) SelectInBox(DragBox());
        else SelectAtPoint(CamMouseWorld());

        leftHeld = false;
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) OrderSelectedMove(CamMouseWorld());
}

void InputDrawSelectionBox(void)
{
    if (!IsDragging()) return;
    Rectangle box = DragBox();
    DrawRectangleRec(box, BOX_FILL);
    DrawRectangleLinesEx(box, 1.0f/gameCamera.zoom, BOX_COLOR);   // 1 screen pixel thick at any zoom
}
