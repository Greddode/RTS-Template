// fog.c - Fog of war.
//
// Each team has one byte per tile: UNSEEN (never seen: black), EXPLORED (seen
// before: dimmed; terrain, gold and buildings you saw are remembered, units
// aren't), VISIBLE (inside the sight radius of one of its units or buildings).
//
// Every FOG_UPDATE_TICKS the visibility is recomputed from scratch:
//   1. every VISIBLE tile becomes EXPLORED;
//   2. every unit and building marks the tiles within its `sight` (from
//      UNIT_STATS / BUILDING_STATS) VISIBLE.
// Step 2 fills each row of the circle with one memset, using a table of the
// circle's half-width per row, so a whole recompute is cheap (see NOTES.md
// for the measured cost) - no need to track which units moved.
//
// FogCanSee() is the one rule everything else asks: drawing, clicking and
// auto-targeting enemies. With fog off - or for the AI when
// AI_SEES_THROUGH_FOG is 1 - it always says yes, and the AI's grid isn't
// even computed.

#include "fog.h"
#include "buildings.h"
#include "config.h"
#include "map.h"
#include "units.h"
#include <math.h>
#include <string.h>

static unsigned char vis[2][MAP_W*MAP_H];
static unsigned char halfWidth[FOG_MAX_SIGHT + 1][FOG_MAX_SIGHT + 1];   // [radius][row offset]
static bool          enabled = FOG_OF_WAR_ENABLED;
static int           countdown = 0;
static double        lastMs = 0.0;
static unsigned int  version = 1;   // bumped on every recompute or on/off switch

static bool TeamUsesFog(int team)
{
    return enabled && !(team == AI_TEAM && AI_SEES_THROUGH_FOG);
}

static void BuildCircleTable(void)
{
    for (int r = 0; r <= FOG_MAX_SIGHT; r++)
        for (int dy = 0; dy <= r; dy++)
            halfWidth[r][dy] = (unsigned char)sqrtf((float)(r*r - dy*dy) + 0.5f);
}

// Mark the circle of radius r around tile (cx, cy) VISIBLE, one row at a time.
static void Reveal(unsigned char *grid, int cx, int cy, int r)
{
    if (r > FOG_MAX_SIGHT) r = FOG_MAX_SIGHT;
    int w = MapWidth(), h = MapHeight();
    for (int dy = -r; dy <= r; dy++)
    {
        int y = cy + dy;
        if (y < 0 || y >= h) continue;
        int half = halfWidth[r][dy < 0 ? -dy : dy];
        int x0 = cx - half, x1 = cx + half;
        if (x0 < 0) x0 = 0;
        if (x1 > w - 1) x1 = w - 1;
        if (x0 <= x1) memset(&grid[y*MAP_W + x0], FOG_VISIBLE, x1 - x0 + 1);
    }
}

static void Recompute(int team)
{
    unsigned char *grid = vis[team];
    int w = MapWidth(), h = MapHeight();

    // 1. What was visible is now only explored.
    for (int y = 0; y < h; y++)
    {
        unsigned char *row = &grid[y*MAP_W];
        for (int x = 0; x < w; x++) if (row[x] == FOG_VISIBLE) row[x] = FOG_EXPLORED;
    }

    // 2. Reveal around every unit and building of the team. (A per-update
    //    loop over the pools, like the tick loop - not a "who's nearby" search.)
    for (int i = 0; i < UnitsPoolEnd(); i++)
    {
        const Unit *u = &units[i];
        if (!UnitIsActiveInWorld(u) || u->team != team) continue;   // loaded units see nothing
        Reveal(grid, (int)(u->pos.x/TILE_SIZE), (int)(u->pos.y/TILE_SIZE), UNIT_STATS[u->type].sight);
    }
    for (int b = 0; b < MAX_BUILDINGS; b++)
    {
        const Building *bd = &buildings[b];
        if (!bd->active || bd->team != team) continue;
        Reveal(grid, bd->tx + bd->size/2, bd->ty + bd->size/2, BUILDING_STATS[bd->type].sight + bd->size/2);
    }

    // Remember enemy buildings this team has now seen (drawn under fog later).
    if (team == PLAYER_TEAM)
    {
        for (int b = 0; b < MAX_BUILDINGS; b++)
        {
            Building *bd = &buildings[b];
            if (bd->active && bd->team != team && FogCanSeeRect(team, BuildingRect(b))) bd->seenByPlayer = true;
        }
    }
}

