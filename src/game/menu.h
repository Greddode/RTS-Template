// menu.h - Main menu, pause menu and the Controls page.
#ifndef MENU_H_INCLUDED
#define MENU_H_INCLUDED

#include <stdbool.h>

typedef enum { MENU_NONE, MENU_PLAY, MENU_RESUME, MENU_MAIN_MENU, MENU_EDITOR, MENU_EXIT } MenuAction;

void       MenuOpen(void);    // call when a menu appears: starts on its first page
MenuAction MenuMain(void);    // draw + handle the main menu; returns what the player chose
MenuAction MenuPause(void);   // same for the pause menu (Esc = resume)
MenuAction MenuGameOver(bool victory);   // Victory / Defeat: MENU_PLAY (play again) or MENU_MAIN_MENU
const char *MenuChosenMap(void);         // after MENU_PLAY from the main menu: map file path, or NULL for Random
void       MenuSetTestPlay(bool on);     // while test-playing an editor map, "Main Menu" reads "Back to Editor"

// The map list on its own (the editor's Load uses it). Call MenuOpenMapPicker()
// once, then MenuPickMap() every frame: it returns PICK_WAITING, PICK_CANCELLED,
// or the chosen map's index (see MapFilePath in mapfile.h).
#define PICK_WAITING   -2
#define PICK_CANCELLED -3
void       MenuOpenMapPicker(void);
int        MenuPickMap(void);

#endif
