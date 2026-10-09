// naval_test.c - Checks water units and the Dock: the placement rule
// (BuildingsPlacementOK), ghost / click / editor agreeing, Dock spawning,
// boats staying on water, reach (nobody chases what it can't get to), what
// hits boats, the Ship's splash, NAVAL regions, map keywords and the editor.
// Runs the real sim ticks on a small map built here. No window needed.
//
//   make test        (or: ./tests/build/naval_test after building tests/)
//
// The editor is #included (like controls_overflow_test does with menu.c) so
// the test can use its own tool code: the same path a mouse click takes.

#include "../src/editor/editor.c"
#include "combat.h"
#include "fog.h"
#include "grid.h"
#include "input.h"
#include "path.h"
#include "raymath.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0, checks = 0;
static void Check(bool ok, const char *what)
{
    checks++;
    if (!ok) { failures++; printf("  FAIL %s\n", what); }
    else if (getenv("NAVAL_VERBOSE")) printf("  ok   %s\n", what);   // NAVAL_VERBOSE=1: list every check
}

// The test map, TW x TH tiles:
//   x < LAND_W        land (grass), with a rock wall in it at x = 8 (y 0..14)
//   x >= LAND_W       the lake (water); a land wall at x = 26..27, y 0..15, so
//                     a boat going from the north-west of it to the north-east sails around
//   POND              a 3x3 pond inside the land: a second, separate NAVAL region
#define TW 40
#define TH 24
#define LAND_W 16
#define POND_X 3
#define POND_Y 18
static unsigned char testTiles[TW*TH];

static Vector2 Tile(float x, float y) { return (Vector2){ (x + 0.5f)*TILE_SIZE, (y + 0.5f)*TILE_SIZE }; }
static bool InPond(int x, int y) { return x >= POND_X && x < POND_X + 3 && y >= POND_Y && y < POND_Y + 3; }

static void NewWorld(void)
{
    for (int y = 0; y < TH; y++)
        for (int x = 0; x < TW; x++)
        {
            TileType t = (x < LAND_W) ? TILE_GRASS : TILE_WATER;
            if (x == 8 && y < 15) t = TILE_ROCK;
            if ((x == 26 || x == 27) && y < 16) t = TILE_GRASS;
            if (InPond(x, y)) t = TILE_WATER;
            testTiles[y*TW + x] = (unsigned char)t;
        }
    UnitsReset();
    BuildingsReset();
    CombatReset();
    PathReset();
    EconomyInit();
    MapSetTiles(TW, TH, testTiles);
    FogSetEnabled(false);
    GridRebuild();
    PathComputeRegions();
}

// One sim tick, in the game's order (main.c), plus the path queue (per frame there).
static void Tick(int n)
{
    for (int k = 0; k < n; k++)
    {
        PathUpdate();
        UnitsTick();
        BuildingsTick();
        CombatProjectilesTick();
        GridRebuild();
    }
}

static int Spawn(UnitType type, int team, Vector2 at)
{
    int id = UnitSpawn(at, type, team);
    GridRebuild();
    return id;
}

