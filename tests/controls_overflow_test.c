// controls_overflow_test.c - Draws the Controls page (both tabs) at several
// window sizes and fails if any string runs outside the box it belongs to.
//
//   make test        (or: cmake -S tests -B tests/build && cmake --build tests/build
//                         && ./tests/build/controls_overflow_test)
//
// How it works:
//   - menu.c is #included below, so the test can set the page's tab and scroll
//     position directly. Before including it, the ui.c calls menu.c makes are
//     renamed to Test* wrappers that remember the BOX each string is meant to
//     fit (a table column, a button, a tab, the panel) and then call the real
//     function. Nothing in the page's own code is replaced.
//   - text_hooks.h (force-included into ui.c and this file) records every
//     string raylib is asked to draw, with its rectangle measured in the real
//     font, and the clipping rectangle in force at the time.
//   - Each tab is drawn at scroll positions from top to bottom (1/8 of the
//     visible height apart), so rows outside the view are checked when they
//     scroll into it.
// Checks for every drawn string: inside its box (left/right), inside the
// window, and (inside the scroll area) not cut off at the top when scrolled
// to the top or at the bottom when scrolled to the bottom. Then: every row
// was drawn in full (all its words appeared), the two columns don't overlap,
// and every unit and building name from the tables appeared.
//
// The build makes a second test program, controls_overflow_test_longnames,
// from a copy of config.h whose longest names are made much longer (see
// CMakeLists.txt), to show the page copes when buyers rename things.
//
// Set CONTROLS_TEST_SHOTS=<folder> to also save a PNG of every page drawn.
// Needs a display (the window stays hidden); on a server use xvfb-run.

#include "text_hooks.h"
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- The boxes strings must fit -------------------------------------------------------
typedef enum { BOX_PANEL, BOX_BUTTON, BOX_TAB, BOX_CELL } BoxKind;
static const char *BOX_NAMES[] = { "panel", "button", "tab", "table cell" };

typedef struct Drawn {
    char      text[128];
    Rectangle r;          // where it was drawn, measured
    BoxKind   kind;
    Rectangle box;        // the box it must fit
    int       cell;       // BOX_CELL: which cell (index into cells[])
    bool      clipped;    // drawn inside a scroll area...
    Rectangle clip;       // ...this one
} Drawn;

#define MAX_DRAWN 512
#define MAX_CELLS 256
static Drawn drawn[MAX_DRAWN];
static int   drawnCount;

// Cells (UiTextWrapped calls) of the current page: their text and box.
typedef struct Cell { char text[128]; Rectangle box; } Cell;
static Cell cells[MAX_CELLS];
static int  cellCount;

static BoxKind   curKind = BOX_PANEL;
static Rectangle curBox;
static int       curCell = -1;
static Rectangle tabsRect;     // UiTabs: the k-th string drawn is tab k
static int       tabsCount, tabsDrawn;
static Rectangle panelRect;
static bool      scissorOn;
static Rectangle scissorRect;
static Rectangle scrollArea;   // the rows' scroll area (last UiScrollBegin)
static float     scrollMax;    // how far it can scroll

// --- Wrappers around the ui.c functions menu.c uses -------------------------------------
float TestTextWrapped(const char *text, float x, float y, float width, float size, Color color);
bool  TestButton(Rectangle r, const char *label, int hotkey);
bool  TestTabs(Rectangle r, const char **labels, int count, int *active);
void  TestLabel(const char *text, float x, float y, float size, Color color);
void  TestPanel(Rectangle r);
float TestScrollBegin(Rectangle area, float contentHeight, float *scroll);

// Plain name swaps (not function-like macros): a (Rectangle){ a, b, c, d } argument has commas a macro would split.
#define UiTextWrapped TestTextWrapped
#define UiButton      TestButton
#define UiTabs        TestTabs
#define UiLabel       TestLabel
#define UiPanel       TestPanel
#define UiScrollBegin TestScrollBegin
#include "menu.c"
#undef UiTextWrapped
#undef UiButton
#undef UiTabs
#undef UiLabel
#undef UiPanel
#undef UiScrollBegin

float TestTextWrapped(const char *text, float x, float y, float width, float size, Color color)
{
    if (cellCount < MAX_CELLS)
    {
        snprintf(cells[cellCount].text, sizeof(cells[cellCount].text), "%s", text);
        cells[cellCount].box = (Rectangle){ x, y, width, 0.0f };
        curCell = cellCount++;
    }
    curKind = BOX_CELL;
    curBox = (Rectangle){ x, y, width, 0.0f };
    float h = UiTextWrapped(text, x, y, width, size, color);
    if (curCell >= 0) cells[curCell].box.height = h;
    curKind = BOX_PANEL; curBox = panelRect; curCell = -1;
    return h;
}

