// text_hooks_passthrough.c - For test programs that link the hooked ui.c (see
// text_hooks.h and CMakeLists.txt) but don't record anything: every hook just
// calls the real raylib function. (Compiled WITHOUT text_hooks.h.)
#include "raylib.h"

int     TestScreenWidth(void)  { return GetScreenWidth(); }
int     TestScreenHeight(void) { return GetScreenHeight(); }
void    TestDrawText(const char *text, int x, int y, int size, Color color) { DrawText(text, x, y, size, color); }
void    TestDrawTextEx(Font font, const char *text, Vector2 pos, float size, float spacing, Color color) { DrawTextEx(font, text, pos, size, spacing, color); }
void    TestBeginScissorMode(int x, int y, int width, int height) { BeginScissorMode(x, y, width, height); }
void    TestEndScissorMode(void) { EndScissorMode(); }
Vector2 TestMousePosition(void) { return GetMousePosition(); }
float   TestMouseWheelMove(void) { return GetMouseWheelMove(); }
bool    TestMouseButton(int button) { return IsMouseButtonPressed(button); }
bool    TestKey(int key) { return IsKeyPressed(key); }