// --- 1. Placement: one rule for the click, the ghost, the AI and the editor --------------
static void TestPlacement(void)
{
    NewWorld();
    BuildingPlace(BUILDING_BARRACKS, PLAYER_TEAM, Tile(3, 4), false);   // the Dock's prerequisite
    PathComputeRegions();

    // A 2x2 footprint centred on tile c covers tiles c-1..c. Water starts at x = LAND_W.
    // (Each result is worked out before its Check: C doesn't fix the order a call's arguments run in.)
    const char *why = "";
    bool farOk = BuildingCanPlace(BUILDING_DOCK, Tile(10, 10), &why);
    Check(!farOk && strcmp(why, "Dock must be next to water") == 0, TextFormat("Dock far from water refused with \"%s\"", why));
    Check(!BuildingCanPlace(BUILDING_DOCK, Tile(LAND_W - 2, 10), NULL), "Dock one tile short of the water refused (margin is 1 tile)");
    Check(BuildingCanPlace(BUILDING_DOCK, Tile(LAND_W - 1, 10), NULL), "Dock with its edge touching the water accepted");
    Check(!BuildingCanPlace(BUILDING_DOCK, Tile(LAND_W, 10), &why) && strcmp(why, "Can't build there") == 0, "Dock with a foot in the water refused (buildings never go on water)");
    Check(BuildingCanPlace(BUILDING_BARRACKS, Tile(10, 10), NULL), "a Barracks (needsWater off) still goes anywhere on open ground");
    Check(BuildingCanPlace(BUILDING_DOCK, Tile(POND_X + 4, POND_Y + 1), NULL), "a Dock by the pond accepted (any water counts)");

    // Every tile of the map: the ghost colour, a click and the editor agree with the rule.
    MapDoc d = { .width = TW, .height = TH };
    memcpy(d.tiles, testTiles, sizeof(testTiles));
    int disagree = 0, green = 0;
    for (int y = 0; y < TH; y++)
        for (int x = 0; x < TW; x++)
        {
            if (y >= 3 && y <= 5 && x >= 2 && x <= 4) continue;   // under the Barracks (the editor's doc doesn't hold it)
            Vector2 at = Tile(x, y);
            bool rule = BuildingCanPlace(BUILDING_DOCK, at, NULL);
            bool ghost = InputPlacementOK(BUILDING_DOCK, at, NULL);
            Rectangle f = BuildingFootprint(BUILDING_DOCK, at);
            MapObject o = { .kind = MAPOBJ_BUILDING, .type = BUILDING_DOCK, .team = PLAYER_TEAM, .x = (int)(f.x/TILE_SIZE), .y = (int)(f.y/TILE_SIZE) };
            bool editorOk = MapDocObjectFits(&d, &o, -1);
            int site = BuildingPlace(BUILDING_DOCK, PLAYER_TEAM, at, true);   // what a click would do
            if (site != -1) BuildingDestroy(site);
            if (rule != ghost || rule != editorOk || rule != (site != -1)) disagree++;
            green += ghost;
        }
    Check(disagree == 0 && green > 0, TextFormat("ghost colour, click, editor and the rule agree on all %d tiles (%d green, %d disagree)", TW*TH, green, disagree));

    // The AI's spot search only offers spots by the water.
    Vector2 spot;
    Check(BuildingsFindSpot(BUILDING_DOCK, Tile(10, 10), &spot) && BuildingCanPlace(BUILDING_DOCK, spot, NULL) &&
          (int)(spot.x/TILE_SIZE) >= LAND_W - 1, "the AI's spot search finds a Dock spot next to water");
}

