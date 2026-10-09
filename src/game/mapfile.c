// mapfile.c - Plain-text map files.
//
// The format is described in maps/FORMAT.md. In short:
//   name <text>            width <n>            height <n>
//   tiles                  then exactly <height> rows of <width> characters,
//                          one per tile, from TILE_INFO in map.c:
//                          . grass   , dirt   ~ water   # rock   : gravel   ^ lava
//   <building> <team> <x> <y>     e.g. "base 0 4 50" (x, y = top-left tile)
//   <unit> <team> <x> <y>         e.g. "worker 0 8 54"
//   gold <x> <y> <amount>
// Building and unit keywords are the names in BUILDING_STATS / UNIT_STATS
// (any case), so new types work in map files with no loader changes.
// Lines starting with # and blank lines are comments - except inside the
// tile grid, which is read row by row exactly as written (# there is rock).
//
// MapFileParse() reads the whole file into a MapDoc and checks EVERYTHING
// (sizes, characters, coordinates, objects on tiles they can't stand on, overlapping objects,
// a building for each team) without touching the game. MapFileLoad() then
// sets the tiles and spawns the objects through the normal pools. The editor
// uses the same MapDoc, MapFileParse() and MapFileWrite().

#include "mapfile.h"
#include "buildings.h"
#include "config.h"
#include "economy.h"
#include "units.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIN_MAP_SIZE  8
#define MAX_GOLD      1000000

static MapDoc loadDoc;          // MapFileLoad's working copy (static: it's ~45 KB)
static char   errorText[256];

static char listNames[MAX_MAP_FILES][MAP_NAME_LEN];
static char listPaths[MAX_MAP_FILES][256];
static int  listCount = 0;

// Record an error as "file:line: message". Always returns false.
static bool Fail(const char *path, int line, const char *fmt, ...)
{
    char msg[200];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    if (line > 0) snprintf(errorText, sizeof(errorText), "%s:%d: %s", GetFileName(path), line, msg);
    else snprintf(errorText, sizeof(errorText), "%s: %s", GetFileName(path), msg);
    TraceLog(LOG_WARNING, "MAP: %s", errorText);
    return false;
}

const char *MapFileError(void)
{
    return errorText;
}

// Case-insensitive, and '_' matches a space: the keyword for "Archery Range"
// is archery_range (map files are read one word at a time).
static bool SameWord(const char *a, const char *b)
{
    while (*a && *b)
    {
        char ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : (*a == ' ') ? '_' : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : (*b == ' ') ? '_' : *b;
        if (ca != cb) return false;
        a++; b++;
    }
    return *a == *b;
}

static int TileFromChar(char c)
{
    for (int t = 0; t < TILE_COUNT; t++)
    {
        if (TILE_INFO[t].fileChar == c) return t;
    }
    return -1;
}

// Could a unit of this class be on tile x,y of the document? (A TileOpenFn, for BuildingsPlacementOK.)
static bool DocTileOpen(const void *source, int x, int y, MoveClass moveClass)
{
    const MapDoc *doc = source;
    if (x < 0 || y < 0 || x >= doc->width || y >= doc->height) return false;
    return TileAllows((TileType)doc->tiles[y*doc->width + x], moveClass);
}

MoveClass MapObjectClass(const MapObject *o)
{
    return (o->kind == MAPOBJ_UNIT) ? UNIT_STATS[o->type].moveClass : MOVE_GROUND;   // buildings and gold need ground
}

static int ObjectSize(const MapObject *o)
{
    return (o->kind == MAPOBJ_BUILDING) ? BUILDING_STATS[o->type].size : 1;
}

static bool Overlap(const MapObject *a, const MapObject *b)
{
    int sa = ObjectSize(a), sb = ObjectSize(b);
    return a->x < b->x + sb && b->x < a->x + sa && a->y < b->y + sb && b->y < a->y + sa;
}

const char *MapDocObjectProblem(const MapDoc *doc, const MapObject *o, int ignoreIndex)
{
    const char *why = NULL;
    if (o->kind == MAPOBJ_BUILDING && !BuildingsPlacementOK((BuildingType)o->type, o->x, o->y, DocTileOpen, doc, &why)) return why;   // the game's own rule
    int size = ObjectSize(o);
    for (int y = o->y; y < o->y + size; y++)
        for (int x = o->x; x < o->x + size; x++)
            if (!DocTileOpen(doc, x, y, MapObjectClass(o))) return "Doesn't fit there";
    for (int i = 0; i < doc->objectCount; i++)
    {
        if (i != ignoreIndex && Overlap(o, &doc->objects[i])) return "Doesn't fit there";
    }
    return NULL;
}

