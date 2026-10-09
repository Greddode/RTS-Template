// editor.c - In-game map editor.
//
// The editor works on its own MapDoc (mapfile.h): tiles plus a list of
// objects - the same thing a .map file holds. It never touches the game's
// unit, building or gold pools. To draw, it copies its tiles into the map
// module (MapSetTiles) and draws objects with the game's own shapes.
//
// Tools (left panel, generated from the tables where possible):
//   tile brushes   one per TILE_INFO entry, brush size 1 / 3 / 5
//   buildings      one per BUILDING_STATS entry, for the chosen team
//   units          one per UNIT_STATS entry, for the chosen team; the brush
//                  size places an NxN block, one unit per tile (click or drag)
//   gold           with an amount field
//   erase          removes the object under the cursor
// Objects can only go where a map file allows them (MapDocObjectFits; for
// buildings that's the game's own rule, BuildingsPlacementOK), and painting
// skips tiles whose object couldn't stand on the new tile (water under a
// Base) or that would leave a Dock without water, so the map stays valid. The
// unit brush skips tiles that don't allow a unit (water, rock, taken), and its
// ghost shows every tile: green = a unit goes there, red = skipped.
//
// Save writes the normal text format, then reads the file back with the
// normal loader (MapFileParse) and shows its error if anything is wrong.
// Undo (Ctrl+Z) covers tile painting: before each brush stroke the tiles are
// copied into a ring of UNDO_STEPS snapshots (fixed memory).
//
// Web build: browsers can't write to disk, so Save downloads the file, and
// Load is disabled.

#include "editor.h"
#include "buildings.h"
#include "camera.h"
#include "config.h"
#include "economy.h"
#include "map.h"
#include "mapfile.h"
#include "menu.h"
#include "ui.h"
#include "units.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__EMSCRIPTEN__)
    // In web_download.js: gives a file from the in-memory file system to the browser as a download.
    extern void EditorDownloadFile(const char *path, const char *name);
#endif

#define UNDO_STEPS     32
#define PANEL_W        240.0f   // reference sizes (720 px tall window), scaled by Ui()
#define ROW_H          30.0f
#define GAP            6.0f
#define PAD            10.0f
#define VIEW_MARGIN    8.0f     // fully zoomed out: gap around the map
#define MESSAGE_LONG   8.0
#define DEFAULT_GOLD   "1500"
#define GRID_COLOR     (Color){ 0, 0, 0, 40 }
#define GHOST_OK       (Color){ 60, 230, 90, 255 }
#define GHOST_BLOCKED  (Color){ 240, 60, 50, 255 }

typedef enum { TOOL_TILE, TOOL_BUILDING, TOOL_UNIT, TOOL_GOLD, TOOL_ERASE } ToolKind;

static MapDoc   doc;              // the map being edited
static MapDoc   checkDoc;         // scratch: reading files back to check / load them
static ToolKind tool = TOOL_TILE;
static int      toolType = TILE_WATER;
static int      team = PLAYER_TEAM;
static int      brushIndex = 0;   // 0, 1, 2 -> size 1, 3, 5 (tiles and units)
static bool     strokeStopped;    // unit brush hit the limit: place nothing more until the mouse is released
static char     goldText[8] = DEFAULT_GOLD;
static char     nameText[MAP_NAME_LEN] = "My map";
static bool     tilesDirty = true;
static bool     loading = false;  // the map list is open
static float    panelScroll = 0.0f;
static float    panelContentH = 0.0f;   // the tool list's height, measured while drawing (reference px)
static char     testPlayPath[300];

static unsigned char undo[UNDO_STEPS][MAP_W*MAP_H];
static int undoNext = 0, undoCount = 0;

static const int BRUSH_SIZES[3] = { 1, 3, 5 };

// --- Small helpers -------------------------------------------------------------------

static int ObjectSize(const MapObject *o)
{
    return (o->kind == MAPOBJ_BUILDING) ? BUILDING_STATS[o->type].size : 1;
}

// The object covering tile x,y, or -1.
static int ObjectAt(int x, int y)
{
    for (int i = 0; i < doc.objectCount; i++)
    {
        const MapObject *o = &doc.objects[i];
        int s = ObjectSize(o);
        if (x >= o->x && x < o->x + s && y >= o->y && y < o->y + s) return i;
    }
    return -1;
}

