// sprites.c - Swappable art: PNGs stitched into one atlas texture at startup.
//
// Drop a PNG into assets/sprites/units, buildings or tiles, named after a type
// in UNIT_STATS, BUILDING_STATS or TILE_INFO (worker.png, barracks.png,
// water.png: lower case, spaces become '_'). At startup SpritesLoad():
//   1. scans the three folders and loads every PNG whose name matches a type
//      (other names get a warning in the log and are ignored);
//   2. packs them all into ONE texture, the atlas (shelf packer, below);
//   3. remembers each type's rectangle inside the atlas.
// Why one texture: raylib collects everything drawn with the same texture
// into one batch and sends it to the GPU in a single draw call. Each texture
// switch ends the batch, so separate textures per type would cost one draw
// call per switch instead of one for all units.
//
// A type with no art (no PNG, unreadable file, or no room left in the atlas)
// keeps its coloured shape, and the log says which and why. With no PNGs at
// all the game looks exactly as it did before sprites existed.

#include "sprites.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define ATLAS_MIN   256   // first atlas size tried; doubled until everything fits
#define PADDING     1     // border around each sprite, see CopyWithBorder()
#define MAX_SPRITES (UNIT_TYPE_COUNT + BUILDING_TYPE_COUNT + TILE_COUNT)   // at most one per type

typedef enum { KIND_UNIT, KIND_BUILDING, KIND_TILE, KIND_COUNT } SpriteKind;
static const char *KIND_FOLDER[KIND_COUNT] = { "units", "buildings", "tiles" };
static const char *KIND_TABLE[KIND_COUNT]  = { "UNIT_STATS", "BUILDING_STATS", "TILE_INFO" };
static const int   KIND_TYPES[KIND_COUNT]  = { UNIT_TYPE_COUNT, BUILDING_TYPE_COUNT, TILE_COUNT };

// Each type's rectangle in the atlas; width 0 = no art for that type.
static Rectangle unitRect[UNIT_TYPE_COUNT];
static Rectangle buildingRect[BUILDING_TYPE_COUNT];
static Rectangle tileRect[TILE_COUNT];
static Rectangle *const kindRects[KIND_COUNT] = { unitRect, buildingRect, tileRect };

static Texture2D atlas;   // id 0 = no atlas
static bool fileFound[KIND_COUNT][MAX_SPRITES];   // a PNG with this type's name exists (loaded or not)

// A PNG found at startup, waiting to be packed.
typedef struct Pending {
    SpriteKind kind;
    int        type;
    Image      image;
    char       file[64];   // "units/worker.png", for log lines
} Pending;

static const char *TypeName(SpriteKind kind, int type)
{
    if (kind == KIND_UNIT) return UNIT_STATS[type].name;
    if (kind == KIND_BUILDING) return BUILDING_STATS[type].name;
    return TILE_INFO[type].name;
}

// Does a file name (without .png) match a type name? Case doesn't matter and
// a space in the type name is written '_' in the file name.
static bool NameMatches(const char *file, const char *typeName)
{
    for (;; file++, typeName++)
    {
        char a = *file, b = *typeName;
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
        if (b == ' ') b = '_';
        if (a != b) return false;
        if (a == '\0') return true;
    }
}

// The file name a type's art must have, without ".png": "Gold Mine" -> "gold_mine".
static const char *FileNameFor(const char *typeName)
{
    static char out[64];
    int n = 0;
    for (; typeName[n] != '\0' && n < (int)sizeof(out) - 1; n++)
    {
        char c = typeName[n];
        out[n] = (c == ' ') ? '_' : (c >= 'A' && c <= 'Z') ? (char)(c + 'a' - 'A') : c;
    }
    out[n] = '\0';
    return out;
}

// Where the art is read from (same rules as maps, see MapFilesFolder()):
// the project's own assets/sprites when built from source, so new PNGs show
// up on the next start without rebuilding; otherwise the copy next to the
// executable; on the web, the folder bundled into the page.
static void SpritesFolder(char *out, int size)
{
#if defined(__EMSCRIPTEN__)
    snprintf(out, size, "/assets/sprites");
#else
  #if defined(SPRITES_SOURCE_DIR)
    if (DirectoryExists(SPRITES_SOURCE_DIR)) { snprintf(out, size, "%s", SPRITES_SOURCE_DIR); return; }
  #endif
    snprintf(out, size, "%sassets/sprites", GetApplicationDirectory());
#endif
}