// --- 2. Spawning at the Dock --------------------------------------------------------------
static void TestDockSpawn(void)
{
    NewWorld();
    // A Dock by the pond: its only water is the pond's column x = POND_X + 2 (3 tiles).
    int dock = BuildingPlace(BUILDING_DOCK, PLAYER_TEAM, Tile(POND_X + 4, POND_Y + 1), false);
    Check(dock != -1, "Dock by the pond placed");
    int blockers[3];
    for (int k = 0; k < 3; k++) blockers[k] = Spawn(UNIT_BOAT, PLAYER_TEAM, Tile(POND_X + 2, POND_Y + k));
    EconomyAdd(PLAYER_TEAM, 1000);
    int gold = EconomyGold(PLAYER_TEAM);
    Check(!BuildingQueueTrain(dock, UNIT_BOAT), "every water tile next to the Dock taken: training refused");
    Check(EconomyGold(PLAYER_TEAM) == gold && buildings[dock].queueCount == 0, TextFormat("...and nothing charged (gold %d -> %d), nothing queued", gold, EconomyGold(PLAYER_TEAM)));

    UnitDespawn(blockers[1]);   // free the middle tile
    GridRebuild();
    Check(BuildingQueueTrain(dock, UNIT_BOAT) && EconomyGold(PLAYER_TEAM) == gold - UNIT_STATS[UNIT_BOAT].cost, "one tile free: training accepted and paid");
    Tick((int)(UNIT_STATS[UNIT_BOAT].trainTime*TICK_RATE) + 2);
    int fresh = -1;
    for (int i = 0; i < MAX_UNITS; i++) if (units[i].active && units[i].type == UNIT_BOAT && i != blockers[0] && i != blockers[2]) fresh = i;
    Check(fresh != -1 && (int)(units[fresh].pos.x/TILE_SIZE) == POND_X + 2 && (int)(units[fresh].pos.y/TILE_SIZE) == POND_Y + 1,
          "the trained Boat appeared on the free water tile next to the Dock");
    Check(MapIsWalkable(MOVE_NAVAL, buildings[dock].rally), "a Dock's default rally point is on its water");

    // Ground units still appear on land; a Dock on the lake trains Ships onto the lake.
    int dock2 = BuildingPlace(BUILDING_DOCK, PLAYER_TEAM, Tile(LAND_W - 1, 6), false);
    Check(BuildingQueueTrain(dock2, UNIT_SHIP), "Ship queued at a Dock on the lake");
    Tick((int)(UNIT_STATS[UNIT_SHIP].trainTime*TICK_RATE) + 2);
    int ship = -1;
    for (int i = 0; i < MAX_UNITS; i++) if (units[i].active && units[i].type == UNIT_SHIP) ship = i;
    Check(ship != -1 && MapIsWalkable(MOVE_NAVAL, units[ship].pos) && PathRegion(MOVE_NAVAL, units[ship].pos) == PathRegion(MOVE_NAVAL, Tile(30, 20)),
          "the Ship appeared on the lake");
}

// --- 3. Boats stay on water; orders onto the wrong terrain go to the nearest right one ------
static void TestNavalMovement(void)
{
    NewWorld();
    int boat = Spawn(UNIT_BOAT, PLAYER_TEAM, Tile(20, 3));
    int ship = Spawn(UNIT_SHIP, PLAYER_TEAM, Tile(22, 2));
    int ids[2] = { boat, ship };
    UnitsOrderMove(ids, 2, Tile(33, 3));   // the far side of the land wall: around its south end
    int offWater = 0, ticks = 0;
    while (ticks < 30*90 && (units[boat].moving || units[ship].moving))
    {
        Tick(1); ticks++;
        for (int k = 0; k < 2; k++) offWater += !MapCircleWalkable(MOVE_NAVAL, units[ids[k]].pos, units[ids[k]].radius);
    }
    Check(offWater == 0, TextFormat("a Boat and a Ship sailed round the land wall (%.0f s) without touching land (%d ticks off water)", ticks/30.0f, offWater));
    Check(Vector2Distance(units[boat].pos, Tile(33, 3)) < TILE_SIZE*2 && Vector2Distance(units[ship].pos, Tile(33, 3)) < TILE_SIZE*2, "...and both got there");

    // A boat ordered onto land goes to the nearest water instead.
    UnitsOrderMove(&boat, 1, Tile(LAND_W - 4, 12));   // past the wall's south end, then west onto land
    Tick(30*40);
    int bx = (int)(units[boat].pos.x/TILE_SIZE);
    Check(MapIsWalkable(MOVE_NAVAL, units[boat].pos) && bx >= LAND_W && bx <= LAND_W + 1, TextFormat("Boat ordered onto land stopped on the shore's water (tile x %d)", bx));
    // ...and one ordered into the pond (another lake) stays in its own water.
    int lake = PathRegion(MOVE_NAVAL, units[boat].pos);
    UnitsOrderMove(&boat, 1, Tile(POND_X + 1, POND_Y + 1));
    Tick(30*30);
    Check(PathRegion(MOVE_NAVAL, units[boat].pos) == lake && !units[boat].moving, "Boat ordered into the pond stayed in its own lake (nearest water it can reach)");

    // A ground unit ordered into the lake goes to the nearest land.
    int melee = Spawn(UNIT_MELEE, PLAYER_TEAM, Tile(10, 18));
    UnitsOrderMove(&melee, 1, Tile(24, 18));
    Tick(30*20);
    int mx = (int)(units[melee].pos.x/TILE_SIZE);
    Check(MapIsWalkable(MOVE_GROUND, units[melee].pos) && mx >= LAND_W - 2, TextFormat("Melee ordered into the lake stopped on the shore (tile x %d)", mx));
}

