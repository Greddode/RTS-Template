// overlay.h - F3 debug overlay: FPS, frame and sim times, counts, AI state.
#ifndef OVERLAY_H_INCLUDED
#define OVERLAY_H_INCLUDED

#include <stdbool.h>

// Shown at startup? Hidden in release builds (CMake's Release defines NDEBUG), shown in debug builds.
#ifdef NDEBUG
    #define DEBUG_OVERLAY_DEFAULT 0
#else
    #define DEBUG_OVERLAY_DEFAULT 1
#endif

#define OVERLAY_FPS_SECONDS 5      // min / avg FPS over this many seconds
#define OVERLAY_REFRESH     0.25   // the unit / building counts are recounted this often (seconds)

void OverlayUpdate(void);   // once per frame: F3 toggles; gathers stats only while shown
void OverlayDrawFull(double tickMs);   // while playing: everything (does nothing when hidden)
void OverlayDrawFpsLine(void);         // menus, pause, editor: one FPS line (does nothing when hidden)

#endif