bool TestButton(Rectangle r, const char *label, int hotkey)
{
    curKind = BOX_BUTTON; curBox = r;
    bool b = UiButton(r, label, hotkey);
    curKind = BOX_PANEL; curBox = panelRect;
    return b;
}

bool TestTabs(Rectangle r, const char **labels, int count, int *active)
{
    curKind = BOX_TAB; tabsRect = r; tabsCount = count; tabsDrawn = 0;
    bool b = UiTabs(r, labels, count, active);
    curKind = BOX_PANEL; curBox = panelRect;
    return b;
}

void TestLabel(const char *text, float x, float y, float size, Color color)
{
    curKind = BOX_PANEL; curBox = panelRect;
    UiLabel(text, x, y, size, color);
}

void TestPanel(Rectangle r)
{
    panelRect = r; curBox = r;
    UiPanel(r);
}

float TestScrollBegin(Rectangle area, float contentHeight, float *scroll)
{
    scrollArea = area;
    scrollMax = (contentHeight > area.height) ? contentHeight - area.height : 0.0f;
    return UiScrollBegin(area, contentHeight, scroll);
}

// --- The raylib hooks (text_hooks.h) ------------------------------------------------------
static int testW = 1280, testH = 720;
int     TestScreenWidth(void)  { return testW; }
int     TestScreenHeight(void) { return testH; }
Vector2 TestMousePosition(void) { return (Vector2){ -1000.0f, -1000.0f }; }   // nothing hovered
float   TestMouseWheelMove(void) { return 0.0f; }
bool    TestMouseButton(int button) { (void)button; return false; }
bool    TestKey(int key) { (void)key; return false; }

#undef BeginScissorMode
#undef EndScissorMode
void TestBeginScissorMode(int x, int y, int width, int height)
{
    scissorOn = true;
    scissorRect = (Rectangle){ (float)x, (float)y, (float)width, (float)height };
    BeginScissorMode(x, y, width, height);
}

void TestEndScissorMode(void)
{
    scissorOn = false;
    EndScissorMode();
}

static void Record(const char *text, Rectangle r)
{
    if (drawnCount >= MAX_DRAWN || text[0] == '\0') return;
    Drawn *d = &drawn[drawnCount++];
    snprintf(d->text, sizeof(d->text), "%s", text);
    d->r = r;
    d->kind = curKind;
    d->box = curBox;
    d->cell = curCell;
    if (curKind == BOX_TAB)   // the k-th label belongs to the k-th tab
    {
        float w = tabsRect.width/tabsCount;
        d->box = (Rectangle){ tabsRect.x + tabsDrawn*w, tabsRect.y, w, tabsRect.height };
        tabsDrawn++;
    }
    d->clipped = scissorOn;
    d->clip = scissorRect;
}

#undef DrawText
#undef DrawTextEx
void TestDrawText(const char *text, int x, int y, int size, Color color)
{
    Record(text, (Rectangle){ (float)x, (float)y, (float)MeasureText(text, size), (float)size });
    DrawText(text, x, y, size, color);
}

void TestDrawTextEx(Font font, const char *text, Vector2 pos, float size, float spacing, Color color)
{
    Record(text, (Rectangle){ pos.x, pos.y, MeasureTextEx(font, text, size, spacing).x, size });
    DrawTextEx(font, text, pos, size, spacing, color);
}

// --- Checks ---------------------------------------------------------------------------------
#define EPS 0.5f   // half a pixel of rounding is fine

static int failures = 0;
static char where[96];   // "1280x720 Keyboard tab, scroll 120"

static void Fail(const char *what)
{
    failures++;
    if (failures <= 40) printf("  FAIL %s: %s\n", where, what);
}

// Covered rows: every cell text seen in full on some scroll position.
#define MAX_SEEN 256
static char seen[MAX_SEEN][128];
static int  seenCount;

static void MarkSeen(const char *text)
{
    for (int i = 0; i < seenCount; i++) if (strcmp(seen[i], text) == 0) return;
    if (seenCount < MAX_SEEN) snprintf(seen[seenCount++], sizeof(seen[0]), "%.127s", text);
}

static bool WasSeen(const char *text)
{
    for (int i = 0; i < seenCount; i++) if (strcmp(seen[i], text) == 0) return true;
    return false;
}

static bool SeenContaining(const char *part)
{
    for (int i = 0; i < seenCount; i++) if (strstr(seen[i], part)) return true;
    return false;
}

