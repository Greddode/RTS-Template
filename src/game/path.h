// path.h - Pathfinding: A* on the tile map, spread across frames with a time budget.
#ifndef PATH_H_INCLUDED
#define PATH_H_INCLUDED

#include "raylib.h"
#include "config.h"   // MoveClass
#include <stdbool.h>

#define PATH_BUDGET_MS      1.0    // max pathfinding time per frame
#define PATH_MAX_WAYPOINTS  32     // stored per unit, after smoothing
#define PATH_MAX_EXPANSIONS 6000   // tiles one search may explore before settling for the closest tile found

typedef enum { PATH_NONE, PATH_PENDING, PATH_READY, PATH_FAILED } PathStatus;

void       PathRequest(int unit, MoveClass moveClass, Vector2 from, Vector2 to);  // queue a search (replaces any older one); AIR: answered at once, straight

void       PathCancel(int unit);
void       PathUpdate(void);                                 // call once per frame: works the queue within budget

PathStatus PathGetStatus(int unit);
bool       PathCurrentWaypoint(int unit, Vector2 *out);      // false once the last waypoint is passed
void       PathAdvance(int unit);                            // current waypoint reached, move to the next

int        PathQueueLength(void);
double     PathLastFrameMs(void);                            // time PathUpdate() used last frame
void       PathReset(void);                                  // drop every request and path (new game)

// Connected areas, per movement class: tiles a unit of that class can move
// between share a region number (0 = it can't be there). Same region means
// pathfinding can get there. Rebuild after the map or buildings change: a
// flood fill over every tile (fast) for each class a unit type uses; a class
// no unit uses is filled only when PathRegion() asks about it.
void       PathComputeRegions(void);
int        PathRegion(MoveClass moveClass, Vector2 worldPos);

// The reach rule (combat, healing, the AI and its ferry all use it): can a unit
// of this class standing at `from` get to a spot where `target` (a unit's
// position as a 0-size rectangle, or a building's footprint) is within
// `range`? AIR: always. Otherwise some tile of its own region must come that
// close, so a Melee never chases a boat out at sea and a boat never chases
// a unit inland. Being in range already isn't checked here (callers do that:
// a unit may always hit what it can hit from where it stands).
bool       PathCanReach(MoveClass moveClass, Vector2 from, Rectangle target, float range);
// The tile centre of `region` nearest `near` (searching outward); false if none within PATH_NEAREST_MAX_TILES.
bool       PathNearestInRegion(MoveClass moveClass, int region, Vector2 near, Vector2 *out);
#define PATH_NEAREST_MAX_TILES 64

#endif