static void RemoveObject(int index)
{
    for (int i = index + 1; i < doc.objectCount; i++) doc.objects[i - 1] = doc.objects[i];
    doc.objectCount--;
}

static void MouseTile(int *x, int *y)
{
    Vector2 m = CamMouseWorld();
    *x = (int)(m.x/TILE_SIZE) - (m.x < 0);
    *y = (int)(m.y/TILE_SIZE) - (m.y < 0);
}

// The object the current tool would place with the mouse on tile x,y.
static MapObject ToolObject(int x, int y)
{
    MapObject o = { .team = team, .type = toolType, .x = x, .y = y };
    if (tool == TOOL_BUILDING) { o.kind = MAPOBJ_BUILDING; o.x -= BUILDING_STATS[toolType].size/2; o.y -= BUILDING_STATS[toolType].size/2; }
    else if (tool == TOOL_UNIT) o.kind = MAPOBJ_UNIT;
    else { o.kind = MAPOBJ_GOLD; o.type = 0; o.team = 0; o.amount = atoi(goldText); }
    return o;
}

static void ResetEditing(void)
{
    undoCount = 0;
    loading = false;
    panelScroll = 0.0f;
    snprintf(nameText, sizeof(nameText), "%s", doc.name);
    MapSetTiles(doc.width, doc.height, doc.tiles);   // now, so the camera below centres on THIS map's size
    tilesDirty = false;
    CamInit();
}

// The screen area beside the tool panel, where the map is shown.
static Rectangle WorldView(void)
{
    float left = Ui(PAD) + Ui(PANEL_W) + Ui(PAD);
    float margin = Ui(VIEW_MARGIN);
    Rectangle view = { left + margin, margin, GetScreenWidth() - left - margin*2.0f, GetScreenHeight() - margin*2.0f };
    if (view.width < Ui(40.0f)) view = (Rectangle){ 0, 0, (float)GetScreenWidth(), (float)GetScreenHeight() };   // window narrower than the panel
    return view;
}

// --- Opening -------------------------------------------------------------------------

void EditorOpenNew(int size)
{
    snprintf(doc.name, sizeof(doc.name), "New map %dx%d", size, size);
    doc.width = doc.height = size;
    memset(doc.tiles, TILE_GRASS, sizeof(doc.tiles));
    doc.objectCount = 0;
    ResetEditing();
}

void EditorOpenFromGame(void)
{
    snprintf(doc.name, sizeof(doc.name), "Edited map");
    doc.width = MapWidth();
    doc.height = MapHeight();
    for (int y = 0; y < doc.height; y++)
        for (int x = 0; x < doc.width; x++) doc.tiles[y*doc.width + x] = (unsigned char)MapGetTile(x, y);
    doc.objectCount = 0;

    // Buildings, then gold, then units. Anything that wouldn't be valid in a
    // file (e.g. units crowded onto one tile) is left out and counted.
    int skipped = 0;
    for (int pass = 0; pass < 3; pass++)
    {
        int count = (pass == 0) ? MAX_BUILDINGS : (pass == 1) ? MAX_GOLD_NODES : MAX_UNITS;
        for (int i = 0; i < count && doc.objectCount < MAP_MAX_OBJECTS; i++)
        {
            MapObject o = { 0 };
            if (pass == 0) { if (!buildings[i].active) continue; o = (MapObject){ MAPOBJ_BUILDING, buildings[i].type, buildings[i].team, buildings[i].tx, buildings[i].ty, 0, 0 }; }
            if (pass == 1) { if (!goldNodes[i].active) continue; o = (MapObject){ MAPOBJ_GOLD, 0, 0, (int)(goldNodes[i].pos.x/TILE_SIZE), (int)(goldNodes[i].pos.y/TILE_SIZE), goldNodes[i].amount, 0 }; }
            if (pass == 2) { if (!UnitIsActiveInWorld(&units[i])) continue; o = (MapObject){ MAPOBJ_UNIT, units[i].type, units[i].team, (int)(units[i].pos.x/TILE_SIZE), (int)(units[i].pos.y/TILE_SIZE), 0, 0 }; }   // units inside a transport aren't copied (maps hold no cargo)
            if (MapDocObjectFits(&doc, &o, -1)) doc.objects[doc.objectCount++] = o;
            else skipped++;
        }
    }
    ResetEditing();
    if (skipped > 0) UiShowMessageFor(TextFormat("Copied the current map (%d units left out: they shared a tile)", skipped), MESSAGE_LONG);
}

