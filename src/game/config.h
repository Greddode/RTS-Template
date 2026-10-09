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

// Damage and armor types (the table that combines them is further down, under
// "Damage and armor"). Declared first: buildings and units both use them.
typedef enum { DAMAGE_PIERCE, DAMAGE_BLUNT, DAMAGE_MAGIC, DAMAGE_TYPE_COUNT } DamageType;
typedef enum { ARMOR_LIGHT, ARMOR_MEDIUM, ARMOR_HEAVY, ARMOR_TYPE_COUNT } ArmorType;

// Building types come next: units say where they're trained.
// To add a building: add it to the enum and give it a row in BUILDING_STATS.
// If it has a cost and a hotkey, workers can build it (it appears in the
// inspector's Build buttons automatically). `requires`: workers can only start
// one while the team owns a FINISHED building of that type (BuildingsCanBuild()).
// `description`: a sentence the inspector shows for it ("" = no box).
// Attack columns (damage, damageType, range, cooldown, hitsGround, hitsAir):
// damage > 0 makes it a tower that shoots arrows at the nearest enemy unit in
// range it can see and hit (combat.c, CombatBuildingTick). 0 = doesn't attack.
// `needsWater`: it must stand on ground with a water tile touching its edge
// (within BUILDING_WATER_MARGIN tiles, buildings.h), like a Dock. Every
// placement (player, ghost, AI, map files, editor) goes through one rule,
// BuildingsPlacementOK() in buildings.c. Map files and the editor can place
// any type (no cost or `requires`), but only where that rule allows.
typedef enum { BUILDING_NONE = -1, BUILDING_BASE, BUILDING_BARRACKS, BUILDING_ARCHERY_RANGE, BUILDING_ACADEMY, BUILDING_AIR_FACTORY, BUILDING_GUARD_TOWER, BUILDING_DOCK, BUILDING_TYPE_COUNT } BuildingType;

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
    bool        needsWater;  // must touch water (a Dock); false = anywhere on open ground
    float       damage;      // per arrow, before armor; 0 = doesn't attack
    DamageType  damageType;
    float       range;       // world pixels, from its centre to the target's centre
    float       cooldown;    // seconds between arrows
    bool        hitsGround;  // shoots ground and naval units
    bool        hitsAir;     // shoots flyers
    const char *description; // shown in the inspector (selected, or hovering its Build button); "" = none
} BuildingStats;

static const BuildingStats BUILDING_STATS[BUILDING_TYPE_COUNT] = {
    //                           name             hp       size  cost  buildTime  hotkey  dropOff  sight           requires           needsWater  damage  damageType     range   cooldown  hitsGround  hitsAir  description
    [BUILDING_BASE]          = { "Base",          1500.0f, 3,    400,  40.0f,     KEY_B,  true,    BUILDING_SIGHT, BUILDING_NONE,     false,      0.0f,   DAMAGE_PIERCE,   0.0f, 0.0f,     false,      false,   "Trains Workers and takes in the gold they mine." },
    [BUILDING_BARRACKS]      = { "Barracks",       900.0f, 2,    150,  25.0f,     KEY_K,  false,   BUILDING_SIGHT, BUILDING_NONE,     false,      0.0f,   DAMAGE_PIERCE,   0.0f, 0.0f,     false,      false,   "Trains Melee soldiers and Knights, the front line of your army." },
    [BUILDING_ARCHERY_RANGE] = { "Archery Range",  800.0f, 2,    175,  25.0f,     KEY_R,  false,   BUILDING_SIGHT, BUILDING_NONE,     false,      0.0f,   DAMAGE_PIERCE,   0.0f, 0.0f,     false,      false,   "Trains Archers and Scouts, who fight and scout from a distance." },
    [BUILDING_ACADEMY]       = { "Academy",       1000.0f, 2,    450,  35.0f,     KEY_E,  false,   BUILDING_SIGHT, BUILDING_BARRACKS, false,      0.0f,   DAMAGE_PIERCE,   0.0f, 0.0f,     false,      false,   "Trains Medics and Mages, once you own a finished Barracks." },
    [BUILDING_AIR_FACTORY]   = { "Air Factory",    900.0f, 2,    250,  30.0f,     KEY_F,  false,   BUILDING_SIGHT, BUILDING_BARRACKS, false,      0.0f,   DAMAGE_PIERCE,   0.0f, 0.0f,     false,      false,   "Trains Falcons and Airships, once you own a finished Barracks." },
    [BUILDING_GUARD_TOWER]   = { "Guard Tower",   1000.0f, 2,    200,  25.0f,     KEY_V,  false,   BUILDING_SIGHT, BUILDING_BARRACKS, false,      12.0f,  DAMAGE_PIERCE, 190.0f, 0.7f,     true,       true,    "Shoots arrows at enemies in range, on the ground and in the air." },
    [BUILDING_DOCK]          = { "Dock",           900.0f, 2,    250,  30.0f,     KEY_D,  false,   BUILDING_SIGHT, BUILDING_BARRACKS, true,       0.0f,   DAMAGE_PIERCE,   0.0f, 0.0f,     false,      false,   "Trains Boats and Ships. Must be built on the shore, touching water." },
};

