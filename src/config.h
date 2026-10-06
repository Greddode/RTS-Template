// config.h - Settings shared by several systems.
// Per-system settings (map size, aggro radius, ...) live in that system's header.
#ifndef CONFIG_H_INCLUDED
#define CONFIG_H_INCLUDED

#define SCREEN_W 1280
#define SCREEN_H 720

// The simulation advances in fixed steps of TICK_DT seconds, independent of FPS.
#define TICK_RATE 30
#define TICK_DT   (1.0f / TICK_RATE)

// Teams. Team 0 is the human player, team 1 is the computer (ai.c).
#define PLAYER_TEAM 0
#define AI_TEAM     1

// Unit types and their stats. To add a type: add it to the enum, give it a row
// in UNIT_STATS, and (optionally) a look in UnitsDraw().
typedef enum { UNIT_MELEE, UNIT_RANGED, UNIT_TYPE_COUNT } UnitType;

typedef struct UnitStats {
    float hp;         // starting / maximum health
    float damage;     // per hit
    float range;      // attack reach, centre to centre, in world pixels
    float cooldown;   // seconds between attacks
    float speed;      // world pixels per second
} UnitStats;

static const UnitStats UNIT_STATS[UNIT_TYPE_COUNT] = {
    //                 hp      damage  range   cooldown  speed
    [UNIT_MELEE]  = { 120.0f, 12.0f,  16.0f,  0.8f,     75.0f },
    [UNIT_RANGED] = {  70.0f,  9.0f, 120.0f,  1.2f,     65.0f },
};

#endif
