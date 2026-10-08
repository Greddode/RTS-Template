// menu.c - Main menu, pause menu and the Controls page.
//
// Built from ui.c helpers. Each function draws its menu and returns the
// player's choice; main.c decides what that means (start a game, resume...).
// The Controls page is generated from CONTROLS in config.h.
// Exit buttons don't exist in the web build: a browser tab can't quit.
//
// Play opens a map picker: "Random" plus every .map file in the maps folder.
// The list is rebuilt from the folder each time it opens (MapFileScan), so a
// new map file shows up without any code change.

#include "menu.h"
#include "config.h"
#include "fog.h"
#include "mapfile.h"
#include "ui.h"
#include <stddef.h>
#include <stdio.h>

#define MENU_W        360.0f   // reference sizes (720 px tall window), scaled by Ui()
#define BUTTON_H      48.0f
#define BUTTON_GAP    12.0f
#define CONTROLS_W    760.0f
#define CONTROLS_H    560.0f
#define CONTROLS_TEXT     18.0f   // Controls rows: normal text size...
#define CONTROLS_MIN_TEXT 16.0f   // ...shrunk at most to this, then wrapped
#define CONTROLS_SPLIT    0.42f   // input column's share of the width
#define CONTROLS_GAP      16.0f   // between the two columns
#define CONTROLS_ROW_GAP  6.0f    // between rows

#define SCREEN_MARGIN 10.0f    // panels never get closer than this to the window edge

#define PICKER_LIST_H 300.0f

static bool showingControls = false;
static bool showingPicker = false;
static float pickerScroll = 0.0f;
static int chosenMap = -1;   // index into the scanned list, -1 = Random
static int mapCount = 0;     // maps found when the picker opened
static bool testPlay = false;   // playing the editor's map: "Main Menu" buttons go back to the editor

static float FitWidth(float w);
static int  controlsTab = CONTROLS_MOUSE;
static float controlsScroll = 0.0f;

void MenuOpen(void)
{
    showingControls = false;
    showingPicker = false;
}

const char *MenuChosenMap(void)
{
    return (chosenMap < 0) ? NULL : MapFilePath(chosenMap);
}

// Returns true when a map was chosen (chosenMap set); Back/Esc closes the page.
// The map list page, shared by Play and the editor's Load (MenuPickMap).
// Returns true when a map was picked (chosenMap set: -1 = Random).
static bool MapPickerPage(bool allowRandom)
{
    int count = mapCount;
    int first = allowRandom ? -1 : 0;
    float w = FitWidth(Ui(MENU_W*1.3f)), listH = Ui(PICKER_LIST_H);
    float h = Ui(90.0f) + listH + Ui(BUTTON_GAP + BUTTON_H + 24.0f);
    Rectangle panel = { (GetScreenWidth() - w)*0.5f, (GetScreenHeight() - h)*0.5f, w, h };
    UiPanel(panel);
    const char *title = "Choose a map";
    float tw = UiTextWidth(title, Ui(32.0f));
    UiLabel(title, panel.x + (w - tw)*0.5f, panel.y + Ui(20.0f), Ui(32.0f), RAYWHITE);

    Rectangle list = { panel.x + Ui(30.0f), panel.y + Ui(76.0f), w - Ui(60.0f), listH };
    float rowH = Ui(BUTTON_H) + Ui(BUTTON_GAP);
    float offset = UiScrollBegin(list, (count - first)*rowH - Ui(BUTTON_GAP), &pickerScroll);
    bool picked = false;
    for (int i = first; i < count; i++)   // -1 = Random
    {
        Rectangle b = { list.x, list.y + offset + (i - first)*rowH, list.width - Ui(12.0f), Ui(BUTTON_H) };
        if (UiButton(b, (i < 0) ? "Random" : MapFileName(i), 0)) { chosenMap = i; picked = true; }
    }
    UiScrollEnd();

    Rectangle back = { panel.x + w - Ui(184.0f), panel.y + h - Ui(BUTTON_H) - Ui(16.0f), Ui(160.0f), Ui(BUTTON_H) };
    if (UiButton(back, "Back", KEY_ESCAPE)) showingPicker = false;
    return picked;
}

// A centred panel with a title and room for `buttons` buttons; returns the
// rectangle of the first button (the rest go below it).
// Panels are sized by window height (Ui), but never wider than the window:
// a narrow window would otherwise cut them off.
static float FitWidth(float w)
{
    float max = GetScreenWidth() - 2.0f*Ui(SCREEN_MARGIN);
    return (w < max) ? w : max;
}

static Rectangle MenuFrame(const char *title, int buttons)
{
    float w = FitWidth(Ui(MENU_W)), h = Ui(90.0f + buttons*(BUTTON_H + BUTTON_GAP));
    Rectangle panel = { (GetScreenWidth() - w)*0.5f, (GetScreenHeight() - h)*0.5f, w, h };
    UiPanel(panel);
    float tw = UiTextWidth(title, Ui(36.0f));
    UiLabel(title, panel.x + (w - tw)*0.5f, panel.y + Ui(20.0f), Ui(36.0f), RAYWHITE);
    return (Rectangle){ panel.x + Ui(30.0f), panel.y + Ui(80.0f), w - Ui(60.0f), Ui(BUTTON_H) };
}