// --- Damage and armor ---------------------------------------------------------------
// Every unit deals one damage type and wears one armor type. A hit does
//     damage = max(base * DAMAGE_MIN_FRACTION, base * DAMAGE_VS_ARMOR[type][armorType] - armor)
// (CombatDamage() in combat.c). Hits on buildings do the plain base damage.
// To add a type: add it to its enum (before the _COUNT), give it a name below,
// and a row (damage type) or a column (armor type) in DAMAGE_VS_ARMOR.
// (DamageType and ArmorType are declared above, before the building table.)

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

// Movement classes: what a unit moves over. TILE_INFO (map.c) says which
// classes can cross each tile; MapTileWalkable(class, x, y) is the one check.
//   MOVE_GROUND  walks on land (grass, dirt, gravel); buildings block it
//   MOVE_NAVAL   sails on water; buildings block it too
//   MOVE_AIR     flies in a straight line over every tile whose `air` column
//                is true (all of them by default) and over buildings; it
//                never pathfinds, and it stays inside the map
typedef enum { MOVE_GROUND, MOVE_NAVAL, MOVE_AIR, MOVE_CLASS_COUNT } MoveClass;

// Unit types and their stats. To add a type: add it to the enum, give it a row
// in UNIT_STATS, and (optionally) art in assets/sprites/units or a look in
// UnitsDrawIcon(). `trainedAt` puts a Train button on that building;
// BUILDING_NONE means it can't be trained. Columns a unit doesn't use stay 0
// (healing, splash, minRange): 0 means "off". `moveClass`: see MoveClass
// above. `description` is what the inspector shows for it ("" = no box).
// What it can hit: `hitsGround` = ground and naval units and buildings,
// `hitsAir` = flying units (MOVE_AIR). A unit never targets, chases or
// damages (splash included) what it can't hit. Healers: false, false.
// `requires`: besides the building in `trainedAt`, the team must own a
// FINISHED one of these to train it (UnitsCanTrain); BUILDING_NONE = nothing.
// Transports (transport.c): `cargoCapacity` > 0 makes a unit carry other
// units, that many slots' worth; `cargoSlots` is how many slots a unit takes
// in one (0 = it can't be carried).
typedef enum { UNIT_MELEE, UNIT_ARCHER, UNIT_WORKER, UNIT_KNIGHT, UNIT_MEDIC, UNIT_MAGE, UNIT_SCOUT, UNIT_FALCON, UNIT_AIRSHIP, UNIT_BOAT, UNIT_SHIP, UNIT_TYPE_COUNT } UnitType;

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
    float splashRadius;   // > 0: fires a bolt that hits every unit and building this close to where it lands that it could hit (friends too)
    float splashFalloff;  // damage at the edge of the splash, as a fraction of the centre's (1 = same everywhere)
    float minRange;       // won't fire at targets closer than this; backs off or picks another (0 = none)
    bool  hitsGround;     // can attack ground / naval units and buildings
    bool  hitsAir;        // can attack flying units
    BuildingType requires;   // must own a finished one of these to train it (besides trainedAt); BUILDING_NONE = nothing
    int   cargoCapacity;  // a transport: slots of cargo it carries (0 = not a transport)
    int   cargoSlots;     // slots it takes inside a transport (0 = can't be carried)
    MoveClass moveClass;  // MOVE_GROUND, MOVE_NAVAL or MOVE_AIR
    const char *description;   // shown in the inspector (selected, or hovering its Train button); "" = none
} UnitStats;

