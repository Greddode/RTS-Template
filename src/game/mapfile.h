// mapfile.h - Map files (maps/*.map): read, check, place, write.
#ifndef MAPFILE_H_INCLUDED
#define MAPFILE_H_INCLUDED

#include "raylib.h"
#include "map.h"
#include <stdbool.h>

#define MAX_MAP_FILES   32
#define MAP_MAX_OBJECTS 1024
#define MAP_NAME_LEN    64

// A map as written in a file: tiles plus a list of objects. The loader reads
// files into this, and the editor edits one of these (never the live game).
typedef enum { MAPOBJ_GOLD, MAPOBJ_BUILDING, MAPOBJ_UNIT } MapObjectKind;

typedef struct MapObject {
    MapObjectKind kind;
    int           type;     // BuildingType or UnitType
    int           team;
    int           x, y;     // tile; for buildings the top-left tile
    int           amount;   // gold
    int           line;     // line in the file it came from (for error messages)
} MapObject;

typedef struct MapDoc {
    char          name[MAP_NAME_LEN];
    int           width, height;
    unsigned char tiles[MAP_W*MAP_H];   // TileType, tiles[y*width + x]
    MapObject     objects[MAP_MAX_OBJECTS];
    int           objectCount;
} MapDoc;

// Where each side starts, for the camera and the AI.
typedef struct MapStart {
    Vector2 baseCentre[2];   // centre of each team's first building
    int     aiBase;          // the AI's first Base (building slot), or -1
} MapStart;

// Read and fully check a file into `doc`. Changes nothing in the game.
// On failure MapFileError() says "file:line: what's wrong".
bool        MapFileParse(const char *path, MapDoc *doc);
// Parse, then set the tiles and spawn everything through the normal pools.
bool        MapFileLoad(const char *path, MapStart *start);
// Write `doc` in the same format (MapFileParse reads it back).
bool        MapFileWrite(const char *path, const MapDoc *doc);
const char *MapFileError(void);

// Rules every map must follow (used by the parser and the editor).
bool        MapDocObjectFits(const MapDoc *doc, const MapObject *o, int ignoreIndex);   // on open ground, not overlapping others

// The maps folder: every *.map file found is listed (sorted by file name).
const char *MapFilesFolder(void);
int         MapFileScan(void);              // returns how many were found
const char *MapFileName(int index);         // the map's `name` line (or file name)
const char *MapFilePath(int index);

#endif