static Rectangle NextButton(Rectangle b)
{
    b.y += b.height + Ui(BUTTON_GAP);
    return b;
}

// --- Controls page ------------------------------------------------------------------
// Every row is measured: if a cell is too wide for its column, the whole table
// is drawn smaller (down to CONTROLS_MIN_TEXT), and what still doesn't fit is
// word-wrapped. The rows sit in a
// scroll area, so a long list (or a short window) never runs past the panel.

#define CONTROLS_ROWS_MAX (CONTROLS_COUNT + UNIT_TYPE_COUNT + BUILDING_TYPE_COUNT)

typedef struct ControlsRowText { char input[96]; char action[96]; } ControlsRowText;

// The rows of the chosen tab: CONTROLS, plus (Keyboard) the Train and Build hotkeys from the tables.
static int ControlsRows(int tab, ControlsRowText *rows)
{
    int n = 0;
    for (int i = 0; i < CONTROLS_COUNT; i++)
    {
        const ControlInfo *c = &CONTROLS[i];
        if ((int)c->category != tab) continue;
        snprintf(rows[n].input, sizeof(rows[n].input), "%s", (c->key != 0) ? TextFormat(c->input, UiKeyName(c->key)) : c->input);
        snprintf(rows[n].action, sizeof(rows[n].action), "%s", c->action);
        n++;
    }
    if (tab != CONTROLS_KEYBOARD) return n;
    for (int t = 0; t < UNIT_TYPE_COUNT; t++)
    {
        const UnitStats *u = &UNIT_STATS[t];
        if (u->trainedAt == BUILDING_NONE || u->hotkey == 0) continue;
        snprintf(rows[n].input, sizeof(rows[n].input), "%s (%s selected)", UiKeyName(u->hotkey), BUILDING_STATS[u->trainedAt].name);
        snprintf(rows[n].action, sizeof(rows[n].action), "Train %s", u->name);
        n++;
    }
    for (int t = 0; t < BUILDING_TYPE_COUNT; t++)
    {
        const BuildingStats *b = &BUILDING_STATS[t];
        if (b->cost <= 0 || b->hotkey == 0) continue;
        snprintf(rows[n].input, sizeof(rows[n].input), "%s (workers selected)", UiKeyName(b->hotkey));
        snprintf(rows[n].action, sizeof(rows[n].action), "Build %s", b->name);
        n++;
    }
    return n;
}

// One cell at `size`, wrapped to its column. Returns its height.
static float ControlsCell(const char *text, float x, float y, float width, float size, bool draw, Color color)
{
    return draw ? UiTextWrapped(text, x, y, width, size, color) : UiTextWrappedHeight(text, width, size);
}

// Returns true when the player leaves the page (Back button or Esc).
static bool ControlsPage(void)
{
    float w = FitWidth(Ui(CONTROLS_W)), h = Ui(CONTROLS_H);
    Rectangle panel = { (GetScreenWidth() - w)*0.5f, (GetScreenHeight() - h)*0.5f, w, h };
    UiPanel(panel);
    UiLabel("Controls", panel.x + Ui(24.0f), panel.y + Ui(18.0f), UiFitSize("Controls", w - Ui(48.0f), Ui(32.0f), Ui(CONTROLS_MIN_TEXT)), RAYWHITE);

    static const char *tabs[CONTROLS_CATEGORY_COUNT] = { "Mouse", "Keyboard" };
    if (UiTabs((Rectangle){ panel.x + Ui(24.0f), panel.y + Ui(64.0f), w - Ui(48.0f), Ui(36.0f) }, tabs, CONTROLS_CATEGORY_COUNT, &controlsTab))
        controlsScroll = 0.0f;   // a new tab starts at its top

    float backW = Ui(160.0f);
    if (backW > w - Ui(48.0f)) backW = w - Ui(48.0f);
    Rectangle back = { panel.x + w - Ui(24.0f) - backW, panel.y + h - Ui(64.0f), backW, Ui(BUTTON_H) };

    // Rows: a scroll area between the tabs and the Back button. Two columns,
    // input (gold) and action (white), with a gap between them.
    Rectangle area = { panel.x + Ui(24.0f), panel.y + Ui(112.0f), w - Ui(48.0f), back.y - Ui(10.0f) - (panel.y + Ui(112.0f)) };
    float inner = area.width - Ui(8.0f) - Ui(12.0f);   // padding left, room for the scrollbar right
    float inputX = area.x + Ui(8.0f), inputW = inner*CONTROLS_SPLIT - Ui(CONTROLS_GAP)*0.5f;
    float actionX = inputX + inner*CONTROLS_SPLIT + Ui(CONTROLS_GAP)*0.5f, actionW = inner*(1.0f - CONTROLS_SPLIT) - Ui(CONTROLS_GAP)*0.5f;

    static ControlsRowText rows[CONTROLS_ROWS_MAX];
    static float rowH[CONTROLS_ROWS_MAX];
    int count = ControlsRows(controlsTab, rows);
    // One text size for the whole table (mixed sizes look messy): the largest
    // at which every cell fits on one line, but not below CONTROLS_MIN_TEXT.
    float size = Ui(CONTROLS_TEXT);
    for (int i = 0; i < count; i++)
    {
        size = UiFitSize(rows[i].input, inputW, size, Ui(CONTROLS_MIN_TEXT));
        size = UiFitSize(rows[i].action, actionW, size, Ui(CONTROLS_MIN_TEXT));
    }
    float contentH = 0.0f;
    for (int i = 0; i < count; i++)   // measure first: the scroll area needs the total height
    {
        float hi = ControlsCell(rows[i].input, 0, 0, inputW, size, false, GOLD);
        float ha = ControlsCell(rows[i].action, 0, 0, actionW, size, false, RAYWHITE);
        rowH[i] = ((hi > ha) ? hi : ha) + Ui(CONTROLS_ROW_GAP);
        contentH += rowH[i];
    }

    float y = area.y + UiScrollBegin(area, contentH, &controlsScroll);
    for (int i = 0; i < count; i++)
    {
        if (y + rowH[i] >= area.y && y <= area.y + area.height)   // only rows that can be seen
        {
            ControlsCell(rows[i].input, inputX, y, inputW, size, true, GOLD);
            ControlsCell(rows[i].action, actionX, y, actionW, size, true, RAYWHITE);
        }
        y += rowH[i];
    }
    UiScrollEnd();

    return UiButton(back, "Back", KEY_ESCAPE);
}