static const UnitStats UNIT_STATS[UNIT_TYPE_COUNT] = {
    //                  name       trainedAt               hotkey  hp      damage  damageType     range   cooldown  speed  armor  armorType     cost  trainTime  sight       canHeal  healRate  healRange  splash  falloff  minRange  hitsGround  hitsAir  requires          cargoCapacity  cargoSlots  moveClass    description
    [UNIT_MELEE]   = { "Melee",   BUILDING_BARRACKS,      KEY_M,  120.0f, 12.0f,  DAMAGE_BLUNT,   16.0f, 0.8f,     75.0f, 1.0f,  ARMOR_MEDIUM, 75,   6.0f,      UNIT_SIGHT, false,   0.0f,     0.0f,      0.0f,   0.0f,    0.0f,     true,       false,   BUILDING_NONE, 0,             1,          MOVE_GROUND, "A cheap, sturdy foot soldier whose mace crushes heavy armor." },
    [UNIT_ARCHER]  = { "Archer",  BUILDING_ARCHERY_RANGE, KEY_C,   70.0f,  9.0f,  DAMAGE_PIERCE, 120.0f, 1.2f,     65.0f, 0.0f,  ARMOR_LIGHT,  100,  7.0f,      UNIT_SIGHT, false,   0.0f,     0.0f,      0.0f,   0.0f,    0.0f,     true,       true,    BUILDING_NONE, 0,             1,          MOVE_GROUND, "Shoots from a distance, deadly against light armor but weak against plate." },
    [UNIT_WORKER]  = { "Worker",  BUILDING_BASE,          KEY_W,   40.0f,  4.0f,  DAMAGE_BLUNT,   16.0f, 1.0f,     70.0f, 0.0f,  ARMOR_LIGHT,  50,   5.0f,      UNIT_SIGHT, false,   0.0f,     0.0f,      0.0f,   0.0f,    0.0f,     true,       false,   BUILDING_NONE, 0,             1,          MOVE_GROUND, "Mines gold and constructs buildings, but barely fights." },
    [UNIT_KNIGHT]  = { "Knight",  BUILDING_BARRACKS,      KEY_N,  300.0f, 18.0f,  DAMAGE_BLUNT,   16.0f, 1.0f,     55.0f, 2.0f,  ARMOR_HEAVY,  175,  10.0f,     UNIT_SIGHT, false,   0.0f,     0.0f,      0.0f,   0.0f,    0.0f,     true,       false,   BUILDING_NONE, 0,             2,          MOVE_GROUND, "A slow, heavily armored soldier that hits hard and shrugs off arrows." },
    [UNIT_MEDIC]   = { "Medic",   BUILDING_ACADEMY,       KEY_D,   60.0f,  0.0f,  DAMAGE_PIERCE,   0.0f, 0.0f,     70.0f, 0.0f,  ARMOR_LIGHT,  125,  8.0f,      UNIT_SIGHT, true,    8.0f,     64.0f,     0.0f,   0.0f,    0.0f,     false,      false,   BUILDING_NONE, 0,             1,          MOVE_GROUND, "Heals wounded allies nearby instead of attacking." },
    [UNIT_MAGE]    = { "Mage",    BUILDING_ACADEMY,       KEY_G,   50.0f, 30.0f,  DAMAGE_MAGIC,  200.0f, 2.5f,     50.0f, 0.0f,  ARMOR_LIGHT,  200,  12.0f,     UNIT_SIGHT, false,   0.0f,     0.0f,      48.0f,  0.3f,    72.0f,    true,       true,    BUILDING_NONE, 0,             1,          MOVE_GROUND, "Hurls magic bolts that hit everything where they land, friends too, but can't fire at close range." },
    [UNIT_SCOUT]   = { "Scout",   BUILDING_ARCHERY_RANGE, KEY_O,   35.0f,  4.0f,  DAMAGE_PIERCE, 100.0f, 1.0f,    110.0f, 0.0f,  ARMOR_LIGHT,  60,   5.0f,      11,         false,   0.0f,     0.0f,      0.0f,   0.0f,    0.0f,     true,       true,    BUILDING_NONE, 0,             1,          MOVE_GROUND, "A fast, far-sighted rider for finding the enemy, fragile in a fight." },
    [UNIT_FALCON]  = { "Falcon",  BUILDING_AIR_FACTORY,   KEY_L,   45.0f,  6.0f,  DAMAGE_PIERCE,  90.0f, 0.9f,    140.0f, 0.0f,  ARMOR_LIGHT,  70,   6.0f,      9,          false,   0.0f,     0.0f,      0.0f,   0.0f,    0.0f,     true,       true,    BUILDING_NONE, 0,             0,          MOVE_AIR,    "A very fast, fragile flyer that pecks at air and ground from a distance." },
    [UNIT_AIRSHIP] = { "Airship", BUILDING_AIR_FACTORY,   KEY_U,  420.0f, 28.0f,  DAMAGE_BLUNT,   24.0f, 3.0f,     38.0f, 3.0f,  ARMOR_HEAVY,  450,  18.0f,     8,          false,   0.0f,     0.0f,      44.0f,  0.5f,    0.0f,     true,       false,   BUILDING_ACADEMY, 8,             0,          MOVE_AIR,    "A slow, armored airship that carries 8 slots of ground troops over anything and bombs the ground below (friends too), but can't hit flyers." },
    [UNIT_BOAT]    = { "Boat",    BUILDING_DOCK,          KEY_T,   90.0f,  7.0f,  DAMAGE_PIERCE, 110.0f, 1.0f,    105.0f, 0.0f,  ARMOR_LIGHT,  80,   7.0f,      8,          false,   0.0f,     0.0f,      0.0f,   0.0f,    0.0f,     true,       true,    BUILDING_NONE, 0,             0,          MOVE_NAVAL,  "A fast, light boat with an archer aboard: shoots ships, the shore and flyers, but sinks quickly." },
    [UNIT_SHIP]    = { "Ship",    BUILDING_DOCK,          KEY_P,  520.0f, 32.0f,  DAMAGE_BLUNT,  240.0f, 3.0f,     40.0f, 3.0f,  ARMOR_HEAVY,  400,  18.0f,     9,          false,   0.0f,     0.0f,      40.0f,  0.4f,    0.0f,     true,       false,   BUILDING_NONE, 0,             0,          MOVE_NAVAL,  "A slow, armored warship whose cannon hits everything where the shot lands (friends too) from far away, but can't hit flyers." },
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
#define KEY_DEBUG_ARMY    KEY_F4    // debug: DEBUG_ARMY_SIZE mixed units per side (big-battle tests)
#define KEY_EDITOR        KEY_F2    // while playing: open the map editor on the current map
#define KEY_DEBUG_OVERLAY KEY_F3    // show / hide the debug overlay (default: DEBUG_OVERLAY_DEFAULT in overlay.h)
#define KEY_UNDO          KEY_Z     // with Ctrl, in the editor
#define KEY_LOAD          KEY_L     // a transport selected: nearby idle units board it (an Air Factory selected: L trains a Falcon)
#define KEY_UNLOAD        KEY_U     // a transport selected: unload everything below it (an Air Factory selected: U trains an Airship)
#if defined(__EMSCRIPTEN__)
#define KEY_UNLOAD_MODIFIER KEY_LEFT_SHIFT   // web: Ctrl is the pause key there, so Shift + right click unloads
#else
#define KEY_UNLOAD_MODIFIER KEY_LEFT_CONTROL // held with a right click on the ground: the transport flies there and unloads
#endif

// Transports (transport.c).
#define TRANSPORT_BOARD_DISTANCE    28.0f  // a unit this close (world px, centre to centre) to its transport gets in
#define TRANSPORT_LOAD_RADIUS_TILES 5      // KEY_LOAD: idle units this close are told to board
#define TRANSPORT_UNLOAD_PER_TICK   2      // units let out per sim tick while unloading (so they don't stack)
#define TRANSPORT_DROP_SEARCH_TILES 12     // how far from a blocked drop point it looks for open ground

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
    { CONTROLS_MOUSE,    0,                "Right click own Airship", "Selected ground units board it" },
    { CONTROLS_MOUSE,    KEY_UNLOAD_MODIFIER, "%s + right click",    "Airship flies there and unloads" },
    { CONTROLS_KEYBOARD, 0,                "Arrow keys",             "Pan camera" },
    { CONTROLS_KEYBOARD, KEY_ATTACK_MOVE,  "%s, then right click",   "Attack-move (fight on the way)" },
    { CONTROLS_KEYBOARD, KEY_STOP,         "%s",                     "Stop: drop all orders" },
    { CONTROLS_KEYBOARD, KEY_HOLD,         "%s",                     "Hold position: never chase" },
    { CONTROLS_KEYBOARD, KEY_PAUSE,        "%s",                     "Cancel placing/attack-move, or pause" },
    { CONTROLS_KEYBOARD, KEY_DEBUG_WAVE,   "%s",                     "Debug: spawn an enemy wave" },
    { CONTROLS_KEYBOARD, KEY_DEBUG_ARMY,   "%s",                     "Debug: a big army for each side" },
    { CONTROLS_KEYBOARD, KEY_EDITOR,       "%s",                     "Map editor on the current map" },
    { CONTROLS_KEYBOARD, KEY_DEBUG_OVERLAY,"%s",                     "Debug overlay (FPS, timings, counts)" },
    { CONTROLS_KEYBOARD, KEY_UNDO,         "Ctrl + %s (editor)",     "Undo tile painting" },
    { CONTROLS_KEYBOARD, KEY_LOAD,         "%s (Airship selected)",  "Nearby idle units board it" },
    { CONTROLS_KEYBOARD, KEY_UNLOAD,       "%s (Airship selected)",  "Unload everything below it" },
};
#define CONTROLS_COUNT ((int)(sizeof(CONTROLS)/sizeof(CONTROLS[0])))