// --- 4. Who hits a boat, and nobody chases what it can't reach --------------------------
static int ShotsAtBoat(UnitType shooter, bool tower)
{
    NewWorld();
    int boat = Spawn(UNIT_BOAT, AI_TEAM, Tile(LAND_W + 2, 10));
    UnitsOrderHold(&boat, 1);
    units[boat].hp = 10000.0f;   // so it outlives the test; we only look at the damage
    if (tower) BuildingPlace(BUILDING_GUARD_TOWER, PLAYER_TEAM, Tile(LAND_W - 3, 10), false);
    else Spawn(shooter, PLAYER_TEAM, Tile(LAND_W - 1, 10));   // on the shore, ~3 tiles away
    PathComputeRegions();
    Tick(30*8);
    return (int)(10000.0f - units[boat].hp);
}

static void TestTargeting(void)
{
    // The rule: boats are "ground" targets (hitsGround), not flyers.
    NewWorld();
    int boat = Spawn(UNIT_BOAT, AI_TEAM, Tile(LAND_W + 2, 10));
    Check(UnitCanHitUnit(UNIT_ARCHER, &units[boat]) && UnitCanHitUnit(UNIT_MELEE, &units[boat]) && UnitCanHitUnit(UNIT_SHIP, &units[boat]),
          "UnitCanHitUnit: a boat is a ground target (Archer, Melee, Ship can hit it)");
    int falcon = Spawn(UNIT_FALCON, AI_TEAM, Tile(LAND_W + 2, 12));
    Check(UnitCanHitUnit(UNIT_BOAT, &units[falcon]) && !UnitCanHitUnit(UNIT_SHIP, &units[falcon]), "Boats hit flyers, Ships don't");

    // Melee never chases a boat: not on its own, not when ordered.
    NewWorld();
    boat = Spawn(UNIT_BOAT, AI_TEAM, Tile(LAND_W + 1, 10));   // 4 tiles away: within aggro, out of its own range
    UnitsOrderHold(&boat, 1);
    Vector2 start = Tile(LAND_W - 3, 10);
    int melee = Spawn(UNIT_MELEE, PLAYER_TEAM, start);   // the boat is within its aggro radius
    Tick(30*10);
    Check(Vector2Distance(units[melee].pos, start) < 2.0f && !units[melee].attacking, "an idle Melee ignores a boat it can't reach");
    UnitsOrderAttack(&melee, 1, boat);
    Tick(30*3);   // the shore is 2 tiles away: there in about a second
    bool stopped = !units[melee].attacking && !units[melee].moving;
    Tick(30*7);
    stopped = stopped && !units[melee].attacking && !units[melee].moving;
    Check(stopped && PathQueueLength() == 0 && MapIsWalkable(MOVE_GROUND, units[melee].pos),
          "ordered to attack it, the Melee walks to the shore and stops there (checked at 3 s and 10 s: no endless chase or pathing)");
    Check(units[boat].hp == UNIT_STATS[UNIT_BOAT].hp, "...and never hits it");

    // Ranged units and towers in range hit a boat.
    const UnitType SHOOTERS[3] = { UNIT_ARCHER, UNIT_SCOUT, UNIT_MAGE };
    for (int k = 0; k < 3; k++)
    {
        int dmg = ShotsAtBoat(SHOOTERS[k], false);
        Check(dmg > 0, TextFormat("%s on the shore hits a boat in range (%d damage in 8 s)", UNIT_STATS[SHOOTERS[k]].name, dmg));
    }
    int dmg = ShotsAtBoat(UNIT_ARCHER, true);
    Check(dmg > 0, TextFormat("a Guard Tower hits a boat in range (%d damage in 8 s)", dmg));

    // A boat doesn't chase a unit inland.
    NewWorld();
    Vector2 boatStart = Tile(LAND_W, 10);
    boat = Spawn(UNIT_BOAT, PLAYER_TEAM, boatStart);
    int enemy = Spawn(UNIT_MELEE, AI_TEAM, Tile(LAND_W - 4, 10));   // within aggro radius, but no water is within the boat's range of it
    UnitsOrderHold(&enemy, 1);
    Tick(30*5);
    Check(Vector2Distance(units[boat].pos, boatStart) < 2.0f, "an idle Boat ignores a unit inland it can't reach or hit");
}

