// minimap.c - The minimap in the bottom-left corner.
//
// Two layers:
//   1. A cached texture: one pixel per tile, the terrain colour from
//      TILE_INFO, black where the player has never looked and dimmed where
//      it's explored but not visible. It's redrawn only when the map or the
//      fog has changed (MapVersion / FogVersion), and at most every
//      MINIMAP_REBUILD_SECONDS. Drawing it is a single textured rectangle.
//   2. Dots drawn on top every frame (plain rectangles, so they batch): gold
//      nodes once explored, buildings (own, or enemy ones seen before) and
//      units (own, or enemies visible right now). Units move all the time, so
//      they're not baked into the texture.
// The camera's view is drawn as an outline.
//
// Clicks: the minimap is a UI area, so the game ignores clicks on it
// (UiWantsMouse). Left click / drag moves the camera there; right click
// sends the selected units there.

#include "minimap.h"
#include "buildings.h"
#include "camera.h"
#include "config.h"
#include "economy.h"
#include "fog.h"
#include "input.h"
#include "map.h"
#include "ui.h"
#include "units.h"

#define EXPLORED_DIM   0.45f                   // brightness of explored-but-not-visible tiles
#define PLAYER_DOT     (Color){ 240, 220, 70, 255 }
#define AI_DOT         (Color){ 230, 60, 50, 255 }
#define GOLD_DOT       (Color){ 255, 200, 40, 255 }
#define VIEW_COLOR     RAYWHITE

static Color     pixels[MAP_W*MAP_H];    // the texture's contents, row by row
static Texture2D texture = { 0 };
static unsigned  builtMapVersion = 0, builtFogVersion = 0;
static double    lastRebuildTime = -100.0, lastRebuildMs = 0.0;
static bool      dragging = false;       // left button went down on the minimap

Rectangle MinimapRect(void)
{
    float size = Ui(MINIMAP_SIZE);
    return (Rectangle){ Ui(10.0f), GetScreenHeight() - Ui(10.0f) - size, size, size };
}

void MinimapReset(void)
{
    builtMapVersion = builtFogVersion = 0;   // forces a redraw
    lastRebuildTime = -100.0;
    dragging = false;
}

double MinimapLastRebuildMs(void)
{
    return lastRebuildMs;
}

// The part of the square the map fills (non-square maps are centred), and
// how many screen pixels one tile gets.
static Rectangle MapArea(float *pixelsPerTile)
{
    Rectangle r = MinimapRect();
    int w = MapWidth(), h = MapHeight();
    float scale = r.width/(float)((w > h) ? w : h);
    *pixelsPerTile = scale;
    return (Rectangle){ r.x + (r.width - w*scale)*0.5f, r.y + (r.height - h*scale)*0.5f, w*scale, h*scale };
}

static Vector2 WorldToMinimap(Rectangle area, float scale, Vector2 world)
{
    return (Vector2){ area.x + world.x/TILE_SIZE*scale, area.y + world.y/TILE_SIZE*scale };
}

static Vector2 MinimapToWorld(Rectangle area, float scale, Vector2 screen)
{
    Vector2 w = { (screen.x - area.x)/scale*TILE_SIZE, (screen.y - area.y)/scale*TILE_SIZE };
    if (w.x < 0) w.x = 0;
    if (w.y < 0) w.y = 0;
    if (w.x > MapWidth()*TILE_SIZE) w.x = (float)(MapWidth()*TILE_SIZE);
    if (w.y > MapHeight()*TILE_SIZE) w.y = (float)(MapHeight()*TILE_SIZE);
    return w;
}

// Redraw the terrain/fog picture if the map or fog changed (rate limited).
static void RebuildIfNeeded(void)
{
    bool changed = (builtMapVersion != MapVersion() || builtFogVersion != FogVersion());
    if (!changed || GetTime() - lastRebuildTime < MINIMAP_REBUILD_SECONDS) return;

    double start = GetTime();
    if (texture.id == 0)   // first use: a fixed MAP_W x MAP_H texture (128x128: a power of two, good for WebGL)
    {
        Image img = GenImageColor(MAP_W, MAP_H, BLACK);
        texture = LoadTextureFromImage(img);
        UnloadImage(img);
        SetTextureFilter(texture, TEXTURE_FILTER_POINT);   // crisp tiles, no blur
    }

    int w = MapWidth(), h = MapHeight();
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            Color c = TILE_INFO[MapGetTile(x, y)].color;
            FogState f = FogTileState(PLAYER_TEAM, x, y);
            if (f == FOG_UNSEEN) c = BLACK;
            else if (f == FOG_EXPLORED) c = (Color){ (unsigned char)(c.r*EXPLORED_DIM), (unsigned char)(c.g*EXPLORED_DIM), (unsigned char)(c.b*EXPLORED_DIM), 255 };
            pixels[y*MAP_W + x] = c;
        }
    }
    UpdateTexture(texture, pixels);

    builtMapVersion = MapVersion();
    builtFogVersion = FogVersion();
    lastRebuildTime = GetTime();
    lastRebuildMs = (lastRebuildTime - start)*1000.0;
}