// --- AI tuning (ai.c) -------------------------------------------------------------
// Every number the computer opponent uses, so it can be tuned without reading ai.c.
#define AI_THINK_TICKS          (TICK_RATE*2)   // decisions every 2 s
#define AI_TRAIN_TICKS          (TICK_RATE*5)   // try to queue a combat unit every 5 s
#define AI_WAVE_SIZE            20              // F1 debug wave
// F4 debug armies: DEBUG_ARMY_SIZE units for EACH side, spawned around its base on free
// spots (never on top of other units), cycling through DEBUG_ARMY_MIX. Press it again to add
// more; the pool limit is MAX_UNITS (units.h). They spawn idle (the AI then uses its own).
#define DEBUG_ARMY_SIZE         500
static const UnitType DEBUG_ARMY_MIX[] = { UNIT_MELEE, UNIT_ARCHER, UNIT_MELEE, UNIT_KNIGHT, UNIT_ARCHER,
                                           UNIT_MAGE, UNIT_MELEE, UNIT_MEDIC, UNIT_SCOUT, UNIT_FALCON };
#define DEBUG_ARMY_MIX_COUNT ((int)(sizeof(DEBUG_ARMY_MIX)/sizeof(DEBUG_ARMY_MIX[0])))
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
#define AI_ANTI_AIR_PER_FLYER   2               // units that can hit air (hitsAir) it wants per player flyer it sees

