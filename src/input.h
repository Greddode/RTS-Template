// input.h - Unit selection and orders from the mouse.
#ifndef INPUT_H_INCLUDED
#define INPUT_H_INCLUDED

void InputUpdate(void);            // call once per frame, after CamUpdate()
void InputDraw(void);              // selection box + order markers; call inside BeginMode2D()
void InputDrawHud(void);           // selected building panel + messages; call after EndMode2D()

#endif
