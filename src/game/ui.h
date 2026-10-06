// ui.h - Tiny immediate-mode UI: buttons, panels, labels, tabs.
#ifndef UI_H_INCLUDED
#define UI_H_INCLUDED

#include "raylib.h"
#include <stdbool.h>

void  UiBegin(void);          // once per frame, at the very start (before input is read)
float UiScale(void);          // window height / 720: multiply UI sizes by this
float Ui(float v);            // shorthand for v * UiScale()
bool  UiWantsMouse(void);     // mouse is over UI: the game world should ignore clicks / wheel

void UiPanel(Rectangle r);                                         // dark box that blocks the mouse
void UiLabel(const char *text, float x, float y, float size, Color color);
bool UiButton(Rectangle r, const char *label, int hotkey);         // true when clicked or hotkey pressed (0 = none)
bool UiButtonEx(Rectangle r, const char *label, int hotkey, bool dimmed);   // dimmed: greyed out (still clickable)
bool UiToggle(Rectangle r, const char *label, bool on);            // button that shows when it's the chosen one
bool UiTabs(Rectangle r, const char **labels, int count, int *active);   // true when the tab changed

// Scrollable area: everything drawn between Begin and End is clipped to `area`.
// Add the returned offset to your content's y positions. The mouse wheel
// scrolls it while the cursor is over it; a scrollbar shows if content is taller.
float UiScrollBegin(Rectangle area, float contentHeight, float *scroll);
void  UiScrollEnd(void);

// Text box: click to type, Enter / Esc / clicking elsewhere finishes. Returns
// true when the text changed. digitsOnly accepts 0-9 only.
bool UiTextField(Rectangle r, char *text, int capacity, bool digitsOnly);
bool UiWantsKeyboard(void);       // a text field is being typed in: ignore game/editor shortcuts
void UiClearFocus(void);          // stop typing (e.g. when the screen with the field closes)

const char *UiKeyName(int key);   // "A", "F1", "Esc", ...

void UiShowMessage(const char *text);   // short feedback line, e.g. "Not enough gold" (text is copied)
void UiShowMessageFor(const char *text, double seconds);   // same, for longer messages (map errors)
void UiDrawMessage(void);               // draws it while it's fresh; once per frame

#endif