void EditorResume(void)
{
    tilesDirty = true;   // the game used the map module; show our tiles again
    loading = false;
}

const char *EditorTestPlayPath(void)
{
    return testPlayPath;
}

// --- Undo (tile painting only) --------------------------------------------------------

static void PushUndo(void)
{
    memcpy(undo[undoNext], doc.tiles, doc.width*doc.height);
    undoNext = (undoNext + 1) % UNDO_STEPS;   // a ring: the oldest step is overwritten
    if (undoCount < UNDO_STEPS) undoCount++;
}

static void PopUndo(void)
{
    if (undoCount == 0) { UiShowMessage("Nothing to undo"); return; }
    undoNext = (undoNext + UNDO_STEPS - 1) % UNDO_STEPS;
    undoCount--;
    memcpy(doc.tiles, undo[undoNext], doc.width*doc.height);
    tilesDirty = true;
}

// --- Editing the map -------------------------------------------------------------------

// Every needsWater building (a Dock) still touches water (BuildingsPlacementOK, via the document).
static bool WaterBuildingsStillFit(void)
{
    for (int i = 0; i < doc.objectCount; i++)
        if (doc.objects[i].kind == MAPOBJ_BUILDING && BUILDING_STATS[doc.objects[i].type].needsWater && !MapDocObjectFits(&doc, &doc.objects[i], i)) return false;
    return true;
}

static void PaintTiles(int cx, int cy)
{
    int half = BRUSH_SIZES[brushIndex]/2;
    for (int y = cy - half; y <= cy + half; y++)
    {
        for (int x = cx - half; x <= cx + half; x++)
        {
            if (x < 0 || y < 0 || x >= doc.width || y >= doc.height) continue;
            int o = ObjectAt(x, y);   // never paint a tile the object on it can't stand on (water under a Base...)
            if (o != -1 && !TileAllows((TileType)toolType, MapObjectClass(&doc.objects[o]))) continue;
            unsigned char old = doc.tiles[y*doc.width + x];
            doc.tiles[y*doc.width + x] = (unsigned char)toolType;
            if (!WaterBuildingsStillFit()) doc.tiles[y*doc.width + x] = old;   // ...or take the last water from a Dock
        }
    }
    tilesDirty = true;
}

// --- Unit brush ----------------------------------------------------------------------

#define BRUSH_MAX_TILES 25   // 5x5

typedef struct BrushTile { MapObject unit; bool fits; } BrushTile;

// The tiles of an NxN unit brush centred on cx,cy (row by row, top-left
// first), each with "a unit fits here" by the map file rules. Tiles outside
// the map aren't listed. Returns how many.
static int BrushTiles(int cx, int cy, BrushTile *out)
{
    int half = BRUSH_SIZES[brushIndex]/2, n = 0;
    for (int y = cy - half; y <= cy + half; y++)
    {
        for (int x = cx - half; x <= cx + half; x++)
        {
            if (x < 0 || y < 0 || x >= doc.width || y >= doc.height) continue;
            out[n].unit = (MapObject){ .kind = MAPOBJ_UNIT, .type = toolType, .team = team, .x = x, .y = y };
            out[n].fits = MapDocObjectFits(&doc, &out[n].unit, -1);   // open ground, nothing on the tile yet
            n++;
        }
    }
    return n;
}

static int DocUnitCount(void)
{
    int n = 0;
    for (int i = 0; i < doc.objectCount; i++) n += (doc.objects[i].kind == MAPOBJ_UNIT);
    return n;
}

