// inspector.h - Bottom panel: what's selected, plus Train and Build buttons.
#ifndef INSPECTOR_H_INCLUDED
#define INSPECTOR_H_INCLUDED

void InspectorDraw(void);          // draw + handle buttons; does nothing when nothing is selected
void InspectorCheckHotkeys(void);  // once at startup: warn if two hotkeys collide

#endif
