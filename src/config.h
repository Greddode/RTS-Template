// config.h - Settings shared by several systems.
// Per-system settings (map size, unit speed, ...) live in that system's header.
#ifndef CONFIG_H_INCLUDED
#define CONFIG_H_INCLUDED

#define SCREEN_W 1280
#define SCREEN_H 720

// The simulation advances in fixed steps of TICK_DT seconds, independent of FPS.
#define TICK_RATE 30
#define TICK_DT   (1.0f / TICK_RATE)

#endif