// Place the brush's units. Called every frame while the button is held: a
// tile that got a unit no longer fits, so a drag never puts two on one tile.
static void PaintUnits(int cx, int cy, bool firstFrame)
{
    if (strokeStopped) return;
    static BrushTile tiles[BRUSH_MAX_TILES];
    int count = BrushTiles(cx, cy, tiles), placed = 0, unitCount = DocUnitCount();
    for (int i = 0; i < count; i++)
    {
        if (!tiles[i].fits) continue;
        if (doc.objectCount >= MAP_MAX_OBJECTS || unitCount >= MAX_UNITS)
        {
            UiShowMessageFor((doc.objectCount >= MAP_MAX_OBJECTS)
                ? TextFormat("Limit reached: a map holds at most %d objects (units, buildings and gold)", MAP_MAX_OBJECTS)
                : TextFormat("Limit reached: a map holds at most %d units", MAX_UNITS), MESSAGE_LONG);
            strokeStopped = true;   // stop this stroke; release the mouse to try again
            return;
        }
        doc.objects[doc.objectCount++] = tiles[i].unit;
        unitCount++;
        placed++;
    }
    if (firstFrame && placed == 0 && count > 0) UiShowMessage("Doesn't fit there");
}

static void WorldInput(void)
{
    if (UiWantsMouse() || loading) return;
    int x, y;
    MouseTile(&x, &y);
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) strokeStopped = false;   // a new stroke

    if (tool == TOOL_TILE)
    {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) PushUndo();   // one undo step per stroke
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) PaintTiles(x, y);
    }
    else if (tool == TOOL_ERASE)
    {
        int i = ObjectAt(x, y);
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && i != -1) RemoveObject(i);
    }
    else if (tool == TOOL_UNIT)
    {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) PaintUnits(x, y, IsMouseButtonPressed(MOUSE_BUTTON_LEFT));
    }
    else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        MapObject o = ToolObject(x, y);
        if (o.kind == MAPOBJ_GOLD && (o.amount <= 0 || o.amount > 1000000)) UiShowMessage("Gold amount must be 1..1000000");
        else if (doc.objectCount >= MAP_MAX_OBJECTS) UiShowMessage("Too many objects");
        else if (!MapDocObjectFits(&doc, &o, -1)) UiShowMessage(MapDocObjectProblem(&doc, &o, -1));   // e.g. "Dock must be next to water"
        else doc.objects[doc.objectCount++] = o;
    }
}

// --- Saving, loading, test play --------------------------------------------------------

// "My Map!" -> "my_map": safe for any file system.
static void FileNameFrom(const char *name, char *out, int size)
{
    int n = 0;
    for (const char *c = name; *c && n < size - 1; c++)
    {
        char ch = *c;
        if (ch >= 'A' && ch <= 'Z') ch += 32;
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-') out[n++] = ch;
        else if (n > 0 && out[n - 1] != '_') out[n++] = '_';
    }
    while (n > 0 && out[n - 1] == '_') n--;
    if (n == 0) { snprintf(out, size, "map"); return; }
    out[n] = '\0';
}

// Write the map, then read it back with the normal loader. Returns true if it loads.
static bool WriteAndCheck(const char *path)
{
    snprintf(doc.name, sizeof(doc.name), "%s", nameText);
    if (!MapFileWrite(path, &doc)) { UiShowMessageFor(MapFileError(), MESSAGE_LONG); return false; }
    if (!MapFileParse(path, &checkDoc))
    {
        UiShowMessageFor(TextFormat("Saved, but the game can't load it: %s", MapFileError()), MESSAGE_LONG);
        return false;
    }
    return true;
}

static void Save(void)
{
    char file[64];
    FileNameFrom(nameText, file, sizeof(file) - 4);
    strcat(file, ".map");
#if defined(__EMSCRIPTEN__)
    // Browsers can't write to disk: write into the in-memory file system,
    // then hand the file to the browser as a download.
    char path[300];
    snprintf(path, sizeof(path), "/tmp/%s", file);
    bool ok = WriteAndCheck(path);
    EditorDownloadFile(path, file);
    if (ok) UiShowMessageFor(TextFormat("Downloaded %s (browsers can't save to disk directly)", file), MESSAGE_LONG);
#else
    char path[300];
    snprintf(path, sizeof(path), "%s/%s", MapFilesFolder(), file);
    if (WriteAndCheck(path)) UiShowMessageFor(TextFormat("Saved %s", path), MESSAGE_LONG);
#endif
}

