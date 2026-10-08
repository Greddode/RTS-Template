// main.c - Window, game states, game loop and performance log.
//
// Game states (config.h): MENU -> Play (pick a map) -> PLAYING <-> PAUSED (Esc),
// and PLAYING -> VICTORY / DEFEAT when one side has no buildings left.
// Each frame works with the state it STARTED in; a change (e.g. Esc opening
// the pause menu) takes effect next frame, so one key press can't count twice.
// While PAUSED nothing in the simulation runs: no ticks, no pathfinding, no
// game input. The frozen world is still drawn behind a dim overlay.
// "Main Menu" then "Play" calls StartNewGame(), which resets every pool.
//
// Maps: StartNewGame() loads the chosen file from maps/ (mapfile.c). If the
// file has an error, the message (file and line) is shown and the generated
// "Random" map is used instead.
//
// Editor (editor/editor.c): "Map Editor" in the main menu, or F2 while
// playing. The editor works on its own copy of the map; when it was opened
// from a game, the game's map and camera are backed up and restored on exit,
// so the paused game carries on untouched. Test Play replaces that game with
// the editor's map; its "Back to Editor" resets everything first.
//
// Win / lose: once a second (not every tick), after a short grace period, a
// side with no buildings at all (finished or not) has lost. VICTORY and DEFEAT
// freeze the sim like PAUSED and show Play Again / Main Menu.
//
// Fixed timestep: the simulation (units, buildings, projectiles, AI) runs exactly
// TICK_RATE times per second no matter the frame rate, so the game behaves the same on a slow
// laptop and a fast desktop. Rendering runs as often as the screen allows and
// blends units between their last two tick positions (alpha), so movement
// still looks smooth at 60+ FPS.

#include "raylib.h"
#include "config.h"
#include "ai.h"
#include "buildings.h"
#include "camera.h"
#include "combat.h"
#include "economy.h"
#include "fog.h"
#include "editor.h"
#include "grid.h"
#include "heal.h"
#include "input.h"
#include "inspector.h"
#include "map.h"
#include "mapfile.h"
#include "menu.h"
#include "minimap.h"
#include "overlay.h"
#include "path.h"
#include "sprites.h"
#include "ui.h"
#include "units.h"
#include "raymath.h"
#include <stdio.h>
#include <string.h>

#if defined(__EMSCRIPTEN__)
    #include <emscripten/emscripten.h>
#endif

#define MAP_SEED        1234
#define AI_BASE_OFFSET  (Vector2){ 1100.0f, -800.0f }   // AI base, relative to the player's base
#define BASE_CLEARING   8        // tiles of open ground made around each base
#define START_WORKERS   6        // player's starting workers
#define START_ARMY      20       // player's starting combat units, half melee / half archers
#define AI_WORKERS      4
#define NODES_PER_BASE  5        // gold nodes in an arc next to each base
#define NODE_DISTANCE   200.0f   // from base centre
#define NEUTRAL_NODES   4        // between the two bases
#define MAX_FRAME_TIME  0.25f    // after a long stall, don't try to catch up more than this
#define PERF_LOG_EVERY  5.0      // seconds between performance lines in the console

#define MENU_BG         (Color){ 24, 30, 36, 255 }
#define PAUSE_DIM       0.55f    // how dark the frozen world gets behind the pause menu
#define GAME_OVER_GRACE (TICK_RATE*5)   // no win/lose check in the first 5 s of a game
#define GAME_OVER_EVERY TICK_RATE       // then check once per second
#define MAP_ERROR_TIME  6.0             // seconds a map loading error stays on screen

static GameState state = STATE_MENU;
static bool quitRequested = false;     // Exit button (desktop only)
static double tickAccumulator = 0.0;   // unsimulated time carried over between frames
static double lastTickMs = 0.0;        // CPU time of the most recent tick
static double nextPerfLog = PERF_LOG_EVERY;
static int perfFrames = 0;             // frames since the last performance line
static long gameTicks = 0;             // sim ticks since this game started
static char currentMap[256] = "";      // map file being played ("" = Random), for Play Again
static bool editorFromGame = false;    // editor opened with F2: Exit returns to the paused game
static bool testPlaying = false;       // playing the editor's map: leaving goes back to the editor
static Camera2D savedCamera;           // the game's camera while the editor borrows it

static void SetupStart(Vector2 playerBase, Vector2 aiBase);
static void StartNewGame(const char *mapPath);
static void UpdateDrawFrame(void);
static void DrawHint(void);