// Load every PNG in one folder that names a type of this kind. Returns how
// many were added to `pending`.
static int ScanFolder(const char *root, SpriteKind kind, Pending *pending, int count)
{
    char folder[600];   // root (512) + "/buildings"
    snprintf(folder, sizeof(folder), "%s/%s", root, KIND_FOLDER[kind]);
    if (!DirectoryExists(folder)) return 0;

    int added = 0;
    FilePathList files = LoadDirectoryFilesEx(folder, ".png", false);
    for (unsigned int f = 0; f < files.count; f++)
    {
        char label[64];
        snprintf(label, sizeof(label), "%s/%s", KIND_FOLDER[kind], GetFileName(files.paths[f]));
        const char *name = GetFileNameWithoutExt(files.paths[f]);

        int type = -1;
        for (int t = 0; t < KIND_TYPES[kind] && type < 0; t++)
            if (NameMatches(name, TypeName(kind, t))) type = t;
        if (type < 0)
        {
            TraceLog(LOG_WARNING, "SPRITES: %s matches no name in %s - ignored", label, KIND_TABLE[kind]);
            continue;
        }

        if (fileFound[kind][type])
        {
            TraceLog(LOG_WARNING, "SPRITES: %s is a second file for %s - ignored", label, TypeName(kind, type));
            continue;
        }
        fileFound[kind][type] = true;

        Image image = LoadImage(files.paths[f]);
        if (image.data == NULL)
        {
            TraceLog(LOG_WARNING, "SPRITES: %s could not be read - %s is drawn as a shape", label, TypeName(kind, type));
            continue;
        }
        ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);   // one pixel = one Color, for copying

        Pending *p = &pending[count + added++];
        p->kind = kind;
        p->type = type;
        p->image = image;
        snprintf(p->file, sizeof(p->file), "%s", label);
    }
    UnloadDirectoryFiles(files);
    return added;
}

