// map.c - Terrain storage, generation and drawing.
//
// The whole map is one flat array of tile types, indexed tiles[y * MAP_W + x].
// A second array, `blocked`, marks tiles covered by buildings (buildings.c
// sets it). Who may enter a tile depends on the unit's movement class
// (MoveClass, config.h): TILE_INFO has a column per class, and buildings block
// GROUND and NAVAL units but not AIR. MapTileWalkable(class, x, y) is the one
// check everything uses (pathfinding, movement, formations, regions), so
// pathfinding and movement avoid buildings without knowing about them.
// MapGenerate() scatters seeded blobs of dirt/water/rock on grass. Swap it for
// your own map loader when you have real maps.
// Tiles are coloured squares from TILE_INFO, or art from assets/sprites/tiles.

#include "map.h"
#include "sprites.h"
#include <math.h>
#include <string.h>

#define LINE_SAMPLE_STEP (TILE_SIZE / 4.0f)   // smaller = more exact line checks, but slower

static unsigned char tiles[MAP_W * MAP_H];
static bool          blocked[MAP_W * MAP_H];
static int           mapWidth = MAP_W, mapHeight = MAP_H;   // this map's real size (<= MAP_W x MAP_H)
static unsigned int  version = 1;                         // bumped on every terrain change

// Colours were picked to stay easy to tell apart when dimmed by the fog of war
// and on the minimap (see README, "Tiles and movement classes").
const TileInfo TILE_INFO[TILE_COUNT] = {
    //               name      char  color                      ground  naval  air
    [TILE_GRASS]  = { "Grass",  '.', {  70, 120,  60, 255 },   true,   false, true },
    [TILE_DIRT]   = { "Dirt",   ',', { 125, 105,  70, 255 },   true,   false, true },
    [TILE_WATER]  = { "Water",  '~', {  50,  90, 160, 255 },   false,  true,  true },
    [TILE_ROCK]   = { "Rock",   '#', {  62,  60,  70, 255 },   false,  false, true },
    [TILE_GRAVEL] = { "Gravel", ':', { 150, 154, 160, 255 },   true,   false, true },
    [TILE_LAVA]   = { "Lava",   '^', { 250, 135,  30, 255 },   false,  false, true },
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
    version++;
    rngState = seed;
    mapWidth = MAP_W;
    mapHeight = MAP_H;
    for (int i = 0; i < MAP_W*MAP_H; i++) { tiles[i] = TILE_GRASS; blocked[i] = false; }

    for (int i = 0; i < 40; i++) PaintBlob(RandRange(0, MAP_W - 1), RandRange(0, MAP_H - 1), RandRange(2, 6), TILE_DIRT);
    for (int i = 0; i < 14; i++) PaintBlob(RandRange(0, MAP_W - 1), RandRange(0, MAP_H - 1), RandRange(2, 5), TILE_WATER);
    for (int i = 0; i < 14; i++) PaintBlob(RandRange(0, MAP_W - 1), RandRange(0, MAP_H - 1), RandRange(1, 3), TILE_ROCK);

    // Keep the middle clear for the starting units.
    PaintBlob(MAP_W/2, MAP_H/2, 12, TILE_GRASS);
}

void MapSetTiles(int width, int height, const unsigned char *types)
{
    version++;
    mapWidth = width;
    mapHeight = height;
    for (int i = 0; i < MAP_W*MAP_H; i++) { tiles[i] = TILE_ROCK; blocked[i] = false; }
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++) tiles[y*MAP_W + x] = types[y*width + x];
    }
}

static unsigned char backupTiles[MAP_W * MAP_H];
static bool          backupBlocked[MAP_W * MAP_H];
static int           backupWidth, backupHeight;

void MapBackup(void)
{
    memcpy(backupTiles, tiles, sizeof(tiles));
    memcpy(backupBlocked, blocked, sizeof(blocked));
    backupWidth = mapWidth;
    backupHeight = mapHeight;
}

void MapRestore(void)
{
    version++;
    memcpy(tiles, backupTiles, sizeof(tiles));
    memcpy(blocked, backupBlocked, sizeof(blocked));
    mapWidth = backupWidth;
    mapHeight = backupHeight;
}

unsigned MapVersion(void) { return version; }
int MapWidth(void)  { return mapWidth; }
int MapHeight(void) { return mapHeight; }