static void StartLoad(void)
{
#if defined(__EMSCRIPTEN__)
    UiShowMessageFor("Loading isn't available in the browser (it can't read files from your disk)", MESSAGE_LONG);
#else
    MenuOpenMapPicker();
    loading = true;
#endif
}

static void LoadPicked(int index)
{
    if (!MapFileParse(MapFilePath(index), &checkDoc)) { UiShowMessageFor(MapFileError(), MESSAGE_LONG); return; }
    memcpy(&doc, &checkDoc, sizeof(doc));   // only replace our map once the file is known to be good
    ResetEditing();
    UiShowMessage(TextFormat("Loaded %s", MapFileName(index)));
}

static bool PrepareTestPlay(void)
{
#if defined(__EMSCRIPTEN__)
    snprintf(testPlayPath, sizeof(testPlayPath), "/tmp/editor_testplay.map");
#else
    snprintf(testPlayPath, sizeof(testPlayPath), "%seditor_testplay.map", GetApplicationDirectory());   // not in maps/: stays out of the map list
#endif
    return WriteAndCheck(testPlayPath);
}

// --- Drawing ---------------------------------------------------------------------------

static void DrawObjects(void)
{
    for (int i = 0; i < doc.objectCount; i++)
    {
        const MapObject *o = &doc.objects[i];
        Vector2 c = { (o->x + 0.5f)*TILE_SIZE, (o->y + 0.5f)*TILE_SIZE };
        if (o->kind == MAPOBJ_BUILDING)
        {
            float s = (float)BUILDING_STATS[o->type].size*TILE_SIZE;
            BuildingsDrawLook((BuildingType)o->type, o->team, (Rectangle){ (float)o->x*TILE_SIZE, (float)o->y*TILE_SIZE, s, s }, false);
        }
        else if (o->kind == MAPOBJ_GOLD) EconomyDrawNode(c, o->amount);
        else UnitsDrawIcon((UnitType)o->type, o->team, c, UNIT_RADIUS);
    }
}

static void DrawTileGrid(Rectangle view)
{
    float line = 1.0f/gameCamera.zoom;
    int x0 = (int)(view.x/TILE_SIZE), x1 = (int)((view.x + view.width)/TILE_SIZE) + 1;
    int y0 = (int)(view.y/TILE_SIZE), y1 = (int)((view.y + view.height)/TILE_SIZE) + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > doc.width) x1 = doc.width;
    if (y1 > doc.height) y1 = doc.height;
    for (int x = x0; x <= x1; x++) DrawRectangleRec((Rectangle){ (float)x*TILE_SIZE, (float)y0*TILE_SIZE, line, (float)(y1 - y0)*TILE_SIZE }, GRID_COLOR);
    for (int y = y0; y <= y1; y++) DrawRectangleRec((Rectangle){ (float)x0*TILE_SIZE, (float)y*TILE_SIZE, (float)(x1 - x0)*TILE_SIZE, line }, GRID_COLOR);
}

// What the current tool would do under the mouse: brush square, object ghost, or erase target.
static void DrawCursor(void)
{
    if (UiWantsMouse() || loading) return;
    int x, y;
    MouseTile(&x, &y);
    float line = 2.0f/gameCamera.zoom;

    if (tool == TOOL_TILE)
    {
        int half = BRUSH_SIZES[brushIndex]/2;
        Rectangle r = { (float)(x - half)*TILE_SIZE, (float)(y - half)*TILE_SIZE, (float)BRUSH_SIZES[brushIndex]*TILE_SIZE, (float)BRUSH_SIZES[brushIndex]*TILE_SIZE };
        DrawRectangleLinesEx(r, line, RAYWHITE);
    }
    else if (tool == TOOL_UNIT)   // one ghost per brush tile: green = placed, red = skipped
    {
        static BrushTile tiles[BRUSH_MAX_TILES];
        int count = BrushTiles(x, y, tiles);
        for (int i = 0; i < count; i++)
        {
            Color c = tiles[i].fits ? GHOST_OK : GHOST_BLOCKED;
            Rectangle r = { (float)tiles[i].unit.x*TILE_SIZE, (float)tiles[i].unit.y*TILE_SIZE, (float)TILE_SIZE, (float)TILE_SIZE };
            DrawRectangleRec(r, Fade(c, 0.35f));
            DrawRectangleLinesEx(r, line, c);
        }
    }
    else if (tool == TOOL_ERASE)
    {
        int i = ObjectAt(x, y);
        if (i == -1) return;
        int s = ObjectSize(&doc.objects[i]);
        DrawRectangleLinesEx((Rectangle){ (float)doc.objects[i].x*TILE_SIZE, (float)doc.objects[i].y*TILE_SIZE, (float)s*TILE_SIZE, (float)s*TILE_SIZE }, line, GHOST_BLOCKED);
    }
    else
    {
        MapObject o = ToolObject(x, y);
        int s = ObjectSize(&o);
        Color c = MapDocObjectFits(&doc, &o, -1) ? GHOST_OK : GHOST_BLOCKED;
        Rectangle r = { (float)o.x*TILE_SIZE, (float)o.y*TILE_SIZE, (float)s*TILE_SIZE, (float)s*TILE_SIZE };
        DrawRectangleRec(r, Fade(c, 0.35f));
        DrawRectangleLinesEx(r, line, c);
    }
}

