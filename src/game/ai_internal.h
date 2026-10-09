// ai_internal.h - What ai.c shares with ai_ferry.c (not for the rest of the game).
#ifndef AI_INTERNAL_H_INCLUDED
#define AI_INTERNAL_H_INCLUDED

#include "raylib.h"
#include "config.h"
#include <stdbool.h>

// ai.c
int     AiAnchorBase(void);                     // the base the AI organises around, or -1
int     AiFindOwn(BuildingType type);           // one of its buildings of that type (a finished one if any), or -1
int     AiFreeWorker(int site, bool *siteBeingBuilt);   // a worker free to build (-1 if none); also: is anyone building `site`?
int     AiStartSite(BuildingType type, int worker, int anchor);   // pay, place near the anchor, send the worker; site or -1
int     AiHomeRegion(void);                     // the ground region its base and army stand in
Vector2 AiPlayerBase(void);                     // where the player's base is (from AiInit)
bool    AiFindFerryField(Vector2 *spot, int *region);   // AI_FERRY_EXPANSION: a gold field in ANOTHER region and a Base spot there
void    AiFerryExpansionStarted(int site, int builder); // the ferried Worker started that Base (ai.c watches it like any expansion)
void    AiFerryExpansionFailed(void);

// ai_ferry.c
void        AiFerryReset(void);
void        AiFerryThink(int anchor);           // every AI think: needs transport? build / train what's missing
void        AiFerryTick(void);                  // every tick: the ferries' trips
bool        AiFerryNeedsTransport(void);        // no player building can be reached by ground
bool        AiFerrySaving(void);                // saving gold for an Academy / Air Factory / Airship (army training waits)
bool        AiFerryBuilding(void);              // getting the Academy / Air Factory ready (the tech order waits)
bool        AiFerryOwns(int unit);              // an Airship the ferry logic is flying (the idle-army loop leaves it alone)
bool        AiFerryExpand(int worker, Vector2 spot);     // AI_FERRY_EXPANSION: carry this Worker over to build a Base at spot
bool        AiFerryHasIdleShip(void);
const char *AiFerryStatus(void);                // "" when there's nothing to say

typedef struct AiFerryStats {
    long ticks;                     // since AiFerryReset
    long needsSince;                // tick it first needed transport (-1 = never)
    long academyDone, factoryDone;  // ticks they were finished (-1 = not yet)
    long firstAirship, firstLanding;
    int  airshipsTrained, trips, landed, aborts, lost;
    bool gaveUp;
} AiFerryStats;
const AiFerryStats *AiFerryGetStats(void);

#endif