TileType MapGetTile(int tx, int ty)
{
    if (tx < 0 || ty < 0 || tx >= mapWidth || ty >= mapHeight) return TILE_ROCK;
    return (TileType)tiles[ty*MAP_W + tx];
}

bool TileAllows(TileType type, MoveClass moveClass)
{
    if (moveClass == MOVE_NAVAL) return TILE_INFO[type].naval;
    if (moveClass == MOVE_AIR) return TILE_INFO[type].air;
    return TILE_INFO[type].ground;
}

bool MapTileWalkable(MoveClass moveClass, int tx, int ty)
{
    if (tx < 0 || ty < 0 || tx >= mapWidth || ty >= mapHeight) return false;   // nobody leaves the map
    if (!TileAllows((TileType)tiles[ty*MAP_W + tx], moveClass)) return false;
    return moveClass == MOVE_AIR || !blocked[ty*MAP_W + tx];   // buildings block ground and naval units
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
    version++;
    PaintBlob((int)(worldPos.x/TILE_SIZE), (int)(worldPos.y/TILE_SIZE), radiusTiles, TILE_GRASS);
}

bool MapIsWalkable(MoveClass moveClass, Vector2 worldPos)
{
    return MapTileWalkable(moveClass, (int)floorf(worldPos.x / TILE_SIZE), (int)floorf(worldPos.y / TILE_SIZE));
}

// Checks the four edge points of the circle: cheap, and close enough for
// units that are much smaller than a tile.
bool MapCircleWalkable(MoveClass m, Vector2 c, float r)
{
    return MapIsWalkable(m, (Vector2){ c.x + r, c.y }) && MapIsWalkable(m, (Vector2){ c.x - r, c.y }) &&
           MapIsWalkable(m, (Vector2){ c.x, c.y + r }) && MapIsWalkable(m, (Vector2){ c.x, c.y - r });
}

// Samples the line every LINE_SAMPLE_STEP pixels and checks the unit fits at each sample.
bool MapLineClear(MoveClass moveClass, Vector2 from, Vector2 to, float radius)
{
    float dx = to.x - from.x, dy = to.y - from.y;
    int steps = (int)(sqrtf(dx*dx + dy*dy) / LINE_SAMPLE_STEP) + 1;
    for (int i = 0; i <= steps; i++)
    {
        float t = (float)i / steps;
        if (!MapCircleWalkable(moveClass, (Vector2){ from.x + dx*t, from.y + dy*t }, radius)) return false;
    }
    return true;
}

// Tiles with art (sprites.c) are drawn in their own pass after the plain
// ones, so all the art goes out in one batch (see UnitsDraw()).
void MapDraw(Rectangle view)
{
    // Grass is the most common tile, so without grass art paint the whole map
    // grass in one rectangle and only draw the other tiles on top: far fewer draw calls.
    bool grassArt = SpritesHaveTile(TILE_GRASS);
    if (!grassArt) DrawRectangle(0, 0, mapWidth*TILE_SIZE, mapHeight*TILE_SIZE, TILE_INFO[TILE_GRASS].color);

    // Only loop over the tiles the camera can see.
    int x0 = (int)floorf(view.x / TILE_SIZE),                y0 = (int)floorf(view.y / TILE_SIZE);
    int x1 = (int)floorf((view.x + view.width) / TILE_SIZE), y1 = (int)floorf((view.y + view.height) / TILE_SIZE);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > mapWidth - 1) x1 = mapWidth - 1;
    if (y1 > mapHeight - 1) y1 = mapHeight - 1;

    for (int y = y0; y <= y1; y++)
    {
        for (int x = x0; x <= x1; x++)
        {
            TileType t = (TileType)tiles[y*MAP_W + x];
            if (t != TILE_GRASS && !SpritesHaveTile(t)) DrawRectangle(x*TILE_SIZE, y*TILE_SIZE, TILE_SIZE, TILE_SIZE, TILE_INFO[t].color);
        }
    }

    for (int y = y0; y <= y1; y++)
    {
        for (int x = x0; x <= x1; x++)
        {
            TileType t = (TileType)tiles[y*MAP_W + x];
            if (SpritesHaveTile(t)) SpritesDrawTile(t, x, y);
        }
    }
}