// "a  b" and "a b" are the same text: compare words only.
static void Words(const char *in, char *out, int size)
{
    int n = 0; bool space = true;
    for (const char *c = in; *c && n < size - 1; c++)
    {
        if (*c == ' ') { if (!space) out[n++] = ' '; space = true; }
        else { out[n++] = *c; space = false; }
    }
    while (n > 0 && out[n - 1] == ' ') n--;
    out[n] = '\0';
}

static void CheckFrame(bool atTop, bool atBottom)
{
    // The panel itself must be inside the window.
    if (panelRect.x < -EPS || panelRect.x + panelRect.width > testW + EPS)
        Fail(TextFormat("panel x %.1f..%.1f is outside the %d px wide window", panelRect.x, panelRect.x + panelRect.width, testW));

    for (int i = 0; i < drawnCount; i++)
    {
        const Drawn *d = &drawn[i];
        Rectangle r = d->r, b = d->box;
        // Text entirely outside its scroll area is clipped away (not visible): nothing to check.
        if (d->clipped && (r.y + r.height <= d->clip.y + EPS || r.y >= d->clip.y + d->clip.height - EPS)) continue;

        if (r.x < b.x - EPS || r.x + r.width > b.x + b.width + EPS)
            Fail(TextFormat("'%s' x %.1f..%.1f overflows its %s x %.1f..%.1f", d->text, r.x, r.x + r.width, BOX_NAMES[d->kind], b.x, b.x + b.width));
        if (d->kind != BOX_CELL && (r.y < b.y - EPS || r.y + r.height > b.y + b.height + EPS))
            Fail(TextFormat("'%s' y %.1f..%.1f overflows its %s y %.1f..%.1f", d->text, r.y, r.y + r.height, BOX_NAMES[d->kind], b.y, b.y + b.height));
        if (r.x < -EPS || r.x + r.width > testW + EPS || r.y < -EPS || r.y + r.height > testH + EPS)
            Fail(TextFormat("'%s' is outside the window", d->text));
        if (d->kind == BOX_CELL && (b.x < panelRect.x - EPS || b.x + b.width > panelRect.x + panelRect.width + EPS))
            Fail(TextFormat("the column of '%s' is outside the panel", d->text));
        if (d->clipped && (r.x < d->clip.x - EPS || r.x + r.width > d->clip.x + d->clip.width + EPS))
            Fail(TextFormat("'%s' is cut off at the side of its scroll area", d->text));
        // Partly visible rows are normal while scrolling, but not at the ends.
        if (d->clipped && atTop && r.y < d->clip.y - EPS)
            Fail(TextFormat("'%s' is cut off at the top of the scroll area while scrolled to the top", d->text));
        if (d->clipped && atBottom && r.y + r.height > d->clip.y + d->clip.height + EPS)
            Fail(TextFormat("'%s' is cut off at the bottom of the scroll area while scrolled to the bottom", d->text));
    }

    // Cells: all words of a cell were drawn (wrapping lost nothing); fully visible ones count as seen.
    for (int c = 0; c < cellCount; c++)
    {
        char joined[512] = "", want[256], got[512];
        bool visible = true;
        for (int i = 0; i < drawnCount; i++)
        {
            const Drawn *d = &drawn[i];
            if (d->cell != c) continue;
            if (strlen(joined) + strlen(d->text) + 2 < sizeof(joined)) { if (joined[0]) strcat(joined, " "); strcat(joined, d->text); }
            if (d->clipped && (d->r.y < d->clip.y - EPS || d->r.y + d->r.height > d->clip.y + d->clip.height + EPS)) visible = false;
        }
        Words(cells[c].text, want, sizeof(want));
        Words(joined, got, sizeof(got));
        // A word cut between letters is drawn as two pieces: compare without spaces too.
        char a[256], b2[512]; int na = 0, nb = 0;
        for (const char *p = want; *p; p++) if (*p != ' ') a[na++] = *p;
        for (const char *p = got; *p; p++) if (*p != ' ') b2[nb++] = *p;
        a[na] = b2[nb] = '\0';
        if (strcmp(a, b2) != 0) Fail(TextFormat("cell '%s' was drawn as '%s'", cells[c].text, got));
        else if (visible) MarkSeen(cells[c].text);
    }

    // The two columns of a row never overlap (cells come in pairs: input, action).
    for (int c = 0; c + 1 < cellCount; c += 2)
        if (cells[c].box.x + cells[c].box.width > cells[c + 1].box.x + EPS)
            Fail(TextFormat("columns overlap: '%s' and '%s'", cells[c].text, cells[c + 1].text));
}

static RenderTexture2D target;

