// tile_class_test.c - Checks TILE_INFO and the movement classes (MoveClass):
// who may enter every tile type, building blocking, the map edge, straight
// lines, regions and air "paths". No window needed.
//
//   make test        (or: ./tests/build/tile_class_test after building tests/)
//
// EXPECTED below is the design: if you change a tile's columns in TILE_INFO
// on purpose, change its row here too.

#include "map.h"
#include "mapfile.h"
#include "path.h"
#include "units.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
static void Check(bool ok, const char *what)
{
    if (!ok) { failures++; printf("  FAIL %s\n", what); }
}

static const char *CLASS_NAMES[MOVE_CLASS_COUNT] = { "GROUND", "NAVAL", "AIR" };

// Who may enter each tile:                    GROUND NAVAL  AIR
static const bool EXPECTED[TILE_COUNT][MOVE_CLASS_COUNT] = {
    [TILE_GRASS]  = { true,  false, true },
    [TILE_DIRT]   = { true,  false, true },
    [TILE_WATER]  = { false, true,  true },
    [TILE_ROCK]   = { false, false, true },
    [TILE_GRAVEL] = { true,  false, true },
    [TILE_LAVA]   = { false, false, true },
};

// A small map: row 0 holds one tile of each type at x = type; the rest is
// land on the left (x < 8) and water on the right, with a rock wall at x = 8.
#define TW 16
#define TH 8
static unsigned char testTiles[TW*TH];

static void SetUpMap(void)
{
    for (int y = 0; y < TH; y++)
        for (int x = 0; x < TW; x++)
            testTiles[y*TW + x] = (unsigned char)((x < 8) ? TILE_GRASS : (x == 8) ? TILE_ROCK : TILE_WATER);
    for (int t = 0; t < TILE_COUNT; t++) testTiles[t] = (unsigned char)t;
    MapSetTiles(TW, TH, testTiles);
}

static Vector2 Centre(int x, int y) { return (Vector2){ (x + 0.5f)*TILE_SIZE, (y + 0.5f)*TILE_SIZE }; }