// --- 5. The Ship's splash: ground, boats and buildings, not flyers ----------------------
static void TestShipSplash(void)
{
    NewWorld();
    Vector2 land = Tile(LAND_W - 1, 10);   // the shot lands on an AI Melee on the shore...
    int melee = Spawn(UNIT_MELEE, AI_TEAM, land);
    int boat = Spawn(UNIT_BOAT, AI_TEAM, (Vector2){ land.x + 26.0f, land.y });   // ...next to an AI boat,
    int falcon = Spawn(UNIT_FALCON, AI_TEAM, (Vector2){ land.x, land.y + 4.0f });   // a Falcon right above it,
    int barracks = BuildingPlace(BUILDING_BARRACKS, AI_TEAM, Tile(LAND_W - 2, 9), false);   // and a Barracks (tiles 13..14 x 8..9): its corner 23 px away
    int still[3] = { melee, boat, falcon };
    UnitsOrderHold(still, 3);
    int ship = Spawn(UNIT_SHIP, PLAYER_TEAM, Tile(LAND_W + 6, 10));   // ~7 tiles out: in its range
    PathComputeRegions();
    float hp[3] = { units[melee].hp, units[boat].hp, units[falcon].hp }, bhp = buildings[barracks].hp;
    UnitsOrderAttack(&ship, 1, melee);
    Tick(30*2);   // one shot (3 s cooldown), and the bolt's flight
    Check(units[melee].hp < hp[0], TextFormat("Ship's splash hit the ground unit (%.0f -> %.0f)", hp[0], units[melee].hp));
    Check(units[boat].hp < hp[1], TextFormat("...the boat next to it (%.0f -> %.0f)", hp[1], units[boat].hp));
    Check(buildings[barracks].hp < bhp, TextFormat("...the building wall in the splash (%.0f -> %.0f)", bhp, buildings[barracks].hp));
    Check(units[falcon].hp == hp[2], "...but not the Falcon above it (it can't hit flyers)");
}

// --- 6. NAVAL regions ------------------------------------------------------------------
static void TestRegions(void)
{
    NewWorld();
    int lake = PathRegion(MOVE_NAVAL, Tile(30, 20)), pond = PathRegion(MOVE_NAVAL, Tile(POND_X + 1, POND_Y + 1));
    Check(lake != 0 && pond != 0 && lake != pond, "the lake and the pond are two NAVAL regions");
    Check(PathRegion(MOVE_NAVAL, Tile(20, 2)) == PathRegion(MOVE_NAVAL, Tile(33, 2)), "both sides of the land wall are one NAVAL region (joined to the south)");
    Check(PathRegion(MOVE_NAVAL, Tile(5, 5)) == 0, "land is NAVAL region 0");
    Check(!PathCanReach(MOVE_NAVAL, Tile(30, 20), (Rectangle){ Tile(POND_X + 1, POND_Y + 1).x, Tile(POND_X + 1, POND_Y + 1).y, 0, 0 }, 16.0f), "PathCanReach: lake to pond: no");
    Check(PathCanReach(MOVE_NAVAL, Tile(30, 20), (Rectangle){ Tile(LAND_W - 1, 10).x, Tile(LAND_W - 1, 10).y, 0, 0 }, 110.0f), "PathCanReach: a boat (range 110) can get in range of the shore");
    Check(!PathCanReach(MOVE_NAVAL, Tile(30, 20), (Rectangle){ Tile(5, 10).x, Tile(5, 10).y, 0, 0 }, 110.0f), "...but not of a point far inland");
    Check(!PathCanReach(MOVE_GROUND, Tile(5, 10), (Rectangle){ Tile(30, 20).x, Tile(30, 20).y, 0, 0 }, 16.0f), "PathCanReach: a Melee can't get to the middle of the lake");
    Check(PathCanReach(MOVE_AIR, Tile(5, 10), (Rectangle){ Tile(30, 20).x, Tile(30, 20).y, 0, 0 }, 0.0f), "PathCanReach: flyers reach anything");
}

