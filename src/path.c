// path.c - Pathfinding.
//
// Requests: PathRequest() puts the unit in a FIFO queue. PathUpdate() runs once
// per frame and works through the queue until PATH_BUDGET_MS is used up. A
// search that doesn't finish in time is paused and resumed next frame, so a
// big group order never causes a frame-time spike - units just start moving a
// few frames apart.
//
// Each request:
//   1. If the straight line is clear, the path is just [goal]. Most orders in
//      open ground end here, with no search at all.
//   2. Otherwise A* over tiles: 8 directions, no cutting corners past
//      water/rock. If the goal isn't reached within PATH_MAX_EXPANSIONS tiles
//      (e.g. it's on an island), the path leads to the closest tile found.
//   3. Smoothing: waypoints that can be skipped with a clear straight line are
//      dropped, so units walk natural lines instead of tile zig-zags.
//
// All memory is fixed arrays sized for the map and unit pool - nothing is
// allocated. Only one A* search runs at a time, sharing one set of scratch arrays.

#include "path.h"
#include "map.h"
#include "units.h"
#include <stdlib.h>

#define NODE_COUNT       (MAP_W * MAP_H)              // one node per tile
#define HEAP_CAP         (PATH_MAX_EXPANSIONS*8 + 1)  // each expansion pushes at most 8 neighbours
#define CHECK_TIME_EVERY 32                           // A* steps between clock checks
#define SMOOTH_LOOKAHEAD 12                           // max tiles skipped per smoothing step; keeps line checks short
#define SQRT2            1.41421356f

// --- Per-unit requests and results ---------------------------------------------
static PathStatus status[MAX_UNITS];
static Vector2    reqFrom[MAX_UNITS], reqTo[MAX_UNITS];
static Vector2    waypoints[MAX_UNITS][PATH_MAX_WAYPOINTS];
static int        waypointCount[MAX_UNITS], waypointIndex[MAX_UNITS];

// --- Request queue: ring buffer of unit ids; a unit is queued at most once ------
static int  queue[MAX_UNITS];
static int  queueHead = 0, queueCount = 0;
static bool inQueue[MAX_UNITS];

// --- A* scratch -----------------------------------------------------------------
// Instead of clearing these arrays before each search, every search gets a new
// `stamp`. A node's data only counts if its stamp matches the current search.
static float        gCost[NODE_COUNT];     // cost from start
static int          parent[NODE_COUNT];    // previous tile on the best route (-1 = start)
static unsigned int seenStamp[NODE_COUNT], closedStamp[NODE_COUNT], stamp = 0;

typedef struct { int node; float f; } HeapItem;   // f = cost so far + estimate to goal
static HeapItem heap[HEAP_CAP];                   // open list, a binary min-heap on f
static int      heapSize = 0;

static int rawPath[NODE_COUNT];   // tile path being turned into waypoints

// --- The search in progress (if any) ----------------------------------------------
static bool  searching = false;
static int   searchUnit, goalNode, bestNode, expansions;
static float bestH;
static double lastFrameMs = 0.0;