int main(void)
{
#if !defined(__EMSCRIPTEN__)
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);   // the UI scales with window height
#endif
    // Web: the canvas stays SCREEN_W x SCREEN_H and the page (web/shell.html)
    // scales it to fit the browser window, keeping the aspect ratio.
    InitWindow(SCREEN_W, SCREEN_H, "RTS Kit");
    SetExitKey(KEY_NULL);   // Esc opens the pause menu instead of closing the window
    InspectorCheckHotkeys();   // logs a warning if two hotkeys clash
    SpritesLoad();             // art from assets/sprites (needs the window: textures live on the GPU)
    UiFontLoad();              // the UI font from assets/fonts (also a texture)

#if defined(__EMSCRIPTEN__)
    // The browser owns the main loop: it calls us once per frame.
    emscripten_set_main_loop(UpdateDrawFrame, 0, 1);
#else
    SetTargetFPS(60);
    while (!WindowShouldClose() && !quitRequested)
    {
        UpdateDrawFrame();
    }
#endif

    SpritesUnload();
    UiFontUnload();
    CloseWindow();
    return 0;
}

// Gold nodes in an arc beside a base, each snapped to open ground.
static void SpawnNodesNear(Vector2 base, float startAngle)
{
    for (int k = 0; k < NODES_PER_BASE; k++)
    {
        float angle = (startAngle + k*25.0f)*DEG2RAD;
        Vector2 p = { base.x + cosf(angle)*NODE_DISTANCE, base.y + sinf(angle)*NODE_DISTANCE };
        UnitsOpenSpots(p, 1, &p);
        EconomySpawnNode(p, GOLD_NODE_AMOUNT);
    }
}

static void SpawnUnitsNear(Vector2 at, int count, UnitType type, int team)
{
    static Vector2 spots[MAX_UNITS];
    int found = UnitsOpenSpots(at, count, spots);
    for (int k = 0; k < found; k++) UnitSpawn(spots[k], type, team);
}

// Both bases, gold nodes, the player's workers and a small army, and the AI's workers.
static void SetupStart(Vector2 playerBase, Vector2 aiBase)
{
    MapClearArea(playerBase, BASE_CLEARING);
    MapClearArea(aiBase, BASE_CLEARING);
    BuildingPlace(BUILDING_BASE, PLAYER_TEAM, playerBase, false);
    int aiBaseId = BuildingPlace(BUILDING_BASE, AI_TEAM, aiBase, false);

    SpawnNodesNear(playerBase, 150.0f);   // arcs facing away from the other base
    SpawnNodesNear(aiBase, -30.0f);
    for (int k = 0; k < NEUTRAL_NODES; k++)
    {
        Vector2 p = Vector2Lerp(playerBase, aiBase, 0.35f + 0.1f*k);
        UnitsOpenSpots(p, 1, &p);
        EconomySpawnNode(p, GOLD_NODE_AMOUNT);
    }

    float below = TILE_SIZE*3.0f;   // just under the 3x3 base
    SpawnUnitsNear((Vector2){ playerBase.x, playerBase.y + below }, START_WORKERS, UNIT_WORKER, PLAYER_TEAM);
    SpawnUnitsNear((Vector2){ playerBase.x - 60.0f, playerBase.y + below*2.5f }, START_ARMY/2, UNIT_MELEE, PLAYER_TEAM);
    SpawnUnitsNear((Vector2){ playerBase.x + 60.0f, playerBase.y + below*2.5f }, START_ARMY/2, UNIT_ARCHER, PLAYER_TEAM);
    SpawnUnitsNear((Vector2){ aiBase.x, aiBase.y + below }, AI_WORKERS, UNIT_WORKER, AI_TEAM);

    AiInit(playerBase, aiBase, aiBaseId);
}

// Empty every pool and reset every system.
static void ResetWorld(void)
{
    UnitsReset();
    BuildingsReset();
    CombatReset();
    PathReset();
    InputReset();
    EconomyInit();
    GridRebuild();   // the grid must exist before anything queries it (placing a base does)
}

