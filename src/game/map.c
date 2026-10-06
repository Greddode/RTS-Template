// map.c - Terrain storage, generation and drawing.
//
// The whole map is one flat array of tile types, indexed tiles[y * MAP_W + x].
// A second array, `blocked`, marks tiles covered by buildings (buildings.c
// sets it). A tile is walkable only if its terrain is open AND it's not blocked,
// so pathfinding and movement avoid buildings without knowing about them.
// MapGenerate() scatters seeded blobs of dirt/water/rock on grass. Swap it for
// your own map loader when you have real maps.

#include "map.h"
#include <math.h>

#define LINE_SAMPLE_STEP (TILE_SIZE / 4.0f)   // smaller = more exact line checks, but slower

static unsigned char tiles[MAP_W * MAP_H];
static bool          blocked[MAP_W * MAP_H];

static const Color tileColors[TILE_COUNT] = {
    [TILE_GRASS] = {  70, 120,  60, 255 },
    [TILE_DIRT]  = { 125, 105,  70, 255 },
    [TILE_WATER] = {  50,  90, 160, 255 },
    [TILE_ROCK]  = {  95,  95, 100, 255 },
};

// Tiny private random generator so map generation doesn't disturb
// (or depend on) raylib's GetRandomValue().
static unsigned int rngState;
static int RandRange(int min, int max)
{
    rngState = rngState * 1103515245u + 12345u;
    return min + (int)((rngState >> 16) % (unsigned int)(max - min + 1));
}

static void PaintBlob(int cx, int cy, int radius, TileType type)
{
    for (int y = cy - radius; y <= cy + radius; y++)
    {
        for (int x = cx - radius; x <= cx + radius; x++)
        {
            if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) continue;
            int dx = x - cx, dy = y - cy;
            if (dx*dx + dy*dy <= radius*radius) tiles[y*MAP_W + x] = (unsigned char)type;
        }
    }
}

void MapGenerate(unsigned int seed)
{
    rngState = seed;
    for (int i = 0; i < MAP_W*MAP_H; i++) tiles[i] = TILE_GRASS;

    for (int i = 0; i < 40; i++) PaintBlob(RandRange(0, MAP_W - 1), RandRange(0, MAP_H - 1), RandRange(2, 6), TILE_DIRT);
    for (int i = 0; i < 14; i++) PaintBlob(RandRange(0, MAP_W - 1), RandRange(0, MAP_H - 1), RandRange(2, 5), TILE_WATER);
    for (int i = 0; i < 14; i++) PaintBlob(RandRange(0, MAP_W - 1), RandRange(0, MAP_H - 1), RandRange(1, 3), TILE_ROCK);

    // Keep the middle clear for the starting units.
    PaintBlob(MAP_W/2, MAP_H/2, 12, TILE_GRASS);
}

TileType MapGetTile(int tx, int ty)
{
    if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) return TILE_ROCK;
    return (TileType)tiles[ty*MAP_W + tx];
}

bool MapTileWalkable(int tx, int ty)
{
    TileType t = MapGetTile(tx, ty);   // out of bounds = rock
    return (t == TILE_GRASS || t == TILE_DIRT) && !blocked[ty*MAP_W + tx];
}

void MapSetBlocked(int tx, int ty, int w, int h, bool isBlocked)
{
    for (int y = ty; y < ty + h; y++)
    {
        for (int x = tx; x < tx + w; x++)
        {
            if (x >= 0 && y >= 0 && x < MAP_W && y < MAP_H) blocked[y*MAP_W + x] = isBlocked;
        }
    }
}

void MapClearArea(Vector2 worldPos, int radiusTiles)
{
    PaintBlob((int)(worldPos.x/TILE_SIZE), (int)(worldPos.y/TILE_SIZE), radiusTiles, TILE_GRASS);
}

bool MapIsWalkable(Vector2 worldPos)
{
    return MapTileWalkable((int)floorf(worldPos.x / TILE_SIZE), (int)floorf(worldPos.y / TILE_SIZE));
}

// Checks the four edge points of the circle: cheap, and close enough for
// units that are much smaller than a tile.
bool MapCircleWalkable(Vector2 c, float r)
{
    return MapIsWalkable((Vector2){ c.x + r, c.y }) && MapIsWalkable((Vector2){ c.x - r, c.y }) &&
           MapIsWalkable((Vector2){ c.x, c.y + r }) && MapIsWalkable((Vector2){ c.x, c.y - r });
}

// Samples the line every LINE_SAMPLE_STEP pixels and checks the unit fits at each sample.
bool MapLineClear(Vector2 from, Vector2 to, float radius)
{
    float dx = to.x - from.x, dy = to.y - from.y;
    int steps = (int)(sqrtf(dx*dx + dy*dy) / LINE_SAMPLE_STEP) + 1;
    for (int i = 0; i <= steps; i++)
    {
        float t = (float)i / steps;
        if (!MapCircleWalkable((Vector2){ from.x + dx*t, from.y + dy*t }, radius)) return false;
    }
    return true;
}

void MapDraw(Rectangle view)
{
    // Grass is the most common tile, so paint the whole map grass in one
    // rectangle and only draw the other tiles on top: far fewer draw calls.
    DrawRectangle(0, 0, MAP_PIXEL_W, MAP_PIXEL_H, tileColors[TILE_GRASS]);

    // Only loop over the tiles the camera can see.
    int x0 = (int)floorf(view.x / TILE_SIZE),                y0 = (int)floorf(view.y / TILE_SIZE);
    int x1 = (int)floorf((view.x + view.width) / TILE_SIZE), y1 = (int)floorf((view.y + view.height) / TILE_SIZE);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > MAP_W - 1) x1 = MAP_W - 1;
    if (y1 > MAP_H - 1) y1 = MAP_H - 1;

    for (int y = y0; y <= y1; y++)
    {
        for (int x = x0; x <= x1; x++)
        {
            TileType t = (TileType)tiles[y*MAP_W + x];
            if (t != TILE_GRASS) DrawRectangle(x*TILE_SIZE, y*TILE_SIZE, TILE_SIZE, TILE_SIZE, tileColors[t]);
        }
    }
}
