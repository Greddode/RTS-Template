// ai.h - Computer opponent (AI_TEAM).
#ifndef AI_H_INCLUDED
#define AI_H_INCLUDED

#include "raylib.h"
#include "config.h"

#define AI_THINK_TICKS (TICK_RATE*2)   // the AI makes decisions every 2 seconds
#define AI_TRAIN_TICKS (TICK_RATE*5)   // and tries to queue a combat unit every 5 seconds
#define AI_WAVE_SIZE   20

void AiInit(Vector2 playerBase, Vector2 aiSpawn, int aiBaseBuilding);
void AiTick(void);              // once per sim tick
void AiSpawnWave(int count);    // debug: drop `count` enemy units at the AI spawn point

#endif