static void DrawPage(int tab, float scroll)
{
    drawnCount = 0; cellCount = 0; curKind = BOX_PANEL; curCell = -1;
    controlsTab = tab;
    controlsScroll = scroll;
    UiBegin();   // also reloads the font for this window size
    BeginTextureMode(target);
    ClearBackground((Color){ 24, 30, 36, 255 });
    ControlsPage();
    EndTextureMode();
}

static void SaveShot(const char *name)
{
    const char *dir = getenv("CONTROLS_TEST_SHOTS");
    if (!dir) return;
    Image img = LoadImageFromTexture(target.texture);
    ImageFlipVertical(&img);
    ExportImage(img, TextFormat("%s/%s.png", dir, name));
    UnloadImage(img);
}

typedef struct { int w, h; const char *name; } Size;
static const Size SIZES[] = {
    {  800,  500, "800x500" },
    { 1024,  600, "1024x600" },
    { 1280,  720, "1280x720" },
    { 1920, 1080, "1920x1080" },
    {  320,  720, "narrow 320x720" },
    {  240,  800, "very narrow 240x800" },
};

int main(void)
{
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    SetTraceLogLevel(LOG_ERROR);   // raylib warns about every font size it loads; not useful here
    InitWindow(320, 240, "controls overflow test");
    UiFontLoad();

    static const char *TAB_NAMES[CONTROLS_CATEGORY_COUNT] = { "Mouse", "Keyboard" };
    for (int s = 0; s < (int)(sizeof(SIZES)/sizeof(SIZES[0])); s++)
    {
        testW = SIZES[s].w; testH = SIZES[s].h;
        target = LoadRenderTexture(testW, testH);
        int before = failures, pages = 0, strings = 0;
        seenCount = 0;
        for (int tab = 0; tab < CONTROLS_CATEGORY_COUNT; tab++)
        {
            DrawPage(tab, 0.0f);   // learn how far this tab scrolls
            // Steps of 1/8 of the visible height: any row up to 7/8 of it tall is fully
            // in view at some step; a row too tall to ever be seen whole fails below.
            float max = scrollMax, step = scrollArea.height/8.0f;
            if (step < 1.0f) step = 1.0f;
            for (float pos = 0.0f;; pos += step)
            {
                if (pos > max) pos = max;
                DrawPage(tab, pos);
                snprintf(where, sizeof(where), "%s %s tab, scroll %.0f of %.0f", SIZES[s].name, TAB_NAMES[tab], pos, max);
                CheckFrame(pos <= 0.0f, pos >= max);
                pages++; strings += drawnCount;
                if (pos == 0.0f) SaveShot(TextFormat("controls_%dx%d_%s", testW, testH, TAB_NAMES[tab]));
                if (pos >= max) { if (max > 0.0f) SaveShot(TextFormat("controls_%dx%d_%s_bottom", testW, testH, TAB_NAMES[tab])); break; }
            }
        }

        // Every row of both tabs was seen in full somewhere, including the longest names.
        static ControlsRowText rows[CONTROLS_ROWS_MAX];
        snprintf(where, sizeof(where), "%s", SIZES[s].name);
        for (int tab = 0; tab < CONTROLS_CATEGORY_COUNT; tab++)
        {
            int n = ControlsRows(tab, rows);
            for (int i = 0; i < n; i++)
            {
                if (!WasSeen(rows[i].input)) Fail(TextFormat("row '%s' was never fully visible", rows[i].input));
                if (!WasSeen(rows[i].action)) Fail(TextFormat("row '%s' was never fully visible", rows[i].action));
            }
        }
        for (int t = 0; t < UNIT_TYPE_COUNT; t++)
            if (UNIT_STATS[t].trainedAt != BUILDING_NONE && UNIT_STATS[t].hotkey != 0 && !SeenContaining(UNIT_STATS[t].name))
                Fail(TextFormat("unit name '%s' never appeared", UNIT_STATS[t].name));
        for (int t = 0; t < BUILDING_TYPE_COUNT; t++)
            if (BUILDING_STATS[t].cost > 0 && BUILDING_STATS[t].hotkey != 0 && !SeenContaining(BUILDING_STATS[t].name))
                Fail(TextFormat("building name '%s' never appeared", BUILDING_STATS[t].name));

        printf("%-22s %s  (%d page views, %d strings checked)\n", SIZES[s].name, failures == before ? "ok" : "FAILED", pages, strings);
        UnloadRenderTexture(target);
    }

    UiFontUnload();
    CloseWindow();
    if (failures > 40) printf("  ... and %d more\n", failures - 40);
    printf("%s: %d problem(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
