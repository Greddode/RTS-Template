// path.h - Pathfinding: A* on the tile map, spread across frames with a time budget.
#ifndef PATH_H_INCLUDED
#define PATH_H_INCLUDED

#include "raylib.h"
#include <stdbool.h>

#define PATH_BUDGET_MS      1.0    // max pathfinding time per frame
#define PATH_MAX_WAYPOINTS  32     // stored per unit, after smoothing
#define PATH_MAX_EXPANSIONS 6000   // tiles one search may explore before settling for the closest tile found

typedef enum { PATH_NONE, PATH_PENDING, PATH_READY, PATH_FAILED } PathStatus;

void       PathRequest(int unit, Vector2 from, Vector2 to);  // queue a search (replaces any older one for this unit)
void       PathCancel(int unit);
void       PathUpdate(void);                                 // call once per frame: works the queue within budget

PathStatus PathGetStatus(int unit);
bool       PathCurrentWaypoint(int unit, Vector2 *out);      // false once the last waypoint is passed
void       PathAdvance(int unit);                            // current waypoint reached, move to the next

int        PathQueueLength(void);
double     PathLastFrameMs(void);                            // time PathUpdate() used last frame
void       PathReset(void);                                  // drop every request and path (new game)

#endif
