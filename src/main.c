// main.c - Window, game loop and debug overlay.
//
// Fixed timestep: the simulation (units, projectiles, AI) runs exactly TICK_RATE times per
// second no matter the frame rate, so the game behaves the same on a slow
// laptop and a fast desktop. Rendering runs as often as the screen allows and
// blends units between their last two tick positions (alpha), so movement
// still looks smooth at 60+ FPS.

#include "raylib.h"
#include "config.h"
#include "ai.h"
#include "camera.h"
#include "combat.h"
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
#define START_UNITS_ROW 10       // player's starting units = ROW * ROW, half melee / half ranged
#define AI_SPAWN_OFFSET (Vector2){ 1100.0f, -800.0f }   // AI spawn point, relative to the player's start
#define MAX_FRAME_TIME  0.25f    // after a long stall, don't try to catch up more than this
#define PERF_LOG_EVERY  5.0      // seconds between performance lines in the console

static double tickAccumulator = 0.0;   // unsimulated time carried over between frames
static double lastTickMs = 0.0;        // CPU time of the most recent tick
static double nextPerfLog = PERF_LOG_EVERY;

static void SpawnStartingUnits(void);
static void UpdateDrawFrame(void);
static void DrawOverlay(void);

int main(void)
{
    InitWindow(SCREEN_W, SCREEN_H, "RTS Kit");

    MapGenerate(MAP_SEED);
    CamInit();
    Vector2 playerBase = { MAP_PIXEL_W/2.0f, MAP_PIXEL_H/2.0f };
    AiInit(playerBase, Vector2Add(playerBase, AI_SPAWN_OFFSET));
    SpawnStartingUnits();
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

// A square block of player units in the middle of the map: melee in front
// (top rows), ranged behind.
static void SpawnStartingUnits(void)
{
    float spacing = UNIT_RADIUS*3.0f;
    float start = -(START_UNITS_ROW - 1)*spacing*0.5f;
    for (int y = 0; y < START_UNITS_ROW; y++)
    {
        for (int x = 0; x < START_UNITS_ROW; x++)
        {
            UnitType type = (y < START_UNITS_ROW/2) ? UNIT_MELEE : UNIT_RANGED;
            UnitSpawn((Vector2){ MAP_PIXEL_W/2.0f + start + x*spacing, MAP_PIXEL_H/2.0f + start + y*spacing }, type, PLAYER_TEAM);
        }
    }
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
            UnitsDraw(view, alpha);
            CombatProjectilesDraw(view, alpha);
            InputDrawSelectionBox();
        EndMode2D();
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
    DrawRectangle(0, 0, 330, 96, Fade(BLACK, 0.6f));
    DrawFPS(10, 8);
    DrawText(TextFormat("Units: %d   Projectiles: %d", UnitsActiveCount(), CombatProjectileCount()), 10, 32, 16, RAYWHITE);
    DrawText(TextFormat("Sim tick: %.2f ms", lastTickMs), 10, 52, 16, RAYWHITE);
    DrawText(TextFormat("Paths queued: %d   Path: %.2f ms", PathQueueLength(), PathLastFrameMs()), 10, 72, 16, RAYWHITE);

    const char *help = "WASD/Arrows/MMB: pan   Wheel: zoom   LMB: select   Shift: add   RMB: move / attack   F1: enemy wave";
    DrawText(help, 10, GetScreenHeight() - 24, 16, RAYWHITE);
}