// A fresh game on a map file, or the generated map when mapPath is NULL / "".
static void StartNewGame(const char *mapPath)
{
    // Copy via a temporary: Play Again passes currentMap itself, and copying a
    // string onto itself (overlapping memory) is undefined behaviour in C.
    char path[sizeof(currentMap)];
    snprintf(path, sizeof(path), "%s", mapPath ? mapPath : "");
    memcpy(currentMap, path, sizeof(currentMap));
    ResetWorld();

    bool loaded = false;
    if (currentMap[0] != '\0')
    {
        MapStart start;
        loaded = MapFileLoad(currentMap, &start);
        if (loaded)
        {
            AiInit(start.baseCentre[PLAYER_TEAM], start.baseCentre[AI_TEAM], start.aiBase);
            CamInit();
            CamLookAt(start.baseCentre[PLAYER_TEAM]);
        }
        else
        {
            UiShowMessageFor(TextFormat("%s - playing the Random map instead", MapFileError()), MAP_ERROR_TIME);
            ResetWorld();   // the file may have placed some things before failing
        }
    }

    if (!loaded)
    {
        MapGenerate(MAP_SEED);
        CamInit();
        Vector2 playerBase = { MAP_PIXEL_W/2.0f, MAP_PIXEL_H/2.0f };
        SetupStart(playerBase, Vector2Add(playerBase, AI_BASE_OFFSET));
    }

    GridRebuild();
    FogReset();   // after the map and starting units exist
    MinimapReset();
    tickAccumulator = 0.0;
    gameTicks = 0;
}

// --- Editor -------------------------------------------------------------------------
static void OpenEditorFromGame(void)
{
    MapBackup();
    savedCamera = gameCamera;
    EditorOpenFromGame();
    editorFromGame = true;
    state = STATE_EDITOR;
}

static void CloseEditor(void)
{
    if (editorFromGame)   // back to the paused game, exactly as it was
    {
        MapRestore();
        gameCamera = savedCamera;
        editorFromGame = false;
        state = STATE_PAUSED;
    }
    else state = STATE_MENU;
    MenuOpen();
}

static void StartTestPlay(void)
{
    editorFromGame = false;   // the paused game (if any) is replaced by the test game
    testPlaying = true;
    MenuSetTestPlay(true);
    StartNewGame(EditorTestPlayPath());
    state = STATE_PLAYING;
}

// Leaving a test game: clear everything it created, then show the editor again.
static void ReturnToEditor(void)
{
    ResetWorld();
    testPlaying = false;
    MenuSetTestPlay(false);
    EditorResume();
    state = STATE_EDITOR;
}

// Once a second after the grace period: a side with no buildings has lost.
static void CheckGameOver(void)
{
    if (gameTicks < GAME_OVER_GRACE || gameTicks % GAME_OVER_EVERY != 0) return;
    if (BuildingsCount(PLAYER_TEAM) == 0) { state = STATE_DEFEAT; MenuOpen(); }
    else if (BuildingsCount(AI_TEAM) == 0) { state = STATE_VICTORY; MenuOpen(); }
}

// Input, pathfinding and the fixed sim ticks. Only runs while PLAYING.
static void UpdatePlaying(void)
{
    // KEY_PAUSE (Esc; Ctrl on web) cancels a pending attack-move / building placement first; otherwise it pauses.
    if (IsKeyPressed(KEY_PAUSE) && !InputHasPendingCommand())
    {
        state = STATE_PAUSED;
        MenuOpen();
        return;
    }
    if (IsKeyPressed(KEY_EDITOR))   // F2: the editor on this map (or back to it from a test game)
    {
        if (testPlaying) ReturnToEditor();
        else OpenEditorFromGame();
        return;
    }

    float frameTime = GetFrameTime();
    if (frameTime > MAX_FRAME_TIME) frameTime = MAX_FRAME_TIME;

    // Per-frame: things that should feel instant.
    CamUpdate(frameTime);
    InputUpdate();
    if (IsKeyPressed(KEY_DEBUG_WAVE)) AiSpawnWave(AI_WAVE_SIZE);   // debug: test wave
    PathUpdate();   // budgeted: leftover requests wait for the next frame

    // Fixed ticks: run as many as the elapsed time covers (0, 1 or several).
    tickAccumulator += frameTime;
    while (tickAccumulator >= TICK_DT && state == STATE_PLAYING)   // game over stops the ticks at once
    {
        double start = GetTime();
        UnitsTick();
        BuildingsTick();
        CombatProjectilesTick();
        AiTick();
        GridRebuild();
        FogTick();
        lastTickMs = (GetTime() - start)*1000.0;
        tickAccumulator -= TICK_DT;
        gameTicks++;
        CheckGameOver();
    }
}