int main(void)
{
    // 1. Every tile x class pair: the table, and the map query on a real tile.
    SetUpMap();
    int pairs = 0;
    for (int t = 0; t < TILE_COUNT; t++)
    {
        for (int c = 0; c < MOVE_CLASS_COUNT; c++, pairs++)
        {
            char what[128];
            snprintf(what, sizeof(what), "%s x %s: TileAllows should be %s", TILE_INFO[t].name, CLASS_NAMES[c], EXPECTED[t][c] ? "true" : "false");
            Check(TileAllows((TileType)t, (MoveClass)c) == EXPECTED[t][c], what);
            snprintf(what, sizeof(what), "%s x %s: MapTileWalkable on the map should be %s", TILE_INFO[t].name, CLASS_NAMES[c], EXPECTED[t][c] ? "true" : "false");
            Check(MapTileWalkable((MoveClass)c, t, 0) == EXPECTED[t][c], what);
        }
    }

    // 2. Buildings block GROUND and NAVAL, not AIR.
    MapSetBlocked(2, 3, 1, 1, true);    // a grass tile
    MapSetBlocked(10, 3, 1, 1, true);   // a water tile (e.g. a dock)
    Check(!MapTileWalkable(MOVE_GROUND, 2, 3), "building on grass blocks GROUND");
    Check(MapTileWalkable(MOVE_AIR, 2, 3), "building on grass doesn't block AIR");
    Check(!MapTileWalkable(MOVE_NAVAL, 10, 3), "building on water blocks NAVAL");
    Check(MapTileWalkable(MOVE_AIR, 10, 3), "building on water doesn't block AIR");
    MapSetBlocked(2, 3, 1, 1, false);
    MapSetBlocked(10, 3, 1, 1, false);
    Check(MapTileWalkable(MOVE_GROUND, 2, 3) && MapTileWalkable(MOVE_NAVAL, 10, 3), "unblocking gives the tiles back");

    // 3. Nobody leaves the map, not even AIR.
    for (int c = 0; c < MOVE_CLASS_COUNT; c++)
    {
        char what[96];
        snprintf(what, sizeof(what), "%s can't be outside the map", CLASS_NAMES[c]);
        Check(!MapTileWalkable((MoveClass)c, -1, 3) && !MapTileWalkable((MoveClass)c, TW, 3) &&
              !MapTileWalkable((MoveClass)c, 3, -1) && !MapTileWalkable((MoveClass)c, 3, TH), what);
        Check(!MapCircleWalkable((MoveClass)c, (Vector2){ 2.0f, 2.0f*TILE_SIZE }, UNIT_RADIUS), "a unit can't hang over the left edge");
    }

    // 4. Straight lines: land -> water across the rock wall.
    Vector2 land = Centre(4, 5), sea = Centre(12, 5);
    Check(!MapLineClear(MOVE_GROUND, land, sea, UNIT_RADIUS), "GROUND can't cross rock and water in a line");
    Check(!MapLineClear(MOVE_NAVAL, land, sea, UNIT_RADIUS), "NAVAL can't start on land");
    Check(MapLineClear(MOVE_NAVAL, Centre(10, 5), sea, UNIT_RADIUS), "NAVAL crosses open water");
    Check(MapLineClear(MOVE_AIR, land, sea, UNIT_RADIUS), "AIR flies over rock and water");
    Check(MapLineClear(MOVE_AIR, Centre(3, 0), Centre(5, 0), UNIT_RADIUS), "AIR flies over rock, gravel and lava");

    // 5. Regions per class.
    PathComputeRegions();
    Check(PathRegion(MOVE_GROUND, land) != 0 && PathRegion(MOVE_GROUND, sea) == 0, "GROUND: land has a region, water none");
    Check(PathRegion(MOVE_NAVAL, sea) != 0 && PathRegion(MOVE_NAVAL, land) == 0, "NAVAL: water has a region, land none");
    Check(PathRegion(MOVE_AIR, land) != 0 && PathRegion(MOVE_AIR, land) == PathRegion(MOVE_AIR, sea), "AIR: one region over land, rock and water");
    Check(PathRegion(MOVE_GROUND, Centre(4, 0)) == PathRegion(MOVE_GROUND, land), "gravel joins the land region");
    Check(PathRegion(MOVE_GROUND, Centre(5, 0)) == 0, "lava is in no GROUND region");
    Check(PathRegion(MOVE_GROUND, Centre(TW + 3, 3)) == 0 && PathRegion(MOVE_AIR, Centre(TW + 3, 3)) == 0, "outside the map: no region");

    // 6. AIR requests are answered at once with a straight line; GROUND ones wait for PathUpdate.
    PathReset();
    PathRequest(0, MOVE_AIR, land, sea);
    Vector2 wp;
    Check(PathGetStatus(0) == PATH_READY && PathCurrentWaypoint(0, &wp) && wp.x == sea.x && wp.y == sea.y && PathQueueLength() == 0,
          "AIR path: ready at once, one waypoint = the goal, nothing queued");
    PathRequest(1, MOVE_GROUND, land, Centre(6, 6));
    Check(PathGetStatus(1) == PATH_PENDING && PathQueueLength() == 1, "GROUND path: queued for PathUpdate");

    // 7. Map-file characters: unique, printable, not a space; unknown ones are refused.
    for (int a = 0; a < TILE_COUNT; a++)
    {
        char c = TILE_INFO[a].fileChar;
        Check(c > ' ' && c < 127, "tile characters are printable and not a space");
        for (int b = a + 1; b < TILE_COUNT; b++) Check(c != TILE_INFO[b].fileChar, "no two tiles share a character");
    }
    const char *bad = "tests_bad_tile.map";
    FILE *f = fopen(bad, "w");
    if (f) { fputs("width 8\nheight 8\ntiles\n", f); for (int y = 0; y < 8; y++) fputs(y == 3 ? "....@...\n" : "........\n", f); fclose(f); }
    static MapDoc doc;
    Check(!MapFileParse(bad, &doc) && strstr(MapFileError(), "unknown tile character '@'") != NULL, "an unknown tile character is refused with a clear error");
    remove(bad);

    printf("%d tile x class pairs checked; %s: %d problem(s)\n", pairs, failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
