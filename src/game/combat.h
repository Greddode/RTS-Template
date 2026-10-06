// combat.h - Attacking, auto-targeting and projectiles.
#ifndef COMBAT_H_INCLUDED
#define COMBAT_H_INCLUDED

#include "raylib.h"

#define COMBAT_AGGRO_RADIUS   160.0f   // idle units attack enemies this close (world px)
#define COMBAT_ACQUIRE_TICKS  3        // idle units look for enemies every N ticks (staggered per unit)
#define MAX_PROJECTILES       1024

// Called by UnitsTick() for each unit.
Vector2 CombatUnitTick(int id);   // attacking unit: chase / hit; returns its movement step
void    CombatAcquireTick(int id); // idle or attack-moving unit: attack a nearby enemy if there is one

void CombatProjectilesTick(void);  // move projectiles, apply hits; once per sim tick
void CombatProjectilesDraw(Rectangle view, float alpha);
int  CombatProjectileCount(void);
void CombatReset(void);   // remove every projectile (new game)

#endif
