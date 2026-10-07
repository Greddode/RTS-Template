// config.h - Settings shared by several systems.
// Per-system settings (map size, aggro radius, ...) live in that system's header.
#ifndef CONFIG_H_INCLUDED
#define CONFIG_H_INCLUDED

#include "raylib.h"   // for the KEY_ constants below

#define SCREEN_W 1280
#define SCREEN_H 720

// The simulation advances in fixed steps of TICK_DT seconds, independent of FPS.
#define TICK_RATE 30
#define TICK_DT   (1.0f / TICK_RATE)

// Teams. Team 0 is the human player, team 1 is the computer (ai.c).
#define PLAYER_TEAM 0
#define AI_TEAM     1

// Sight: how far (in tiles) a unit or building reveals the fog of war around
// it. These are the defaults the tables below use; give a row its own number
// to change one type.
#define UNIT_SIGHT     7
#define BUILDING_SIGHT 8

// Building types come first: units say where they're trained.
// To add a building: add it to the enum and give it a row in BUILDING_STATS.
// If it has a cost and a hotkey, workers can build it (it appears in the
// inspector's Build buttons automatically).
typedef enum { BUILDING_NONE = -1, BUILDING_BASE, BUILDING_BARRACKS, BUILDING_TYPE_COUNT } BuildingType;

typedef struct BuildingStats {
    const char *name;
    float       hp;
    int         size;        // in tiles (square)
    int         cost;        // gold to build; 0 = workers can't build it
    float       buildTime;   // seconds for one worker
    int         hotkey;      // build hotkey (workers selected); 0 = none
    bool        dropOff;     // workers can bring gold here
    int         sight;       // fog of war: tiles it reveals around it
} BuildingStats;

static const BuildingStats BUILDING_STATS[BUILDING_TYPE_COUNT] = {
    //                      name        hp       size  cost  buildTime  hotkey  dropOff  sight
    [BUILDING_BASE]     = { "Base",     1500.0f, 3,    400,  40.0f,     KEY_B,  true,    BUILDING_SIGHT },
    [BUILDING_BARRACKS] = { "Barracks",  900.0f, 2,    150,  25.0f,     KEY_K,  false,   BUILDING_SIGHT },
};

// Unit types and their stats. To add a type: add it to the enum, give it a row
// in UNIT_STATS, and (optionally) a look in UnitsDrawIcon(). `trainedAt` puts
// a Train button on that building; BUILDING_NONE means it can't be trained.
typedef enum { UNIT_MELEE, UNIT_RANGED, UNIT_WORKER, UNIT_TYPE_COUNT } UnitType;

typedef struct UnitStats {
    const char  *name;
    BuildingType trainedAt;   // which building trains it
    int          hotkey;      // train hotkey (that building selected)
    float hp;         // starting / maximum health
    float damage;     // per hit
    float range;      // attack reach in world pixels (to a unit's centre, or a building's wall)
    float cooldown;   // seconds between attacks
    float speed;      // world pixels per second
    int   cost;       // gold to train
    float trainTime;  // seconds to train
    int   sight;      // fog of war: tiles it reveals around it
} UnitStats;

static const UnitStats UNIT_STATS[UNIT_TYPE_COUNT] = {
    //                 name      trainedAt          hotkey  hp      damage  range   cooldown  speed   cost  trainTime  sight
    [UNIT_MELEE]  = { "Melee",  BUILDING_BARRACKS, KEY_M,  120.0f, 12.0f,  16.0f,  0.8f,     75.0f,  75,   6.0f,      UNIT_SIGHT },
    [UNIT_RANGED] = { "Ranged", BUILDING_BARRACKS, KEY_R,   70.0f,  9.0f, 120.0f,  1.2f,     65.0f,  100,  7.0f,      UNIT_SIGHT },
    [UNIT_WORKER] = { "Worker", BUILDING_BASE,     KEY_W,   40.0f,  4.0f,  16.0f,  1.0f,     70.0f,  50,   5.0f,      UNIT_SIGHT },
};

// Auto-targeting leash: an idle unit that starts chasing an enemy on its own
// gives up once it's this far from where it was standing, and walks back.
// (Player / AI orders and attack-move aren't leashed.)
#define COMBAT_LEASH_TILES 6

// Fog of war (fog.c).
#define FOG_OF_WAR_ENABLED  1     // 0: everything always visible (also a toggle in the pause menu)
#define AI_SEES_THROUGH_FOG 1     // 1: the AI ignores fog (so it isn't crippled); 0: same rules as the player
#define FOG_UPDATE_TICKS    6     // recompute visibility every 6 sim ticks (5x per second)
#define FOG_MAX_SIGHT       16    // largest sight radius allowed in the tables (tiles)
#define FOG_EXPLORED_ALPHA  150   // darkness of explored-but-not-visible tiles (0..255); unseen is black

// Game states (main.c switches between them).
typedef enum { STATE_MENU, STATE_PLAYING, STATE_PAUSED, STATE_VICTORY, STATE_DEFEAT, STATE_EDITOR } GameState;

// Key bindings. input.c reads these names, and the Controls screen lists them
// from CONTROLS below, so changing a key here changes both.
#define KEY_ATTACK_MOVE   KEY_A
#define KEY_STOP          KEY_S
#define KEY_HOLD          KEY_H
#define KEY_PAUSE         KEY_ESCAPE
#define KEY_DEBUG_WAVE    KEY_F1
#define KEY_EDITOR        KEY_F2    // while playing: open the map editor on the current map
#define KEY_UNDO          KEY_Z     // with Ctrl, in the editor