bool MapDocObjectFits(const MapDoc *doc, const MapObject *o, int ignoreIndex)
{
    return MapDocObjectProblem(doc, o, ignoreIndex) == NULL;
}

static const char *ObjectName(const MapObject *o)
{
    if (o->kind == MAPOBJ_GOLD) return "gold";
    return (o->kind == MAPOBJ_BUILDING) ? BUILDING_STATS[o->type].name : UNIT_STATS[o->type].name;
}

// Cut the next line out of `text` (in place). Returns NULL at the end.
static char *NextLine(char **text)
{
    if (**text == '\0') return NULL;
    char *line = *text;
    char *end = strchr(line, '\n');
    if (end) { *end = '\0'; *text = end + 1; }
    else *text = line + strlen(line);
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' ' || line[len - 1] == '\t')) line[--len] = '\0';
    return line;
}

static bool IsComment(const char *line)
{
    while (*line == ' ' || *line == '\t') line++;
    return *line == '\0' || *line == '#';
}

// One object line, e.g. "base 0 4 50" or "gold 10 12 1500".
static bool ParseObject(const char *path, int lineNo, const char *line, MapDoc *doc)
{
    if (doc->objectCount >= MAP_MAX_OBJECTS) return Fail(path, lineNo, "too many objects (max %d)", MAP_MAX_OBJECTS);
    char word[32], extra;
    int a, b, c;
    int n = sscanf(line, "%31s %d %d %d %c", word, &a, &b, &c, &extra);
    MapObject *o = &doc->objects[doc->objectCount];
    *o = (MapObject){ .line = lineNo, .type = -1 };

    if (SameWord(word, "gold"))
    {
        if (n != 4) return Fail(path, lineNo, "expected: gold <x> <y> <amount>");
        o->kind = MAPOBJ_GOLD; o->type = 0; o->x = a; o->y = b; o->amount = c;
        if (c <= 0 || c > MAX_GOLD) return Fail(path, lineNo, "gold amount must be 1..%d", MAX_GOLD);
    }
    else
    {
        for (int t = 0; t < BUILDING_TYPE_COUNT; t++) if (SameWord(word, BUILDING_STATS[t].name)) { o->kind = MAPOBJ_BUILDING; o->type = t; }
        for (int t = 0; t < UNIT_TYPE_COUNT; t++) if (SameWord(word, UNIT_STATS[t].name)) { o->kind = MAPOBJ_UNIT; o->type = t; }
        if (o->type == -1) return Fail(path, lineNo, "unknown object '%s' (expected gold, a building or a unit name)", word);
        if (n != 4) return Fail(path, lineNo, "expected: %s <team> <x> <y>", word);
        if (a != PLAYER_TEAM && a != AI_TEAM) return Fail(path, lineNo, "team must be %d (player) or %d (AI)", PLAYER_TEAM, AI_TEAM);
        o->team = a; o->x = b; o->y = c;
    }

    if (o->x < 0 || o->y < 0 || o->x >= doc->width || o->y >= doc->height)
        return Fail(path, lineNo, "%s at %d,%d is outside the %dx%d map", word, o->x, o->y, doc->width, doc->height);

    int size = ObjectSize(o);
    for (int y = o->y; y < o->y + size; y++)
        for (int x = o->x; x < o->x + size; x++)
            if (!DocTileOpen(doc, x, y, MapObjectClass(o)))
                return Fail(path, lineNo, (size > 1) ? "%s at %d,%d covers a tile it can't stand on (water, rock, lava) or the map edge"
                                                     : "%s at %d,%d is on a tile it can't stand on (TILE_INFO in map.c)", word, o->x, o->y);
    const char *why;
    if (o->kind == MAPOBJ_BUILDING && !BuildingsPlacementOK((BuildingType)o->type, o->x, o->y, DocTileOpen, doc, &why))
        return Fail(path, lineNo, "%s at %d,%d: %s", word, o->x, o->y, why);   // e.g. a Dock away from water

    for (int i = 0; i < doc->objectCount; i++)
    {
        const MapObject *other = &doc->objects[i];
        if (Overlap(o, other)) return Fail(path, lineNo, "%s at %d,%d overlaps the %s from line %d", word, o->x, o->y, ObjectName(other), other->line);
    }

    doc->objectCount++;
    return true;
}

