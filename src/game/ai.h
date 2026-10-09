// ai.h - Computer opponent (AI_TEAM).
#ifndef AI_H_INCLUDED
#define AI_H_INCLUDED

#include "raylib.h"
#include "config.h"

// All AI numbers (think rate, worker targets, expansion rules...) are in config.h.

void        AiInit(Vector2 playerBase, Vector2 aiSpawn, int aiBaseBuilding);
void        AiTick(void);              // once per sim tick
void        AiSpawnWave(int count);    // debug: drop `count` enemy units at the AI spawn point
void        AiSpawnArmies(int perSide); // debug: `perSide` mixed units (DEBUG_ARMY_MIX) around each side's base
const char *AiDebugLine(void);         // gold, workers, bases (for the debug overlay)
const char *AiStatus(void);            // what it's currently trying to do

#endif