// --- The tool panel ---------------------------------------------------------------------

static void Section(float x, float *y, float w, const char *title)
{
    *y += Ui(GAP);
    UiLabel(title, x, *y, Ui(16.0f), LIGHTGRAY);
    *y += Ui(20.0f);
    (void)w;
}

static Rectangle Row(float x, float *y, float w)
{
    Rectangle r = { x, *y, w, Ui(ROW_H) };
    *y += Ui(ROW_H + GAP);
    return r;
}

// Two buttons side by side.
static void HalfRows(float x, float y, float w, Rectangle *left, Rectangle *right)
{
    float half = (w - Ui(GAP))*0.5f;
    *left = (Rectangle){ x, y, half, Ui(ROW_H) };
    *right = (Rectangle){ x + half + Ui(GAP), y, half, Ui(ROW_H) };
}

static EditorAction DrawPanel(void)
{
    EditorAction action = EDITOR_STAY;
    Rectangle panel = { Ui(PAD), Ui(PAD), Ui(PANEL_W), GetScreenHeight() - Ui(PAD)*2.0f };
    UiPanel(panel);
    Rectangle area = { panel.x + Ui(PAD), panel.y + Ui(PAD), panel.width - Ui(PAD)*2.0f, panel.height - Ui(PAD)*2.0f };
    float w = area.width - Ui(8.0f);   // room for the scrollbar
    float x = area.x;
    // The list's height is measured as it's drawn (last frame's), so new tile,
    // unit or building buttons are always reachable by scrolling.
    float top = area.y + UiScrollBegin(area, Ui(panelContentH > 0.0f ? panelContentH : 1100.0f), &panelScroll);
    float y = top;
    Rectangle a, b;

    UiLabel("Map Editor", x, y, Ui(24.0f), RAYWHITE);
    y += Ui(32.0f);
    UiLabel(TextFormat("%dx%d tiles, %d objects", doc.width, doc.height, doc.objectCount), x, y, Ui(15.0f), LIGHTGRAY);
    y += Ui(22.0f);

    Section(x, &y, w, "Name");
    UiTextField(Row(x, &y, w), nameText, sizeof(nameText), false);
    HalfRows(x, y, w, &a, &b); y += Ui(ROW_H + GAP);
    if (UiButton(a, "Save", 0)) Save();
#if defined(__EMSCRIPTEN__)
    if (UiButtonEx(b, "Load", 0, true)) StartLoad();   // dimmed: explains why it's unavailable
#else
    if (UiButton(b, "Load", 0)) StartLoad();
#endif
    HalfRows(x, y, w, &a, &b); y += Ui(ROW_H + GAP);
    if (UiButton(a, "Test Play", 0) && PrepareTestPlay()) action = EDITOR_TEST_PLAY;
    if (UiButton(b, "Exit", 0)) action = EDITOR_EXIT;

    Section(x, &y, w, "New map (all grass)");
    float third = (w - Ui(GAP)*2.0f)/3.0f;
    static const int NEW_SIZES[3] = { 32, 64, 128 };
    for (int i = 0; i < 3; i++)
    {
        Rectangle r = { x + i*(third + Ui(GAP)), y, third, Ui(ROW_H) };
        if (UiButton(r, TextFormat("%d", NEW_SIZES[i]), 0)) EditorOpenNew(NEW_SIZES[i]);
    }
    y += Ui(ROW_H + GAP);

    Section(x, &y, w, "Tiles (Ctrl+Z: undo)");
    for (int t = 0; t < TILE_COUNT; t++)   // one brush per tile type in TILE_INFO
    {
        Rectangle r = Row(x, &y, w);
        if (UiToggle(r, TILE_INFO[t].name, tool == TOOL_TILE && toolType == t)) { tool = TOOL_TILE; toolType = t; }
        DrawRectangleRec((Rectangle){ r.x + Ui(6.0f), r.y + Ui(7.0f), Ui(16.0f), Ui(16.0f) }, TILE_INFO[t].color);   // colour swatch
    }
    static const char *brushLabels[3] = { "1", "3", "5" };
    UiLabel("Brush size (tiles and units)", x, y, Ui(15.0f), LIGHTGRAY);
    y += Ui(18.0f);
    UiTabs(Row(x, &y, w), brushLabels, 3, &brushIndex);

    Section(x, &y, w, "Objects for team");
    static const char *teamLabels[2] = { "Player", "AI" };
    UiTabs(Row(x, &y, w), teamLabels, 2, &team);
    for (int t = 0; t < BUILDING_TYPE_COUNT; t++)   // one button per building type
    {
        if (UiToggle(Row(x, &y, w), BUILDING_STATS[t].name, tool == TOOL_BUILDING && toolType == t)) { tool = TOOL_BUILDING; toolType = t; }
    }
    for (int t = 0; t < UNIT_TYPE_COUNT; t++)       // one button per unit type
    {
        if (UiToggle(Row(x, &y, w), UNIT_STATS[t].name, tool == TOOL_UNIT && toolType == t)) { tool = TOOL_UNIT; toolType = t; }
    }
    HalfRows(x, y, w, &a, &b); y += Ui(ROW_H + GAP);
    if (UiToggle(a, "Gold", tool == TOOL_GOLD)) tool = TOOL_GOLD;
    UiTextField(b, goldText, sizeof(goldText), true);
    if (UiToggle(Row(x, &y, w), "Erase object", tool == TOOL_ERASE)) tool = TOOL_ERASE;
    panelContentH = (y - top)/UiScale();

    UiScrollEnd();
    return action;
}

