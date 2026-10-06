// editor.h - In-game map editor.
#ifndef EDITOR_H_INCLUDED
#define EDITOR_H_INCLUDED

#include <stdbool.h>

typedef enum { EDITOR_STAY, EDITOR_EXIT, EDITOR_TEST_PLAY } EditorAction;

void         EditorOpenNew(int size);    // a blank all-grass map, size x size tiles
void         EditorOpenFromGame(void);   // copy the map being played (tiles, buildings, gold, units)
void         EditorResume(void);         // show the editor's map again (after Test Play)
EditorAction EditorFrame(void);          // input + drawing; call between BeginDrawing() and EndDrawing()
const char  *EditorTestPlayPath(void);   // the temporary file Test Play saved

#endif
