// ui.c - Tiny immediate-mode UI, drawn with plain raylib shapes and text.
//
// "Immediate mode" means there are no widget objects: each frame you call
// UiButton(rect, ...) where you want a button, and it draws itself and tells
// you if it was clicked. Nothing to create or free.
//
// Scaling: all sizes are written for a 720 px tall window and multiplied by
// UiScale(), so the UI stays the same relative size at any window size.
//
// Blocking the game: every panel/button records its rectangle. The game's
// input code runs before the UI is drawn, so UiWantsMouse() checks the mouse
// against the rectangles recorded on the PREVIOUS frame. One frame late is
// fine: the UI doesn't move.

#include "ui.h"
#include <stdio.h>
#include <string.h>

#define REFERENCE_HEIGHT 720.0f
#define MAX_UI_RECTS     64

#define PANEL_COLOR      (Color){ 20, 22, 26, 225 }
#define PANEL_EDGE       (Color){ 90, 95, 105, 255 }
#define BUTTON_COLOR     (Color){ 50, 55, 65, 255 }
#define BUTTON_HOVER     (Color){ 75, 85, 100, 255 }
#define BUTTON_ACTIVE    (Color){ 95, 120, 70, 255 }
#define BUTTON_DIMMED    (Color){ 38, 40, 45, 255 }
#define TEXT_COLOR       RAYWHITE
#define TEXT_DIMMED      (Color){ 130, 130, 130, 255 }
#define MESSAGE_TIME     1.5   // seconds a message stays up
#define SCROLL_STEP      30.0f // pixels (reference) per wheel notch
#define SCROLLBAR_W      5.0f
#define SCROLLBAR_COLOR  (Color){ 130, 135, 145, 255 }

static Rectangle rects[2][MAX_UI_RECTS];   // [0] this frame, [1] last frame
static int       rectCount[2];

static bool      clipping = false;          // inside UiScrollBegin/End
static Rectangle clipRect;

// Is the mouse over `r`, and (inside a scroll area) over its visible part?
static bool Hover(Rectangle r)
{
    Vector2 m = GetMousePosition();
    return CheckCollisionPointRec(m, r) && (!clipping || CheckCollisionPointRec(m, clipRect));
}

void UiBegin(void)
{
    for (int i = 0; i < rectCount[0]; i++) rects[1][i] = rects[0][i];
    rectCount[1] = rectCount[0];
    rectCount[0] = 0;
}

float UiScale(void)
{
    return GetScreenHeight()/REFERENCE_HEIGHT;
}

float Ui(float v)
{
    return v*UiScale();
}

static void RecordRect(Rectangle r)
{
    if (rectCount[0] < MAX_UI_RECTS) rects[0][rectCount[0]++] = r;
}

bool UiWantsMouse(void)
{
    Vector2 m = GetMousePosition();
    for (int i = 0; i < rectCount[1]; i++)
    {
        if (CheckCollisionPointRec(m, rects[1][i])) return true;
    }
    return false;
}

void UiPanel(Rectangle r)
{
    RecordRect(r);
    DrawRectangleRec(r, PANEL_COLOR);
    DrawRectangleLinesEx(r, 1.0f, PANEL_EDGE);
}

void UiLabel(const char *text, float x, float y, float size, Color color)
{
    DrawText(text, (int)x, (int)y, (int)size, color);
}

// Shrinks the text if it's wider than the rectangle (long names in buttons).
static void CenteredText(Rectangle r, const char *text, float size, Color color)
{
    while (size > 10.0f && MeasureText(text, (int)size) > r.width - Ui(8.0f)) size -= 1.0f;
    int w = MeasureText(text, (int)size);
    DrawText(text, (int)(r.x + (r.width - w)*0.5f), (int)(r.y + (r.height - size)*0.5f), (int)size, color);
}

bool UiButtonEx(Rectangle r, const char *label, int hotkey, bool dimmed)
{
    RecordRect(r);
    bool hover = Hover(r);
    DrawRectangleRec(r, dimmed ? BUTTON_DIMMED : hover ? BUTTON_HOVER : BUTTON_COLOR);
    DrawRectangleLinesEx(r, 1.0f, PANEL_EDGE);
    float size = (r.height < Ui(40.0f)) ? Ui(16.0f) : Ui(20.0f);
    CenteredText(r, label, size, dimmed ? TEXT_DIMMED : TEXT_COLOR);

    bool clicked = hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    return clicked || (hotkey != 0 && IsKeyPressed(hotkey));
}