// --- Per frame ---------------------------------------------------------------------------

EditorAction EditorFrame(void)
{
    if (tilesDirty) { MapSetTiles(doc.width, doc.height, doc.tiles); tilesDirty = false; }   // first: the camera needs the map's size

    // Camera: pan and zoom like the game (it ignores the mouse over UI), but
    // zooming out stops where the whole map fits beside the panel.
    CamUpdateEditor(GetFrameTime(), WorldView());

    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (!UiWantsKeyboard() && ctrl && IsKeyPressed(KEY_UNDO)) PopUndo();
    WorldInput();
    if (tilesDirty) { MapSetTiles(doc.width, doc.height, doc.tiles); tilesDirty = false; }   // painted this frame

    ClearBackground(BLACK);
    BeginMode2D(gameCamera);
        Rectangle view = CamViewRect();
        MapDraw(view);
        DrawTileGrid(view);
        DrawObjects();
        DrawCursor();
    EndMode2D();

    EditorAction action = DrawPanel();
    if (!UiWantsKeyboard() && IsKeyPressed(KEY_EDITOR)) action = EDITOR_EXIT;   // F2 again closes the editor

    if (loading)
    {
        int picked = MenuPickMap();
        if (picked >= 0) { loading = false; LoadPicked(picked); }
        else if (picked == PICK_CANCELLED) loading = false;
    }

    int mx, my;
    MouseTile(&mx, &my);
    UiLabel(TextFormat("Tile %d, %d", mx, my), GetScreenWidth() - Ui(130.0f), GetScreenHeight() - Ui(26.0f), Ui(16.0f), RAYWHITE);
    UiDrawMessage();
    if (action != EDITOR_STAY) UiClearFocus();
    return action;
}