void FogUpdateNow(void)
{
    double start = GetTime();
    for (int team = 0; team < 2; team++) if (TeamUsesFog(team)) Recompute(team);
    lastMs = (GetTime() - start)*1000.0;
    version++;
}

void FogReset(void)
{
    if (halfWidth[1][0] == 0) BuildCircleTable();
    memset(vis, FOG_UNSEEN, sizeof(vis));
    countdown = 0;
    FogUpdateNow();
}

void FogTick(void)
{
    if (--countdown > 0) return;
    countdown = FOG_UPDATE_TICKS;
    FogUpdateNow();
}

bool FogEnabled(void)
{
    return enabled;
}

void FogSetEnabled(bool on)
{
    enabled = on;
    version++;
    if (on) FogUpdateNow();   // the grids may be stale after being off
}

FogState FogTileState(int team, int tx, int ty)
{
    if (!TeamUsesFog(team)) return FOG_VISIBLE;
    if (tx < 0 || ty < 0 || tx >= MapWidth() || ty >= MapHeight()) return FOG_UNSEEN;
    return (FogState)vis[team][ty*MAP_W + tx];
}

bool FogCanSee(int team, Vector2 pos)
{
    return FogTileState(team, (int)floorf(pos.x/TILE_SIZE), (int)floorf(pos.y/TILE_SIZE)) == FOG_VISIBLE;
}

bool FogCanSeeRect(int team, Rectangle r)
{
    if (!TeamUsesFog(team)) return true;
    int x0 = (int)(r.x/TILE_SIZE), y0 = (int)(r.y/TILE_SIZE);
    int x1 = (int)((r.x + r.width - 1)/TILE_SIZE), y1 = (int)((r.y + r.height - 1)/TILE_SIZE);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            if (FogTileState(team, x, y) == FOG_VISIBLE) return true;
    return false;
}

bool FogExplored(int team, Vector2 pos)
{
    return FogTileState(team, (int)floorf(pos.x/TILE_SIZE), (int)floorf(pos.y/TILE_SIZE)) != FOG_UNSEEN;
}

unsigned FogVersion(void)
{
    return version;
}

double FogLastUpdateMs(void)
{
    return lastMs;
}

// One pass over the tiles on screen. Neighbouring tiles in a row with the
// same state become one rectangle, so most rows are a handful of quads.
void FogDraw(Rectangle view)
{
    if (!TeamUsesFog(PLAYER_TEAM)) return;
    int x0 = (int)floorf(view.x/TILE_SIZE), y0 = (int)floorf(view.y/TILE_SIZE);
    int x1 = (int)floorf((view.x + view.width)/TILE_SIZE), y1 = (int)floorf((view.y + view.height)/TILE_SIZE);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > MapWidth() - 1) x1 = MapWidth() - 1;
    if (y1 > MapHeight() - 1) y1 = MapHeight() - 1;

    const unsigned char *grid = vis[PLAYER_TEAM];
    for (int y = y0; y <= y1; y++)
    {
        int x = x0;
        while (x <= x1)
        {
            unsigned char state = grid[y*MAP_W + x];
            int start = x;
            while (x <= x1 && grid[y*MAP_W + x] == state) x++;   // run of equal tiles
            if (state == FOG_VISIBLE) continue;
            Color c = (state == FOG_UNSEEN) ? BLACK : (Color){ 0, 0, 0, FOG_EXPLORED_ALPHA };
            DrawRectangle(start*TILE_SIZE, y*TILE_SIZE, (x - start)*TILE_SIZE, TILE_SIZE, c);
        }
    }
}