bool UiToggle(Rectangle r, const char *label, bool on)
{
    RecordRect(r);
    bool hover = Hover(r);
    DrawRectangleRec(r, on ? BUTTON_ACTIVE : hover ? BUTTON_HOVER : BUTTON_COLOR);
    DrawRectangleLinesEx(r, 1.0f, PANEL_EDGE);
    CenteredText(r, label, Ui(16.0f), TEXT_COLOR);
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

bool UiButton(Rectangle r, const char *label, int hotkey)
{
    return UiButtonEx(r, label, hotkey, false);
}

bool UiTabs(Rectangle r, const char **labels, int count, int *active)
{
    RecordRect(r);
    bool changed = false;
    float w = r.width/count;
    for (int i = 0; i < count; i++)
    {
        Rectangle tab = { r.x + i*w, r.y, w, r.height };
        bool hover = Hover(tab);
        DrawRectangleRec(tab, (i == *active) ? BUTTON_ACTIVE : hover ? BUTTON_HOVER : BUTTON_COLOR);
        DrawRectangleLinesEx(tab, 1.0f, PANEL_EDGE);
        CenteredText(tab, labels[i], Ui(18.0f), TEXT_COLOR);
        if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && i != *active) { *active = i; changed = true; }
    }
    return changed;
}

const char *UiKeyName(int key)
{
    static char name[8];
    if (key >= KEY_A && key <= KEY_Z) { snprintf(name, sizeof(name), "%c", 'A' + (key - KEY_A)); return name; }
    if (key >= KEY_F1 && key <= KEY_F12) { snprintf(name, sizeof(name), "F%d", 1 + (key - KEY_F1)); return name; }
    switch (key)
    {
        case KEY_ESCAPE: return "Esc";
        case KEY_LEFT_CONTROL: return "Ctrl";
        case KEY_SPACE:  return "Space";
        case KEY_ENTER:  return "Enter";
        case KEY_TAB:    return "Tab";
        default:         return "?";
    }
}

static char   message[256] = "";
static double messageUntil = -100.0;

void UiShowMessageFor(const char *text, double seconds)
{
    snprintf(message, sizeof(message), "%s", text);   // copy: TextFormat() results don't last
    messageUntil = GetTime() + seconds;
}

void UiShowMessage(const char *text)
{
    UiShowMessageFor(text, MESSAGE_TIME);
}

void UiDrawMessage(void)
{
    if (GetTime() >= messageUntil) return;
    float size = Ui(22.0f);
    while (size > 10.0f && MeasureText(message, (int)size) > GetScreenWidth() - Ui(20.0f)) size -= 1.0f;   // shrink to fit
    int w = MeasureText(message, (int)size);
    UiLabel(message, (GetScreenWidth() - w)*0.5f, Ui(44.0f), size, ORANGE);
}

float UiScrollBegin(Rectangle area, float contentHeight, float *scroll)
{
    RecordRect(area);
    float maxScroll = contentHeight - area.height;
    if (maxScroll < 0.0f) maxScroll = 0.0f;
    if (CheckCollisionPointRec(GetMousePosition(), area)) *scroll -= GetMouseWheelMove()*Ui(SCROLL_STEP);
    if (*scroll > maxScroll) *scroll = maxScroll;
    if (*scroll < 0.0f) *scroll = 0.0f;

    // Scrollbar on the right edge, only when there's something to scroll.
    if (maxScroll > 0.0f)
    {
        float barW = Ui(SCROLLBAR_W);
        float thumbH = area.height*area.height/contentHeight;
        float thumbY = area.y + (area.height - thumbH)*(*scroll/maxScroll);
        DrawRectangleRec((Rectangle){ area.x + area.width - barW, area.y, barW, area.height }, BUTTON_DIMMED);
        DrawRectangleRec((Rectangle){ area.x + area.width - barW, thumbY, barW, thumbH }, SCROLLBAR_COLOR);
    }

    BeginScissorMode((int)area.x, (int)area.y, (int)area.width, (int)area.height);
    clipping = true;
    clipRect = area;
    return -*scroll;
}

void UiScrollEnd(void)
{
    EndScissorMode();
    clipping = false;
}

static char *focusedField = NULL;   // the text field being typed in, if any

bool UiTextField(Rectangle r, char *text, int capacity, bool digitsOnly)
{
    RecordRect(r);
    bool hover = Hover(r);
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        if (hover) focusedField = text;
        else if (focusedField == text) focusedField = NULL;   // clicked elsewhere
    }

    bool focused = (focusedField == text), changed = false;
    if (focused)
    {
        int len = (int)strlen(text);
        for (int c = GetCharPressed(); c > 0; c = GetCharPressed())
        {
            bool ok = digitsOnly ? (c >= '0' && c <= '9') : (c >= 32 && c < 127);
            if (ok && len < capacity - 1) { text[len++] = (char)c; text[len] = '\0'; changed = true; }
        }
        if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && len > 0) { text[--len] = '\0'; changed = true; }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE)) focusedField = NULL;
    }

    DrawRectangleRec(r, focused ? BUTTON_HOVER : BUTTON_DIMMED);
    DrawRectangleLinesEx(r, 1.0f, focused ? TEXT_COLOR : PANEL_EDGE);
    float size = Ui(18.0f);
    const char *shown = (focused && ((int)(GetTime()*2.0) % 2 == 0)) ? TextFormat("%s_", text) : text;   // blinking cursor
    DrawText(shown, (int)(r.x + Ui(6.0f)), (int)(r.y + (r.height - size)*0.5f), (int)size, TEXT_COLOR);
    return changed;
}

bool UiWantsKeyboard(void)
{
    return focusedField != NULL;
}

void UiClearFocus(void)
{
    focusedField = NULL;
}