static void Dot(Rectangle area, float scale, Vector2 world, float size, Color c)
{
    Vector2 p = WorldToMinimap(area, scale, world);
    DrawRectangleRec((Rectangle){ p.x - size*0.5f, p.y - size*0.5f, size, size }, c);
}

static void DrawDots(Rectangle area, float scale)
{
    float unitSize = (scale > 2.0f) ? scale : 2.0f;   // at least 2 screen pixels

    for (int i = 0; i < MAX_GOLD_NODES; i++)
    {
        const GoldNode *n = &goldNodes[i];
        if (n->active && FogExplored(PLAYER_TEAM, n->pos)) Dot(area, scale, n->pos, unitSize*1.5f, GOLD_DOT);
    }
    for (int b = 0; b < MAX_BUILDINGS; b++)
    {
        const Building *bd = &buildings[b];
        if (!bd->active) continue;
        Rectangle r = BuildingRect(b);
        if (bd->team != PLAYER_TEAM && !bd->seenByPlayer && !FogCanSeeRect(PLAYER_TEAM, r)) continue;
        Vector2 p = WorldToMinimap(area, scale, (Vector2){ r.x, r.y });
        float s = bd->size*scale;
        if (s < 3.0f) s = 3.0f;
        DrawRectangleRec((Rectangle){ p.x, p.y, s, s }, (bd->team == PLAYER_TEAM) ? PLAYER_DOT : AI_DOT);
    }
    // A loop over the pool once per frame to draw dots, like UnitsTick - not a "who's nearby" search.
    for (int i = 0; i < MAX_UNITS; i++)
    {
        const Unit *u = &units[i];
        if (!u->active) continue;
        if (u->team != PLAYER_TEAM && !FogCanSee(PLAYER_TEAM, u->pos)) continue;   // enemies: only where visible
        Dot(area, scale, u->pos, unitSize, (u->team == PLAYER_TEAM) ? PLAYER_DOT : AI_DOT);
    }
}

static void DrawCameraView(Rectangle area, float scale)
{
    Rectangle view = CamViewRect();
    Vector2 a = WorldToMinimap(area, scale, (Vector2){ view.x, view.y });
    Vector2 b = WorldToMinimap(area, scale, (Vector2){ view.x + view.width, view.y + view.height });
    // Keep the outline inside the minimap when the view reaches past the map edge.
    Rectangle mm = MinimapRect();
    if (a.x < mm.x) a.x = mm.x;
    if (a.y < mm.y) a.y = mm.y;
    if (b.x > mm.x + mm.width) b.x = mm.x + mm.width;
    if (b.y > mm.y + mm.height) b.y = mm.y + mm.height;
    if (b.x > a.x && b.y > a.y) DrawRectangleLinesEx((Rectangle){ a.x, a.y, b.x - a.x, b.y - a.y }, 1.0f, VIEW_COLOR);
}

static void HandleClicks(Rectangle area, float scale)
{
    Vector2 m = GetMousePosition();
    bool over = CheckCollisionPointRec(m, MinimapRect());

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && over) dragging = true;
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) dragging = false;
    if (dragging) CamLookAt(MinimapToWorld(area, scale, m));   // keeps following if the drag leaves the minimap

    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && over) InputMoveSelectedTo(MinimapToWorld(area, scale, m));
}

void MinimapDraw(void)
{
    if (!MINIMAP_ENABLED) return;
    RebuildIfNeeded();

    Rectangle frame = MinimapRect();
    UiPanel((Rectangle){ frame.x - 2.0f, frame.y - 2.0f, frame.width + 4.0f, frame.height + 4.0f });   // also makes the game ignore clicks here
    DrawRectangleRec(frame, BLACK);

    float scale;
    Rectangle area = MapArea(&scale);
    Rectangle src = { 0, 0, (float)MapWidth(), (float)MapHeight() };
    DrawTexturePro(texture, src, area, (Vector2){ 0, 0 }, 0.0f, WHITE);
    DrawDots(area, scale);
    DrawCameraView(area, scale);
    HandleClicks(area, scale);
}