// Ferrying by Airship (ai_ferry.c). Only when NO player building can be reached
// by ground from its army's region ("needs transport", e.g. islands); on maps
// where the army can walk to the player none of this runs.
#define AI_FERRY_CHECK_TICKS        10              // ferries are looked after 3x per second
#define AI_FERRY_MAX_AIRSHIPS       3               // most Airships it ferries with at once
#define AI_FERRY_EXTRA_AIRSHIP_GOLD 900             // gold banked above this, every Airship busy: train another
#define AI_FERRY_GATHER_SECONDS     25              // how long it waits to fill an Airship before going with what's aboard
#define AI_FERRY_MIN_CARGO_SLOTS    3               // ...as long as at least this many slots are full
#define AI_FERRY_ESCORT_MIN         3               // Medics only ride along with at least this many fighters aboard...
#define AI_FERRY_MEDICS_PER_TRIP    1               // ...and at most this many per trip
#define AI_FERRY_DROP_SEARCH_TILES  24              // drop point: spiral search this far round the target
#define AI_FERRY_TOWER_MARGIN_TILES 2               // ...staying this much further than a known tower's range
#define AI_FERRY_CROWD_TILES        4               // ...and with at most AI_FERRY_CROWD_MAX enemy units this close
#define AI_FERRY_CROWD_MAX          2
#define AI_FERRY_DANGER_TILES       0               // an enemy that can hit air within its range + this of the drop point or the flight line: pick another
#define AI_FERRY_ABORT_DAMAGE       0.35f           // Airship lost this share of its max HP on a trip: turn back
#define AI_FERRY_COMMIT_TILES       10              // this close to its drop point it lands even if enemies gather there (they come to shoot at it)
#define AI_FERRY_RETRY_SECONDS      60              // after losing an Airship, wait this long before trying again...
#define AI_FERRY_MAX_FAILURES       3               // ...and give up ferrying after losing this many
#define AI_FERRY_EXPANSION          0               // 1: it may also ferry a Worker to gold in another region and build a Base there