MenuAction MenuMain(void)
{
    if (showingControls) { if (ControlsPage()) showingControls = false; return MENU_NONE; }
    if (showingPicker)
    {
        if (!MapPickerPage(true)) return MENU_NONE;
        showingPicker = false;
        return MENU_PLAY;
    }

#if defined(__EMSCRIPTEN__)
    Rectangle b = MenuFrame("RTS Kit", 3);
#else
    Rectangle b = MenuFrame("RTS Kit", 4);
#endif
    if (UiButton(b, "Play", KEY_ENTER)) { showingPicker = true; pickerScroll = 0.0f; mapCount = MapFileScan(); }   // scan once per opening
    b = NextButton(b);
    if (UiButton(b, "Map Editor", 0)) return MENU_EDITOR;
    b = NextButton(b);
    if (UiButton(b, "Controls", 0)) { showingControls = true; controlsScroll = 0.0f; }
#if !defined(__EMSCRIPTEN__)
    b = NextButton(b);
    if (UiButton(b, "Exit", 0)) return MENU_EXIT;
#endif
    UiLabel("Version " GAME_VERSION, Ui(10.0f), GetScreenHeight() - Ui(26.0f), Ui(16.0f), GRAY);   // bottom left
    return MENU_NONE;
}

MenuAction MenuPause(void)
{
    if (showingControls) { if (ControlsPage()) showingControls = false; return MENU_NONE; }

#if defined(__EMSCRIPTEN__)
    Rectangle b = MenuFrame("Paused", 4);
#else
    Rectangle b = MenuFrame("Paused", 5);
#endif
    if (UiButton(b, "Resume", KEY_PAUSE)) return MENU_RESUME;
    b = NextButton(b);
    if (UiButton(b, FogEnabled() ? "Fog of war: On" : "Fog of war: Off", 0)) FogSetEnabled(!FogEnabled());
    b = NextButton(b);
    if (UiButton(b, "Controls", 0)) { showingControls = true; controlsScroll = 0.0f; }
    b = NextButton(b);
    if (UiButton(b, testPlay ? "Back to Editor" : "Main Menu", 0)) return MENU_MAIN_MENU;
#if !defined(__EMSCRIPTEN__)
    b = NextButton(b);
    if (UiButton(b, "Exit", 0)) return MENU_EXIT;
#endif
    return MENU_NONE;
}

MenuAction MenuGameOver(bool victory)
{
    Rectangle b = MenuFrame(victory ? "Victory" : "Defeat", 2);
    if (UiButton(b, "Play Again", KEY_ENTER)) return MENU_PLAY;
    b = NextButton(b);
    if (UiButton(b, testPlay ? "Back to Editor" : "Main Menu", 0)) return MENU_MAIN_MENU;
    return MENU_NONE;
}

void MenuSetTestPlay(bool on)
{
    testPlay = on;
}

void MenuOpenMapPicker(void)
{
    showingPicker = true;
    pickerScroll = 0.0f;
    mapCount = MapFileScan();
}

int MenuPickMap(void)
{
    if (!MapPickerPage(false)) return showingPicker ? PICK_WAITING : PICK_CANCELLED;
    showingPicker = false;
    return chosenMap;
}
