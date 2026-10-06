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

#include "grid.h"
#include "units.h"

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
        if (!units[i].active) continue;
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
                if (!CheckCollisionPointRec(units[i].pos, area)) continue;
                if (count == maxOut) return count;
                out[count++] = i;
            }
        }
    }
    return count;
}