// Every control, for the in-game Controls screen. If `key` isn't 0, "%s" in
// `input` is replaced by that key's name (so a remapped key shows correctly).
// Train and Build hotkeys aren't listed here: the Controls screen adds them
// from UNIT_STATS and BUILDING_STATS.
typedef enum { CONTROLS_MOUSE, CONTROLS_KEYBOARD, CONTROLS_CATEGORY_COUNT } ControlsCategory;

typedef struct ControlInfo {
    ControlsCategory category;
    int              key;      // a KEY_ binding above, or 0 for mouse / fixed inputs
    const char      *input;
    const char      *action;
} ControlInfo;

static const ControlInfo CONTROLS[] = {
    { CONTROLS_MOUSE,    0,                "Left click / drag",      "Select units / box select" },
    { CONTROLS_MOUSE,    0,                "Shift + select",         "Add to selection" },
    { CONTROLS_MOUSE,    0,                "Left click building",    "Select it (inspector shows queue)" },
    { CONTROLS_MOUSE,    0,                "Left click gold",        "Inspect gold left" },
    { CONTROLS_MOUSE,    0,                "Click a queue icon",     "Cancel it, refund gold" },
    { CONTROLS_MOUSE,    0,                "Right click ground",     "Move (building selected: rally point)" },
    { CONTROLS_MOUSE,    0,                "Right click enemy",      "Attack unit or building" },
    { CONTROLS_MOUSE,    0,                "Right click gold",       "Workers mine it" },
    { CONTROLS_MOUSE,    0,                "Right click unfinished", "Workers help build it" },
    { CONTROLS_MOUSE,    0,                "Placing: left / right",  "Place building / cancel" },
    { CONTROLS_MOUSE,    0,                "Middle drag",            "Pan camera" },
    { CONTROLS_MOUSE,    0,                "Mouse wheel",            "Zoom (over a panel: scroll it)" },
    { CONTROLS_KEYBOARD, 0,                "Arrow keys",             "Pan camera" },
    { CONTROLS_KEYBOARD, KEY_ATTACK_MOVE,  "%s, then right click",   "Attack-move (fight on the way)" },
    { CONTROLS_KEYBOARD, KEY_STOP,         "%s",                     "Stop: drop all orders" },
    { CONTROLS_KEYBOARD, KEY_HOLD,         "%s",                     "Hold position: never chase" },
    { CONTROLS_KEYBOARD, KEY_PAUSE,        "%s",                     "Cancel placing/attack-move, or pause" },
    { CONTROLS_KEYBOARD, KEY_DEBUG_WAVE,   "%s",                     "Debug: spawn an enemy wave" },
    { CONTROLS_KEYBOARD, KEY_EDITOR,       "%s",                     "Map editor on the current map" },
    { CONTROLS_KEYBOARD, KEY_UNDO,         "Ctrl + %s (editor)",     "Undo tile painting" },
};
#define CONTROLS_COUNT ((int)(sizeof(CONTROLS)/sizeof(CONTROLS[0])))

// --- AI tuning (ai.c) -------------------------------------------------------------
// Every number the computer opponent uses, so it can be tuned without reading ai.c.
#define AI_THINK_TICKS          (TICK_RATE*2)   // decisions every 2 s
#define AI_TRAIN_TICKS          (TICK_RATE*5)   // try to queue a combat unit every 5 s
#define AI_WAVE_SIZE            20              // F1 debug wave
#define AI_BARRACKS_WORKERS     3               // workers needed before it builds a Barracks
#define AI_WORKERS_PER_NODE     8               // worker target: this many per gold node near a base...
#define AI_MAX_WORKERS_PER_BASE 16              // ...but at most this many per base
#define AI_WORKER_QUEUE         1               // workers queued at a base at once (keeps gold free)
#define AI_NODE_RANGE_TILES     12              // a node this close to a finished drop-off belongs to that base
#define AI_EXPAND_CHECK_TICKS   (TICK_RATE*4)   // look for an expansion every 4 s
#define AI_EXPAND_MIN_TILES     15              // (a) candidate node: at least this far from all our drop-offs
#define AI_EXPAND_MIN_GOLD      800             // (b) ...with at least this much gold left
#define AI_EXPAND_ENEMY_TILES   20              // (d) ...and no enemy building this close to it
#define AI_EXPAND_RESERVE       100             // "spare gold": the base's cost plus this
#define AI_EXPAND_LOW_GOLD      2000            // gold left in our nodes below this: expand without the reserve
#define AI_SAVE_FOR_EXPANSION   1               // 1: pause combat training while saving for an expansion
#define AI_MAX_BASES            3               // cap on bases (finished or being built)
#define AI_MAX_FAILED_NODES     16              // nodes it gave up expanding to (builder died) are remembered
#define AI_BARRACKS_QUEUE       2               // combat units queued per Barracks; "full" means this many
#define AI_EXTRA_BARRACKS_GOLD  600             // more gold banked than this, every Barracks full: build another
#define AI_MAX_BARRACKS         3               // cap on Barracks (finished or being built)

#endif
