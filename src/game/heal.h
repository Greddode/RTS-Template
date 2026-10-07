// heal.h - Healers (UNIT_STATS canHeal): heal damaged allies instead of attacking.
#ifndef HEAL_H_INCLUDED
#define HEAL_H_INCLUDED

#include "raylib.h"
#include <stdbool.h>

#define HEAL_SEARCH_RADIUS 160.0f   // idle / attack-moving healers look this far for damaged allies (world px)

bool UnitNeedsHealing(int id);   // THE rule for "damaged": alive and below its max HP

// Called by UnitsTick().
void    HealBeginTick(void);      // once per sim tick, before the units update
void    HealAcquireTick(int id);  // idle or attack-moving healer: pick the nearest damaged ally
Vector2 HealUnitTick(int id);     // healer with a target: walk into range and heal; returns its step

void HealOrderFollow(const int *ids, int count, int target);   // right click: follow and heal this ally (healers only)
void HealDraw(Rectangle view, float alpha);                    // green heal lines, one batched pass

#endif
