// transport.h - Units that carry units (cargoCapacity > 0 in UNIT_STATS: the Airship).
#ifndef TRANSPORT_H_INCLUDED
#define TRANSPORT_H_INCLUDED

#include "raylib.h"
#include "config.h"
#include <stdbool.h>

#define TRANSPORT_MAX_CARGO 32   // most units one transport can list at once (CargoList)

bool IsTransport(int id);                          // its type has cargoCapacity > 0
int  TransportUsedSlots(int transport);            // slots taken by the units inside
int  TransportFreeSlots(int transport);
int  TransportCargo(int transport, int *out, int max);   // the units inside (pool order), returns how many

// Orders (player input only: the AI doesn't use transports yet).
void TransportOrderBoard(const int *ids, int count, int transport);   // walk to it and get in (those that can be carried)
int  TransportLoadNearby(int transport);           // KEY_LOAD: idle units nearby board it, nearest first; returns how many were told
bool TransportOrderUnload(int transport, Vector2 at);   // fly to the nearest open ground at `at` and let everyone out
bool TransportUnloadOne(int transport, int cargo); // one unit out now, if the transport is over open ground

// Per tick (UnitsTick).
Vector2 TransportBoardTick(int id);   // a boarding unit: walk to the transport, get in when close
void    TransportCargoTick(int id);   // a loaded unit: ride along (and never outlive its transport)
void    TransportTick(int id);        // a transport: unloading in progress
void    TransportDestroyed(int id);   // UnitDespawn: drop the cargo on land, lose it over water / rock / lava

#endif
