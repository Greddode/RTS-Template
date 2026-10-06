// input.h - Unit selection and orders from the mouse.
#ifndef INPUT_H_INCLUDED
#define INPUT_H_INCLUDED

void InputUpdate(void);            // call once per frame, after CamUpdate()
void InputDrawSelectionBox(void);  // call inside BeginMode2D()

#endif
