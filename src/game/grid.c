// grid.c - Spatial grid.
//
// The world is split into square cells. Each cell keeps a linked list of the
// units whose centre is inside it, stored in two fixed arrays (no malloc):
//   cellHead[cell]   first unit in that cell (-1 = empty)
//   nextInCell[unit] next unit in the same cell (-1 = end of list)
// A query only walks the cells overlapping the search area, so its cost
// depends on how crowded that area is, not on the total number of units.
//
// The grid is rebuilt from scratch once per sim tick. For a few thousand units
// that's cheap, and much simpler than tracking units moving between cells.
// Units that die during a tick stay listed until the next rebuild, so every
// query skips inactive units. Units inside a transport aren't listed at all
// (UnitIsActiveInWorld), so every grid search ignores them.

#include "grid.h"
#include "units.h"
#include "fog.h"

static int cellHead[GRID_W * GRID_H];
static int nextInCell[MAX_UNITS];

static int CellCoord(float worldCoord, int cellCount)
{
    int c = (int)(worldCoord / GRID_CELL_SIZE);
    if (c < 0) return 0;
    if (c > cellCount - 1) return cellCount - 1;
    return c;
}

void GridRebuild(void)
{
    for (int i = 0; i < GRID_W*GRID_H; i++) cellHead[i] = -1;

    for (int i = 0; i < MAX_UNITS; i++)
    {
        if (!UnitIsActiveInWorld(&units[i])) continue;   // loaded units aren't in the world
        int cell = CellCoord(units[i].pos.y, GRID_H)*GRID_W + CellCoord(units[i].pos.x, GRID_W);
        nextInCell[i] = cellHead[cell];
        cellHead[cell] = i;
    }
}

int GridQuery(Rectangle area, int *out, int maxOut)
{
    int x0 = CellCoord(area.x, GRID_W), x1 = CellCoord(area.x + area.width, GRID_W);
    int y0 = CellCoord(area.y, GRID_H), y1 = CellCoord(area.y + area.height, GRID_H);
    int count = 0;

    for (int cy = y0; cy <= y1; cy++)
    {
        for (int cx = x0; cx <= x1; cx++)
        {
            for (int i = cellHead[cy*GRID_W + cx]; i != -1; i = nextInCell[i])
            {
                if (!UnitIsActiveInWorld(&units[i]) || !CheckCollisionPointRec(units[i].pos, area)) continue;
                if (count == maxOut) return count;
                out[count++] = i;
            }
        }
    }
    return count;
}

// Check one cell for a closer enemy (helper for GridFindNearestEnemy).
static void CheckCellForEnemy(int cx, int cy, Vector2 pos, int myTeam, bool ground, bool air, int *best, float *bestDistSq)
{
    if (cx < 0 || cy < 0 || cx >= GRID_W || cy >= GRID_H) return;
    for (int i = cellHead[cy*GRID_W + cx]; i != -1; i = nextInCell[i])
    {
        if (!UnitIsActiveInWorld(&units[i]) || units[i].team == myTeam) continue;
        if (!(UnitIsFlying(&units[i]) ? air : ground)) continue;   // e.g. a flyer, for a melee attacker
        if (!FogCanSee(myTeam, units[i].pos)) continue;   // can't target what it can't see
        if (units[i].hp <= units[i].incomingDamage) continue;   // already doomed by projectiles in flight
        float dx = units[i].pos.x - pos.x, dy = units[i].pos.y - pos.y;
        float d = dx*dx + dy*dy;
        if (d < *bestDistSq) { *bestDistSq = d; *best = i; }
    }
}

// Search square rings of cells outward from `pos`. Units in ring r are at
// least (r - 1) cells away, so once that's further than the best match so
// far, no later ring can beat it and the search stops. Nearby enemies are
// found after a handful of cells. Enemies that projectiles already in the air
// will kill are skipped (targeting them would only waste attacks), and so are
// enemies hidden by the fog of war. `ground` / `air`: which enemies count
// (an attacker passes its hitsGround / hitsAir from UNIT_STATS).
int GridFindNearestEnemy(Vector2 pos, float maxDist, int myTeam, bool ground, bool air)
{
    int cx = CellCoord(pos.x, GRID_W), cy = CellCoord(pos.y, GRID_H);
    int best = -1;
    float bestDistSq = maxDist*maxDist;
    int maxRing = (int)(maxDist/GRID_CELL_SIZE) + 1;
    int gridSize = (GRID_W > GRID_H) ? GRID_W : GRID_H;
    if (maxRing > gridSize) maxRing = gridSize;

    for (int r = 0; r <= maxRing; r++)
    {
        float ringMinDist = (float)(r - 1)*GRID_CELL_SIZE;
        if (r > 1 && ringMinDist*ringMinDist > bestDistSq) break;

        // The ring's top and bottom rows, then its left and right columns.
        for (int x = cx - r; x <= cx + r; x++)
        {
            CheckCellForEnemy(x, cy - r, pos, myTeam, ground, air, &best, &bestDistSq);
            if (r > 0) CheckCellForEnemy(x, cy + r, pos, myTeam, ground, air, &best, &bestDistSq);
        }
        for (int y = cy - r + 1; y <= cy + r - 1; y++)
        {
            CheckCellForEnemy(cx - r, y, pos, myTeam, ground, air, &best, &bestDistSq);
            CheckCellForEnemy(cx + r, y, pos, myTeam, ground, air, &best, &bestDistSq);
        }
    }
    return best;
}
