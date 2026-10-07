// overlay.c - The F3 debug overlay.
//
// F3 (KEY_DEBUG_OVERLAY) shows or hides it in every screen. While playing it
// lists FPS (now, and min / avg over the last OVERLAY_FPS_SECONDS), frame
// time, sim tick, fog and path costs, units and buildings per team,
// projectiles, the AI's state, and what drawing the overlay itself cost. In
// the menus, the pause menu and the editor it's a single FPS line.
//
// Hidden costs nothing: no counting, no drawing, only the F3 check. The
// performance line in the console (main.c) counts frames on its own, so its
// numbers don't depend on the overlay.
//
// It never blocks the mouse: the background is drawn directly, not as a UI
// panel, so clicks go through to the game. It sits top left, clear of the
// gold counter (top right), the minimap and the inspector (bottom).

#include "overlay.h"
#include "ai.h"
#include "buildings.h"
#include "combat.h"
#include "config.h"
#include "fog.h"
#include "minimap.h"
#include "path.h"
#include "ui.h"
#include "units.h"
#include "raylib.h"

#define OVERLAY_BG   (Color){ 15, 18, 22, 190 }
#define OVERLAY_W    430.0f   // reference sizes (720 px tall window), scaled by Ui()
#define LINE_H       20.0f
#define TEXT_SIZE    16.0f

static bool visible = DEBUG_OVERLAY_DEFAULT;

// Per-second FPS samples (a small ring), collected only while shown.
static int    secondFps[OVERLAY_FPS_SECONDS];
static int    samples = 0, nextSample = 0;
static int    framesThisSecond = 0;
static double secondStart = 0.0;

// Counts, refreshed every OVERLAY_REFRESH seconds.
static int    teamUnits[2], teamBuildings[2];
static double nextRefresh = 0.0;
static double lastDrawMs = 0.0;   // what drawing the overlay cost last frame

void OverlayUpdate(void)
{
    if (IsKeyPressed(KEY_DEBUG_OVERLAY) && !UiWantsKeyboard())
    {
        visible = !visible;
        samples = nextSample = framesThisSecond = 0;   // fresh numbers each time it opens
        secondStart = GetTime();
        nextRefresh = 0.0;
    }
    if (!visible) return;

    double now = GetTime();
    framesThisSecond++;
    if (now - secondStart >= 1.0)
    {
        secondFps[nextSample] = framesThisSecond;
        nextSample = (nextSample + 1) % OVERLAY_FPS_SECONDS;
        if (samples < OVERLAY_FPS_SECONDS) samples++;
        framesThisSecond = 0;
        secondStart = now;
    }
    if (now >= nextRefresh)   // a full pass over the pool, but only 4 times a second
    {
        nextRefresh = now + OVERLAY_REFRESH;
        teamUnits[PLAYER_TEAM] = teamUnits[AI_TEAM] = 0;
        for (int i = 0; i < MAX_UNITS; i++) if (units[i].active) teamUnits[units[i].team == AI_TEAM]++;
        teamBuildings[PLAYER_TEAM] = BuildingsCount(PLAYER_TEAM);
        teamBuildings[AI_TEAM] = BuildingsCount(AI_TEAM);
    }
}

static const char *MinAvgText(void)
{
    if (samples == 0) return "min/avg: wait 1 s";
    int lo = secondFps[0], sum = 0;
    for (int k = 0; k < samples; k++) { if (secondFps[k] < lo) lo = secondFps[k]; sum += secondFps[k]; }
    return TextFormat("min %d / avg %d over %d s", lo, sum/samples, samples);
}

void OverlayDrawFull(double tickMs)
{
    if (!visible) return;
    double start = GetTime();
    float x = Ui(10.0f), y = Ui(8.0f), size = Ui(TEXT_SIZE), line = Ui(LINE_H);
    DrawRectangleRec((Rectangle){ 0, 0, Ui(OVERLAY_W), Ui(8.0f)*2.0f + line*8.6f }, OVERLAY_BG);   // not a UiPanel: clicks pass through

    UiLabel(TextFormat("%d FPS   %s", GetFPS(), MinAvgText()), x, y, Ui(20.0f), LIME);
    y += line*1.3f;
    UiLabel(TextFormat("Frame %.2f ms   Sim tick %.2f ms   Fog %.2f ms", GetFrameTime()*1000.0f, tickMs, FogLastUpdateMs()), x, y, size, RAYWHITE);
    y += line;
    UiLabel(TextFormat("Path %.2f ms (queued %d)   Minimap %.2f ms   Overlay %.2f ms", PathLastFrameMs(), PathQueueLength(), MinimapLastRebuildMs(), lastDrawMs), x, y, size, RAYWHITE);
    y += line;
    UiLabel(TextFormat("Units: you %d, AI %d (%d of %d)   Projectiles %d", teamUnits[PLAYER_TEAM], teamUnits[AI_TEAM], UnitsActiveCount(), MAX_UNITS, CombatProjectileCount()), x, y, size, RAYWHITE);
    y += line;
    UiLabel(TextFormat("Buildings: you %d, AI %d", teamBuildings[PLAYER_TEAM], teamBuildings[AI_TEAM]), x, y, size, RAYWHITE);
    y += line;
    UiLabel(AiDebugLine(), x, y, size, RAYWHITE);
    y += line;
    UiLabel(TextFormat("AI: %s", AiStatus()), x, y, size, GOLD);
    y += line;
    UiLabel(TextFormat("%s: hide this overlay", UiKeyName(KEY_DEBUG_OVERLAY)), x, y, size, GRAY);

    lastDrawMs = (GetTime() - start)*1000.0;
}

void OverlayDrawFpsLine(void)
{
    if (!visible) return;
    const char *text = TextFormat("%d FPS  %.1f ms", GetFPS(), GetFrameTime()*1000.0f);
    float size = Ui(TEXT_SIZE);
    float w = (float)MeasureText(text, (int)size) + Ui(12.0f), h = size + Ui(8.0f);
    // Top right, under where the gold counter sits (pause / game over): clear of the
    // menus, the editor's panel (left) and its tile readout (bottom right).
    float x = GetScreenWidth() - w - Ui(6.0f), y = Ui(40.0f);
    DrawRectangleRec((Rectangle){ x, y, w, h }, OVERLAY_BG);
    UiLabel(text, x + Ui(6.0f), y + Ui(4.0f), size, LIME);
}