// --- 7. Map files and the editor ---------------------------------------------------------
static const char *TEST_DIR = ".";
static void WriteMap(const char *path, const char *objects)
{
    FILE *f = fopen(path, "w");
    fprintf(f, "name Naval test\nwidth %d\nheight %d\ntiles\n", TW, TH);
    for (int y = 0; y < TH; y++)
    {
        for (int x = 0; x < TW; x++) fputc(TILE_INFO[testTiles[y*TW + x]].fileChar, f);
        fputc('\n', f);
    }
    fprintf(f, "base 0 2 2\nbase 1 2 10\n%s", objects);
    fclose(f);
}

static void TestMapFiles(void)
{
    NewWorld();   // fills testTiles
    char path[300];
    snprintf(path, sizeof(path), "%s/naval_test.map", TEST_DIR);
    WriteMap(path, "dock 0 14 9\nboat 0 20 5\nship 1 22 7\n");
    MapStart start;
    UnitsReset(); BuildingsReset(); EconomyInit(); GridRebuild();
    bool loaded = MapFileLoad(path, &start);
    Check(loaded, TextFormat("a map with dock, boat and ship loads (%s)", loaded ? "ok" : MapFileError()));
    int docks = 0, boats = 0, ships = 0;
    for (int b = 0; b < MAX_BUILDINGS; b++) docks += buildings[b].active && buildings[b].type == BUILDING_DOCK;
    for (int i = 0; i < MAX_UNITS; i++) { boats += units[i].active && units[i].type == UNIT_BOAT; ships += units[i].active && units[i].type == UNIT_SHIP; }
    Check(docks == 1 && boats == 1 && ships == 1, TextFormat("...with 1 Dock, 1 Boat, 1 Ship (%d, %d, %d)", docks, boats, ships));

    WriteMap(path, "dock 0 6 9\n");
    bool parsed = MapFileParse(path, &checkDoc);
    Check(!parsed && strstr(MapFileError(), "must be next to water"), TextFormat("a Dock away from water is refused: \"%s\"", MapFileError()));
    WriteMap(path, "boat 0 5 5\n");
    Check(!MapFileParse(path, &checkDoc), "a Boat on land is refused");
    remove(path);
}

// The editor (its own tool code, the path a click takes): rebuild harbor_64x64.map object by
// object, add a Dock, a Boat and a Ship, save it as harbor_64x64.map and read it back.
static bool EditorPlace(ToolKind kind, int type, int objTeam, int x, int y)
{
    tool = kind; toolType = type; team = objTeam;
    int half = (kind == TOOL_BUILDING) ? BUILDING_STATS[type].size/2 : 0;
    MapObject o = ToolObject(x + half, y + half);   // the mouse is on the footprint's middle tile
    if (!MapDocObjectFits(&doc, &o, -1)) return false;
    doc.objects[doc.objectCount++] = o;
    return true;
}