static const int DIR_X[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
static const int DIR_Y[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };

static int NodeAt(Vector2 p)
{
    int tx = (int)(p.x / TILE_SIZE), ty = (int)(p.y / TILE_SIZE);
    return ty*MAP_W + tx;
}

static Vector2 NodeCentre(int node)
{
    return (Vector2){ (node % MAP_W + 0.5f)*TILE_SIZE, (node / MAP_W + 0.5f)*TILE_SIZE };
}

// "Octile" distance: the exact cost between two tiles if nothing is in the way.
static float Heuristic(int a, int b)
{
    int dx = abs(a % MAP_W - b % MAP_W), dy = abs(a / MAP_W - b / MAP_W);
    int diag = dx < dy ? dx : dy;
    return (float)(dx + dy) + (SQRT2 - 2.0f)*diag;
}

static void HeapPush(int node, float f)
{
    int i = heapSize++;
    while (i > 0)
    {
        int up = (i - 1)/2;
        if (heap[up].f <= f) break;
        heap[i] = heap[up];
        i = up;
    }
    heap[i] = (HeapItem){ node, f };
}

static int HeapPop(void)
{
    int top = heap[0].node;
    HeapItem last = heap[--heapSize];
    int i = 0;
    for (;;)
    {
        int child = 2*i + 1;
        if (child >= heapSize) break;
        if (child + 1 < heapSize && heap[child + 1].f < heap[child].f) child++;
        if (last.f <= heap[child].f) break;
        heap[i] = heap[child];
        i = child;
    }
    heap[i] = last;
    return top;
}

static void QueuePush(int unit)
{
    queue[(queueHead + queueCount) % MAX_UNITS] = unit;
    queueCount++;
    inQueue[unit] = true;
}

static int QueuePop(void)
{
    int unit = queue[queueHead];
    queueHead = (queueHead + 1) % MAX_UNITS;
    queueCount--;
    inQueue[unit] = false;
    return unit;
}

// Take the next request off the queue: finish it at once if the straight line
// is clear, otherwise start an A* search. Returns false if the queue is empty.
static bool StartNextRequest(void)
{
    if (queueCount == 0) return false;

    int unit = QueuePop();
    if (status[unit] != PATH_PENDING) return true;   // cancelled while waiting

    if (MapLineClear(reqFrom[unit], reqTo[unit], UNIT_RADIUS))
    {
        waypoints[unit][0] = reqTo[unit];
        waypointCount[unit] = 1;
        status[unit] = PATH_READY;
        return true;
    }

    int start = NodeAt(reqFrom[unit]);
    stamp++;
    heapSize = 0;
    gCost[start] = 0.0f;
    parent[start] = -1;
    seenStamp[start] = stamp;

    searchUnit = unit;
    goalNode = NodeAt(reqTo[unit]);
    bestNode = start;
    bestH = Heuristic(start, goalNode);
    expansions = 0;
    HeapPush(start, bestH);
    searching = true;
    return true;
}

// Run up to `steps` A* steps. Returns true when the search is over: the goal
// was reached, or there's nothing left worth exploring.
static bool StepSearch(int steps)
{
    while (steps-- > 0)
    {
        if (heapSize == 0 || expansions >= PATH_MAX_EXPANSIONS) return true;

        int node = HeapPop();
        if (closedStamp[node] == stamp) continue;   // stale duplicate, already done
        closedStamp[node] = stamp;
        expansions++;

        // Remember the explored tile closest to the goal, for unreachable goals.
        float h = Heuristic(node, goalNode);
        if (h < bestH) { bestH = h; bestNode = node; }
        if (node == goalNode) return true;

        int x = node % MAP_W, y = node / MAP_W;
        for (int d = 0; d < 8; d++)
        {
            int nx = x + DIR_X[d], ny = y + DIR_Y[d];
            if (!MapTileWalkable(nx, ny)) continue;

            // Diagonal moves need both side tiles open, or units would clip corners.
            bool diagonal = DIR_X[d] != 0 && DIR_Y[d] != 0;
            if (diagonal && (!MapTileWalkable(nx, y) || !MapTileWalkable(x, ny))) continue;

            int next = ny*MAP_W + nx;
            if (closedStamp[next] == stamp) continue;

            float g = gCost[node] + (diagonal ? SQRT2 : 1.0f);
            if (seenStamp[next] == stamp && g >= gCost[next]) continue;

            seenStamp[next] = stamp;
            gCost[next] = g;
            parent[next] = node;
            HeapPush(next, g + Heuristic(next, goalNode));
        }
    }
    return false;
}

// Turn the finished search into smoothed waypoints for the unit.
static void FinishSearch(void)
{
    int unit = searchUnit;
    searching = false;

    // Walk the parent links back from the end tile: rawPath[0] = end, rawPath[len-1] = start.
    int len = 0;
    for (int n = bestNode; n != -1; n = parent[n]) rawPath[len++] = n;

    bool reachedGoal = (bestNode == goalNode);
    if (len <= 1 && !reachedGoal)
    {
        status[unit] = PATH_FAILED;   // can't get any closer than where it stands
        return;
    }

    // The last waypoint is the exact goal if we got there, else the closest tile's centre.
    Vector2 end = reachedGoal ? reqTo[unit] : NodeCentre(bestNode);

    // Smoothing: from `anchor`, look as far along the path as a straight clear
    // line reaches (up to SMOOTH_LOOKAHEAD tiles), put a waypoint there, and
    // repeat from it. The lookahead limit keeps the cost small: long line
    // checks were the most expensive part of pathfinding before it.
    Vector2 anchor = reqFrom[unit];
    int count = 0;
    int i = len - 2;   // first tile after the start tile
    while (i >= 0 && count < PATH_MAX_WAYPOINTS)
    {
        for (int ahead = 0; i > 0 && ahead < SMOOTH_LOOKAHEAD; ahead++)
        {
            Vector2 further = (i - 1 == 0) ? end : NodeCentre(rawPath[i - 1]);
            if (!MapLineClear(anchor, further, UNIT_RADIUS)) break;
            i--;
        }
        anchor = (i == 0) ? end : NodeCentre(rawPath[i]);
        waypoints[unit][count++] = anchor;
        i--;
    }
    if (count == 0) waypoints[unit][count++] = end;

    waypointCount[unit] = count;
    status[unit] = PATH_READY;
}

void PathRequest(int unit, Vector2 from, Vector2 to)
{
    if (searching && searchUnit == unit) searching = false;   // drop the old search

    reqFrom[unit] = from;
    reqTo[unit] = to;
    status[unit] = PATH_PENDING;
    waypointCount[unit] = 0;
    waypointIndex[unit] = 0;
    if (!inQueue[unit]) QueuePush(unit);
}

void PathCancel(int unit)
{
    if (searching && searchUnit == unit) searching = false;
    status[unit] = PATH_NONE;   // if still queued, it's skipped when it comes up
}

void PathUpdate(void)
{
    double start = GetTime();
    double deadline = start + PATH_BUDGET_MS/1000.0;

    while (GetTime() < deadline)
    {
        if (!searching && !StartNextRequest()) break;   // nothing left to do
        if (searching && StepSearch(CHECK_TIME_EVERY)) FinishSearch();
    }

    lastFrameMs = (GetTime() - start)*1000.0;
}

PathStatus PathGetStatus(int unit)
{
    return status[unit];
}

bool PathCurrentWaypoint(int unit, Vector2 *out)
{
    if (status[unit] != PATH_READY || waypointIndex[unit] >= waypointCount[unit]) return false;
    *out = waypoints[unit][waypointIndex[unit]];
    return true;
}

void PathAdvance(int unit)
{
    waypointIndex[unit]++;
}

int PathQueueLength(void)
{
    return queueCount;
}

double PathLastFrameMs(void)
{
    return lastFrameMs;
}
