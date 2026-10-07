// config.h - Settings shared by several systems.
// Per-system settings (map size, aggro radius, ...) live in that system's header.
#ifndef CONFIG_H_INCLUDED
#define CONFIG_H_INCLUDED

#include "raylib.h"   // for the KEY_ constants below

// Shown on the main menu. Bump it for each release (major.minor.patch).
#define GAME_VERSION "1.0.0"

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
// inspector's Build buttons automatically). `requires`: workers can only start
// one while the team owns a FINISHED building of that type (BuildingsCanBuild()).
// Map files and the editor can place anything.
typedef enum { BUILDING_NONE = -1, BUILDING_BASE, BUILDING_BARRACKS, BUILDING_ARCHERY_RANGE, BUILDING_ACADEMY, BUILDING_TYPE_COUNT } BuildingType;

typedef struct BuildingStats {
    const char *name;
    float       hp;
    int         size;        // in tiles (square)
    int         cost;        // gold to build; 0 = workers can't build it
    float       buildTime;   // seconds for one worker
    int         hotkey;      // build hotkey (workers selected); 0 = none
    bool        dropOff;     // workers can bring gold here
    int         sight;       // fog of war: tiles it reveals around it
    BuildingType requires;   // must own a finished one of these first; BUILDING_NONE = nothing
} BuildingStats;

static const BuildingStats BUILDING_STATS[BUILDING_TYPE_COUNT] = {
    //                           name             hp       size  cost  buildTime  hotkey  dropOff  sight           requires
    [BUILDING_BASE]          = { "Base",          1500.0f, 3,    400,  40.0f,     KEY_B,  true,    BUILDING_SIGHT, BUILDING_NONE     },
    [BUILDING_BARRACKS]      = { "Barracks",       900.0f, 2,    150,  25.0f,     KEY_K,  false,   BUILDING_SIGHT, BUILDING_NONE     },
    [BUILDING_ARCHERY_RANGE] = { "Archery Range",  800.0f, 2,    175,  25.0f,     KEY_R,  false,   BUILDING_SIGHT, BUILDING_NONE     },
    [BUILDING_ACADEMY]       = { "Academy",       1000.0f, 2,    450,  35.0f,     KEY_E,  false,   BUILDING_SIGHT, BUILDING_BARRACKS },
};

// --- Damage and armor ---------------------------------------------------------------
// Every unit deals one damage type and wears one armor type. A hit does
//     damage = max(base * DAMAGE_MIN_FRACTION, base * DAMAGE_VS_ARMOR[type][armorType] - armor)
// (CombatDamage() in combat.c). Hits on buildings do the plain base damage.
// To add a type: add it to its enum (before the _COUNT), give it a name below,
// and a row (damage type) or a column (armor type) in DAMAGE_VS_ARMOR.
typedef enum { DAMAGE_PIERCE, DAMAGE_BLUNT, DAMAGE_MAGIC, DAMAGE_TYPE_COUNT } DamageType;
typedef enum { ARMOR_LIGHT, ARMOR_MEDIUM, ARMOR_HEAVY, ARMOR_TYPE_COUNT } ArmorType;

static const char *const DAMAGE_TYPE_NAMES[DAMAGE_TYPE_COUNT] = { "Pierce", "Blunt", "Magic" };
static const char *const ARMOR_TYPE_NAMES[ARMOR_TYPE_COUNT]   = { "Light", "Medium", "Heavy" };

// Damage multiplier: row = attacker's damage type, column = target's armor type.
static const float DAMAGE_VS_ARMOR[DAMAGE_TYPE_COUNT][ARMOR_TYPE_COUNT] = {
    //                 LIGHT  MEDIUM  HEAVY
    [DAMAGE_PIERCE] = { 1.25f, 1.0f,   0.5f },   // arrows: good vs light, bounce off plate
    [DAMAGE_BLUNT]  = { 1.0f,  1.0f,   1.5f },   // maces: crush heavy armor
    [DAMAGE_MAGIC]  = { 1.0f,  1.0f,   1.5f },   // spells: armor type doesn't help, plate makes it worse
};

// Armor never blocks everything: a hit always does at least this fraction of its base damage.
#define DAMAGE_MIN_FRACTION 0.1f

// Unit types and their stats. To add a type: add it to the enum, give it a row
// in UNIT_STATS, and (optionally) art in assets/sprites/units or a look in
// UnitsDrawIcon(). `trainedAt` puts a Train button on that building;
// BUILDING_NONE means it can't be trained. Columns a unit doesn't use stay 0
// (healing, splash, minRange): 0 means "off".
typedef enum { UNIT_MELEE, UNIT_ARCHER, UNIT_WORKER, UNIT_KNIGHT, UNIT_MEDIC, UNIT_MAGE, UNIT_SCOUT, UNIT_TYPE_COUNT } UnitType;

