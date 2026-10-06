// economy.h - Gold, gold nodes and workers mining them.
#ifndef ECONOMY_H_INCLUDED
#define ECONOMY_H_INCLUDED

#include "raylib.h"
#include <stdbool.h>

#define MAX_GOLD_NODES   64
#define GOLD_NODE_AMOUNT 1500   // gold in a fresh node
#define START_GOLD       200

typedef struct GoldNode {
    bool         active;
    unsigned int serial;   // unique per node, so workers notice a depleted one
    Vector2      pos;
    int          amount;   // gold left
} GoldNode;

extern GoldNode goldNodes[MAX_GOLD_NODES];

void EconomyInit(void);   // starting gold, no nodes (new game)
int  EconomyGold(int team);
bool EconomySpend(int team, int amount);   // false (and nothing spent) if the team can't afford it
void EconomyAdd(int team, int amount);     // income or refunds

int  EconomySpawnNode(Vector2 pos, int amount);
bool EconomyNodeIsAlive(int id, unsigned int serial);
int  EconomyNodeAt(Vector2 p);                        // node under a point, or -1
int  EconomyNearestNode(Vector2 pos, float maxDist);  // or -1

void    EconomyOrderGather(const int *ids, int count, int node);   // workers only; others are skipped
Vector2 EconomyWorkerTick(int id);                                  // a gathering worker's step this tick

void EconomyDrawNodes(Rectangle view);
void EconomyDrawNode(Vector2 pos, int amount);   // one node's look (also used by the editor)
void EconomyDrawHud(int team);   // gold counter, top-right of the screen

#endif
