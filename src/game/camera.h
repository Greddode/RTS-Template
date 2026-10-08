// camera.h - RTS camera: pan, zoom, and "what's on screen" queries.
#ifndef CAMERA_H_INCLUDED
#define CAMERA_H_INCLUDED

#include "raylib.h"

extern Camera2D gameCamera;

void      CamInit(void);
void      CamLookAt(Vector2 worldPos);   // centre the view on a point (e.g. the player's base)
void      CamUpdate(float dt);   // read keyboard/mouse, call once per frame
void      CamUpdateEditor(float dt, Rectangle view);   // the editor's: world shown in `view`, can zoom out until the whole map fits
float     CamFitZoom(Rectangle view);   // zoom at which the whole map fits in `view` (screen pixels)
Rectangle CamViewRect(void);     // visible area in world coords, for culling
Vector2   CamMouseWorld(void);   // mouse position in world coords

#endif
