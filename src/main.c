// main.c - Window, game loop and debug overlay.
//
// Fixed timestep: the simulation (UnitsTick) runs exactly TICK_RATE times per
// second no matter the frame rate, so the game behaves the same on a slow
// laptop and a fast desktop. Rendering runs as often as the screen allows and
// blends units between their last two tick positions (alpha), so movement
// still looks smooth at 60+ FPS.

#include "raylib.h"
#include "config.h"
#include "camera.h"
#include "grid.h"
#include "input.h"
#include "map.h"
#include "path.h"
#include "units.h"

#if defined(__EMSCRIPTEN__)
    #include <emscripten/emscripten.h>
#endif

#define MAP_SEED        1234
#define START_UNITS_ROW 24       // starting units = ROW * ROW
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

// A square block of units in the middle of the map.
static void SpawnStartingUnits(void)
{
    float spacing = UNIT_RADIUS*3.0f;
    float start = -(START_UNITS_ROW - 1)*spacing*0.5f;
    for (int y = 0; y < START_UNITS_ROW; y++)
    {
        for (int x = 0; x < START_UNITS_ROW; x++)
        {
            UnitSpawn((Vector2){ MAP_PIXEL_W/2.0f + start + x*spacing, MAP_PIXEL_H/2.0f + start + y*spacing });
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
    PathUpdate();   // budgeted: leftover requests wait for the next frame

    // Fixed ticks: run as many as the elapsed time covers (0, 1 or several).
    tickAccumulator += frameTime;
    while (tickAccumulator >= TICK_DT)
    {
        double start = GetTime();
        UnitsTick();
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
            InputDrawSelectionBox();
        EndMode2D();
        DrawOverlay();
    EndDrawing();

    if (GetTime() >= nextPerfLog)
    {
        TraceLog(LOG_INFO, "PERF: %d FPS | %d units | tick %.2f ms | paths queued %d", GetFPS(), UnitsActiveCount(), lastTickMs, PathQueueLength());
        nextPerfLog += PERF_LOG_EVERY;
    }
}

static void DrawOverlay(void)
{
    DrawRectangle(0, 0, 330, 76, Fade(BLACK, 0.6f));
    DrawFPS(10, 8);
    DrawText(TextFormat("Units: %d   Tick: %.2f ms", UnitsActiveCount(), lastTickMs), 10, 32, 16, RAYWHITE);
    DrawText(TextFormat("Paths queued: %d   Path: %.2f ms", PathQueueLength(), PathLastFrameMs()), 10, 52, 16, RAYWHITE);

    const char *help = "WASD/Arrows/MMB: pan   Wheel: zoom   LMB: select   Shift: add   RMB: move";
    DrawText(help, 10, GetScreenHeight() - 24, 16, RAYWHITE);
}
