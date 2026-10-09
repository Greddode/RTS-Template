// combat.h - Attacking, auto-targeting and projectiles.
#ifndef COMBAT_H_INCLUDED
#define COMBAT_H_INCLUDED

#include "raylib.h"
#include "config.h"

#define COMBAT_AGGRO_RADIUS   160.0f   // idle units attack enemies this close (world px)
#define COMBAT_ACQUIRE_TICKS  3        // idle units look for enemies every N ticks (staggered per unit)
#define MAX_PROJECTILES       1024

// Called by UnitsTick() for each unit.
Vector2 CombatUnitTick(int id);   // attacking unit: chase / hit; returns its movement step
void    CombatAcquireTick(int id); // idle or attack-moving unit: attack a nearby enemy if there is one
void    CombatBuildingTick(int id); // a finished tower (BUILDING_STATS damage > 0): fire at the nearest enemy in range
// Can this unit fight that target: in range from where it stands, or able to get in range
// (PathCanReach)? Combat, auto-targeting and the AI all ask this before chasing.
bool    CombatCanEngage(int unit, bool isBuilding, int target);

// Damage one hit does after armor (formula and table in config.h).
float CombatDamage(float base, DamageType type, ArmorType armorType, float armor);

void CombatProjectilesTick(void);  // move projectiles, apply hits; once per sim tick
void CombatProjectilesDraw(Rectangle view, float alpha);
int  CombatProjectileCount(void);
void CombatReset(void);   // remove every projectile (new game)

#endif
