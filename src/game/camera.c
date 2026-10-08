// camera.c - Camera controls.
//   Arrow keys          pan (letter keys are kept free for unit commands)
//   Middle mouse drag   pan
//   Mouse wheel         zoom toward the cursor
//
// Uses raylib's Camera2D: `target` is the world point shown at the screen
// centre (`offset`), and `zoom` scales the world.
//
// The map editor uses CamUpdateEditor() instead: the world is shown in the
// area beside its tool panel, and the furthest it zooms out is computed from
// the map and window size so the WHOLE map fits there (CamFitZoom). The game
// keeps the fixed CAM_ZOOM_MIN / CAM_ZOOM_MAX.

#include "camera.h"
#include "map.h"
#include "ui.h"
#include "raymath.h"
#include <math.h>

#define CAM_PAN_SPEED 800.0f   // screen pixels per second
#define CAM_ZOOM_MIN  0.5f
#define CAM_ZOOM_MAX  2.0f
#define CAM_ZOOM_STEP 0.1f     // per wheel notch

Camera2D gameCamera;

void CamInit(void)
{
    gameCamera.target = (Vector2){ MapWidth()*TILE_SIZE/2.0f, MapHeight()*TILE_SIZE/2.0f };
    gameCamera.offset = (Vector2){ GetScreenWidth()/2.0f, GetScreenHeight()/2.0f };
    gameCamera.rotation = 0.0f;
    gameCamera.zoom = 1.0f;
}

void CamLookAt(Vector2 worldPos)
{
    gameCamera.target = worldPos;
}

// Pan and zoom. `view`: the screen area the world is shown in (its centre is
// the camera's centre); zoom stays within zoomMin..CAM_ZOOM_MAX.
static void Update(float dt, Rectangle view, float zoomMin)
{
    // Keep the camera centred if the window/canvas size changes.
    gameCamera.offset = (Vector2){ view.x + view.width/2.0f, view.y + view.height/2.0f };

    // Keyboard pan. Dividing by zoom keeps the on-screen speed the same at any zoom.
    Vector2 dir = { 0 };
    if (IsKeyDown(KEY_LEFT))  dir.x -= 1;
    if (IsKeyDown(KEY_RIGHT)) dir.x += 1;
    if (IsKeyDown(KEY_UP))    dir.y -= 1;
    if (IsKeyDown(KEY_DOWN))  dir.y += 1;
    gameCamera.target = Vector2Add(gameCamera.target, Vector2Scale(dir, CAM_PAN_SPEED*dt/gameCamera.zoom));

    bool overUi = UiWantsMouse();   // the wheel / drag belong to the UI there

    // Middle-mouse drag: the world follows the mouse.
    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) && !overUi)
    {
        gameCamera.target = Vector2Subtract(gameCamera.target, Vector2Scale(GetMouseDelta(), 1.0f/gameCamera.zoom));
    }

    // Wheel zoom. Afterwards, shift the camera so the world point under the
    // mouse stays under the mouse (zooming "toward the cursor").
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f && !overUi)
    {
        Vector2 before = CamMouseWorld();
        gameCamera.zoom = Clamp(gameCamera.zoom*(1.0f + CAM_ZOOM_STEP*wheel), zoomMin, CAM_ZOOM_MAX);
        Vector2 after = CamMouseWorld();
        gameCamera.target = Vector2Add(gameCamera.target, Vector2Subtract(before, after));
    }

    // Don't let the screen centre leave the map.
    gameCamera.target.x = Clamp(gameCamera.target.x, 0.0f, (float)(MapWidth()*TILE_SIZE));
    gameCamera.target.y = Clamp(gameCamera.target.y, 0.0f, (float)(MapHeight()*TILE_SIZE));
}

void CamUpdate(float dt)
{
    Update(dt, (Rectangle){ 0, 0, (float)GetScreenWidth(), (float)GetScreenHeight() }, CAM_ZOOM_MIN);
}

float CamFitZoom(Rectangle view)
{
    float fit = fminf(view.width/(MapWidth()*TILE_SIZE), view.height/(MapHeight()*TILE_SIZE));
    return Clamp(fit, 0.01f, CAM_ZOOM_MAX);   // a tiny map in a big window: never past the normal maximum
}

void CamUpdateEditor(float dt, Rectangle view)
{
    float zoomMin = CamFitZoom(view);
    gameCamera.zoom = Clamp(gameCamera.zoom, zoomMin, CAM_ZOOM_MAX);   // the window or map may have changed size
    Update(dt, view, zoomMin);

    // Where the whole map fits across (or down), keep it centred that way.
    float mapW = (float)(MapWidth()*TILE_SIZE), mapH = (float)(MapHeight()*TILE_SIZE);
    if (mapW*gameCamera.zoom <= view.width + 0.5f) gameCamera.target.x = mapW/2.0f;
    if (mapH*gameCamera.zoom <= view.height + 0.5f) gameCamera.target.y = mapH/2.0f;
}

Rectangle CamViewRect(void)
{
    Vector2 topLeft = GetScreenToWorld2D((Vector2){ 0, 0 }, gameCamera);
    Vector2 bottomRight = GetScreenToWorld2D((Vector2){ (float)GetScreenWidth(), (float)GetScreenHeight() }, gameCamera);
    return (Rectangle){ topLeft.x, topLeft.y, bottomRight.x - topLeft.x, bottomRight.y - topLeft.y };
}

Vector2 CamMouseWorld(void)
{
    return GetScreenToWorld2D(GetMousePosition(), gameCamera);
}
