// text_hooks.h - Force-included (-include) into ui.c and the test when building
// tests/controls_overflow_test. Never part of the game.
//
// These macros rename raylib calls inside those files only:
//   - the window size is whatever the test says (no real window of that size needed)
//   - every string drawn is recorded with its measured rectangle
//   - scissor (clipping) areas are recorded, so clipped text can be told apart
//   - the mouse and keyboard do nothing, so no button is hovered or pressed
// raylib.h comes first, so raylib itself still sees the real functions.

#ifndef TEXT_HOOKS_H
#define TEXT_HOOKS_H

#include "raylib.h"

int     TestScreenWidth(void);
int     TestScreenHeight(void);
void    TestDrawText(const char *text, int x, int y, int size, Color color);
void    TestDrawTextEx(Font font, const char *text, Vector2 pos, float size, float spacing, Color color);
void    TestBeginScissorMode(int x, int y, int width, int height);
void    TestEndScissorMode(void);
Vector2 TestMousePosition(void);
float   TestMouseWheelMove(void);
bool    TestMouseButton(int button);
bool    TestKey(int key);

#define GetScreenWidth        TestScreenWidth
#define GetScreenHeight       TestScreenHeight
#define DrawText              TestDrawText
#define DrawTextEx            TestDrawTextEx
#define BeginScissorMode      TestBeginScissorMode
#define EndScissorMode        TestEndScissorMode
#define GetMousePosition      TestMousePosition
#define GetMouseWheelMove     TestMouseWheelMove
#define IsMouseButtonPressed  TestMouseButton
#define IsKeyPressed          TestKey

#endif