typedef struct UnitStats {
    const char  *name;
    BuildingType trainedAt;   // which building trains it
    int          hotkey;      // train hotkey (that building selected)
    float hp;         // starting / maximum health
    float damage;     // per hit, before armor
    DamageType damageType;
    float range;      // attack reach in world pixels (to a unit's centre, or a building's wall)
    float cooldown;   // seconds between attacks
    float speed;      // world pixels per second
    float armor;      // flat: taken off every hit (after the DAMAGE_VS_ARMOR multiplier)
    ArmorType armorType;
    int   cost;       // gold to train
    float trainTime;  // seconds to train
    int   sight;      // fog of war: tiles it reveals around it
    bool  canHeal;    // healer (heal.c): heals damaged allies instead of attacking (give it damage 0)
    float healRate;   // HP per second it gives its target
    float healRange;  // world pixels from its centre to the target's centre
    float splashRadius;   // > 0: fires a bolt that hits EVERY unit and building this close to where it lands (friends too)
    float splashFalloff;  // damage at the edge of the splash, as a fraction of the centre's (1 = same everywhere)
    float minRange;       // won't fire at targets closer than this; backs off or picks another (0 = none)
} UnitStats;

static const UnitStats UNIT_STATS[UNIT_TYPE_COUNT] = {
    //                 name      trainedAt               hotkey  hp      damage  damageType     range   cooldown  speed  armor  armorType     cost  trainTime  sight       canHeal  healRate  healRange  splash  falloff  minRange
    [UNIT_MELEE]  = { "Melee",  BUILDING_BARRACKS,      KEY_M,  120.0f, 12.0f,  DAMAGE_BLUNT,   16.0f, 0.8f,     75.0f, 1.0f,  ARMOR_MEDIUM, 75,   6.0f,      UNIT_SIGHT, false,   0.0f,     0.0f,      0.0f,   0.0f,    0.0f  },
    [UNIT_ARCHER] = { "Archer", BUILDING_ARCHERY_RANGE, KEY_C,   70.0f,  9.0f,  DAMAGE_PIERCE, 120.0f, 1.2f,     65.0f, 0.0f,  ARMOR_LIGHT,  100,  7.0f,      UNIT_SIGHT, false,   0.0f,     0.0f,      0.0f,   0.0f,    0.0f  },
    [UNIT_WORKER] = { "Worker", BUILDING_BASE,          KEY_W,   40.0f,  4.0f,  DAMAGE_BLUNT,   16.0f, 1.0f,     70.0f, 0.0f,  ARMOR_LIGHT,  50,   5.0f,      UNIT_SIGHT, false,   0.0f,     0.0f,      0.0f,   0.0f,    0.0f  },
    [UNIT_KNIGHT] = { "Knight", BUILDING_BARRACKS,      KEY_N,  300.0f, 18.0f,  DAMAGE_BLUNT,   16.0f, 1.0f,     55.0f, 2.0f,  ARMOR_HEAVY,  175,  10.0f,     UNIT_SIGHT, false,   0.0f,     0.0f,      0.0f,   0.0f,    0.0f  },
    [UNIT_MEDIC]  = { "Medic",  BUILDING_ACADEMY,       KEY_D,   60.0f,  0.0f,  DAMAGE_PIERCE,   0.0f, 0.0f,     70.0f, 0.0f,  ARMOR_LIGHT,  125,  8.0f,      UNIT_SIGHT, true,    8.0f,     64.0f,     0.0f,   0.0f,    0.0f  },
    [UNIT_MAGE]   = { "Mage",   BUILDING_ACADEMY,       KEY_G,   50.0f, 30.0f,  DAMAGE_MAGIC,  200.0f, 2.5f,     50.0f, 0.0f,  ARMOR_LIGHT,  200,  12.0f,     UNIT_SIGHT, false,   0.0f,     0.0f,      48.0f,  0.3f,    72.0f },
    [UNIT_SCOUT]  = { "Scout",  BUILDING_ARCHERY_RANGE, KEY_O,   35.0f,  4.0f,  DAMAGE_PIERCE, 100.0f, 1.0f,    110.0f, 0.0f,  ARMOR_LIGHT,  60,   5.0f,      11,         false,   0.0f,     0.0f,      0.0f,   0.0f,    0.0f  },
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

// Minimap (minimap.c).
#define MINIMAP_ENABLED         1      // 0: no minimap
#define MINIMAP_SIZE            180.0f // square, in reference pixels (a 720 px tall window), scaled with the UI
#define MINIMAP_REBUILD_SECONDS 0.2    // its terrain/fog picture is redrawn at most this often (5x per second)

// Game states (main.c switches between them).
typedef enum { STATE_MENU, STATE_PLAYING, STATE_PAUSED, STATE_VICTORY, STATE_DEFEAT, STATE_EDITOR } GameState;

// Key bindings. input.c reads these names, and the Controls screen lists them
// from CONTROLS below, so changing a key here changes both.
#define KEY_ATTACK_MOVE   KEY_A
#define KEY_STOP          KEY_S
#define KEY_HOLD          KEY_H
#if defined(__EMSCRIPTEN__)
#define KEY_PAUSE         KEY_LEFT_CONTROL   // web: browsers use Esc to leave fullscreen, so pause is on Ctrl
#else
#define KEY_PAUSE         KEY_ESCAPE
#endif
#define KEY_DEBUG_WAVE    KEY_F1
#define KEY_EDITOR        KEY_F2    // while playing: open the map editor on the current map
#define KEY_DEBUG_OVERLAY KEY_F3    // show / hide the debug overlay (default: DEBUG_OVERLAY_DEFAULT in overlay.h)
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
    { CONTROLS_MOUSE,    0,                "Minimap: left / drag",   "Move the camera there" },
    { CONTROLS_MOUSE,    0,                "Minimap: right click",   "Move selected units there" },
    { CONTROLS_MOUSE,    0,                "Mouse wheel",            "Zoom (over a panel: scroll it)" },
    { CONTROLS_KEYBOARD, 0,                "Arrow keys",             "Pan camera" },
    { CONTROLS_KEYBOARD, KEY_ATTACK_MOVE,  "%s, then right click",   "Attack-move (fight on the way)" },
    { CONTROLS_KEYBOARD, KEY_STOP,         "%s",                     "Stop: drop all orders" },
    { CONTROLS_KEYBOARD, KEY_HOLD,         "%s",                     "Hold position: never chase" },
    { CONTROLS_KEYBOARD, KEY_PAUSE,        "%s",                     "Cancel placing/attack-move, or pause" },
    { CONTROLS_KEYBOARD, KEY_DEBUG_WAVE,   "%s",                     "Debug: spawn an enemy wave" },
    { CONTROLS_KEYBOARD, KEY_EDITOR,       "%s",                     "Map editor on the current map" },
    { CONTROLS_KEYBOARD, KEY_DEBUG_OVERLAY,"%s",                     "Debug overlay (FPS, timings, counts)" },
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
// Expansions go to gold FIELDS (clusters of nodes), like StarCraft mineral fields:
#define AI_FIELD_TILES          7               // a field: a gold node plus every node this close to it
#define AI_CLAIMED_TILES        10              // a node with any Base (either side, even unfinished) this close is taken
#define AI_EXPAND_MIN_GOLD      3000            // a field needs at least this much unclaimed gold to be worth a Base
#define AI_EXPAND_ENEMY_TILES   20              // ...and no enemy building this close to it
#define AI_BASE_GOLD_GAP        2               // tiles of open ground kept between a new Base and any gold (room for workers)
#define AI_EXPAND_RESERVE       100             // "spare gold": the base's cost plus this
#define AI_EXPAND_LOW_GOLD      2000            // gold left in our nodes below this: expand without the reserve
#define AI_SAVE_FOR_EXPANSION   1               // 1: pause combat training while saving for an expansion
#define AI_MAX_BASES            3               // cap on bases (finished or being built)
#define AI_MAX_FAILED_NODES     32              // nodes of fields it gave up expanding to (builder died) are remembered
#define AI_BARRACKS_QUEUE       2               // combat units queued per production building; "full" means this many
#define AI_EXTRA_BARRACKS_GOLD  600             // more gold banked than this, every Barracks full: build another
#define AI_MAX_BARRACKS         3               // cap on Barracks (finished or being built)

// Tech buildings: after its first Barracks the AI builds one of each, in this order (each once
// the one before is finished, and only when BuildingsCanBuild() allows it), and rebuilds them if
// destroyed. Army training pauses while it saves up for the next one.
static const BuildingType AI_TECH_ORDER[] = { BUILDING_ARCHERY_RANGE, BUILDING_ACADEMY };
#define AI_TECH_COUNT ((int)(sizeof(AI_TECH_ORDER)/sizeof(AI_TECH_ORDER[0])))

// Army mix: at each production building the AI trains the unit type (of those trained there)
// that is furthest below its share of the army, counting units alive and queued. maxAlive caps
// a type (0 = no cap). Types not listed are never trained by the AI (Workers are handled
// separately). Example: 4 Melee, 2 Knights, 3 Archers, 1 Mage per 10 fighters; at most
// 2 Scouts and 4 Medics.
typedef struct AiArmyShare { UnitType type; int share; int maxAlive; } AiArmyShare;
static const AiArmyShare AI_ARMY_MIX[] = {
    //  type          share  maxAlive
    { UNIT_MELEE,     4,     0 },
    { UNIT_KNIGHT,    2,     0 },
    { UNIT_ARCHER,    3,     0 },
    { UNIT_MAGE,      1,     0 },
    { UNIT_SCOUT,     1,     2 },
    { UNIT_MEDIC,     1,     4 },
};
#define AI_ARMY_MIX_COUNT ((int)(sizeof(AI_ARMY_MIX)/sizeof(AI_ARMY_MIX[0])))

#endif