static void DrawWorld(void)
{
    float alpha = (float)(tickAccumulator/TICK_DT);   // frozen while paused, so the picture holds still
    BeginMode2D(gameCamera);
        Rectangle view = CamViewRect();
        MapDraw(view);
        EconomyDrawNodes(view);
        BuildingsDraw(view);
        UnitsDraw(view, alpha);
        HealDraw(view, alpha);
        CombatProjectilesDraw(view, alpha);
        FogDraw(view);
        InputDraw();
    EndMode2D();
}

static void UpdateDrawFrame(void)
{
    UiBegin();
    OverlayUpdate();   // F3, in every screen
    GameState frameState = state;   // changes made below take effect next frame

    if (frameState == STATE_PLAYING) UpdatePlaying();

    BeginDrawing();
    ClearBackground(frameState == STATE_MENU ? MENU_BG : BLACK);

    if (frameState == STATE_MENU)
    {
        MenuAction action = MenuMain();
        if (action == MENU_PLAY) { StartNewGame(MenuChosenMap()); state = STATE_PLAYING; }
        if (action == MENU_EDITOR) { editorFromGame = false; EditorOpenNew(64); state = STATE_EDITOR; }
        if (action == MENU_EXIT) quitRequested = true;
    }
    else if (frameState == STATE_EDITOR)
    {
        EditorAction action = EditorFrame();
        if (action == EDITOR_EXIT) CloseEditor();
        if (action == EDITOR_TEST_PLAY) StartTestPlay();
    }
    else if (frameState == STATE_PLAYING)
    {
        DrawWorld();
        EconomyDrawHud(PLAYER_TEAM);
        MinimapDraw();     // clickable: only while playing
        InspectorDraw();   // has the Train/Build buttons: only while playing
        UiDrawMessage();
        DrawHint();
        OverlayDrawFull(lastTickMs);
    }
    else if (frameState == STATE_VICTORY || frameState == STATE_DEFEAT)
    {
        DrawWorld();   // frozen, like paused
        EconomyDrawHud(PLAYER_TEAM);
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, PAUSE_DIM));
        MenuAction action = MenuGameOver(frameState == STATE_VICTORY);
        if (action == MENU_PLAY) { StartNewGame(currentMap); state = STATE_PLAYING; }   // same map again
        if (action == MENU_MAIN_MENU) { if (testPlaying) ReturnToEditor(); else { state = STATE_MENU; MenuOpen(); } }
    }
    else   // STATE_PAUSED
    {
        DrawWorld();
        EconomyDrawHud(PLAYER_TEAM);
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, PAUSE_DIM));
        MenuAction action = MenuPause();
        if (action == MENU_RESUME) state = STATE_PLAYING;
        if (action == MENU_MAIN_MENU) { if (testPlaying) ReturnToEditor(); else { state = STATE_MENU; MenuOpen(); } }
        if (action == MENU_EXIT) quitRequested = true;
    }

    if (frameState != STATE_PLAYING) OverlayDrawFpsLine();   // menus, pause, game over, editor
    EndDrawing();

#if defined(__EMSCRIPTEN__)
    if (quitRequested) emscripten_cancel_main_loop();   // a browser tab can't close itself; just stop
#endif

    // Count frames ourselves: raylib's GetFPS() only samples when it's called,
    // and with the F3 overlay hidden nothing else calls it, so it would report nonsense.
    perfFrames++;
    if (GetTime() >= nextPerfLog)
    {
        static const char *stateNames[] = { "menu", "playing", "paused", "victory", "defeat", "editor" };
        TraceLog(LOG_INFO, "PERF: %s | %.0f FPS | %d units | %d projectiles | tick %.2f ms | paths queued %d",
                 stateNames[state], perfFrames/PERF_LOG_EVERY, UnitsActiveCount(), CombatProjectileCount(), lastTickMs, PathQueueLength());
        nextPerfLog += PERF_LOG_EVERY;
        perfFrames = 0;
    }
}

// Bottom left, above the minimap: how to reach the pause menu (always shown while playing).
static void DrawHint(void)
{
    float hintY = MINIMAP_ENABLED ? MinimapRect().y - Ui(24.0f) : GetScreenHeight() - Ui(26.0f);
    UiLabel(TextFormat("%s: pause menu & controls", UiKeyName(KEY_PAUSE)), Ui(10.0f), hintY, Ui(18.0f), RAYWHITE);
}
