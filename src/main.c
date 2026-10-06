// main.c - Window, game loop and debug overlay.
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
#include "grid.h"
#include "input.h"
#include "map.h"
#include "path.h"
#include "units.h"
#include "raymath.h"

#if defined(__EMSCRIPTEN__)
    #include <emscripten/emscripten.h>
#endif

#define MAP_SEED        1234
#define AI_BASE_OFFSET  (Vector2){ 1100.0f, -800.0f }   // AI base, relative to the player's base
#define BASE_CLEARING   8        // tiles of open ground made around each base
#define START_WORKERS   6        // player's starting workers
#define START_ARMY      20       // player's starting combat units, half melee / half ranged
#define AI_WORKERS      4
#define NODES_PER_BASE  5        // gold nodes in an arc next to each base
#define NODE_DISTANCE   200.0f   // from base centre
#define NEUTRAL_NODES   4        // between the two bases
#define MAX_FRAME_TIME  0.25f    // after a long stall, don't try to catch up more than this
#define PERF_LOG_EVERY  5.0      // seconds between performance lines in the console

static double tickAccumulator = 0.0;   // unsimulated time carried over between frames
static double lastTickMs = 0.0;        // CPU time of the most recent tick
static double nextPerfLog = PERF_LOG_EVERY;

static void SetupStart(Vector2 playerBase, Vector2 aiBase);
static void UpdateDrawFrame(void);
static void DrawOverlay(void);

int main(void)
{
    InitWindow(SCREEN_W, SCREEN_H, "RTS Kit");
    SetExitKey(KEY_NULL);   // Esc is used to cancel orders; close with the window's X

    MapGenerate(MAP_SEED);
    CamInit();
    EconomyInit();
    GridRebuild();   // the grid must exist before anything queries it (placing a base does)
    Vector2 playerBase = { MAP_PIXEL_W/2.0f, MAP_PIXEL_H/2.0f };
    SetupStart(playerBase, Vector2Add(playerBase, AI_BASE_OFFSET));
    GridRebuild();

#if defined(__EMSCRIPTEN__)
    // The browser owns the main loop: it calls us once per frame.
    emscripten_set_main_loop(UpdateDrawFrame, 0, 1);
#else
    SetTargetFPS(60);
    while (!WindowShouldClose())
    {
        UpdateDrawFrame();
    }
#endif

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
    BuildingPlace(BUILDING_BASE, PLAYER_TEAM, playerBase);
    int aiBaseId = BuildingPlace(BUILDING_BASE, AI_TEAM, aiBase);

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
    SpawnUnitsNear((Vector2){ playerBase.x + 60.0f, playerBase.y + below*2.5f }, START_ARMY/2, UNIT_RANGED, PLAYER_TEAM);
    SpawnUnitsNear((Vector2){ aiBase.x, aiBase.y + below }, AI_WORKERS, UNIT_WORKER, AI_TEAM);

    AiInit(playerBase, aiBase, aiBaseId);
}

static void UpdateDrawFrame(void)
{
    float frameTime = GetFrameTime();
    if (frameTime > MAX_FRAME_TIME) frameTime = MAX_FRAME_TIME;

    // Per-frame: things that should feel instant.
    CamUpdate(frameTime);
    InputUpdate();
    if (IsKeyPressed(KEY_F1)) AiSpawnWave(AI_WAVE_SIZE);   // debug: test wave
    PathUpdate();   // budgeted: leftover requests wait for the next frame

    // Fixed ticks: run as many as the elapsed time covers (0, 1 or several).
    tickAccumulator += frameTime;
    while (tickAccumulator >= TICK_DT)
    {
        double start = GetTime();
        UnitsTick();
        BuildingsTick();
        CombatProjectilesTick();
        AiTick();
        GridRebuild();
        lastTickMs = (GetTime() - start)*1000.0;
        tickAccumulator -= TICK_DT;
    }
    float alpha = (float)(tickAccumulator/TICK_DT);

    BeginDrawing();
        ClearBackground(BLACK);
        BeginMode2D(gameCamera);
            Rectangle view = CamViewRect();
            MapDraw(view);
            EconomyDrawNodes(view);
            BuildingsDraw(view);
            UnitsDraw(view, alpha);
            CombatProjectilesDraw(view, alpha);
            InputDraw();
        EndMode2D();
        EconomyDrawHud(PLAYER_TEAM);
        InputDrawHud();
        DrawOverlay();
    EndDrawing();

    if (GetTime() >= nextPerfLog)
    {
        TraceLog(LOG_INFO, "PERF: %d FPS | %d units | %d projectiles | tick %.2f ms | paths queued %d", GetFPS(), UnitsActiveCount(), CombatProjectileCount(), lastTickMs, PathQueueLength());
        nextPerfLog += PERF_LOG_EVERY;
    }
}

static void DrawOverlay(void)
{
    DrawRectangle(0, 0, 330, 116, Fade(BLACK, 0.6f));
    DrawFPS(10, 8);
    DrawText(TextFormat("Units: %d   Projectiles: %d", UnitsActiveCount(), CombatProjectileCount()), 10, 32, 16, RAYWHITE);
    DrawText(TextFormat("Sim tick: %.2f ms", lastTickMs), 10, 52, 16, RAYWHITE);
    DrawText(TextFormat("Paths queued: %d   Path: %.2f ms", PathQueueLength(), PathLastFrameMs()), 10, 72, 16, RAYWHITE);
    DrawText(TextFormat("AI gold: %d", EconomyGold(AI_TEAM)), 10, 92, 16, RAYWHITE);

    DrawText("Arrows/MMB: pan  Wheel: zoom  LMB: select  Shift: add  RMB: move / attack / mine gold", 10, GetScreenHeight() - 44, 16, RAYWHITE);
    DrawText("A+RMB: attack-move  S: stop  H: hold  Base selected - W: train worker  F1: enemy wave", 10, GetScreenHeight() - 24, 16, RAYWHITE);
}
