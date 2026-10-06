// input.h - Selection, orders and building placement from mouse and keys.
#ifndef INPUT_H_INCLUDED
#define INPUT_H_INCLUDED

#include "config.h"
#include <stdbool.h>

void InputUpdate(void);            // call once per frame, after CamUpdate()
void InputDraw(void);              // selection box, markers, placement ghost; call inside BeginMode2D()
bool InputHasPendingCommand(void); // attack-move armed or placing a building: Esc cancels that first
void InputReset(void);             // forget selection and pending input (new game)

// What's selected (read by inspector.c). Everything is stored as (slot, serial),
// so dead units, destroyed buildings and empty nodes drop out by themselves.
bool InputHasSelection(void);                // cheap: no scanning
int  InputSelectedUnits(const int **ids);    // live selected units
int  InputSelectedBuilding(void);            // or -1
int  InputSelectedNode(void);                // or -1

void InputTogglePlacement(BuildingType type);   // Build button: start placing, or cancel if already placing it
bool InputIsPlacing(BuildingType type);

#endif
