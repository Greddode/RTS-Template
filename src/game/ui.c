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
//
// Text: every string in the game is drawn by UiLabel() and measured by
// UiTextWidth(), in one font (UI_FONT_FILE in assets/fonts). Nothing else
// calls raylib's DrawText / MeasureText, so swapping the font is one line.
// A font file is turned into a texture of pre-drawn letters at ONE pixel
// size; drawing much bigger or smaller than that looks blurry. So the font
// is loaded at sizes that follow the UI scale and reloaded when the window
// height changes. If the file is missing, raylib's built-in font is used.

#include "ui.h"
#include <math.h>
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

#define UI_FONT_FILE     "Inter-Regular.ttf"   // in assets/fonts; any .ttf / .otf works
#define UI_FONT_SIZE     20.0f   // reference sizes it's loaded at (scaled like the UI): most text is 15-22,
#define UI_FONT_TITLE    34.0f   // titles 24-36
#define UI_LINE_SPACING  1.25f   // wrapped text: line height = text size * this
#define TEXT_BOX_PAD     6.0f

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

bool UiHover(Rectangle r)
{
    return Hover(r);
}

// --- Font --------------------------------------------------------------------------
// Loaded twice: at the size of normal text and at title size. Each piece of
// text uses the copy nearest its size, so letters are never stretched far.

static const float FONT_SIZES[2] = { UI_FONT_SIZE, UI_FONT_TITLE };
static Font font[2];
static bool fontFromFile = false;   // false: raylib's default font
static int  fontPixels = 0;         // pixel size of font[0] (0 = nothing loaded); font[1] follows it

static void FontPath(char *out, int size)
{
#if defined(__EMSCRIPTEN__)
    snprintf(out, size, "/assets/fonts/%s", UI_FONT_FILE);
#else
  #if defined(FONTS_SOURCE_DIR)
    if (DirectoryExists(FONTS_SOURCE_DIR)) { snprintf(out, size, "%s/%s", FONTS_SOURCE_DIR, UI_FONT_FILE); return; }
  #endif
    snprintf(out, size, "%sassets/fonts/%s", GetApplicationDirectory(), UI_FONT_FILE);
#endif
}

static int PixelsFor(float referenceSize)
{
    int pixels = (int)lroundf(Ui(referenceSize));
    return (pixels < 8) ? 8 : pixels;
}

void UiFontUnload(void)
{
    if (fontFromFile) { UnloadFont(font[0]); UnloadFont(font[1]); }
    fontFromFile = false;
    fontPixels = 0;
}

void UiFontLoad(void)
{
    // Basic Latin (space .. ~) and Latin-1 (no-break space .. ÿ): English plus
    // the accented letters of most western European languages.
    static int codepoints[95 + 96];
    int n = 0;
    for (int c = 32; c <= 126; c++) codepoints[n++] = c;
    for (int c = 160; c <= 255; c++) codepoints[n++] = c;

    char path[512];
    FontPath(path, sizeof(path));
    UiFontUnload();
    fontPixels = PixelsFor(FONT_SIZES[0]);

    fontFromFile = FileExists(path);
    for (int i = 0; i < 2 && fontFromFile; i++)
    {
        font[i] = LoadFontEx(path, PixelsFor(FONT_SIZES[i]), codepoints, n);
        if (font[i].texture.id == 0 || font[i].glyphCount == 0) { fontFromFile = false; if (i == 1) UnloadFont(font[0]); }
        else SetTextureFilter(font[i].texture, TEXTURE_FILTER_BILINEAR);   // smooth when drawn a little bigger or smaller
    }
    if (!fontFromFile)
    {
        static bool warned = false;   // one log line, not one per window resize
        if (!warned) TraceLog(LOG_WARNING, "UI font: %s not found or unreadable, using raylib's default font", path);
        warned = true;
        font[0] = font[1] = GetFontDefault();
    }
}

// The loaded copy nearest to `size` (switch where the two meet, in proportion).
static Font FontFor(float size)
{
    float split = Ui(sqrtf(FONT_SIZES[0]*FONT_SIZES[1]));
    return font[(size > split) ? 1 : 0];
}

// raylib's default font is meant to be drawn with a gap of size/10 between
// letters; a real font file has its spacing built in.
static float Spacing(float size)
{
    return fontFromFile ? 0.0f : size/10.0f;
}

float UiTextWidth(const char *text, float size)
{
    if (fontPixels == 0) return (float)MeasureText(text, (int)size);   // before UiFontLoad (or after unload)
    return MeasureTextEx(FontFor(size), text, size, Spacing(size)).x;
}

void UiBegin(void)
{
    for (int i = 0; i < rectCount[0]; i++) rects[1][i] = rects[0][i];
    rectCount[1] = rectCount[0];
    rectCount[0] = 0;

    // The window height changed enough to change the font's pixel size: redraw its letters.
    if (fontPixels != 0 && PixelsFor(UI_FONT_SIZE) != fontPixels) UiFontLoad();
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
    if (fontPixels == 0) { DrawText(text, (int)x, (int)y, (int)size, color); return; }   // before UiFontLoad
    // Whole pixels: letters drawn between pixels come out blurry.
    DrawTextEx(FontFor(size), text, (Vector2){ roundf(x), roundf(y) }, size, Spacing(size), color);
}

