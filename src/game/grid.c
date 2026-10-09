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
// query skips inactive units.
//
// Each cell also keeps one list per team (teamHead / nextInTeam), threaded
// through the same units in the same order. The enemy search walks only the
// other team's list, so a unit in a crowd of thousands of friends doesn't read
// every one of them to find no enemy (measured: this was 58% of the sim tick
// with 8,000 units). One catch keeps it exact: if a unit is spawned between
// rebuilds into a listed slot whose old unit was on the other team, that slot
// sits in the wrong team's list until the next rebuild. UnitSpawn tells the
// grid (GridNoteSpawn), and until then the search walks the full lists, as
// before. Results are identical either way. Units inside a transport aren't listed at all
// (UnitIsActiveInWorld), so every grid search ignores them.

#include "grid.h"
#include "units.h"
#include "fog.h"
#include "path.h"

#define GRID_TEAMS 2   // PLAYER_TEAM and AI_TEAM

static int cellHead[GRID_W * GRID_H];
static int nextInCell[MAX_UNITS];
static int teamHead[GRID_TEAMS][GRID_W * GRID_H];
static int nextInTeam[MAX_UNITS];
static signed char listedTeam[MAX_UNITS];   // team it was listed under at the last rebuild, -1 = not listed
static bool teamsMixed = false;             // a slot changed team since the rebuild: use the full lists
static int  listedEnd = MAX_UNITS;          // listedTeam is valid up to here (UnitsPoolEnd at the last rebuild)

static int CellCoord(float worldCoord, int cellCount)
{
    int c = (int)(worldCoord / GRID_CELL_SIZE);
    if (c < 0) return 0;
    if (c > cellCount - 1) return cellCount - 1;
    return c;
}

void GridRebuild(void)
{
    for (int i = 0; i < GRID_W*GRID_H; i++) cellHead[i] = teamHead[0][i] = teamHead[1][i] = -1;
    teamsMixed = false;

    int end = UnitsPoolEnd();   // every slot from here up is free
    for (int i = end; i < listedEnd; i++) listedTeam[i] = -1;   // the pool shrank since last time
    listedEnd = end;
    for (int i = 0; i < end; i++)
    {
        listedTeam[i] = -1;
        if (!UnitIsActiveInWorld(&units[i])) continue;   // loaded units aren't in the world
        int cell = CellCoord(units[i].pos.y, GRID_H)*GRID_W + CellCoord(units[i].pos.x, GRID_W);
        nextInCell[i] = cellHead[cell];
        cellHead[cell] = i;
        int team = units[i].team;
        nextInTeam[i] = teamHead[team][cell];
        teamHead[team][cell] = i;
        listedTeam[i] = (signed char)team;
    }
}

void GridNoteSpawn(int id, int team)
{
    if (listedTeam[id] != -1 && listedTeam[id] != team) teamsMixed = true;
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
typedef struct Searcher { Vector2 pos; int team; bool ground, air; MoveClass moveClass; float range; } Searcher;
static void CheckCellForEnemy(int cx, int cy, const Searcher *s, int *best, float *bestDistSq)
{
    if (cx < 0 || cy < 0 || cx >= GRID_W || cy >= GRID_H) return;
    int cell = cy*GRID_W + cx;
    bool full = teamsMixed;   // see the top of the file
    for (int i = full ? cellHead[cell] : teamHead[1 - s->team][cell]; i != -1; i = full ? nextInCell[i] : nextInTeam[i])
    {
        if (!UnitIsActiveInWorld(&units[i]) || units[i].team == s->team) continue;
        if (!(UnitIsFlying(&units[i]) ? s->air : s->ground)) continue;   // e.g. a flyer, for a melee attacker
        if (!FogCanSee(s->team, units[i].pos)) continue;   // can't target what it can't see
        if (units[i].hp <= units[i].incomingDamage) continue;   // already doomed by projectiles in flight
        float dx = units[i].pos.x - s->pos.x, dy = units[i].pos.y - s->pos.y;
        float d = dx*dx + dy*dy;
        if (d >= *bestDistSq) continue;
        if (d > s->range*s->range && !PathCanReach(s->moveClass, s->pos, (Rectangle){ units[i].pos.x, units[i].pos.y, 0, 0 }, s->range)) continue;   // a boat at sea, for a Melee
        *bestDistSq = d; *best = i;
    }
}

// Search square rings of cells outward from `pos`. Units in ring r are at
// least (r - 1) cells away, so once that's further than the best match so
// far, no later ring can beat it and the search stops. Nearby enemies are
// found after a handful of cells. Enemies that projectiles already in the air
// will kill are skipped (targeting them would only waste attacks), and so are
// enemies hidden by the fog of war. `ground` / `air`: which enemies count
// (an attacker passes its hitsGround / hitsAir from UNIT_STATS).
int GridFindNearestEnemy(Vector2 pos, float maxDist, int myTeam, bool ground, bool air, MoveClass moveClass, float range)
{
    Searcher s = { pos, myTeam, ground, air, moveClass, range };
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
            CheckCellForEnemy(x, cy - r, &s, &best, &bestDistSq);
            if (r > 0) CheckCellForEnemy(x, cy + r, &s, &best, &bestDistSq);
        }
        for (int y = cy - r + 1; y <= cy + r - 1; y++)
        {
            CheckCellForEnemy(cx - r, y, &s, &best, &bestDistSq);
            CheckCellForEnemy(cx + r, y, &s, &best, &bestDistSq);
        }
    }
    return best;
}