static void TestEditor(void)
{
    char original[300];
    snprintf(original, sizeof(original), "%s/harbor_64x64.map", MAPS_DIR);
    bool ok = MapFileParse(original, &doc);
    Check(ok, TextFormat("maps/harbor_64x64.map loads (%s)", ok ? "ok" : MapFileError()));
    if (!ok) return;
    static MapDoc copy;
    copy = doc;
    snprintf(nameText, sizeof(nameText), "%s", doc.name);
    doc.objectCount = 0;
    int placed = 0;
    for (int i = 0; i < copy.objectCount; i++)
    {
        const MapObject *o = &copy.objects[i];
        ToolKind kind = (o->kind == MAPOBJ_BUILDING) ? TOOL_BUILDING : (o->kind == MAPOBJ_UNIT) ? TOOL_UNIT : TOOL_GOLD;
        if (kind == TOOL_GOLD) snprintf(goldText, sizeof(goldText), "%d", o->amount);
        placed += EditorPlace(kind, o->type, o->team, o->x, o->y);
    }
    Check(placed == copy.objectCount, TextFormat("the editor re-placed all %d objects of the Harbor map", copy.objectCount));

    tool = TOOL_BUILDING; toolType = BUILDING_DOCK; team = PLAYER_TEAM;
    MapObject inland = ToolObject(6, 6);
    Check(!MapDocObjectFits(&doc, &inland, -1) && strcmp(MapDocObjectProblem(&doc, &inland, -1), "Dock must be next to water") == 0,
          "the editor refuses a Dock inland with \"Dock must be next to water\"");
    // The shore by the player's base: land at x 18, gravel beach at x 19, water from x 20 (row 31).
    Check(EditorPlace(TOOL_BUILDING, BUILDING_DOCK, PLAYER_TEAM, 18, 33), "the editor places a Dock on the Harbor shore");
    Check(EditorPlace(TOOL_UNIT, UNIT_BOAT, PLAYER_TEAM, 24, 31) && EditorPlace(TOOL_UNIT, UNIT_SHIP, AI_TEAM, 39, 32), "...a Boat and a Ship on the bay");
    Check(!EditorPlace(TOOL_UNIT, UNIT_BOAT, PLAYER_TEAM, 14, 20), "...but not a Boat on land");

    // Painting land over the Dock's water is skipped while it's the Dock's last water.
    toolType = TILE_GRASS; brushIndex = 0;
    for (int y = 30; y <= 37; y++) for (int x = 19; x <= 21; x++) PaintTiles(x, y);
    Check(WaterBuildingsStillFit() && MapDocObjectFits(&doc, &doc.objects[doc.objectCount - 3], doc.objectCount - 3), "painting land around a Dock never takes all of its water");

    char saved[300];
    snprintf(saved, sizeof(saved), "%s/harbor_64x64.map", TEST_DIR);   // the test's own folder, not maps/
    bool written = WriteAndCheck(saved);
    Check(written, TextFormat("the editor saved harbor_64x64.map and it loads back%s%s", written ? "" : ": ", written ? "" : MapFileError()));
    int d = 0, b = 0, s = 0;
    for (int i = 0; i < checkDoc.objectCount; i++)
    {
        d += checkDoc.objects[i].kind == MAPOBJ_BUILDING && checkDoc.objects[i].type == BUILDING_DOCK;
        b += checkDoc.objects[i].kind == MAPOBJ_UNIT && checkDoc.objects[i].type == UNIT_BOAT;
        s += checkDoc.objects[i].kind == MAPOBJ_UNIT && checkDoc.objects[i].type == UNIT_SHIP;
    }
    Check(checkDoc.objectCount == copy.objectCount + 3 && d == 1 && b == 1 && s == 1, TextFormat("...with the Dock, Boat and Ship in it (%d objects)", checkDoc.objectCount));
    remove(saved);
}

int main(int argc, char **argv)
{
    if (argc > 1) TEST_DIR = argv[1];
    SetTraceLogLevel(LOG_WARNING);
    TestPlacement();
    TestDockSpawn();
    TestNavalMovement();
    TestTargeting();
    TestShipSplash();
    TestRegions();
    TestMapFiles();
    TestEditor();
    printf("%d naval checks; %s: %d problem(s)\n", checks, failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