// Shelf packer. Sort the boxes tallest first, then fill rows ("shelves") left
// to right; when the next box doesn't fit in the row, start a new row below,
// as tall as its first (= tallest) box. Fast, simple, and good enough
// when the sizes are similar, as sprites usually are.
// w/h: box sizes (padding included). Writes each box's top-left to x/y, or
// x = -1 if it didn't fit. Returns how many fitted.
static int ShelfPack(int count, const int *w, const int *h, int atlasSize, int *x, int *y)
{
    int order[MAX_SPRITES];

    // Insertion sort by height (then width), tallest first. Stable, and plenty
    // fast for a few hundred boxes.
    for (int i = 0; i < count; i++)
    {
        int j = i;
        while (j > 0 && (h[order[j - 1]] < h[i] || (h[order[j - 1]] == h[i] && w[order[j - 1]] < w[i])))
        {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = i;
    }

    int penX = 0, penY = 0, shelfH = 0, placed = 0;
    for (int k = 0; k < count; k++)
    {
        int i = order[k];
        x[i] = -1;
        if (w[i] > atlasSize || h[i] > atlasSize) continue;          // never fits
        if (penX + w[i] > atlasSize) { penY += shelfH; penX = 0; shelfH = 0; }   // row full: new row
        if (penY + h[i] > atlasSize) continue;                       // no room below either
        x[i] = penX;
        y[i] = penY;
        penX += w[i];
        if (h[i] > shelfH) shelfH = h[i];
        placed++;
    }
    return placed;
}

// Copy a sprite into the atlas at (px, py) with a PADDING border that repeats
// its edge pixels outward. When a sprite is scaled, the GPU blends each pixel
// with its neighbours; at the sprite's edge the neighbour is then a copy of
// the edge, not the next sprite in the atlas, so no seams or stray colours.
static void CopyWithBorder(Image *dst, const Image *src, int px, int py)
{
    Color *out = (Color *)dst->data;
    const Color *in = (const Color *)src->data;
    for (int y = -PADDING; y < src->height + PADDING; y++)
    {
        int sy = (y < 0) ? 0 : (y >= src->height) ? src->height - 1 : y;
        for (int x = -PADDING; x < src->width + PADDING; x++)
        {
            int sx = (x < 0) ? 0 : (x >= src->width) ? src->width - 1 : x;
            out[(py + PADDING + y)*dst->width + (px + PADDING + x)] = in[sy*src->width + sx];
        }
    }
}

void SpritesLoad(void)
{
    double start = GetTime();
    char root[512];
    SpritesFolder(root, sizeof(root));

    static Pending pending[MAX_SPRITES];
    memset(fileFound, 0, sizeof(fileFound));
    int count = 0;
    for (int kind = 0; kind < KIND_COUNT; kind++) count += ScanFolder(root, (SpriteKind)kind, pending, count);

    // Pack into the smallest square atlas that holds everything (a smaller
    // texture uses less memory, which matters most in the browser).
    static int w[MAX_SPRITES], h[MAX_SPRITES], x[MAX_SPRITES], y[MAX_SPRITES];
    for (int i = 0; i < count; i++)
    {
        w[i] = pending[i].image.width + 2*PADDING;
        h[i] = pending[i].image.height + 2*PADDING;
    }
    int size = ATLAS_MIN, placed = 0;
    for (;;)
    {
        placed = ShelfPack(count, w, h, size, x, y);
        if (placed == count || size >= SPRITES_ATLAS_MAX) break;
        size *= 2;
    }

    if (placed > 0)
    {
        Image image = GenImageColor(size, size, BLANK);
        for (int i = 0; i < count; i++)
        {
            Pending *p = &pending[i];
            if (x[i] < 0)
            {
                TraceLog(LOG_WARNING, "SPRITES: %s (%dx%d) doesn't fit in the %dx%d atlas - %s is drawn as a shape",
                         p->file, p->image.width, p->image.height, SPRITES_ATLAS_MAX, SPRITES_ATLAS_MAX, TypeName(p->kind, p->type));
                continue;
            }
            CopyWithBorder(&image, &p->image, x[i], y[i]);
            kindRects[p->kind][p->type] = (Rectangle){ (float)(x[i] + PADDING), (float)(y[i] + PADDING),
                                                       (float)p->image.width, (float)p->image.height };
        }
        atlas = LoadTextureFromImage(image);
        SetTextureFilter(atlas, SPRITES_FILTER);
        UnloadImage(image);
    }
    for (int i = 0; i < count; i++) UnloadImage(pending[i].image);

    // One line per type left without art, so a missing or misnamed file is easy to spot.
    for (int kind = 0; kind < KIND_COUNT; kind++)
        for (int t = 0; t < KIND_TYPES[kind]; t++)
            if (!fileFound[kind][t])
                TraceLog(LOG_INFO, "SPRITES: no %s/%s.png - %s is drawn as a shape", KIND_FOLDER[kind],
                         FileNameFor(TypeName((SpriteKind)kind, t)), TypeName((SpriteKind)kind, t));

    TraceLog(LOG_INFO, "SPRITES: %d of %d PNGs packed into a %dx%d atlas in %.1f ms (from %s)",
             placed, count, placed > 0 ? size : 0, placed > 0 ? size : 0, (GetTime() - start)*1000.0, root);
}

void SpritesUnload(void)
{
    if (atlas.id != 0) UnloadTexture(atlas);
    atlas = (Texture2D){ 0 };
    memset(unitRect, 0, sizeof(unitRect));
    memset(buildingRect, 0, sizeof(buildingRect));
    memset(tileRect, 0, sizeof(tileRect));
}

bool SpritesHaveUnit(UnitType type)         { return unitRect[type].width > 0; }
bool SpritesHaveBuilding(BuildingType type) { return buildingRect[type].width > 0; }
bool SpritesHaveTile(TileType type)         { return tileRect[type].width > 0; }

// Scale `src` to fit inside `box` (keeping its aspect ratio), centred.
static Rectangle FitInside(Rectangle src, Rectangle box)
{
    float scale = fminf(box.width/src.width, box.height/src.height);
    float w = src.width*scale, h = src.height*scale;
    return (Rectangle){ box.x + (box.width - w)*0.5f, box.y + (box.height - h)*0.5f, w, h };
}

void SpritesDrawUnit(UnitType type, Vector2 centre, float radius, bool flipX, Color tint)
{
    Rectangle src = unitRect[type];
    Rectangle dest = FitInside(src, (Rectangle){ centre.x - radius, centre.y - radius, radius*2.0f, radius*2.0f });
    if (flipX) src.width = -src.width;   // a negative source width mirrors the picture
    DrawTexturePro(atlas, src, dest, (Vector2){ 0 }, 0.0f, tint);
}

void SpritesDrawBuilding(BuildingType type, Rectangle dest, Color tint)
{
    Rectangle src = buildingRect[type];
    DrawTexturePro(atlas, src, FitInside(src, dest), (Vector2){ 0 }, 0.0f, tint);
}

void SpritesDrawTile(TileType type, int tx, int ty)
{
    Rectangle dest = { (float)(tx*TILE_SIZE), (float)(ty*TILE_SIZE), (float)TILE_SIZE, (float)TILE_SIZE };
    DrawTexturePro(atlas, tileRect[type], dest, (Vector2){ 0 }, 0.0f, WHITE);
}
