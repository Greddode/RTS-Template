// camera.c - Camera controls.
//   Arrow keys          pan (letter keys are kept free for unit commands)
//   Middle mouse drag   pan
//   Mouse wheel         zoom toward the cursor
//
// Uses raylib's Camera2D: `target` is the world point shown at the screen
// centre (`offset`), and `zoom` scales the world.

#include "camera.h"
#include "map.h"
#include "raymath.h"

#define CAM_PAN_SPEED 800.0f   // screen pixels per second
#define CAM_ZOOM_MIN  0.5f
#define CAM_ZOOM_MAX  2.0f
#define CAM_ZOOM_STEP 0.1f     // per wheel notch

Camera2D gameCamera;

void CamInit(void)
{
    gameCamera.target = (Vector2){ MAP_PIXEL_W/2.0f, MAP_PIXEL_H/2.0f };
    gameCamera.offset = (Vector2){ GetScreenWidth()/2.0f, GetScreenHeight()/2.0f };
    gameCamera.rotation = 0.0f;
    gameCamera.zoom = 1.0f;
}

void CamUpdate(float dt)
{
    // Keep the camera centred if the window/canvas size changes.
    gameCamera.offset = (Vector2){ GetScreenWidth()/2.0f, GetScreenHeight()/2.0f };

    // Keyboard pan. Dividing by zoom keeps the on-screen speed the same at any zoom.
    Vector2 dir = { 0 };
    if (IsKeyDown(KEY_LEFT))  dir.x -= 1;
    if (IsKeyDown(KEY_RIGHT)) dir.x += 1;
    if (IsKeyDown(KEY_UP))    dir.y -= 1;
    if (IsKeyDown(KEY_DOWN))  dir.y += 1;
    gameCamera.target = Vector2Add(gameCamera.target, Vector2Scale(dir, CAM_PAN_SPEED*dt/gameCamera.zoom));

    // Middle-mouse drag: the world follows the mouse.
    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE))
    {
        gameCamera.target = Vector2Subtract(gameCamera.target, Vector2Scale(GetMouseDelta(), 1.0f/gameCamera.zoom));
    }

    // Wheel zoom. Afterwards, shift the camera so the world point under the
    // mouse stays under the mouse (zooming "toward the cursor").
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f)
    {
        Vector2 before = CamMouseWorld();
        gameCamera.zoom = Clamp(gameCamera.zoom*(1.0f + CAM_ZOOM_STEP*wheel), CAM_ZOOM_MIN, CAM_ZOOM_MAX);
        Vector2 after = CamMouseWorld();
        gameCamera.target = Vector2Add(gameCamera.target, Vector2Subtract(before, after));
    }

    // Don't let the screen centre leave the map.
    gameCamera.target.x = Clamp(gameCamera.target.x, 0.0f, (float)MAP_PIXEL_W);
    gameCamera.target.y = Clamp(gameCamera.target.y, 0.0f, (float)MAP_PIXEL_H);
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
