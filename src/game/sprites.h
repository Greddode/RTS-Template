// sprites.h - Optional PNG art for units, buildings and tiles, packed into one atlas.
#ifndef SPRITES_H_INCLUDED
#define SPRITES_H_INCLUDED

#include "raylib.h"
#include "config.h"
#include "map.h"
#include <stdbool.h>

#define SPRITES_ATLAS_MAX 2048                     // atlas is at most this many pixels square
#define SPRITES_FILTER    TEXTURE_FILTER_BILINEAR  // TEXTURE_FILTER_POINT for crisp pixel art

void SpritesLoad(void);     // scan assets/sprites and build the atlas (after InitWindow)
void SpritesUnload(void);   // before CloseWindow

// Has this type got art? If not, draw its usual coloured shape instead.
bool SpritesHaveUnit(UnitType type);
bool SpritesHaveBuilding(BuildingType type);
bool SpritesHaveTile(TileType type);

// Draw a type's art. Units fit inside a circle's square, buildings inside
// their footprint (both keep the PNG's aspect ratio); tiles fill their tile.
// The tint multiplies the art: white parts take the tint's colour.
void SpritesDrawUnit(UnitType type, Vector2 centre, float radius, bool flipX, Color tint);
void SpritesDrawBuilding(BuildingType type, Rectangle dest, Color tint);
void SpritesDrawTile(TileType type, int tx, int ty);

#endif