// The largest size (at most `size`, at least `minSize`) at which `text` fits in `width`.
float UiFitSize(const char *text, float width, float size, float minSize)
{
    while (size > minSize && UiTextWidth(text, size) > width) size -= 1.0f;
    return (size < minSize) ? minSize : size;
}

// Shrinks the text if it's wider than the rectangle (long names in buttons).
static void CenteredText(Rectangle r, const char *text, float size, Color color)
{
    size = UiFitSize(text, r.width - Ui(8.0f), size, 8.0f);
    float w = UiTextWidth(text, size);
    UiLabel(text, r.x + (r.width - w)*0.5f, r.y + (r.height - size)*0.5f, size, color);
}

// --- Word wrap ---------------------------------------------------------------------
// Splits `text` into lines no wider than `width` (at spaces; a word longer than
// a whole line is cut after a '/' or '-' if it has one, else between letters)
// and draws them, or only measures when `draw` is false. Returns the height
// used. '\n' starts a new line.
#define WRAP_LINE_MAX 256

static float WrapText(const char *text, float x, float y, float width, float size, Color color, bool draw)
{
    float lineH = size*UI_LINE_SPACING;
    char line[WRAP_LINE_MAX];
    int lines = 0;
    const char *p = text;
    while (*p != '\0')
    {
        // Take words while the line still fits.
        int len = 0;           // characters of `line` in use
        const char *next = p;  // where the next line starts
        while (*next != '\0' && *next != '\n')
        {
            const char *wordEnd = next;
            while (*wordEnd == ' ') wordEnd++;
            while (*wordEnd != '\0' && *wordEnd != ' ' && *wordEnd != '\n') wordEnd++;
            int wordLen = (int)(wordEnd - next);
            if (len == 0 && wordLen >= WRAP_LINE_MAX) { wordLen = WRAP_LINE_MAX - 1; wordEnd = next + wordLen; }   // absurdly long word
            if (len + wordLen >= WRAP_LINE_MAX) break;
            memcpy(line + len, next, wordLen);
            line[len + wordLen] = '\0';
            if (UiTextWidth(line, size) > width)
            {
                if (len > 0) break;   // the line has words: this one goes on the next line
                // One word wider than the line: as many letters as fit (at least one).
                int fit = 1;
                while (fit < wordLen && (next[fit] & 0xC0) == 0x80) fit++;   // never split a UTF-8 letter (é is 2 bytes)
                while (fit < wordLen)
                {
                    int more = fit + 1;
                    while (more < wordLen && (next[more] & 0xC0) == 0x80) more++;
                    char keep = line[more];
                    line[more] = '\0';
                    bool fits = UiTextWidth(line, size) <= width;
                    line[more] = keep;
                    if (!fits) break;
                    fit = more;
                }
                // Nicer: break just after a '/' or '-' inside the part that fits ("attack-|move").
                for (int k = fit - 1; k > 0; k--)
                    if (next[k - 1] == '/' || next[k - 1] == '-') { fit = k; break; }
                wordEnd = next + fit;
                wordLen = fit;
            }
            len += wordLen;
            next = wordEnd;
        }
        line[len] = '\0';
        if (draw)
        {
            const char *shown = line;
            while (*shown == ' ') shown++;   // a wrapped line doesn't start with the space it broke at
            UiLabel(shown, x, y + lines*lineH, size, color);
        }
        lines++;
        while (*next == ' ') next++;
        if (*next == '\n') next++;
        p = next;
    }
    if (lines == 0) return 0.0f;
    return (lines - 1)*lineH + size;   // the last line needs only its own height
}

float UiTextWrapped(const char *text, float x, float y, float width, float size, Color color)
{
    return WrapText(text, x, y, width, size, color, true);
}

float UiTextWrappedHeight(const char *text, float width, float size)
{
    return WrapText(text, 0.0f, 0.0f, width, size, BLANK, false);
}

// Height of a UiTextBox `width` wide that shows all of `text`, but at most maxHeight.
float UiTextBoxHeight(const char *text, float width, float size, float maxHeight)
{
    float textW = width - Ui(TEXT_BOX_PAD)*2.0f - Ui(SCROLLBAR_W) - Ui(4.0f);
    float h = UiTextWrappedHeight(text, textW, size) + Ui(TEXT_BOX_PAD)*2.0f;
    return (h > maxHeight) ? maxHeight : h;
}

void UiTextBox(Rectangle r, const char *text, float size, Color color, float *scroll)
{
    UiPanel(r);
    Rectangle area = { r.x + Ui(TEXT_BOX_PAD), r.y + Ui(TEXT_BOX_PAD), r.width - Ui(TEXT_BOX_PAD)*2.0f, r.height - Ui(TEXT_BOX_PAD)*2.0f };
    float textW = area.width - Ui(SCROLLBAR_W) - Ui(4.0f);   // room for the scrollbar
    float offset = UiScrollBegin(area, UiTextWrappedHeight(text, textW, size), scroll);
    UiTextWrapped(text, area.x, area.y + offset, textW, size, color);
    UiScrollEnd();
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
        case KEY_LEFT_SHIFT: return "Shift";
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
    float size = UiFitSize(message, GetScreenWidth() - Ui(20.0f), Ui(22.0f), 10.0f);   // shrink to fit
    float w = UiTextWidth(message, size);
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
    UiLabel(shown, r.x + Ui(6.0f), r.y + (r.height - size)*0.5f, size, TEXT_COLOR);
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