bool MapFileParse(const char *path, MapDoc *doc)
{
    errorText[0] = '\0';
    char *text = LoadFileText(path);
    if (!text) return Fail(path, 0, "can't open the file");
    char *cursor = text;

    snprintf(doc->name, sizeof(doc->name), "%s", GetFileNameWithoutExt(path));
    doc->width = doc->height = doc->objectCount = 0;
    int lineNo = 0, row = 0;
    bool inGrid = false, gridDone = false, ok = true;

    char *line;
    while (ok && (line = NextLine(&cursor)) != NULL)
    {
        lineNo++;
        if (inGrid)   // tile rows: read exactly as written
        {
            int len = (int)strlen(line);
            if (len != doc->width) { ok = Fail(path, lineNo, "tile row %d has %d characters, expected %d", row + 1, len, doc->width); break; }
            for (int x = 0; x < doc->width && ok; x++)
            {
                int t = TileFromChar(line[x]);
                if (t < 0) ok = Fail(path, lineNo, "unknown tile character '%c' in column %d (use . , ~ # : ^)", line[x], x + 1);
                else doc->tiles[row*doc->width + x] = (unsigned char)t;
            }
            if (++row == doc->height) { inGrid = false; gridDone = true; }
            continue;
        }
        if (IsComment(line)) continue;

        char key[32];
        int value;
        sscanf(line, "%31s", key);
        if (!gridDone && SameWord(key, "name")) snprintf(doc->name, sizeof(doc->name), "%s", line + 5);
        else if (!gridDone && (SameWord(key, "width") || SameWord(key, "height")))
        {
            bool isWidth = SameWord(key, "width");
            int max = isWidth ? MAP_W : MAP_H;
            if (sscanf(line, "%*s %d", &value) != 1) ok = Fail(path, lineNo, "expected: %s <number>", key);
            else if (value < MIN_MAP_SIZE || value > max) ok = Fail(path, lineNo, "%s %d is out of range (%d..%d)", key, value, MIN_MAP_SIZE, max);
            else if (isWidth) doc->width = value; else doc->height = value;
        }
        else if (!gridDone && SameWord(key, "tiles"))
        {
            if (doc->width == 0 || doc->height == 0) ok = Fail(path, lineNo, "'tiles' must come after width and height");
            inGrid = true;
        }
        else if (!gridDone) ok = Fail(path, lineNo, "unknown header line '%s' (expected name, width, height, tiles)", key);
        else ok = ParseObject(path, lineNo, line, doc);
    }
    UnloadFileText(text);
    if (!ok) return false;

    if (!gridDone) return Fail(path, lineNo, inGrid ? "file ends after %d of %d tile rows" : "no 'tiles' section", row, doc->height);

    // Each side needs a building, or it would lose as soon as the game starts.
    for (int team = 0; team < 2; team++)
    {
        bool has = false;
        for (int i = 0; i < doc->objectCount; i++) has |= (doc->objects[i].kind == MAPOBJ_BUILDING && doc->objects[i].team == team);
        if (!has) return Fail(path, 0, "team %d (%s) has no building - each side needs at least one", team, team == PLAYER_TEAM ? "player" : "AI");
    }
    return true;
}

static Vector2 TileCentre(int x, int y)
{
    return (Vector2){ (x + 0.5f)*TILE_SIZE, (y + 0.5f)*TILE_SIZE };
}

// Set the tiles and spawn everything. MapFileParse() already checked the
// rules, so the only things left to fail are full pools.
static bool Apply(const char *path, const MapDoc *doc, MapStart *start)
{
    MapSetTiles(doc->width, doc->height, doc->tiles);
    start->aiBase = -1;
    bool haveBase[2] = { false, false };

    for (int i = 0; i < doc->objectCount; i++)
    {
        const MapObject *o = &doc->objects[i];
        if (o->kind == MAPOBJ_GOLD)
        {
            if (EconomySpawnNode(TileCentre(o->x, o->y), o->amount) == -1) return Fail(path, o->line, "too many gold nodes (max %d)", MAX_GOLD_NODES);
        }
        else if (o->kind == MAPOBJ_BUILDING)
        {
            float half = BUILDING_STATS[o->type].size*0.5f;
            Vector2 centre = { (o->x + half)*TILE_SIZE, (o->y + half)*TILE_SIZE };
            int id = BuildingPlace((BuildingType)o->type, o->team, centre, false);
            if (id == -1) return Fail(path, o->line, "too many buildings (max %d)", MAX_BUILDINGS);
            if (!haveBase[o->team]) { haveBase[o->team] = true; start->baseCentre[o->team] = BuildingCentre(id); }
            if (o->team == AI_TEAM && o->type == BUILDING_BASE && start->aiBase == -1) start->aiBase = id;
        }
        else if (UnitSpawn(TileCentre(o->x, o->y), (UnitType)o->type, o->team) == -1) return Fail(path, o->line, "too many units (max %d)", MAX_UNITS);
    }
    return true;
}

