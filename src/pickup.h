/*
 * pickup.h - Bonus laches par les gloutons : munitions et salade (soin).
 */
#ifndef PICKUP_H
#define PICKUP_H

#include "raylib.h"

void PickupsClear(void);
void PickupDrop(Vector3 pos);     // avec une certaine probabilite
void PickupsUpdate(float dt);     // ramassage par les joueurs
void PickupsDraw(void);

#endif