// Tech buildings: after its first Barracks the AI builds one of each, in this order (each once
// the one before is finished, and only when BuildingsCanBuild() allows it), and rebuilds them if
// destroyed. Army training pauses while it saves up for the next one. A type listed twice means
// two of them. One that doesn't fit near its base is skipped (not saved for).
// AI_BUILDS_TOWERS 1: AI_MAIN_BASE_TOWERS Guard Towers are added to the end of that list, so
// the AI builds them by its main base once its other tech buildings stand (not at expansions).
// AI_BUILDS_DOCKS 1: a Dock is added too (near its base, touching water; skipped if there's no
// such spot), and Boats join its army mix (at most AI_BOATS_MAX alive). Off: it never builds a
// Dock or trains Boats or Ships.
#define AI_BUILDS_TOWERS    1
#define AI_MAIN_BASE_TOWERS 2   // 0 to 3 (one #if row each below)
#define AI_BUILDS_DOCKS     0
#define AI_BOATS_MAX        6
static const BuildingType AI_TECH_ORDER[] = { BUILDING_ARCHERY_RANGE, BUILDING_ACADEMY, BUILDING_AIR_FACTORY,
#if AI_BUILDS_TOWERS && AI_MAIN_BASE_TOWERS >= 1
    BUILDING_GUARD_TOWER,
#endif
#if AI_BUILDS_TOWERS && AI_MAIN_BASE_TOWERS >= 2
    BUILDING_GUARD_TOWER,
#endif
#if AI_BUILDS_TOWERS && AI_MAIN_BASE_TOWERS >= 3
    BUILDING_GUARD_TOWER,
#endif
#if AI_BUILDS_DOCKS
    BUILDING_DOCK,
#endif
};
#define AI_TECH_COUNT ((int)(sizeof(AI_TECH_ORDER)/sizeof(AI_TECH_ORDER[0])))

// Army mix: at each production building the AI trains the unit type (of those trained there)
// that is furthest below its share of the army, counting units alive and queued. maxAlive caps
// a type (0 = no cap). Types not listed are never trained by the AI (Workers are handled
// separately). Example: 4 Melee, 2 Knights, 3 Archers, 1 Mage, 1 Falcon per 11 fighters; at
// most 2 Scouts, 4 Medics and 4 Falcons.
typedef struct AiArmyShare { UnitType type; int share; int maxAlive; } AiArmyShare;
static const AiArmyShare AI_ARMY_MIX[] = {
    //  type          share  maxAlive
    { UNIT_MELEE,     4,     0 },
    { UNIT_KNIGHT,    2,     0 },
    { UNIT_ARCHER,    3,     0 },
    { UNIT_MAGE,      1,     0 },
    { UNIT_SCOUT,     1,     2 },
    { UNIT_MEDIC,     1,     4 },
    { UNIT_FALCON,    1,     4 },
#if AI_BUILDS_DOCKS
    { UNIT_BOAT,      2,     AI_BOATS_MAX },
#endif
};
#define AI_ARMY_MIX_COUNT ((int)(sizeof(AI_ARMY_MIX)/sizeof(AI_ARMY_MIX[0])))

#endif