bool MapFileLoad(const char *path, MapStart *start)
{
    return MapFileParse(path, &loadDoc) && Apply(path, &loadDoc, start);
}

bool MapFileWrite(const char *path, const MapDoc *doc)
{
    errorText[0] = '\0';
    FILE *f = fopen(path, "w");
    if (!f) return Fail(path, 0, "can't write the file");

    fprintf(f, "# Made with the RTS Kit map editor. The file format is described in maps/FORMAT.md.\n\n");
    fprintf(f, "name %s\nwidth %d\nheight %d\ntiles\n", doc->name, doc->width, doc->height);
    for (int y = 0; y < doc->height; y++)
    {
        for (int x = 0; x < doc->width; x++) fputc(TILE_INFO[doc->tiles[y*doc->width + x]].fileChar, f);
        fputc('\n', f);
    }
    fputc('\n', f);
    for (int i = 0; i < doc->objectCount; i++)
    {
        const MapObject *o = &doc->objects[i];
        char word[32];
        snprintf(word, sizeof(word), "%s", ObjectName(o));
        for (char *c = word; *c; c++)   // keywords: lower case, spaces as '_'
        {
            if (*c >= 'A' && *c <= 'Z') *c += 32;
            if (*c == ' ') *c = '_';
        }
        if (o->kind == MAPOBJ_GOLD) fprintf(f, "gold %d %d %d\n", o->x, o->y, o->amount);
        else fprintf(f, "%s %d %d %d\n", word, o->team, o->x, o->y);
    }
    bool ok = (fclose(f) == 0);
    return ok ? true : Fail(path, 0, "error while writing the file");
}

// --- Map list -----------------------------------------------------------------------

// Where maps are read from and saved to:
//   - built from source (MAPS_SOURCE_DIR set by CMake, folder exists): the
//     project's own maps/ folder, so editor saves land where git sees them;
//   - otherwise the maps/ folder the build copies next to the executable;
//   - web: the folder bundled into the browser's virtual file system.
const char *MapFilesFolder(void)
{
#if defined(__EMSCRIPTEN__)
    return "/maps";
#else
  #if defined(MAPS_SOURCE_DIR)
    if (DirectoryExists(MAPS_SOURCE_DIR)) return MAPS_SOURCE_DIR;
  #endif
    const char *nextToExe = TextFormat("%smaps", GetApplicationDirectory());
    return DirectoryExists(nextToExe) ? nextToExe : "maps";
#endif
}

// The `name` line of a map file, or its file name if it has none.
static void ReadMapName(const char *path, char *out, int size)
{
    snprintf(out, size, "%s", GetFileNameWithoutExt(path));
    char *text = LoadFileText(path);
    if (!text) return;
    char *cursor = text, *line;
    while ((line = NextLine(&cursor)) != NULL)
    {
        if (strncmp(line, "name ", 5) == 0) { snprintf(out, size, "%s", line + 5); break; }
        if (strncmp(line, "tiles", 5) == 0) break;   // header is over
    }
    UnloadFileText(text);
}

static int ComparePaths(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

int MapFileScan(void)
{
    listCount = 0;
    char folder[256];
    snprintf(folder, sizeof(folder), "%s", MapFilesFolder());   // copy: TextFormat results don't last
    if (!DirectoryExists(folder)) return 0;

    FilePathList files = LoadDirectoryFilesEx(folder, ".map", false);
    for (unsigned int i = 0; i < files.count && listCount < MAX_MAP_FILES; i++)
    {
        snprintf(listPaths[listCount++], sizeof(listPaths[0]), "%s", files.paths[i]);
    }
    UnloadDirectoryFiles(files);

    qsort(listPaths, listCount, sizeof(listPaths[0]), ComparePaths);
    for (int i = 0; i < listCount; i++) ReadMapName(listPaths[i], listNames[i], sizeof(listNames[0]));
    return listCount;
}

const char *MapFileName(int index) { return listNames[index]; }
const char *MapFilePath(int index) { return listPaths[index]; }
